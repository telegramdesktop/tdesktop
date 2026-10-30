/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_conformance_context.h"

#include "data/data_peer.h"
#include "test/test_widget_bot.h"
#include "test/test_widget_conformance_state.h"
#include "tgw/tgw_media.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonValue>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string>

namespace Test::Conformance {
namespace {

// The chat a run's message context needs: what WIDGET_MESSAGE_KINDS says
// each kind is (an original in the bot's private chat, one in a group,
// a forwarded copy anywhere).
enum class ChatNeed {
	User,
	Group,
	Any,
};

// What a chat is: at setup only the PeerId's category is known, where a
// channel may be a megagroup or a broadcast; the loaded peer tells them
// apart.
enum class ChatCategory {
	User,
	BasicGroup,
	Channel,
	Megagroup,
	Broadcast,
};

struct MessageKind {
	const char *name = nullptr;
	Tgw::WidgetFlags flags;
	ChatNeed need = ChatNeed::Any;
};

// WIDGET_MESSAGE_KINDS of widget-sandbox's sdk-policy.ts, the server's
// flags of each kind, as the client reads them from the message; the
// preview's own flags need a preview host, so it runs as private.
constexpr auto kKinds = std::array{
	MessageKind{
		.name = "private",
		.flags = { .canQuery = true, .backgroundQuery = true },
		.need = ChatNeed::User,
	},
	MessageKind{
		.name = "group",
		.flags = { .canQuery = true },
		.need = ChatNeed::Group,
	},
	MessageKind{
		.name = "no-queries",
		.need = ChatNeed::User,
	},
	MessageKind{
		.name = "copy",
		.flags = { .isCopy = true },
		.need = ChatNeed::Any,
	},
	MessageKind{
		.name = "live-copy",
		.flags = { .isCopy = true, .canQuery = true },
		.need = ChatNeed::Any,
	},
	MessageKind{
		.name = "preview",
		.flags = { .canQuery = true, .backgroundQuery = true },
		.need = ChatNeed::User,
	},
};

constexpr auto kPrivate = "private";
constexpr auto kPreview = "preview";

[[nodiscard]] const MessageKind *FindKind(const QString &name) {
	for (const auto &kind : kKinds) {
		if (name == QString::fromLatin1(kind.name)) {
			return &kind;
		}
	}
	return nullptr;
}

[[nodiscard]] const MessageKind &PrivateKind() {
	return *FindKind(QString::fromLatin1(kPrivate));
}

[[nodiscard]] QString KindNames() {
	auto result = QStringList();
	for (const auto &kind : kKinds) {
		result.push_back(QString::fromLatin1(kind.name));
	}
	return result.join(u", "_q);
}

[[nodiscard]] QString NeedText(ChatNeed need) {
	switch (need) {
	case ChatNeed::User: return u"a user's private chat"_q;
	case ChatNeed::Group: return u"a basic group or a megagroup"_q;
	case ChatNeed::Any: return u"any chat"_q;
	}
	return QString();
}

[[nodiscard]] QString CategoryText(ChatCategory category) {
	switch (category) {
	case ChatCategory::User: return u"a user's private chat"_q;
	case ChatCategory::BasicGroup: return u"a basic group"_q;
	case ChatCategory::Channel:
		return u"a channel (a megagroup or a broadcast)"_q;
	case ChatCategory::Megagroup: return u"a megagroup"_q;
	case ChatCategory::Broadcast: return u"a broadcast channel"_q;
	}
	return QString();
}

[[nodiscard]] ChatCategory CategoryOf(PeerId chat) {
	return peerIsUser(chat)
		? ChatCategory::User
		: peerIsChat(chat)
		? ChatCategory::BasicGroup
		: ChatCategory::Channel;
}

[[nodiscard]] ChatCategory CategoryOf(not_null<PeerData*> peer) {
	return peer->isUser()
		? ChatCategory::User
		: peer->isChat()
		? ChatCategory::BasicGroup
		: peer->isMegagroup()
		? ChatCategory::Megagroup
		: ChatCategory::Broadcast;
}

// A channel by its id is only a candidate for a group run: the loaded
// peer must then be a megagroup.
[[nodiscard]] bool Fits(ChatNeed need, ChatCategory category) {
	switch (need) {
	case ChatNeed::User: return (category == ChatCategory::User);
	case ChatNeed::Group:
		return (category == ChatCategory::BasicGroup)
			|| (category == ChatCategory::Channel)
			|| (category == ChatCategory::Megagroup);
	case ChatNeed::Any: return true;
	}
	return false;
}

[[nodiscard]] QString KindText(const ManifestMessage &message) {
	return message.implicit
		? u"no message: the private chat's"_q
		: (message.kind == QString::fromLatin1(kPreview))
		? u"preview, run as private"_q
		: message.kind;
}

// Why a chat of |category| cannot host |test|'s run, or empty; empty too
// when the entry's kind was not read, which its malformed entry reports.
[[nodiscard]] QString Unfit(const ManifestTest &test, ChatCategory category) {
	const auto kind = FindKind(test.message.kind);
	if (!kind || Fits(kind->need, category)) {
		return QString();
	}
	return u"its message context (%1) needs %2, and the chat is %3"_q.arg(
		KindText(test.message),
		NeedText(kind->need),
		CategoryText(category));
}

[[nodiscard]] QJsonValue OptionalText(
		const std::optional<std::string> &value) {
	return value
		? QJsonValue(QString::fromStdString(*value))
		: QJsonValue(QJsonValue::Null);
}

[[nodiscard]] QJsonValue StateValue(const std::optional<QByteArray> &value) {
	return value
		? QJsonValue(QString::fromUtf8(*value))
		: QJsonValue(QJsonValue::Null);
}

[[nodiscard]] QJsonValue ChatTypeValue(Tgw::ChatType type) {
	switch (type) {
	case Tgw::ChatType::None: return QJsonValue(QJsonValue::Null);
	case Tgw::ChatType::Private: return u"private"_q;
	case Tgw::ChatType::Group: return u"group"_q;
	case Tgw::ChatType::Supergroup: return u"supergroup"_q;
	case Tgw::ChatType::Channel: return u"channel"_q;
	}
	return QJsonValue(QJsonValue::Null);
}

[[nodiscard]] QJsonObject FlagsObject(Tgw::WidgetFlags flags) {
	return QJsonObject{
		{ u"isCopy"_q, flags.isCopy },
		{ u"canQuery"_q, flags.canQuery },
		{ u"backgroundQuery"_q, flags.backgroundQuery },
	};
}

// |derived| is the run's reading, absent until it was made.
[[nodiscard]] QJsonObject MessageObject(
		const ManifestTest &test,
		const std::optional<Tgw::MessageContext> &derived) {
	const auto &message = test.message;
	const auto &fields = message.fields;
	auto result = QJsonObject{
		{ u"requestedKind"_q, message.kind },
		{ u"kind"_q, EffectiveKind(message) },
		{ u"implicit"_q, message.implicit },
		{ u"specs"_q, message.specs },
		{ u"flags"_q, FlagsObject(fields.flags) },
		{ u"state"_q, QJsonObject{
			{ u"chatData"_q, StateValue(fields.state.chatData) },
			{ u"publicData"_q, StateValue(fields.state.publicData) },
		} },
		{ u"widgetId"_q, fields.widgetId
			? QJsonValue(QString::number(*fields.widgetId))
			: QJsonValue(QJsonValue::Null) },
	};
	if (message.kind == QString::fromLatin1(kPreview)) {
		result.insert(u"previewLeg"_q, u"untested: the desktop builds no "
			u"host with context.preview; loaded as a private message"_q);
	}
	if (derived) {
		auto object = FlagsObject(derived->flags);
		object.insert(u"chatType"_q, ChatTypeValue(derived->chatType));
		result.insert(u"derived"_q, object);
	}
	return result;
}

[[nodiscard]] QJsonObject TableObject(const ManifestTest &test) {
	auto result = QJsonObject();
	for (const auto &[data, answer] : test.bot) {
		result.insert(
			QString::fromStdString(data),
			answer
				? QJsonObject{
					{ u"result"_q, QString::fromStdString(*answer) },
				}
				: QJsonObject());
	}
	return result;
}

[[nodiscard]] QJsonArray UnplayedArray(const ManifestTest &test) {
	auto result = QJsonArray();
	for (const auto &unplayed : test.botUnplayed) {
		result.push_back(QJsonObject{
			{ u"data"_q, unplayed.data },
			{ u"operation"_q, unplayed.operation },
		});
	}
	return result;
}

[[nodiscard]] QJsonArray RequestsArray(const WidgetBotRecord &record) {
	auto result = QJsonArray();
	for (const auto &request : ReadWidgetBot(record)) {
		result.push_back(QJsonObject{
			{ u"data"_q, QString::fromStdString(request.data) },
			{ u"listed"_q, request.listed },
			{ u"n"_q, request.n },
			{ u"result"_q, OptionalText(request.result) },
			{ u"sinceArmedMs"_q, qint64(request.ms) },
		});
	}
	return result;
}

// The run's `bot` extra: `armed`, `played` (whether any request was
// answered), `scope` "message-lifetime", `hosts` (the run's tap reading's
// selected host factories), `table`, `unplayed` and every played request of
// the message, of all its hosts, in order. Nullopt for a run no bot was
// armed for: one never injected.
[[nodiscard]] std::optional<QJsonObject> BotObject(
		const ManifestTest *test,
		const ConformanceRun &run) {
	if (!run.bot) {
		return std::nullopt;
	}
	const auto requests = RequestsArray(*run.bot);
	return QJsonObject{
		{ u"armed"_q, true },
		{ u"played"_q, !requests.isEmpty() },
		{ u"scope"_q, u"message-lifetime"_q },
		{ u"hosts"_q, run.reading.factoriesSelected },
		{ u"table"_q, test ? TableObject(*test) : QJsonObject() },
		{ u"unplayed"_q, test ? UnplayedArray(*test) : QJsonArray() },
		{ u"requests"_q, requests },
	};
}

[[nodiscard]] WidgetOpsSelfCheckRow CheckRow(
		bool passed,
		const QString &name,
		const QString &details) {
	return {
		.name = u"bot table self-check: "_q + name,
		.details = details,
		.passed = passed,
	};
}

[[nodiscard]] WidgetOpsSelfCheckRow ContextRow(
		bool passed,
		const QString &name,
		const QString &details) {
	return {
		.name = u"message context self-check: "_q + name,
		.details = details,
		.passed = passed,
	};
}

[[nodiscard]] QJsonObject Parse(const char *json, QStringList &problems) {
	const auto entry = QJsonDocument::fromJson(QByteArray(json)).object();
	auto test = ManifestTest();
	ParseManifestBot(entry, test, problems);
	return QJsonObject{
		{ u"table"_q, TableObject(test) },
		{ u"unplayed"_q, UnplayedArray(test) },
	};
}

[[nodiscard]] QString ProblemsOf(const char *json) {
	auto problems = QStringList();
	const auto read = Parse(json, problems);
	return read.value(u"table"_q).toObject().isEmpty()
		? problems.join(u"; "_q)
		: u"read a table: "_q + CompactJson(read);
}

[[nodiscard]] ManifestTest ParseMessage(
		const char *json,
		QStringList &problems) {
	const auto entry = QJsonDocument::fromJson(QByteArray(json)).object();
	auto result = ManifestTest{ .id = u"auto/x"_q, .set = u"auto"_q };
	ParseManifestMessage(entry, result, problems);
	return result;
}

// The parsed message as its run file extra shows it, nothing derived.
[[nodiscard]] QJsonObject ReadMessage(const ManifestTest &test) {
	return MessageObject(test, std::nullopt);
}

// The pinned kinds take WIDGET_MESSAGE_KINDS' flags whatever the spec's
// allow_queries says, keep their state and count their specs; a preview
// runs as private with its state.
[[nodiscard]] WidgetOpsSelfCheckRow CheckKinds() {
	struct Expected {
		const char *json = nullptr;
		const char *requested = nullptr;
		const char *effective = nullptr;
		Tgw::WidgetFlags flags;
		int specs = 0;
	};
	const auto cases = std::array{
		Expected{
			R"({"message":[{"kind":"private","allow_queries":true,)"
				R"("chat_data":"c","public_data":"p"}]})",
			"private",
			"private",
			{ .canQuery = true, .backgroundQuery = true },
			1,
		},
		Expected{
			R"({"message":[{"kind":"group","allow_queries":true,)"
				R"("chat":"group","chat_data":"c","public_data":"p"}]})",
			"group",
			"group",
			{ .canQuery = true },
			1,
		},
		Expected{
			R"({"message":[{"kind":"no-queries","allow_queries":true,)"
				R"("chat_data":"c","public_data":"p"}]})",
			"no-queries",
			"no-queries",
			{},
			1,
		},
		Expected{
			R"({"message":[{"kind":"live-copy","allow_queries":true,)"
				R"("fwd_allow_queries":true,"chat_data":"c",)"
				R"("public_data":"p","forward":true},)"
				R"({"kind":"copy","chat_data":"c","public_data":"q"}]})",
			"live-copy",
			"live-copy",
			{ .isCopy = true, .canQuery = true },
			2,
		},
		Expected{
			R"({"message":[{"kind":"preview","allow_queries":true,)"
				R"("inline":true,"chat_data":"c","public_data":"p"}]})",
			"preview",
			"private",
			{ .canQuery = true, .backgroundQuery = true },
			1,
		},
	};
	auto passed = true;
	auto read = QJsonArray();
	for (const auto &expected : cases) {
		auto problems = QStringList();
		const auto test = ParseMessage(expected.json, problems);
		const auto &message = test.message;
		passed = passed
			&& problems.isEmpty()
			&& (message.kind == QString::fromLatin1(expected.requested))
			&& (EffectiveKind(message)
				== QString::fromLatin1(expected.effective))
			&& !message.implicit
			&& (message.specs == expected.specs)
			&& (message.fields.flags == expected.flags)
			&& (message.fields.state.chatData == QByteArray("c"))
			&& (message.fields.state.publicData == QByteArray("p"))
			&& !message.fields.widgetId;
		auto entry = ReadMessage(test);
		entry.insert(u"problems"_q, problems.join(u"; "_q));
		read.push_back(entry);
	}
	return ContextRow(
		passed,
		u"private, group, no-queries, live-copy and preview take "
			u"WIDGET_MESSAGE_KINDS' flags F/T/T, F/T/F, F/F/F, T/T/F and the "
			u"private F/T/T whatever allow_queries says, with the first "
			u"spec's state and no widget id; only the first of two specs is "
			u"run, and a preview runs as private"_q,
		CompactJson(QJsonObject{ { u"read"_q, read } }));
}

// No message is the private chat with no state; a copy-shaped spec is a
// copy that keeps the state it was sent with; an empty chat_data is
// present and empty, a missing public_data unset.
[[nodiscard]] WidgetOpsSelfCheckRow CheckDefaults() {
	auto problems = QStringList();
	const auto none = ParseMessage(R"({"id":"auto/x"})", problems);
	const auto copy = ParseMessage(
		R"({"message":[{"kind":"copy","allow_queries":true,)"
			R"("chat_data":"conf-chat","public_data":"conf-public",)"
			R"("forward":true}]})",
		problems);
	const auto empty = ParseMessage(
		R"({"message":[{"kind":"private","chat_data":""}]})",
		problems);
	const auto &implicit = none.message;
	const auto implicitPassed = (implicit.kind == u"private"_q)
		&& implicit.implicit
		&& (implicit.specs == 0)
		&& (implicit.fields.flags
			== Tgw::WidgetFlags{ .canQuery = true, .backgroundQuery = true })
		&& !implicit.fields.state.chatData
		&& !implicit.fields.state.publicData
		&& !implicit.fields.widgetId;
	const auto &copied = copy.message;
	const auto copyPassed = (copied.kind == u"copy"_q)
		&& !copied.implicit
		&& (copied.fields.flags == Tgw::WidgetFlags{ .isCopy = true })
		&& (copied.fields.state.chatData == QByteArray("conf-chat"))
		&& (copied.fields.state.publicData == QByteArray("conf-public"))
		&& !copied.fields.widgetId;
	const auto &state = empty.message.fields.state;
	const auto emptyPassed = state.chatData
		&& state.chatData->isEmpty()
		&& !state.publicData;
	return ContextRow(
		problems.isEmpty() && implicitPassed && copyPassed && emptyPassed,
		u"no message is the private chat's context (F/T/T) with no state; a "
			u"copy-shaped spec is a copy (T/F/F) keeping its chat_data and "
			u"public_data; an empty chat_data is present and empty, a "
			u"missing public_data unset"_q,
		CompactJson(QJsonObject{
			{ u"none"_q, ReadMessage(none) },
			{ u"copy"_q, ReadMessage(copy) },
			{ u"empty"_q, ReadMessage(empty) },
			{ u"problems"_q, problems.join(u"; "_q) },
		}));
}

// Each malformed message shape is a problem by name and leaves no kind.
[[nodiscard]] WidgetOpsSelfCheckRow CheckMalformed() {
	const auto shape = u"message is not a non-empty array whose first item "
		u"is an object"_q;
	const auto unknown = [](const QString &kind) {
		return u"message's first kind [%1] is none of %2"_q.arg(
			kind,
			KindNames());
	};
	struct Case {
		const char *json = nullptr;
		QString problem;
		bool kindRead = false;
	};
	const auto cases = std::array{
		Case{ R"({"message":{"kind":"private"}})", shape },
		Case{ R"({"message":[]})", shape },
		Case{ R"({"message":["private"]})", shape },
		Case{ R"({"message":[{"kind":"via-bot"}]})", unknown(u"via-bot"_q) },
		Case{ R"({"message":[{"chat_data":"c"}]})", unknown(QString()) },
		Case{
			R"({"message":[{"kind":"private","chat_data":1}]})",
			u"message's first chat_data is not a string"_q,
			true,
		},
		Case{
			R"({"message":[{"kind":"group","public_data":null}]})",
			u"message's first public_data is not a string"_q,
			true,
		},
	};
	auto passed = true;
	auto read = QStringList();
	for (const auto &entry : cases) {
		auto problems = QStringList();
		const auto test = ParseMessage(entry.json, problems);
		passed = passed
			&& (problems == QStringList{ entry.problem })
			&& (test.message.kind.isEmpty() != entry.kindRead);
		read.push_back(u"[%1] kind=[%2]"_q.arg(
			problems.join(u"; "_q),
			test.message.kind));
	}
	return ContextRow(
		passed,
		u"a message that is no array, an empty one, a first spec that is no "
			u"object, via-bot, a missing kind and a state that is no string "
			u"are malformed by name"_q,
		read.join(u" | "_q));
}

// Each kind's chat: a private context only a user's chat, a group one a
// basic group, a megagroup or a channel candidate but never a broadcast,
// a copy any chat; an unread kind fits all, its entry being malformed.
[[nodiscard]] WidgetOpsSelfCheckRow CheckChats() {
	auto ignored = QStringList();
	const auto implicit = ParseMessage(R"({"id":"auto/x"})", ignored);
	const auto preview = ParseMessage(
		R"({"message":[{"kind":"preview"}]})",
		ignored);
	const auto noQueries = ParseMessage(
		R"({"message":[{"kind":"no-queries"}]})",
		ignored);
	const auto group = ParseMessage(
		R"({"message":[{"kind":"group"}]})",
		ignored);
	const auto copy = ParseMessage(
		R"({"message":[{"kind":"live-copy"}]})",
		ignored);
	const auto unread = ParseMessage(
		R"({"message":[{"kind":"via-bot"}]})",
		ignored);
	const auto all = std::array{
		ChatCategory::User,
		ChatCategory::BasicGroup,
		ChatCategory::Channel,
		ChatCategory::Megagroup,
		ChatCategory::Broadcast,
	};
	// One letter per category above: f fits, u does not.
	const auto pattern = [&](const ManifestTest &test) {
		auto result = QString();
		for (const auto category : all) {
			result.push_back(QChar(Unfit(test, category).isEmpty()
				? u'f'
				: u'u'));
		}
		return result;
	};
	const auto patterns = QJsonObject{
		{ u"implicit"_q, pattern(implicit) },
		{ u"preview"_q, pattern(preview) },
		{ u"noQueries"_q, pattern(noQueries) },
		{ u"group"_q, pattern(group) },
		{ u"liveCopy"_q, pattern(copy) },
		{ u"unread"_q, pattern(unread) },
	};
	const auto user = peerFromUser(UserId(BareId(1)));
	const auto chat = peerFromChat(ChatId(BareId(1)));
	const auto channel = peerFromChannel(ChannelId(BareId(1)));
	const auto byId = std::array{
		ChatPartition(implicit, user).isEmpty(),
		!ChatPartition(implicit, channel).isEmpty(),
		ChatPartition(group, channel).isEmpty(),
		ChatPartition(group, chat).isEmpty(),
		!ChatPartition(group, user).isEmpty(),
		ChatPartition(copy, chat).isEmpty(),
	};
	const auto broadcast = Unfit(group, ChatCategory::Broadcast);
	const auto passed = (patterns == QJsonObject{
			{ u"implicit"_q, u"fuuuu"_q },
			{ u"preview"_q, u"fuuuu"_q },
			{ u"noQueries"_q, u"fuuuu"_q },
			{ u"group"_q, u"ufffu"_q },
			{ u"liveCopy"_q, u"fffff"_q },
			{ u"unread"_q, u"fffff"_q },
		})
		&& std::all_of(begin(byId), end(byId), [](bool fits) { return fits; })
		&& (broadcast == u"its message context (group) needs a basic group "
			u"or a megagroup, and the chat is a broadcast channel"_q);
	return ContextRow(
		passed,
		u"no message, a preview and no-queries need a user's chat, a group "
			u"a basic group, a megagroup or a channel by its id but never a "
			u"broadcast, a live copy any chat, by the id's category at setup "
			u"and by the loaded peer"_q,
		CompactJson(QJsonObject{
			{ u"patterns"_q, patterns },
			{ u"broadcast"_q, broadcast },
			{ u"implicitInChannel"_q, ChatPartition(implicit, channel) },
		}));
}

// A table with a result, an entry without one and unplayed operations is
// read; each malformed shape is a problem by name; no `bot` is no table.
[[nodiscard]] WidgetOpsSelfCheckRow CheckParse() {
	auto problems = QStringList();
	const auto read = Parse(R"({"bot":{)"
		R"("conf:a":{"result":"ok {n}"},)"
		R"("conf:b":{},)"
		R"("conf:c":{"edit":{"chat_data":"{n}"},"result":"edited"},)"
		R"("conf:d":{"editShared":{"public_data":"{n}"},)"
		R"("repeat":{"every":10,"times":2}}}})",
		problems);
	const auto expected = QJsonObject{
		{ u"table"_q, QJsonObject{
			{ u"conf:a"_q, QJsonObject{ { u"result"_q, u"ok {n}"_q } } },
			{ u"conf:b"_q, QJsonObject() },
			{ u"conf:c"_q, QJsonObject{ { u"result"_q, u"edited"_q } } },
			{ u"conf:d"_q, QJsonObject() },
		} },
		{ u"unplayed"_q, QJsonArray{
			QJsonObject{
				{ u"data"_q, u"conf:c"_q },
				{ u"operation"_q, u"edit"_q },
			},
			QJsonObject{
				{ u"data"_q, u"conf:d"_q },
				{ u"operation"_q, u"editShared"_q },
			},
			QJsonObject{
				{ u"data"_q, u"conf:d"_q },
				{ u"operation"_q, u"repeat"_q },
			},
		} },
	};
	const auto clean = problems.isEmpty() && (read == expected);
	auto none = QStringList();
	const auto absent = Parse(R"({"id":"auto/x"})", none);
	const auto empty = none.isEmpty()
		&& absent.value(u"table"_q).toObject().isEmpty()
		&& absent.value(u"unplayed"_q).toArray().isEmpty();
	auto malformed = QStringList();
	for (const auto json : {
			R"({"bot":[]})",
			R"({"bot":{"conf:a":"ok"}})",
			R"({"bot":{"conf:a":{"result":1}}})" }) {
		malformed.push_back(ProblemsOf(json));
	}
	const auto named = (malformed.size() == 3)
		&& (malformed[0] == u"bot is not an object"_q)
		&& (malformed[1] == u"bot [conf:a] is not an object"_q)
		&& (malformed[2] == u"bot [conf:a]'s result is not a string"_q);
	return CheckRow(
		clean && empty && named,
		u"a table's results, an entry without result and the operations "
			u"the played bot does not perform are read; no bot is an empty "
			u"table; a bot, an entry or a result of the wrong type is "
			u"malformed by name"_q,
		CompactJson(QJsonObject{
			{ u"read"_q, read },
			{ u"problems"_q, problems.join(u"; "_q) },
			{ u"absent"_q, absent },
			{ u"malformed"_q, malformed.join(u" | "_q) },
		}));
}

// An injected run's extras hold every request of the message, across its
// hosts, in order, and the context the client derived; a run never
// injected has its requested context only, no derived one and no bot, so
// it is never labelled played.
[[nodiscard]] WidgetOpsSelfCheckRow CheckExtra() {
	const auto id = GlobalMsgId{ FullMsgId(PeerId(), MsgId(-101)), 1 };
	auto problems = QStringList();
	auto state = ConformanceState();
	state.tests.push_back(ParseMessage(
		R"({"message":[{"kind":"live-copy","chat_data":"c"}]})",
		problems));
	auto &test = state.tests.back();
	test.bot = { { "conf:a", "ok {n}" } };
	test.botUnplayed = { { .data = u"conf:a"_q, .operation = u"edit"_q } };
	auto injected = ConformanceRun{ .id = u"auto/x"_q };
	injected.test = 0;
	injected.reading.factoriesSelected = 2;
	injected.context = Tgw::MessageContext{
		.flags = { .isCopy = true, .canQuery = true },
		.chatType = Tgw::ChatType::None,
	};
	injected.bot = ArmWidgetBot(id, test.bot);
	const auto ignore = [](const twidget::WidgetRequestOutcome &) {};
	const auto first = PlayWidgetBot(id, "conf:a", ignore);
	const auto second = PlayWidgetBot(id, "conf:z", ignore);
	DisarmWidgetBot(id);
	auto never = ConformanceRun{ .id = u"auto/x"_q };
	never.test = 0;
	auto extras = QJsonObject();
	InsertContextExtras(extras, state, injected);
	auto missing = QJsonObject();
	InsertContextExtras(missing, state, never);
	const auto object = extras.value(u"bot"_q).toObject();
	const auto requests = object.value(u"requests"_q).toArray();
	const auto request = [&](int index) {
		return requests.at(index).toObject();
	};
	const auto message = extras.value(u"message"_q).toObject();
	const auto derived = message.value(u"derived"_q).toObject();
	const auto requested = missing.value(u"message"_q).toObject();
	const auto passed = problems.isEmpty()
		&& first
		&& second
		&& (object.value(u"armed"_q) == QJsonValue(true))
		&& (object.value(u"played"_q) == QJsonValue(true))
		&& (object.value(u"scope"_q).toString() == u"message-lifetime"_q)
		&& (object.value(u"hosts"_q).toInt() == 2)
		&& (object.value(u"table"_q).toObject()
			== QJsonObject{
				{ u"conf:a"_q, QJsonObject{ { u"result"_q, u"ok {n}"_q } } },
			})
		&& (object.value(u"unplayed"_q).toArray().size() == 1)
		&& (requests.size() == 2)
		&& (request(0).value(u"data"_q).toString() == u"conf:a"_q)
		&& (request(0).value(u"listed"_q) == QJsonValue(true))
		&& (request(0).value(u"n"_q).toInt() == 1)
		&& (request(0).value(u"result"_q).toString() == u"ok 1"_q)
		&& (request(1).value(u"data"_q).toString() == u"conf:z"_q)
		&& (request(1).value(u"listed"_q) == QJsonValue(false))
		&& request(1).value(u"result"_q).isNull()
		&& (message.value(u"kind"_q).toString() == u"live-copy"_q)
		&& (message.value(u"state"_q).toObject()
			== QJsonObject{
				{ u"chatData"_q, u"c"_q },
				{ u"publicData"_q, QJsonValue(QJsonValue::Null) },
			})
		&& (derived.value(u"isCopy"_q) == QJsonValue(true))
		&& (derived.value(u"backgroundQuery"_q) == QJsonValue(false))
		&& derived.contains(u"chatType"_q)
		&& derived.value(u"chatType"_q).isNull()
		&& !missing.contains(u"bot"_q)
		&& (requested.value(u"kind"_q).toString() == u"live-copy"_q)
		&& !requested.contains(u"derived"_q);
	return CheckRow(
		passed,
		u"an injected run's bot extra carries its table, the unplayed "
			u"operations and every request of the message in order, with "
			u"scope message-lifetime and the host count, beside its message "
			u"extra's derived context; a run never injected carries its "
			u"requested context only, no derived context and no bot"_q,
		CompactJson(QJsonObject{
			{ u"played"_q, QJsonArray{ first, second } },
			{ u"extras"_q, extras },
			{ u"neverInjected"_q, missing },
		}));
}

} // namespace

void ParseManifestMessage(
		const QJsonObject &entry,
		ManifestTest &test,
		QStringList &problems) {
	auto &message = test.message;
	const auto value = entry.value(u"message"_q);
	if (value.isUndefined()) {
		message = ManifestMessage{
			.kind = QString::fromLatin1(kPrivate),
			.fields = { .flags = PrivateKind().flags },
			.implicit = true,
		};
		return;
	}
	const auto specs = value.toArray();
	const auto first = specs.isEmpty() ? QJsonValue() : specs.first();
	if (!value.isArray() || !first.isObject()) {
		problems.push_back(u"message is not a non-empty array whose first "
			u"item is an object"_q);
		return;
	}
	const auto spec = first.toObject();
	const auto name = spec.value(u"kind"_q).toString();
	const auto kind = FindKind(name);
	if (!kind) {
		problems.push_back(u"message's first kind [%1] is none of %2"_q.arg(
			name,
			KindNames()));
		return;
	}
	message = ManifestMessage{
		.kind = name,
		.fields = { .flags = kind->flags },
		.specs = int(specs.size()),
	};
	const auto read = [&](const QString &key) {
		const auto field = spec.value(key);
		if (field.isUndefined()) {
			return std::optional<QByteArray>();
		} else if (!field.isString()) {
			problems.push_back(
				u"message's first %1 is not a string"_q.arg(key));
			return std::optional<QByteArray>();
		}
		return std::make_optional(field.toString().toUtf8());
	};
	message.fields.state = {
		.chatData = read(u"chat_data"_q),
		.publicData = read(u"public_data"_q),
	};
}

QString EffectiveKind(const ManifestMessage &message) {
	return (message.kind == QString::fromLatin1(kPreview))
		? QString::fromLatin1(kPrivate)
		: message.kind;
}

QString PreviewOmission(const ManifestTest &test) {
	if ((test.set != u"auto"_q)
		|| (test.message.kind != QString::fromLatin1(kPreview))) {
		return QString();
	}
	return u"its message is a preview (its first message spec's kind), an "
		u"inline result shown in the bot's list before it is sent. Not "
		u"implemented on the desktop: the owner deferred preview hosts "
		u"pending preview design mockups (2026-10-26), inline results are "
		u"listed as article rows and no host is built with context.preview, "
		u"nor does the runner build one; so neither tw.preview nor "
		u"behaviour.preview gets evidence from this test"_q;
}

QString ChatPartition(const ManifestTest &test, PeerId chat) {
	const auto reason = Unfit(test, CategoryOf(chat));
	return reason.isEmpty()
		? QString()
		: (reason + u", by its id: a launch on a fitting chat runs it"_q);
}

QString ChatRefusal(const ManifestTest &test, not_null<PeerData*> peer) {
	const auto reason = Unfit(test, CategoryOf(peer));
	return reason.isEmpty()
		? QString()
		: (reason + u": a wrong fixture for this run"_q);
}

void InsertContextExtras(
		QJsonObject &entry,
		const ConformanceState &state,
		const ConformanceRun &run) {
	const auto test = ((run.test >= 0) && (run.test < int(state.tests.size())))
		? &state.tests[run.test]
		: nullptr;
	if (test && !test->message.kind.isEmpty()) {
		entry.insert(u"message"_q, MessageObject(*test, run.context));
	}
	if (const auto bot = BotObject(test, run)) {
		entry.insert(u"bot"_q, *bot);
	}
}

QStringList ContextNotes(const ConformanceState &state) {
	auto result = QStringList();
	result.push_back(u"message context: each run's message is injected with "
		u"the flags of its manifest entry's first message spec's kind, as "
		u"WIDGET_MESSAGE_KINDS in sdk-policy.ts gives them (isCopy, "
		u"canQuery, backgroundQuery: private F/T/T, group F/T/F, no-queries "
		u"F/F/F, copy T/F/F, live-copy T/T/F), whatever the spec's "
		u"allow_queries, and with that spec's chat_data and public_data as "
		u"given, an absent one unset; never a widget_id, so nothing polls "
		u"it. An entry without message runs in the private chat's context "
		u"with no state, as CLIENTS.md says. Later specs of an entry are "
		u"not run (message.specs counts them). The client derives the chat "
		u"type itself (WidgetMedia::context() of the injected item, each "
		u"run's message.derived) and the viewer (the account's own user, "
		u"only where canQuery); the runner sets neither."_q);
	auto derived = std::map<QString, int>();
	for (const auto &run : state.runs) {
		if (run.context) {
			const auto type = ChatTypeValue(run.context->chatType);
			++derived[type.isNull() ? u"none"_q : type.toString()];
		}
	}
	auto types = QStringList();
	for (const auto &[type, count] : derived) {
		types.push_back(u"%1 %2"_q.arg(type, QString::number(count)));
	}
	result.push_back(u"chat: this launch's chat is %1 by its id. A run whose "
		u"message context needs a user's private chat (no message, private, "
		u"no-queries, a preview run as private) runs only in one, a group "
		u"run only in a basic group or a megagroup (a broadcast channel is "
		u"refused before its injection), a copy in any chat; any other run "
		u"is omitted from this launch, for a launch on a fitting chat. The "
		u"chat types WidgetMedia::context() derived for this launch's "
		u"injected runs (none for a copy): %2."_q.arg(
			CategoryText(CategoryOf(state.args.chat)),
			types.isEmpty() ? u"none read"_q : types.join(u", "_q)));
	for (auto i = 0; i != int(state.tests.size()); ++i) {
		const auto &test = state.tests[i];
		const auto ran = std::any_of(
			begin(state.runs),
			end(state.runs),
			[&](const ConformanceRun &run) { return (run.test == i); });
		if (!ran || (test.message.kind != QString::fromLatin1(kPreview))) {
			continue;
		}
		result.push_back(u"%1 asks for a preview, an inline result in the "
			u"bot's list before it is sent (requested kind preview): it "
			u"loads as a private message (effective kind private: the "
			u"private flags, its chat_data and public_data as given), as a "
			u"load check only. The desktop builds no host with "
			u"context.preview - inline results are listed as article rows, "
			u"and the owner deferred preview hosts pending preview design "
			u"mockups (2026-10-26) - so its preview leg is untested and no "
			u"check of a preview context is claimed from it."_q.arg(test.id));
	}
	result.push_back(u"bot: the answers are played. Every injected run's "
		u"message is armed with its entry's bot table (empty without one) "
		u"in the call that injects it, and Tgw::Runtime::botRequested() "
		u"hands every request that passed the framework's gate to the "
		u"runner, which answers a listed result with {n} counted per data "
		u"and message, {from} Conformance and {time} 12:00, and an entry "
		u"without result and data not in the table without result. Real "
		u"are the widget's own gate with the client's context, the host's "
		u"handler, the hop to the main thread and the renderer core's and "
		u"the runtime's routing. A run's bot extra holds every request of "
		u"its message over the message's lifetime, every host of it "
		u"included (scope message-lifetime). Not measured by this file: "
		u"Tgw::Bridge's messages.getBotWidgetQueryAnswer, its errors and "
		u"deadline, the server and the bot itself; the bot's edits of a "
		u"message's or a shared widget's state (edit, editShared, repeat, "
		u"named in each bot extra's unplayed); and shared-state polling, "
		u"which a message without a widget_id never gets."_q);
	return result;
}

std::vector<WidgetOpsSelfCheckRow> RunContextCheck() {
	return {
		CheckKinds(),
		CheckDefaults(),
		CheckMalformed(),
		CheckChats(),
		CheckParse(),
		CheckExtra(),
	};
}

void ParseManifestBot(
		const QJsonObject &entry,
		ManifestTest &test,
		QStringList &problems) {
	const auto value = entry.value(u"bot"_q);
	if (value.isUndefined()) {
		return;
	} else if (!value.isObject()) {
		problems.push_back(u"bot is not an object"_q);
		return;
	}
	const auto table = value.toObject();
	for (const auto &data : table.keys()) {
		const auto item = table.value(data);
		const auto action = item.toObject();
		const auto result = action.value(u"result"_q);
		if (!item.isObject()) {
			problems.push_back(u"bot [%1] is not an object"_q.arg(data));
			continue;
		} else if (!result.isUndefined() && !result.isString()) {
			problems.push_back(
				u"bot [%1]'s result is not a string"_q.arg(data));
			continue;
		}
		test.bot.emplace(
			data.toStdString(),
			result.isString()
				? std::make_optional(result.toString().toStdString())
				: std::nullopt);
		for (const auto &key : action.keys()) {
			if (key != u"result"_q) {
				test.botUnplayed.push_back({
					.data = data,
					.operation = key,
				});
			}
		}
	}
}

} // namespace Test::Conformance

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
