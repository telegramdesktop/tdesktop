/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/data/export_data_types.h"

#include <QtCore/QJsonObject>
#include <QtCore/QStringList>

#include <initializer_list>
#include <optional>

namespace Export::Tests {

constexpr auto kSelfId = BareId(1000);
constexpr auto kAliceId = BareId(1001);
constexpr auto kBobId = BareId(1002);
constexpr auto kMissingId = BareId(1099);
constexpr auto kGroupId = BareId(2001);
constexpr auto kChannelId = BareId(3001);
constexpr auto kGroupDialog = 0;
constexpr auto kChannelDialog = 1;
constexpr auto kGroupPage = "chats/chat_1/messages.html";
constexpr auto kAliceFromName = "<div class=\"from_name\">\nAlice Example\n";
constexpr auto kUnsupportedNotice = "This message is not supported by this "
	"version of Telegram Desktop. Please update the application.";

struct SliceFixture {
	std::vector<MTPMessage> wire;
	std::vector<Data::Message (*)()> direct;
	std::map<int32, Data::File::SkipReason> skips;
	std::map<uint64, Data::File::SkipReason> richSkips;
};

struct TextFixture {
	int32 id = 0;
	TimeId date = 0;
	PeerId fromId = 0;
	PeerId peerId = 0;
	int32 replyToId = 0;
	PeerId replyToPeerId = 0;
	const char *text = nullptr;
	std::optional<MTPMessageFwdHeader> forward;
	std::optional<MTPMessageMedia> media;
	QVector<MTPMessageEntity> entities;
	const char *signature = nullptr;
	std::optional<MTPRichMessage> rich;
};

struct HtmlRecord {
	int32 id = 0;
	QByteArray html;
};

struct JsonDialog {
	QJsonObject header;
	std::vector<int32> order;
	std::map<int32, QJsonObject> messages;
};

struct Outputs {
	QString failure;
	QString parserOrder;
	std::vector<std::vector<Data::Message>> parsed;
	std::vector<std::vector<int32>> fixtureIds;
	std::vector<JsonDialog> json;
	std::map<QString, QByteArray> files;
	std::map<QString, QStringList> folders;
	std::map<QString, std::vector<HtmlRecord>> pages;
};

enum class ActionOutput {
	AliceService,
	Service,
	AliceCall,
	Fallback,
};

enum class ActionExecution {
	Parsed,
	WriterOnly,
};

using ConstructorId = decltype(mtpc_messageActionEmpty);

struct ActionRow {
	int32 id = 0;
	const char *name = nullptr;
	ConstructorId type = {};
	MTPmessageAction (*action)() = nullptr;
	Data::Message (*direct)() = nullptr;
	std::size_t alternative = 0;
	ActionOutput output = ActionOutput::AliceService;
	const char *json = nullptr;
	const char *html = nullptr;
	int32 replyToId = 0;
	BareId fromId = kAliceId;
	bool out = false;
	Data::File::SkipReason skip = Data::File::SkipReason::None;
	QString (*extra)(
		const Outputs &outputs,
		const Data::Message &parsed) = nullptr;
};

[[nodiscard]] std::vector<ActionRow> ActionRows();
[[nodiscard]] ActionExecution Execution(const ActionRow &row);
[[nodiscard]] SliceFixture ActionSlice();
[[nodiscard]] QString CheckActionRow(
	const Outputs &outputs,
	const ActionRow &row);
[[nodiscard]] QString CheckActionInventory(const Outputs &outputs);
[[nodiscard]] SliceFixture RichSlice();
[[nodiscard]] std::vector<std::pair<QString, QString>> RichChecks(
	const Outputs &outputs);
[[nodiscard]] MTPPhoto PhotoFixture(
	BareId id,
	int width,
	int height,
	int size);
[[nodiscard]] MTPDocument DocumentFixture(
	BareId id,
	const char *name,
	const char *mime,
	int64 size);
[[nodiscard]] Data::Message AliceGroupMessage(int32 id, TimeId date);

[[nodiscard]] MTPMessage TextMessage(const TextFixture &data);
[[nodiscard]] MTPMessageReplyHeader ReplyHeader(
	int32 id,
	PeerId peerId = 0);
[[nodiscard]] const QByteArray &OutputFile(
	const Outputs &outputs,
	const char *path);
[[nodiscard]] QByteArray PageRecord(
	const Outputs &outputs,
	const char *page,
	int32 id);
[[nodiscard]] QJsonObject JsonMessage(
	const Outputs &outputs,
	int dialog,
	int32 id);
[[nodiscard]] QJsonObject Json(const char *literal);
[[nodiscard]] QJsonObject Merged(
	QJsonObject common,
	const QJsonObject &fields);
[[nodiscard]] const Data::Message *ParsedMessage(
	const Outputs &outputs,
	int dialog,
	int32 id);
[[nodiscard]] QString ExpectObject(
	const QJsonObject &got,
	const QJsonObject &expected);
[[nodiscard]] QString ExpectNumber(
	const QString &label,
	qint64 got,
	qint64 expected);
[[nodiscard]] QString ExpectInOrder(
	const QString &label,
	const QByteArray &html,
	std::initializer_list<QByteArray> fragments);
[[nodiscard]] QString ExpectAbsent(
	const QString &label,
	const QByteArray &html,
	std::initializer_list<QByteArray> fragments);
[[nodiscard]] QString ExpectServiceHtml(
	const Outputs &outputs,
	int32 id,
	const QByteArray &body);
[[nodiscard]] QByteArray RecordStart(int32 id, bool joined = false);
[[nodiscard]] QString ExpectGroupHtml(
	const Outputs &outputs,
	int32 id,
	std::initializer_list<QByteArray> fragments);
[[nodiscard]] QString FirstFailure(std::initializer_list<QString> results);
[[nodiscard]] std::vector<QByteArray> RawJsonMembers(
	const Outputs &outputs,
	int32 id,
	const char *key);
[[nodiscard]] QString ExpectRawValues(
	const QString &label,
	const std::vector<QByteArray> &got,
	std::initializer_list<QByteArray> expected);

} // namespace Export::Tests
