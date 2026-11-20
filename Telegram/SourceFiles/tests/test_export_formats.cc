/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tests/test_export_formats.hpp"

#include "data/data_peer_id.h"
#include "export/output/export_output_abstract.h"
#include "export/output/export_output_result.h"
#include "export/export_settings.h"
#include "lang/lang_values.h"
#include "platform/platform_specific.h"

#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QLocale>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimeZone>

#include <cstdio>

// WHY: linked export code references these app entries; no case here reaches them.
namespace Lang::details {

QString Current(ushort key) {
	Unexpected("Lang::details::Current in test_export_formats.");
}

bool IsNonDefaultPlural(ushort keyBase) {
	Unexpected("Lang::details::IsNonDefaultPlural in test_export_formats.");
}

} // namespace Lang::details

#ifndef Q_OS_MACOS
namespace Platform::ThirdParty {

void start() {
	Unexpected("Platform::ThirdParty::start in test_export_formats.");
}

} // namespace Platform::ThirdParty
#endif // !Q_OS_MACOS

namespace crl {

rpl::producer<> on_main_update_requests() {
	Unexpected("crl::on_main_update_requests in test_export_formats.");
}

} // namespace crl

namespace Export::Tests {

using namespace Export::Data;

namespace {

constexpr auto kMessagesPerPage = 1000;
constexpr auto kFillerPerSlice = 100;
constexpr auto kSequenceDate = TimeId(1704240000);
constexpr auto kMessageDate = TimeId(1704067200);
constexpr auto kGroupNextPage = "chats/chat_1/messages2.html";
constexpr auto kChannelPage = "chats/chat_2/messages.html";
constexpr auto kEntityText = "\xF0\x9F\x98\x80 Bold <i>& link code tail";
constexpr auto kEscapedText = "Esc \"q\" 'a' & <b> \\ "
	"x\x01y\xE2\x80\xA8z\xE2\x80\xA9w\nend";

struct DialogFixture {
	DialogInfo info;
	PeerId selfPeerId = 0;
	QVector<MTPUser> users;
	QVector<MTPChat> chats;
	std::vector<SliceFixture> slices;
};

struct Check {
	const char *name = nullptr;
	QString (*run)(const Outputs &outputs) = nullptr;
};

[[nodiscard]] MTPUser UserFixture(
		BareId id,
		const char *firstName,
		const char *lastName,
		bool self = false) {
	using Flag = MTPDuser::Flag;
	const auto flags = Flag::f_first_name
		| Flag::f_last_name
		| (self ? Flag::f_self : Flag());
	return MTP_user(
		MTP_flags(flags),
		MTP_long(id),
		MTPlong(),
		MTP_string(firstName),
		MTP_string(lastName),
		MTPstring(),
		MTPstring(),
		MTPUserProfilePhoto(),
		MTPUserStatus(),
		MTPint(),
		MTPVector<MTPRestrictionReason>(),
		MTPstring(),
		MTPstring(),
		MTPEmojiStatus(),
		MTPVector<MTPUsername>(),
		MTPRecentStory(),
		MTPPeerColor(),
		MTPPeerColor(),
		MTPint(),
		MTPlong(),
		MTPlong(),
		MTPlong());
}

[[nodiscard]] MTPChat GroupFixture(BareId id, const char *title) {
	return MTP_chat(
		MTP_flags(0),
		MTP_long(id),
		MTP_string(title),
		MTP_chatPhotoEmpty(),
		MTP_int(2),
		MTP_int(1703980800),
		MTP_int(1),
		MTPInputChannel(),
		MTPChatAdminRights(),
		MTPChatBannedRights());
}

[[nodiscard]] MTPChat ChannelFixture(BareId id, const char *title) {
	return MTP_channel(
		MTP_flags(MTPDchannel::Flag::f_broadcast),
		MTP_long(id),
		MTPlong(),
		MTP_string(title),
		MTPstring(),
		MTP_chatPhotoEmpty(),
		MTP_int(1703980800),
		MTPVector<MTPRestrictionReason>(),
		MTPChatAdminRights(),
		MTPChatBannedRights(),
		MTPChatBannedRights(),
		MTPint(),
		MTPVector<MTPUsername>(),
		MTPRecentStory(),
		MTPPeerColor(),
		MTPPeerColor(),
		MTPEmojiStatus(),
		MTPint(),
		MTPint(),
		MTPlong(),
		MTPlong(),
		MTPlong(),
		MTPlong(),
		MTPint());
}

[[nodiscard]] MTPMessageFwdHeader ForwardHeader(
		PeerId fromId,
		const char *fromName,
		TimeId date) {
	using Flag = MTPDmessageFwdHeader::Flag;
	return MTP_messageFwdHeader(
		MTP_flags((fromId ? Flag::f_from_id : Flag())
			| (fromName ? Flag::f_from_name : Flag())),
		fromId ? MTPPeer(peerToMTP(fromId)) : MTPPeer(),
		fromName ? MTP_string(fromName) : MTPstring(),
		MTP_int(date),
		MTPint(),
		MTPstring(),
		MTPPeer(),
		MTPint(),
		MTPPeer(),
		MTPstring(),
		MTPint(),
		MTPstring());
}

[[nodiscard]] MTPMessageMedia DocumentMedia(
		BareId id,
		const char *name,
		const char *mime,
		int64 size) {
	return MTP_messageMediaDocument(
		MTP_flags(MTPDmessageMediaDocument::Flag::f_document),
		DocumentFixture(id, name, mime, size),
		MTPVector<MTPDocument>(),
		MTPPhoto(),
		MTPint(),
		MTPint());
}

[[nodiscard]] TextFixture GroupText(
		int32 id,
		TimeId seconds,
		BareId fromId,
		const char *text) {
	return {
		.id = id,
		.date = kMessageDate + seconds,
		.fromId = peerFromUser(UserId(fromId)),
		.peerId = peerFromChat(ChatId(kGroupId)),
		.text = text,
	};
}

[[nodiscard]] std::vector<MTPMessage> SenderMessages() {
	auto forwarded = GroupText(4005, 3660, kAliceId, "Forwarded from Bob.");
	forwarded.forward = ForwardHeader(
		peerFromUser(UserId(kBobId)),
		nullptr,
		kMessageDate - 3600);
	auto hidden = GroupText(4006, 3720, kAliceId, "Forwarded, name only.");
	hidden.forward = ForwardHeader(
		PeerId(0),
		"Hidden <Sender> & Co",
		kMessageDate - 7200);
	auto sameChat = GroupText(4007, 7200, kAliceId, "Reply in this chat.");
	sameChat.replyToId = 4001;
	auto ownPeer = GroupText(4008, 7260, kAliceId, "Reply naming this chat.");
	ownPeer.replyToId = 4002;
	ownPeer.replyToPeerId = peerFromChat(ChatId(kGroupId));
	auto otherChat = GroupText(4009, 7320, kAliceId, "Reply elsewhere.");
	otherChat.replyToId = 77;
	otherChat.replyToPeerId = peerFromChannel(ChannelId(kChannelId));
	return {
		TextMessage(GroupText(4001, 0, kAliceId, "Plain message.")),
		TextMessage(GroupText(4002, 900, kAliceId, "Joined at 900.")),
		TextMessage(GroupText(4003, 1801, kAliceId, "Wrapped at 901.")),
		TextMessage(GroupText(4004, 3600, kMissingId, "From nobody known.")),
		TextMessage(forwarded),
		TextMessage(hidden),
		TextMessage(sameChat),
		TextMessage(ownPeer),
		TextMessage(otherChat),
	};
}

[[nodiscard]] std::vector<MTPMessage> ContentMessages() {
	auto photo = GroupText(4010, 10800, kAliceId, "Sunrise & \"sea\"");
	photo.media = MTP_messageMediaPhoto(
		MTP_flags(MTPDmessageMediaPhoto::Flag::f_photo),
		PhotoFixture(4410, 1280, 720, 98765),
		MTPint(),
		MTPDocument());
	auto document = GroupText(4011, 10860, kBobId, "Quarterly <report>");
	document.media = DocumentMedia(
		4411,
		"report.pdf",
		"application/pdf",
		2048);
	auto skipped = GroupText(4012, 10920, kBobId, "");
	skipped.media = DocumentMedia(
		4412,
		"archive.zip",
		"application/zip",
		3145728);
	auto unsupported = GroupText(4013, 14400, kAliceId, "Hidden body.");
	unsupported.media = MTP_messageMediaUnsupported();
	auto entities = GroupText(4015, 18000, kAliceId, kEntityText);
	entities.entities = {
		MTP_messageEntityBold(MTP_int(3), MTP_int(4)),
		MTP_messageEntityItalic(MTP_int(8), MTP_int(4)),
		MTP_messageEntityTextUrl(
			MTP_int(13),
			MTP_int(4),
			MTP_string("https://example.com/a?x=1&y=2")),
		MTP_messageEntityCode(MTP_int(18), MTP_int(4)),
	};
	return {
		TextMessage(photo),
		TextMessage(document),
		TextMessage(skipped),
		TextMessage(unsupported),
		TextMessage(GroupText(4014, 14460, kAliceId, "")),
		TextMessage(entities),
		TextMessage(GroupText(4016, 18060, kAliceId, kEscapedText)),
		TextMessage(GroupText(4017, 75570, kAliceId, "Before midnight.")),
		TextMessage(GroupText(4018, 75630, kAliceId, "After midnight.")),
	};
}

[[nodiscard]] SliceFixture MessageSlice() {
	auto result = SliceFixture{
		.wire = SenderMessages(),
		.skips = { { 4012, File::SkipReason::FileSize } },
	};
	auto content = ContentMessages();
	result.wire.insert(end(result.wire), begin(content), end(content));
	return result;
}

[[nodiscard]] int FixtureCount(const DialogFixture &dialog) {
	auto result = 0;
	for (const auto &slice : dialog.slices) {
		result += int(slice.wire.size() + slice.direct.size());
	}
	return result;
}

void AppendPagination(DialogFixture &dialog) {
	const auto alice = [&](
			int32 id,
			TimeId date,
			const char *text,
			int32 replyToId) {
		return TextMessage({
			.id = id,
			.date = date,
			.fromId = peerFromUser(UserId(kAliceId)),
			.peerId = dialog.info.peerId,
			.replyToId = replyToId,
			.text = text,
		});
	};
	dialog.slices.push_back({
		.wire = { alice(5001, kSequenceDate, "Seam before.", 0) },
	});
	dialog.slices.push_back({
		.wire = { alice(5002, kSequenceDate + 60, "Seam after.", 0) },
	});
	const auto filler = kMessagesPerPage - 1 - FixtureCount(dialog);
	for (auto i = 0; i < filler; ++i) {
		if (!(i % kFillerPerSlice)) {
			dialog.slices.emplace_back();
		}
		dialog.slices.back().wire.push_back(
			alice(6001 + i, kSequenceDate + 120 + i, "Filler.", 0));
	}
	dialog.slices.push_back({
		.wire = {
			alice(9000, kSequenceDate + 3600, "Last on page one.", 5001),
			alice(9001, kSequenceDate + 3660, "First on page two.", 9000),
		},
	});
}

[[nodiscard]] DialogFixture GroupDialog() {
	auto result = DialogFixture{
		.info = DialogInfo{
			.type = DialogInfo::Type::PrivateGroup,
			.name = "Fixture Group",
			.input = MTP_inputPeerChat(MTP_long(kGroupId)),
			.peerId = peerFromChat(ChatId(kGroupId)),
		},
		.selfPeerId = peerFromUser(UserId(kSelfId)),
		.users = {
			UserFixture(kSelfId, "Self", "Example", true),
			UserFixture(kAliceId, "Alice", "Example"),
			UserFixture(kBobId, "Bob \"Quoted\"", "O'Example"),
		},
		.chats = { GroupFixture(kGroupId, "Fixture Group") },
	};
	result.slices.push_back(ActionSlice());
	result.slices.push_back(MessageSlice());
	result.slices.push_back(RichSlice());
	AppendPagination(result);
	return result;
}

[[nodiscard]] DialogFixture ChannelDialog() {
	const auto channel = peerFromChannel(ChannelId(kChannelId));
	const auto post = [&](
			int32 id,
			TimeId date,
			const char *text,
			const char *signature) {
		return TextMessage({
			.id = id,
			.date = date,
			.peerId = channel,
			.text = text,
			.signature = signature,
		});
	};
	auto result = DialogFixture{
		.info = DialogInfo{
			.type = DialogInfo::Type::PrivateChannel,
			.name = "Fixture Channel",
			.input = MTP_inputPeerChannel(
				MTP_long(kChannelId),
				MTP_long(0)),
			.peerId = channel,
		},
		.selfPeerId = peerFromUser(UserId(kSelfId)),
		.chats = { ChannelFixture(kChannelId, "Fixture Channel") },
	};
	result.slices.push_back({
		.wire = {
			post(7001, kSequenceDate, "Channel post one.", nullptr),
			post(
				7002,
				kSequenceDate + 60,
				"Channel post two.",
				"Editor \"Night\" <Desk>"),
		},
	});
	return result;
}

[[nodiscard]] std::vector<DialogFixture> Fixtures() {
	auto result = std::vector<DialogFixture>();
	result.push_back(GroupDialog());
	result.push_back(ChannelDialog());
	return result;
}

[[nodiscard]] std::vector<int32> WireIds(const SliceFixture &slice) {
	auto result = std::vector<int32>();
	for (const auto &message : slice.wire) {
		result.push_back(message.match([](const auto &data) {
			return data.vid().v;
		}));
	}
	return result;
}

[[nodiscard]] std::vector<Message> DirectMessages(const SliceFixture &slice) {
	auto result = std::vector<Message>();
	for (const auto build : slice.direct) {
		result.push_back(build());
	}
	return result;
}

template <typename Item, typename Projection>
[[nodiscard]] std::vector<Item> Interleaved(
		std::vector<Item> wire,
		std::vector<Item> direct,
		Projection id) {
	auto result = std::vector<Item>();
	result.reserve(wire.size() + direct.size());
	auto next = begin(direct);
	const auto directFirst = [&](const Item &item) {
		return (next != end(direct))
			&& (std::invoke(id, *next) < std::invoke(id, item));
	};
	for (auto &item : wire) {
		while (directFirst(item)) {
			result.push_back(std::move(*next++));
		}
		result.push_back(std::move(item));
	}
	for (; next != end(direct); ++next) {
		result.push_back(std::move(*next));
	}
	return result;
}

[[nodiscard]] std::vector<int32> FixtureIds(const DialogFixture &dialog) {
	auto result = std::vector<int32>();
	for (const auto &slice : dialog.slices) {
		const auto direct = DirectMessages(slice);
		const auto ids = Interleaved(
			WireIds(slice),
			direct
				| ranges::views::transform(&Message::id)
				| ranges::to_vector,
			std::identity());
		result.insert(end(result), begin(ids), end(ids));
	}
	return result;
}

[[nodiscard]] QString ExpectIds(
		const QString &label,
		const std::vector<int32> &got,
		const std::vector<int32> &expected) {
	if (got == expected) {
		return QString();
	}
	const auto [left, right] = ranges::mismatch(got, expected);
	const auto show = [](const std::vector<int32> &ids, auto i) {
		return (i != end(ids)) ? QString::number(*i) : u"none"_q;
	};
	return u"%1 has %2 ids, expected %3; first difference at %4: "
		"%5, expected %6"_q
		.arg(label)
		.arg(int(got.size()))
		.arg(int(expected.size()))
		.arg(int(left - begin(got)))
		.arg(show(got, left), show(expected, right));
}

[[nodiscard]] Settings FixtureSettings(const QString &path) {
	auto result = Settings();
	result.path = path;
	result.format = Export::Output::Format::HtmlAndJson;
	result.types = Settings::Type::AnyChatsMask;
	result.fullChats = Settings::Type::AnyChatsMask;
	return result;
}

[[nodiscard]] Environment FixtureEnvironment() {
	return Environment{
		.internalLinksDomain = u"https://t.me/"_q,
		.aboutTelegram = "About fixture export.",
		.aboutChats = "About fixture chats.",
		.aboutLeftChats = "About fixture left chats.",
	};
}

void EmulateDownload(File &file, File::SkipReason skip) {
	if (file.suggestedPath.isEmpty()) {
		return;
	} else if (skip != File::SkipReason::None) {
		file.skipReason = skip;
	} else {
		file.relativePath = file.suggestedPath;
	}
}

void EmulateDownloads(MessagesSlice &result, const SliceFixture &slice) {
	const auto skip = [](const auto &skips, auto key) {
		const auto i = skips.find(key);
		return (i != end(skips)) ? i->second : File::SkipReason::None;
	};
	for (auto &message : result.list) {
		EmulateDownload(message.file(), skip(slice.skips, message.id));
		if (!message.richMessage) {
			continue;
		}
		for (auto &[id, photo] : message.richMessage->photos) {
			EmulateDownload(photo.image.file, skip(slice.richSkips, id));
		}
		for (auto &[id, document] : message.richMessage->documents) {
			EmulateDownload(document.file, skip(slice.richSkips, id));
		}
	}
}

[[nodiscard]] MessagesSlice PrepareSlice(
		ParseMediaContext &context,
		const DialogFixture &fixture,
		const SliceFixture &slice,
		const QString &folder,
		QString &orderFailure) {
	auto newestFirst = QVector<MTPMessage>(
		slice.wire.rbegin(),
		slice.wire.rend());
	auto result = ParseMessagesSlice(
		context,
		MTP_vector<MTPMessage>(std::move(newestFirst)),
		MTP_vector<MTPUser>(fixture.users),
		MTP_vector<MTPChat>(fixture.chats),
		folder);
	const auto declared = WireIds(slice);
	const auto order = ExpectIds(
		u"parser slice %1-%2"_q
			.arg(declared.empty() ? 0 : declared.front())
			.arg(declared.empty() ? 0 : declared.back()),
		result.list
			| ranges::views::transform(&Message::id)
			| ranges::to_vector,
		declared);
	if (orderFailure.isEmpty()) {
		orderFailure = order;
	}
	result.list = Interleaved(
		std::move(result.list),
		DirectMessages(slice),
		&Message::id);
	EmulateDownloads(result, slice);
	return result;
}

[[nodiscard]] QString WriteExport(
		const Settings &settings,
		const DialogsInfo &info,
		const std::vector<DialogFixture> &fixtures,
		Outputs &outputs) {
	using Export::Output::Result;

	auto failure = QString();
	const auto stage = [&](const char *name, const Result &result) {
		if (!result && failure.isEmpty()) {
			failure = u"%1 failed at '%2'"_q.arg(
				QString::fromLatin1(name),
				result.path);
		}
		return failure.isEmpty();
	};
	const auto environment = FixtureEnvironment();
	const auto writer = Export::Output::CreateWriter(
		Export::Output::Format::HtmlAndJson);
	if (!stage("start", writer->start(settings, environment, nullptr))
		|| !stage("writeDialogsStart", writer->writeDialogsStart(info))) {
		return failure;
	}
	for (auto i = 0; i != int(fixtures.size()); ++i) {
		const auto &fixture = fixtures[i];
		const auto &dialog = info.chats[i];
		if (!stage("writeDialogStart", writer->writeDialogStart(dialog))) {
			return failure;
		}
		auto context = ParseMediaContext{
			.selfPeerId = fixture.selfPeerId,
		};
		auto &parsed = outputs.parsed.emplace_back();
		for (const auto &messages : fixture.slices) {
			auto slice = PrepareSlice(
				context,
				fixture,
				messages,
				dialog.relativePath,
				outputs.parserOrder);
			if (!stage("writeDialogSlice", writer->writeDialogSlice(slice))) {
				return failure;
			}
			for (auto &message : slice.list) {
				parsed.push_back(std::move(message));
			}
		}
		if (!stage("writeDialogEnd", writer->writeDialogEnd())) {
			return failure;
		}
	}
	if (!stage("writeDialogsEnd", writer->writeDialogsEnd())
		|| !stage("finish", writer->finish())) {
		return failure;
	}
	return QString();
}

[[nodiscard]] QByteArray ReadFile(const QString &path) {
	auto file = QFile(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

[[nodiscard]] std::vector<HtmlRecord> SplitRecords(const QByteArray &page) {
	const auto start = QByteArray("<div class=\"message ");
	const auto marker = QByteArray("id=\"message");
	const auto pagination = QByteArray("<a class=\"pagination block_link\"");
	const auto bounded = [&](qsizetype index) {
		return (index < 0) ? page.size() : index;
	};
	auto result = std::vector<HtmlRecord>();
	for (auto from = page.indexOf(start); from >= 0;) {
		const auto next = page.indexOf(start, from + start.size());
		const auto till = std::min(
			bounded(next),
			bounded(page.indexOf(pagination, from)));
		const auto tag = page.mid(from, page.indexOf('>', from) - from);
		const auto at = tag.indexOf(marker);
		const auto id = (at < 0)
			? 0
			: tag.mid(at + marker.size()).split('"').front().toInt();
		result.push_back({ .id = id, .html = page.mid(from, till - from) });
		from = next;
	}
	return result;
}

[[nodiscard]] std::vector<JsonDialog> IndexJson(const QJsonObject &json) {
	auto result = std::vector<JsonDialog>();
	const auto list = json.value(u"chats"_q).toObject()
		.value(u"list"_q).toArray();
	for (const auto &value : list) {
		auto &dialog = result.emplace_back();
		dialog.header = value.toObject();
		const auto messages = dialog.header.take(u"messages"_q).toArray();
		for (const auto &message : messages) {
			const auto object = message.toObject();
			const auto id = object.value(u"id"_q).toInt();
			dialog.order.push_back(id);
			dialog.messages.emplace(id, object);
		}
	}
	return result;
}

[[nodiscard]] QString ReadOutputs(
		const QString &root,
		const DialogsInfo &info,
		Outputs &outputs) {
	const auto read = [&](const QString &path) {
		const auto content = ReadFile(root + path);
		if (!content.isEmpty()) {
			outputs.files.emplace(path, content);
		}
		return !content.isEmpty();
	};
	const auto summary = {
		u"export_results.html"_q,
		u"lists/chats.html"_q,
		u"result.json"_q,
	};
	for (const auto &path : summary) {
		if (!read(path)) {
			return u"missing or empty %1"_q.arg(path);
		}
	}
	for (const auto &dialog : info.chats) {
		const auto folder = dialog.relativePath;
		const auto pages = QDir(root + folder).entryList(
			{ u"*.html"_q },
			QDir::Files,
			QDir::Name);
		outputs.folders.emplace(folder, pages);
		for (const auto &page : pages) {
			const auto path = folder + page;
			if (!read(path)) {
				return u"empty %1"_q.arg(path);
			}
			outputs.pages.emplace(path, SplitRecords(outputs.files[path]));
		}
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(
		outputs.files[u"result.json"_q],
		&error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return u"result.json: %1"_q.arg(error.errorString());
	}
	outputs.json = IndexJson(document.object());
	return QString();
}

[[nodiscard]] Outputs RunExport() {
	auto result = Outputs();
	const auto directory = QTemporaryDir();
	if (!directory.isValid()) {
		result.failure = u"no temporary directory: %1"_q.arg(
			directory.errorString());
		return result;
	}
	const auto root = directory.path() + '/';
	const auto settings = FixtureSettings(root);
	const auto fixtures = Fixtures();
	auto info = DialogsInfo();
	for (const auto &fixture : fixtures) {
		info.chats.push_back(fixture.info);
		result.fixtureIds.push_back(FixtureIds(fixture));
	}
	FinalizeDialogsInfo(info, settings);
	result.failure = WriteExport(settings, info, fixtures, result);
	if (result.failure.isEmpty()) {
		result.failure = ReadOutputs(root, info, result);
	}
	return result;
}

[[nodiscard]] const std::vector<HtmlRecord> &PageRecords(
		const Outputs &outputs,
		const char *page) {
	static const auto empty = std::vector<HtmlRecord>();
	const auto i = outputs.pages.find(QString::fromLatin1(page));
	return (i != end(outputs.pages)) ? i->second : empty;
}

[[nodiscard]] int PageRecordCount(
		const Outputs &outputs,
		const char *page,
		int32 id) {
	return int(ranges::count(
		PageRecords(outputs, page),
		id,
		&HtmlRecord::id));
}

[[nodiscard]] std::vector<int32> RealIds(
		const std::vector<HtmlRecord> &records) {
	auto result = std::vector<int32>();
	for (const auto &record : records) {
		if (record.id > 0) {
			result.push_back(record.id);
		}
	}
	return result;
}

[[nodiscard]] QJsonObject TextFields(
		const QJsonValue &from,
		const char *fromId,
		const char *text) {
	const auto value = QString::fromUtf8(text);
	const auto plain = QJsonObject{
		{ u"type"_q, u"plain"_q },
		{ u"text"_q, value },
	};
	return QJsonObject{
		{ u"type"_q, u"message"_q },
		{ u"from"_q, from },
		{ u"from_id"_q, QString::fromLatin1(fromId) },
		{ u"text"_q, value },
		{
			u"text_entities"_q,
			value.isEmpty() ? QJsonArray() : QJsonArray{ plain },
		},
	};
}

[[nodiscard]] QJsonObject AliceTextFields(const char *text) {
	return TextFields(u"Alice Example"_q, "user1001", text);
}

[[nodiscard]] QJsonObject BobTextFields(const char *text) {
	return TextFields(u"Bob \"Quoted\" O'Example"_q, "user1002", text);
}

[[nodiscard]] QString Compact(const QJsonObject &object) {
	return QString::fromUtf8(
		QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString Excerpt(const QByteArray &html) {
	constexpr auto kLimit = 2048;
	return QString::fromUtf8(html.left(kLimit))
		+ ((html.size() > kLimit) ? u"..."_q : QString());
}

[[nodiscard]] QString ExpectFolder(
		const Outputs &outputs,
		const QString &folder,
		const QStringList &expected) {
	const auto i = outputs.folders.find(folder);
	const auto got = (i != end(outputs.folders)) ? i->second : QStringList();
	return (got == expected)
		? QString()
		: u"%1 holds [%2], expected [%3]"_q.arg(
			folder,
			got.join(u", "_q),
			expected.join(u", "_q));
}

[[nodiscard]] QString CheckLifecycle(const Outputs &outputs) {
	return outputs.failure;
}

[[nodiscard]] QString CheckDialogs(const Outputs &outputs) {
	const auto header = [&](int dialog) {
		return (dialog < int(outputs.json.size()))
			? outputs.json[dialog].header
			: QJsonObject();
	};
	const auto title = [](const char *name) {
		return "<div class=\"text bold\">\n" + QByteArray(name) + " \n";
	};
	return FirstFailure({
		ExpectNumber(u"JSON dialogs"_q, int(outputs.json.size()), 2),
		ExpectObject(header(kGroupDialog), Json(R"({
			"name": "Fixture Group",
			"type": "private_group",
			"id": 2001
		})")),
		ExpectObject(header(kChannelDialog), Json(R"({
			"name": "Fixture Channel",
			"type": "private_channel",
			"id": 3001
		})")),
		ExpectInOrder(
			u"lists/chats.html"_q,
			OutputFile(outputs, "lists/chats.html"),
			{
				"<a class=\"entry block_link clearfix\" "
				"href=\"../chats/chat_1/messages.html#allow_back\">",
				"<div class=\"pull_right info details\">\ngroup\n",
				"<div class=\"name bold\">\nFixture Group \n",
				"<div class=\"details_entry details\">\n1001 messages\n",
				"<a class=\"entry block_link clearfix\" "
				"href=\"../chats/chat_2/messages.html#allow_back\">",
				"<div class=\"pull_right info details\">\nchannel\n",
				"<div class=\"name bold\">\nFixture Channel \n",
				"<div class=\"details_entry details\">\n2 messages\n",
			}),
		ExpectInOrder(
			u"group page one"_q,
			OutputFile(outputs, kGroupPage),
			{ title("Fixture Group") }),
		ExpectInOrder(
			u"group page two"_q,
			OutputFile(outputs, kGroupNextPage),
			{ title("Fixture Group") }),
		ExpectInOrder(
			u"channel page"_q,
			OutputFile(outputs, kChannelPage),
			{ title("Fixture Channel") }),
	});
}

[[nodiscard]] QString CheckStreamOrder(const Outputs &outputs) {
	if (outputs.json.size() != 2 || outputs.fixtureIds.size() != 2) {
		return u"expected two dialogs, JSON has %1"_q.arg(
			int(outputs.json.size()));
	}
	const auto &group = outputs.fixtureIds[kGroupDialog];
	const auto ascending = (ranges::adjacent_find(
		group,
		std::greater_equal<>()) == end(group));
	const auto pageOne = RealIds(PageRecords(outputs, kGroupPage));
	const auto pageTwo = RealIds(PageRecords(outputs, kGroupNextPage));
	auto pages = pageOne;
	pages.insert(end(pages), begin(pageTwo), end(pageTwo));
	return FirstFailure({
		outputs.parserOrder,
		ascending ? QString() : u"group fixture ids are not ascending"_q,
		ExpectNumber(u"group fixture messages"_q, int(group.size()), 1001),
		ExpectIds(
			u"JSON group stream"_q,
			outputs.json[kGroupDialog].order,
			group),
		ExpectIds(u"HTML group pages"_q, pages, group),
		ExpectNumber(u"page one messages"_q, int(pageOne.size()), 1000),
		ExpectNumber(
			u"page one last id"_q,
			pageOne.empty() ? 0 : pageOne.back(),
			9000),
		ExpectIds(u"page two"_q, pageTwo, { 9001 }),
		ExpectIds(
			u"JSON channel stream"_q,
			outputs.json[kChannelDialog].order,
			{ 7001, 7002 }),
		ExpectIds(
			u"HTML channel page"_q,
			RealIds(PageRecords(outputs, kChannelPage)),
			{ 7001, 7002 }),
	});
}

[[nodiscard]] QString CheckSliceSeamJoin(const Outputs &outputs) {
	const auto before = PageRecord(outputs, kGroupPage, 5001);
	const auto after = PageRecord(outputs, kGroupPage, 5002);
	return FirstFailure({
		ExpectObject(
			JsonMessage(outputs, kGroupDialog, 5001),
			Merged(AliceTextFields("Seam before."), Json(R"({
				"id": 5001,
				"date": "2024-01-03T03:00:00",
				"date_unixtime": "1704240000"
			})"))),
		ExpectObject(
			JsonMessage(outputs, kGroupDialog, 5002),
			Merged(AliceTextFields("Seam after."), Json(R"({
				"id": 5002,
				"date": "2024-01-03T03:01:00",
				"date_unixtime": "1704240060"
			})"))),
		ExpectInOrder(u"HTML 5001"_q, before, {
			"<div class=\"message default clearfix\" id=\"message5001\">",
			"<div class=\"pull_left userpic_wrap\">",
			kAliceFromName,
			"<div class=\"text\">\nSeam before.\n",
		}),
		ExpectInOrder(u"HTML 5002"_q, after, {
			"<div class=\"message default clearfix joined\" "
			"id=\"message5002\">",
			"<div class=\"text\">\nSeam after.\n",
		}),
		ExpectAbsent(u"HTML 5002"_q, after, { "userpic_wrap", "from_name" }),
	});
}

[[nodiscard]] QString CheckPageOneLast(const Outputs &outputs) {
	return FirstFailure({
		ExpectObject(
			JsonMessage(outputs, kGroupDialog, 9000),
			Merged(AliceTextFields("Last on page one."), Json(R"({
				"id": 9000,
				"date": "2024-01-03T04:00:00",
				"date_unixtime": "1704243600",
				"reply_to_message_id": 5001
			})"))),
		ExpectNumber(
			u"page two records of 9000"_q,
			PageRecordCount(outputs, kGroupNextPage, 9000),
			0),
		ExpectInOrder(u"HTML 9000"_q, PageRecord(outputs, kGroupPage, 9000), {
			"<div class=\"message default clearfix\" id=\"message9000\">",
			"<div class=\"pull_left userpic_wrap\">",
			kAliceFromName,
			"<div class=\"reply_to details\">\nIn reply to "
			"<a href=\"#go_to_message5001\" "
			"onclick=\"return GoToMessage(5001)\">this message</a>\n",
			"<div class=\"text\">\nLast on page one.\n",
		}),
	});
}

[[nodiscard]] QString CheckPageTwoFirst(const Outputs &outputs) {
	const auto &records = PageRecords(outputs, kGroupNextPage);
	const auto first = records.empty() ? HtmlRecord() : records[0];
	const auto second = (records.size() < 2) ? HtmlRecord() : records[1];
	return FirstFailure({
		ExpectObject(
			JsonMessage(outputs, kGroupDialog, 9001),
			Merged(AliceTextFields("First on page two."), Json(R"({
				"id": 9001,
				"date": "2024-01-03T04:01:00",
				"date_unixtime": "1704243660",
				"reply_to_message_id": 9000
			})"))),
		ExpectNumber(
			u"page one records of 9001"_q,
			PageRecordCount(outputs, kGroupPage, 9001),
			0),
		ExpectInOrder(u"page two first record"_q, first.html, {
			"<div class=\"message service\" id=\"message-",
			"<div class=\"body details\">\n3 January 2024\n",
		}),
		ExpectNumber(u"page two second record"_q, second.id, 9001),
		ExpectInOrder(u"HTML 9001"_q, second.html, {
			"<div class=\"message default clearfix\" id=\"message9001\">",
			"<div class=\"pull_left userpic_wrap\">",
			kAliceFromName,
			"<div class=\"reply_to details\">\nIn reply to "
			"<a href=\"messages.html#go_to_message9000\">this message</a>\n",
			"<div class=\"text\">\nFirst on page two.\n",
		}),
	});
}

[[nodiscard]] QString CheckPageLinks(const Outputs &outputs) {
	const auto record = QByteArray("<div class=\"message ");
	const auto next = QByteArray("<a class=\"pagination block_link\" "
		"href=\"messages2.html\">\nNext messages\n");
	const auto previous = QByteArray("<a class=\"pagination block_link\" "
		"href=\"messages.html\">\nPrevious messages\n");
	const auto &one = OutputFile(outputs, kGroupPage);
	const auto &two = OutputFile(outputs, kGroupNextPage);
	const auto nextAt = one.indexOf(next);
	const auto previousAt = two.indexOf(previous);
	return FirstFailure({
		ExpectFolder(
			outputs,
			u"chats/chat_1/"_q,
			{ u"messages.html"_q, u"messages2.html"_q }),
		ExpectFolder(outputs, u"chats/chat_2/"_q, { u"messages.html"_q }),
		(nextAt >= 0 && nextAt > one.lastIndexOf(record))
			? QString()
			: u"page one has no Next messages link after its records"_q,
		(previousAt >= 0 && previousAt < two.indexOf(record))
			? QString()
			: u"page two has no Previous messages link before its records"_q,
		ExpectAbsent(u"page one"_q, one, { "Previous messages" }),
		ExpectAbsent(u"page two"_q, two, { "Next messages" }),
		ExpectAbsent(
			u"channel page"_q,
			OutputFile(outputs, kChannelPage),
			{ "Previous messages", "Next messages" }),
	});
}

[[nodiscard]] QByteArray Zone() {
	return QTimeZone::systemTimeZone().displayName(
		QTimeZone::StandardTime,
		QTimeZone::OffsetName).toUtf8();
}

[[nodiscard]] QByteArray DateHtml(const char *title, const char *time) {
	return "<div class=\"pull_right date details\" title=\""
		+ QByteArray(title)
		+ ' '
		+ Zone()
		+ "\">\n"
		+ time
		+ '\n';
}

[[nodiscard]] QByteArray ForwardedHtml(const char *name, const char *date) {
	return "<div class=\"from_name\">\n"
		+ QByteArray(name)
		+ "<span class=\"date details\" title=\""
		+ date
		+ ' '
		+ Zone()
		+ "\"> "
		+ date
		+ "</span>";
}

[[nodiscard]] QByteArray TextHtml(const QByteArray &text) {
	return "<div class=\"text\">\n" + text + '\n';
}

[[nodiscard]] QString ExpectGroupJson(
		const Outputs &outputs,
		int32 id,
		const QJsonObject &expected) {
	return ExpectObject(JsonMessage(outputs, kGroupDialog, id), expected);
}

[[nodiscard]] QString ExpectDivider(
		const Outputs &outputs,
		int32 id,
		const QByteArray &day) {
	const auto &records = PageRecords(outputs, kGroupPage);
	const auto i = ranges::find(records, id, &HtmlRecord::id);
	const auto before = (i != end(records) && i != begin(records))
		? (i - 1)->html
		: QByteArray();
	return ExpectInOrder(u"record before %1"_q.arg(id), before, {
		"<div class=\"message service\" id=\"message-",
		"<div class=\"body details\">\n" + day + '\n',
	});
}

[[nodiscard]] QString CheckPlainMessage(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4001,
			Merged(AliceTextFields("Plain message."), Json(R"({
				"id": 4001,
				"date": "2024-01-01T03:00:00",
				"date_unixtime": "1704067200"
			})"))),
		ExpectDivider(outputs, 4001, "1 January 2024"),
		ExpectGroupHtml(outputs, 4001, {
			RecordStart(4001),
			"<div class=\"pull_left userpic_wrap\">",
			DateHtml("01.01.2024 03:00:00", "03:00"),
			kAliceFromName,
			TextHtml("Plain message."),
		}),
	});
}

[[nodiscard]] QString CheckJoinWithin900(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4002,
			Merged(AliceTextFields("Joined at 900."), Json(R"({
				"id": 4002,
				"date": "2024-01-01T03:15:00",
				"date_unixtime": "1704068100"
			})"))),
		ExpectGroupHtml(outputs, 4002, {
			RecordStart(4002, true),
			DateHtml("01.01.2024 03:15:00", "03:15"),
			TextHtml("Joined at 900."),
		}),
		ExpectAbsent(
			u"HTML 4002"_q,
			PageRecord(outputs, kGroupPage, 4002),
			{ "userpic_wrap", "from_name" }),
	});
}

[[nodiscard]] QString CheckWrapAfter901(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4003,
			Merged(AliceTextFields("Wrapped at 901."), Json(R"({
				"id": 4003,
				"date": "2024-01-01T03:30:01",
				"date_unixtime": "1704069001"
			})"))),
		ExpectGroupHtml(outputs, 4003, {
			RecordStart(4003),
			"<div class=\"pull_left userpic_wrap\">",
			DateHtml("01.01.2024 03:30:01", "03:30"),
			kAliceFromName,
			TextHtml("Wrapped at 901."),
		}),
	});
}

[[nodiscard]] QString CheckMissingSender(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4004,
			Merged(
				TextFields(QJsonValue(), "user1099", "From nobody known."),
				Json(R"({
					"id": 4004,
					"date": "2024-01-01T04:00:00",
					"date_unixtime": "1704070800"
				})"))),
		ExpectGroupHtml(outputs, 4004, {
			RecordStart(4004),
			"<div class=\"pull_left userpic_wrap\">",
			"<div class=\"from_name\">\nDeleted Account\n",
			TextHtml("From nobody known."),
		}),
	});
}

[[nodiscard]] QString CheckForwardedUser(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4005,
			Merged(AliceTextFields("Forwarded from Bob."), Json(R"({
				"id": 4005,
				"date": "2024-01-01T04:01:00",
				"date_unixtime": "1704070860",
				"forwarded_from": "Bob \"Quoted\" O'Example",
				"forwarded_from_id": "user1002"
			})"))),
		ExpectGroupHtml(outputs, 4005, {
			RecordStart(4005),
			kAliceFromName,
			"<div class=\"pull_left forwarded userpic_wrap\">",
			"<div class=\"forwarded body\">",
			ForwardedHtml(
				"Bob &quot;Quoted&quot; O&apos;Example",
				"01.01.2024 02:00:00"),
			TextHtml("Forwarded from Bob."),
		}),
	});
}

[[nodiscard]] QString CheckForwardedHidden(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4006,
			Merged(AliceTextFields("Forwarded, name only."), Json(R"({
				"id": 4006,
				"date": "2024-01-01T04:02:00",
				"date_unixtime": "1704070920",
				"forwarded_from": "Hidden <Sender> & Co"
			})"))),
		ExpectGroupHtml(outputs, 4006, {
			RecordStart(4006),
			kAliceFromName,
			"<div class=\"pull_left forwarded userpic_wrap\">",
			"<div class=\"forwarded body\">",
			ForwardedHtml(
				"Hidden &lt;Sender&gt; &amp; Co",
				"01.01.2024 01:00:00"),
			TextHtml("Forwarded, name only."),
		}),
	});
}

