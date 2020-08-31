/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_text.h"

#include "export/output/export_output_result.h"
#include "export/data/export_data_types.h"
#include "core/utils.h"

#include <QtCore/QFile>

namespace Export {
namespace Output {
namespace {

#ifdef Q_OS_WIN
const auto kLineBreak = QByteArrayLiteral("\r\n");
#else // Q_OS_WIN
const auto kLineBreak = QByteArrayLiteral("\n");
#endif // Q_OS_WIN

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

Data::Utf8String FormatUsername(const Data::Utf8String &username) {
	return username.isEmpty() ? username : ('@' + username);
}

QByteArray FormatFilePath(const Data::File &file) {
	return file.relativePath.toUtf8();
}

QByteArray SerializeMessage(
		const Data::Message &message,
		const std::map<Data::PeerId, Data::Peer> &peers,
		const QString &internalLinksDomain) {
	using namespace Data;

	if (v::is<UnsupportedMedia>(message.media.content)) {
		return "Error! This message is not supported "
			"by this version of Telegram Desktop. "
			"Please update the application.";
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
		{ "ID", NumberToString(message.id) },
		{ "Date", FormatDateTime(message.date) },
		{ "Edited", FormatDateTime(message.edited) },
	};
	const auto push = [&](const QByteArray &key, const QByteArray &value) {
		if (!value.isEmpty()) {
			values.emplace_back(key, value);
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
			list.push_back(wrapUserName(userId));
		}
		if (list.size() == 1) {
			push(labelOne, list[0]);
		} else if (!list.empty()) {
			push(labelMany, JoinList(", ", list));
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
	const auto pushPath = [&](
			const Data::File &file,
			const QByteArray &label,
			const QByteArray &name = QByteArray()) {
		Expects(!file.relativePath.isEmpty()
			|| file.skipReason != SkipReason::None);

		push(label, [&]() -> QByteArray {
			const auto pre = name.isEmpty() ? QByteArray() : name + ' ';
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
			case SkipReason::None: return FormatFilePath(file);
			}
			Unexpected("Skip reason while writing file path.");
		}());
	};
	const auto pushPhoto = [&](const Image &image) {
		pushPath(image.file, "Photo");
		if (image.width && image.height) {
			push("Width", NumberToString(image.width));
			push("Height", NumberToString(image.height));
		}
	};

	v::match(message.action.content, [&](const ActionChatCreate &data) {
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
	}, [&](const ActionContactSignUp &data) {
		pushActor();
		pushAction("Join Telegram");
	}, [&](const ActionPhoneNumberRequest &data) {
		pushActor();
		pushAction("Request Phone Number");
	}, [](v::null_t) {});

	if (v::is_null(message.action.content)) {
		pushFrom();
		push("Author", message.signature);
		if (message.forwardedFromId) {
			push("Forwarded from", wrapPeerName(message.forwardedFromId));
		} else if (!message.forwardedFromName.isEmpty()) {
			push("Forwarded from", message.forwardedFromName);
		}
		if (message.savedFromChatId) {
			push("Saved from", wrapPeerName(message.savedFromChatId));
		}
		pushReplyToMsgId();
		if (message.viaBotId) {
			push("Via", user(message.viaBotId).username);
		}
	}

	v::match(message.media.content, [&](const Photo &photo) {
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
		push("Contact information", SerializeKeyValue({
			{ "First name", data.info.firstName },
			{ "Last name", data.info.lastName },
			{ "Phone number", FormatPhoneNumber(data.info.phoneNumber) },
		}));
		if (!data.vcard.content.isEmpty()) {
			pushPath(data.vcard, "Contact vcard");
		}
	}, [&](const GeoPoint &data) {
		push("Location", data.valid ? SerializeKeyValue({
			{ "Latitude", NumberToString(data.latitude) },
			{ "Longitude", NumberToString(data.longitude) },
		}) : QByteArray("(empty value)"));
		pushTTL("Live location period");
	}, [&](const Venue &data) {
		push("Place name", data.title);
		push("Address", data.address);
		if (data.point.valid) {
			push("Location", SerializeKeyValue({
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
		push("Invoice", SerializeKeyValue({
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
	}, [&](const Poll &data) {
		push("Poll", SerializeKeyValue({
			{ "Question", data.question },
			{ "Closed", data.closed ? QByteArray("Yes") : QByteArray() },
			{ "Votes", NumberToString(data.totalVotes) },
		}));
		for (const auto &answer : data.answers) {
			push("Answer", SerializeKeyValue({
				{ "Text", answer.text },
				{ "Votes", NumberToString(answer.votes) },
				{ "Chosen", answer.my ? QByteArray("Yes") : QByteArray() }
			}));
		}
	}, [](const UnsupportedMedia &data) {
		Unexpected("Unsupported message.");
	}, [](v::null_t) {});

	auto value = JoinList(QByteArray(), ranges::view::all(
		message.text
	) | ranges::view::transform([](const Data::TextPart &part) {
		return part.text;
	}) | ranges::to_vector);
	push("Text", value);

	return SerializeKeyValue(std::move(values));
}

} // namespace

Result TextWriter::start(
		const Settings &settings,
		const Environment &environment,
		Stats *stats) {
	Expects(settings.path.endsWith('/'));

	_settings = base::duplicate(settings);
	_environment = environment;
	_stats = stats;
	_summary = fileWithRelativePath(mainFileRelativePath());
	return _summary->writeBlock(_environment.aboutTelegram
		+ kLineBreak
		+ kLineBreak);
}

Result TextWriter::writePersonal(const Data::PersonalInfo &data) {
	Expects(_summary != nullptr);

	const auto &info = data.user.info;
	const auto serialized = SerializeKeyValue({
		{ "First name", info.firstName },
		{ "Last name", info.lastName },
		{ "Phone number", Data::FormatPhoneNumber(info.phoneNumber) },
		{ "Username", FormatUsername(data.user.username) },
		{ "Bio", data.bio },
		})
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(serialized);
}

Result TextWriter::writeUserpicsStart(const Data::UserpicsInfo &data) {
	Expects(_summary != nullptr);
	Expects(_userpics == nullptr);

	_userpicsCount = data.count;
	if (!_userpicsCount) {
		return Result::Success();
	}
	const auto filename = "lists/profile_pictures.txt";
	_userpics = fileWithRelativePath(filename);

	const auto serialized = "Profile pictures"
		"(" + Data::NumberToString(_userpicsCount) + ") - " + filename
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(serialized);
}

Result TextWriter::writeUserpicsSlice(const Data::UserpicsSlice &data) {
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
				case SkipReason::None: return FormatFilePath(file);
				}
				Unexpected("Skip reason while writing photo path.");
			}();
			lines.push_back(SerializeKeyValue({
				{ "Added", Data::FormatDateTime(userpic.date) },
				{ "Photo", path },
			}));
		}
	}
	return _userpics->writeBlock(JoinList(kLineBreak, lines) + kLineBreak);
}

Result TextWriter::writeUserpicsEnd() {
	_userpics = nullptr;
	return Result::Success();
}

Result TextWriter::writeContactsList(const Data::ContactsList &data) {
	Expects(_summary != nullptr);

	if (const auto result = writeSavedContacts(data); !result) {
		return result;
	} else if (const auto result = writeFrequentContacts(data); !result) {
		return result;
	}
	return Result::Success();
}

Result TextWriter::writeSavedContacts(const Data::ContactsList &data) {
	if (data.list.empty()) {
		return Result::Success();
	}

	const auto filename = "lists/contacts.txt";
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
				{ "First name", contact.firstName },
				{ "Last name", contact.lastName },
				{
					"Phone number",
					Data::FormatPhoneNumber(contact.phoneNumber)
				},
				{ "Added", Data::FormatDateTime(contact.date) }
			}));
		}
	}
	const auto full = _environment.aboutContacts
		+ kLineBreak
		+ kLineBreak
		+ JoinList(kLineBreak, list);
	if (const auto result = file->writeBlock(full); !result) {
		return result;
	}

