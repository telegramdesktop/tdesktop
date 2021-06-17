/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_abstract.h"

#include "export/output/export_output_html.h"
#include "export/output/export_output_json.h"
#include "export/output/export_output_stats.h"
#include "export/output/export_output_result.h"

#include <QtCore/QDir>
#include <QtCore/QDate>

namespace Export {
namespace Output {

QString NormalizePath(const Settings &settings) {
	QDir folder(settings.path);
	const auto path = folder.absolutePath();
	auto result = path.endsWith('/') ? path : (path + '/');
	if (!folder.exists() && !settings.forceSubPath) {
		return result;
	}
	const auto mode = QDir::AllEntries | QDir::NoDotAndDotDot;
	const auto list = folder.entryInfoList(mode);
	if (list.isEmpty() && !settings.forceSubPath) {
		return result;
	}
	const auto date = QDate::currentDate();
	const auto base = QString(settings.onlySinglePeer()
		? "ChatExport_%1"
		: "DataExport_%1"
	).arg(date.toString(Qt::ISODate));
	const auto add = [&](int i) {
		return base + (i ? " (" + QString::number(i) + ')' : QString());
	};
	auto index = 0;
	while (QDir(result + add(index)).exists()) {
		++index;
	}
	result += add(index) + '/';
	return result;
}

std::unique_ptr<AbstractWriter> CreateWriter(Format format) {
	switch (format) {
	case Format::Html: return std::make_unique<HtmlWriter>();
	case Format::Json: return std::make_unique<JsonWriter>();
	}
	Unexpected("Format in Export::Output::CreateWriter.");
}

Stats AbstractWriter::produceTestExample(
		const QString &path,
		const Environment &environment) {
	auto result = Stats();
	const auto folder = QDir(path).absolutePath();
	auto settings = Settings();
	settings.format = format();
	settings.path = (folder.endsWith('/') ? folder : (folder + '/'))
		+ "ExportExample/";
	settings.types = Settings::Type::AllMask;
	settings.fullChats = Settings::Type::AllMask
		& ~(Settings::Type::PublicChannels | Settings::Type::PublicGroups);
	settings.media.types = MediaSettings::Type::AllMask;
	settings.media.sizeLimit = 1024 * 1024;

	const auto check = [](Result result) {
		Assert(result.isSuccess());
	};

	check(start(settings, environment, &result));

	const auto counter = [&] {
		static auto GlobalCounter = 0;
		return ++GlobalCounter;
	};
	const auto date = [&] {
		return time(nullptr) - 86400 + counter();
	};
	const auto prevdate = [&] {
		return date() - 86400;
	};

	auto personal = Data::PersonalInfo();
	personal.bio = "Nice text about me.";
	personal.user.info.firstName = "John";
	personal.user.info.lastName = "Preston";
	personal.user.info.phoneNumber = "447400000000";
	personal.user.info.date = date();
	personal.user.username = "preston";
	personal.user.info.userId = counter();
	personal.user.isBot = false;
	personal.user.isSelf = true;
	check(writePersonal(personal));

	const auto generatePhoto = [&] {
		static auto index = 0;
		auto result = Data::Photo();
		result.date = date();
		result.id = counter();
		result.image.width = 512;
		result.image.height = 512;
		result.image.file.relativePath = "files/photo_"
			+ QString::number(++index)
			+ ".jpg";
		return result;
	};

	auto userpics = Data::UserpicsInfo();
	userpics.count = 3;
	auto userpicsSlice1 = Data::UserpicsSlice();
	userpicsSlice1.list.push_back(generatePhoto());
	userpicsSlice1.list.push_back(generatePhoto());
	auto userpicsSlice2 = Data::UserpicsSlice();
	userpicsSlice2.list.push_back(generatePhoto());
	check(writeUserpicsStart(userpics));
	check(writeUserpicsSlice(userpicsSlice1));
	check(writeUserpicsSlice(userpicsSlice2));
	check(writeUserpicsEnd());

	auto contacts = Data::ContactsList();
	auto topUser = Data::TopPeer();
	auto user = personal.user;
	auto peerUser = Data::Peer{ user };
	topUser.peer = peerUser;
	topUser.rating = 0.5;
	auto topChat = Data::TopPeer();
	auto chat = Data::Chat();
	chat.bareId = counter();
	chat.title = "Group chat";
	auto peerChat = Data::Peer{ chat };
	topChat.peer = peerChat;
	topChat.rating = 0.25;
	auto topBot = Data::TopPeer();
	auto bot = Data::User();
	bot.info.date = date();
	bot.isBot = true;
	bot.info.firstName = "Bot";
	bot.info.lastName = "Father";
	bot.info.userId = counter();
	bot.username = "botfather";
	auto peerBot = Data::Peer{ bot };
	topBot.peer = peerBot;
	topBot.rating = 0.125;

	auto peers = std::map<PeerId, Data::Peer>();
	peers.emplace(peerUser.id(), peerUser);
	peers.emplace(peerBot.id(), peerBot);
	peers.emplace(peerChat.id(), peerChat);

	contacts.correspondents.push_back(topUser);
	contacts.correspondents.push_back(topChat);
	contacts.inlineBots.push_back(topBot);
	contacts.inlineBots.push_back(topBot);
	contacts.phoneCalls.push_back(topUser);
	contacts.list.push_back(user.info);
	contacts.list.push_back(bot.info);

	check(writeContactsList(contacts));

	auto sessions = Data::SessionsList();
	auto session = Data::Session();
	session.applicationName = "Telegram Desktop";
	session.applicationVersion = "1.3.8";
	session.country = "GB";
	session.created = date();
	session.deviceModel = "PC";
	session.ip = "127.0.0.1";
	session.lastActive = date();
	session.platform = "Windows";
	session.region = "London";
	session.systemVersion = "10";
	sessions.list.push_back(session);
	sessions.list.push_back(session);
	auto webSession = Data::WebSession();
	webSession.botUsername = "botfather";
	webSession.browser = "Google Chrome";
	webSession.created = date();
	webSession.domain = "telegram.org";
	webSession.ip = "127.0.0.1";
	webSession.lastActive = date();
	webSession.platform = "Windows";
	webSession.region = "London, GB";
	sessions.webList.push_back(webSession);
	sessions.webList.push_back(webSession);
	check(writeSessionsList(sessions));

	auto sampleMessage = [&] {
		auto message = Data::Message();
		message.id = counter();
		message.date = prevdate();
		message.edited = date();
		static auto count = 0;
		if (++count % 3 == 0) {
			message.forwardedFromId = peerFromUser(user.info.userId);
			message.forwardedDate = date();
		} else if (count % 3 == 2) {
			message.forwardedFromName = "Test hidden forward";
			message.forwardedDate = date();
		}
		message.fromId = user.info.userId;
		message.replyToMsgId = counter();
		message.viaBotId = bot.info.userId;
		message.text.push_back(Data::TextPart{
			Data::TextPart::Type::Text,
			("Text message " + QString::number(counter())).toUtf8()
		});
		return message;
	};
	auto sliceBot1 = Data::MessagesSlice();
	sliceBot1.peers = peers;
	sliceBot1.list.push_back(sampleMessage());
	sliceBot1.list.push_back([&] {
		auto message = sampleMessage();
		message.media.content = generatePhoto();
		message.media.ttl = counter();
		return message;
	}());
	sliceBot1.list.push_back([&] {
		auto message = sampleMessage();
		auto document = Data::Document();
		document.date = prevdate();
		document.duration = counter();
		auto photo = generatePhoto();
		document.file = photo.image.file;
		document.width = photo.image.width;
		document.height = photo.image.height;
		document.id = counter();
		message.media.content = document;
		return message;
	}());
	sliceBot1.list.push_back([&] {
		auto message = sampleMessage();
		auto contact = Data::SharedContact();
		contact.info = user.info;
		message.media.content = contact;
		return message;
	}());
	auto sliceBot2 = Data::MessagesSlice();
	sliceBot2.peers = peers;
	sliceBot2.list.push_back([&] {
		auto message = sampleMessage();
		auto point = Data::GeoPoint();
		point.latitude = 1.5;
		point.longitude = 2.8;
		point.valid = true;
		message.media.content = point;
		message.media.ttl = counter();
		return message;
	}());
	sliceBot2.list.push_back([&] {
		auto message = sampleMessage();
		message.replyToMsgId = sliceBot1.list.back().id;
		auto venue = Data::Venue();
		venue.point.latitude = 1.5;
		venue.point.longitude = 2.8;
		venue.point.valid = true;
		venue.address = "Test address";
		venue.title = "Test venue";
		message.media.content = venue;
		return message;
	}());
	sliceBot2.list.push_back([&] {
		auto message = sampleMessage();
		auto game = Data::Game();
		game.botId = bot.info.userId;
		game.title = "Test game";
		game.description = "Test game description";
		game.id = counter();
		game.shortName = "testgame";
		message.media.content = game;
		return message;
	}());
	sliceBot2.list.push_back([&] {
		auto message = sampleMessage();
		auto invoice = Data::Invoice();
		invoice.amount = counter();
		invoice.currency = "GBP";
		invoice.title = "Huge invoice.";
		invoice.description = "So money.";
		invoice.receiptMsgId = sliceBot2.list.front().id;
		message.media.content = invoice;
		return message;
	}());
	auto serviceMessage = [&] {
		auto message = Data::Message();
		message.id = counter();
		message.date = prevdate();
		message.fromId = user.info.userId;
		return message;
	};
	auto sliceChat1 = Data::MessagesSlice();
	sliceChat1.peers = peers;
	sliceChat1.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionChatCreate();
		action.title = "Test chat";
		action.userIds.push_back(user.info.userId);
		action.userIds.push_back(bot.info.userId);
		message.action.content = action;
		return message;
	}());
	sliceChat1.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionChatEditTitle();
		action.title = "New title";
		message.action.content = action;
		return message;
	}());
	sliceChat1.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionChatEditPhoto();
		action.photo = generatePhoto();
		message.action.content = action;
		return message;
	}());
	sliceChat1.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionChatDeletePhoto();
		message.action.content = action;
		return message;
	}());
	sliceChat1.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionChatAddUser();
		action.userIds.push_back(user.info.userId);
		action.userIds.push_back(bot.info.userId);
		message.action.content = action;
		return message;
	}());
	sliceChat1.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionChatDeleteUser();
		action.userId = bot.info.userId;
		message.action.content = action;
		return message;
	}());
	sliceChat1.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionChatJoinedByLink();
		action.inviterId = bot.info.userId;
		message.action.content = action;
		return message;
	}());
	sliceChat1.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionChannelCreate();
		action.title = "Channel name";
		message.action.content = action;
		return message;
	}());
	sliceChat1.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionChatMigrateTo();
		action.channelId = ChannelId(chat.bareId);
		message.action.content = action;
		return message;
	}());
	sliceChat1.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionChannelMigrateFrom();
		action.chatId = ChatId(chat.bareId);
		action.title = "Supergroup now";
		message.action.content = action;
		return message;
	}());
	auto sliceChat2 = Data::MessagesSlice();
	sliceChat2.peers = peers;
	sliceChat2.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionPinMessage();
		message.replyToMsgId = sliceChat1.list.back().id;
		message.action.content = action;
		return message;
	}());
	sliceChat2.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionHistoryClear();
		message.action.content = action;
		return message;
	}());
	sliceChat2.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionGameScore();
		action.score = counter();
		action.gameId = counter();
		message.replyToMsgId = sliceChat2.list.back().id;
		message.action.content = action;
		return message;
	}());
	sliceChat2.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionPaymentSent();
		action.amount = counter();
		action.currency = "GBP";
		message.replyToMsgId = sliceChat2.list.front().id;
		message.action.content = action;
		return message;
	}());
	sliceChat2.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionPhoneCall();
		action.duration = counter();
		action.discardReason = Data::ActionPhoneCall::DiscardReason::Busy;
		message.action.content = action;
		return message;
	}());
	sliceChat2.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionScreenshotTaken();
		message.action.content = action;
		return message;
	}());
	sliceChat2.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionCustomAction();
		action.message = "Custom chat action.";
		message.action.content = action;
		return message;
	}());
	sliceChat2.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionBotAllowed();
		action.domain = "telegram.org";
		message.action.content = action;
		return message;
	}());
	sliceChat2.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionSecureValuesSent();
		using Type = Data::ActionSecureValuesSent::Type;
		action.types.push_back(Type::BankStatement);
		action.types.push_back(Type::Phone);
		message.action.content = action;
		return message;
	}());
	sliceChat2.list.push_back([&] {
		auto message = serviceMessage();
		auto action = Data::ActionContactSignUp();
		message.action.content = action;
		return message;
	}());
	auto dialogs = Data::DialogsInfo();
	auto dialogBot = Data::DialogInfo();
	dialogBot.messagesCountPerSplit.push_back(sliceBot1.list.size());
	dialogBot.messagesCountPerSplit.push_back(sliceBot2.list.size());
	dialogBot.type = Data::DialogInfo::Type::Bot;
	dialogBot.name = peerBot.name();
	dialogBot.onlyMyMessages = false;
	dialogBot.peerId = peerBot.id();
	dialogBot.relativePath = "chats/chat_"
		+ QString::number(counter())
		+ '/';
	dialogBot.splits.push_back(0);
	dialogBot.splits.push_back(1);
	dialogBot.topMessageDate = sliceBot2.list.back().date;
	dialogBot.topMessageId = sliceBot2.list.back().id;
	auto dialogChat = Data::DialogInfo();
	dialogChat.messagesCountPerSplit.push_back(sliceChat1.list.size());
	dialogChat.messagesCountPerSplit.push_back(sliceChat2.list.size());
	dialogChat.type = Data::DialogInfo::Type::PrivateGroup;
	dialogChat.name = peerChat.name();
	dialogChat.onlyMyMessages = true;
	dialogChat.peerId = peerChat.id();
	dialogChat.relativePath = "chats/chat_"
		+ QString::number(counter())
		+ '/';
	dialogChat.splits.push_back(0);
	dialogChat.splits.push_back(1);
	dialogChat.topMessageDate = sliceChat2.list.back().date;
	dialogChat.topMessageId = sliceChat2.list.back().id;
	dialogs.chats.push_back(dialogBot);
	dialogs.chats.push_back(dialogChat);

	check(writeDialogsStart(dialogs));
	check(writeDialogStart(dialogBot));
	check(writeDialogSlice(sliceBot1));
	check(writeDialogSlice(sliceBot2));
	check(writeDialogEnd());
	check(writeDialogStart(dialogChat));
	check(writeDialogSlice(sliceChat1));
	check(writeDialogSlice(sliceChat2));
	check(writeDialogEnd());
	check(writeDialogsEnd());

	check(finish());

	return result;
}

} // namespace Output
} // namespace Export