[[nodiscard]] QString CheckReplySameChat(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4007,
			Merged(AliceTextFields("Reply in this chat."), Json(R"({
				"id": 4007,
				"date": "2024-01-01T05:00:00",
				"date_unixtime": "1704074400",
				"reply_to_message_id": 4001
			})"))),
		ExpectGroupHtml(outputs, 4007, {
			RecordStart(4007),
			"<div class=\"reply_to details\">\nIn reply to "
			"<a href=\"#go_to_message4001\" "
			"onclick=\"return GoToMessage(4001)\">this message</a>\n",
			TextHtml("Reply in this chat."),
		}),
	});
}

[[nodiscard]] QString CheckReplyOwnPeer(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4008,
			Merged(AliceTextFields("Reply naming this chat."), Json(R"({
				"id": 4008,
				"date": "2024-01-01T05:01:00",
				"date_unixtime": "1704074460",
				"reply_to_message_id": 4002
			})"))),
		ExpectGroupHtml(outputs, 4008, {
			RecordStart(4008, true),
			"<div class=\"reply_to details\">\nIn reply to "
			"<a href=\"#go_to_message4002\" "
			"onclick=\"return GoToMessage(4002)\">this message</a>\n",
			TextHtml("Reply naming this chat."),
		}),
	});
}