	const auto header = "Contacts "
		"(" + Data::NumberToString(data.list.size()) + ") - " + filename
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(header);
}

Result TextWriter::writeFrequentContacts(const Data::ContactsList &data) {
	const auto size = data.correspondents.size()
		+ data.inlineBots.size()
		+ data.phoneCalls.size();
	if (!size) {
		return Result::Success();
	}

	const auto filename = "lists/frequent.txt";
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
				{ "Category", category },
				{ "User",  top.peer.user() ? user : QByteArray() },
				{ "Chat", saved },
				{ chatType, chat },
				{ "Rating", Data::NumberToString(top.rating) }
			}));
		}
	};
	writeList(data.correspondents, "People");
	writeList(data.inlineBots, "Inline bots");
	writeList(data.phoneCalls, "Calls");
	const auto full = _environment.aboutFrequent
		+ kLineBreak
		+ kLineBreak
		+ JoinList(kLineBreak, list);
	if (const auto result = file->writeBlock(full); !result) {
		return result;
	}

	const auto header = "Frequent contacts "
		"(" + Data::NumberToString(size) + ") - lists/frequent.txt"
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(header);
}

Result TextWriter::writeSessionsList(const Data::SessionsList &data) {
	Expects(_summary != nullptr);

	if (const auto result = writeSessions(data); !result) {
		return result;
	} else if (const auto result = writeWebSessions(data); !result) {
		return result;
	}
	return Result::Success();
}

