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

#include <QtCore/QFile>

namespace Export {
namespace Output {
namespace {

constexpr auto kMessagesInFile = 1000;

const auto kLineBreak = QByteArrayLiteral("<br>");

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

void SerializeMultiline(
		QByteArray &appendTo,
		const QByteArray &value,
		int newline) {
	const auto data = value.data();
	auto offset = 0;
	do {
		appendTo.append("> ");
		const auto win = (newline > 0 && *(data + newline - 1) == '\r');
		if (win) --newline;
		appendTo.append(data + offset, newline - offset).append(kLineBreak);
		if (win) ++newline;
		offset = newline + 1;
		newline = value.indexOf('\n', offset);
	} while (newline > 0);
	if (const auto size = value.size(); size > offset) {
		appendTo.append("> ");
		appendTo.append(data + offset, size - offset).append(kLineBreak);
	}
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

QByteArray SerializeKeyValue(
		std::vector<std::pair<QByteArray, QByteArray>> &&values) {
	auto result = QByteArray();
	for (const auto &[key, value] : values) {
		if (value.isEmpty()) {
			continue;
		}
		result.append(key);
		if (const auto newline = value.indexOf('\n'); newline >= 0) {
			result.append(':').append(kLineBreak);
			SerializeMultiline(result, value, newline);
		} else {
			result.append(": ").append(value).append(kLineBreak);
		}
	}
	return result;
}

QByteArray SerializeBlockquote(
		std::vector<std::pair<QByteArray, QByteArray>> &&values) {
	return "<blockquote>"
		+ SerializeKeyValue(std::move(values))
		+ "</blockquote>";
}

Data::Utf8String FormatUsername(const Data::Utf8String &username) {
	return username.isEmpty() ? username : ('@' + username);
}

QByteArray FormatFilePath(const Data::File &file) {
	return file.relativePath.toUtf8();
}

QByteArray SerializeLink(
		const Data::Utf8String &text,
		const QString &path) {
	return "<a href=\"" + path.toUtf8() + "\">" + text + "</a>";
}

QByteArray SerializeMessage(
		Fn<QString(QString)> relativePath,
		const Data::Message &message,
		const std::map<Data::PeerId, Data::Peer> &peers,
		const QString &internalLinksDomain) {
	using namespace Data;

	if (message.media.content.is<UnsupportedMedia>()) {
		return SerializeString("Error! This message is not supported "
			"by this version of Telegram Desktop. "
			"Please update the application.");
	}

	const auto peer = [&](PeerId peerId) -> const Peer& {
		if (const auto i = peers.find(peerId); i != end(peers)) {
			return i->second;
		}
		static auto empty = Peer{ User() };
		return empty;
	};
	const auto user = [&](int32 userId) -> const User& {
		if (const auto result = peer(UserPeerId(userId)).user()) {
			return *result;
		}
		static auto empty = User();
		return empty;
	};
	const auto chat = [&](int32 chatId) -> const Chat& {
		if (const auto result = peer(ChatPeerId(chatId)).chat()) {
			return *result;
		}
		static auto empty = Chat();
		return empty;
	};

	auto values = std::vector<std::pair<QByteArray, QByteArray>>{
		{ "ID", SerializeString(NumberToString(message.id)) },
		{ "Date", SerializeString(FormatDateTime(message.date)) },
		{ "Edited", SerializeString(FormatDateTime(message.edited)) },
	};
	const auto pushBare = [&](
			const QByteArray &key,
			const QByteArray &value) {
		values.emplace_back(key, value);
	};
	const auto push = [&](const QByteArray &key, const QByteArray &value) {
		if (!value.isEmpty()) {
			pushBare(key, SerializeString(value));
		}
	};
	const auto wrapPeerName = [&](PeerId peerId) {
		const auto result = peer(peerId).name();
		return result.isEmpty() ? QByteArray("(deleted peer)") : result;
	};
	const auto wrapUserName = [&](int32 userId) {
		const auto result = user(userId).name();
		return result.isEmpty() ? QByteArray("(deleted user)") : result;
	};
	const auto pushFrom = [&](const QByteArray &label = "From") {
		if (message.fromId) {
			push(label, wrapUserName(message.fromId));
		}
	};
	const auto pushReplyToMsgId = [&](
			const QByteArray &label = "Reply to message") {
		if (message.replyToMsgId) {
			push(label, "ID-" + NumberToString(message.replyToMsgId));
		}
	};
	const auto pushUserNames = [&](
			const std::vector<int32> &data,
			const QByteArray &labelOne = "Member",
			const QByteArray &labelMany = "Members") {
		auto list = std::vector<QByteArray>();
		for (const auto userId : data) {
			list.push_back(SerializeString(wrapUserName(userId)));
		}
		if (list.size() == 1) {
			pushBare(labelOne, list[0]);
		} else if (!list.empty()) {
			pushBare(labelMany, JoinList(", ", list));
		}
	};
	const auto pushActor = [&] {
		pushFrom("Actor");
	};
	const auto pushAction = [&](const QByteArray &action) {
		push("Action", action);
	};
	const auto pushTTL = [&](
		const QByteArray &label = "Self destruct period") {
		if (const auto ttl = message.media.ttl) {
			push(label, NumberToString(ttl) + " sec.");
		}
	};

	using SkipReason = Data::File::SkipReason;
	const auto formatPath = [&](
			const Data::File &file,
			const QByteArray &label,
			const QByteArray &name = QByteArray()) {
		Expects(!file.relativePath.isEmpty()
			|| file.skipReason != SkipReason::None);

		const auto pre = name.isEmpty()
			? QByteArray()
			: SerializeString(name + ' ');
		switch (file.skipReason) {
		case SkipReason::Unavailable:
			return pre + "(" + label + " unavailable, "
				"please try again later)";
		case SkipReason::FileSize:
			return pre + "(" + label + " exceeds maximum size. "
				"Change data exporting settings to download.)";
		case SkipReason::FileType:
			return pre + "(" + label + " not included. "
				"Change data exporting settings to download.)";
		case SkipReason::None: return SerializeLink(
			FormatFilePath(file),
			relativePath(file.relativePath));
		}
		Unexpected("Skip reason while writing file path.");
	};
	const auto pushPath = [&](
			const Data::File &file,
			const QByteArray &label,
			const QByteArray &name = QByteArray()) {
		pushBare(label, formatPath(file, label, name));
	};
	const auto pushPhoto = [&](const Image &image) {
		pushPath(image.file, "Photo");
		if (image.width && image.height) {
			push("Width", NumberToString(image.width));
			push("Height", NumberToString(image.height));
		}
	};

	message.action.content.match([&](const ActionChatCreate &data) {
		pushActor();
		pushAction("Create group");
		push("Title", data.title);
		pushUserNames(data.userIds);
	}, [&](const ActionChatEditTitle &data) {
		pushActor();
		pushAction("Edit group title");
		push("New title", data.title);
	}, [&](const ActionChatEditPhoto &data) {
		pushActor();
		pushAction("Edit group photo");
		pushPhoto(data.photo.image);
	}, [&](const ActionChatDeletePhoto &data) {
		pushActor();
		pushAction("Delete group photo");
	}, [&](const ActionChatAddUser &data) {
		pushActor();
		pushAction("Invite members");
		pushUserNames(data.userIds);
	}, [&](const ActionChatDeleteUser &data) {
		pushActor();
		pushAction("Remove members");
		push("Member", wrapUserName(data.userId));
	}, [&](const ActionChatJoinedByLink &data) {
		pushActor();
		pushAction("Join group by link");
		push("Inviter", wrapUserName(data.inviterId));
	}, [&](const ActionChannelCreate &data) {
		pushActor();
		pushAction("Create channel");
		push("Title", data.title);
	}, [&](const ActionChatMigrateTo &data) {
		pushActor();
		pushAction("Convert this group to supergroup");
	}, [&](const ActionChannelMigrateFrom &data) {
		pushActor();
		pushAction("Basic group converted to supergroup");
		push("Title", data.title);
	}, [&](const ActionPinMessage &data) {
		pushActor();
		pushAction("Pin message");
		pushReplyToMsgId("Message");
	}, [&](const ActionHistoryClear &data) {
		pushActor();
		pushAction("Clear history");
	}, [&](const ActionGameScore &data) {
		pushActor();
		pushAction("Score in a game");
		pushReplyToMsgId("Game message");
		push("Score", NumberToString(data.score));
	}, [&](const ActionPaymentSent &data) {
		pushAction("Send payment");
		push(
			"Amount",
			Data::FormatMoneyAmount(data.amount, data.currency));
		pushReplyToMsgId("Invoice message");
	}, [&](const ActionPhoneCall &data) {
		pushActor();
		pushAction("Phone call");
		if (data.duration) {
			push("Duration", NumberToString(data.duration) + " sec.");
		}
		using Reason = ActionPhoneCall::DiscardReason;
		push("Discard reason", [&] {
			switch (data.discardReason) {
			case Reason::Busy: return "Busy";
			case Reason::Disconnect: return "Disconnect";
			case Reason::Hangup: return "Hangup";
			case Reason::Missed: return "Missed";
			}
			return "";
		}());
	}, [&](const ActionScreenshotTaken &data) {
		pushActor();
		pushAction("Take screenshot");
	}, [&](const ActionCustomAction &data) {
		pushActor();
		push("Information", data.message);
	}, [&](const ActionBotAllowed &data) {
		pushAction("Allow sending messages");
		push("Reason", "Login on \"" + data.domain + "\"");
	}, [&](const ActionSecureValuesSent &data) {
		pushAction("Send Telegram Passport values");
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
		if (list.size() == 1) {
			push("Value", list[0]);
		} else if (!list.empty()) {
			push("Values", JoinList(", ", list));
		}
	}, [](const base::none_type &) {});

	if (!message.action.content) {
		pushFrom();
		push("Author", message.signature);
		if (message.forwardedFromId) {
			push("Forwarded from", wrapPeerName(message.forwardedFromId));
		}
		if (message.savedFromChatId) {
			push("Saved from", wrapPeerName(message.savedFromChatId));
		}
		pushReplyToMsgId();
		if (message.viaBotId) {
			push("Via", user(message.viaBotId).username);
		}
	}

	message.media.content.match([&](const Photo &photo) {
		pushPhoto(photo.image);
		pushTTL();
	}, [&](const Document &data) {
		const auto pushMyPath = [&](const QByteArray &label) {
			return pushPath(data.file, label);
		};
		if (data.isSticker) {
			pushMyPath("Sticker");
			push("Emoji", data.stickerEmoji);
		} else if (data.isVideoMessage) {
			pushMyPath("Video message");
		} else if (data.isVoiceMessage) {
			pushMyPath("Voice message");
		} else if (data.isAnimated) {
			pushMyPath("Animation");
		} else if (data.isVideoFile) {
			pushMyPath("Video file");
		} else if (data.isAudioFile) {
			pushMyPath("Audio file");
			push("Performer", data.songPerformer);
			push("Title", data.songTitle);
		} else {
			pushMyPath("File");
		}
		if (!data.isSticker) {
			push("Mime type", data.mime);
		}
		if (data.duration) {
			push("Duration", NumberToString(data.duration) + " sec.");
		}
		if (data.width && data.height) {
			push("Width", NumberToString(data.width));
			push("Height", NumberToString(data.height));
		}
		pushTTL();
	}, [&](const SharedContact &data) {
		pushBare("Contact information", SerializeBlockquote({
			{ "First name", data.info.firstName },
			{ "Last name", data.info.lastName },
			{ "Phone number", FormatPhoneNumber(data.info.phoneNumber) },
			{ "vCard", (data.vcard.content.isEmpty()
				? QByteArray()
				: formatPath(data.vcard, "vCard")) }
		}));
	}, [&](const GeoPoint &data) {
		pushBare("Location", data.valid ? SerializeBlockquote({
			{ "Latitude", NumberToString(data.latitude) },
			{ "Longitude", NumberToString(data.longitude) },
		}) : QByteArray("(empty value)"));
		pushTTL("Live location period");
	}, [&](const Venue &data) {
		push("Place name", data.title);
		push("Address", data.address);
		if (data.point.valid) {
			pushBare("Location", SerializeBlockquote({
				{ "Latitude", NumberToString(data.point.latitude) },
				{ "Longitude", NumberToString(data.point.longitude) },
			}));
		}
	}, [&](const Game &data) {
		push("Game", data.title);
		push("Description", data.description);
		if (data.botId != 0 && !data.shortName.isEmpty()) {
			const auto bot = user(data.botId);
			if (bot.isBot && !bot.username.isEmpty()) {
				push("Link", internalLinksDomain.toUtf8()
					+ bot.username
					+ "?game="
					+ data.shortName);
			}
		}
	}, [&](const Invoice &data) {
		pushBare("Invoice", SerializeBlockquote({
			{ "Title", data.title },
			{ "Description", data.description },
			{
				"Amount",
				Data::FormatMoneyAmount(data.amount, data.currency)
			},
			{ "Receipt message", (data.receiptMsgId
				? "ID-" + NumberToString(data.receiptMsgId)
				: QByteArray()) }
		}));
	}, [](const UnsupportedMedia &data) {
		Unexpected("Unsupported message.");
	}, [](const base::none_type &) {});

	auto value = JoinList(QByteArray(), ranges::view::all(
		message.text
	) | ranges::view::transform([&](const Data::TextPart &part) {
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
		case Type::Hashtag: return "<a href=\"#hash-"
			+ text.mid(1)
			+ "\">" + text + "</a>";
		case Type::BotCommand: return "<a href=\"#command-"
			+ text.mid(1)
			+ "\">" + text + "</a>";
		case Type::Url: return "<a href=\""
			+ text
			+ "\">" + text + "</a>";
		case Type::Email: return "<a href=\"mailto:"
			+ text
			+ "\">" + text + "</a>";
		case Type::Bold: return "<b>" + text + "</b>";
		case Type::Italic: return "<i>" + text + "</i>";
		case Type::Code: return "<code>" + text + "</code>";
		case Type::Pre: return "<pre>" + text + "</pre>";
		case Type::TextUrl: return "<a href=\""
			+ SerializeString(part.additional)
			+ "\">" + text + "</a>";
		case Type::MentionName: return "<a href=\"#mention-"
			+ part.additional
			+ "\">" + text + "</a>";
		case Type::Phone: return "<a href=\"tel:"
			+ text
			+ "\">" + text + "</a>";
		case Type::Cashtag: return "<a href=\"#cash-"
			+ text.mid(1)
			+ "\">" + text + "</a>";
		}
		Unexpected("Type in text entities serialization.");
	}) | ranges::to_vector);
	pushBare("Text", value);

	return SerializeKeyValue(std::move(values));
}

} // namespace

class HtmlWriter::Wrap {
public:
	Wrap(const QString &path, const QString &base, Stats *stats);