[[nodiscard]] QString CheckReplyOtherChat(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4009,
			Merged(AliceTextFields("Reply elsewhere."), Json(R"({
				"id": 4009,
				"date": "2024-01-01T05:02:00",
				"date_unixtime": "1704074520",
				"reply_to_message_id": 77,
				"reply_to_peer_id": "channel3001"
			})"))),
		ExpectGroupHtml(outputs, 4009, {
			RecordStart(4009, true),
			"<div class=\"reply_to details\">\n"
			"In reply to a message in another chat\n",
			TextHtml("Reply elsewhere."),
		}),
	});
}

[[nodiscard]] QString CheckChannelPosts(const Outputs &outputs) {
	const auto channel = [](const char *text) {
		return TextFields(u"Fixture Channel"_q, "channel3001", text);
	};
	const auto first = PageRecord(outputs, kChannelPage, 7001);
	return FirstFailure({
		ExpectObject(
			JsonMessage(outputs, kChannelDialog, 7001),
			Merged(channel("Channel post one."), Json(R"({
				"id": 7001,
				"date": "2024-01-03T03:00:00",
				"date_unixtime": "1704240000"
			})"))),
		ExpectObject(
			JsonMessage(outputs, kChannelDialog, 7002),
			Merged(channel("Channel post two."), Json(R"({
				"id": 7002,
				"date": "2024-01-03T03:01:00",
				"date_unixtime": "1704240060",
				"author": "Editor \"Night\" <Desk>"
			})"))),
		ExpectInOrder(u"HTML 7001"_q, first, {
			RecordStart(7001),
			"<div class=\"pull_left userpic_wrap\">",
			"<div class=\"from_name\">\nFixture Channel \n",
			TextHtml("Channel post one."),
		}),
		ExpectAbsent(u"HTML 7001"_q, first, { "signature" }),
		ExpectInOrder(
			u"HTML 7002"_q,
			PageRecord(outputs, kChannelPage, 7002),
			{
				RecordStart(7002, true),
				TextHtml("Channel post two."),
				"<div class=\"signature details\">\n"
				"Editor &quot;Night&quot; &lt;Desk&gt;\n",
			}),
	});
}

[[nodiscard]] QString CheckPhotoMessage(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4010,
			Merged(AliceTextFields("Sunrise & \"sea\""), Json(R"({
				"id": 4010,
				"date": "2024-01-01T06:00:00",
				"date_unixtime": "1704078000",
				"photo": "chats/chat_1/photos/photo_3@01-01-2024_06-00-00.jpg",
				"photo_file_size": 98765,
				"width": 1280,
				"height": 720
			})"))),
		ExpectGroupHtml(outputs, 4010, {
			RecordStart(4010),
			kAliceFromName,
			"<div class=\"media_wrap clearfix\">",
			"<a class=\"media clearfix pull_left block_link media_photo\" "
			"href=\"../../chats/chat_1/photos/"
			"photo_3@01-01-2024_06-00-00.jpg\">",
			"<div class=\"fill pull_left\">",
			"<div class=\"title bold\">\nPhoto\n",
			"<div class=\"status details\">\n1280\xC3\x97" "720\n",
			TextHtml("Sunrise &amp; &quot;sea&quot;"),
		}),
		ExpectAbsent(
			u"HTML 4010"_q,
			PageRecord(outputs, kGroupPage, 4010),
			{ "<div class=\"description\">" }),
	});
}