Result TextWriter::writeSessions(const Data::SessionsList &data) {
	Expects(_summary != nullptr);

	if (data.list.empty()) {
		return Result::Success();
	}

	const auto filename = "lists/sessions.txt";
	const auto file = fileWithRelativePath(filename);
	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	for (const auto &session : data.list) {
		list.push_back(SerializeKeyValue({
			{ "Last active", Data::FormatDateTime(session.lastActive) },
			{ "Last IP address", session.ip },
			{ "Last country", session.country },
			{ "Last region", session.region },
			{
				"Application name",
				(session.applicationName.isEmpty()
					? Data::Utf8String("(unknown)")
					: session.applicationName)
			},
			{ "Application version", session.applicationVersion },
			{ "Device model", session.deviceModel },
			{ "Platform", session.platform },
			{ "System version", session.systemVersion },
			{ "Created", Data::FormatDateTime(session.created) },
		}));
	}
	const auto full = _environment.aboutSessions
		+ kLineBreak
		+ kLineBreak
		+ JoinList(kLineBreak, list);
	if (const auto result = file->writeBlock(full); !result) {
		return result;
	}

	const auto header = "Sessions "
		"(" + Data::NumberToString(data.list.size()) + ") - " + filename
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(header);
}

Result TextWriter::writeWebSessions(const Data::SessionsList &data) {
	Expects(_summary != nullptr);

	if (data.webList.empty()) {
		return Result::Success();
	}

	const auto filename = "lists/web_sessions.txt";
	const auto file = fileWithRelativePath(filename);
	auto list = std::vector<QByteArray>();
	list.reserve(data.webList.size());
	for (const auto &session : data.webList) {
		list.push_back(SerializeKeyValue({
			{ "Last active", Data::FormatDateTime(session.lastActive) },
			{ "Last IP address", session.ip },
			{ "Last region", session.region },
			{
				"Bot username",
				(session.botUsername.isEmpty()
					? Data::Utf8String("(unknown)")
					: session.botUsername)
			},
			{
				"Domain name",
				(session.domain.isEmpty()
					? Data::Utf8String("(unknown)")
					: session.domain)
			},
			{ "Browser", session.browser },
			{ "Platform", session.platform },
			{ "Created", Data::FormatDateTime(session.created) },
		}));
	}
	const auto full = _environment.aboutWebSessions
		+ kLineBreak
		+ kLineBreak
		+ JoinList(kLineBreak, list);
	if (const auto result = file->writeBlock(full); !result) {
		return result;
	}

	const auto header = "Web sessions "
		"(" + Data::NumberToString(data.webList.size()) + ") - " + filename
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(header);
}

Result TextWriter::writeOtherData(const Data::File &data) {
	Expects(_summary != nullptr);

	const auto header = "Other data - " + data.relativePath.toUtf8()
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(header);
}

