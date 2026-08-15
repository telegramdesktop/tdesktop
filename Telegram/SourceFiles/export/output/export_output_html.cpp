/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_html.h"

#include "core/utils.h"
#include "countries/countries_instance.h"
#include "export/data/export_data_types.h"
#include "export/output/export_output_result.h"
#include "ui/text/format_values.h"

#include <cmath>

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QSize>
#include <QtCore/QUrl>
#include <QtGui/QImageReader>

namespace Export {
namespace Output {
namespace {

constexpr auto kMessagesInFile = 1000;
constexpr auto kPersonalUserpicSize = 90;
constexpr auto kEntryUserpicSize = 48;
constexpr auto kServiceMessagePhotoSize = 60;
constexpr auto kHistoryUserpicSize = 42;
constexpr auto kSavedMessagesColorIndex = uint8(3);
constexpr auto kJoinWithinSeconds = 900;
constexpr auto kPhotoMaxWidth = 520;
constexpr auto kPhotoMaxHeight = 520;
constexpr auto kPhotoMinWidth = 80;
constexpr auto kPhotoMinHeight = 80;
constexpr auto kStickerMaxWidth = 384;
constexpr auto kStickerMaxHeight = 384;
constexpr auto kStickerMinWidth = 80;
constexpr auto kStickerMinHeight = 80;
constexpr auto kStoryThumbWidth = 45;
constexpr auto kStoryThumbHeight = 80;

constexpr auto kChatsPriority = 0;
constexpr auto kContactsPriority = 2;
constexpr auto kFrequentContactsPriority = 3;
constexpr auto kUserpicsPriority = 4;
constexpr auto kStoriesPriority = 5;
constexpr auto kProfileMusicPriority = 6;
constexpr auto kSessionsPriority = 7;
constexpr auto kWebSessionsPriority = 8;
constexpr auto kOtherPriority = 9;

const auto kLineBreak = QByteArrayLiteral("<br>");

using Context = details::HtmlContext;
using UserpicData = details::UserpicData;
using StoryData = details::StoryData;
using PeersMap = details::PeersMap;
using MediaData = details::MediaData;

bool IsGlobalLink(const QString &link) {
	return link.startsWith(u"http://"_q, Qt::CaseInsensitive)
		|| link.startsWith(u"https://"_q, Qt::CaseInsensitive);
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

Data::File RichFilePresentation(const Data::File *file) {
	using SkipReason = Data::File::SkipReason;

	auto result = file ? *file : Data::File();
	if (!result.relativePath.isEmpty()) {
		result.skipReason = SkipReason::None;
	} else {
		switch (result.skipReason) {
		case SkipReason::FileType:
		case SkipReason::FileSize:
			break;
		case SkipReason::None:
		case SkipReason::Unavailable:
		case SkipReason::DateLimits:
			result.skipReason = SkipReason::Unavailable;
			break;
		}
	}
	return result;
}

Data::Photo RichPhotoPresentation(const Data::Photo *photo) {
	auto result = photo ? *photo : Data::Photo();
	result.image.file = RichFilePresentation(
		photo ? &photo->image.file : nullptr);
	return result;
}

Data::Document RichDocumentPresentation(const Data::Document *document) {
	auto result = document ? *document : Data::Document();
	result.file = RichFilePresentation(document ? &document->file : nullptr);
	result.thumb.file = RichFilePresentation(
		document ? &document->thumb.file : nullptr);
	return result;
}

QByteArray RichFileDescription(const Data::File *file) {
	return NoFileDescription(RichFilePresentation(file).skipReason);
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

QByteArray FormatCustomEmoji(
		const Data::Utf8String &custom_emoji,
		const QByteArray &text,
		const QString &relativeLinkBase) {
	return (custom_emoji.isEmpty()
		? "<a href=\"\" onclick=\"return ShowNotLoadedEmoji();\">"
		: (custom_emoji == Data::TextPart::UnavailableEmoji())
		? "<a href=\"\" onclick=\"return ShowNotAvailableEmoji();\">"
		: ("<a href = \""
			+ (relativeLinkBase + custom_emoji).toUtf8()
			+ "\">"))
		+ text
		+ "</a>";
}

QByteArray FormatText(
		const std::vector<Data::TextPart> &data,
		const QString &internalLinksDomain,
		const QString &relativeLinkBase) {
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
		case Type::Spoiler: return "<span class=\"spoiler hidden\" "
			"onclick=\"ShowSpoiler(this)\">"
			"<span aria-hidden=\"true\">"
			+ text + "</span></span>";
		case Type::CustomEmoji:
			return FormatCustomEmoji(part.additional, text, relativeLinkBase);
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
	return QDateTime::fromSecsSinceEpoch(date).date()
		!= QDateTime::fromSecsSinceEpoch(previousDate).date();
}

QByteArray FormatDateText(TimeId date) {
	const auto parsed = QDateTime::fromSecsSinceEpoch(date).date();
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
	const auto parsed = QDateTime::fromSecsSinceEpoch(date).time();
	return Data::NumberToString(parsed.hour(), 2)
		+ ':'
		+ Data::NumberToString(parsed.minute(), 2);
}

} // namespace

namespace details {

struct UserpicData {
	uint8 colorIndex = 0;
	int pixelSize = 0;
	QString imageLink;
	QString largeLink;
	QByteArray firstName;
	QByteArray lastName;
	QByteArray tooltip;
};

struct StoryData {
	QString imageLink;
	QString largeLink;
};

class PeersMap {
public:
	using Peer = Data::Peer;
	using User = Data::User;
	using Chat = Data::Chat;

	PeersMap(const std::map<PeerId, Peer> &data);

	const Peer &peer(PeerId peerId) const;
	const User &user(UserId userId) const;

	QByteArray wrapPeerName(PeerId peerId) const;
	QByteArray wrapUserName(UserId userId) const;
	QByteArray wrapUserNames(const std::vector<UserId> &data) const;

private:
	const std::map<PeerId, Data::Peer> &_data;

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

auto PeersMap::user(UserId userId) const -> const User & {
	if (const auto result = peer(peerFromUser(userId)).user()) {
		return *result;
	}
	static auto empty = User();
	return empty;
}

QByteArray PeersMap::wrapPeerName(PeerId peerId) const {
	const auto result = peer(peerId).name();
	return result.isEmpty()
		? QByteArray("Deleted")
		: SerializeString(result);
}

QByteArray PeersMap::wrapUserName(UserId userId) const {
	const auto result = user(userId).name();
	return result.isEmpty()
		? QByteArray("Deleted Account")
		: SerializeString(result);
}

QByteArray PeersMap::wrapUserNames(const std::vector<UserId> &data) const {
	auto list = std::vector<QByteArray>();
	for (const auto &userId : data) {
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

namespace {

MediaData PrepareAudioMediaData(const Data::Document &data) {
	const auto hasFile = !data.file.relativePath.isEmpty();
	auto result = MediaData();
	result.title = (!data.songPerformer.isEmpty()
		&& !data.songTitle.isEmpty())
		? (data.songPerformer + " \xe2\x80\x93 " + data.songTitle)
		: !data.name.isEmpty()
		? data.name
		: QByteArray("Audio file");
	result.status = Data::FormatDuration(data.duration);
	if (!hasFile) {
		result.status += ", " + Data::FormatFileSize(data.file.size);
	}
	result.classes = "media_audio_file";
	result.link = data.file.relativePath;
	result.description = NoFileDescription(data.file.skipReason);
	return result;
}

struct RichMediaCallbacks {
	Fn<QByteArray(const Data::Photo*)> photo = {};
	Fn<QByteArray(const Data::Document*)> video = {};
	Fn<QByteArray(const Data::Document*)> audio = {};
	Fn<QByteArray(const MediaData&)> generic = {};
	Fn<QByteArray(const MediaData&, const Data::Photo*)> photoCard = {};
};

enum class RichSourceMetaKind {
	Source,
	EmbedPost,
};

enum class RichTailState {
	Empty,
	Media,
	Other,
};

constexpr auto kRichTargetMaxBytes = 4096;
constexpr auto kRichAnchorDirectBytes = 72;
constexpr auto kRichAnchorPrefixBytes = 48;
constexpr auto kRichTableSpanMax = 1000;
constexpr auto kRichHashtagMinLength = 2;
constexpr auto kRichHashtagMaxLength = 64;
constexpr auto kRichCommandMaxLength = 64;
constexpr auto kRichCashtagMaxLength = 8;
constexpr auto kRichUsernameMinLength = 5;
constexpr auto kRichUsernameMaxLength = 32;

class RichHtmlRenderer {
public:
	RichHtmlRenderer(
		Context &context,
		const Data::RichMessage &message,
		int messageId,
		const QString &internalLinksDomain,
		const QByteArray &relativeBase,
		const RichMediaCallbacks &media);

	[[nodiscard]] QByteArray renderMessage();

private:
	[[nodiscard]] const Data::Photo *findPhoto(uint64 id) const;
	[[nodiscard]] const Data::Document *findDocument(uint64 id) const;
	[[nodiscard]] RichTailState tailState(
		const Data::RichBlock &block) const;
	[[nodiscard]] RichTailState tailState(
		const std::vector<Data::RichBlock> &blocks) const;
	[[nodiscard]] QByteArray renderTexts(
		const std::vector<Data::RichText> &texts);
	[[nodiscard]] QByteArray renderText(const Data::RichText &text);
	[[nodiscard]] QByteArray renderTextChildren(const Data::RichText &text);
	[[nodiscard]] QByteArray renderTextLink(
		const Data::RichText &text,
		const std::optional<QByteArray> &href,
		std::map<QByteArray, QByteArray> attributes);
	[[nodiscard]] QByteArray renderCustomEmojiLink(
		const QByteArray &alt,
		std::map<QByteArray, QByteArray> attributes);
	[[nodiscard]] QByteArray renderBlocks(
		const std::vector<Data::RichBlock> &blocks);
	[[nodiscard]] QByteArray renderBlock(const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderPhoto(const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderVideo(const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderAudio(const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderMediaGroup(
		const Data::RichBlock &block,
		const QByteArray &kind,
		bool renderGroupCaption);
	[[nodiscard]] QByteArray renderEmbed(const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderEmbedPost(const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderChannel(const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderMap(
		const Data::RichBlock &block,
		const QByteArray &kind);
	[[nodiscard]] QByteArray renderRelatedArticles(
		const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderRelatedArticle(
		const Data::RichRelatedArticle &article,
		int index);
	[[nodiscard]] QByteArray renderCaption(
		const Data::RichCaption &caption);
	[[nodiscard]] QByteArray renderSourceMeta(
		const QByteArray &source,
		RichSourceMetaKind kind);
	[[nodiscard]] QByteArray renderTextBlock(
		const QByteArray &tag,
		std::map<QByteArray, QByteArray> attributes,
		const Data::RichText &text);
	[[nodiscard]] QByteArray renderAuthorDate(
		const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderCode(const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderList(const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderListItem(
		const Data::RichListItem &item,
		bool ordered);
	[[nodiscard]] QByteArray renderQuote(const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderDetails(const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderTable(const Data::RichBlock &block);
	[[nodiscard]] QByteArray renderTableRow(
		const Data::RichTableRow &row);
	[[nodiscard]] QByteArray renderTableCell(
		const Data::RichTableCell &cell);
	[[nodiscard]] QByteArray renderFallback(
		const QByteArray &kind,
		const QByteArray &label);

	void collectAnchors();
	void collectTextAnchors(const Data::RichText &text);
	void collectTextAnchors(const std::vector<Data::RichText> &texts);
	void collectCaptionAnchors(const Data::RichCaption &caption);
	void collectBlockAnchors(const Data::RichBlock &block);
	void collectBlockAnchors(const std::vector<Data::RichBlock> &blocks);
	[[nodiscard]] QByteArray registerAnchor(const QByteArray &name);
	[[nodiscard]] std::optional<QByteArray> fragmentHref(
		const QByteArray &target) const;
	[[nodiscard]] std::optional<QByteArray> textAnchorId(
		const Data::RichText &text) const;
	[[nodiscard]] std::optional<QByteArray> blockAnchorId(
		const Data::RichBlock &block) const;

	Context &_context;
	const Data::RichMessage &_message;
	int _messageId = 0;
	bool _linkActive = false;
	const QString &_internalLinksDomain;
	const QByteArray &_relativeBase;
	const RichMediaCallbacks &_media;
	std::map<QByteArray, QByteArray> _firstAnchorIds;
	std::map<QByteArray, int> _anchorOccurrences;
	std::map<const Data::RichText*, QByteArray> _textAnchorIds;
	std::map<const Data::RichBlock*, QByteArray> _blockAnchorIds;

};

std::map<QByteArray, QByteArray> RichBlockAttributes(
		const QByteArray &classes,
		const QByteArray &kind) {
	return {
		{ "class", QByteArray("rich_block ") + classes },
		{ "data-rich-kind", kind },
	};
}

QByteArray RichHeadingTag(int level) {
	switch (level) {
	case 1: return "h1";
	case 2: return "h2";
	case 3: return "h3";
	case 4: return "h4";
	case 5: return "h5";
	case 6: return "h6";
	}
	return "h6";
}

QByteArray RichBoolAttribute(bool value) {
	return value ? QByteArray("true") : QByteArray("false");
}

bool RichTextHasOutput(const Data::RichText &text) {
	using Type = Data::RichText::Type;
	switch (text.type) {
	case Type::Empty:
		return false;
	case Type::Plain:
		return !text.text.isEmpty();
	case Type::Concat:
		for (const auto &child : text.children) {
			if (RichTextHasOutput(child)) {
				return true;
			}
		}
		return false;
	case Type::Bold:
	case Type::Italic:
	case Type::Underline:
	case Type::Strike:
	case Type::Fixed:
	case Type::Url:
	case Type::Email:
	case Type::Phone:
	case Type::Subscript:
	case Type::Superscript:
	case Type::Marked:
	case Type::Anchor:
	case Type::Math:
	case Type::CustomEmoji:
	case Type::Spoiler:
	case Type::Mention:
	case Type::Hashtag:
	case Type::BotCommand:
	case Type::Cashtag:
	case Type::AutoUrl:
	case Type::AutoEmail:
	case Type::AutoPhone:
	case Type::BankCard:
	case Type::MentionName:
	case Type::FormattedDate:
	case Type::InlineImage:
	case Type::Diff:
		return true;
	}
	Unexpected("Type in RichTextHasOutput.");
}

bool RichCaptionHasOutput(const Data::RichCaption &caption) {
	return RichTextHasOutput(caption.text)
		|| RichTextHasOutput(caption.credit);
}

int HexValue(char value) {
	if (value >= '0' && value <= '9') {
		return value - '0';
	} else if (value >= 'a' && value <= 'f') {
		return value - 'a' + 10;
	} else if (value >= 'A' && value <= 'F') {
		return value - 'A' + 10;
	}
	return -1;
}

bool HasEncodedControl(const QByteArray &value) {
	for (auto i = 0; i + 2 < value.size(); ++i) {
		if (value[i] != '%') {
			continue;
		}
		const auto high = HexValue(value[i + 1]);
		const auto low = HexValue(value[i + 2]);
		if (high < 0 || low < 0) {
			continue;
		}
		const auto decoded = (high << 4) | low;
		if (decoded < 32 || decoded == 127) {
			return true;
		}
	}
	return false;
}

bool IsStrictTarget(const QByteArray &value) {
	if (value.isEmpty() || value.size() > kRichTargetMaxBytes) {
		return false;
	}
	for (const auto character : value) {
		const auto byte = static_cast<uchar>(character);
		if (byte < 32 || byte == 127) {
			return false;
		}
	}
	const auto decoded = QString::fromUtf8(value);
	if (decoded.toUtf8() != value || decoded.trimmed() != decoded) {
		return false;
	}
	for (const auto character : decoded) {
		const auto category = character.category();
		if (category == QChar::Other_Control
			|| category == QChar::Separator_Line
			|| category == QChar::Separator_Paragraph) {
			return false;
		}
	}
	return true;
}

std::optional<QByteArray> SafeHttpHref(const QByteArray &value) {
	if (!IsStrictTarget(value)
		|| value.startsWith("//")
		|| HasEncodedControl(value)) {
		return std::nullopt;
	}
	const auto url = QUrl::fromEncoded(value, QUrl::StrictMode);
	const auto scheme = url.scheme();
	if (!url.isValid()
		|| url.isRelative()
		|| url.host().isEmpty()
		|| (scheme.compare(u"http"_q, Qt::CaseInsensitive) != 0
			&& scheme.compare(u"https"_q, Qt::CaseInsensitive) != 0)) {
		return std::nullopt;
	}
	const auto result = url.toEncoded(QUrl::FullyEncoded);
	return (IsStrictTarget(result) && !HasEncodedControl(result))
		? std::make_optional(result)
		: std::nullopt;
}

std::optional<QByteArray> SafeEmailHref(const QByteArray &address) {
	if (!IsStrictTarget(address)) {
		return std::nullopt;
	}
	const auto separator = address.indexOf('@');
	if (separator <= 0
		|| separator != address.lastIndexOf('@')
		|| separator + 1 == address.size()) {
		return std::nullopt;
	}
	for (const auto character : QString::fromUtf8(address)) {
		if (character.isSpace()) {
			return std::nullopt;
		}
	}
	for (const auto forbidden : QByteArray("/\\:%?#&=")) {
		if (address.contains(forbidden)) {
			return std::nullopt;
		}
	}
	const auto result = QByteArray("mailto:") + address;
	return IsStrictTarget(result)
		? std::make_optional(result)
		: std::nullopt;
}

std::optional<QByteArray> SafePhoneHref(const QByteArray &phone) {
	if (!IsStrictTarget(phone)) {
		return std::nullopt;
	}
	auto hasDigit = false;
	for (const auto character : phone) {
		if (character >= '0' && character <= '9') {
			hasDigit = true;
		} else if (character != '+'
			&& character != '-'
			&& character != ' '
			&& character != '.'
			&& character != '('
			&& character != ')') {
			return std::nullopt;
		}
	}
	const auto result = QByteArray("tel:") + phone;
	return (hasDigit && IsStrictTarget(result))
		? std::make_optional(result)
		: std::nullopt;
}

std::optional<QByteArray> SafeMentionHref(
		const QByteArray &mention,
		const QString &internalLinksDomain) {
	if (!IsStrictTarget(mention)
		|| mention.size() < 2
		|| mention.front() != '@') {
		return std::nullopt;
	}
	const auto username = mention.mid(1);
	for (const auto character : username) {
		if ((character < 'a' || character > 'z')
			&& (character < 'A' || character > 'Z')
			&& (character < '0' || character > '9')
			&& character != '_') {
			return std::nullopt;
		}
	}
	return SafeHttpHref(internalLinksDomain.toUtf8() + username);
}

bool IsAsciiWord(
		const QByteArray &value,
		int minimumLength,
		int maximumLength) {
	if (value.size() < minimumLength || value.size() > maximumLength) {
		return false;
	}
	for (const auto character : value) {
		if ((character < 'a' || character > 'z')
			&& (character < 'A' || character > 'Z')
			&& (character < '0' || character > '9')
			&& character != '_') {
			return false;
		}
	}
	return true;
}

QByteArray RichChannelSourceName(Data::RichChannel::Source source) {
	using Source = Data::RichChannel::Source;
	switch (source) {
	case Source::ChatEmpty: return "chat_empty";
	case Source::Chat: return "chat";
	case Source::ChatForbidden: return "chat_forbidden";
	case Source::Channel: return "channel";
	case Source::ChannelForbidden: return "channel_forbidden";
	case Source::Community: return "community";
	case Source::CommunityForbidden: return "community_forbidden";
	}
	Unexpected("Source in RichChannelSourceName.");
}

QByteArray RichChannelStatus(const Data::RichChannel &channel) {
	using Source = Data::RichChannel::Source;
	auto label = QByteArray();
	switch (channel.source) {
	case Source::ChatEmpty:
	case Source::Chat:
		label = "Chat";
		break;
	case Source::ChatForbidden:
		label = "Chat unavailable";
		break;
	case Source::Channel:
		label = "Channel";
		break;
	case Source::ChannelForbidden:
		label = "Channel unavailable";
		break;
	case Source::Community:
		label = "Community";
		break;
	case Source::CommunityForbidden:
		label = "Community unavailable";
		break;
	}
	if (label.isEmpty()) {
		Unexpected("Source in RichChannelStatus.");
	}
	return (channel.username && !channel.username->isEmpty())
		? ('@' + *channel.username + ", " + label)
		: label;
}

QByteArray RichMapPointSourceName(Data::RichMapPoint::Source source) {
	using Source = Data::RichMapPoint::Source;
	switch (source) {
	case Source::GeoPointEmpty: return "geo_point_empty";
	case Source::GeoPoint: return "geo_point";
	case Source::InputGeoPointEmpty: return "input_geo_point_empty";
	case Source::InputGeoPoint: return "input_geo_point";
	}
	Unexpected("Source in RichMapPointSourceName.");
}

struct RichMapCoordinateValues {
	QByteArray latitude;
	QByteArray longitude;
};

std::optional<RichMapCoordinateValues> RichMapCoordinates(
		const Data::RichMapPoint &point) {
	using Source = Data::RichMapPoint::Source;
	switch (point.source) {
	case Source::GeoPointEmpty:
	case Source::InputGeoPointEmpty:
		return std::nullopt;
	case Source::GeoPoint:
	case Source::InputGeoPoint:
		break;
	}
	if (!std::isfinite(point.latitude)
		|| !std::isfinite(point.longitude)
		|| point.latitude < -90.
		|| point.latitude > 90.
		|| point.longitude < -180.
		|| point.longitude > 180.) {
		return std::nullopt;
	}
	return RichMapCoordinateValues{
		Data::NumberToString(point.latitude),
		Data::NumberToString(point.longitude),
	};
}

bool IsUnicodeHashtagWord(const QByteArray &value) {
	const auto characters = QString::fromUtf8(value).toUcs4();
	if (characters.size() < kRichHashtagMinLength
		|| characters.size() > kRichHashtagMaxLength) {
		return false;
	}
	auto allDecimalDigits = true;
	for (const auto character : characters) {
		const auto category = QChar::category(character);
		const auto isMark = (category >= QChar::Mark_NonSpacing)
			&& (category <= QChar::Mark_Enclosing);
		if (!QChar::isLetterOrNumber(character)
			&& category != QChar::Punctuation_Connector
			&& !isMark) {
			return false;
		}
		allDecimalDigits = allDecimalDigits && QChar::isDigit(character);
	}
	return !allDecimalDigits;
}

bool IsHashtagAction(const QByteArray &value) {
	const auto separator = value.indexOf('@');
	const auto hashtag = (separator < 0) ? value : value.left(separator);
	if (!IsUnicodeHashtagWord(hashtag)) {
		return false;
	}
	return (separator < 0)
		|| (separator == value.lastIndexOf('@')
			&& IsAsciiWord(
				value.mid(separator + 1),
				1,
				kRichUsernameMaxLength));
}

bool IsBotCommandAction(const QByteArray &value) {
	const auto separator = value.indexOf('@');
	const auto command = (separator < 0) ? value : value.left(separator);
	if (!IsAsciiWord(command, 1, kRichCommandMaxLength)) {
		return false;
	}
	return (separator < 0)
		|| (separator == value.lastIndexOf('@')
			&& IsAsciiWord(
				value.mid(separator + 1),
				kRichUsernameMinLength,
				kRichUsernameMaxLength));
}

bool IsCashtagAction(const QByteArray &value) {
	if (value.isEmpty() || value.size() > kRichCashtagMaxLength) {
		return false;
	}
	for (const auto character : value) {
		if (character < 'A' || character > 'Z') {
			return false;
		}
	}
	return true;
}

bool IsValidActionValue(const QByteArray &value, char prefix) {
	switch (prefix) {
	case '#': return IsHashtagAction(value);
	case '/': return IsBotCommandAction(value);
	case '$': return IsCashtagAction(value);
	}
	return false;
}

std::optional<QByteArray> SafeActionData(
		const QByteArray &target,
		char prefix) {
	if (!IsStrictTarget(target)
		|| target.front() != prefix
		|| target.size() < 2) {
		return std::nullopt;
	}
	const auto result = target.mid(1);
	return (IsValidActionValue(result, prefix) && IsStrictTarget(result))
		? std::make_optional(result)
		: std::nullopt;
}

bool IsAsciiDecimal(const QByteArray &value) {
	if (value.isEmpty()) {
		return false;
	}
	for (const auto character : value) {
		if (character < '0' || character > '9') {
			return false;
		}
	}
	return true;
}

std::optional<QByteArray> SafeRelativeEmojiHref(
		const QByteArray &path,
		const QByteArray &relativeBase) {
	if (!IsStrictTarget(path)
		|| path.startsWith('/')
		|| path.contains('\\')
		|| path.contains(':')
		|| path.contains('%')
		|| path.contains('?')
		|| path.contains('#')
		|| path == Data::TextPart::UnavailableEmoji()
		|| IsAsciiDecimal(path)) {
		return std::nullopt;
	}
	for (const auto &component : path.split('/')) {
		if (component.isEmpty() || component == "." || component == "..") {
			return std::nullopt;
		}
	}
	const auto result = relativeBase + path;
	return IsStrictTarget(result)
		? std::make_optional(result)
		: std::nullopt;
}

bool AppendPlainTarget(
		QByteArray &result,
		const QByteArray &value) {
	if (result.size() > kRichTargetMaxBytes
		|| value.size() > kRichTargetMaxBytes - result.size()) {
		return false;
	}
	result.append(value);
	return true;
}

bool AppendPlainTarget(
		QByteArray &result,
		const std::vector<Data::RichText> &texts);

bool AppendPlainTarget(
		QByteArray &result,
		const Data::RichText &text) {
	using Type = Data::RichText::Type;
	switch (text.type) {
	case Type::Empty:
		return true;
	case Type::Plain:
		return AppendPlainTarget(result, text.text);
	case Type::Concat:
	case Type::Bold:
	case Type::Italic:
	case Type::Underline:
	case Type::Strike:
	case Type::Fixed:
	case Type::Url:
	case Type::Email:
	case Type::Phone:
	case Type::Subscript:
	case Type::Superscript:
	case Type::Marked:
	case Type::Anchor:
	case Type::Spoiler:
	case Type::Mention:
	case Type::Hashtag:
	case Type::BotCommand:
	case Type::Cashtag:
	case Type::AutoUrl:
	case Type::AutoEmail:
	case Type::AutoPhone:
	case Type::BankCard:
	case Type::MentionName:
	case Type::FormattedDate:
		return AppendPlainTarget(result, text.children);
	case Type::CustomEmoji:
		return AppendPlainTarget(result, text.text);
	case Type::Diff:
		return AppendPlainTarget(result, text.children);
	case Type::Math:
	case Type::InlineImage:
		return false;
	}
	return false;
}

bool AppendPlainTarget(
		QByteArray &result,
		const std::vector<Data::RichText> &texts) {
	for (const auto &text : texts) {
		if (!AppendPlainTarget(result, text)) {
			return false;
		}
	}
	return true;
}

std::optional<QByteArray> PlainTarget(const Data::RichText &text) {
	auto result = QByteArray();
	return AppendPlainTarget(result, text)
		? std::make_optional(result)
		: std::nullopt;
}

QByteArray AnchorToken(const QByteArray &name) {
	if (name.isEmpty()) {
		return "empty";
	}
	const auto encoded = [](const QByteArray &value) {
		return value.toBase64(
			QByteArray::Base64UrlEncoding
			| QByteArray::OmitTrailingEquals);
	};
	if (name.size() <= kRichAnchorDirectBytes) {
		return encoded(name);
	}
	const auto hash = hashSha256(name.data(), int(name.size()));
	return encoded(name.left(kRichAnchorPrefixBytes))
		+ '-'
		+ QByteArray(hash.data(), int(hash.size())).toHex().left(16);
}

RichHtmlRenderer::RichHtmlRenderer(
	Context &context,
	const Data::RichMessage &message,
	int messageId,
	const QString &internalLinksDomain,
	const QByteArray &relativeBase,
	const RichMediaCallbacks &media)
: _context(context)
, _message(message)
, _messageId(messageId)
, _internalLinksDomain(internalLinksDomain)
, _relativeBase(relativeBase)
, _media(media) {
}

const Data::Photo *RichHtmlRenderer::findPhoto(uint64 id) const {
	if (!id) {
		return nullptr;
	}
	const auto i = _message.photos.find(id);
	return (i != end(_message.photos)) ? &i->second : nullptr;
}

const Data::Document *RichHtmlRenderer::findDocument(uint64 id) const {
	if (!id) {
		return nullptr;
	}
	const auto i = _message.documents.find(id);
	return (i != end(_message.documents)) ? &i->second : nullptr;
}

RichTailState RichHtmlRenderer::tailState(
		const std::vector<Data::RichBlock> &blocks) const {
	for (auto i = blocks.rbegin(); i != blocks.rend(); ++i) {
		const auto state = tailState(*i);
		if (state != RichTailState::Empty) {
			return state;
		}
	}
	return RichTailState::Empty;
}

RichTailState RichHtmlRenderer::tailState(
		const Data::RichBlock &block) const {
	using Content = Data::RichListItemContent;
	using Kind = Data::RichBlock::Kind;
	using QuoteContent = Data::RichQuoteContent;

	switch (block.kind) {
	case Kind::Unsupported:
	case Kind::Heading:
	case Kind::Paragraph:
	case Kind::Footer:
	case Kind::Thinking:
	case Kind::AuthorDate:
	case Kind::Code:
	case Kind::Divider:
	case Kind::Math:
	case Kind::Table:
	case Kind::Unknown:
		return RichTailState::Other;
	case Kind::Anchor:
		return RichTailState::Empty;
	case Kind::List: {
		if (block.listItems.empty()) {
			return RichTailState::Empty;
		}
		const auto &item = block.listItems.back();
		if (item.content == Content::Blocks) {
			const auto state = tailState(item.blocks);
			if (state != RichTailState::Empty) {
				return state;
			}
		}
		return RichTailState::Other;
	}
	case Kind::Quote:
		if (RichTextHasOutput(block.quoteCaption)) {
			return RichTailState::Other;
		}
		if (block.quoteContent == QuoteContent::Blocks) {
			const auto state = tailState(block.blocks);
			if (state != RichTailState::Empty) {
				return state;
			}
		}
		return RichTailState::Other;
	case Kind::Photo:
		return (RichCaptionHasOutput(block.caption)
			|| (block.optionalUrl && !block.optionalUrl->isEmpty()))
			? RichTailState::Other
			: RichTailState::Media;
	case Kind::Video:
	case Kind::Audio:
	case Kind::Map:
	case Kind::InputMap:
		return RichCaptionHasOutput(block.caption)
			? RichTailState::Other
			: RichTailState::Media;
	case Kind::Cover:
		return tailState(block.blocks);
	case Kind::Embed:
		return (RichCaptionHasOutput(block.caption)
			|| (block.posterPhotoId
				&& block.optionalUrl
				&& !block.optionalUrl->isEmpty()))
			? RichTailState::Other
			: RichTailState::Media;
	case Kind::EmbedPost: {
		if (RichCaptionHasOutput(block.caption)) {
			return RichTailState::Other;
		}
		const auto state = tailState(block.blocks);
		if (state != RichTailState::Empty) {
			return state;
		}
		return block.url.isEmpty()
			? RichTailState::Media
			: RichTailState::Other;
	}
	case Kind::Collage:
	case Kind::Slideshow:
		return RichCaptionHasOutput(block.caption)
			? RichTailState::Other
			: tailState(block.blocks);
	case Kind::Channel:
		return RichTailState::Media;
	case Kind::Details:
		if (block.open) {
			const auto state = tailState(block.blocks);
			if (state != RichTailState::Empty) {
				return state;
			}
		}
		return RichTailState::Other;
	case Kind::RelatedArticles:
		if (block.relatedArticles.empty()) {
			return RichTextHasOutput(block.text)
				? RichTailState::Other
				: RichTailState::Empty;
		} else {
			const auto &article = block.relatedArticles.back();
			if (!article.url.isEmpty()) {
				return RichTailState::Other;
			}
			const auto description = article.description
				? *article.description
				: QByteArray();
			if (article.photoId && !description.isEmpty()) {
				const auto photo = findPhoto(*article.photoId);
				if (!RichFileDescription(
						photo ? &photo->image.file : nullptr).isEmpty()) {
					return RichTailState::Other;
				}
			}
			return RichTailState::Media;
		}
	}
	Unexpected("Kind in RichHtmlRenderer::tailState.");
}

QByteArray RichHtmlRenderer::renderMessage() {
	collectAnchors();
	auto result = _context.pushTag(
		"div",
		{
			{ "class", "text rich_message" },
			{ "data-rich-message", Data::NumberToString(_messageId) },
			{ "data-rich-part", RichBoolAttribute(_message.part) },
			{ "dir", _message.rtl
				? QByteArray("rtl")
				: QByteArray("auto") },
			{ "inline", QByteArray() },
		});
	result += renderBlocks(_message.blocks);
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderCaption(
		const Data::RichCaption &caption) {
	const auto text = renderText(caption.text);
	const auto credit = renderText(caption.credit);
	if (text.isEmpty() && credit.isEmpty()) {
		return QByteArray();
	}
	auto result = _context.pushTag("figcaption", {
		{ "class", "rich_media_caption" },
		{ "dir", "auto" },
	});
	if (!text.isEmpty()) {
		result += _context.pushTag("div", {
			{ "class", "rich_caption_text" },
		});
		result += text;
		result += _context.popTag();
	}
	if (!credit.isEmpty()) {
		result += _context.pushTag("cite", {
			{ "class", "rich_caption_credit" },
		});
		result += credit;
		result += _context.popTag();
	}
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderSourceMeta(
		const QByteArray &source,
		RichSourceMetaKind kind) {
	if (source.isEmpty()) {
		return QByteArray();
	}
	const auto containerClass = [kind] {
		switch (kind) {
		case RichSourceMetaKind::Source:
			return QByteArray("rich_source_meta");
		case RichSourceMetaKind::EmbedPost:
			return QByteArray("rich_embed_post_meta");
		}
		Unexpected("Kind in RichHtmlRenderer::renderSourceMeta.");
	}();
	auto result = _context.pushTag("div", {
		{ "class", containerClass },
	});
	const auto href = SafeHttpHref(source);
	if (href && !_linkActive) {
		result += _context.pushTag("a", {
			{ "class", "rich_source_link" },
			{ "href", *href },
			{ "inline", QByteArray() },
		});
		const auto previous = _linkActive;
		_linkActive = true;
		const auto guard = gsl::finally([&] { _linkActive = previous; });
		result += SerializeString(source);
		result += _context.popTag();
	} else {
		result += _context.pushTag("span", {
			{ "class", "rich_source_text" },
			{ "inline", QByteArray() },
		});
		result += SerializeString(source);
		result += _context.popTag();
	}
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderPhoto(
		const Data::RichBlock &block) {
	auto attributes = RichBlockAttributes("rich_media_item", "photo");
	attributes.emplace(
		"data-photo-id",
		Data::NumberToString(block.photoId));
	attributes.emplace("data-spoiler", RichBoolAttribute(block.spoiler));
	if (block.optionalWebpageId) {
		attributes.emplace(
			"data-webpage-id",
			Data::NumberToString(*block.optionalWebpageId));
	}
	auto result = _context.pushTag("figure", std::move(attributes));
	result += _media.photo(findPhoto(block.photoId));
	if (block.optionalUrl) {
		result += renderSourceMeta(
			*block.optionalUrl,
			RichSourceMetaKind::Source);
	}
	result += renderCaption(block.caption);
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderVideo(
		const Data::RichBlock &block) {
	auto attributes = RichBlockAttributes("rich_media_item", "video");
	attributes.emplace(
		"data-document-id",
		Data::NumberToString(block.documentId));
	attributes.emplace("data-autoplay", RichBoolAttribute(block.autoplay));
	attributes.emplace("data-loop", RichBoolAttribute(block.loop));
	attributes.emplace("data-spoiler", RichBoolAttribute(block.spoiler));
	auto result = _context.pushTag("figure", std::move(attributes));
	result += _media.video(findDocument(block.documentId));
	result += renderCaption(block.caption);
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderAudio(
		const Data::RichBlock &block) {
	auto attributes = RichBlockAttributes("rich_media_item", "audio");
	attributes.emplace(
		"data-document-id",
		Data::NumberToString(block.documentId));
	auto result = _context.pushTag("figure", std::move(attributes));
	result += _media.audio(findDocument(block.documentId));
	result += renderCaption(block.caption);
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderMediaGroup(
		const Data::RichBlock &block,
		const QByteArray &kind,
		bool renderGroupCaption) {
	auto attributes = RichBlockAttributes("rich_media_group", kind);
	attributes.emplace(
		"data-rich-has-items",
		RichBoolAttribute(!block.blocks.empty()));
	attributes.emplace(
		"data-rich-items-end-media",
		RichBoolAttribute(
			tailState(block.blocks) == RichTailState::Media));
	auto result = _context.pushTag("section", std::move(attributes));
	result += _context.pushTag("div", {
		{ "class", "rich_media_group_items" },
	});
	result += renderBlocks(block.blocks);
	result += _context.popTag();
	if (renderGroupCaption) {
		result += renderCaption(block.caption);
	}
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderEmbed(
		const Data::RichBlock &block) {
	const auto hasPoster = block.posterPhotoId.has_value();
	const auto hasMeta = hasPoster
		&& block.optionalUrl
		&& !block.optionalUrl->isEmpty();
	auto attributes = RichBlockAttributes(
		"rich_media_item rich_embed",
		"embed");
	attributes.emplace(
		"data-full-width",
		RichBoolAttribute(block.fullWidth));
	attributes.emplace(
		"data-allow-scrolling",
		RichBoolAttribute(block.allowScrolling));
	attributes.emplace("data-rich-has-meta", RichBoolAttribute(hasMeta));
	if (block.posterPhotoId) {
		attributes.emplace(
			"data-poster-photo-id",
			Data::NumberToString(*block.posterPhotoId));
	}
	if (block.width) {
		attributes.emplace(
			"data-source-width",
			Data::NumberToString(*block.width));
	}
	if (block.height) {
		attributes.emplace(
			"data-source-height",
			Data::NumberToString(*block.height));
	}
	auto result = _context.pushTag("figure", std::move(attributes));
	if (hasPoster) {
		result += _media.photo(findPhoto(*block.posterPhotoId));
		if (block.optionalUrl) {
			result += renderSourceMeta(
				*block.optionalUrl,
				RichSourceMetaKind::Source);
		}
	} else {
		auto media = MediaData();
		media.title = "Embed";
		media.classes = "media_file";
		if (block.optionalUrl) {
			media.status = *block.optionalUrl;
			if (const auto href = SafeHttpHref(*block.optionalUrl)) {
				media.link = QString::fromUtf8(*href);
			}
		}
		result += _media.generic(media);
	}
	result += renderCaption(block.caption);
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderEmbedPost(
		const Data::RichBlock &block) {
	const auto photo = findPhoto(block.authorPhotoId);
	const auto hasMeta = !block.url.isEmpty();
	auto attributes = RichBlockAttributes("rich_embed_post", "embed-post");
	attributes.emplace(
		"data-webpage-id",
		Data::NumberToString(block.webpageId));
	attributes.emplace(
		"data-author-photo-id",
		Data::NumberToString(block.authorPhotoId));
	attributes.emplace("data-date", Data::NumberToString(block.date));
	attributes.emplace("data-rich-has-meta", RichBoolAttribute(hasMeta));
	attributes.emplace(
		"data-rich-has-items",
		RichBoolAttribute(!block.blocks.empty()));
	attributes.emplace(
		"data-rich-items-end-media",
		RichBoolAttribute(
			tailState(block.blocks) == RichTailState::Media));
	auto result = _context.pushTag("section", std::move(attributes));
	result += _context.pushTag("div", {
		{ "class", "rich_embed_post_author" },
	});
	auto media = MediaData();
	media.title = block.author.isEmpty()
		? QByteArray("Embed post")
		: block.author;
	media.description = RichFileDescription(
		photo ? &photo->image.file : nullptr);
	media.status = block.date
		? Data::FormatDateTime(block.date)
		: QByteArray();
	media.classes = "media_contact";
	result += _media.photoCard(media, photo);
	result += _context.popTag();
	result += renderSourceMeta(block.url, RichSourceMetaKind::EmbedPost);
	result += _context.pushTag("div", {
		{ "class", "rich_embed_post_blocks" },
	});
	result += renderBlocks(block.blocks);
	result += _context.popTag();
	result += renderCaption(block.caption);
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderChannel(
		const Data::RichBlock &block) {
	const auto &channel = block.channel;
	auto attributes = RichBlockAttributes(
		"rich_reference_block",
		"channel");
	attributes.emplace(
		"data-channel-source",
		RichChannelSourceName(channel.source));
	attributes.emplace("data-channel-id", Data::NumberToString(channel.id));
	attributes.emplace("data-broadcast", RichBoolAttribute(channel.broadcast));
	attributes.emplace("data-megagroup", RichBoolAttribute(channel.megagroup));
	attributes.emplace("data-monoforum", RichBoolAttribute(channel.monoforum));
	if (channel.accessHash) {
		attributes.emplace(
			"data-access-hash",
			Data::NumberToString(*channel.accessHash));
	}
	auto result = _context.pushTag("section", std::move(attributes));
	auto media = MediaData();
	media.title = (channel.title && !channel.title->isEmpty())
		? *channel.title
		: QByteArray("Channel");
	media.status = RichChannelStatus(channel);
	media.classes = "media_contact";
	if (channel.username
		&& IsAsciiWord(
			*channel.username,
			kRichUsernameMinLength,
			kRichUsernameMaxLength)) {
		const auto target = _internalLinksDomain.toUtf8() + *channel.username;
		if (const auto href = SafeHttpHref(target)) {
			media.link = QString::fromUtf8(*href);
		}
	}
	result += _media.generic(media);
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderMap(
		const Data::RichBlock &block,
		const QByteArray &kind) {
	const auto &point = block.mapPoint;
	auto attributes = RichBlockAttributes("rich_reference_block", kind);
	attributes.emplace(
		"data-point-source",
		RichMapPointSourceName(point.source));
	attributes.emplace("data-zoom", Data::NumberToString(block.zoom));
	attributes.emplace(
		"data-source-width",
		Data::NumberToString(block.mapWidth));
	attributes.emplace(
		"data-source-height",
		Data::NumberToString(block.mapHeight));
	if (point.accessHash) {
		attributes.emplace(
			"data-access-hash",
			Data::NumberToString(*point.accessHash));
	}
	if (point.accuracyRadius) {
		attributes.emplace(
			"data-accuracy-radius",
			Data::NumberToString(*point.accuracyRadius));
	}
	auto result = _context.pushTag("figure", std::move(attributes));
	auto media = MediaData();
	media.title = "Location";
	media.classes = "media_location";
	if (const auto coordinates = RichMapCoordinates(point)) {
		const auto joined = coordinates->latitude
			+ ','
			+ coordinates->longitude;
		media.status = coordinates->latitude
			+ ", "
			+ coordinates->longitude;
		const auto target = QByteArray("https://maps.google.com/maps?q=")
			+ joined
			+ "&ll="
			+ joined
			+ "&z=16";
		if (const auto href = SafeHttpHref(target)) {
			media.link = QString::fromUtf8(*href);
		}
	}
	result += _media.generic(media);
	result += renderCaption(block.caption);
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderRelatedArticle(
		const Data::RichRelatedArticle &article,
		int index) {
	auto attributes = std::map<QByteArray, QByteArray>{
		{ "class", "rich_related_article" },
		{ "data-rich-article-index", Data::NumberToString(index) },
		{ "data-webpage-id", Data::NumberToString(article.webpageId) },
	};
	if (article.photoId) {
		attributes.emplace(
			"data-photo-id",
			Data::NumberToString(*article.photoId));
	}
	if (article.publishedDate) {
		attributes.emplace(
			"data-published-date",
			Data::NumberToString(*article.publishedDate));
	}
	auto result = _context.pushTag("article", std::move(attributes));
	auto media = MediaData();
	media.title = (article.title && !article.title->isEmpty())
		? *article.title
		: QByteArray("Related article");
	if (article.author && !article.author->isEmpty()) {
		media.status = *article.author;
	}
	const auto publishedDate = article.publishedDate
		? Data::FormatDateTime(*article.publishedDate)
		: QByteArray();
	if (!publishedDate.isEmpty()) {
		if (!media.status.isEmpty()) {
			media.status += ", ";
		}
		media.status += publishedDate;
	}
	const auto description = article.description
		? *article.description
		: QByteArray();
	auto displacedDescription = false;
	if (article.photoId) {
		const auto photo = findPhoto(*article.photoId);
		const auto fileDescription = RichFileDescription(
			photo ? &photo->image.file : nullptr);
		media.classes = "media_photo";
		media.description = fileDescription.isEmpty()
			? description
			: fileDescription;
		displacedDescription = !fileDescription.isEmpty()
			&& !description.isEmpty();
		result += _media.photoCard(media, photo);
	} else {
		media.classes = "media_file";
		media.description = description;
		result += _media.generic(media);
	}
	if (displacedDescription) {
		result += _context.pushTag("div", {
			{ "class", "rich_article_description" },
		});
		result += SerializeString(description);
		result += _context.popTag();
	}
	result += renderSourceMeta(article.url, RichSourceMetaKind::Source);
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderRelatedArticles(
		const Data::RichBlock &block) {
	const auto title = renderText(block.text);
	auto result = _context.pushTag(
		"section",
		RichBlockAttributes(
			"rich_related_articles",
			"related-articles"));
	if (!title.isEmpty()) {
		result += _context.pushTag("div", {
			{ "class", "rich_related_title" },
			{ "dir", "auto" },
		});
		result += title;
		result += _context.popTag();
	}
	result += _context.pushTag("div", {
		{ "class", "rich_related_items" },
	});
	auto index = 0;
	for (const auto &article : block.relatedArticles) {
		result += renderRelatedArticle(article, index++);
	}
	result += _context.popTag();
	result += _context.popTag();
	return result;
}

void RichHtmlRenderer::collectAnchors() {
	_firstAnchorIds.clear();
	_anchorOccurrences.clear();
	_textAnchorIds.clear();
	_blockAnchorIds.clear();
	collectBlockAnchors(_message.blocks);
}

QByteArray RichHtmlRenderer::registerAnchor(const QByteArray &name) {
	const auto base = QByteArray("rich-message-")
		+ Data::NumberToString(_messageId)
		+ "-anchor-"
		+ AnchorToken(name);
	auto &occurrence = _anchorOccurrences[base];
	++occurrence;
	const auto result = base + ((occurrence > 1)
		? ('-' + Data::NumberToString(occurrence))
		: QByteArray());
	_firstAnchorIds.emplace(name, result);
	return result;
}

void RichHtmlRenderer::collectTextAnchors(const Data::RichText &text) {
	using Type = Data::RichText::Type;
	switch (text.type) {
	case Type::Anchor:
		_textAnchorIds.emplace(&text, registerAnchor(text.data));
		collectTextAnchors(text.children);
		break;
	case Type::Concat:
	case Type::Bold:
	case Type::Italic:
	case Type::Underline:
	case Type::Strike:
	case Type::Fixed:
	case Type::Url:
	case Type::Email:
	case Type::Phone:
	case Type::Subscript:
	case Type::Superscript:
	case Type::Marked:
	case Type::Spoiler:
	case Type::Mention:
	case Type::Hashtag:
	case Type::BotCommand:
	case Type::Cashtag:
	case Type::AutoUrl:
	case Type::AutoEmail:
	case Type::AutoPhone:
	case Type::BankCard:
	case Type::MentionName:
	case Type::FormattedDate:
		collectTextAnchors(text.children);
		break;
	case Type::Diff:
		collectTextAnchors(text.children);
		collectTextAnchors(text.oldChildren);
		break;
	case Type::Empty:
	case Type::Plain:
	case Type::Math:
	case Type::CustomEmoji:
	case Type::InlineImage:
		break;
	}
}

void RichHtmlRenderer::collectTextAnchors(
		const std::vector<Data::RichText> &texts) {
	for (const auto &text : texts) {
		collectTextAnchors(text);
	}
}

void RichHtmlRenderer::collectCaptionAnchors(
		const Data::RichCaption &caption) {
	collectTextAnchors(caption.text);
	collectTextAnchors(caption.credit);
}

void RichHtmlRenderer::collectBlockAnchors(
		const std::vector<Data::RichBlock> &blocks) {
	for (const auto &block : blocks) {
		collectBlockAnchors(block);
	}
}

void RichHtmlRenderer::collectBlockAnchors(const Data::RichBlock &block) {
	using Content = Data::RichListItemContent;
	using Kind = Data::RichBlock::Kind;
	using QuoteContent = Data::RichQuoteContent;
	switch (block.kind) {
	case Kind::Heading:
	case Kind::Paragraph:
	case Kind::Footer:
	case Kind::Thinking:
	case Kind::AuthorDate:
	case Kind::Code:
		collectTextAnchors(block.text);
		break;
	case Kind::Anchor:
		_blockAnchorIds.emplace(&block, registerAnchor(block.name));
		break;
	case Kind::List:
		for (const auto &item : block.listItems) {
			switch (item.content) {
			case Content::Text:
				if (item.text) {
					collectTextAnchors(*item.text);
				}
				break;
			case Content::Blocks:
				collectBlockAnchors(item.blocks);
				break;
			}
		}
		break;
	case Kind::Quote:
		switch (block.quoteContent) {
		case QuoteContent::Text:
			collectTextAnchors(block.text);
			break;
		case QuoteContent::Blocks:
			collectBlockAnchors(block.blocks);
			break;
		}
		collectTextAnchors(block.quoteCaption);
		break;
	case Kind::Photo:
	case Kind::Video:
	case Kind::Audio:
	case Kind::Embed:
	case Kind::Map:
	case Kind::InputMap:
		collectCaptionAnchors(block.caption);
		break;
	case Kind::EmbedPost:
		collectBlockAnchors(block.blocks);
		collectCaptionAnchors(block.caption);
		break;
	case Kind::Cover:
		collectBlockAnchors(block.blocks);
		break;
	case Kind::Collage:
	case Kind::Slideshow:
		collectBlockAnchors(block.blocks);
		collectCaptionAnchors(block.caption);
		break;
	case Kind::Table:
		collectTextAnchors(block.text);
		for (const auto &row : block.tableRows) {
			for (const auto &cell : row.cells) {
				if (cell.text) {
					collectTextAnchors(*cell.text);
				}
			}
		}
		break;
	case Kind::Details:
		collectTextAnchors(block.text);
		collectBlockAnchors(block.blocks);
		break;
	case Kind::RelatedArticles:
		collectTextAnchors(block.text);
		break;
	case Kind::Unsupported:
	case Kind::Divider:
	case Kind::Channel:
	case Kind::Math:
	case Kind::Unknown:
		break;
	}
}

std::optional<QByteArray> RichHtmlRenderer::fragmentHref(
		const QByteArray &target) const {
	if (!IsStrictTarget(target) || target.front() != '#') {
		return std::nullopt;
	}
	const auto i = _firstAnchorIds.find(target.mid(1));
	return (i != end(_firstAnchorIds))
		? std::make_optional('#' + i->second)
		: std::nullopt;
}

std::optional<QByteArray> RichHtmlRenderer::textAnchorId(
		const Data::RichText &text) const {
	const auto i = _textAnchorIds.find(&text);
	return (i != end(_textAnchorIds))
		? std::make_optional(i->second)
		: std::nullopt;
}

std::optional<QByteArray> RichHtmlRenderer::blockAnchorId(
		const Data::RichBlock &block) const {
	const auto i = _blockAnchorIds.find(&block);
	return (i != end(_blockAnchorIds))
		? std::make_optional(i->second)
		: std::nullopt;
}

QByteArray RichHtmlRenderer::renderTexts(
		const std::vector<Data::RichText> &texts) {
	auto result = QByteArray();
	for (const auto &text : texts) {
		result += renderText(text);
	}
	return result;
}

QByteArray RichHtmlRenderer::renderTextChildren(
		const Data::RichText &text) {
	if (!text.children.empty()) {
		return renderTexts(text.children);
	}
	auto result = _context.pushTag("span", {
		{ "class", "rich_inline_fallback" },
		{ "data-rich-kind", "unsupported-text" },
		{ "inline", QByteArray() },
	});
	result += "Unsupported rich text";
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderTextLink(
		const Data::RichText &text,
		const std::optional<QByteArray> &href,
		std::map<QByteArray, QByteArray> attributes) {
	attributes.emplace("inline", QByteArray());
	if (!href || _linkActive) {
		attributes.erase("href");
		attributes.erase("onclick");
		attributes.emplace("class", "rich_inert_link");
		auto result = _context.pushTag("span", std::move(attributes));
		result += renderTextChildren(text);
		result += _context.popTag();
		return result;
	}
	attributes.emplace("href", *href);
	auto result = _context.pushTag("a", std::move(attributes));
	const auto previous = _linkActive;
	_linkActive = true;
	const auto guard = gsl::finally([&] { _linkActive = previous; });
	result += renderTextChildren(text);
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderCustomEmojiLink(
		const QByteArray &alt,
		std::map<QByteArray, QByteArray> attributes) {
	attributes.emplace("inline", QByteArray());
	if (_linkActive) {
		attributes.erase("href");
		attributes.erase("onclick");
		attributes.emplace("class", "rich_inert_link");
		auto result = _context.pushTag("span", std::move(attributes));
		result += alt;
		result += _context.popTag();
		return result;
	}
	auto result = _context.pushTag("a", std::move(attributes));
	const auto previous = _linkActive;
	_linkActive = true;
	const auto guard = gsl::finally([&] { _linkActive = previous; });
	result += alt;
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderText(const Data::RichText &text) {
	using Attributes = std::map<QByteArray, QByteArray>;
	using Type = Data::RichText::Type;

	const auto wrapChildren = [this, &text](
			const QByteArray &tag,
			Attributes attributes) {
		attributes.emplace("inline", QByteArray());
		auto result = _context.pushTag(tag, std::move(attributes));
		result += renderTextChildren(text);
		result += _context.popTag();
		return result;
	};
	switch (text.type) {
	case Type::Empty:
		return QByteArray();
	case Type::Plain:
		return SerializeString(text.text);
	case Type::Concat:
		return renderTexts(text.children);
	case Type::Bold:
		return wrapChildren("strong", {});
	case Type::Italic:
		return wrapChildren("em", {});
	case Type::Underline:
		return wrapChildren("u", {});
	case Type::Strike:
		return wrapChildren("s", {});
	case Type::Fixed:
		return wrapChildren("code", {
			{ "class", "rich_inline_code" },
		});
	case Type::Url: {
		const auto href = text.data.startsWith('#')
			? fragmentHref(text.data)
			: SafeHttpHref(text.data);
		auto attributes = Attributes();
		if (text.id) {
			attributes.emplace(
				"data-webpage-id",
				Data::NumberToString(text.id));
		}
		return renderTextLink(text, href, std::move(attributes));
	}
	case Type::Email:
		return renderTextLink(text, SafeEmailHref(text.data), {});
	case Type::Phone:
		return renderTextLink(text, SafePhoneHref(text.data), {});
	case Type::Subscript:
		return wrapChildren("sub", {});
	case Type::Superscript:
		return wrapChildren("sup", {});
	case Type::Marked:
		return wrapChildren("mark", {});
	case Type::Anchor: {
		auto attributes = Attributes{
			{ "class", "rich_reference" },
			{ "inline", QByteArray() },
		};
		if (const auto id = textAnchorId(text)) {
			attributes.emplace("id", *id);
		}
		auto result = _context.pushTag("span", std::move(attributes));
		result += renderTexts(text.children);
		result += _context.popTag();
		return result;
	}
	case Type::Math: {
		auto result = _context.pushTag("span", {
			{ "class", "rich_math_inline" },
			{ "data-rich-kind", "inline-math" },
			{ "dir", "ltr" },
			{ "inline", QByteArray() },
		});
		result += SerializeString(text.data);
		result += _context.popTag();
		return result;
	}
	case Type::CustomEmoji: {
		auto result = _context.pushTag("span", {
			{ "class", "rich_custom_emoji" },
			{ "data-document-id", Data::NumberToString(text.id) },
			{ "inline", QByteArray() },
		});
		const auto alt = SerializeString(text.text);
		const auto unavailable =
			(text.customEmojiData == Data::TextPart::UnavailableEmoji());
		const auto notLoaded = text.customEmojiData.isEmpty()
			|| IsAsciiDecimal(text.customEmojiData);
		const auto href = (unavailable || notLoaded)
			? std::optional<QByteArray>()
			: SafeRelativeEmojiHref(text.customEmojiData, _relativeBase);
		if (href) {
			result += renderCustomEmojiLink(alt, {
				{ "href", *href },
			});
		} else if (unavailable || notLoaded) {
			result += renderCustomEmojiLink(alt, {
				{ "href", QByteArray() },
				{ "onclick", unavailable
					? QByteArray("return ShowNotAvailableEmoji()")
					: QByteArray("return ShowNotLoadedEmoji()") },
			});
		} else {
			result += alt;
		}
		result += _context.popTag();
		return result;
	}
	case Type::Spoiler: {
		auto result = _context.pushTag("span", {
			{ "class", "spoiler hidden" },
			{ "inline", QByteArray() },
			{ "onclick", "ShowSpoiler(this)" },
		});
		result += _context.pushTag("span", {
			{ "aria-hidden", "true" },
			{ "inline", QByteArray() },
		});
		result += renderTextChildren(text);
		result += _context.popTag();
		result += _context.popTag();
		return result;
	}
	case Type::Mention: {
		const auto target = PlainTarget(text);
		const auto href = target
			? SafeMentionHref(*target, _internalLinksDomain)
			: std::optional<QByteArray>();
		return renderTextLink(text, href, {});
	}
	case Type::Hashtag:
	case Type::BotCommand:
	case Type::Cashtag: {
		const auto target = PlainTarget(text);
		const auto prefix = (text.type == Type::Hashtag)
			? '#'
			: (text.type == Type::BotCommand)
			? '/'
			: '$';
		const auto action = target
			? SafeActionData(*target, prefix)
			: std::optional<QByteArray>();
		if (!action) {
			return renderTextLink(text, std::nullopt, {});
		}
		const auto attribute = (text.type == Type::BotCommand)
			? QByteArray("data-command")
			: QByteArray("data-tag");
		const auto callback = (text.type == Type::Hashtag)
			? QByteArray("return ShowHashtag(this.dataset.tag)")
			: (text.type == Type::BotCommand)
			? QByteArray("return ShowBotCommand(this.dataset.command)")
			: QByteArray("return ShowCashtag(this.dataset.tag)");
		return renderTextLink(text, QByteArray(), {
			{ attribute, *action },
			{ "onclick", callback },
		});
	}
	case Type::AutoUrl: {
		const auto target = PlainTarget(text);
		const auto href = target
			? SafeHttpHref(*target)
			: std::optional<QByteArray>();
		return renderTextLink(text, href, {});
	}
	case Type::AutoEmail: {
		const auto target = PlainTarget(text);
		const auto href = target
			? SafeEmailHref(*target)
			: std::optional<QByteArray>();
		return renderTextLink(text, href, {});
	}
	case Type::AutoPhone: {
		const auto target = PlainTarget(text);
		const auto href = target
			? SafePhoneHref(*target)
			: std::optional<QByteArray>();
		return renderTextLink(text, href, {});
	}
	case Type::BankCard:
		return wrapChildren("span", {
			{ "class", "rich_bank_card" },
		});
	case Type::MentionName:
		return renderTextLink(text, QByteArray(), {
			{ "data-user-id", Data::NumberToString(text.id) },
			{ "onclick", "return ShowMentionName()" },
		});
	case Type::FormattedDate: {
		const auto datetime = QDateTime::fromSecsSinceEpoch(text.date);
		auto result = _context.pushTag("time", {
			{ "class", "rich_formatted_date" },
			{ "data-date", Data::NumberToString(text.date) },
			{ "data-day-of-week", RichBoolAttribute(text.dayOfWeek) },
			{ "data-long-date", RichBoolAttribute(text.longDate) },
			{ "data-long-time", RichBoolAttribute(text.longTime) },
			{ "data-relative", RichBoolAttribute(text.relative) },
			{ "data-short-date", RichBoolAttribute(text.shortDate) },
			{ "data-short-time", RichBoolAttribute(text.shortTime) },
			{ "datetime", datetime.toString(Qt::ISODate).toUtf8() },
			{ "dir", "ltr" },
			{ "inline", QByteArray() },
			{ "title", Data::FormatDateTime(text.date) },
		});
		result += renderTextChildren(text);
		result += _context.popTag();
		return result;
	}
	case Type::InlineImage: {
		auto result = _context.pushTag("span", {
			{ "class", "rich_inline_fallback" },
			{ "data-document-id", Data::NumberToString(text.id) },
			{ "data-rich-kind", "inline-image" },
			{ "data-source-height", Data::NumberToString(text.height) },
			{ "data-source-width", Data::NumberToString(text.width) },
			{ "inline", QByteArray() },
		});
		result += "Inline image";
		result += _context.popTag();
		return result;
	}
	case Type::Diff: {
		auto result = _context.pushTag("span", {
			{ "class", "rich_diff" },
			{ "data-rich-kind", "diff" },
			{ "inline", QByteArray() },
		});
		result += renderTextChildren(text);
		result += _context.pushTag("del", {
			{ "class", "rich_diff_previous" },
			{ "inline", QByteArray() },
		});
		result += "Previous: ";
		if (text.oldChildren.empty()) {
			result += _context.pushTag("span", {
				{ "class", "rich_inline_fallback" },
				{ "data-rich-kind", "unsupported-text" },
				{ "inline", QByteArray() },
			});
			result += "Unsupported rich text";
			result += _context.popTag();
		} else {
			result += renderTexts(text.oldChildren);
		}
		result += _context.popTag();
		result += _context.popTag();
		return result;
	}
	}
	Unexpected("Type in RichHtmlRenderer::renderText.");
}

QByteArray RichHtmlRenderer::renderBlocks(
		const std::vector<Data::RichBlock> &blocks) {
	auto result = QByteArray();
	for (const auto &block : blocks) {
		result += renderBlock(block);
	}
	return result;
}

QByteArray RichHtmlRenderer::renderTextBlock(
		const QByteArray &tag,
		std::map<QByteArray, QByteArray> attributes,
		const Data::RichText &text) {
	auto result = _context.pushTag(tag, std::move(attributes));
	result += renderText(text);
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderAuthorDate(
		const Data::RichBlock &block) {
	auto attributes = RichBlockAttributes("rich_author_date", "author-date");
	attributes.emplace("dir", "auto");
	auto result = _context.pushTag("address", std::move(attributes));
	result += renderText(block.text);
	if (block.date) {
		const auto datetime = QDateTime::fromSecsSinceEpoch(block.date);
		result += " \xE2\x80\xA2 ";
		result += _context.pushTag("time", {
			{ "data-date", Data::NumberToString(block.date) },
			{ "datetime", datetime.toString(Qt::ISODate).toUtf8() },
			{ "dir", "ltr" },
			{ "inline", QByteArray() },
		});
		result += SerializeString(Data::FormatDateTime(block.date));
		result += _context.popTag();
	}
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderCode(const Data::RichBlock &block) {
	auto attributes = RichBlockAttributes("rich_code", "code");
	attributes.emplace("dir", "auto");
	attributes.emplace("inline", QByteArray());
	if (!block.language.isEmpty()) {
		attributes.emplace("data-language", block.language);
	}
	auto result = _context.pushTag("pre", std::move(attributes));
	result += _context.pushTag("code", {
		{ "class", "rich_code_content" },
		{ "inline", QByteArray() },
	});
	result += renderText(block.text);
	result += _context.popTag();
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderList(const Data::RichBlock &block) {
	using Kind = Data::RichListKind;

	const auto ordered = (block.listKind == Kind::Ordered);
	auto attributes = RichBlockAttributes(
		"rich_list",
		ordered ? QByteArray("ordered-list") : QByteArray("list"));
	if (ordered) {
		if (block.orderedList.reversed) {
			attributes.emplace("reversed", QByteArray());
		}
		if (block.orderedList.start) {
			attributes.emplace(
				"start",
				Data::NumberToString(*block.orderedList.start));
		}
		if (block.orderedList.type) {
			const auto &type = *block.orderedList.type;
			auto native = false;
			if (type.size() == 1) {
				switch (type[0]) {
				case '1':
				case 'a':
				case 'A':
				case 'i':
				case 'I':
					native = true;
					break;
				}
			}
			attributes.emplace(
				native ? QByteArray("type") : QByteArray("data-list-type"),
				type);
		}
	}
	auto result = _context.pushTag(
		ordered ? QByteArray("ol") : QByteArray("ul"),
		std::move(attributes));
	for (const auto &item : block.listItems) {
		result += renderListItem(item, ordered);
	}
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderListItem(
		const Data::RichListItem &item,
		bool ordered) {
	using Content = Data::RichListItemContent;
	using State = Data::RichTaskState;

	const auto task = (item.taskState != State::None);
	const auto checked = (item.taskState == State::Checked);
	const auto customMarker = ordered
		&& !task
		&& item.num
		&& !item.num->isEmpty();
	auto classes = QByteArray("rich_list_item");
	if (task) {
		classes += " rich_task_item";
	} else if (customMarker) {
		classes += " rich_order_item";
	}
	auto attributes = std::map<QByteArray, QByteArray>{
		{ "class", std::move(classes) },
		{ "data-rich-content", (item.content == Content::Blocks)
			? QByteArray("blocks")
			: QByteArray("text") },
	};
	if (item.num) {
		attributes.emplace("data-num", *item.num);
	}
	if (item.type) {
		attributes.emplace("data-item-type", *item.type);
	}
	if (item.value) {
		attributes.emplace("value", Data::NumberToString(*item.value));
	}
	auto result = _context.pushTag("li", std::move(attributes));
	if (task) {
		result += _context.pushTag("span", {
			{ "aria-checked", RichBoolAttribute(checked) },
			{ "class", "rich_task_marker" },
			{ "inline", QByteArray() },
			{ "role", "checkbox" },
		});
		result += checked ? "\xE2\x98\x91" : "\xE2\x98\x90";
		result += _context.popTag();
	} else if (customMarker) {
		result += _context.pushTag("span", {
			{ "class", "rich_order_marker" },
			{ "inline", QByteArray() },
		});
		result += SerializeString(*item.num);
		result += _context.popTag();
	}
	result += _context.pushTag(
		(item.content == Content::Blocks) ? "div" : "span",
		{
			{ "class", "rich_list_content" },
			{ "inline", QByteArray() },
		});
	switch (item.content) {
	case Content::Text:
		if (item.text) {
			result += renderText(*item.text);
		}
		break;
	case Content::Blocks:
		result += renderBlocks(item.blocks);
		break;
	}
	result += _context.popTag();
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderQuote(const Data::RichBlock &block) {
	using Content = Data::RichQuoteContent;

	const auto kind = block.pullquote
		? QByteArray("pullquote")
		: (block.quoteContent == Content::Blocks)
		? QByteArray("quote-blocks")
		: QByteArray("quote");
	const auto classes = block.pullquote
		? QByteArray("rich_quote rich_pullquote")
		: QByteArray("rich_quote");
	auto result = _context.pushTag(
		block.pullquote ? QByteArray("aside") : QByteArray("blockquote"),
		RichBlockAttributes(classes, kind));
	result += _context.pushTag("div", {
		{ "class", "rich_quote_body" },
	});
	switch (block.quoteContent) {
	case Content::Text:
		result += renderText(block.text);
		break;
	case Content::Blocks:
		result += renderBlocks(block.blocks);
		break;
	}
	result += _context.popTag();
	const auto caption = renderText(block.quoteCaption);
	if (!caption.isEmpty()) {
		result += _context.pushTag("cite", {
			{ "class", "rich_quote_caption" },
		});
		result += caption;
		result += _context.popTag();
	}
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderDetails(const Data::RichBlock &block) {
	auto attributes = RichBlockAttributes("rich_details", "details");
	if (block.open) {
		attributes.emplace("open", QByteArray());
	}
	auto result = _context.pushTag("details", std::move(attributes));
	result += _context.pushTag("summary", {});
	result += renderText(block.text);
	result += _context.popTag();
	result += _context.pushTag("div", {
		{ "class", "rich_details_body" },
	});
	result += renderBlocks(block.blocks);
	result += _context.popTag();
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderTable(const Data::RichBlock &block) {
	auto wrapperAttributes = RichBlockAttributes("rich_table_wrap", "table");
	wrapperAttributes.emplace("tabindex", "0");
	auto result = _context.pushTag("div", std::move(wrapperAttributes));
	auto classes = QByteArray("rich_table");
	if (block.bordered) {
		classes += " bordered";
	}
	if (block.striped) {
		classes += " striped";
	}
	result += _context.pushTag("table", {
		{ "class", std::move(classes) },
	});
	const auto caption = renderText(block.text);
	if (!caption.isEmpty()) {
		result += _context.pushTag("caption", {});
		result += caption;
		result += _context.popTag();
	}
	result += _context.pushTag("tbody", {});
	for (const auto &row : block.tableRows) {
		result += renderTableRow(row);
	}
	result += _context.popTag();
	result += _context.popTag();
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderTableRow(
		const Data::RichTableRow &row) {
	auto result = _context.pushTag("tr", {});
	for (const auto &cell : row.cells) {
		result += renderTableCell(cell);
	}
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderTableCell(
		const Data::RichTableCell &cell) {
	using Alignment = Data::RichTableAlignment;
	using VerticalAlignment = Data::RichTableVerticalAlignment;

	auto horizontalClass = QByteArray("rich_align_left");
	switch (cell.alignment) {
	case Alignment::Left:
		break;
	case Alignment::Center:
		horizontalClass = "rich_align_center";
		break;
	case Alignment::Right:
		horizontalClass = "rich_align_right";
		break;
	}
	auto verticalClass = QByteArray("rich_valign_top");
	switch (cell.verticalAlignment) {
	case VerticalAlignment::Top:
		break;
	case VerticalAlignment::Middle:
		verticalClass = "rich_valign_middle";
		break;
	case VerticalAlignment::Bottom:
		verticalClass = "rich_valign_bottom";
		break;
	}
	auto attributes = std::map<QByteArray, QByteArray>{
		{ "class", horizontalClass + ' ' + verticalClass },
	};
	if (cell.colspan && *cell.colspan > 0) {
		const auto source = *cell.colspan;
		const auto span = (source > kRichTableSpanMax)
			? kRichTableSpanMax
			: source;
		attributes.emplace("colspan", Data::NumberToString(span));
		if (span != source) {
			attributes.emplace(
				"data-source-colspan",
				Data::NumberToString(source));
		}
	}
	if (cell.rowspan && *cell.rowspan > 0) {
		const auto source = *cell.rowspan;
		const auto span = (source > kRichTableSpanMax)
			? kRichTableSpanMax
			: source;
		attributes.emplace("rowspan", Data::NumberToString(span));
		if (span != source) {
			attributes.emplace(
				"data-source-rowspan",
				Data::NumberToString(source));
		}
	}
	auto result = _context.pushTag(
		cell.header ? QByteArray("th") : QByteArray("td"),
		std::move(attributes));
	if (cell.text) {
		result += renderText(*cell.text);
	}
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderFallback(
		const QByteArray &kind,
		const QByteArray &label) {
	auto result = _context.pushTag(
		"div",
		RichBlockAttributes("rich_fallback", kind));
	result += SerializeString(label);
	result += _context.popTag();
	return result;
}

QByteArray RichHtmlRenderer::renderBlock(const Data::RichBlock &block) {
	using Kind = Data::RichBlock::Kind;

	switch (block.kind) {
	case Kind::Unsupported:
		return renderFallback("unsupported", "Unsupported rich content");
	case Kind::Heading: {
		auto attributes = RichBlockAttributes("rich_heading", "heading");
		attributes.emplace(
			"data-level",
			Data::NumberToString(block.headingLevel));
		return renderTextBlock(
			RichHeadingTag(block.headingLevel),
			std::move(attributes),
			block.text);
	}
	case Kind::Paragraph: {
		auto attributes = RichBlockAttributes("rich_paragraph", "paragraph");
		attributes.emplace("dir", "auto");
		return renderTextBlock("p", std::move(attributes), block.text);
	}
	case Kind::Footer: {
		auto attributes = RichBlockAttributes("rich_footer", "footer");
		attributes.emplace("dir", "auto");
		return renderTextBlock("footer", std::move(attributes), block.text);
	}
	case Kind::Thinking: {
		auto attributes = RichBlockAttributes("rich_thinking", "thinking");
		attributes.emplace("dir", "auto");
		return renderTextBlock("p", std::move(attributes), block.text);
	}
	case Kind::AuthorDate:
		return renderAuthorDate(block);
	case Kind::Code:
		return renderCode(block);
	case Kind::Divider: {
		auto attributes = RichBlockAttributes("rich_divider", "divider");
		attributes.emplace("empty", QByteArray());
		return _context.pushTag("hr", std::move(attributes));
	}
	case Kind::Anchor: {
		const auto id = blockAnchorId(block);
		if (!id) {
			return renderFallback("malformed", "Malformed rich content");
		}
		auto attributes = RichBlockAttributes("rich_anchor", "anchor");
		attributes.emplace("id", *id);
		attributes.emplace("inline", QByteArray());
		auto result = _context.pushTag("span", std::move(attributes));
		result += _context.popTag();
		return result;
	}
	case Kind::List:
		return renderList(block);
	case Kind::Quote:
		return renderQuote(block);
	case Kind::Photo:
		return renderPhoto(block);
	case Kind::Video:
		return renderVideo(block);
	case Kind::Cover:
		return renderMediaGroup(block, "cover", false);
	case Kind::Embed:
		return renderEmbed(block);
	case Kind::EmbedPost:
		return renderEmbedPost(block);
	case Kind::Collage:
		return renderMediaGroup(block, "collage", true);
	case Kind::Slideshow:
		return renderMediaGroup(block, "slideshow", true);
	case Kind::Channel:
		return renderChannel(block);
	case Kind::Audio:
		return renderAudio(block);
	case Kind::Math: {
		auto attributes = RichBlockAttributes("rich_math_display", "math");
		attributes.emplace("dir", "ltr");
		attributes.emplace("inline", QByteArray());
		auto result = _context.pushTag("div", std::move(attributes));
		result += SerializeString(block.formula);
		result += _context.popTag();
		return result;
	}
	case Kind::Table:
		return renderTable(block);
	case Kind::Details:
		return renderDetails(block);
	case Kind::RelatedArticles:
		return renderRelatedArticles(block);
	case Kind::Map:
		return renderMap(block, "map");
	case Kind::InputMap:
		return renderMap(block, "input-map");
	case Kind::Unknown:
		return renderFallback("unknown", "Unknown rich content");
	}
	Unexpected("Kind in RichHtmlRenderer::renderBlock.");
}

[[nodiscard]] QByteArray RenderRichMessage(
		Context &context,
		const Data::RichMessage &message,
		int messageId,
		const QString &internalLinksDomain,
		const QByteArray &relativeBase,
		const RichMediaCallbacks &media) {
	return RichHtmlRenderer(
		context,
		message,
		messageId,
		internalLinksDomain,
		relativeBase,
		media).renderMessage();
}

} // namespace

struct HtmlWriter::MessageInfo {
	enum class Type {
		Service,
		Default,
	};
	int id = 0;
	Type type = Type::Service;
	PeerId fromId = 0;
	UserId viaBotId = 0;
	TimeId date = 0;
	PeerId forwardedFromId = 0;
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
	[[nodiscard]] QByteArray pushStoriesListEntry(
		const StoryData &story,
		const QByteArray &name,
		const QByteArrayList &details,
		const QByteArray &info,
		const std::vector<Data::TextPart> &caption,
		const QString &internalLinksDomain,
		const QString &link = QString());
	[[nodiscard]] QByteArray pushAudioEntry(
		const QByteArray &name,
		const QByteArray &info,
		const QByteArrayList &details,
		const QByteArray &duration,
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
		const QString &internalLinksDomain,
		Fn<QByteArray(int messageId, QByteArray text)> wrapMessageLink);
	[[nodiscard]] QByteArray pushGenericMedia(const MediaData &data);
	[[nodiscard]] QByteArray pushRichPhotoMedia(
		const Data::Photo *data,
		const QString &basePath);
	[[nodiscard]] QByteArray pushRichVideoMedia(
		const Data::Document *data,
		const QString &basePath);
	[[nodiscard]] QByteArray pushRichAudioMedia(
		const Data::Document *data);
	[[nodiscard]] QByteArray pushRichReferenceMedia(
		MediaData data,
		const Data::Photo *photo,
		const QString &basePath);
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
	[[nodiscard]] QByteArray pushPoll(
		const Data::Poll &data,
		const QString &internalLinksDomain,
		const QString &relativeLinkBase);
	[[nodiscard]] QByteArray pushTodoList(
		const Data::TodoList &data,
		const QString &internalLinksDomain,
		const QString &relativeLinkBase);
	[[nodiscard]] QByteArray pushGiveaway(
		const PeersMap &peers,
		const Data::GiveawayStart &data);
	[[nodiscard]] QByteArray pushGiveaway(
		const PeersMap &peers,
		const Data::GiveawayResults &data,
		Fn<QByteArray(int messageId, QByteArray text)> wrapMessageLink);

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
		if (userpic.tooltip.isEmpty()) {
			result.append(pushDiv(
				"initials",
				"line-height: " + size));
		} else {
			result.append(pushTag("div", {
				{ "class", "initials" },
				{ "style", "line-height: " + size },
				{ "title", userpic.tooltip },
			}));
		}
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

QByteArray HtmlWriter::Wrap::pushStoriesListEntry(
		const StoryData &story,
		const QByteArray &name,
		const QByteArrayList &details,
		const QByteArray &info,
		const std::vector<Data::TextPart> &caption,
		const QString &internalLinksDomain,
		const QString &link) {
	auto result = pushDiv("entry clearfix");
	if (!link.isEmpty()) {
		result.append(pushTag("a", {
			{ "class", "pull_left userpic_wrap" },
			{ "href", relativePath(link).toUtf8() + "#allow_back" },
		}));
	} else {
		result.append(pushDiv("pull_left userpic_wrap"));
	}
	if (!story.imageLink.isEmpty()) {
		const auto sizeStyle = "width: "
			+ Data::NumberToString(kStoryThumbWidth)
			+ "px; height: "
			+ Data::NumberToString(kStoryThumbHeight)
			+ "px";
		result.append(pushTag("img", {
			{ "class", "story" },
			{ "style", sizeStyle },
			{ "src", relativePath(story.imageLink).toUtf8() },
			{ "empty", "" }
		}));
	}
	result.append(popTag());
	result.append(pushDiv("body"));
	if (!info.isEmpty()) {
		result.append(pushDiv("pull_right info details"));
		result.append(SerializeString(info));
		result.append(popTag());
	}
	if (!name.isEmpty()) {
		if (!link.isEmpty()) {
			result.append(pushTag("a", {
				{ "class", "block_link expanded" },
				{ "href", relativePath(link).toUtf8() + "#allow_back" },
			}));
		}
		result.append(pushDiv("name bold"));
		result.append(SerializeString(name));
		result.append(popTag());
		if (!link.isEmpty()) {
			result.append(popTag());
		}
	}
	const auto text = caption.empty()
		? QByteArray()
		: FormatText(caption, internalLinksDomain, _base);
	if (!text.isEmpty()) {
		result.append(pushDiv("text"));
		result.append(text);
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

QByteArray HtmlWriter::Wrap::pushAudioEntry(
		const QByteArray &name,
		const QByteArray &info,
		const QByteArrayList &details,
		const QByteArray &duration,
		const QString &link) {
	auto result = pushDiv("entry clearfix");
	if (!link.isEmpty()) {
		result.append(pushTag("a", {
			{ "class", "pull_left userpic_wrap" },
			{ "href", relativePath(link).toUtf8() + "#allow_back" },
		}));
	} else {
		result.append(pushDiv("pull_left userpic_wrap"));
	}
	result.append(pushDiv("userpic audio_icon"));
	result.append(popTag());
	result.append(popTag());
	result.append(pushDiv("body"));
	if (!duration.isEmpty()) {
		result.append(pushDiv("pull_right info details"));
		result.append(SerializeString(duration));
		result.append(popTag());
	}
	if (!info.isEmpty()) {
		if (!link.isEmpty()) {
			result.append(pushTag("a", {
				{ "class", "block_link expanded" },
				{ "href", relativePath(link).toUtf8() + "#allow_back" },
			}));
		}
		result.append(pushDiv("name bold"));
		result.append(SerializeString(info));
		result.append(popTag());
		if (!link.isEmpty()) {
			result.append(popTag());
		}
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
		userpic.colorIndex = dialog.colorIndex;
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
		const auto amount = FormatMoneyAmount(data.amount, data.currency);
		if (data.recurringUsed) {
			return "You were charged " + amount + " via recurring payment";
		}
		auto result = "You have successfully transferred "
			+ amount
			+ " for "
			+ wrapReplyToLink("this invoice");
		if (data.recurringInit) {
			result += " and allowed future recurring payments";
		}
		return result;
	}, [&](const ActionPhoneCall &data) {
		return QByteArray();
	}, [&](const ActionScreenshotTaken &data) {
		return serviceFrom + " took a screenshot";
	}, [&](const ActionCustomAction &data) {
		return data.message;
	}, [&](const ActionBotAllowed &data) {
		return data.attachMenu
			? "You allowed this bot to message you "
			"when you added it in the attachment menu."_q
			: data.fromRequest
			? "You allowed this bot to message you in his web-app."_q
			: data.app.isEmpty()
			? ("You allowed this bot to message you when you opened "
				+ SerializeString(data.app))
			: ("You allowed this bot to message you when you logged in on "
				+ SerializeString(data.domain));
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
		const auto durationText = (data.duration
			? (" (" + QString::number(data.duration) + " seconds)")
			: QString()).toUtf8();
		return isChannel
			? ("Voice chat" + durationText)
			: (serviceFrom + " started voice chat" + durationText);
	}, [&](const ActionInviteToGroupCall &data) {
		return serviceFrom
			+ " invited "
			+ peers.wrapUserNames(data.userIds)
			+ " to the voice chat";
	}, [&](const ActionSetMessagesTTL &data) {
		const auto periodText = (data.period == 7 * 86400)
			? "7 days"
			: (data.period == 86400)
			? "24 hours"
			: QByteArray();
		return isChannel
			? (data.period
				? "New messages will auto-delete in " + periodText
				: "New messages will not auto-delete")
			: (data.period
				? (serviceFrom
					+ " has set messages to auto-delete in " + periodText)
				: (serviceFrom
					+ " has set messages not to auto-delete"));
	}, [&](const ActionGroupCallScheduled &data) {
		const auto dateText = FormatDateTime(data.date);
		return isChannel
			? ("Voice chat scheduled for " + dateText)
			: (serviceFrom + " scheduled a voice chat for " + dateText);
	}, [&](const ActionSetChatTheme &data) {
		if (data.emoji.isEmpty()) {
			return isChannel
				? "Channel theme was disabled"
				: (serviceFrom + " disabled chat theme");
		}
		return isChannel
			? ("Channel theme was changed to " + data.emoji).toUtf8()
			: (serviceFrom + " changed chat theme to " + data.emoji).toUtf8();
	}, [&](const ActionChatJoinedByRequest &data) {
		return serviceFrom
			+ " joined group by request";
	}, [&](const ActionWebViewDataSent &data) {
		return "You have just successfully transferred data from the &laquo;"
			+ SerializeString(data.text)
			+ "&raquo; button to the bot";
	}, [&](const ActionGiftPremium &data) {
		if (!data.days || data.cost.isEmpty()) {
			return serviceFrom + " sent you a gift.";
		}
		return serviceFrom
			+ " sent you a gift for "
			+ data.cost
			+ ": Telegram Premium for "
			+ QString::number(data.days).toUtf8()
			+ " days.";
	}, [&](const ActionTopicCreate &data) {
		return serviceFrom
			+ " created topic &laquo;"
			+ SerializeString(data.title)
			+ "&raquo;";
	}, [&](const ActionTopicEdit &data) {
		auto parts = QList<QByteArray>();
		if (!data.title.isEmpty()) {
			parts.push_back("title to &laquo;"
				+ SerializeString(data.title)
				+ "&raquo;");
		}
		if (data.iconEmojiId) {
			parts.push_back("icon to &laquo;"
				+ QString::number(*data.iconEmojiId).toUtf8()
				+ "&raquo;");
		}
		return serviceFrom + " changed topic " + parts.join(',');
	}, [&](const ActionSuggestProfilePhoto &data) {
		return serviceFrom + " suggests to use this photo";
	}, [&](const ActionRequestedPeer &data) {
		return "requested: "_q/* + data.peerId*/;
	}, [&](const ActionSetChatWallPaper &data) {
		return serviceFrom
			+ (data.same
				? (" set "
					+ wrapReplyToLink("the same background")
					+ " for this chat")
				: " set a new background for this chat");
	}, [&](const ActionGiftCode &data) {
		return data.unclaimed
			? ("This is an unclaimed Telegram Premium for "
				+ NumberToString(data.days)
				+ (data.days > 1 ? " days" : " day")
				+ " prize in a giveaway organized by a channel.")
			: data.viaGiveaway
			? ("You won a Telegram Premium for "
				+ NumberToString(data.days)
				+ (data.days > 1 ? " days" : " day")
				+ " prize in a giveaway organized by a channel.")
			: ("You've received a Telegram Premium for "
				+ NumberToString(data.days)
				+ (data.days > 1 ? " days" : " day")
				+ " gift from a channel.");
	}, [&](const ActionGiveawayLaunch &data) {
		return serviceFrom + " just started a giveaway "
			"of Telegram Premium subscriptions to its followers.";
	}, [&](const ActionGiveawayResults &data) {
		return !data.winners
			? "No winners of the giveaway could be selected."
			: (data.credits && data.unclaimed)
			? "Some winners of the giveaway were randomly selected by "
				"Telegram and received their prize."
			: (!data.credits && data.unclaimed)
			? "Some winners of the giveaway were randomly selected by "
				"Telegram and received private messages with giftcodes."
			: (data.credits && !data.unclaimed)
			? NumberToString(data.winners) + " of the giveaway was randomly "
				"selected by Telegram and received their prize."
			: NumberToString(data.winners) + " of the giveaway was randomly "
				"selected by Telegram and received private messages with "
				"giftcodes.";
	}, [&](const ActionBoostApply &data) {
		return serviceFrom
			+ " boosted the group "
			+ QByteArray::number(data.boosts)
			+ (data.boosts > 1 ? " times" : " time");
	}, [&](const ActionPaymentRefunded &data) {
		const auto amount = FormatMoneyAmount(data.amount, data.currency);
		auto result = peers.wrapPeerName(data.peerId)
			+ " refunded back "
			+ amount;
		return result;
	}, [&](const ActionGiftCredits &data) {
		if (!data.amount || data.cost.isEmpty()) {
			return serviceFrom + " sent you a gift.";
		}
		return serviceFrom
			+ " sent you a gift for "
			+ data.cost
			+ ": "
			+ QString::number(data.amount.value()).toUtf8()
			+ (data.amount.ton() ? " TON." : " Telegram Stars.");
	}, [&](const ActionPrizeStars &data) {
		return "You won a prize in a giveaway organized by "
			+ peers.wrapPeerName(data.peerId)
			+ ".\n Your prize is "
			+ QString::number(data.amount).toUtf8()
			+ " Telegram Stars.";
	}, [&](const ActionStarGift &data) {
		return serviceFrom
			+ " sent you a gift of "
			+ QByteArray::number(data.stars)
			+ " Telegram Stars.";
	}, [&](const ActionPaidMessagesRefunded &data) {
		auto result = message.out
			? ("You refunded "
				+ QString::number(data.stars).toUtf8()
				+ " Stars for "
				+ QString::number(data.messages).toUtf8()
				+ " messages to "
				+ peers.wrapPeerName(dialog.peerId))
			: (peers.wrapPeerName(dialog.peerId)
				+ " refunded "
				+ QString::number(data.stars).toUtf8()
				+ " Stars for "
				+ QString::number(data.messages).toUtf8()
				+ " messages to you");
		return result;
	}, [&](const ActionPaidMessagesPrice &data) {
		if (isChannel) {
			auto result = !data.broadcastAllowed
				? "Direct messages were disabled."
				: ("Price per direct message changed to "
					+ QString::number(data.stars).toUtf8()
					+ " Telegram Stars.");
			return result;
		}
		auto result = "Price per message changed to "
			+ QString::number(data.stars).toUtf8()
			+ " Telegram Stars.";
		return result;
	}, [&](const ActionTodoCompletions &data) {
		auto completed = QByteArrayList();
		for (const auto index : data.completed) {
			completed.push_back(QByteArray::number(index));
		}
		auto incompleted = QByteArrayList();
		for (const auto index : data.incompleted) {
			incompleted.push_back(QByteArray::number(index));
		}
		const auto list = [](const QByteArrayList &v) {
			return v.isEmpty()
				? QByteArray()
				: (v.size() > 1)
				? (v.mid(0, v.size() - 1).join(", ") + " and " + v.back())
				: v.front();
		};
		if (completed.isEmpty() && !incompleted.isEmpty()) {
			return serviceFrom
				+ " marked "
				+ list(incompleted)
				+ " as not done yet in "
				+ wrapReplyToLink("this todo list") + ".";
		} else if (!completed.isEmpty() && incompleted.isEmpty()) {
			return serviceFrom
				+ " marked "
				+ list(completed)
				+ " as done in "
				+ wrapReplyToLink("this todo list") + ".";
		}
		return serviceFrom
			+ " marked "
			+ list(completed)
			+ " as done and "
			+ list(incompleted)
			+ " as not done yet in "
			+ wrapReplyToLink("this todo list") + ".";
	}, [&](const ActionTodoAppendTasks &data) {
		auto tasks = QByteArrayList();
		for (const auto &task : data.items) {
			tasks.push_back("&quot;"
				+ FormatText(task.text, internalLinksDomain, _base)
				+ "&quot;");
		}
		return serviceFrom + " added tasks: " + tasks.join(", ");
	}, [&](const ActionPollAppendAnswer &data) {
		return serviceFrom + " added &quot;"
			+ data.option
			+ "&quot; to the poll.";
	}, [&](const ActionPollDeleteAnswer &data) {
		return serviceFrom + " removed &quot;"
			+ data.option
			+ "&quot; from the poll.";
	}, [&](const ActionSuggestedPostApproval &data) {
		return serviceFrom
			+ (data.rejected ? " rejected " : " approved ")
			+ "your suggested post"
			+ (data.price
				? (", for "
					+ QString::number(data.price.value()).toUtf8()
					+ (data.price.ton() ? " TON" : " stars"))
				: "")
			+ (data.scheduleDate
				? (", "
					+ FormatDateText(data.scheduleDate)
					+ " at "
					+ FormatTimeText(data.scheduleDate))
				: "")
			+ (data.rejectComment.isEmpty()
				? "."
				: (", with comment: &quot;"
					+ SerializeString(data.rejectComment)
					+ "&quot;"));
	}, [&](const ActionSuggestedPostSuccess &data) {
		return "The paid post was shown for 24 hours and "
			+ QString::number(data.price.value()).toUtf8()
			+ (data.price.ton() ? " TON" : " stars")
			+ " were transferred to the channel.";
	}, [&](const ActionSuggestedPostRefund &data) {
		return QByteArray() + (data.payerInitiated
			? "The user refunded the payment, post was deleted."
			: "The admin deleted the post early, the payment was refunded.");
	}, [&](const ActionSuggestBirthday &data) {
		return serviceFrom
			+ " suggests to add a date of birth: "
			+ QByteArray::number(data.birthday.day())
			+ [&] {
				switch (data.birthday.month()) {
				case 1: return " January";
				case 2: return " February";
				case 3: return " March";
				case 4: return " April";
				case 5: return " May";
				case 6: return " June";
				case 7: return " July";
				case 8: return " August";
				case 9: return " September";
				case 10: return " October";
				case 11: return " November";
				case 12: return " December";
				}
				return "";
			}() + (data.birthday.year()
				? (' ' + QByteArray::number(data.birthday.year()))
				: QByteArray());
	}, [&](const ActionNoForwardsToggle &data) {
		return serviceFrom
			+ (data.newValue
				? " disabled sharing in this chat"
				: " enabled sharing in this chat");
	}, [&](const ActionNoForwardsRequest &data) {
		return serviceFrom
			+ " requested to enable sharing in this chat";
	}, [&](const ActionNewCreatorPending &data) {
		return peers.wrapUserName(data.newCreatorId)
			+ " will become the new main admin in 7 days if "
			+ serviceFrom
			+ " does not return";
	}, [&](const ActionChangeCreator &data) {
		return serviceFrom
			+ " made "
			+ peers.wrapUserName(data.newCreatorId)
			+ " the new main admin of the group";
	}, [&](const ActionManagedBotCreated &data) {
		return serviceFrom
			+ " created a bot "
			+ peers.wrapUserName(data.botId);
	}, [](v::null_t) { return QByteArray(); });

	if (!serviceText.isEmpty()) {
		const auto &content = message.action.content;
		const auto photo = v::is<ActionChatEditPhoto>(content)
			? &v::get<ActionChatEditPhoto>(content).photo
			: v::is<ActionSuggestProfilePhoto>(content)
			? &v::get<ActionSuggestProfilePhoto>(content).photo
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
			? PeerColorIndex(message.forwardedFromId)
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
		userpic.colorIndex = PeerColorIndex(fromPeerId);
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
		{ "title", FormatDateTime(message.date, true) },
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
				{ "class", "date details" },
				{ "title", FormatDateTime(message.forwardedDate, true) },
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

	block.append(
		pushMedia(
			message,
			basePath,
			peers,
			internalLinksDomain,
			wrapMessageLink));

	if (message.richMessage) {
		const auto callbacks = RichMediaCallbacks{
			.photo = [this, basePath](const Data::Photo *photo) {
				return pushRichPhotoMedia(photo, basePath);
			},
			.video = [this, basePath](const Data::Document *document) {
				return pushRichVideoMedia(document, basePath);
			},
			.audio = [this](const Data::Document *document) {
				return pushRichAudioMedia(document);
			},
			.generic = [this](const MediaData &data) {
				return pushGenericMedia(data);
			},
			.photoCard = [this, basePath](
					const MediaData &data,
					const Data::Photo *photo) {
				return pushRichReferenceMedia(data, photo, basePath);
			},
		};
		block.append(RenderRichMessage(
			_context,
			*message.richMessage,
			message.id,
			internalLinksDomain,
			_base,
			callbacks));
	} else {
		const auto text = FormatText(message.text, internalLinksDomain, _base);
		if (!text.isEmpty()) {
			block.append(pushDiv("text"));
			block.append(text);
			block.append(popTag());
		}
	}
	if (!message.inlineButtonRows.empty()) {
		using Type = HistoryMessageMarkupButton::Type;
		const auto endline = u" | "_q;
		block.append(pushTag("table", { { "class", "bot_buttons_table" } }));
		block.append(pushTag("tbody"));
		for (const auto &row : message.inlineButtonRows) {
			block.append(pushTag("tr"));
			block.append(pushTag("td", { { "class", "bot_button_row" } }));
			for (const auto &button : row) {
				using Attribute = std::pair<QByteArray, QByteArray>;
				const auto content = (!button.data.isEmpty()
						? (u"Data: "_q + button.data + endline)
						: QString())
					+ (!button.forwardText.isEmpty()
						? (u"Forward text: "_q + button.forwardText + endline)
						: QString())
					+ (u"Type: "_q
						+ HistoryMessageMarkupButton::TypeToString(button));
				const auto link = (button.type == Type::Url)
					? button.data
					: QByteArray();
				const auto onclick = (button.type != Type::Url)
					? ("return ShowTextCopied('"
							+ QString(content)
								.replace('\\', u"\\\\"_q)
								.replace('\'', u"\\'"_q)
							+ "');").toUtf8()
					: QByteArray();
				block.append(pushTag("div", { { "class", "bot_button" } }));
				block.append(pushTag("a", {
					link.isEmpty() ? Attribute() : Attribute{ "href", link },
					onclick.isEmpty()
						? Attribute()
						: Attribute{ "onclick", onclick },
				}));
				block.append(pushTag("div"));
				block.append(SerializeString(button.text.toUtf8()));
				block.append(popTag());
				block.append(popTag());
				block.append(popTag());

				if (&button != &row.back()) {
					block.append(pushTag("div", {
						{ "class", "bot_button_column_separator" }
					}));
					block.append(popTag());
				}
			}
			block.append(popTag());
			block.append(popTag());
		}
		block.append(popTag());
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
	if (!message.reactions.empty()) {
		block.append(pushTag("span", {
			{ "class", "reactions" },
		}));
		for (const auto &reaction : message.reactions) {
			auto reactionClass = QByteArray("reaction");
			for (const auto &recent : reaction.recent) {
				const auto peer = peers.peer(recent.peerId);
				if (peer.user() && peer.user()->isSelf) {
					reactionClass += " active";
					break;
				}
			}
			if (reaction.type == Reaction::Type::Paid) {
				reactionClass += " paid";
			}

			block.append(pushTag("span", {
				{ "class", reactionClass },
			}));
			block.append(pushTag("span", {
				{ "class", "emoji" },
			}));
			switch (reaction.type) {
				case Reaction::Type::Emoji:
					block.append(SerializeString(reaction.emoji.toUtf8()));
					break;
				case Reaction::Type::CustomEmoji:
					block.append(FormatCustomEmoji(
						reaction.documentId,
						"\U0001F44B",
						_base));
					break;
				case Reaction::Type::Paid:
					block.append(SerializeString("\u2B50"));
					break;
			}
			block.append(popTag());
			if (!reaction.recent.empty()) {
				block.append(pushTag("span", {
					{ "class", "userpics" },
				}));
				for (const auto &recent : reaction.recent) {
					const auto peer = peers.peer(recent.peerId);
					block.append(pushUserpic(UserpicData({
						.colorIndex = peer.colorIndex(),
						.pixelSize = 20,
						.firstName = peer.user()
							? peer.user()->info.firstName
							: peer.name(),
						.lastName = peer.user()
							? peer.user()->info.lastName
							: "",
						.tooltip = peer.name(),
					})));
				}
				block.append(popTag());
			}
			if (reaction.recent.empty()
				|| (reaction.count > reaction.recent.size())) {
				block.append(pushTag("span", {
					{ "class", "count" },
				}));
				block.append(NumberToString(reaction.count));
				block.append(popTag());
			}
			block.append(popTag());
		}
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
	} else if (QDateTime::fromSecsSinceEpoch(previous->date).date()
		!= QDateTime::fromSecsSinceEpoch(message.date).date()) {
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
		const QString &internalLinksDomain,
		Fn<QByteArray(int messageId, QByteArray text)> wrapMessageLink) {
	const auto data = prepareMediaData(
		message,
		basePath,
		peers,
		internalLinksDomain);
	if (!data.classes.isEmpty()) {
		return pushGenericMedia(data);
	}
	using namespace Data;
	const auto &content = message.media.content;
	if (const auto document = std::get_if<Document>(&content)) {
		Assert(!message.media.ttl);
		if (document->isSticker) {
			return pushStickerMedia(*document, basePath);
		} else if (document->isAnimated) {
			return pushAnimatedMedia(*document, basePath);
		} else if (document->isVideoFile) {
			return pushVideoFileMedia(*document, basePath);
		}
		Unexpected("Non generic document in HtmlWriter::Wrap::pushMedia.");
	} else if (const auto photo = std::get_if<Photo>(&content)) {
		Assert(!message.media.ttl);
		return pushPhotoMedia(*photo, basePath);
	} else if (const auto poll = std::get_if<Poll>(&content)) {
		return pushPoll(*poll, internalLinksDomain, _base);
	} else if (const auto todo = std::get_if<TodoList>(&content)) {
		return pushTodoList(*todo, internalLinksDomain, _base);
	} else if (const auto giveaway = std::get_if<GiveawayStart>(&content)) {
		return pushGiveaway(peers, *giveaway);
	} else if (const auto giveaway = std::get_if<GiveawayResults>(&content)) {
		return pushGiveaway(peers, *giveaway, wrapMessageLink);
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

	const auto &[thumb, size] = WriteImageThumb(
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

	const auto &[thumb, size] = WriteImageThumb(
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
		generic.status = Ui::FormatImageSizeText(
			QSize(data.image.width, data.image.height)).toUtf8();
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

QByteArray HtmlWriter::Wrap::pushRichPhotoMedia(
		const Data::Photo *data,
		const QString &basePath) {
	const auto presentation = RichPhotoPresentation(data);
	return pushPhotoMedia(presentation, basePath);
}

QByteArray HtmlWriter::Wrap::pushRichVideoMedia(
		const Data::Document *data,
		const QString &basePath) {
	auto presentation = RichDocumentPresentation(data);
	const auto &thumbPath = presentation.thumb.file.relativePath;
	if (!thumbPath.isEmpty()) {
		QImageReader reader(basePath + thumbPath);
		if (!reader.canRead() || reader.read().isNull()) {
			presentation.thumb.file.relativePath.clear();
		}
	}
	return pushVideoFileMedia(presentation, basePath);
}

QByteArray HtmlWriter::Wrap::pushRichAudioMedia(
		const Data::Document *data) {
	const auto presentation = RichDocumentPresentation(data);
	return pushGenericMedia(PrepareAudioMediaData(presentation));
}

QByteArray HtmlWriter::Wrap::pushRichReferenceMedia(
		MediaData data,
		const Data::Photo *photo,
		const QString &basePath) {
	const auto presentation = RichPhotoPresentation(photo);
	const auto &path = presentation.image.file.relativePath;
	data.link = path;
	data.thumb = path.isEmpty()
		? QString()
		: Data::WriteImageThumb(
			basePath,
			path,
			kEntryUserpicSize * 2,
			kEntryUserpicSize * 2,
			u"_rich_card_thumb"_q);
	return pushGenericMedia(data);
}

QByteArray HtmlWriter::Wrap::pushPoll(
		const Data::Poll &data,
		const QString &internalLinksDomain,
		const QString &relativeLinkBase) {
	using namespace Data;

	auto result = pushDiv("media_wrap clearfix");
	result.append(pushDiv("media_poll"));
	result.append(pushDiv("question bold"));
	result.append(FormatText(
		data.question,
		internalLinksDomain,
		relativeLinkBase));
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
		result.append("- "
			+ FormatText(answer.text, internalLinksDomain, relativeLinkBase)
			+ details(answer));
		result.append(popTag());
	}
	result.append(pushDiv("total details	"));
	result.append(votes(data.totalVotes));
	result.append(popTag());
	result.append(popTag());
	result.append(popTag());
	return result;
}

QByteArray HtmlWriter::Wrap::pushTodoList(
		const Data::TodoList &data,
		const QString &internalLinksDomain,
		const QString &relativeLinkBase) {
	using namespace Data;

	auto result = pushDiv("media_wrap clearfix");
	result.append(pushDiv("media_poll"));
	result.append(pushDiv("question bold"));
	result.append(FormatText(
		data.title,
		internalLinksDomain,
		relativeLinkBase));
	result.append(popTag());
	result.append(pushDiv("details"));
	result.append(SerializeString("To-do List"));
	result.append(popTag());
	const auto details = [&](const TodoListItem &item) {
		return QByteArray(""); // #TODO todo
	};
	for (const auto &item : data.items) {
		result.append(pushDiv("answer"));
		result.append("- "
			+ FormatText(item.text, internalLinksDomain, relativeLinkBase)
			+ details(item));
		result.append(popTag());
	}
	result.append(popTag());
	result.append(popTag());
	return result;
}

QByteArray HtmlWriter::Wrap::pushGiveaway(
		const PeersMap &peers,
		const Data::GiveawayStart &data) {
	auto result = pushDiv("media_wrap clearfix");
	result.append(pushDiv("media_giveaway"));

	result.append(pushDiv("section_title bold"));
	result.append((data.quantity > 1)
		? SerializeString("Giveaway Prizes")
		: SerializeString("Giveaway Prize"));
	result.append(popTag());

	{
		result.append(pushDiv("section_body"));
		result.append("<b>"
			+ Data::NumberToString(data.quantity)
			+ "</b> "
			+ SerializeString(data.additionalPrize.toUtf8()));
		result.append(popTag());
		result.append(pushDiv("section_title bold"));
		result.append(SerializeString("with"));
		result.append(popTag());
	};
	result.append(pushDiv("section_body"));
	if (data.credits > 0) {
		result.append("<b>"
			+ Data::NumberToString(data.credits)
			+ (SerializeString(data.credits == 1 ? (" Star") : (" Stars")))
			+ "</b> " + SerializeString("will be distributed ")
			+ ((data.quantity == 1)
				? SerializeString("to ")
					+ "<b>"
					+ Data::NumberToString(data.quantity)
					+ "</b> " + SerializeString("winner.")
				: SerializeString("among ")
					+ "<b>"
					+ Data::NumberToString(data.quantity)
					+ "</b> " + SerializeString("winners.")));
	} else {
		result.append("<b>"
			+ Data::NumberToString(data.quantity)
			+ "</b> "
			+ SerializeString((data.quantity > 1)
				? "Telegram Premium Subscriptions"
				: "Telegram Premium Subscription")
			+ " for <b>" + Data::NumberToString(data.months) + "</b> "
			+ (data.months > 1 ? "months." : "month."));
	}
	result.append(popTag());

	result.append(pushDiv("section_title bold"));
	result.append(SerializeString("Participants"));
	result.append(popTag());
	result.append(pushDiv("section_body"));
	auto channels = QByteArrayList();
	auto anyChannel = false;
	auto anyGroup = false;
	for (const auto &channel : data.channels) {
		if (const auto chat = peers.peer(channel).chat()) {
			if (chat->isBroadcast) {
				anyChannel = true;
			} else if (chat->isSupergroup) {
				anyGroup = true;
			}
		}
		channels.append("<b>" + peers.wrapPeerName(channel) + "</b>");
	}

	const auto participants = [&] {
		if (data.all && !anyGroup && anyChannel && channels.size() == 1) {
			return "All subscribers of the channel:";
		}
		if (data.all && !anyGroup && anyChannel && channels.size() > 1) {
			return "All subscribers of the channels:";
		}
		if (data.all && anyGroup && !anyChannel && channels.size() == 1) {
			return "All members of the group:";
		}
		if (data.all && anyGroup && !anyChannel && channels.size() > 1) {
			return "All members of the groups:";
		}
		if (data.all && anyGroup && anyChannel && channels.size() == 1) {
			return "All members of the group:";
		}
		if (data.all && anyGroup && anyChannel && channels.size() > 1) {
			return "All members of the groups and channels:";
		}
		if (!data.all && !anyGroup && anyChannel && channels.size() == 1) {
			return "All users who joined the channel below after this date:";
		}
		if (!data.all && !anyGroup && anyChannel && channels.size() > 1) {
			return "All users who joined the channels below after this date:";
		}
		if (!data.all && anyGroup && !anyChannel && channels.size() == 1) {
			return "All users who joined the group below after this date:";
		}
		if (!data.all && anyGroup && !anyChannel && channels.size() > 1) {
			return "All users who joined the groups below after this date:";
		}
		if (!data.all && anyGroup && anyChannel && channels.size() == 1) {
			return "All users who joined the group below after this date:";
		}
		if (!data.all && anyGroup && anyChannel && channels.size() > 1) {
			return "All users who joined the groups and channels below "
				"after this date:";
		}
		return "";
	}();

	result.append(SerializeString(participants)) + channels.join(", ");
	result.append(popTag());

	{
		const auto &instance = Countries::Instance();
		auto countries = QStringList();
		for (const auto &country : data.countries) {
			const auto name = instance.countryNameByISO2(country);
			const auto flag = instance.flagEmojiByISO2(country);
			countries.push_back(flag + QChar(0xA0) + name);
		}

		if (const auto count = countries.size()) {
			auto united = countries.front();
			for (auto i = 1; i != count; ++i) {
				united = ((i + 1 == count)
					? u"%1 and %2"_q
					: u"%1, %2"_q).arg(united, countries[i]);
			}
			result.append(pushDiv("section_body"));
			result.append(
				SerializeString((u"from %1"_q).arg(united).toUtf8()));
			result.append(popTag());
		}
	}
	result.append(pushDiv("section_title bold"));
	result.append(SerializeString("Winners Selection Date"));
	result.append(popTag());
	result.append(pushDiv("section_body"));
	result.append(Data::FormatDateTime(data.untilDate));
	result.append(popTag());

	result.append(popTag());
	result.append(popTag());
	return result;
}

QByteArray HtmlWriter::Wrap::pushGiveaway(
		const PeersMap &peers,
		const Data::GiveawayResults &data,
		Fn<QByteArray(int messageId, QByteArray text)> wrapMessageLink) {
	auto result = pushDiv("media_wrap clearfix");
	result.append(pushDiv("media_giveaway"));

	result.append(pushDiv("section_title bold"));
	result.append((data.winnersCount > 1)
		? SerializeString("Winners Selected!")
		: SerializeString("Winner Selected!"));
	result.append(popTag());

	result.append(pushDiv("section_body"));
	result.append(
		"<b>" + Data::NumberToString(data.winnersCount) + "</b> "
		+ SerializeString((data.winnersCount > 1) ? "winners" : "winner")
		+ " of the "
		+ wrapMessageLink(data.launchId, "Giveaway")
		+ " was randomly selected by Telegram.");
	result.append(popTag());

	result.append(pushDiv("section_title bold"));
	result.append((data.winnersCount > 1)
		? SerializeString("Winners")
		: SerializeString("Winner"));
	result.append(popTag());

	result.append(pushDiv("section_body"));
	auto winners = QByteArrayList();
	for (const auto &winner : data.winners) {
		winners.append("<b>" + peers.wrapPeerName(winner) + "</b>");
	}
	const auto andMore = [&, size = data.winners.size()] {
		if (data.winnersCount > size) {
			return SerializeString(" and ")
				+ Data::NumberToString(data.winnersCount - size)
				+ SerializeString(" more!");
		}
		return QByteArray();
	}();
	result.append(winners.join(", ") + andMore);
	result.append(popTag());

	result.append(pushDiv("section_body"));
	const auto prize = [&, singleStar = (data.credits == 1)] {
		if (data.credits && data.winnersCount == 1) {
			return SerializeString("The winner received ")
				+ "<b>"
				+ Data::NumberToString(data.credits)
				+ "</b>"
				+ SerializeString(singleStar ? " Star." : " Stars.");
		} else if (data.credits && data.winnersCount > 1) {
			return SerializeString("All winners received ")
				+ "<b>"
				+ Data::NumberToString(data.credits)
				+ "</b>"
				+ SerializeString(singleStar
					? " Star in total."
					: " Stars in total.");
		} else if (data.unclaimedCount) {
			return SerializeString("Some winners couldn't be selected.");
		} else if (data.winnersCount == 1) {
			return SerializeString(
				"The winner received their gift link in a private message.");
		} else if (data.winnersCount > 1) {
			return SerializeString(
				"All winners received gift links in private messages.");
		}
		return QByteArray();
	}();
	result.append(prize);
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
			using State = ActionPhoneCall::State;
			const auto state = call->state;
			if (state == State::Invitation) {
				return "Invitation";
			} else if (state == State::Active) {
				return "Ongoing";
			} else if (message.out) {
				return (state == State::Missed) ? "Cancelled" : "Outgoing";
			} else if (state == State::Missed) {
				return "Missed";
			} else if (state == State::Busy) {
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
			result = PrepareAudioMediaData(data);
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
	}, [](const TodoList &data) {
	}, [](const GiveawayStart &data) {
	}, [](const GiveawayResults &data) {
	}, [&](const PaidMedia &data) {
		result.classes = "media_invoice";
		result.status = Data::FormatMoneyAmount(data.stars, "XTR");
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
	} else if (!peerIsUser(message.forwardedFromId)) {
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
		"images/section_music.png",
		"images/section_other.png",
		"images/section_photos.png",
		"images/section_sessions.png",
		"images/section_stories.png",
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

	_selfColorIndex = data.user.info.colorIndex;
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
		kUserpicsPriority,
		"Profile pictures",
		"photos",
		_userpicsCount,
		userpicsFilePath());
}

Result HtmlWriter::writeStoriesStart(const Data::StoriesInfo &data) {
	Expects(_summary != nullptr);
	Expects(_stories == nullptr);

	_storiesCount = data.count;
	if (!_storiesCount) {
		return Result::Success();
	}
	_stories = fileWithRelativePath(storiesFilePath());

	auto block = _stories->pushHeader(
		"Stories archive",
		mainFileRelativePath());
	block.append(_stories->pushDiv("page_body list_page"));
	block.append(_stories->pushDiv("entry_list"));
	if (const auto result = _stories->writeBlock(block); !result) {
		return result;
	}
	return Result::Success();
}

Result HtmlWriter::writeStoriesSlice(const Data::StoriesSlice &data) {
	Expects(_stories != nullptr);

	_storiesCount -= data.skipped;
	if (data.list.empty()) {
		return Result::Success();
	}
	auto block = QByteArray();
	for (const auto &story : data.list) {
		auto data = StoryData{};
		using SkipReason = Data::File::SkipReason;
		const auto &file = story.file();
		Assert(!file.relativePath.isEmpty()
			|| file.skipReason != SkipReason::None);
		auto status = QByteArrayList();
		if (story.pinned) {
			status.append("Saved to Profile");
		}
		if (story.expires > 0) {
			status.append("Expiring: " + Data::FormatDateTime(story.expires));
		}
		status.append([&]() -> Data::Utf8String {
			switch (file.skipReason) {
			case SkipReason::Unavailable:
				return "(Story unavailable, please try again later)";
			case SkipReason::FileSize:
				return "(Story exceeds maximum size. "
					"Change data exporting settings to download.)";
			case SkipReason::FileType:
				return "(Story not included. "
					"Change data exporting settings to download.)";
			case SkipReason::None: return Data::FormatFileSize(file.size);
			}
			Unexpected("Skip reason while writing story path.");
		}());
		const auto &path = story.file().relativePath;
		const auto &image = story.thumb().file.relativePath.isEmpty()
			? story.file().relativePath
			: story.thumb().file.relativePath;
		data.imageLink = Data::WriteImageThumb(
			_settings.path,
			image,
			kStoryThumbWidth * 2,
			kStoryThumbHeight * 2);
		const auto info = (story.date > 0)
			? Data::FormatDateTime(story.date)
			: QByteArray();
		block.append(_stories->pushStoriesListEntry(
			data,
			(path.isEmpty() ? QString("Story unavailable") : path).toUtf8(),
			status,
			info,
			story.caption,
			_environment.internalLinksDomain,
			path));
	}
	return _stories->writeBlock(block);
}

Result HtmlWriter::writeStoriesEnd() {
	pushStoriesSection();
	if (_stories) {
		return base::take(_stories)->close();
	}
	return Result::Success();
}

Result HtmlWriter::writeProfileMusicStart(const Data::ProfileMusicInfo &data) {
	Expects(_summary != nullptr);
	Expects(_profileMusic == nullptr);

	_profileMusicCount = data.count;
	if (!_profileMusicCount) {
		return Result::Success();
	}
	_profileMusic = fileWithRelativePath(profileMusicFilePath());

	auto block = _profileMusic->pushHeader(
		"Profile Music",
		mainFileRelativePath());
	block.append(_profileMusic->pushDiv("page_body list_page"));
	block.append(_profileMusic->pushDiv("entry_list"));
	if (const auto result = _profileMusic->writeBlock(block); !result) {
		return result;
	}
	return Result::Success();
}

Result HtmlWriter::writeProfileMusicSlice(const Data::ProfileMusicSlice &data) {
	Expects(_profileMusic != nullptr);

	_profileMusicCount -= data.skipped;
	if (data.list.empty()) {
		return Result::Success();
	}
	auto block = QByteArray();
	for (const auto &message : data.list) {
		if (!v::is<Data::Document>(message.media.content)) {
			continue;
		}
		const auto &doc = v::get<Data::Document>(message.media.content);
		if (!doc.isAudioFile) {
			continue;
		}
		using SkipReason = Data::File::SkipReason;
		const auto &file = doc.file;
		Assert(!file.relativePath.isEmpty()
			|| file.skipReason != SkipReason::None);
		auto status = QByteArrayList();
		status.append([&]() -> Data::Utf8String {
			switch (file.skipReason) {
			case SkipReason::Unavailable:
				return "(File unavailable, please try again later)";
			case SkipReason::FileSize:
				return "(File exceeds maximum size. "
					"Change data exporting settings to download.)";
			case SkipReason::FileType:
				return "(File not included. "
					"Change data exporting settings to download.)";
			case SkipReason::None: return Data::FormatFileSize(file.size);
			}
			Unexpected("Skip reason while writing profile music path.");
		}());
		const auto &path = file.relativePath;
		const auto title = !doc.songTitle.isEmpty()
			? doc.songTitle
			: !doc.name.isEmpty()
			? doc.name
			: Data::Utf8String("Unknown Track");
		const auto performer = !doc.songPerformer.isEmpty()
			? doc.songPerformer
			: Data::Utf8String("Unknown Artist");
		const auto info = performer + " - " + title;
		const auto duration = doc.duration > 0
			? Data::FormatDuration(doc.duration)
			: QByteArray();
		block.append(_profileMusic->pushAudioEntry(
			(path.isEmpty() ? QString("File unavailable") : path).toUtf8(),
			info,
			status,
			duration,
			path));
	}
	return _profileMusic->writeBlock(block);
}

Result HtmlWriter::writeProfileMusicEnd() {
	pushProfileMusicSection();
	if (_profileMusic) {
		return base::take(_profileMusic)->close();
	}
	return Result::Success();
}

QString HtmlWriter::storiesFilePath() const {
	return "lists/stories.html";
}

void HtmlWriter::pushStoriesSection() {
	pushSection(
		kStoriesPriority,
		"Stories archive",
		"stories",
		_storiesCount,
		storiesFilePath());
}

QString HtmlWriter::profileMusicFilePath() const {
	return "lists/profile_music.html";
}

void HtmlWriter::pushProfileMusicSection() {
	pushSection(
		kProfileMusicPriority,
		"Profile Music",
		"music",
		_profileMusicCount,
		profileMusicFilePath());
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
		if (contact.userId) {
			const auto raw = contact.userId.bare & PeerId::kChatTypeMask;
			userpic.tooltip = (u"ID: "_q + QString::number(raw)).toUtf8();
		}
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
		kContactsPriority,
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
				Data::PeerColorIndex(top.peer.id()),
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
		kFrequentContactsPriority,
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
		kSessionsPriority,
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
		kWebSessionsPriority,
		"Web sessions",
		"web",
		data.webList.size(),
		filename);
	return Result::Success();
}

Result HtmlWriter::writeOtherData(const Data::File &data) {
	Expects(_summary != nullptr);

	pushSection(
		kOtherPriority,
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
		kChatsPriority,
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
		const auto &[info, content] = _chat->pushMessage(
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
		case Type::VerifyCodes:
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
		case Type::VerifyCodes:
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
		} else if (dialog.type == Type::VerifyCodes) {
			return "Verification Codes";
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
		((_dialog.type == Type::Self
			|| _dialog.type == Type::Replies
			|| _dialog.type == Type::VerifyCodes)
			? kSavedMessagesColorIndex
			: Data::PeerColorIndex(_dialog.peerId)),
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