[[nodiscard]] QString CheckDocumentMessage(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4011,
			Merged(BobTextFields("Quarterly <report>"), Json(R"({
				"id": 4011,
				"date": "2024-01-01T06:01:00",
				"date_unixtime": "1704078060",
				"file": "chats/chat_1/files/report.pdf",
				"file_name": "report.pdf",
				"file_size": 2048,
				"mime_type": "application/pdf"
			})"))),
		ExpectGroupHtml(outputs, 4011, {
			RecordStart(4011),
			"<div class=\"from_name\">\n"
			"Bob &quot;Quoted&quot; O&apos;Example\n",
			"<a class=\"media clearfix pull_left block_link media_file\" "
			"href=\"../../chats/chat_1/files/report.pdf\">",
			"<div class=\"title bold\">\nreport.pdf\n",
			"<div class=\"status details\">\n2.0 KB\n",
			TextHtml("Quarterly &lt;report&gt;"),
		}),
	});
}

[[nodiscard]] QString CheckSkippedFile(const Outputs &outputs) {
	const auto record = PageRecord(outputs, kGroupPage, 4012);
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4012,
			Merged(BobTextFields(""), Json(R"json({
				"id": 4012,
				"date": "2024-01-01T06:02:00",
				"date_unixtime": "1704078120",
				"file": "(File exceeds maximum size. )json"
				R"json(Change data exporting settings to download.)",
				"file_name": "archive.zip",
				"file_size": 3145728,
				"mime_type": "application/zip"
			})json"))),
		ExpectInOrder(u"HTML 4012"_q, record, {
			RecordStart(4012, true),
			"<div class=\"media clearfix pull_left media_file\">",
			"<div class=\"title bold\">\narchive.zip\n",
			"<div class=\"description\">\nExceeds maximum size, "
			"change data exporting settings to download.\n",
			"<div class=\"status details\">\n3.0 MB\n",
		}),
		ExpectAbsent(
			u"HTML 4012"_q,
			record,
			{ "href", "<div class=\"text\">" }),
	});
}

