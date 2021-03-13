/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_html.h"

#include "export/output/export_output_result.h"
#include "export/data/export_data_types.h"
#include "core/utils.h"

#include <QtCore/QSize>
#include <QtCore/QFile>
#include <QtCore/QDateTime>

namespace Export {
namespace Output {
namespace {

constexpr auto kMessagesInFile = 1000;
constexpr auto kPersonalUserpicSize = 90;
constexpr auto kEntryUserpicSize = 48;
constexpr auto kServiceMessagePhotoSize = 60;
constexpr auto kHistoryUserpicSize = 42;
constexpr auto kSavedMessagesColorIndex = 3;
constexpr auto kJoinWithinSeconds = 900;
constexpr auto kPhotoMaxWidth = 520;
constexpr auto kPhotoMaxHeight = 520;
constexpr auto kPhotoMinWidth = 80;
constexpr auto kPhotoMinHeight = 80;
constexpr auto kStickerMaxWidth = 384;
constexpr auto kStickerMaxHeight = 384;
constexpr auto kStickerMinWidth = 80;
constexpr auto kStickerMinHeight = 80;

const auto kLineBreak = QByteArrayLiteral("<br>");

using Context = details::HtmlContext;
using UserpicData = details::UserpicData;
using PeersMap = details::PeersMap;
using MediaData = details::MediaData;

bool IsGlobalLink(const QString &link) {
	return link.startsWith(qstr("http://"), Qt::CaseInsensitive)
		|| link.startsWith(qstr("https://"), Qt::CaseInsensitive);
}

QByteArray NoFileDescription(Data::File::SkipReason reason) {
	using SkipReason = Data::File::SkipReason;
	switch (reason) {
	case SkipReason::Unavailable:
		return "Unavailable, please try again later.";
	case SkipReason::FileSize:
		return "Exceeds maximum size, "
			"change data exporting settings to download.";
	case SkipReason::FileType:
		return "Not included, "
			"change data exporting settings to download.";
	case SkipReason::None:
		return "";
	}
	Unexpected("Skip reason in NoFileDescription.");
}

auto CalculateThumbSize(
		int maxWidth,
		int maxHeight,
		int minWidth,
		int minHeight,
		bool expandForRetina = false) {
	return [=](QSize largeSize) {
		const auto multiplier = (expandForRetina ? 2 : 1);
		const auto checkWidth = largeSize.width() * multiplier;
		const auto checkHeight = largeSize.height() * multiplier;
		const auto smallSize = (checkWidth > maxWidth
			|| checkHeight > maxHeight)
			? largeSize.scaled(
				maxWidth,
				maxHeight,
				Qt::KeepAspectRatio)
			: largeSize;
		const auto retinaSize = QSize(
			smallSize.width() & ~0x01,
			smallSize.height() & ~0x01);
		return (retinaSize.width() < kPhotoMinWidth
			|| retinaSize.height() < kPhotoMinHeight)
			? QSize()
			: retinaSize;
	};
}

QByteArray SerializeString(const QByteArray &value) {
	const auto size = value.size();
	const auto begin = value.data();
	const auto end = begin + size;

	auto result = QByteArray();
	result.reserve(size * 6);
	for (auto p = begin; p != end; ++p) {
		const auto ch = *p;
		if (ch == '\n') {
			result.append("<br>", 4);
		} else if (ch == '"') {
			result.append("&quot;", 6);
		} else if (ch == '&') {
			result.append("&amp;", 5);
		} else if (ch == '\'') {
			result.append("&apos;", 6);
		} else if (ch == '<') {
			result.append("&lt;", 4);
		} else if (ch == '>') {
			result.append("&gt;", 4);
		} else if (ch >= 0 && ch < 32) {
			result.append("&#x", 3).append('0' + (ch >> 4));
			const auto left = (ch & 0x0F);
			if (left >= 10) {
				result.append('A' + (left - 10));
			} else {
				result.append('0' + left);
			}
			result.append(';');
		} else if (ch == char(0xE2)
			&& (p + 2 < end)
			&& *(p + 1) == char(0x80)) {
			if (*(p + 2) == char(0xA8)) { // Line separator.
				result.append("<br>", 4);
			} else if (*(p + 2) == char(0xA9)) { // Paragraph separator.
				result.append("<br>", 4);
			} else {
				result.append(ch);
			}
		} else {
			result.append(ch);
		}
	}
	return result;
}

QByteArray SerializeList(const std::vector<QByteArray> &values) {
	const auto count = values.size();
	if (count == 1) {
		return values[0];
	} else if (count > 1) {
		auto result = values[0];
		for (auto i = 1; i != count - 1; ++i) {
			result += ", " + values[i];
		}
		return result + " and " + values[count - 1];
	}
	return QByteArray();
}

QByteArray MakeLinks(const QByteArray &value) {
	const auto domain = QByteArray("https://telegram.org/");
	auto result = QByteArray();
	auto offset = 0;
	while (true) {
		const auto start = value.indexOf(domain, offset);
		if (start < 0) {
			break;
		}
		auto end = start + domain.size();
		for (; end != value.size(); ++end) {
			const auto ch = value[end];
			if ((ch < 'a' || ch > 'z')
				&& (ch < 'A' || ch > 'Z')
				&& (ch < '0' || ch > '9')
				&& (ch != '-')
				&& (ch != '_')
				&& (ch != '/')) {
				break;
			}
		}
		if (start > offset) {
			const auto link = value.mid(start, end - start);
			result.append(value.mid(offset, start - offset));
			result.append("<a href=\"").append(link).append("\">");
			result.append(link);
			result.append("</a>");
			offset = end;
		}
	}
	if (result.isEmpty()) {
		return value;
	}
	if (offset < value.size()) {
		result.append(value.mid(offset));
	}
	return result;
}

QByteArray JoinList(
		const QByteArray &separator,
		const std::vector<QByteArray> &list) {
	if (list.empty()) {
		return QByteArray();
	} else if (list.size() == 1) {
		return list[0];
	}
	auto size = (list.size() - 1) * separator.size();
	for (const auto &value : list) {
		size += value.size();
	}
	auto result = QByteArray();
	result.reserve(size);
	auto counter = 0;
	while (true) {
		result.append(list[counter]);
		if (++counter == list.size()) {
			break;
		} else {
			result.append(separator);
		}
	}
	return result;
}

QByteArray FormatText(
		const std::vector<Data::TextPart> &data,
		const QString &internalLinksDomain) {
	return JoinList(QByteArray(), ranges::views::all(
		data
	) | ranges::views::transform([&](const Data::TextPart &part) {
		const auto text = SerializeString(part.text);
		using Type = Data::TextPart::Type;
		switch (part.type) {
		case Type::Text: return text;
		case Type::Unknown: return text;
		case Type::Mention:
			return "<a href=\""
				+ internalLinksDomain.toUtf8()
				+ text.mid(1)
				+ "\">" + text + "</a>";
		case Type::Hashtag: return "<a href=\"\" "
			"onclick=\"return ShowHashtag("
			+ SerializeString('"' + text.mid(1) + '"')
			+ ")\">" + text + "</a>";
		case Type::BotCommand: return "<a href=\"\" "
			"onclick=\"return ShowBotCommand("
			+ SerializeString('"' + text.mid(1) + '"')
			+ ")\">" + text + "</a>";
		case Type::Url: return "<a href=\""
			+ text
			+ "\">" + text + "</a>";
		case Type::Email: return "<a href=\"mailto:"
			+ text
			+ "\">" + text + "</a>";
		case Type::Bold: return "<strong>" + text + "</strong>";
		case Type::Italic: return "<em>" + text + "</em>";
		case Type::Code: return "<code>" + text + "</code>";
		case Type::Pre: return "<pre>" + text + "</pre>";
		case Type::TextUrl: return "<a href=\""
			+ SerializeString(part.additional)
			+ "\">" + text + "</a>";
		case Type::MentionName: return "<a href=\"\" "
			"onclick=\"return ShowMentionName()\">" + text + "</a>";
		case Type::Phone: return "<a href=\"tel:"
			+ text
			+ "\">" + text + "</a>";
		case Type::Cashtag: return "<a href=\"\" "
			"onclick=\"return ShowCashtag("
			+ SerializeString('"' + text.mid(1) + '"')
			+ ")\">" + text + "</a>";
		case Type::Underline: return "<u>" + text + "</u>";
		case Type::Strike: return "<s>" + text + "</s>";
		case Type::Blockquote:
			return "<blockquote>" + text + "</blockquote>";
		case Type::BankCard:
			return text;
		}
		Unexpected("Type in text entities serialization.");
	}) | ranges::to_vector);
}

Data::Utf8String FormatUsername(const Data::Utf8String &username) {
	return username.isEmpty() ? username : ('@' + username);
}

bool DisplayDate(TimeId date, TimeId previousDate) {
	if (!previousDate) {
		return true;
	}
	return QDateTime::fromTime_t(date).date()
		!= QDateTime::fromTime_t(previousDate).date();
}

QByteArray FormatDateText(TimeId date) {
	const auto parsed = QDateTime::fromTime_t(date).date();
	const auto month = [](int index) {
		switch (index) {
		case 1: return "January";
		case 2: return "February";
		case 3: return "March";
		case 4: return "April";
		case 5: return "May";
		case 6: return "June";
		case 7: return "July";
		case 8: return "August";
		case 9: return "September";
		case 10: return "October";
		case 11: return "November";
		case 12: return "December";
		}
		return "Unknown";
	};
	return Data::NumberToString(parsed.day())
		+ ' '
		+ month(parsed.month())
		+ ' '
		+ Data::NumberToString(parsed.year());
}

QByteArray FormatTimeText(TimeId date) {
	const auto parsed = QDateTime::fromTime_t(date).time();
	return Data::NumberToString(parsed.hour(), 2)
		+ ':'
		+ Data::NumberToString(parsed.minute(), 2);
}

QByteArray SerializeLink(
		const Data::Utf8String &text,
		const QString &path) {
	return "<a href=\"" + path.toUtf8() + "\">" + text + "</a>";
}

} // namespace

namespace details {

struct UserpicData {
	int colorIndex = 0;
	int pixelSize = 0;
	QString imageLink;
	QString largeLink;
	QByteArray firstName;
	QByteArray lastName;
};

class PeersMap {
public:
	using PeerId = Data::PeerId;
	using Peer = Data::Peer;
	using User = Data::User;
	using Chat = Data::Chat;

	PeersMap(const std::map<PeerId, Peer> &data);

	const Peer &peer(PeerId peerId) const;
	const User &user(int32 userId) const;
	const Chat &chat(int32 chatId) const;