	[[nodiscard]] bool empty() const;

	[[nodiscard]] Result writeBlock(const QByteArray &block);

	[[nodiscard]] Result close();

	[[nodiscard]] QString relativePath(const QString &path) const;
	[[nodiscard]] QString relativePath(const Data::File &file) const;

	~Wrap();

private:
	QByteArray begin() const;
	QByteArray end() const;

	File _file;
	bool _closed = false;
	QByteArray _base;

};

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
}

bool HtmlWriter::Wrap::empty() const {
	return _file.empty();
}

Result HtmlWriter::Wrap::writeBlock(const QByteArray &block) {
	Expects(!_closed);

	const auto result = [&] {
		if (block.isEmpty()) {
			return _file.writeBlock(block);
		} else if (_file.empty()) {
			return _file.writeBlock(begin() + block);
		}
		return _file.writeBlock(block);
	}();
	if (!result) {
		_closed = true;
	}
	return result;
}

Result HtmlWriter::Wrap::close() {
	if (!std::exchange(_closed, true) && !_file.empty()) {
		return _file.writeBlock(end());
	}
	return Result::Success();
}

QString HtmlWriter::Wrap::relativePath(const QString &path) const {
	return _base + path;
}

QString HtmlWriter::Wrap::relativePath(const Data::File &file) const {
	return relativePath(file.relativePath);
}

QByteArray HtmlWriter::Wrap::begin() const {
	return "\
<!DOCTYPE html>\n\
<html>\n\
<head>\n\
	<meta charset=\"utf-8\">\n\
	<title>Exported Data</title>\n\
	<meta name=\"viewport\" "
	"content=\"width=device-width, initial-scale=1.0\">\n\
	<link href=\"" + _base + "css/style.css\" rel=\"stylesheet\">\n\
</head>\n\
<body>\n\
<div class=\"container page_wrap\">\n";
}

QByteArray HtmlWriter::Wrap::end() const {
	return "\
</div>\n\
</body>\n\
</html>\n";
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
	_summary = fileWithRelativePath(mainFileRelativePath());

	//const auto result = copyFile(
	//	":/export/css/bootstrap.min.css",
	//	"css/bootstrap.min.css");
	//if (!result) {
	//	return result;
	//}
	const auto result = copyFile(":/export/css/style.css", "css/style.css");
	if (!result) {
		return result;
	}
	return _summary->writeBlock(
		MakeLinks(SerializeString(_environment.aboutTelegram))
			+ kLineBreak
			+ kLineBreak);
}

Result HtmlWriter::writePersonal(const Data::PersonalInfo &data) {
	Expects(_summary != nullptr);

	const auto &info = data.user.info;
	const auto serialized = SerializeKeyValue({
		{ "First name", SerializeString(info.firstName) },
		{ "Last name", SerializeString(info.lastName) },
		{
			"Phone number",
			SerializeString(Data::FormatPhoneNumber(info.phoneNumber))
		},
		{ "Username", SerializeString(FormatUsername(data.user.username)) },
		{ "Bio", SerializeString(data.bio) },
		})
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(serialized);
}

Result HtmlWriter::writeUserpicsStart(const Data::UserpicsInfo &data) {
	Expects(_summary != nullptr);
	Expects(_userpics == nullptr);

	_userpicsCount = data.count;
	if (!_userpicsCount) {
		return Result::Success();
	}
	const auto filename = "lists/profile_pictures.html";
	_userpics = fileWithRelativePath(filename);

	const auto serialized = SerializeLink(
		"Profile pictures "
		"(" + Data::NumberToString(_userpicsCount) + ")",
		_summary->relativePath(filename))
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(serialized);
}

Result HtmlWriter::writeUserpicsSlice(const Data::UserpicsSlice &data) {
	Expects(_userpics != nullptr);
	Expects(!data.list.empty());

	auto lines = std::vector<QByteArray>();
	lines.reserve(data.list.size());
	for (const auto &userpic : data.list) {
		if (!userpic.date) {
			lines.push_back("(deleted photo)");
		} else {
			using SkipReason = Data::File::SkipReason;
			const auto &file = userpic.image.file;
			Assert(!file.relativePath.isEmpty()
				|| file.skipReason != SkipReason::None);
			const auto path = [&]() -> Data::Utf8String {
				switch (file.skipReason) {
				case SkipReason::Unavailable:
					return "(Photo unavailable, please try again later)";
				case SkipReason::FileSize:
					return "(Photo exceeds maximum size. "
						"Change data exporting settings to download.)";
				case SkipReason::FileType:
					return "(Photo not included. "
						"Change data exporting settings to download.)";
				case SkipReason::None: return SerializeLink(
					FormatFilePath(file),
					_userpics->relativePath(file.relativePath));
				}
				Unexpected("Skip reason while writing photo path.");
			}();
			lines.push_back(SerializeKeyValue({
				{
					"Added",
					SerializeString(Data::FormatDateTime(userpic.date))
				},
				{ "Photo", path },
			}));
		}
	}
	return _userpics->writeBlock(JoinList(kLineBreak, lines) + kLineBreak);
}

Result HtmlWriter::writeUserpicsEnd() {
	if (_userpics) {
		return base::take(_userpics)->close();
	}
	return Result::Success();
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
	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	for (const auto index : Data::SortedContactsIndices(data)) {
		const auto &contact = data.list[index];
		if (contact.firstName.isEmpty()
			&& contact.lastName.isEmpty()
			&& contact.phoneNumber.isEmpty()) {
			list.push_back("(deleted user)" + kLineBreak);
		} else {
			list.push_back(SerializeKeyValue({
				{ "First name", SerializeString(contact.firstName) },
				{ "Last name", SerializeString(contact.lastName) },
				{
					"Phone number",
					SerializeString(
						Data::FormatPhoneNumber(contact.phoneNumber))
				},
				{
					"Added",
					SerializeString(Data::FormatDateTime(contact.date))
				}
			}));
		}
	}
	const auto full = MakeLinks(SerializeString(_environment.aboutContacts))
		+ kLineBreak
		+ kLineBreak
		+ JoinList(kLineBreak, list);
	if (const auto result = file->writeBlock(full); !result) {
		return result;
	} else if (const auto closed = file->close(); !closed) {
		return closed;
	}

	const auto header = SerializeLink(
		"Contacts "
		"(" + Data::NumberToString(data.list.size()) + ")",
		_summary->relativePath(filename))
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(header);
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
	auto list = std::vector<QByteArray>();
	list.reserve(size);
	const auto writeList = [&](
			const std::vector<Data::TopPeer> &peers,
			Data::Utf8String category) {
		for (const auto &top : peers) {
			const auto user = [&]() -> Data::Utf8String {
				if (!top.peer.user() || top.peer.user()->isSelf) {
					return Data::Utf8String();
				} else if (top.peer.name().isEmpty()) {
					return "(deleted user)";
				}
				return top.peer.name();
			}();
			const auto chatType = [&] {
				if (const auto chat = top.peer.chat()) {
					return chat->username.isEmpty()
						? (chat->isBroadcast
							? "Private channel"
							: (chat->isSupergroup
								? "Private supergroup"
								: "Private group"))
						: (chat->isBroadcast
							? "Public channel"
							: "Public supergroup");
				}
				return "";
			}();
			const auto chat = [&]() -> Data::Utf8String {
				if (!top.peer.chat()) {
					return Data::Utf8String();
				} else if (top.peer.name().isEmpty()) {
					return "(deleted chat)";
				}
				return top.peer.name();
			}();
			const auto saved = [&]() -> Data::Utf8String {
				if (!top.peer.user() || !top.peer.user()->isSelf) {
					return Data::Utf8String();
				}
				return "Saved messages";
			}();
			list.push_back(SerializeKeyValue({
				{ "Category", SerializeString(category) },
				{
					"User",
					top.peer.user() ? SerializeString(user) : QByteArray()
				},
				{ "Chat", SerializeString(saved) },
				{ chatType, SerializeString(chat) },
				{
					"Rating",
					SerializeString(Data::NumberToString(top.rating))
				}
			}));
		}
	};
	writeList(data.correspondents, "People");
	writeList(data.inlineBots, "Inline bots");
	writeList(data.phoneCalls, "Calls");
	const auto full = MakeLinks(SerializeString(_environment.aboutFrequent))
		+ kLineBreak
		+ kLineBreak
		+ JoinList(kLineBreak, list);
	if (const auto result = file->writeBlock(full); !result) {
		return result;
	} else if (const auto closed = file->close(); !closed) {
		return closed;
	}

	const auto header = SerializeLink(
		"Frequent contacts "
		"(" + Data::NumberToString(size) + ")",
		_summary->relativePath(filename))
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(header);
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
	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	for (const auto &session : data.list) {
		list.push_back(SerializeKeyValue({
			{
				"Last active",
				SerializeString(Data::FormatDateTime(session.lastActive))
			},
			{ "Last IP address", SerializeString(session.ip) },
			{ "Last country", SerializeString(session.country) },
			{ "Last region", SerializeString(session.region) },
			{
				"Application name",
				(session.applicationName.isEmpty()
					? Data::Utf8String("(unknown)")
					: SerializeString(session.applicationName))
			},
			{
				"Application version",
				SerializeString(session.applicationVersion)
			},
			{ "Device model", SerializeString(session.deviceModel) },
			{ "Platform", SerializeString(session.platform) },
			{ "System version", SerializeString(session.systemVersion) },
			{ "Created", Data::FormatDateTime(session.created) },
		}));
	}
	const auto full = MakeLinks(SerializeString(_environment.aboutSessions))
		+ kLineBreak
		+ kLineBreak
		+ JoinList(kLineBreak, list);
	if (const auto result = file->writeBlock(full); !result) {
		return result;
	} else if (const auto closed = file->close(); !closed) {
		return closed;
	}

	const auto header = SerializeLink(
		"Sessions "
		"(" + Data::NumberToString(data.list.size()) + ")",
		_summary->relativePath(filename))
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(header);
}

Result HtmlWriter::writeWebSessions(const Data::SessionsList &data) {
	Expects(_summary != nullptr);

	if (data.webList.empty()) {
		return Result::Success();
	}

	const auto filename = "lists/web_sessions.html";
	const auto file = fileWithRelativePath(filename);
	auto list = std::vector<QByteArray>();
	list.reserve(data.webList.size());
	for (const auto &session : data.webList) {
		list.push_back(SerializeKeyValue({
			{
				"Last active",
				SerializeString(Data::FormatDateTime(session.lastActive))
			},
			{ "Last IP address", SerializeString(session.ip) },
			{ "Last region", SerializeString(session.region) },
			{
				"Bot username",
				(session.botUsername.isEmpty()
					? Data::Utf8String("(unknown)")
					: SerializeString(session.botUsername))
			},
			{
				"Domain name",
				(session.domain.isEmpty()
					? Data::Utf8String("(unknown)")
					: SerializeString(session.domain))
			},
			{ "Browser", SerializeString(session.browser) },
			{ "Platform", SerializeString(session.platform) },
			{
				"Created",
				SerializeString(Data::FormatDateTime(session.created))
			},
		}));
	}
	const auto full = MakeLinks(
		SerializeString(_environment.aboutWebSessions))
		+ kLineBreak
		+ kLineBreak
		+ JoinList(kLineBreak, list);
	if (const auto result = file->writeBlock(full); !result) {
		return result;
	} else if (const auto closed = file->close(); !closed) {
		return closed;
	}

	const auto header = SerializeLink(
		"Web sessions "
		"(" + Data::NumberToString(data.webList.size()) + ")",
		_summary->relativePath(filename))
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(header);
}

Result HtmlWriter::writeOtherData(const Data::File &data) {
	Expects(_summary != nullptr);

	const auto header = SerializeLink(
		"Other data",
		_summary->relativePath(data))
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(header);
}

Result HtmlWriter::writeDialogsStart(const Data::DialogsInfo &data) {
	return writeChatsStart(
		data,
		"Chats",
		_environment.aboutChats,
		"lists/chats.html");
}

Result HtmlWriter::writeDialogStart(const Data::DialogInfo &data) {
	return writeChatStart(data);
}

Result HtmlWriter::writeDialogSlice(const Data::MessagesSlice &data) {
	return writeChatSlice(data);
}

Result HtmlWriter::writeDialogEnd() {
	return writeChatEnd();
}

Result HtmlWriter::writeDialogsEnd() {
	return writeChatsEnd();
}

Result HtmlWriter::writeLeftChannelsStart(const Data::DialogsInfo &data) {
	return writeChatsStart(
		data,
		"Left chats",
		_environment.aboutLeftChats,
		"lists/left_chats.html");
}

Result HtmlWriter::writeLeftChannelStart(const Data::DialogInfo &data) {
	return writeChatStart(data);
}

Result HtmlWriter::writeLeftChannelSlice(const Data::MessagesSlice &data) {
	return writeChatSlice(data);
}

Result HtmlWriter::writeLeftChannelEnd() {
	return writeChatEnd();
}

Result HtmlWriter::writeLeftChannelsEnd() {
	return writeChatsEnd();
}

Result HtmlWriter::writeChatsStart(
		const Data::DialogsInfo &data,
		const QByteArray &listName,
		const QByteArray &about,
		const QString &fileName) {
	Expects(_summary != nullptr);
	Expects(_chats == nullptr);

	if (data.list.empty()) {
		return Result::Success();
	}

	_chats = fileWithRelativePath(fileName);
	_dialogIndex = 0;
	_dialogsCount = data.list.size();

	const auto block = MakeLinks(SerializeString(about)) + kLineBreak;
	if (const auto result = _chats->writeBlock(block); !result) {
		return result;
	}

	const auto header = SerializeLink(
		listName + " "
		"(" + Data::NumberToString(data.list.size()) + ")",
		_summary->relativePath(fileName))
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(header);
}

Result HtmlWriter::writeChatStart(const Data::DialogInfo &data) {
	Expects(_chat == nullptr);
	Expects(_dialogIndex < _dialogsCount);

	const auto digits = Data::NumberToString(_dialogsCount - 1).size();
	const auto number = Data::NumberToString(++_dialogIndex, digits, '0');
	_chat = fileWithRelativePath(data.relativePath + messagesFile(0));
	_messagesCount = 0;
	_dialog = data;
	return Result::Success();
}

Result HtmlWriter::writeChatSlice(const Data::MessagesSlice &data) {
	Expects(_chat != nullptr);
	Expects(!data.list.empty());

	const auto wasIndex = (_messagesCount / kMessagesInFile);
	_messagesCount += data.list.size();
	const auto nowIndex = (_messagesCount / kMessagesInFile);
	if (nowIndex != wasIndex) {
		if (const auto result = switchToNextChatFile(nowIndex); !result) {
			return result;
		}
	}

	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	for (const auto &message : data.list) {
		list.push_back(SerializeMessage(
			[&](QString path) { return _chat->relativePath(path); },
			message,
			data.peers,
			_environment.internalLinksDomain));
	}
	const auto full = _chat->empty()
		? JoinList(kLineBreak, list)
		: kLineBreak + JoinList(kLineBreak, list);
	return _chat->writeBlock(full);
}

Result HtmlWriter::writeChatEnd() {
	Expects(_chats != nullptr);
	Expects(_chat != nullptr);

	if (const auto closed = base::take(_chat)->close(); !closed) {
		return closed;
	}

	using Type = Data::DialogInfo::Type;
	const auto TypeString = [](Type type) {
		switch (type) {
		case Type::Unknown: return "(unknown)";
		case Type::Self:
		case Type::Personal: return "Personal chat";
		case Type::Bot: return "Bot chat";
		case Type::PrivateGroup: return "Private group";
		case Type::PrivateSupergroup: return "Private supergroup";
		case Type::PublicSupergroup: return "Public supergroup";
		case Type::PrivateChannel: return "Private channel";
		case Type::PublicChannel: return "Public channel";
		}
		Unexpected("Dialog type in TypeString.");
	};
	const auto NameString = [](
			const Data::DialogInfo &dialog,
			Type type) -> QByteArray {
		if (dialog.type == Type::Self) {
			return "Saved messages";
		}
		const auto name = dialog.name;
		if (!name.isEmpty()) {
			return name;
		}
		switch (type) {
		case Type::Unknown: return "(unknown)";
		case Type::Personal: return "(deleted user)";
		case Type::Bot: return "(deleted bot)";
		case Type::PrivateGroup:
		case Type::PrivateSupergroup:
		case Type::PublicSupergroup: return "(deleted group)";
		case Type::PrivateChannel:
		case Type::PublicChannel: return "(deleted channel)";
		}
		Unexpected("Dialog type in TypeString.");
	};
	return _chats->writeBlock(kLineBreak + SerializeKeyValue({
		{ "Name", SerializeString(NameString(_dialog, _dialog.type)) },
		{ "Type", SerializeString(TypeString(_dialog.type)) },
		{
			(_dialog.onlyMyMessages
				? "Outgoing messages count"
				: "Messages count"),
			SerializeString(Data::NumberToString(_messagesCount))
		},
		{
			"Content",
			(_messagesCount > 0
				? SerializeLink(
					(_dialog.relativePath + "messages.html").toUtf8(),
					_chats->relativePath(
						(_dialog.relativePath + "messages.html")))
				: QByteArray())
		}
	}));
}

Result HtmlWriter::writeChatsEnd() {
	if (_chats) {
		return base::take(_chats)->close();
	}
	return Result::Success();
}

Result HtmlWriter::switchToNextChatFile(int index) {
	Expects(_chat != nullptr);

	const auto nextPath = messagesFile(index);
	const auto link = kLineBreak + "<a href=\""
		+ nextPath.toUtf8()
		+ "\">Next messages part</a>";
	if (const auto result = _chat->writeBlock(link); !result) {
		return result;
	}
	_chat = fileWithRelativePath(_dialog.relativePath + nextPath);
	return Result::Success();
}

Result HtmlWriter::finish() {
	Expects(_summary != nullptr);

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
	return pathWithRelativePath(mainFileRelativePath());
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