[[nodiscard]] QString CheckUnsupportedMedia(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(outputs, 4013, Json(R"({
			"id": 4013,
			"type": "unsupported"
		})")),
		ExpectServiceHtml(outputs, 4013, kUnsupportedNotice),
		ExpectAbsent(
			u"HTML 4013"_q,
			PageRecord(outputs, kGroupPage, 4013),
			{ "Hidden body." }),
	});
}

[[nodiscard]] QString CheckEmptyMessage(const Outputs &outputs) {
	const auto record = PageRecord(outputs, kGroupPage, 4014);
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4014,
			Merged(AliceTextFields(""), Json(R"({
				"id": 4014,
				"date": "2024-01-01T07:01:00",
				"date_unixtime": "1704081660"
			})"))),
		ExpectInOrder(u"HTML 4014"_q, record, {
			RecordStart(4014),
			DateHtml("01.01.2024 07:01:00", "07:01"),
			kAliceFromName,
		}),
		ExpectAbsent(
			u"HTML 4014"_q,
			record,
			{ "<div class=\"text\">", "media_wrap" }),
	});
}

[[nodiscard]] QString CheckEntities(const Outputs &outputs) {
	const auto text = RawJsonMembers(outputs, 4015, "text");
	return FirstFailure({
		ExpectGroupJson(outputs, 4015, Json(R"({
			"id": 4015,
			"type": "message",
			"date": "2024-01-01T08:00:00",
			"date_unixtime": "1704085200",
			"from": "Alice Example",
			"from_id": "user1001",
			"text": [
				"\ud83d\ude00 ",
				{ "type": "bold", "text": "Bold" },
				" ",
				{ "type": "italic", "text": "<i>&" },
				" ",
				{
					"type": "text_link",
					"text": "link",
					"href": "https://example.com/a?x=1&y=2"
				},
				" ",
				{ "type": "code", "text": "code" },
				" tail"
			],
			"text_entities": [
				{ "type": "plain", "text": "\ud83d\ude00 " },
				{ "type": "bold", "text": "Bold" },
				{ "type": "plain", "text": " " },
				{ "type": "italic", "text": "<i>&" },
				{ "type": "plain", "text": " " },
				{
					"type": "text_link",
					"text": "link",
					"href": "https://example.com/a?x=1&y=2"
				},
				{ "type": "plain", "text": " " },
				{ "type": "code", "text": "code" },
				{ "type": "plain", "text": " tail" }
			]
		})")),
		ExpectNumber(u"raw JSON 4015 text members"_q, int(text.size()), 1),
		ExpectInOrder(
			u"raw JSON 4015 text"_q,
			text.empty() ? QByteArray() : text.front(),
			{ "[", "\"\xF0\x9F\x98\x80 \"," }),
		ExpectGroupHtml(outputs, 4015, {
			RecordStart(4015),
			TextHtml("\xF0\x9F\x98\x80 <strong>Bold</strong> "
				"<em>&lt;i&gt;&amp;</em> "
				"<a href=\"https://example.com/a?x=1&amp;y=2\">link</a> "
				"<code>code</code> tail"),
		}),
	});
}