	QByteArray wrapPeerName(PeerId peerId) const;
	QByteArray wrapUserName(int32 userId) const;
	QByteArray wrapUserNames(const std::vector<int32> &data) const;

private:
	const std::map<Data::PeerId, Data::Peer> &_data;

};

struct MediaData {
	QByteArray title;
	QByteArray description;
	QByteArray status;
	QByteArray classes;
	QString thumb;
	QString link;
};

PeersMap::PeersMap(const std::map<PeerId, Peer> &data) : _data(data) {
}

auto PeersMap::peer(PeerId peerId) const -> const Peer & {
	if (const auto i = _data.find(peerId); i != end(_data)) {
		return i->second;
	}
	static auto empty = Peer{ User() };
	return empty;
}

auto PeersMap::user(int32 userId) const -> const User & {
	if (const auto result = peer(Data::UserPeerId(userId)).user()) {
		return *result;
	}
	static auto empty = User();
	return empty;
}

auto PeersMap::chat(int32 chatId) const -> const Chat & {
	if (const auto result = peer(Data::ChatPeerId(chatId)).chat()) {
		return *result;
	}
	static auto empty = Chat();
	return empty;
}

QByteArray PeersMap::wrapPeerName(PeerId peerId) const {
	const auto result = peer(peerId).name();
	return result.isEmpty()
		? QByteArray("Deleted")
		: SerializeString(result);
}

QByteArray PeersMap::wrapUserName(int32 userId) const {
	const auto result = user(userId).name();
	return result.isEmpty()
		? QByteArray("Deleted Account")
		: SerializeString(result);
}

QByteArray PeersMap::wrapUserNames(const std::vector<int32> &data) const {
	auto list = std::vector<QByteArray>();
	for (const auto userId : data) {
		list.push_back(wrapUserName(userId));
	}
	return SerializeList(list);
}

QByteArray HtmlContext::pushTag(
		const QByteArray &tag,
		std::map<QByteArray, QByteArray> &&attributes) {
	auto data = Tag();
	data.name = tag;
	auto empty = false;
	auto inner = QByteArray();
	for (const auto &[name, value] : attributes) {
		if (name == "inline") {
			data.block = false;
		} else if (name == "empty") {
			empty = true;
		} else {
			inner.append(' ').append(name);
			inner.append("=\"").append(SerializeString(value)).append("\"");
		}
	}
	auto result = (data.block ? ("\n" + indent()) : QByteArray())
		+ "<" + data.name + inner + (empty ? "/" : "") + ">"
		+ (data.block ? "\n" : "");
	if (!empty) {
		_tags.push_back(data);
	}
	return result;
}

QByteArray HtmlContext::popTag() {
	Expects(!_tags.empty());

	const auto data = _tags.back();
	_tags.pop_back();
	return (data.block ? ("\n" + indent()) : QByteArray())
		+ "</" + data.name + ">"
		+ (data.block ? "\n" : "");
}

QByteArray HtmlContext::indent() const {
	return QByteArray(_tags.size(), ' ');
}

bool HtmlContext::empty() const {
	return _tags.empty();
}

} // namespace details

struct HtmlWriter::MessageInfo {
	enum class Type {
		Service,
		Default,
	};
	int32 id = 0;
	Type type = Type::Service;
	Data::PeerId fromId = 0;
	int32 viaBotId = 0;
	TimeId date = 0;
	Data::PeerId forwardedFromId = 0;
	QString forwardedFromName;
	bool forwarded = false;
	bool showForwardedAsOriginal = false;
	TimeId forwardedDate = 0;
};

class HtmlWriter::Wrap {
public:
	Wrap(const QString &path, const QString &base, Stats *stats);

	[[nodiscard]] bool empty() const;

	[[nodiscard]] QByteArray pushTag(
		const QByteArray &tag,
		std::map<QByteArray, QByteArray> &&attributes = {});
	[[nodiscard]] QByteArray popTag();
	[[nodiscard]] QByteArray indent() const;

	[[nodiscard]] QByteArray pushDiv(
		const QByteArray &className,
		const QByteArray &style = {});

	[[nodiscard]] QByteArray pushUserpic(const UserpicData &userpic);
	[[nodiscard]] QByteArray pushListEntry(
		const UserpicData &userpic,
		const QByteArray &name,
		const QByteArray &details,
		const QByteArray &info,
		const QString &link = QString());
	[[nodiscard]] QByteArray pushSessionListEntry(
		int apiId,
		const QByteArray &name,
		const QByteArray &subname,
		std::initializer_list<QByteArray> details,
		const QByteArray &info = QByteArray());

	[[nodiscard]] QByteArray pushHeader(
		const QByteArray &header,
		const QString &path = QString());
	[[nodiscard]] QByteArray pushSection(
		const QByteArray &label,
		const QByteArray &type,
		int count,
		const QString &path);
	[[nodiscard]] QByteArray pushAbout(
		const QByteArray &text,
		bool withDivider = false);
	[[nodiscard]] QByteArray pushServiceMessage(
		int messageId,
		const Data::DialogInfo &dialog,
		const QString &basePath,
		const QByteArray &text,
		const Data::Photo *photo = nullptr);
	[[nodiscard]] std::pair<MessageInfo, QByteArray> pushMessage(
		const Data::Message &message,
		const MessageInfo *previous,
		const Data::DialogInfo &dialog,
		const QString &basePath,
		const PeersMap &peers,
		const QString &internalLinksDomain,
		Fn<QByteArray(int messageId, QByteArray text)> wrapMessageLink);

	[[nodiscard]] Result writeBlock(const QByteArray &block);

	[[nodiscard]] Result close();

	[[nodiscard]] QString relativePath(const QString &path) const;
	[[nodiscard]] QString relativePath(const Data::File &file) const;

	~Wrap();

private:
	[[nodiscard]] QByteArray composeStart();
	[[nodiscard]] QByteArray pushGenericListEntry(
		const QString &link,
		const UserpicData &userpic,
		const QByteArray &name,
		const QByteArray &subname,
		std::initializer_list<QByteArray> details,
		const QByteArray &info);

	[[nodiscard]] bool messageNeedsWrap(
		const Data::Message &message,
		const MessageInfo *previous) const;
	[[nodiscard]] bool forwardedNeedsWrap(
		const Data::Message &message,
		const MessageInfo *previous) const;

	[[nodiscard]] MediaData prepareMediaData(
		const Data::Message &message,
		const QString &basePath,
		const PeersMap &peers,
		const QString &internalLinksDomain) const;
	[[nodiscard]] QByteArray pushMedia(
		const Data::Message &message,
		const QString &basePath,
		const PeersMap &peers,
		const QString &internalLinksDomain);
	[[nodiscard]] QByteArray pushGenericMedia(const MediaData &data);
	[[nodiscard]] QByteArray pushStickerMedia(
		const Data::Document &data,
		const QString &basePath);
	[[nodiscard]] QByteArray pushAnimatedMedia(
		const Data::Document &data,
		const QString &basePath);
	[[nodiscard]] QByteArray pushVideoFileMedia(
		const Data::Document &data,
		const QString &basePath);
	[[nodiscard]] QByteArray pushPhotoMedia(
		const Data::Photo &data,
		const QString &basePath);
	[[nodiscard]] QByteArray pushPoll(const Data::Poll &data);

