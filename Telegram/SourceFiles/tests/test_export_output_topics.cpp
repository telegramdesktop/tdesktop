/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/data/export_data_types.h"
#include "export/export_settings.h"
#include "export/output/export_output_abstract.h"
#include "export/output/export_output_result.h"
#include "export/output/export_output_stats.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QResource>
#include <QtCore/QUrl>
#include <QtCore/QTemporaryDir>
#include <cstdio>

#include <cstdlib>
#include <utility>

namespace {
namespace Data = Export::Data;

using namespace Export;
using namespace Export::Output;

void Require(bool condition, const char *message) {
	if (!condition) {
		std::fprintf(stderr, "Forum export writer check failed: %s\n", message);
		std::abort();
	}
}

QByteArray Read(const QString &path) {
	auto file = QFile(path);
	Require(file.open(QIODevice::ReadOnly), "output file must exist");
	return file.readAll();
}

QString TopicPath(const QString &root, int id) {
	return root + "chats/chat_77/topic_" + QString::number(id) + '/';
}

Data::DialogInfo Topic(int id) {
	auto result = Data::DialogInfo();
	result.type = Data::DialogInfo::Type::PublicSupergroup;
	result.name = "Same <script>alert(1)</script>";
	result.input = MTP_inputPeerSelf();
	result.peerId = PeerId(PeerIdHelper(123));
	result.isForum = true;
	result.topicRootId = id;
	result.topicChatName = "Forum & <unsafe>";
	result.relativePath = "chats/chat_77/topic_"
		+ QString::number(id)
		+ '/';
	return result;
}

void CheckTopicSelection() {
	auto settings = Settings();
	Require(settings.includesTopic(123, 1),
		"an absent peer selection includes new topics by default");
	settings.topicSelection[123] = { 101, 303 };
	Require(settings.includesTopic(123, 101)
		&& !settings.includesTopic(123, 202)
		&& settings.includesTopic(123, 303),
		"an explicit selection includes only selected topic ids");
	settings.topicSelection[123].clear();
	Require(!settings.includesTopic(123, 1),
		"an explicit empty selection excludes known active topics");
	Require(settings.includesTopic(456, 1),
		"selection for one forum does not affect another forum");
}

void CheckForumTopicParser() {
	const auto topic = [](int id, const char *title, int topMessageId, int date) {
		return MTP_forumTopic(
			MTP_flags(MTPDforumTopic::Flags(0)),
			MTP_int(id),
			MTP_int(date),
			MTP_peerChannel(MTP_long(77)),
			MTP_string(title),
			MTP_int(0),
			MTPlong(),
			MTP_int(topMessageId),
			MTP_int(0),
			MTP_int(0),
			MTP_int(0),
			MTP_int(0),
			MTP_int(0),
			MTP_int(0),
			MTPPeer(),
			MTPPeerNotifySettings(),
			MTP_draftMessageEmpty(MTP_flags(0), MTP_int(0)));
	};
	const auto response = [](
			MTPVector<MTPForumTopic> topics,
			MTPDmessages_forumTopics::Flags flags) {
		return MTPmessages_ForumTopics(MTP_messages_forumTopics(
			MTP_flags(flags),
			MTP_int(1),
			topics,
			MTPVector<MTPMessage>(),
			MTPVector<MTPChat>(),
			MTPVector<MTPUser>(),
			MTP_int(0)));
	};

	const auto empty = Data::ParseForumTopicsSlice(response(
		MTPVector<MTPForumTopic>(),
		MTPDmessages_forumTopics::Flags(0)));
	Require(empty.list.empty(), "empty forum topic response stays empty");

	auto deletedTopics = MTPVector<MTPForumTopic>();
	deletedTopics.v.push_back(MTP_forumTopicDeleted(MTP_int(1)));
	const auto deleted = Data::ParseForumTopicsSlice(response(
		std::move(deletedTopics),
		MTPDmessages_forumTopics::Flags(0)));
	Require(deleted.list.empty(),
		"deleted General topic is not treated as an active root");

	auto activeTopics = MTPVector<MTPForumTopic>();
	activeTopics.v.push_back(topic(1, "General", 11, 1700000000));
	activeTopics.v.push_back(MTP_forumTopicDeleted(MTP_int(42)));
	const auto parsed = Data::ParseForumTopicsSlice(response(
		std::move(activeTopics),
		MTPDmessages_forumTopics::Flag::f_order_by_create_date));
	Require(parsed.list.size() == 1
		&& parsed.list[0].rootId == 1
		&& parsed.list[0].title == "General",
		"active General topic survives a deleted sibling");
	Require(parsed.offsetDate == 1700000000
		&& parsed.offsetId == 11
		&& parsed.offsetTopicId == 1,
		"forum topic cursor follows the active General topic");

	auto chat = Data::DialogInfo();
	chat.name = "Forum";
	chat.relativePath = "chats/chat_77/";
	chat.splits = { 10, 20 };
	chat.messagesCountPerSplit = { 4, 9 };
	chat.migratedFromInput = MTP_inputPeerChat(MTP_long(55));
	const auto general = Data::DialogInfoFromTopic(chat, parsed.list[0]);
	Require(general.splits == chat.splits
		&& general.messagesCountPerSplit == chat.messagesCountPerSplit
		&& general.migratedFromInput.type() == chat.migratedFromInput.type(),
		"General topic retains its pre-migration history");
	Require(general.relativePath == "chats/chat_77/topic_1/",
		"General topic uses the numeric root folder");

	auto migratedTopic = Data::ForumTopic();
	migratedTopic.rootId = 42;
	migratedTopic.title = "Later";
	const auto migrated = Data::DialogInfoFromTopic(chat, migratedTopic);
	Require(migrated.splits == std::vector<int>{ 20 }
		&& migrated.messagesCountPerSplit == std::vector<int>{ 0 }
		&& migrated.migratedFromInput.type() == mtpc_inputPeerEmpty,
		"non-General topic keeps only the current-channel phase");
}

void CheckTopicMessageFilter() {
	const auto message = [](int id, bool outgoing) {
		auto result = Data::Message();
		result.id = id;
		result.out = outgoing;
		return result;
	};

	auto initial = Data::MessagesSlice();
	initial.list.push_back(message(0, true));
	initial.list.push_back(message(2, false));
	initial = Data::FilterTopicMessagesSlice(std::move(initial), 0);
	Require(initial.list.size() == 1 && initial.list[0].id == 2,
		"initial topic cursor zero keeps only positive message ids");

	auto first = Data::MessagesSlice();
	first.list.push_back(message(1, true));
	first.list.push_back(message(3, false));
	first.list.push_back(message(5, true));
	first = Data::FilterTopicMessagesSlice(std::move(first), 1);
	Require(first.list.size() == 2
		&& first.list[0].id == 3
		&& !first.list[0].out
		&& first.list[1].id == 5
		&& first.list[1].out,
		"root boundary is excluded while incoming and outgoing replies remain");

	auto overlap = Data::MessagesSlice();
	overlap.list.push_back(message(3, false));
	overlap.list.push_back(message(5, true));
	overlap.list.push_back(message(7, false));
	overlap.list.push_back(message(9, true));
	overlap = Data::FilterTopicMessagesSlice(std::move(overlap), 5);
	Require(overlap.list.size() == 2
		&& overlap.list[0].id == 7
		&& !overlap.list[0].out
		&& overlap.list[1].id == 9
		&& overlap.list[1].out,
		"overlap is removed and all strictly newer ids survive across gaps");
	auto repeated = Data::MessagesSlice();
	repeated.list.push_back(message(3, false));
	repeated.list.push_back(message(5, true));
	Require(Data::FilterTopicMessagesSlice(
		std::move(repeated),
		5).list.empty(),
		"overlap-only page is empty after filtering");
}

void CheckTopicRootFilter() {
	const auto message = [](int id, int date, bool outgoing) {
		auto result = Data::Message();
		result.id = id;
		result.date = date;
		result.out = outgoing;
		return result;
	};
	const auto makeSlice = [&] {
		auto result = Data::MessagesSlice();
		result.list.push_back(message(1, 0, true));
		result.list.push_back(message(2, 1700000000, false));
		result.list.push_back(message(3, 1700000000, true));
		return result;
	};
	const auto all = Data::FilterTopicRootSlice(makeSlice(), false);
	const auto mine = Data::FilterTopicRootSlice(makeSlice(), true);
	Require(all.list.size() == 2
		&& all.list[0].id == 2
		&& all.list[1].id == 3
		&& mine.list.size() == 1
		&& mine.list[0].id == 3,
		"root filtering keeps dated replies and applies outgoing-only policy");
}

Data::MessagesSlice MigratedGeneralSlice() {
	auto old = Data::Message();
	old.id = 42;
	old.replyToMsgId = 41;
	auto crossPeerReply = Data::Message();
	crossPeerReply.id = 43;
	crossPeerReply.replyToMsgId = 40;
	crossPeerReply.replyToPeerId = PeerId(PeerIdHelper(77));
	auto result = Data::MessagesSlice();
	result.list.push_back(std::move(old));
	result.list.push_back(std::move(crossPeerReply));
	return Data::AdjustMigrateMessageIds(std::move(result));
}

void CheckMigrateMessageIds() {
	const auto migrated = MigratedGeneralSlice();
	Require(migrated.list.size() == 2
		&& migrated.list[0].id == -999999958
		&& migrated.list[0].replyToMsgId == -999999959
		&& migrated.list[1].id == -999999957
		&& migrated.list[1].replyToMsgId == 40,
		"migration shifts old message ids and only same-peer reply ids");
}
Data::MessagesSlice Slice(const Data::DialogInfo &dialog) {
	const auto id = dialog.topicRootId;
	const auto baseMessage = [&] {
		auto result = Data::Message();
		result.date = 1700000000;
		result.peerId = dialog.peerId;
		result.fromId = PeerId(PeerIdHelper(456));
		return result;
	};

	auto message = baseMessage();
	message.id = id + 1;
	message.text.push_back({
		Data::TextPart::Type::Text,
		"topic message " + QByteArray::number(id),
	});
	auto document = Data::Document();
	document.name = "media.txt";
	document.mime = "text/plain";
	document.file.relativePath = dialog.relativePath
		+ "files/media_"
		+ QString::number(id)
		+ ".txt";
	document.file.size = 42;
	message.media.content = std::move(document);

	const auto emojiPath = dialog.relativePath
		+ "stickers/emoji_"
		+ QString::number(id)
		+ ".tgs";
	auto emojiMessage = baseMessage();
	emojiMessage.id = id + 2;
	emojiMessage.text.push_back({
		Data::TextPart::Type::CustomEmoji,
		"🙂",
		emojiPath.toUtf8(),
	});
	auto reaction = Data::Reaction();
	reaction.type = Data::Reaction::Type::CustomEmoji;
	reaction.documentId = emojiPath.toUtf8();
	reaction.count = 1;
	emojiMessage.reactions.push_back(std::move(reaction));

	auto richMessage = baseMessage();
	richMessage.id = id + 3;
	auto rich = Data::RichMessage();
	auto paragraph = Data::RichBlock();
	paragraph.kind = Data::RichBlock::Kind::Paragraph;
	paragraph.text.type = Data::RichText::Type::Concat;
	auto richText = Data::RichText();
	richText.type = Data::RichText::Type::Plain;
	richText.text = "rich topic message " + QByteArray::number(id);
	auto richEmoji = Data::RichText();
	richEmoji.type = Data::RichText::Type::CustomEmoji;
	richEmoji.text = "🙂";
	richEmoji.customEmojiData = emojiPath.toUtf8();
	paragraph.text.children.push_back(std::move(richText));
	paragraph.text.children.push_back(std::move(richEmoji));
	rich.blocks.push_back(std::move(paragraph));
	auto fileBlock = Data::RichBlock();
	fileBlock.kind = Data::RichBlock::Kind::File;
	fileBlock.documentId = id + 1000;
	auto richDocument = Data::Document();
	richDocument.name = "rich.txt";
	richDocument.mime = "text/plain";
	richDocument.file.relativePath = dialog.relativePath
		+ "files/rich_"
		+ QString::number(id)
		+ ".txt";
	richDocument.file.size = 24;
	rich.documents.emplace(fileBlock.documentId, std::move(richDocument));
	rich.blocks.push_back(std::move(fileBlock));
	richMessage.richMessage = std::move(rich);

	auto result = Data::MessagesSlice();
	result.list.push_back(std::move(message));
	result.list.push_back(std::move(emojiMessage));
	result.list.push_back(std::move(richMessage));
	return result;
}

void WriteFixtureFile(const QString &path, const QByteArray &contents) {
	Require(QDir().mkpath(QFileInfo(path).absolutePath()),
		"fixture media folder is created");
	auto file = QFile(path);
	Require(file.open(QIODevice::WriteOnly), "fixture media file opens");
	Require(file.write(contents) == contents.size(),
		"fixture media file writes");
}

void WriteTopicFiles(const QString &root, const Data::DialogInfo &dialog) {
	const auto id = QString::number(dialog.topicRootId);
	WriteFixtureFile(
		root + dialog.relativePath + "files/media_" + id + ".txt",
		"media fixture");
	WriteFixtureFile(
		root + dialog.relativePath + "files/rich_" + id + ".txt",
		"rich media fixture");
	WriteFixtureFile(
		root + dialog.relativePath + "stickers/emoji_" + id + ".tgs",
		"sticker fixture");
}

void WriteTopics(Output::Format format, const QString &root, bool singlePeer) {
	auto settings = Settings();
	settings.path = root;
	settings.format = format;
	settings.splitTopics = singlePeer;
	if (singlePeer) {
		settings.singlePeer = MTP_inputPeerSelf();
	}
	auto environment = Environment();
	environment.aboutTelegram = "synthetic writer fixture";
	auto stats = Stats();
	auto writer = CreateWriter(format);
	Require(writer->start(settings, environment, &stats).isSuccess(), "writer starts");

	auto dialogs = Data::DialogsInfo();
	dialogs.chats = { Topic(101), Topic(202), Topic(303) };
	WriteTopicFiles(root, dialogs.chats[0]);
	WriteTopicFiles(root, dialogs.chats[1]);
	Require(writer->writeDialogsStart(dialogs).isSuccess(), "topic list starts");
	for (auto i = 0; i != int(dialogs.chats.size()); ++i) {
		const auto &dialog = dialogs.chats[i];
		Require(writer->writeDialogStart(dialog).isSuccess(), "topic starts");
		if (dialog.topicRootId != 303) {
			const auto slice = Slice(dialog);
			Require(writer->writeDialogSlice(slice).isSuccess(), "topic message writes");
		}
		Require(writer->writeDialogEnd().isSuccess(), "topic ends");
	}
	Require(writer->writeDialogsEnd().isSuccess(), "topic list ends");
	Require(writer->finish().isSuccess(), "writer finishes");
}

void CheckTopicMessages(const QJsonArray &messages, int id) {
	if (id == 303) {
		Require(messages.isEmpty(), "empty topic has no messages");
		return;
	}
	Require(messages.size() == 3,
		"topic includes its text, emoji, and rich-message records");
	const auto text = messages[0].toObject();
	Require(text.value("id").toInt() == id + 1,
		"topic message ids do not bleed across topics");
	Require(text.value("text").toString()
		== "topic message " + QString::number(id),
		"topic JSON preserves distinct message text");
	Require(text.value("file").toString()
		== "files/media_" + QString::number(id) + ".txt",
		"child JSON media paths are relative to the topic folder");

	const auto emoji = messages[1].toObject();
	const auto emojiPart = emoji.value("text").toArray()[0].toObject();
	const auto emojiPath = "stickers/emoji_" + QString::number(id) + ".tgs";
	Require(emojiPart.value("document_id").toString() == emojiPath,
		"plain custom emoji path is relative to the topic folder");
	Require(emoji.value("reactions").toArray()[0].toObject()
		.value("document_id").toString() == emojiPath,
		"custom emoji reaction path is relative to the topic folder");

	const auto rich = messages[2].toObject().value("rich_message").toObject();
	const auto blocks = rich.value("blocks").toArray();
	const auto richEmoji = blocks[0].toObject().value("text").toObject()
		.value("text").toArray()[1].toObject();
	Require(richEmoji.value("document_id").toString() == emojiPath,
		"rich custom emoji path is relative to the topic folder");
	const auto fileBlock = blocks[1].toObject();
	Require(fileBlock.value("file").toString()
		== "files/rich_" + QString::number(id) + ".txt",
		"rich media path is relative to the topic folder");
	Require(!fileBlock.contains("file_skip_reason"),
		"downloaded rich media has no unavailable skip reason");
}

void CheckJson(const QString &root) {
	const auto index = QJsonDocument::fromJson(Read(root + "result.json")).object();
	const auto topics = index.value("topics").toArray();
	Require(topics.size() == 3, "root JSON lists every selected topic");
	Require(topics[0].toObject().value("name").toString()
		== "Same <script>alert(1)</script>", "duplicate topic title is preserved");
	Require(topics[0].toObject().value("file").toString()
		== "chats/chat_77/topic_101/result.json",
		"root JSON links use nested numeric topic paths");
	for (const auto id : { 101, 202, 303 }) {
		const auto path = TopicPath(root, id) + "result.json";
		const auto dialog = QJsonDocument::fromJson(Read(path)).object();
		Require(dialog.value("topic_root_id").toInt() == id,
			"topic JSON carries its root id");
		Require(dialog.value("name").toString()
			== "Same <script>alert(1)</script>", "topic JSON preserves unsafe-looking title as data");
		CheckTopicMessages(dialog.value("messages").toArray(), id);
	}
}

void CheckGlobalJson(const QString &root) {
	const auto index = QJsonDocument::fromJson(Read(root + "result.json")).object();
	const auto topics = index.value("chats").toObject()
		.value("list").toArray();
	Require(topics.size() == 3, "global JSON lists forum topics");
	for (const auto id : { 101, 202, 303 }) {
		const auto entry = topics[id == 101 ? 0 : id == 202 ? 1 : 2]
			.toObject();
		Require(entry.value("topic_root_id").toInt() == id,
			"global JSON keeps topic metadata");
		Require(entry.value("topic_file").toString()
			== TopicPath(QString(), id) + "result.json",
			"global JSON links to each topic file");
		const auto messages = entry.value("messages").toArray();
		Require(messages.size() == (id == 303 ? 0 : 3),
			"global topic JSON embeds its full message set");
		if (!messages.isEmpty()) {
			Require(messages[0].toObject().value("file").toString()
				== TopicPath(QString(), id) + "files/media_"
					+ QString::number(id) + ".txt",
				"global JSON keeps root-relative media paths in root messages");
		}
		const auto topic = QJsonDocument::fromJson(Read(
			TopicPath(root, id) + "result.json")).object();
		CheckTopicMessages(topic.value("messages").toArray(), id);
	}
}

QString ResolvedReference(
		const QString &page,
		const QByteArray &html,
		const QString &fragment) {
	const auto position = html.indexOf(fragment.toUtf8());
	Require(position >= 0, "HTML reference exists");
	const auto start = html.lastIndexOf('"', position);
	const auto end = html.indexOf('"', position);
	Require(start >= 0 && end > start, "HTML reference is quoted");
	const auto reference = QString::fromUtf8(
		html.mid(start + 1, end - start - 1));
	return QUrl::fromLocalFile(QFileInfo(page).absoluteFilePath())
		.resolved(QUrl(reference))
		.toLocalFile();
}

void CheckHtml(const QString &root) {
	const auto index = Read(root + "lists/chats.html");
	Require(index.contains("chat_77/topic_101/messages.html")
		&& index.contains("chat_77/topic_202/messages.html"),
		"HTML index links duplicate-title topics to nested numeric folders");
	Require(index.contains("Forum &amp; &lt;unsafe&gt;"),
		"HTML escapes the parent forum title");
	const auto first = Read(TopicPath(root, 101) + "messages.html");
	const auto second = Read(TopicPath(root, 202) + "messages.html");
	const auto empty = Read(TopicPath(root, 303) + "messages.html");
	Require(first.contains("&lt;script&gt;") && !first.contains("<script>"),
		"HTML escapes unsafe topic titles");
	Require(first.contains("topic message 101")
		&& !first.contains("topic message 202")
		&& second.contains("topic message 202")
		&& !second.contains("topic message 101"),
		"HTML topic pages do not bleed each other's messages");
	const auto topicPage = TopicPath(root, 101) + "messages.html";
	const auto media = ResolvedReference(
		topicPage,
		first,
		"chat_77/topic_101/files/media_101.txt");
	Require(QFileInfo::exists(media),
		"nested topic media link resolves to an existing file");
	const auto back = ResolvedReference(
		topicPage,
		first,
		"../../lists/chats.html");
	Require(back == QFileInfo(root + "lists/chats.html").absoluteFilePath(),
		"topic back link resolves to the forum index");
	const auto indexTarget = ResolvedReference(
		root + "lists/chats.html",
		index,
		"chat_77/topic_101/messages.html");
	Require(indexTarget == topicPage,
		"forum index topic link resolves to its message page");
	Require(empty.contains("No exported messages"),
		"empty topic still has a linked, valid HTML page");
}

void WriteEmptyGlobalJson(const QString &root) {
	auto settings = Settings();
	settings.path = root;
	settings.format = Format::Json;
	auto environment = Environment();
	environment.aboutTelegram = "empty export";
	auto stats = Stats();
	auto writer = CreateWriter(Format::Json);
	Require(writer->start(settings, environment, &stats).isSuccess(),
		"empty global writer starts");
	Require(writer->writeDialogsStart(Data::DialogsInfo()).isSuccess(),
		"empty global dialogs start");
	Require(writer->writeDialogsEnd().isSuccess(),
		"empty global dialogs end without nesting underflow");
	Require(writer->finish().isSuccess(), "empty global writer finishes");
	const auto result = QJsonDocument::fromJson(Read(root + "result.json")).object();
	Require(result.value("about").toString() == "empty export",
		"empty global JSON remains a valid root object");
}

void WriteGeneralPhases(const QString &root) {
	auto chat = Data::DialogInfo();
	chat.type = Data::DialogInfo::Type::PublicSupergroup;
	chat.name = "Forum";
	chat.input = MTP_inputPeerSelf();
	chat.peerId = PeerId(PeerIdHelper(123));
	chat.isForum = true;
	chat.relativePath = "chats/chat_77/";
	chat.splits = { -55, 123 };
	chat.migratedFromInput = MTP_inputPeerChat(MTP_long(55));
	auto topic = Data::ForumTopic();
	topic.rootId = 1;
	topic.title = "General";
	const auto general = Data::DialogInfoFromTopic(chat, topic);

	auto settings = Settings();
	settings.path = root;
	settings.format = Format::Json;
	auto stats = Stats();
	auto writer = CreateWriter(Format::Json);
	Require(writer->start(settings, Environment(), &stats).isSuccess(),
		"General topic writer starts");
	auto dialogs = Data::DialogsInfo();
	dialogs.chats.push_back(general);
	Require(writer->writeDialogsStart(dialogs).isSuccess(),
		"General topic list starts");
	Require(writer->writeDialogStart(general).isSuccess(),
		"General topic starts");
	const auto old = MigratedGeneralSlice();
	Require(writer->writeDialogSlice(old).isSuccess(),
		"shifted pre-migration General messages write first");
	auto current = Data::MessagesSlice();
	auto message = Data::Message();
	message.id = 17;
	message.replyToMsgId = old.list[0].id;
	message.text.push_back({ Data::TextPart::Type::Text, "current General" });
	current.list.push_back(std::move(message));
	Require(writer->writeDialogSlice(current).isSuccess(),
		"current-channel General messages write after migrated history");
	Require(writer->writeDialogEnd().isSuccess()
		&& writer->writeDialogsEnd().isSuccess()
		&& writer->finish().isSuccess(),
		"General topic export closes successfully");
	const auto path = root + general.relativePath + "result.json";
	const auto messages = QJsonDocument::fromJson(Read(path))
		.object().value("messages").toArray();
	Require(messages.size() == 3
		&& messages[0].toObject().value("id").toInt() == -999999958
		&& messages[0].toObject().value("reply_to_message_id").toInt()
			== -999999959
		&& messages[2].toObject().value("id").toInt() == 17
		&& messages[2].toObject().value("reply_to_message_id").toInt()
			== -999999958,
		"General output keeps migrated history before current-channel messages");
}

void WriteOrdinaryChat(const QString &root) {
	auto settings = Settings();
	settings.path = root;
	settings.format = Format::Json;
	auto environment = Environment();
	auto stats = Stats();
	auto writer = CreateWriter(Format::Json);
	Require(writer->start(settings, environment, &stats).isSuccess(), "ordinary writer starts");
	auto dialog = Topic(404);
	dialog.isForum = false;
	dialog.topicRootId = 0;
	dialog.topicChatName.clear();
	dialog.relativePath = "chats/chat_404/";
	auto dialogs = Data::DialogsInfo();
	dialogs.chats.push_back(dialog);
	Require(writer->writeDialogsStart(dialogs).isSuccess(), "ordinary chats start");
	Require(writer->writeDialogStart(dialog).isSuccess(), "ordinary chat starts");
	const auto slice = Slice(dialog);
	Require(writer->writeDialogSlice(slice).isSuccess(), "ordinary message writes");
	Require(writer->writeDialogEnd().isSuccess(), "ordinary chat ends");
	Require(writer->writeDialogsEnd().isSuccess(), "ordinary chats end");
	Require(writer->finish().isSuccess(), "ordinary writer finishes");
	const auto rootJson = QJsonDocument::fromJson(Read(root + "result.json")).object();
	const auto chats = rootJson.value("chats").toObject().value("list").toArray();
	Require(chats.size() == 1 && chats[0].toObject().contains("messages"),
		"ordinary chat retains its existing embedded-message JSON shape");
	Require(!chats[0].toObject().contains("topic_file"),
		"ordinary chat does not gain forum metadata");
	Require(!QFile::exists(root + "chats/chat_404/result.json"),
		"ordinary chat does not gain a topic output file");
}

} // namespace

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	Q_INIT_RESOURCE(export);
	CheckTopicRootFilter();
	CheckMigrateMessageIds();
	CheckTopicMessageFilter();
	CheckTopicSelection();
	auto temporary = QTemporaryDir();
	CheckForumTopicParser();
	Require(temporary.isValid(), "temporary output root exists");
	auto root = temporary.path();
	if (argc > 1) {
		const auto persistent = QString::fromLocal8Bit(argv[1]);
		Require(!QFileInfo::exists(persistent),
			"persistent output path must not already exist");
		Require(QDir().mkpath(persistent), "persistent output root is created");
		root = persistent;
	}
	for (const auto format : { Format::Html, Format::Json, Format::HtmlAndJson }) {
		const auto base = root + '/' + QString::number(int(format)) + '/';
		const auto single = base + "single/";
		const auto global = base + "global/";
		WriteTopics(format, single, true);
		WriteTopics(format, global, false);
		if (format != Format::Json) {
			CheckHtml(single);
			CheckHtml(global);
		}
		if (format != Format::Html) {
			CheckJson(single);
			CheckGlobalJson(global);
		}
	}
	WriteEmptyGlobalJson(root + "/empty-global/");
	WriteOrdinaryChat(root + "/ordinary/");
	WriteGeneralPhases(root + "/general/");
	return EXIT_SUCCESS;
}