[[nodiscard]] QString CheckEscaping(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4016,
			Merged(AliceTextFields(kEscapedText), Json(R"({
				"id": 4016,
				"date": "2024-01-01T08:01:00",
				"date_unixtime": "1704085260"
			})"))),
		ExpectRawValues(
			u"raw JSON 4016 text members"_q,
			RawJsonMembers(outputs, 4016, "text"),
			{
				R"("Esc \"q\" 'a' & <b> \\ )"
				R"(x\u0001y\u2028z\u2029w\nend")",
			}),
		ExpectGroupHtml(outputs, 4016, {
			RecordStart(4016, true),
			TextHtml("Esc &quot;q&quot; &apos;a&apos; &amp; &lt;b&gt; \\ "
				"x&#x01;y<br>z<br>w<br>end"),
		}),
	});
}

[[nodiscard]] QString CheckMidnight(const Outputs &outputs) {
	return FirstFailure({
		ExpectGroupJson(
			outputs,
			4017,
			Merged(AliceTextFields("Before midnight."), Json(R"({
				"id": 4017,
				"date": "2024-01-01T23:59:30",
				"date_unixtime": "1704142770"
			})"))),
		ExpectGroupJson(
			outputs,
			4018,
			Merged(AliceTextFields("After midnight."), Json(R"({
				"id": 4018,
				"date": "2024-01-02T00:00:30",
				"date_unixtime": "1704142830"
			})"))),
		ExpectGroupHtml(outputs, 4017, {
			RecordStart(4017),
			DateHtml("01.01.2024 23:59:30", "23:59"),
			TextHtml("Before midnight."),
		}),
		ExpectDivider(outputs, 4018, "2 January 2024"),
		ExpectGroupHtml(outputs, 4018, {
			RecordStart(4018),
			"<div class=\"pull_left userpic_wrap\">",
			DateHtml("02.01.2024 00:00:30", "00:00"),
			kAliceFromName,
			TextHtml("After midnight."),
		}),
	});
}

[[nodiscard]] std::vector<Check> Checks() {
	return {
		{ "export.lifecycle", CheckLifecycle },
		{ "export.dialogs", CheckDialogs },
		{ "sequence.streamOrder", CheckStreamOrder },
		{ "sequence.sliceSeamJoin", CheckSliceSeamJoin },
		{ "sequence.pageOneLast", CheckPageOneLast },
		{ "sequence.pageTwoFirst", CheckPageTwoFirst },
		{ "sequence.pageLinks", CheckPageLinks },
		{ "message.plain", CheckPlainMessage },
		{ "message.join-900", CheckJoinWithin900 },
		{ "message.wrap-901", CheckWrapAfter901 },
		{ "message.missing-sender", CheckMissingSender },
		{ "message.forwarded-user", CheckForwardedUser },
		{ "message.forwarded-hidden", CheckForwardedHidden },
		{ "message.reply-same-chat", CheckReplySameChat },
		{ "message.reply-own-peer", CheckReplyOwnPeer },
		{ "message.reply-other-chat", CheckReplyOtherChat },
		{ "message.channel-post", CheckChannelPosts },
		{ "message.photo", CheckPhotoMessage },
		{ "message.document", CheckDocumentMessage },
		{ "message.skipped-file", CheckSkippedFile },
		{ "message.unsupported", CheckUnsupportedMedia },
		{ "message.empty", CheckEmptyMessage },
		{ "message.entities", CheckEntities },
		{ "message.escaping", CheckEscaping },
		{ "message.midnight", CheckMidnight },
		{ "action.inventory", CheckActionInventory },
	};
}

[[nodiscard]] std::vector<std::pair<QString, QString>> RunChecks(
		const Outputs &outputs,
		const std::vector<ActionRow> &rows) {
	auto result = std::vector<std::pair<QString, QString>>();
	for (const auto &check : Checks()) {
		result.emplace_back(
			QString::fromLatin1(check.name),
			check.run(outputs));
	}
	for (const auto &row : rows) {
		const auto parsed = (Execution(row) == ActionExecution::Parsed);
		result.emplace_back(
			(parsed ? u"action."_q : u"writer."_q)
				+ QString::fromLatin1(row.name),
			CheckActionRow(outputs, row));
	}
	auto rich = RichChecks(outputs);
	result.insert(end(result), begin(rich), end(rich));
	return result;
}

} // namespace

MTPMessage TextMessage(const TextFixture &data) {
	using Flag = MTPDmessage::Flag;
	const auto flags = (data.fromId ? Flag::f_from_id : Flag())
		| (data.replyToId ? Flag::f_reply_to : Flag())
		| (data.forward ? Flag::f_fwd_from : Flag())
		| (data.media ? Flag::f_media : Flag())
		| (data.entities.isEmpty() ? Flag() : Flag::f_entities)
		| (data.signature ? Flag::f_post_author : Flag())
		| (data.rich ? Flag::f_rich_message : Flag());
	return MTP_message(
		MTP_flags(flags),
		MTP_int(data.id),
		data.fromId ? MTPPeer(peerToMTP(data.fromId)) : MTPPeer(),
		MTPint(),
		MTPstring(),
		peerToMTP(data.peerId),
		MTPPeer(),
		data.forward.value_or(MTPMessageFwdHeader()),
		MTPlong(),
		MTPlong(),
		MTPPeer(),
		(data.replyToId
			? ReplyHeader(data.replyToId, data.replyToPeerId)
			: MTPMessageReplyHeader()),
		MTP_int(data.date),
		MTP_string(data.text),
		data.media.value_or(MTPMessageMedia()),
		MTPReplyMarkup(),
		MTP_vector<MTPMessageEntity>(data.entities),
		MTPint(),
		MTPint(),
		MTPMessageReplies(),
		MTPint(),
		data.signature ? MTP_string(data.signature) : MTPstring(),
		MTPlong(),
		MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>(),
		MTPint(),
		MTPint(),
		MTPlong(),
		MTPFactCheck(),
		MTPint(),
		MTPlong(),
		MTPSuggestedPost(),
		MTPint(),
		MTPstring(),
		data.rich.value_or(MTPRichMessage()));
}

MTPDocument DocumentFixture(
		BareId id,
		const char *name,
		const char *mime,
		int64 size) {
	return MTP_document(
		MTP_flags(0),
		MTP_long(id),
		MTP_long(0),
		MTP_bytes(),
		MTP_int(kMessageDate),
		MTP_string(mime),
		MTP_long(size),
		MTPVector<MTPPhotoSize>(),
		MTPVector<MTPVideoSize>(),
		MTP_int(2),
		MTP_vector<MTPDocumentAttribute>(
			1,
			MTP_documentAttributeFilename(MTP_string(name))));
}

MTPMessageReplyHeader ReplyHeader(int32 id, PeerId peerId) {
	using Flag = MTPDmessageReplyHeader::Flag;
	return MTP_messageReplyHeader(
		MTP_flags(Flag::f_reply_to_msg_id
			| (peerId ? Flag::f_reply_to_peer_id : Flag())),
		MTP_int(id),
		peerId ? MTPPeer(peerToMTP(peerId)) : MTPPeer(),
		MTPMessageFwdHeader(),
		MTPMessageMedia(),
		MTPint(),
		MTPstring(),
		MTPVector<MTPMessageEntity>(),
		MTPint(),
		MTPint(),
		MTPbytes(),
		MTPVector<MTPRichText>());
}

const QByteArray &OutputFile(
		const Outputs &outputs,
		const char *path) {
	static const auto empty = QByteArray();
	const auto i = outputs.files.find(QString::fromLatin1(path));
	return (i != end(outputs.files)) ? i->second : empty;
}

QByteArray PageRecord(
		const Outputs &outputs,
		const char *page,
		int32 id) {
	const auto &records = PageRecords(outputs, page);
	const auto i = ranges::find(records, id, &HtmlRecord::id);
	return (i != end(records) && PageRecordCount(outputs, page, id) == 1)
		? i->html
		: QByteArray();
}

QJsonObject JsonMessage(
		const Outputs &outputs,
		int dialog,
		int32 id) {
	if (dialog >= int(outputs.json.size())) {
		return QJsonObject();
	}
	const auto &messages = outputs.json[dialog].messages;
	const auto i = messages.find(id);
	return (i != end(messages)) ? i->second : QJsonObject();
}

QJsonObject Json(const char *literal) {
	return QJsonDocument::fromJson(literal).object();
}

QJsonObject Merged(
		QJsonObject common,
		const QJsonObject &fields) {
	for (auto i = fields.begin(); i != fields.end(); ++i) {
		common.insert(i.key(), i.value());
	}
	return common;
}

const Message *ParsedMessage(
		const Outputs &outputs,
		int dialog,
		int32 id) {
	if (dialog >= int(outputs.parsed.size())) {
		return nullptr;
	}
	const auto &list = outputs.parsed[dialog];
	const auto i = ranges::find(list, id, &Message::id);
	return (i != end(list) && ranges::count(list, id, &Message::id) == 1)
		? &*i
		: nullptr;
}

QString ExpectObject(
		const QJsonObject &got,
		const QJsonObject &expected) {
	return (got == expected)
		? QString()
		: u"JSON got %1, expected %2"_q.arg(Compact(got), Compact(expected));
}

QString ExpectNumber(
		const QString &label,
		qint64 got,
		qint64 expected) {
	return (got == expected)
		? QString()
		: u"%1 is %2, expected %3"_q.arg(label).arg(got).arg(expected);
}

QString ExpectInOrder(
		const QString &label,
		const QByteArray &html,
		std::initializer_list<QByteArray> fragments) {
	auto from = qsizetype(0);
	for (const auto &fragment : fragments) {
		const auto at = html.indexOf(fragment, from);
		if (at < 0) {
			return u"%1 lacks '%2' in order within '%3'"_q.arg(
				label,
				QString::fromUtf8(fragment),
				Excerpt(html.mid(from)));
		}
		from = at + fragment.size();
	}
	return QString();
}

QString ExpectAbsent(
		const QString &label,
		const QByteArray &html,
		std::initializer_list<QByteArray> fragments) {
	for (const auto &fragment : fragments) {
		if (html.contains(fragment)) {
			return u"%1 unexpectedly has '%2'"_q.arg(
				label,
				QString::fromUtf8(fragment));
		}
	}
	return QString();
}

QString ExpectServiceHtml(
		const Outputs &outputs,
		int32 id,
		const QByteArray &body) {
	const auto record = PageRecord(outputs, kGroupPage, id);
	const auto start = "<div class=\"message service\" id=\"message"
		+ QByteArray::number(id)
		+ "\">";
	return (record.startsWith(start) && record.contains(">\n" + body + '\n'))
		? QString()
		: u"HTML record %1 is '%2', expected service body '%3'"_q.arg(
			QString::number(id),
			QString::fromUtf8(record),
			QString::fromUtf8(body));
}

QByteArray RecordStart(int32 id, bool joined) {
	return "<div class=\"message default clearfix"
		+ QByteArray(joined ? " joined" : "")
		+ "\" id=\"message"
		+ QByteArray::number(id)
		+ "\">";
}

QString ExpectGroupHtml(
		const Outputs &outputs,
		int32 id,
		std::initializer_list<QByteArray> fragments) {
	return ExpectInOrder(
		u"HTML %1"_q.arg(id),
		PageRecord(outputs, kGroupPage, id),
		fragments);
}

QString FirstFailure(std::initializer_list<QString> results) {
	for (const auto &result : results) {
		if (!result.isEmpty()) {
			return result;
		}
	}
	return QString();
}

std::vector<QByteArray> RawJsonMembers(
		const Outputs &outputs,
		int32 id,
		const char *key) {
	const auto &json = OutputFile(outputs, "result.json");
	const auto marker = "\"id\": " + QByteArray::number(id) + ",\n";
	const auto at = json.indexOf(marker);
	if (at < 0 || json.indexOf(marker, at + 1) >= 0) {
		return {};
	}
	const auto open = json.lastIndexOf('{', at);
	if (open < 0 || !json.mid(open + 1, at - open - 1).trimmed().isEmpty()) {
		return {};
	}
	const auto prefix = '"' + QByteArray(key) + "\":";
	auto result = std::vector<QByteArray>();
	auto from = open + 1;
	const auto member = [&](qsizetype till) {
		const auto raw = json.mid(from, till - from).trimmed();
		if (raw.startsWith(prefix)) {
			result.push_back(raw.mid(prefix.size()).trimmed());
		}
		from = till + 1;
	};
	auto depth = 0;
	auto string = false;
	for (auto i = open; i < json.size(); ++i) {
		const auto ch = json[i];
		if (string) {
			if (ch == '\\') {
				++i;
			} else if (ch == '"') {
				string = false;
			}
		} else if (ch == '"') {
			string = true;
		} else if (ch == '{' || ch == '[') {
			++depth;
		} else if (ch == '}' || ch == ']') {
			if (!--depth) {
				member(i);
				return result;
			}
		} else if (ch == ',' && depth == 1) {
			member(i);
		}
	}
	return {};
}

QString ExpectRawValues(
		const QString &label,
		const std::vector<QByteArray> &got,
		std::initializer_list<QByteArray> expected) {
	const auto joined = [](const auto &values) {
		return QString::fromUtf8(
			QByteArrayList(begin(values), end(values)).join(", "));
	};
	return ranges::equal(got, expected)
		? QString()
		: u"%1 are [%2], expected [%3]"_q.arg(
			label,
			joined(got),
			joined(expected));
}

} // namespace Export::Tests

int main() {
	qputenv("TZ", "TST-03");
	QLocale::setDefault(QLocale::c());

	const auto outputs = Export::Tests::RunExport();
	const auto rows = Export::Tests::ActionRows();
	const auto results = Export::Tests::RunChecks(outputs, rows);
	auto failed = 0;
	for (const auto &[name, failure] : results) {
		if (failure.isEmpty()) {
			std::printf("PASS %s\n", name.toUtf8().constData());
		} else {
			std::printf(
				"FAIL %s: %s\n",
				name.toUtf8().constData(),
				failure.toUtf8().constData());
			++failed;
		}
	}
	const auto total = int(results.size());
	std::printf("%d of %d checks passed.\n", total - failed, total);
	return failed ? 1 : 0;
}