Result TextWriter::writeDialogsStart(const Data::DialogsInfo &data) {
	_dialogsCount = data.chats.size();
	_leftChannelsCount = data.left.size();
	return Result::Success();
}

Result TextWriter::writeDialogStart(const Data::DialogInfo &data) {
	Expects(_chat == nullptr);

	const auto result = validateDialogsMode(data.isLeftChannel);
	if (!result) {
		return result;
	}

	_chat = fileWithRelativePath(data.relativePath + "messages.txt");
	_messagesCount = 0;
	_dialog = data;
	return Result::Success();
}

Result TextWriter::validateDialogsMode(bool isLeftChannel) {
	const auto mode = isLeftChannel
		? DialogsMode::Left
		: DialogsMode::Chats;
	if (_dialogsMode == mode) {
		return Result::Success();
	} else if (_dialogsMode != DialogsMode::None) {
		if (const auto result = writeChatsEnd(); !result) {
			return result;
		}
	}
	_dialogsMode = mode;
	return writeChatsStart(
		isLeftChannel ? _leftChannelsCount : _dialogsCount,
		isLeftChannel ? "Left chats" : "Chats",
		(isLeftChannel
			? _environment.aboutLeftChats
			: _environment.aboutChats),
		isLeftChannel ? "lists/left_chats.txt" : "lists/chats.txt");
}

Result TextWriter::writeDialogSlice(const Data::MessagesSlice &data) {
	Expects(_chat != nullptr);
	Expects(!data.list.empty());

	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	for (const auto &message : data.list) {
		if (Data::SkipMessageByDate(message, _settings)) {
			continue;
		}
		list.push_back(SerializeMessage(
			message,
			data.peers,
			_environment.internalLinksDomain));
		++_messagesCount;
	}
	if (list.empty()) {
		return Result::Success();
	}
	const auto full = _chat->empty()
		? JoinList(kLineBreak, list)
		: kLineBreak + JoinList(kLineBreak, list);
	return _chat->writeBlock(full);
}

Result TextWriter::writeDialogEnd() {
	Expects(_chats != nullptr);
	Expects(_chat != nullptr);

	_chat = nullptr;

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
		{ "Name", NameString(_dialog, _dialog.type) },
		{ "Type", TypeString(_dialog.type) },
		{
			(_dialog.onlyMyMessages
				? "Outgoing messages count"
				: "Messages count"),
			Data::NumberToString(_messagesCount)
		},
		{
			"Content",
			(_messagesCount > 0
				? (_dialog.relativePath + "messages.txt").toUtf8()
				: QByteArray())
		}
	}));
}

Result TextWriter::writeDialogsEnd() {
	return writeChatsEnd();
}

Result TextWriter::writeChatsStart(
		int count,
		const QByteArray &listName,
		const QByteArray &about,
		const QString &fileName) {
	Expects(_summary != nullptr);
	Expects(_chats == nullptr);

	if (!count) {
		return Result::Success();
	}

	_chats = fileWithRelativePath(fileName);

	const auto block = about + kLineBreak;
	if (const auto result = _chats->writeBlock(block); !result) {
		return result;
	}

	const auto header = listName + " "
		"(" + Data::NumberToString(count) + ") - "
		+ fileName.toUtf8()
		+ kLineBreak
		+ kLineBreak;
	return _summary->writeBlock(header);
}

Result TextWriter::writeChatsEnd() {
	_chats = nullptr;
	return Result::Success();
}

Result TextWriter::finish() {
	return Result::Success();
}

QString TextWriter::mainFilePath() {
	return pathWithRelativePath(mainFileRelativePath());
}

QString TextWriter::mainFileRelativePath() const {
	return "export_results.txt";
}

QString TextWriter::pathWithRelativePath(const QString &path) const {
	return _settings.path + path;
}

std::unique_ptr<File> TextWriter::fileWithRelativePath(
		const QString &path) const {
	return std::make_unique<File>(pathWithRelativePath(path), _stats);
}

} // namespace Output
} // namespace Export