	File _file;
	QByteArray _composedStart;
	bool _closed = false;
	QByteArray _base;
	Context _context;

};

struct HtmlWriter::SavedSection {
	int priority = 0;
	QByteArray label;
	QByteArray type;
	int count = 0;
	QString path;
};

void FillUserpicNames(UserpicData &data, const Data::Peer &peer) {
	if (peer.user()) {
		data.firstName = peer.user()->info.firstName;
		data.lastName = peer.user()->info.lastName;
	} else if (peer.chat()) {
		data.firstName = peer.name();
	}
}

void FillUserpicNames(UserpicData &data, const QByteArray &full) {
	const auto names = full.split(' ');
	data.firstName = names[0];
	for (auto i = 1; i != names.size(); ++i) {
		if (names[i].isEmpty()) {
			continue;
		}
		if (!data.lastName.isEmpty()) {
			data.lastName.append(' ');
		}
		data.lastName.append(names[i]);
	}
}

QByteArray ComposeName(const UserpicData &data, const QByteArray &empty) {
	return ((data.firstName.isEmpty() && data.lastName.isEmpty())
		? empty
		: (data.firstName + ' ' + data.lastName));
}

QString WriteUserpicThumb(
		const QString &basePath,
		const QString &largePath,
		const UserpicData &userpic,
		const QString &postfix = "_thumb") {
	return Data::WriteImageThumb(
		basePath,
		largePath,
		userpic.pixelSize * 2,
		userpic.pixelSize * 2,
		postfix);
}

HtmlWriter::Wrap::Wrap(
	const QString &path,
	const QString &base,
	Stats *stats)
: _file(path, stats) {
	Expects(base.endsWith('/'));
	Expects(path.startsWith(base));

	const auto left = path.mid(base.size());
	const auto nesting = ranges::count(left, '/');
	_base = QString("../").repeated(nesting).toUtf8();

	_composedStart = composeStart();
}

bool HtmlWriter::Wrap::empty() const {
	return _file.empty();
}

QByteArray HtmlWriter::Wrap::pushTag(
		const QByteArray &tag,
		std::map<QByteArray, QByteArray> &&attributes) {
	return _context.pushTag(tag, std::move(attributes));
}

QByteArray HtmlWriter::Wrap::popTag() {
	return _context.popTag();
}

QByteArray HtmlWriter::Wrap::indent() const {
	return _context.indent();
}

QByteArray HtmlWriter::Wrap::pushDiv(
		const QByteArray &className,
		const QByteArray &style) {
	return style.isEmpty()
		? _context.pushTag("div", { { "class", className } })
		: _context.pushTag("div", {
			{ "class", className },
			{ "style", style }
		});
}

QByteArray HtmlWriter::Wrap::pushUserpic(const UserpicData &userpic) {
	const auto size = Data::NumberToString(userpic.pixelSize) + "px";
	auto result = QByteArray();
	if (!userpic.largeLink.isEmpty()) {
		result.append(pushTag("a", {
			{ "class", "userpic_link" },
			{ "href", relativePath(userpic.largeLink).toUtf8() }
		}));
	}
	const auto sizeStyle = "width: " + size + "; height: " + size;
	if (!userpic.imageLink.isEmpty()) {
		result.append(pushTag("img", {
			{ "class", "userpic" },
			{ "style", sizeStyle },
			{ "src", relativePath(userpic.imageLink).toUtf8() },
			{ "empty", "" }
		}));
	} else {
		result.append(pushTag("div", {
			{
				"class",
				"userpic userpic"
				+ Data::NumberToString(userpic.colorIndex + 1)
			},
			{ "style", sizeStyle }
		}));
		result.append(pushDiv(
			"initials",
			"line-height: " + size));
		auto character = [](const QByteArray &from) {
			const auto utf = QString::fromUtf8(from).trimmed();
			return utf.isEmpty()
				? QByteArray()
				: SerializeString(utf.mid(0, 1).toUtf8());
		};
		result.append(character(userpic.firstName));
		result.append(character(userpic.lastName));
		result.append(popTag());
		result.append(popTag());
	}
	if (!userpic.largeLink.isEmpty()) {
		result.append(popTag());
	}
	return result;
}

QByteArray HtmlWriter::Wrap::pushListEntry(
		const UserpicData &userpic,
		const QByteArray &name,
		const QByteArray &details,
		const QByteArray &info,
		const QString &link) {
	return pushGenericListEntry(
		link,
		userpic,
		name,
		{},
		{ details },
		info);
}

QByteArray HtmlWriter::Wrap::pushSessionListEntry(
		int apiId,
		const QByteArray &name,
		const QByteArray &subname,
		std::initializer_list<QByteArray> details,
		const QByteArray &info) {
	const auto link = QString();
	auto userpic = UserpicData{
		Data::ApplicationColorIndex(apiId),
		kEntryUserpicSize
	};
	userpic.firstName = name;
	return pushGenericListEntry(
		link,
		userpic,
		name,
		subname,
		details,
		info);
}

QByteArray HtmlWriter::Wrap::pushGenericListEntry(
		const QString &link,
		const UserpicData &userpic,
		const QByteArray &name,
		const QByteArray &subname,
		std::initializer_list<QByteArray> details,
		const QByteArray &info) {
	auto result = link.isEmpty()
		? pushDiv("entry clearfix")
		: pushTag("a", {
			{ "class", "entry block_link clearfix" },
			{ "href", relativePath(link).toUtf8() + "#allow_back" },
		});
	result.append(pushDiv("pull_left userpic_wrap"));
	result.append(pushUserpic(userpic));
	result.append(popTag());
	result.append(pushDiv("body"));
	if (!info.isEmpty()) {
		result.append(pushDiv("pull_right info details"));
		result.append(SerializeString(info));
		result.append(popTag());
	}
	if (!name.isEmpty()) {
		result.append(pushDiv("name bold"));
		result.append(SerializeString(name));
		result.append(popTag());
	}
	if (!subname.isEmpty()) {
		result.append(pushDiv("subname bold"));
		result.append(SerializeString(subname));
		result.append(popTag());
	}
	for (const auto &detail : details) {
		result.append(pushDiv("details_entry details"));
		result.append(SerializeString(detail));
		result.append(popTag());
	}
	result.append(popTag());
	result.append(popTag());
	return result;
}

Result HtmlWriter::Wrap::writeBlock(const QByteArray &block) {
	Expects(!_closed);

	const auto result = [&] {
		if (block.isEmpty()) {
			return _file.writeBlock(block);
		} else if (_file.empty()) {
			return _file.writeBlock(_composedStart + block);
		}
		return _file.writeBlock(block);
	}();
	if (!result) {
		_closed = true;
	}
	return result;
}

QByteArray HtmlWriter::Wrap::pushHeader(
		const QByteArray &header,
		const QString &path) {
	auto result = pushDiv("page_header");
	result.append(path.isEmpty()
		? pushDiv("content")
		: pushTag("a", {
			{ "class", "content block_link" },
			{ "href", relativePath(path).toUtf8() },
			{ "onclick", "return GoBack(this)"},
		}));
	result.append(pushDiv("text bold"));
	result.append(SerializeString(header));
	result.append(popTag());
	result.append(popTag());
	result.append(popTag());
	return result;
}

QByteArray HtmlWriter::Wrap::pushSection(
		const QByteArray &header,
		const QByteArray &type,
		int count,
		const QString &link) {
	auto result = pushTag("a", {
		{ "class", "section block_link " + type },
		{ "href", link.toUtf8() + "#allow_back" },
	});
	result.append(pushDiv("counter details"));
	result.append(Data::NumberToString(count));
	result.append(popTag());
	result.append(pushDiv("label bold"));
	result.append(SerializeString(header));
	result.append(popTag());
	result.append(popTag());
	return result;
}

QByteArray HtmlWriter::Wrap::pushAbout(
		const QByteArray &text,
		bool withDivider) {
	auto result = pushDiv(withDivider
		? "page_about details with_divider"
		: "page_about details");
	result.append(MakeLinks(SerializeString(text)));
	result.append(popTag());
	return result;
}

QByteArray HtmlWriter::Wrap::pushServiceMessage(
		int messageId,
		const Data::DialogInfo &dialog,
		const QString &basePath,
		const QByteArray &serialized,
		const Data::Photo *photo) {
	auto result = pushTag("div", {
		{ "class", "message service" },
		{ "id", "message" + Data::NumberToString(messageId) }
	});
	result.append(pushDiv("body details"));
	result.append(serialized);
	result.append(popTag());
	if (photo) {
		auto userpic = UserpicData();
		userpic.colorIndex = Data::PeerColorIndex(
			Data::BarePeerId(dialog.peerId));
		userpic.firstName = dialog.name;
		userpic.lastName = dialog.lastName;
		userpic.pixelSize = kServiceMessagePhotoSize;
		userpic.largeLink = photo->image.file.relativePath;
		userpic.imageLink = WriteUserpicThumb(
			basePath,
			userpic.largeLink,
			userpic);
		result.append(pushDiv("userpic_wrap"));
		result.append(pushUserpic(userpic));
		result.append(popTag());
	}
	result.append(popTag());
	return result;
}

auto HtmlWriter::Wrap::pushMessage(
	const Data::Message &message,
	const MessageInfo *previous,
	const Data::DialogInfo &dialog,
	const QString &basePath,
	const PeersMap &peers,
	const QString &internalLinksDomain,
	Fn<QByteArray(int messageId, QByteArray text)> wrapMessageLink
) -> std::pair<MessageInfo, QByteArray> {
	using namespace Data;

	auto info = MessageInfo();
	info.id = message.id;
	info.fromId = message.fromId;
	info.viaBotId = message.viaBotId;
	info.date = message.date;
	info.forwardedFromId = message.forwardedFromId;
	info.forwardedFromName = message.forwardedFromName;
	info.forwardedDate = message.forwardedDate;
	info.forwarded = message.forwarded;
	info.showForwardedAsOriginal = message.showForwardedAsOriginal;
	if (v::is<UnsupportedMedia>(message.media.content)) {
		return { info, pushServiceMessage(
			message.id,
			dialog,
			basePath,
			"This message is not supported by this version "
			"of Telegram Desktop. Please update the application.") };
	}

	const auto wrapReplyToLink = [&](const QByteArray &text) {
		return wrapMessageLink(message.replyToMsgId, text);
	};

	using DialogType = Data::DialogInfo::Type;
	const auto isChannel = (dialog.type == DialogType::PrivateChannel)
		|| (dialog.type == DialogType::PublicChannel);
	const auto serviceFrom = peers.wrapPeerName(message.fromId);
	const auto serviceText = v::match(message.action.content, [&](
			const ActionChatCreate &data) {
		return serviceFrom
			+ " created group &laquo;"
			+ SerializeString(data.title)
			+ "&raquo;"
			+ (data.userIds.empty()
				? QByteArray()
				: " with members " + peers.wrapUserNames(data.userIds));
	}, [&](const ActionChatEditTitle &data) {
		return isChannel
			? ("Channel title changed to &laquo;"
				+ SerializeString(data.title)
				+ "&raquo;")
			: (serviceFrom
				+ " changed group title to &laquo;"
				+ SerializeString(data.title)
				+ "&raquo;");
	}, [&](const ActionChatEditPhoto &data) {
		return isChannel
			? QByteArray("Channel photo changed")
			: (serviceFrom + " changed group photo");
	}, [&](const ActionChatDeletePhoto &data) {
		return isChannel
			? QByteArray("Channel photo removed")
			: (serviceFrom + " removed group photo");
	}, [&](const ActionChatAddUser &data) {
		return serviceFrom
			+ " invited "
			+ peers.wrapUserNames(data.userIds);
	}, [&](const ActionChatDeleteUser &data) {
		return serviceFrom
			+ " removed "
			+ peers.wrapUserName(data.userId);
	}, [&](const ActionChatJoinedByLink &data) {
		return serviceFrom
			+ " joined group by link from "
			+ peers.wrapUserName(data.inviterId);
	}, [&](const ActionChannelCreate &data) {
		return "Channel &laquo;"
			+ SerializeString(data.title)
			+ "&raquo; created";
	}, [&](const ActionChatMigrateTo &data) {
		return serviceFrom
			+ " converted this group to a supergroup";
	}, [&](const ActionChannelMigrateFrom &data) {
		return serviceFrom
			+ " converted a basic group to this supergroup "
			+ "&laquo;" + SerializeString(data.title) + "&raquo;";
	}, [&](const ActionPinMessage &data) {
		return serviceFrom
			+ " pinned "
			+ wrapReplyToLink("this message");
	}, [&](const ActionHistoryClear &data) {
		return QByteArray("History cleared");
	}, [&](const ActionGameScore &data) {
		return serviceFrom
			+ " scored "
			+ NumberToString(data.score)
			+ " in "
			+ wrapReplyToLink("this game");
	}, [&](const ActionPaymentSent &data) {
		return "You have successfully transferred "
			+ FormatMoneyAmount(data.amount, data.currency)
			+ " for "
			+ wrapReplyToLink("this invoice");
	}, [&](const ActionPhoneCall &data) {
		return QByteArray();
	}, [&](const ActionScreenshotTaken &data) {
		return serviceFrom + " took a screenshot";
	}, [&](const ActionCustomAction &data) {
		return data.message;
	}, [&](const ActionBotAllowed &data) {
		return "You allowed this bot to message you when you logged in on "
			+ SerializeString(data.domain);
	}, [&](const ActionSecureValuesSent &data) {
		auto list = std::vector<QByteArray>();
		for (const auto type : data.types) {
			list.push_back([&] {
				using Type = ActionSecureValuesSent::Type;
				switch (type) {
				case Type::PersonalDetails: return "Personal details";
				case Type::Passport: return "Passport";
				case Type::DriverLicense: return "Driver license";
				case Type::IdentityCard: return "Identity card";
				case Type::InternalPassport: return "Internal passport";
				case Type::Address: return "Address information";
				case Type::UtilityBill: return "Utility bill";
				case Type::BankStatement: return "Bank statement";
				case Type::RentalAgreement: return "Rental agreement";
				case Type::PassportRegistration:
					return "Passport registration";
				case Type::TemporaryRegistration:
					return "Temporary registration";
				case Type::Phone: return "Phone number";
				case Type::Email: return "Email";
				}
				return "";
			}());
		}
		return "You have sent the following documents: "
			+ SerializeList(list);
	}, [&](const ActionContactSignUp &data) {
		return serviceFrom + " joined Telegram";
	}, [&](const ActionGeoProximityReached &data) {
		const auto fromName = peers.wrapPeerName(data.fromId);
		const auto toName = peers.wrapPeerName(data.toId);
		const auto distance = [&]() -> QString {
			if (data.distance >= 1000) {
				const auto km = (10 * (data.distance / 10)) / 1000.;
				return QString::number(km) + " km";
			} else if (data.distance == 1) {
				return "1 meter";
			} else {
				return QString::number(data.distance) + " meters";
			}
		}().toUtf8();
		if (data.fromSelf) {
			return "You are now within " + distance + " from " + toName;
		} else if (data.toSelf) {
			return fromName + " is now within " + distance + " from you";
		} else {
			return fromName
				+ " is now within "
				+ distance
				+ " from "
				+ toName;
		}
	}, [&](const ActionPhoneNumberRequest &data) {
		return serviceFrom + " requested your phone number";
	}, [&](const ActionGroupCall &data) {
		return "Group call"
			+ (data.duration
				? (" (" + QString::number(data.duration) + " seconds)")
				: QString()).toUtf8();
	}, [&](const ActionInviteToGroupCall &data) {
		return serviceFrom
			+ " invited "
			+ peers.wrapUserNames(data.userIds)
			+ " to the voice chat";
	}, [](v::null_t) { return QByteArray(); });

	if (!serviceText.isEmpty()) {
		const auto &content = message.action.content;
		const auto photo = v::is<ActionChatEditPhoto>(content)
			? &v::get<ActionChatEditPhoto>(content).photo
			: nullptr;
		return { info, pushServiceMessage(
			message.id,
			dialog,
			basePath,
			serviceText,
			photo) };
	}
	info.type = MessageInfo::Type::Default;

	const auto wrap = messageNeedsWrap(message, previous);
	const auto fromPeerId = message.fromId;
	const auto showForwardedInfo = message.forwarded
		&& !message.showForwardedAsOriginal;
	auto forwardedUserpic = UserpicData();
	if (message.forwarded) {
		forwardedUserpic.colorIndex = message.forwardedFromId
			? PeerColorIndex(BarePeerId(message.forwardedFromId))
			: PeerColorIndex(message.id);
		forwardedUserpic.pixelSize = kHistoryUserpicSize;
		if (message.forwardedFromId) {
			FillUserpicNames(
				forwardedUserpic,
				peers.peer(message.forwardedFromId));
		} else {
			FillUserpicNames(forwardedUserpic, message.forwardedFromName);
		}
	}
	auto userpic = UserpicData();
	if (message.showForwardedAsOriginal) {
		userpic = forwardedUserpic;
	} else {
		userpic.colorIndex = PeerColorIndex(BarePeerId(fromPeerId));
		userpic.pixelSize = kHistoryUserpicSize;
		FillUserpicNames(userpic, peers.peer(fromPeerId));
	}

	const auto via = [&] {
		if (message.viaBotId) {
			const auto &user = peers.user(message.viaBotId);
			if (!user.username.isEmpty()) {
				return SerializeString(user.username);
			}
		}
		return QByteArray();
	}();

	const auto className = wrap
		? "message default clearfix"
		: "message default clearfix joined";
	auto block = pushTag("div", {
		{ "class", className },
		{ "id", "message" + NumberToString(message.id) }
	});
	if (wrap) {
		block.append(pushDiv("pull_left userpic_wrap"));
		block.append(pushUserpic(userpic));
		block.append(popTag());
	}
	block.append(pushDiv("body"));
	block.append(pushTag("div", {
		{ "class", "pull_right date details" },
		{ "title", FormatDateTime(message.date) },
	}));
	block.append(FormatTimeText(message.date));
	block.append(popTag());
	if (wrap) {
		block.append(pushDiv("from_name"));
		block.append(SerializeString(
			ComposeName(userpic, "Deleted Account")));
		if (!via.isEmpty()
			&& (!message.forwarded || message.showForwardedAsOriginal)) {
			block.append(" via @" + via);
		}
		block.append(popTag());
	}
	if (showForwardedInfo) {
		const auto forwardedWrap = forwardedNeedsWrap(message, previous);
		if (forwardedWrap) {
			block.append(pushDiv("pull_left forwarded userpic_wrap"));
			block.append(pushUserpic(forwardedUserpic));
			block.append(popTag());
		}
		block.append(pushDiv("forwarded body"));
		if (forwardedWrap) {
			block.append(pushDiv("from_name"));
			block.append(SerializeString(
				ComposeName(forwardedUserpic, "Deleted Account")));
			if (!via.isEmpty()) {
				block.append(" via @" + via);
			}
			block.append(pushTag("span", {
				{ "class", "details" },
				{ "inline", "" }
			}));
			block.append(' ' + FormatDateTime(message.forwardedDate));
			block.append(popTag());
			block.append(popTag());
		}
	}
	if (message.replyToMsgId) {
		block.append(pushDiv("reply_to details"));
		if (message.replyToPeerId) {
			block.append("In reply to a message in another chat");
		} else {
			block.append("In reply to ");
			block.append(wrapReplyToLink("this message"));
		}
		block.append(popTag());
	}

	block.append(pushMedia(message, basePath, peers, internalLinksDomain));

	const auto text = FormatText(message.text, internalLinksDomain);
	if (!text.isEmpty()) {
		block.append(pushDiv("text"));
		block.append(text);
		block.append(popTag());
	}
	if (!message.signature.isEmpty()) {
		block.append(pushDiv("signature details"));
		block.append(SerializeString(message.signature));
		block.append(popTag());
	}
	if (showForwardedInfo) {
		block.append(popTag());
	}
	block.append(popTag());
	block.append(popTag());

	return { info, block };
}

bool HtmlWriter::Wrap::messageNeedsWrap(
		const Data::Message &message,
		const MessageInfo *previous) const {
	if (!previous) {
		return true;
	} else if (previous->type != MessageInfo::Type::Default) {
		return true;
	} else if (!message.fromId || previous->fromId != message.fromId) {
		return true;
	} else if (message.viaBotId != previous->viaBotId) {
		return true;
	} else if (QDateTime::fromTime_t(previous->date).date()
		!= QDateTime::fromTime_t(message.date).date()) {
		return true;
	} else if (message.forwarded != previous->forwarded
		|| message.showForwardedAsOriginal != previous->showForwardedAsOriginal
		|| message.forwardedFromId != previous->forwardedFromId
		|| message.forwardedFromName != previous->forwardedFromName) {
		return true;
	} else if (std::abs(message.date - previous->date)
		> ((message.forwardedFromId || !message.forwardedFromName.isEmpty())
			? 1
			: kJoinWithinSeconds)) {
		return true;
	}
	return false;
}

QByteArray HtmlWriter::Wrap::pushMedia(
		const Data::Message &message,
		const QString &basePath,
		const PeersMap &peers,
		const QString &internalLinksDomain) {
	const auto data = prepareMediaData(
		message,
		basePath,
		peers,
		internalLinksDomain);
	if (!data.classes.isEmpty()) {
		return pushGenericMedia(data);
	}
	const auto &content = message.media.content;
	if (const auto document = std::get_if<Data::Document>(&content)) {
		Assert(!message.media.ttl);
		if (document->isSticker) {
			return pushStickerMedia(*document, basePath);
		} else if (document->isAnimated) {
			return pushAnimatedMedia(*document, basePath);
		} else if (document->isVideoFile) {
			return pushVideoFileMedia(*document, basePath);
		}
		Unexpected("Non generic document in HtmlWriter::Wrap::pushMedia.");
	} else if (const auto photo = std::get_if<Data::Photo>(&content)) {
		Assert(!message.media.ttl);
		return pushPhotoMedia(*photo, basePath);
	} else if (const auto poll = std::get_if<Data::Poll>(&content)) {
		return pushPoll(*poll);
	}
	Assert(v::is_null(content));
	return QByteArray();
}

QByteArray HtmlWriter::Wrap::pushGenericMedia(const MediaData &data) {
	auto result = pushDiv("media_wrap clearfix");
	if (data.link.isEmpty()) {
		result.append(pushDiv("media clearfix pull_left " + data.classes));
	} else {
		result.append(pushTag("a", {
			{
				"class",
				"media clearfix pull_left block_link " + data.classes
			},
			{
				"href",
				(IsGlobalLink(data.link)
					? data.link.toUtf8()
					: relativePath(data.link).toUtf8())
			}
		}));
	}
	if (data.thumb.isEmpty()) {
		result.append(pushDiv("fill pull_left"));
		result.append(popTag());
	} else {
		result.append(pushTag("img", {
			{ "class", "thumb pull_left" },
			{ "src", relativePath(data.thumb).toUtf8() },
			{ "empty", "" }
		}));
	}
	result.append(pushDiv("body"));
	if (!data.title.isEmpty()) {
		result.append(pushDiv("title bold"));
		result.append(SerializeString(data.title));
		result.append(popTag());
	}
	if (!data.description.isEmpty()) {
		result.append(pushDiv("description"));
		result.append(SerializeString(data.description));
		result.append(popTag());
	}
	if (!data.status.isEmpty()) {
		result.append(pushDiv("status details"));
		result.append(SerializeString(data.status));
		result.append(popTag());
	}
	result.append(popTag());
	result.append(popTag());
	result.append(popTag());
	return result;
}

QByteArray HtmlWriter::Wrap::pushStickerMedia(
		const Data::Document &data,
		const QString &basePath) {
	using namespace Data;

	const auto [thumb, size] = WriteImageThumb(
		basePath,
		data.file.relativePath,
		CalculateThumbSize(
			kStickerMaxWidth,
			kStickerMaxHeight,
			kStickerMinWidth,
			kStickerMinHeight),
		"PNG",
		-1);
	if (thumb.isEmpty()) {
		auto generic = MediaData();
		generic.title = "Sticker";
		generic.status = data.stickerEmoji;
		if (data.file.relativePath.isEmpty()) {
			if (!generic.status.isEmpty()) {
				generic.status += ", ";
			}
			generic.status += FormatFileSize(data.file.size);
		} else {
			generic.link = data.file.relativePath;
		}
		generic.description = NoFileDescription(data.file.skipReason);
		generic.classes = "media_photo";
		return pushGenericMedia(generic);
	}
	auto result = pushDiv("media_wrap clearfix");
	result.append(pushTag("a", {
		{ "class", "sticker_wrap clearfix pull_left" },
		{
			"href",
			relativePath(data.file.relativePath).toUtf8()
		}
	}));
	const auto sizeStyle = "width: "
		+ NumberToString(size.width() / 2)
		+ "px; height: "
		+ NumberToString(size.height() / 2)
		+ "px";
	result.append(pushTag("img", {
		{ "class", "sticker" },
		{ "style", sizeStyle },
		{ "src", relativePath(thumb).toUtf8() },
		{ "empty", "" }
	}));
	result.append(popTag());
	result.append(popTag());
	return result;
}

QByteArray HtmlWriter::Wrap::pushAnimatedMedia(
		const Data::Document &data,
		const QString &basePath) {
	using namespace Data;

	auto size = QSize(data.width, data.height);
	auto thumbSize = CalculateThumbSize(
		kPhotoMaxWidth,
		kPhotoMaxHeight,
		kPhotoMinWidth,
		kPhotoMinHeight,
		true)(size);
	if (data.thumb.file.relativePath.isEmpty()
		|| data.file.relativePath.isEmpty()
		|| !thumbSize.width()
		|| !thumbSize.height()) {
		auto generic = MediaData();
		generic.title = "Animation";
		generic.status = FormatFileSize(data.file.size);
		generic.link = data.file.relativePath;
		generic.description = NoFileDescription(data.file.skipReason);
		generic.classes = "media_video";
		return pushGenericMedia(generic);
	}
	auto result = pushDiv("media_wrap clearfix");
	result.append(pushTag("a", {
		{ "class", "animated_wrap clearfix pull_left" },
		{
			"href",
			relativePath(data.file.relativePath).toUtf8()
		}
	}));
	result.append(pushDiv("video_play_bg"));
	result.append(pushDiv("gif_play"));
	result.append("GIF");
	result.append(popTag());
	result.append(popTag());
	const auto sizeStyle = "width: "
		+ NumberToString(thumbSize.width() / 2)
		+ "px; height: "
		+ NumberToString(thumbSize.height() / 2)
		+ "px";
	result.append(pushTag("img", {
		{ "class", "animated" },
		{ "style", sizeStyle },
		{ "src", relativePath(data.thumb.file.relativePath).toUtf8() },
		{ "empty", "" }
	}));
	result.append(popTag());
	result.append(popTag());
	return result;
}

QByteArray HtmlWriter::Wrap::pushVideoFileMedia(
		const Data::Document &data,
		const QString &basePath) {
	using namespace Data;

	auto size = QSize(data.width, data.height);
	auto thumbSize = CalculateThumbSize(
		kPhotoMaxWidth,
		kPhotoMaxHeight,
		kPhotoMinWidth,
		kPhotoMinHeight,
		true)(size);
	if (data.thumb.file.relativePath.isEmpty()
		|| data.file.relativePath.isEmpty()
		|| !thumbSize.width()
		|| !thumbSize.height()) {
		auto generic = MediaData();
		generic.title = "Video file";
		generic.status = FormatDuration(data.duration);
		if (data.file.relativePath.isEmpty()) {
			generic.status += ", " + FormatFileSize(data.file.size);
		} else {
			generic.link = data.file.relativePath;
		}
		generic.description = NoFileDescription(data.file.skipReason);
		generic.classes = "media_video";
		return pushGenericMedia(generic);
	}
	auto result = pushDiv("media_wrap clearfix");
	result.append(pushTag("a", {
		{ "class", "video_file_wrap clearfix pull_left" },
		{
			"href",
			relativePath(data.file.relativePath).toUtf8()
		}
	}));
	result.append(pushDiv("video_play_bg"));
	result.append(pushDiv("video_play"));
	result.append(popTag());
	result.append(popTag());
	result.append(pushDiv("video_duration"));
	result.append(FormatDuration(data.duration));
	result.append(popTag());
	const auto sizeStyle = "width: "
		+ NumberToString(thumbSize.width() / 2)
		+ "px; height: "
		+ NumberToString(thumbSize.height() / 2)
		+ "px";
	result.append(pushTag("img", {
		{ "class", "video_file" },
		{ "style", sizeStyle },
		{ "src", relativePath(data.thumb.file.relativePath).toUtf8() },
		{ "empty", "" }
	}));
	result.append(popTag());
	result.append(popTag());
	return result;
}

QByteArray HtmlWriter::Wrap::pushPhotoMedia(
		const Data::Photo &data,
		const QString &basePath) {
	using namespace Data;

	const auto [thumb, size] = WriteImageThumb(
		basePath,
		data.image.file.relativePath,
		CalculateThumbSize(
			kPhotoMaxWidth,
			kPhotoMaxHeight,
			kPhotoMinWidth,
			kPhotoMinHeight));
	if (thumb.isEmpty()) {
		auto generic = MediaData();
		generic.title = "Photo";
		generic.status = NumberToString(data.image.width)
			+ "x"
			+ NumberToString(data.image.height);
		if (data.image.file.relativePath.isEmpty()) {
			generic.status += ", " + FormatFileSize(data.image.file.size);
		} else {
			generic.link = data.image.file.relativePath;
		}
		generic.description = NoFileDescription(data.image.file.skipReason);
		generic.classes = "media_photo";
		return pushGenericMedia(generic);
	}
	auto result = pushDiv("media_wrap clearfix");
	result.append(pushTag("a", {
		{ "class", "photo_wrap clearfix pull_left" },
		{
			"href",
			relativePath(data.image.file.relativePath).toUtf8()
		}
	}));
	const auto sizeStyle = "width: "
		+ NumberToString(size.width() / 2)
		+ "px; height: "
		+ NumberToString(size.height() / 2)
		+ "px";
	result.append(pushTag("img", {
		{ "class", "photo" },
		{ "style", sizeStyle },
		{ "src", relativePath(thumb).toUtf8() },
		{ "empty", "" }
	}));
	result.append(popTag());
	result.append(popTag());
	return result;
}

QByteArray HtmlWriter::Wrap::pushPoll(const Data::Poll &data) {
	using namespace Data;

	auto result = pushDiv("media_wrap clearfix");
	result.append(pushDiv("media_poll"));
	result.append(pushDiv("question bold"));
	result.append(SerializeString(data.question));
	result.append(popTag());
	result.append(pushDiv("details"));
	if (data.closed) {
		result.append(SerializeString("Final results"));
	} else {
		result.append(SerializeString("Anonymous poll"));
	}
	result.append(popTag());
	const auto votes = [](int count) {
		if (count > 1) {
			return NumberToString(count) + " votes";
		} else if (count > 0) {
			return NumberToString(count) + " vote";
		}
		return QByteArray("No votes");
	};
	const auto details = [&](const Poll::Answer &answer) {
		if (!answer.votes) {
			return QByteArray("");
		} else if (!answer.my) {
			return " <span class=\"details\">"
				+ votes(answer.votes)
				+ "</span>";
		}
		return " <span class=\"details\">"
			+ votes(answer.votes)
			+ ", chosen vote</span>";
	};
	for (const auto &answer : data.answers) {
		result.append(pushDiv("answer"));
		result.append("- " + SerializeString(answer.text) + details(answer));
		result.append(popTag());
	}
	result.append(pushDiv("total details	"));
	result.append(votes(data.totalVotes));
	result.append(popTag());
	result.append(popTag());
	result.append(popTag());
	return result;
}

MediaData HtmlWriter::Wrap::prepareMediaData(
		const Data::Message &message,
		const QString &basePath,
		const PeersMap &peers,
		const QString &internalLinksDomain) const {
	using namespace Data;

	auto result = MediaData();
	const auto &action = message.action;
	if (const auto call = std::get_if<ActionPhoneCall>(&action.content)) {
		result.classes = "media_call";
		result.title = peers.peer(message.out
				? message.peerId
				: message.selfId).name();
		result.status = [&] {
			using Reason = ActionPhoneCall::DiscardReason;
			const auto reason = call->discardReason;
			if (message.out) {
				return reason == Reason::Missed ? "Cancelled" : "Outgoing";
			} else if (reason == Reason::Missed) {
				return "Missed";
			} else if (reason == Reason::Busy) {
				return "Declined";
			}
			return "Incoming";
		}();
		if (call->duration > 0) {
			result.classes += " success";
			result.status += " ("
				+ NumberToString(call->duration)
				+ " seconds)";
		}
		return result;
	}

	v::match(message.media.content, [&](const Photo &data) {
		if (message.media.ttl) {
			result.title = "Self-destructing photo";
			result.status = data.id
				? "Please view it on your mobile"
				: "Expired";
			result.classes = "media_photo";
			return;
		}
		// At least try to pushPhotoMedia.
	}, [&](const Document &data) {
		if (message.media.ttl) {
			result.title = "Self-destructing video";
			result.status = data.id
				? "Please view it on your mobile"
				: "Expired";
			result.classes = "media_video";
			return;
		}
		const auto hasFile = !data.file.relativePath.isEmpty();
		result.link = data.file.relativePath;
		result.description = NoFileDescription(data.file.skipReason);
		if (data.isSticker) {
			// At least try to pushStickerMedia.
		} else if (data.isVideoMessage) {
			result.title = "Video message";
			result.status = FormatDuration(data.duration);
			if (!hasFile) {
				result.status += ", " + FormatFileSize(data.file.size);
			}
			result.thumb = data.thumb.file.relativePath;
			result.classes = "media_video";
		} else if (data.isVoiceMessage) {
			result.title = "Voice message";
			result.status = FormatDuration(data.duration);
			if (!hasFile) {
				result.status += ", " + FormatFileSize(data.file.size);
			}
			result.classes = "media_voice_message";
		} else if (data.isAnimated) {
			// At least try to pushAnimatedMedia.
		} else if (data.isVideoFile) {
			// At least try to pushVideoFileMedia.
		} else if (data.isAudioFile) {
			result.title = (data.songPerformer.isEmpty()
				|| data.songTitle.isEmpty())
				? QByteArray("Audio file")
				: data.songPerformer + " \xe2\x80\x93 " + data.songTitle;
			result.status = FormatDuration(data.duration);
			if (!hasFile) {
				result.status += ", " + FormatFileSize(data.file.size);
			}
			result.classes = "media_audio_file";
		} else {
			result.title = data.name.isEmpty()
				? QByteArray("File")
				: data.name;
			result.status = FormatFileSize(data.file.size);
			result.classes = "media_file";
		}
	}, [&](const SharedContact &data) {
		result.title = data.info.firstName + ' ' + data.info.lastName;
		result.classes = "media_contact";
		result.status = FormatPhoneNumber(data.info.phoneNumber);
		if (!data.vcard.content.isEmpty()) {
			result.status += " - vCard";
			result.link = data.vcard.relativePath;
		}
	}, [&](const GeoPoint &data) {
		if (message.media.ttl) {
			result.classes = "media_live_location";
			result.title = "Live location";
			result.status = "";
		} else {
			result.classes = "media_location";
			result.title = "Location";
		}
		if (data.valid) {
			const auto latitude = NumberToString(data.latitude);
			const auto longitude = NumberToString(data.longitude);
			const auto coords = latitude + ',' + longitude;
			result.status = latitude + ", " + longitude;
			result.link = "https://maps.google.com/maps?q="
				+ coords
				+ "&ll="
				+ coords
				+ "&z=16";
		}
	}, [&](const Venue &data) {
		result.classes = "media_venue";
		result.title = data.title;
		result.description = data.address;
		if (data.point.valid) {
			const auto latitude = NumberToString(data.point.latitude);
			const auto longitude = NumberToString(data.point.longitude);
			const auto coords = latitude + ',' + longitude;
			result.link = "https://maps.google.com/maps?q="
				+ coords
				+ "&ll="
				+ coords
				+ "&z=16";
		}
	}, [&](const Game &data) {
		result.classes = "media_game";
		result.title = data.title;
		result.description = data.description;
		if (data.botId != 0 && !data.shortName.isEmpty()) {
			const auto bot = peers.user(data.botId);
			if (bot.isBot && !bot.username.isEmpty()) {
				const auto link = internalLinksDomain.toUtf8()
					+ bot.username
					+ "?game="
					+ data.shortName;
				result.link = link;
				result.status = link;
			}
		}
	}, [&](const Invoice &data) {
		result.classes = "media_invoice";
		result.title = data.title;
		result.description = data.description;
		result.status = Data::FormatMoneyAmount(data.amount, data.currency);
	}, [](const Poll &data) {
	}, [](const UnsupportedMedia &data) {
		Unexpected("Unsupported message.");
	}, [](v::null_t) {});
	return result;
}

bool HtmlWriter::Wrap::forwardedNeedsWrap(
		const Data::Message &message,
		const MessageInfo *previous) const {
	Expects(message.forwarded);

	if (messageNeedsWrap(message, previous)) {
		return true;
	} else if (!message.forwardedFromId
		|| message.forwardedFromId != previous->forwardedFromId) {
		return true;
	} else if (Data::IsChatPeerId(message.forwardedFromId)) {
		return true;
	} else if (abs(message.forwardedDate - previous->forwardedDate)
		> kJoinWithinSeconds) {
		return true;
	}
	return false;
}

Result HtmlWriter::Wrap::close() {
	if (!std::exchange(_closed, true) && !_file.empty()) {
		auto block = QByteArray();
		while (!_context.empty()) {
			block.append(_context.popTag());
		}
		return _file.writeBlock(block);
	}
	return Result::Success();
}

QString HtmlWriter::Wrap::relativePath(const QString &path) const {
	return _base + path;
}

QString HtmlWriter::Wrap::relativePath(const Data::File &file) const {
	return relativePath(file.relativePath);
}

QByteArray HtmlWriter::Wrap::composeStart() {
	auto result = "<!DOCTYPE html>" + _context.pushTag("html");
	result.append(pushTag("head"));
	result.append(pushTag("meta", {
		{ "charset", "utf-8" },
		{ "empty", "" }
	}));
	result.append(pushTag("title", { { "inline", "" } }));
	result.append("Exported Data");
	result.append(popTag());
	result.append(_context.pushTag("meta", {
		{ "name", "viewport" },
		{ "content", "width=device-width, initial-scale=1.0" },
		{ "empty", "" }
	}));
	result.append(_context.pushTag("link", {
		{ "href", _base + "css/style.css" },
		{ "rel", "stylesheet" },
		{ "empty", "" }
	}));
	result.append(_context.pushTag("script", {
		{ "src", _base + "js/script.js" },
		{ "type", "text/javascript" },
	}));
	result.append(_context.popTag());
	result.append(popTag());
	result.append(pushTag("body", {
		{ "onload", "CheckLocation();" }
	}));
	result.append(pushDiv("page_wrap"));
	return result;
}

HtmlWriter::Wrap::~Wrap() {
	(void)close();
}

HtmlWriter::HtmlWriter() = default;

Result HtmlWriter::start(
		const Settings &settings,
		const Environment &environment,
		Stats *stats) {
	Expects(settings.path.endsWith('/'));

	_settings = base::duplicate(settings);
	_environment = environment;
	_stats = stats;

	//const auto result = copyFile(
	//	":/export/css/bootstrap.min.css",
	//	"css/bootstrap.min.css");
	//if (!result) {
	//	return result;
	//}
	const auto copy = [&](const QString &filename) {
		return copyFile(":/export/" + filename, filename);
	};
	const auto files = {
		"css/style.css",
		"images/back.png",
		"images/media_call.png",
		"images/media_contact.png",
		"images/media_file.png",
		"images/media_game.png",
		"images/media_location.png",
		"images/media_music.png",
		"images/media_photo.png",
		"images/media_shop.png",
		"images/media_video.png",
		"images/media_voice.png",
		"images/section_calls.png",
		"images/section_chats.png",
		"images/section_contacts.png",
		"images/section_frequent.png",
		"images/section_other.png",
		"images/section_photos.png",
		"images/section_sessions.png",
		"images/section_web.png",
		"js/script.js",
	};
	for (const auto path : files) {
		const auto name = QString(path);
		if (const auto result = copy(name); !result) {
			return result;
		} else if (const auto png = name.indexOf(".png"); png > 0) {
			const auto x2 = name.mid(0, png) + "@2x.png";
			if (const auto result = copy(x2); !result) {
				return result;
			}
		}
	}

	if (_settings.onlySinglePeer()) {
		return Result::Success();
	}
	_summary = fileWithRelativePath(mainFileRelativePath());
	auto block = _summary->pushHeader("Exported Data");
	block.append(_summary->pushDiv("page_body"));
	return _summary->writeBlock(block);
}

Result HtmlWriter::writePersonal(const Data::PersonalInfo &data) {
	Expects(_summary != nullptr);

	_selfColorIndex = Data::PeerColorIndex(data.user.info.userId);
	if (_settings.types & Settings::Type::Userpics) {
		_delayedPersonalInfo = std::make_unique<Data::PersonalInfo>(data);
		return Result::Success();
	}
	return writeDefaultPersonal(data);
}

Result HtmlWriter::writeDefaultPersonal(const Data::PersonalInfo &data) {
	return writePreparedPersonal(data, QString());
}

Result HtmlWriter::writeDelayedPersonal(const QString &userpicPath) {
	if (!_delayedPersonalInfo) {
		return Result::Success();
	}
	const auto result = writePreparedPersonal(
		*base::take(_delayedPersonalInfo),
		userpicPath);
	if (!result) {
		return result;
	}
	if (_userpicsCount) {
		pushUserpicsSection();
	}
	return Result::Success();
}

Result HtmlWriter::writePreparedPersonal(
		const Data::PersonalInfo &data,
		const QString &userpicPath) {
	Expects(_summary != nullptr);

	const auto &info = data.user.info;

	auto userpic = UserpicData{ _selfColorIndex, kPersonalUserpicSize };
	userpic.largeLink = userpicPath.isEmpty()
		? QString()
		: userpicsFilePath();
	userpic.imageLink = WriteUserpicThumb(
		_settings.path,
		userpicPath,
		userpic,
		"_info");
	userpic.firstName = info.firstName;
	userpic.lastName = info.lastName;

	auto block = _summary->pushDiv("personal_info clearfix");
	block.append(_summary->pushDiv("pull_right userpic_wrap"));
	block.append(_summary->pushUserpic(userpic));
	block.append(_summary->popTag());
	const auto pushRows = [&](
			QByteArray name,
			std::vector<std::pair<QByteArray, QByteArray>> &&values) {
		block.append(_summary->pushDiv("rows " + name));
		for (const auto &[key, value] : values) {
			if (value.isEmpty()) {
				continue;
			}
			block.append(_summary->pushDiv("row"));
			block.append(_summary->pushDiv("label details"));
			block.append(SerializeString(key));
			block.append(_summary->popTag());
			block.append(_summary->pushDiv("value bold"));
			block.append(SerializeString(value));
			block.append(_summary->popTag());
			block.append(_summary->popTag());
		}
		block.append(_summary->popTag());
	};
	pushRows("names", {
		{ "First name", info.firstName },
		{ "Last name", info.lastName },
	});
	pushRows("info", {
		{ "Phone number", Data::FormatPhoneNumber(info.phoneNumber) },
		{ "Username", FormatUsername(data.user.username) },
	});
	pushRows("bio", { { "Bio", data.bio } });
	block.append(_summary->popTag());

	_summaryNeedDivider = true;
	return _summary->writeBlock(block);
}

Result HtmlWriter::writeUserpicsStart(const Data::UserpicsInfo &data) {
	Expects(_summary != nullptr);
	Expects(_userpics == nullptr);

	_userpicsCount = data.count;
	if (!_userpicsCount) {
		return Result::Success();
	}
	_userpics = fileWithRelativePath(userpicsFilePath());

	auto block = _userpics->pushHeader(
		"Profile pictures",
		mainFileRelativePath());
	block.append(_userpics->pushDiv("page_body list_page"));
	block.append(_userpics->pushDiv("entry_list"));
	if (const auto result = _userpics->writeBlock(block); !result) {
		return result;
	}
	if (!_delayedPersonalInfo) {
		pushUserpicsSection();
	}
	return Result::Success();
}

Result HtmlWriter::writeUserpicsSlice(const Data::UserpicsSlice &data) {
	Expects(_userpics != nullptr);
	Expects(!data.list.empty());

	const auto firstPath = data.list.front().image.file.relativePath;
	if (const auto result = writeDelayedPersonal(firstPath); !result) {
		return result;
	}

	auto block = QByteArray();
	for (const auto &userpic : data.list) {
		auto data = UserpicData{ _selfColorIndex, kEntryUserpicSize };
		using SkipReason = Data::File::SkipReason;
		const auto &file = userpic.image.file;
		Assert(!file.relativePath.isEmpty()
			|| file.skipReason != SkipReason::None);
		const auto status = [&]() -> Data::Utf8String {
			switch (file.skipReason) {
			case SkipReason::Unavailable:
				return "(Photo unavailable, please try again later)";
			case SkipReason::FileSize:
				return "(Photo exceeds maximum size. "
					"Change data exporting settings to download.)";
			case SkipReason::FileType:
				return "(Photo not included. "
					"Change data exporting settings to download.)";
			case SkipReason::None: return Data::FormatFileSize(file.size);
			}
			Unexpected("Skip reason while writing photo path.");
		}();
		const auto &path = userpic.image.file.relativePath;
		data.imageLink = WriteUserpicThumb(_settings.path, path, data);
		data.firstName = path.toUtf8();
		block.append(_userpics->pushListEntry(
			data,
			(path.isEmpty() ? QString("Photo unavailable") : path).toUtf8(),
			status,
			(userpic.date > 0
				? Data::FormatDateTime(userpic.date)
				: QByteArray()),
			path));
	}
	return _userpics->writeBlock(block);
}

Result HtmlWriter::writeUserpicsEnd() {
	if (const auto result = writeDelayedPersonal(QString()); !result) {
		return result;
	} else if (_userpics) {
		return base::take(_userpics)->close();
	}
	return Result::Success();
}

QString HtmlWriter::userpicsFilePath() const {
	return "lists/profile_pictures.html";
}

void HtmlWriter::pushUserpicsSection() {
	pushSection(
		4,
		"Profile pictures",
		"photos",
		_userpicsCount,
		userpicsFilePath());
}

Result HtmlWriter::writeContactsList(const Data::ContactsList &data) {
	Expects(_summary != nullptr);

	if (const auto result = writeSavedContacts(data); !result) {
		return result;
	} else if (const auto result = writeFrequentContacts(data); !result) {
		return result;
	}
	return Result::Success();
}

Result HtmlWriter::writeSavedContacts(const Data::ContactsList &data) {
	if (data.list.empty()) {
		return Result::Success();
	}

	const auto filename = "lists/contacts.html";
	const auto file = fileWithRelativePath(filename);
	auto block = file->pushHeader(
		"Contacts",
		mainFileRelativePath());
	block.append(file->pushDiv("page_body list_page"));
	block.append(file->pushAbout(_environment.aboutContacts));
	block.append(file->pushDiv("entry_list"));
	for (const auto index : Data::SortedContactsIndices(data)) {
		const auto &contact = data.list[index];
		auto userpic = UserpicData{
			Data::ContactColorIndex(contact),
			kEntryUserpicSize
		};
		userpic.firstName = contact.firstName;
		userpic.lastName = contact.lastName;
		block.append(file->pushListEntry(
			userpic,
			ComposeName(userpic, "Deleted Account"),
			Data::FormatPhoneNumber(contact.phoneNumber),
			Data::FormatDateTime(contact.date)));
	}
	if (const auto result = file->writeBlock(block); !result) {
		return result;
	} else if (const auto closed = file->close(); !closed) {
		return closed;
	}

	pushSection(
		2,
		"Contacts",
		"contacts",
		data.list.size(),
		filename);
	return Result::Success();
}

Result HtmlWriter::writeFrequentContacts(const Data::ContactsList &data) {
	const auto size = data.correspondents.size()
		+ data.inlineBots.size()
		+ data.phoneCalls.size();
	if (!size) {
		return Result::Success();
	}

	const auto filename = "lists/frequent.html";
	const auto file = fileWithRelativePath(filename);
	auto block = file->pushHeader(
		"Frequent contacts",
		mainFileRelativePath());
	block.append(file->pushDiv("page_body list_page"));
	block.append(file->pushAbout(_environment.aboutFrequent));
	block.append(file->pushDiv("entry_list"));
	const auto writeList = [&](
			const std::vector<Data::TopPeer> &peers,
			Data::Utf8String category) {
		for (const auto &top : peers) {
			const auto name = [&]() -> Data::Utf8String {
				if (top.peer.chat()) {
					return top.peer.name();
				} else if (top.peer.user()->isSelf) {
					return "Saved messages";
				} else {
					return top.peer.user()->info.firstName;
				}
			}();
			const auto lastName = [&]() -> Data::Utf8String {
				if (top.peer.user() && !top.peer.user()->isSelf) {
					return top.peer.user()->info.lastName;
				}
				return {};
			}();
			auto userpic = UserpicData{
				Data::PeerColorIndex(Data::BarePeerId(top.peer.id())),
				kEntryUserpicSize
			};
			userpic.firstName = name;
			userpic.lastName = lastName;
			block.append(file->pushListEntry(
				userpic,
				ComposeName(userpic, "Deleted Account"),
				"Rating: " + Data::NumberToString(top.rating),
				category));
		}
	};
	writeList(data.correspondents, "people");
	writeList(data.inlineBots, "inline bots");
	writeList(data.phoneCalls, "calls");
	if (const auto result = file->writeBlock(block); !result) {
		return result;
	} else if (const auto closed = file->close(); !closed) {
		return closed;
	}

	pushSection(
		3,
		"Frequent contacts",
		"frequent",
		size,
		filename);
	return Result::Success();
}

Result HtmlWriter::writeSessionsList(const Data::SessionsList &data) {
	Expects(_summary != nullptr);

	if (const auto result = writeSessions(data); !result) {
		return result;
	} else if (const auto result = writeWebSessions(data); !result) {
		return result;
	}
	return Result::Success();
}

Result HtmlWriter::writeSessions(const Data::SessionsList &data) {
	Expects(_summary != nullptr);

	if (data.list.empty()) {
		return Result::Success();
	}

	const auto filename = "lists/sessions.html";
	const auto file = fileWithRelativePath(filename);
	auto block = file->pushHeader(
		"Sessions",
		mainFileRelativePath());
	block.append(file->pushDiv("page_body list_page"));
	block.append(file->pushAbout(_environment.aboutSessions));
	block.append(file->pushDiv("entry_list"));
	for (const auto &session : data.list) {
		block.append(file->pushSessionListEntry(
			session.applicationId,
			((session.applicationName.isEmpty()
				? Data::Utf8String("Unknown")
				: session.applicationName)
				+ ' '
				+ session.applicationVersion),
			(session.deviceModel
				+ ", "
				+ session.platform
				+ ' '
				+ session.systemVersion),
			{
				(session.ip
					+ " \xE2\x80\x93 "
					+ session.region
					+ ((session.region.isEmpty() || session.country.isEmpty())
						? QByteArray()
						: QByteArray(", "))
					+ session.country),
				"Last active: " + Data::FormatDateTime(session.lastActive),
				"Created: " + Data::FormatDateTime(session.created)
			}));
	}
	if (const auto result = file->writeBlock(block); !result) {
		return result;
	} else if (const auto closed = file->close(); !closed) {
		return closed;
	}

	pushSection(
		5,
		"Sessions",
		"sessions",
		data.list.size(),
		filename);
	return Result::Success();
}

Result HtmlWriter::writeWebSessions(const Data::SessionsList &data) {
	Expects(_summary != nullptr);

	if (data.webList.empty()) {
		return Result::Success();
	}

	const auto filename = "lists/web_sessions.html";
	const auto file = fileWithRelativePath(filename);
	auto block = file->pushHeader(
		"Web sessions",
		mainFileRelativePath());
	block.append(file->pushDiv("page_body list_page"));
	block.append(file->pushAbout(_environment.aboutWebSessions));
	block.append(file->pushDiv("entry_list"));
	for (const auto &session : data.webList) {
		block.append(file->pushSessionListEntry(
			Data::DomainApplicationId(session.domain),
			(session.domain.isEmpty()
				? Data::Utf8String("Unknown")
				: session.domain),
			session.platform + ", " + session.browser,
			{
				session.ip + " \xE2\x80\x93 " + session.region,
				"Last active: " + Data::FormatDateTime(session.lastActive),
				"Created: " + Data::FormatDateTime(session.created)
			},
			(session.botUsername.isEmpty()
				? QByteArray()
				: ('@' + session.botUsername))));
	}
	if (const auto result = file->writeBlock(block); !result) {
		return result;
	} else if (const auto closed = file->close(); !closed) {
		return closed;
	}

	pushSection(
		6,
		"Web sessions",
		"web",
		data.webList.size(),
		filename);
	return Result::Success();
}

Result HtmlWriter::writeOtherData(const Data::File &data) {
	Expects(_summary != nullptr);

	pushSection(
		7,
		"Other data",
		"other",
		1,
		data.relativePath);
	return Result::Success();
}

Result HtmlWriter::writeDialogsStart(const Data::DialogsInfo &data) {
	Expects(_chats == nullptr);

	if (data.chats.empty() && data.left.empty()) {
		return Result::Success();
	} else if (_settings.onlySinglePeer()) {
		return Result::Success();
	}

	_dialogsRelativePath = "lists/chats.html";
	_chats = fileWithRelativePath(_dialogsRelativePath);

	auto block = _chats->pushHeader(
		"Chats",
		mainFileRelativePath());
	block.append(_chats->pushDiv("page_body list_page"));
	if (const auto result = _chats->writeBlock(block); !result) {
		return result;
	}

	pushSection(
		0,
		"Chats",
		"chats",
		data.chats.size() + data.left.size(),
		"lists/chats.html");
	return writeSections();
}

Result HtmlWriter::writeDialogStart(const Data::DialogInfo &data) {
	Expects(_chat == nullptr);

	_chat = fileWithRelativePath(data.relativePath + messagesFile(0));
	_chatFileEmpty = true;
	_messagesCount = 0;
	_dateMessageId = 0;
	_lastMessageInfo = nullptr;
	_lastMessageIdsPerFile.clear();
	_dialog = data;
	return Result::Success();
}

Result HtmlWriter::writeDialogSlice(const Data::MessagesSlice &data) {
	Expects(_chat != nullptr);
	Expects(!data.list.empty());

	const auto messageLinkWrapper = [&](int messageId, QByteArray text) {
		return wrapMessageLink(messageId, text);
	};
	auto oldIndex = (_messagesCount > 0)
		? ((_messagesCount - 1) / kMessagesInFile)
		: 0;
	auto previous = _lastMessageInfo.get();
	auto saved = std::optional<MessageInfo>();
	auto block = QByteArray();
	for (const auto &message : data.list) {
		if (Data::SkipMessageByDate(message, _settings)) {
			continue;
		}
		const auto newIndex = (_messagesCount / kMessagesInFile);
		if (oldIndex != newIndex) {
			if (const auto result = _chat->writeBlock(block); !result) {
				return result;
			} else if (const auto next = switchToNextChatFile(newIndex)) {
				Assert(saved.has_value() || _lastMessageInfo != nullptr);
				_lastMessageIdsPerFile.push_back(saved
					? saved->id
					: _lastMessageInfo->id);
				block = QByteArray();
				_lastMessageInfo = nullptr;
				previous = nullptr;
				saved = std::nullopt;
				oldIndex = newIndex;
			} else {
				return next;
			}
		}
		if (_chatFileEmpty) {
			if (const auto result = writeDialogOpening(oldIndex); !result) {
				return result;
			}
			_chatFileEmpty = false;
		}
		const auto date = message.date;
		if (DisplayDate(date, previous ? previous->date : 0)) {
			block.append(_chat->pushServiceMessage(
				--_dateMessageId,
				_dialog,
				_settings.path,
				FormatDateText(date)));
		}
		const auto [info, content] = _chat->pushMessage(
			message,
			previous,
			_dialog,
			_settings.path,
			data.peers,
			_environment.internalLinksDomain,
			messageLinkWrapper);
		block.append(content);

		++_messagesCount;
		saved = info;
		previous = &*saved;
	}
	if (saved) {
		_lastMessageInfo = std::make_unique<MessageInfo>(*saved);
	}
	return block.isEmpty() ? Result::Success() : _chat->writeBlock(block);
}

Result HtmlWriter::writeEmptySinglePeer() {
	Expects(_chat != nullptr);

	if (!_settings.onlySinglePeer() || _messagesCount != 0) {
		return Result::Success();
	}
	Assert(_chatFileEmpty);
	if (const auto result = writeDialogOpening(0); !result) {
		return result;
	}
	return _chat->writeBlock(_chat->pushServiceMessage(
		--_dateMessageId,
		_dialog,
		_settings.path,
		"No exported messages"));
}

Result HtmlWriter::writeDialogEnd() {
	Expects(_settings.onlySinglePeer() || _chats != nullptr);
	Expects(_chat != nullptr);

	if (const auto result = writeEmptySinglePeer(); !result) {
		return result;
	}

	if (const auto closed = base::take(_chat)->close(); !closed) {
		return closed;
	} else if (_settings.onlySinglePeer()) {
		return Result::Success();
	}

	using Type = Data::DialogInfo::Type;
	const auto TypeString = [](Type type) {
		switch (type) {
		case Type::Unknown: return "unknown";
		case Type::Self:
		case Type::Replies:
		case Type::Personal: return "private";
		case Type::Bot: return "bot";
		case Type::PrivateGroup:
		case Type::PrivateSupergroup:
		case Type::PublicSupergroup: return "group";
		case Type::PrivateChannel:
		case Type::PublicChannel: return "channel";
		}
		Unexpected("Dialog type in TypeString.");
	};
	const auto DeletedString = [](Type type) {
		switch (type) {
		case Type::Unknown:
		case Type::Self:
		case Type::Replies:
		case Type::Personal:
		case Type::Bot: return "Deleted Account";
		case Type::PrivateGroup:
		case Type::PrivateSupergroup:
		case Type::PublicSupergroup: return "Deleted Group";
		case Type::PrivateChannel:
		case Type::PublicChannel: return "Deleted Channel";
		}
		Unexpected("Dialog type in TypeString.");
	};
	const auto NameString = [](
			const Data::DialogInfo &dialog) -> QByteArray {
		if (dialog.type == Type::Self) {
			return "Saved messages";
		} else if (dialog.type == Type::Replies) {
			return "Replies";
		}
		return dialog.name;
	};
	const auto LastNameString = [](
			const Data::DialogInfo &dialog) -> QByteArray {
		if (dialog.type != Type::Personal && dialog.type != Type::Bot) {
			return {};
		}
		return dialog.lastName;
	};
	const auto CountString = [](int count, bool outgoing) -> QByteArray {
		if (count == 1) {
			return outgoing ? "1 outgoing message" : "1 message";
		} else if (!count) {
			return outgoing ? "No outgoing messages" : "No messages";
		}
		return Data::NumberToString(count)
			+ (outgoing ? " outgoing messages" : " messages");
	};
	auto userpic = UserpicData{
		((_dialog.type == Type::Self || _dialog.type == Type::Replies)
			? kSavedMessagesColorIndex
			: Data::PeerColorIndex(Data::BarePeerId(_dialog.peerId))),
		kEntryUserpicSize
	};
	userpic.firstName = NameString(_dialog);
	userpic.lastName = LastNameString(_dialog);

	const auto result = validateDialogsMode(_dialog.isLeftChannel);
	if (!result) {
		return result;
	}

	return _chats->writeBlock(_chats->pushListEntry(
		userpic,
		ComposeName(userpic, DeletedString(_dialog.type)),
		CountString(_messagesCount, _dialog.onlyMyMessages),
		TypeString(_dialog.type),
		(_messagesCount > 0
			? (_dialog.relativePath + "messages.html")
			: QString())));
}

Result HtmlWriter::validateDialogsMode(bool isLeftChannel) {
	const auto mode = isLeftChannel
		? DialogsMode::Left
		: DialogsMode::Chats;
	if (_dialogsMode == mode) {
		return Result::Success();
	} else if (_dialogsMode != DialogsMode::None) {
		const auto result = _chats->writeBlock(_chats->popTag());
		if (!result) {
			return result;
		}
	}
	_dialogsMode = mode;
	auto block = _chats->pushAbout(isLeftChannel
		? _environment.aboutLeftChats
		: _environment.aboutChats);
	block.append(_chats->pushDiv("entry_list"));
	return _chats->writeBlock(block);
}

Result HtmlWriter::writeDialogsEnd() {
	if (_chats) {
		return base::take(_chats)->close();
	}
	return Result::Success();
}

Result HtmlWriter::writeDialogOpening(int index) {
	const auto name = (_dialog.name.isEmpty()
		&& _dialog.lastName.isEmpty())
		? QByteArray("Deleted Account")
		: (_dialog.name + ' ' + _dialog.lastName);
	auto block = _chat->pushHeader(
		name,
		_settings.onlySinglePeer() ? QString() : _dialogsRelativePath);
	block.append(_chat->pushDiv("page_body chat_page"));
	block.append(_chat->pushDiv("history"));
	if (index > 0) {
		const auto previousPath = messagesFile(index - 1);
		block.append(_chat->pushTag("a", {
			{ "class", "pagination block_link" },
			{ "href", previousPath.toUtf8() }
			}));
		block.append("Previous messages");
		block.append(_chat->popTag());
	}
	return _chat->writeBlock(block);
}

void HtmlWriter::pushSection(
		int priority,
		const QByteArray &label,
		const QByteArray &type,
		int count,
		const QString &path) {
	_savedSections.push_back({
		priority,
		label,
		type,
		count,
		path
	});
}

Result HtmlWriter::writeSections() {
	Expects(_summary != nullptr);

	if (_savedSections.empty()) {
		return Result::Success();
	} else if (!_haveSections) {
		auto block = _summary->pushDiv(
			_summaryNeedDivider ? "sections with_divider" : "sections");
		if (const auto result = _summary->writeBlock(block); !result) {
			return result;
		}
		_haveSections = true;
		_summaryNeedDivider = false;
	}

	auto block = QByteArray();
	ranges::sort(_savedSections, std::less<>(), [](const SavedSection &data) {
		return data.priority;
	});
	for (const auto &section : base::take(_savedSections)) {
		block.append(_summary->pushSection(
			section.label,
			section.type,
			section.count,
			_summary->relativePath(section.path)));
	}
	return _summary->writeBlock(block);
}

QByteArray HtmlWriter::wrapMessageLink(int messageId, QByteArray text) {
	const auto finishedCount = _lastMessageIdsPerFile.size();
	const auto it = ranges::find_if(_lastMessageIdsPerFile, [&](int maxMessageId) {
		return messageId <= maxMessageId;
	});
	if (it == end(_lastMessageIdsPerFile)) {
		return "<a href=\"#go_to_message"
			+ Data::NumberToString(messageId)
			+ "\" onclick=\"return GoToMessage("
			+ Data::NumberToString(messageId)
			+ ")\">"
			+ text + "</a>";
	} else {
		const auto index = it - begin(_lastMessageIdsPerFile);
		return "<a href=\"" + messagesFile(index).toUtf8()
			+ "#go_to_message"
			+ Data::NumberToString(messageId)
			+ "\">"
			+ text + "</a>";

	}
}

Result HtmlWriter::switchToNextChatFile(int index) {
	Expects(_chat != nullptr);

	const auto nextPath = messagesFile(index);
	auto next = _chat->pushTag("a", {
		{ "class", "pagination block_link" },
		{ "href", nextPath.toUtf8() }
	});
	next.append("Next messages");
	next.append(_chat->popTag());
	if (const auto result = _chat->writeBlock(next); !result) {
		return result;
	} else if (const auto end = _chat->close(); !end) {
		return end;
	}
	_chat = fileWithRelativePath(_dialog.relativePath + nextPath);
	_chatFileEmpty = true;
	return Result::Success();
}

Result HtmlWriter::finish() {
	Expects(_settings.onlySinglePeer() || _summary != nullptr);

	if (_settings.onlySinglePeer()) {
		return Result::Success();
	}

	if (const auto result = writeSections(); !result) {
		return result;
	}
	auto block = QByteArray();
	if (_haveSections) {
		block.append(_summary->popTag());
		_summaryNeedDivider = true;
		_haveSections = false;
	}
	block.append(_summary->pushAbout(
		_environment.aboutTelegram,
		_summaryNeedDivider));
	if (const auto result = _summary->writeBlock(block); !result) {
		return result;
	}
	return _summary->close();
}

Result HtmlWriter::copyFile(
		const QString &source,
		const QString &relativePath) const {
	return File::Copy(
		source,
		pathWithRelativePath(relativePath),
		_stats);
}

QString HtmlWriter::mainFilePath() {
	return pathWithRelativePath(_settings.onlySinglePeer()
		? messagesFile(0)
		: mainFileRelativePath());
}

QString HtmlWriter::mainFileRelativePath() const {
	return "export_results.html";
}

QString HtmlWriter::pathWithRelativePath(const QString &path) const {
	return _settings.path + path;
}

QString HtmlWriter::messagesFile(int index) const {
	return "messages"
		+ (index > 0 ? QString::number(index + 1) : QString())
		+ ".html";
}

std::unique_ptr<HtmlWriter::Wrap> HtmlWriter::fileWithRelativePath(
		const QString &path) const {
	return std::make_unique<Wrap>(
		pathWithRelativePath(path),
		_settings.path,
		_stats);
}

HtmlWriter::~HtmlWriter() = default;

} // namespace Output
} // namespace Export
