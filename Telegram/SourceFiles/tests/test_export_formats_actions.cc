/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tests/test_export_formats.hpp"

#include <QtCore/QJsonDocument>

#include <set>

namespace Export::Tests {

using namespace Export::Data;

namespace {

constexpr auto kActionDate = TimeId(1703980800);

[[nodiscard]] TimeId ActionDate(int32 id) {
	return kActionDate + id * 60;
}

using PurchaseOfferFlag = MTPDmessageActionStarGiftPurchaseOffer::Flag;
using OfferDeclinedFlag = MTPDmessageActionStarGiftPurchaseOfferDeclined::Flag;

[[nodiscard]] MTPMessage ServiceMessage(const ActionRow &row) {
	using Flag = MTPDmessageService::Flag;
	const auto flags = Flag::f_from_id
		| (row.replyToId ? Flag::f_reply_to : Flag())
		| (row.out ? Flag::f_out : Flag());
	return MTP_messageService(
		MTP_flags(flags),
		MTP_int(row.id),
		peerToMTP(peerFromUser(UserId(row.fromId))),
		peerToMTP(peerFromChat(ChatId(kGroupId))),
		MTPPeer(),
		row.replyToId ? ReplyHeader(row.replyToId) : MTPMessageReplyHeader(),
		MTP_int(ActionDate(row.id)),
		row.action(),
		MTPMessageReactions(),
		MTPint());
}

[[nodiscard]] MTPVector<MTPlong> UserIdList(
		std::initializer_list<BareId> ids) {
	auto result = QVector<MTPlong>();
	for (const auto id : ids) {
		result.push_back(MTP_long(id));
	}
	return MTP_vector<MTPlong>(std::move(result));
}

[[nodiscard]] MTPTextWithEntities WireText(const char *text) {
	return MTP_textWithEntities(
		MTP_string(text),
		MTPVector<MTPMessageEntity>());
}

[[nodiscard]] MTPStarGift StarGiftFixture(
		BareId id,
		int64 stars,
		bool limited) {
	using Flag = MTPDstarGift::Flag;
	return MTP_starGift(
		MTP_flags(limited ? Flag::f_limited : Flag()),
		MTP_long(id),
		MTP_documentEmpty(MTP_long(0)),
		MTP_long(stars),
		MTP_int(10),
		MTP_int(100),
		MTPlong(),
		MTP_long(stars / 2),
		MTPint(),
		MTPint(),
		MTPlong(),
		MTPlong(),
		MTPstring(),
		MTPPeer(),
		MTPint(),
		MTPint(),
		MTPint(),
		MTPstring(),
		MTPint(),
		MTPint(),
		MTPint(),
		MTPStarGiftBackground());
}

[[nodiscard]] MTPStarGift UniqueGiftFixture(BareId id, BareId giftId) {
	return MTP_starGiftUnique(
		MTP_flags(0),
		MTP_long(id),
		MTP_long(giftId),
		MTP_string("Unique Cap"),
		MTP_string("UniqueCap-1"),
		MTP_int(1),
		MTPPeer(),
		MTPstring(),
		MTPstring(),
		MTPVector<MTPStarGiftAttribute>(),
		MTP_int(1),
		MTP_int(10),
		MTPstring(),
		MTPVector<MTPStarsAmount>(),
		MTPPeer(),
		MTPlong(),
		MTPstring(),
		MTPlong(),
		MTPPeer(),
		MTPPeerColor(),
		MTPPeer(),
		MTPint(),
		MTPint());
}

[[nodiscard]] MTPPollAnswer PollAnswerFixture(const char *text) {
	return MTP_pollAnswer(
		MTP_flags(0),
		WireText(text),
		MTP_bytes(QByteArray(text).left(1)),
		MTPMessageMedia(),
		MTPPeer(),
		MTPint());
}

[[nodiscard]] MTPmessageAction BotAllowedAppAction() {
	return MTP_messageActionBotAllowed(
		MTP_flags(MTPDmessageActionBotAllowed::Flag::f_app),
		MTPstring(),
		MTP_botApp(
			MTP_flags(0),
			MTP_long(5074),
			MTP_long(0),
			MTP_string("fixture_app"),
			MTP_string("Mini <App>"),
			MTPstring(),
			MTP_photoEmpty(MTP_long(0)),
			MTPDocument(),
			MTP_long(0)));
}

[[nodiscard]] Message PhoneNumberRequestMessage() {
	auto result = AliceGroupMessage(73, 1703985180);
	result.action.content = ActionPhoneNumberRequest();
	return result;
}

template <typename Type>
[[nodiscard]] const Type *ParsedAction(const Message &parsed) {
	return std::get_if<Type>(&parsed.action.content);
}

template <typename Type>
[[nodiscard]] std::size_t Alternative() {
	using Content = decltype(ServiceAction::content);
	return Content(std::in_place_type<Type>).index();
}

[[nodiscard]] QJsonObject ServiceFields() {
	return Json(R"({
		"type": "service",
		"text": "",
		"text_entities": []
	})");
}

[[nodiscard]] QJsonObject AliceServiceFields() {
	return Merged(ServiceFields(), Json(R"({
		"actor": "Alice Example",
		"actor_id": "user1001"
	})"));
}

[[nodiscard]] QJsonObject AliceFallbackFields() {
	return Json(R"({
		"type": "message",
		"from": "Alice Example",
		"from_id": "user1001",
		"text": "",
		"text_entities": []
	})");
}

[[nodiscard]] QString ExpectNames(
		const QString &label,
		const QStringList &got,
		const QStringList &expected) {
	return (got == expected)
		? QString()
		: u"%1 are [%2], expected [%3]"_q.arg(
			label,
			got.join(u", "_q),
			expected.join(u", "_q));
}

[[nodiscard]] QString ExpectCallHtml(
		const Outputs &outputs,
		int32 id,
		const QByteArray &classes,
		const QByteArray &status) {
	return ExpectInOrder(
		u"HTML %1"_q.arg(id),
		PageRecord(outputs, kGroupPage, id),
		{
			"<div class=\"media clearfix pull_left " + classes + "\">",
			"<div class=\"title bold\">\nSelf Example\n",
			"<div class=\"status details\">\n" + status + '\n',
		});
}

[[nodiscard]] QString ExpectServicePhoto(
		const Outputs &outputs,
		const Message &parsed,
		const char *suggestedPath) {
	const auto &path = parsed.file().suggestedPath;
	return FirstFailure({
		(path == QString::fromLatin1(suggestedPath))
			? QString()
			: u"parsed photo path is '%1'"_q.arg(path),
		ExpectInOrder(
			u"HTML %1"_q.arg(parsed.id),
			PageRecord(outputs, kGroupPage, parsed.id),
			{
				"<div class=\"body details\">",
				"<div class=\"userpic_wrap\">",
				" style=\"width: 60px; height: 60px\">",
				"<div class=\"initials\" style=\"line-height: 60px\">\nF\n",
			}),
	});
}

template <typename Type, typename Value, typename Expected>
[[nodiscard]] QString ExpectParsed(
		const Message &parsed,
		const char *label,
		Value Type::*field,
		const Expected &expected) {
	const auto data = ParsedAction<Type>(parsed);
	if (!data) {
		return u"parsed %1 has another action"_q.arg(parsed.id);
	}
	const auto &value = data->*field;
	const auto name = u"parsed "_q + label;
	if constexpr (std::is_same_v<Value, CreditsAmount>) {
		return FirstFailure({
			ExpectNumber(name + u" whole"_q, value.whole(), expected.whole()),
			ExpectNumber(name + u" nano"_q, value.nano(), expected.nano()),
			ExpectNumber(name + u" ton"_q, value.ton(), expected.ton()),
		});
	} else if constexpr (std::is_arithmetic_v<Value>) {
		return ExpectNumber(name, qint64(value), expected);
	} else {
		return ExpectNumber(name, qint64(value.bare), expected);
	}
}

[[nodiscard]] QString ExpectOffer(
		const Message &parsed,
		const ActionStarGift &expected) {
	const auto field = [&](const char *label, auto member) {
		return ExpectParsed(parsed, label, member, expected.*member);
	};
	return FirstFailure({
		field("offer", &ActionStarGift::offer),
		field("offerPrice", &ActionStarGift::offerPrice),
		field("offerExpireAt", &ActionStarGift::offerExpireAt),
		field("offerAccepted", &ActionStarGift::offerAccepted),
		field("offerDeclined", &ActionStarGift::offerDeclined),
		field("offerExpired", &ActionStarGift::offerExpired),
	});
}

[[nodiscard]] QString CheckActionHtml(
		const Outputs &outputs,
		const ActionRow &row) {
	if (row.output == ActionOutput::AliceService
		|| row.output == ActionOutput::Service) {
		return ExpectServiceHtml(outputs, row.id, row.html);
	}
	const auto label = u"HTML %1"_q.arg(row.id);
	const auto record = PageRecord(outputs, kGroupPage, row.id);
	return FirstFailure({
		ExpectInOrder(label, record, {
			"<div class=\"message default clearfix\" id=\"message"
				+ QByteArray::number(row.id)
				+ "\">",
			"<div class=\"pull_left userpic_wrap\">",
			kAliceFromName,
		}),
		ExpectAbsent(label, record, { "body details" }),
		((row.output == ActionOutput::Fallback)
			? ExpectAbsent(label, record, {
				"<div class=\"text\">",
				"media_wrap",
			})
			: QString()),
	});
}

[[nodiscard]] QJsonObject ExpectedActionJson(const ActionRow &row) {
	auto error = QJsonParseError();
	const auto payload = QJsonDocument::fromJson(
		'{' + QByteArray(row.json) + '}',
		&error).object();
	if (error.error != QJsonParseError::NoError) {
		return QJsonObject{
			{ u"fixture_literal_error"_q, error.errorString() },
		};
	}
	switch (row.output) {
	case ActionOutput::AliceService:
	case ActionOutput::AliceCall:
		return Merged(AliceServiceFields(), payload);
	case ActionOutput::Service:
		return Merged(ServiceFields(), payload);
	case ActionOutput::Fallback:
		return Merged(AliceFallbackFields(), payload);
	}
	Unexpected("Output in ExpectedActionJson.");
}

[[nodiscard]] std::vector<ActionRow> ChatActionRows() {
	return {
		{
			.id = 1,
			.name = "chatCreate",
			.type = mtpc_messageActionChatCreate,
			.action = [] {
				return MTP_messageActionChatCreate(
					MTP_string("Planning \"Q1\""),
					UserIdList({ kAliceId, kBobId, kMissingId }));
			},
			.alternative = Alternative<ActionChatCreate>(),
			.json = R"("id": 1, "date": "2023-12-31T03:01:00",
				"date_unixtime": "1703980860", "action": "create_group",
				"title": "Planning \"Q1\"", "members": ["Alice Example",
				"Bob \"Quoted\" O'Example", null])",
			.html = "Alice Example created group &laquo;Planning &quot;Q1"
				"&quot;&raquo; with members Alice Example, Bob &quot;Quoted"
				"&quot; O&apos;Example and Deleted Account",
		},
		{
			.id = 2,
			.name = "chatEditTitle",
			.type = mtpc_messageActionChatEditTitle,
			.action = [] {
				return MTP_messageActionChatEditTitle(
					MTP_string("<Title & Co>"));
			},
			.alternative = Alternative<ActionChatEditTitle>(),
			.json = R"("id": 2, "date": "2023-12-31T03:02:00",
				"date_unixtime": "1703980920", "action": "edit_group_title",
				"title": "<Title & Co>")",
			.html = "Alice Example changed group title to "
				"&laquo;&lt;Title &amp; Co&gt;&raquo;",
		},
		{
			.id = 3,
			.name = "chatEditPhoto",
			.type = mtpc_messageActionChatEditPhoto,
			.action = [] {
				return MTP_messageActionChatEditPhoto(
					PhotoFixture(4003, 640, 480, 54321));
			},
			.alternative = Alternative<ActionChatEditPhoto>(),
			.json = R"json("id": 3, "date": "2023-12-31T03:03:00",
				"date_unixtime": "1703980980", "action": "edit_group_photo",
				"photo": "(File not included. Change data exporting )json"
				R"json(settings to download.)", "photo_file_size": 54321,
				"width": 640, "height": 480)json",
			.html = "Alice Example changed group photo",
			.skip = File::SkipReason::FileType,
			.extra = [](const Outputs &outputs, const Message &parsed) {
				return ExpectServicePhoto(
					outputs,
					parsed,
					"chats/chat_1/photos/photo_1@31-12-2023_03-03-00.jpg");
			},
		},
		{
			.id = 4,
			.name = "chatDeletePhoto",
			.type = mtpc_messageActionChatDeletePhoto,
			.action = [] { return MTP_messageActionChatDeletePhoto(); },
			.alternative = Alternative<ActionChatDeletePhoto>(),
			.json = R"("id": 4, "date": "2023-12-31T03:04:00",
				"date_unixtime": "1703981040",
				"action": "delete_group_photo")",
			.html = "Alice Example removed group photo",
		},
		{
			.id = 5,
			.name = "chatAddUser",
			.type = mtpc_messageActionChatAddUser,
			.action = [] {
				return MTP_messageActionChatAddUser(
					UserIdList({ kBobId, kMissingId }));
			},
			.alternative = Alternative<ActionChatAddUser>(),
			.json = R"("id": 5, "date": "2023-12-31T03:05:00",
				"date_unixtime": "1703981100", "action": "invite_members",
				"members": ["Bob \"Quoted\" O'Example", null])",
			.html = "Alice Example invited Bob &quot;Quoted&quot; "
				"O&apos;Example and Deleted Account",
		},
		{
			.id = 6,
			.name = "chatDeleteUser",
			.type = mtpc_messageActionChatDeleteUser,
			.action = [] {
				return MTP_messageActionChatDeleteUser(MTP_long(kBobId));
			},
			.alternative = Alternative<ActionChatDeleteUser>(),
			.json = R"("id": 6, "date": "2023-12-31T03:06:00",
				"date_unixtime": "1703981160", "action": "remove_members",
				"members": ["Bob \"Quoted\" O'Example"])",
			.html = "Alice Example removed Bob &quot;Quoted&quot; "
				"O&apos;Example",
		},
		{
			.id = 7,
			.name = "chatJoinedByLink",
			.type = mtpc_messageActionChatJoinedByLink,
			.action = [] {
				return MTP_messageActionChatJoinedByLink(MTP_long(kBobId));
			},
			.alternative = Alternative<ActionChatJoinedByLink>(),
			.json = R"("id": 7, "date": "2023-12-31T03:07:00",
				"date_unixtime": "1703981220", "action": "join_group_by_link",
				"inviter": "Bob \"Quoted\" O'Example")",
			.html = "Alice Example joined group by link from "
				"Bob &quot;Quoted&quot; O&apos;Example",
		},
		{
			.id = 8,
			.name = "channelCreate",
			.type = mtpc_messageActionChannelCreate,
			.action = [] {
				return MTP_messageActionChannelCreate(
					MTP_string("News & Notes"));
			},
			.alternative = Alternative<ActionChannelCreate>(),
			.json = R"("id": 8, "date": "2023-12-31T03:08:00",
				"date_unixtime": "1703981280", "action": "create_channel",
				"title": "News & Notes")",
			.html = "Channel &laquo;News &amp; Notes&raquo; created",
		},
		{
			.id = 9,
			.name = "chatMigrateTo",
			.type = mtpc_messageActionChatMigrateTo,
			.action = [] {
				return MTP_messageActionChatMigrateTo(MTP_long(3005));
			},
			.alternative = Alternative<ActionChatMigrateTo>(),
			.json = R"("id": 9, "date": "2023-12-31T03:09:00",
				"date_unixtime": "1703981340",
				"action": "migrate_to_supergroup")",
			.html = "Alice Example converted this group to a supergroup",
			.extra = [](const Outputs &, const Message &parsed) {
				return ExpectParsed(
					parsed,
					"channelId",
					&ActionChatMigrateTo::channelId,
					3005);
			},
		},
	};
}

[[nodiscard]] std::vector<ActionRow> HistoryActionRows() {
	return {
		{
			.id = 10,
			.name = "channelMigrateFrom",
			.type = mtpc_messageActionChannelMigrateFrom,
			.action = [] {
				return MTP_messageActionChannelMigrateFrom(
					MTP_string("Old Group"),
					MTP_long(2005));
			},
			.alternative = Alternative<ActionChannelMigrateFrom>(),
			.json = R"("id": 10, "date": "2023-12-31T03:10:00",
				"date_unixtime": "1703981400", "action": "migrate_from_group",
				"title": "Old Group")",
			.html = "Alice Example converted a basic group to this "
				"supergroup &laquo;Old Group&raquo;",
			.extra = [](const Outputs &, const Message &parsed) {
				return ExpectParsed(
					parsed,
					"chatId",
					&ActionChannelMigrateFrom::chatId,
					2005);
			},
		},
		{
			.id = 11,
			.name = "pinMessage",
			.type = mtpc_messageActionPinMessage,
			.action = [] { return MTP_messageActionPinMessage(); },
			.alternative = Alternative<ActionPinMessage>(),
			.json = R"("id": 11, "date": "2023-12-31T03:11:00",
				"date_unixtime": "1703981460", "action": "pin_message",
				"message_id": 101)",
			.html = "Alice Example pinned <a href=\"#go_to_message101\" "
				"onclick=\"return GoToMessage(101)\">this message</a>",
			.replyToId = 101,
		},
		{
			.id = 12,
			.name = "historyClear",
			.type = mtpc_messageActionHistoryClear,
			.action = [] { return MTP_messageActionHistoryClear(); },
			.alternative = Alternative<ActionHistoryClear>(),
			.json = R"("id": 12, "date": "2023-12-31T03:12:00",
				"date_unixtime": "1703981520", "action": "clear_history")",
			.html = "History cleared",
		},
		{
			.id = 13,
			.name = "gameScore",
			.type = mtpc_messageActionGameScore,
			.action = [] {
				return MTP_messageActionGameScore(
					MTP_long(5013),
					MTP_int(42));
			},
			.alternative = Alternative<ActionGameScore>(),
			.json = R"("id": 13, "date": "2023-12-31T03:13:00",
				"date_unixtime": "1703981580", "action": "score_in_game",
				"game_message_id": 102, "score": 42)",
			.html = "Alice Example scored 42 in <a href=\"#go_to_message102\" "
				"onclick=\"return GoToMessage(102)\">this game</a>",
			.replyToId = 102,
			.extra = [](const Outputs &, const Message &parsed) {
				return ExpectParsed(
					parsed,
					"gameId",
					&ActionGameScore::gameId,
					5013);
			},
		},
		{
			.id = 14,
			.name = "paymentSentMe",
			.type = mtpc_messageActionPaymentSentMe,
			.action = [] {
				return MTP_messageActionPaymentSentMe(
					MTP_flags(0),
					MTP_string("USD"),
					MTP_long(1400),
					MTP_bytes(),
					MTPPaymentRequestedInfo(),
					MTPstring(),
					MTP_paymentCharge(
						MTP_string("charge-14"),
						MTP_string("provider-14")),
					MTPint());
			},
			.alternative = Alternative<v::null_t>(),
			.output = ActionOutput::Fallback,
			.json = R"("id": 14, "date": "2023-12-31T03:14:00",
				"date_unixtime": "1703981640")",
		},
		{
			.id = 15,
			.name = "paymentSent",
			.type = mtpc_messageActionPaymentSent,
			.action = [] {
				using Flag = MTPDmessageActionPaymentSent::Flag;
				return MTP_messageActionPaymentSent(
					MTP_flags(Flag::f_recurring_init),
					MTP_string("USD"),
					MTP_long(12345),
					MTPstring(),
					MTPint());
			},
			.alternative = Alternative<ActionPaymentSent>(),
			.output = ActionOutput::Service,
			.json = R"("id": 15, "date": "2023-12-31T03:15:00",
				"date_unixtime": "1703981700", "action": "send_payment",
				"amount": 12345, "currency": "USD",
				"invoice_message_id": 103, "recurring": "init")",
			.html = "You have successfully transferred $123.45 for <a "
				"href=\"#go_to_message103\" onclick=\"return "
				"GoToMessage(103)\">this invoice</a> and allowed future "
				"recurring payments",
			.replyToId = 103,
		},
		{
			.id = 16,
			.name = "phoneCall",
			.type = mtpc_messageActionPhoneCall,
			.action = [] {
				using Flag = MTPDmessageActionPhoneCall::Flag;
				return MTP_messageActionPhoneCall(
					MTP_flags(Flag::f_reason | Flag::f_duration),
					MTP_long(5016),
					MTP_phoneCallDiscardReasonBusy(),
					MTP_int(125));
			},
			.alternative = Alternative<ActionPhoneCall>(),
			.output = ActionOutput::AliceCall,
			.json = R"("id": 16, "date": "2023-12-31T03:16:00",
				"date_unixtime": "1703981760", "action": "phone_call",
				"duration_seconds": 125, "discard_reason": "busy")",
			.extra = [](const Outputs &outputs, const Message &) {
				return ExpectCallHtml(
					outputs,
					16,
					"media_call success",
					"Declined (125 seconds)");
			},
		},
	};
}

[[nodiscard]] std::vector<ActionRow> NoticeActionRows() {
	return {
		{
			.id = 17,
			.name = "screenshotTaken",
			.type = mtpc_messageActionScreenshotTaken,
			.action = [] { return MTP_messageActionScreenshotTaken(); },
			.alternative = Alternative<ActionScreenshotTaken>(),
			.json = R"("id": 17, "date": "2023-12-31T03:17:00",
				"date_unixtime": "1703981820", "action": "take_screenshot")",
			.html = "Alice Example took a screenshot",
		},
		{
			.id = 18,
			.name = "customAction",
			.type = mtpc_messageActionCustomAction,
			.action = [] {
				return MTP_messageActionCustomAction(
					MTP_string("Custom <b>note</b> & more"));
			},
			.alternative = Alternative<ActionCustomAction>(),
			.json = R"("id": 18, "date": "2023-12-31T03:18:00",
				"date_unixtime": "1703981880",
				"information_text": "Custom <b>note</b> & more")",
			.html = "Custom &lt;b&gt;note&lt;/b&gt; &amp; more",
		},
		{
			.id = 19,
			.name = "botAllowed",
			.type = mtpc_messageActionBotAllowed,
			.action = [] {
				return MTP_messageActionBotAllowed(
					MTP_flags(MTPDmessageActionBotAllowed::Flag::f_domain),
					MTP_string("example.org"),
					MTPBotApp());
			},
			.alternative = Alternative<ActionBotAllowed>(),
			.output = ActionOutput::Service,
			.json = R"("id": 19, "date": "2023-12-31T03:19:00",
				"date_unixtime": "1703981940",
				"action": "allow_sending_messages",
				"reason_domain": "example.org")",
			.html = "You allowed this bot to message you when you logged in "
				"on example.org",
		},
		{
			.id = 20,
			.name = "secureValuesSentMe",
			.type = mtpc_messageActionSecureValuesSentMe,
			.action = [] {
				return MTP_messageActionSecureValuesSentMe(
					MTPVector<MTPSecureValue>(),
					MTP_secureCredentialsEncrypted(
						MTP_bytes(),
						MTP_bytes(),
						MTP_bytes()));
			},
			.alternative = Alternative<v::null_t>(),
			.output = ActionOutput::Fallback,
			.json = R"("id": 20, "date": "2023-12-31T03:20:00",
				"date_unixtime": "1703982000")",
		},
		{
			.id = 21,
			.name = "secureValuesSent",
			.type = mtpc_messageActionSecureValuesSent,
			.action = [] {
				return MTP_messageActionSecureValuesSent(
					MTP_vector<MTPSecureValueType>(QVector<MTPSecureValueType>{
						MTP_secureValueTypePassport(),
						MTP_secureValueTypeAddress(),
						MTP_secureValueTypeEmail(),
					}));
			},
			.alternative = Alternative<ActionSecureValuesSent>(),
			.output = ActionOutput::Service,
			.json = R"("id": 21, "date": "2023-12-31T03:21:00",
				"date_unixtime": "1703982060",
				"action": "send_passport_values",
				"values": ["passport", "address_information", "email"])",
			.html = "You have sent the following documents: Passport, "
				"Address information and Email",
		},
		{
			.id = 22,
			.name = "contactSignUp",
			.type = mtpc_messageActionContactSignUp,
			.action = [] { return MTP_messageActionContactSignUp(); },
			.alternative = Alternative<ActionContactSignUp>(),
			.json = R"("id": 22, "date": "2023-12-31T03:22:00",
				"date_unixtime": "1703982120", "action": "joined_telegram")",
			.html = "Alice Example joined Telegram",
		},
		{
			.id = 23,
			.name = "geoProximityReached",
			.type = mtpc_messageActionGeoProximityReached,
			.action = [] {
				return MTP_messageActionGeoProximityReached(
					MTP_peerUser(MTP_long(kBobId)),
					MTP_peerUser(MTP_long(kSelfId)),
					MTP_int(1530));
			},
			.alternative = Alternative<ActionGeoProximityReached>(),
			.output = ActionOutput::Service,
			.json = R"("id": 23, "date": "2023-12-31T03:23:00",
				"date_unixtime": "1703982180", "action": "proximity_reached",
				"from": "Bob \"Quoted\" O'Example", "from_id": "user1002",
				"to": "Self Example", "to_id": "user1000", "distance": 1530)",
			.html = "Bob &quot;Quoted&quot; O&apos;Example is now within "
				"1.53 km from you",
		},
		{
			.id = 24,
			.name = "groupCall",
			.type = mtpc_messageActionGroupCall,
			.action = [] {
				return MTP_messageActionGroupCall(
					MTP_flags(MTPDmessageActionGroupCall::Flag::f_duration),
					MTP_inputGroupCall(MTP_long(5024), MTP_long(0)),
					MTP_int(754));
			},
			.alternative = Alternative<ActionGroupCall>(),
			.json = R"("id": 24, "date": "2023-12-31T03:24:00",
				"date_unixtime": "1703982240", "action": "group_call",
				"duration": 754)",
			.html = "Alice Example started voice chat (754 seconds)",
		},
	};
}

[[nodiscard]] std::vector<ActionRow> ChatSettingsActionRows() {
	return {
		{
			.id = 25,
			.name = "inviteToGroupCall",
			.type = mtpc_messageActionInviteToGroupCall,
			.action = [] {
				return MTP_messageActionInviteToGroupCall(
					MTP_inputGroupCall(MTP_long(5025), MTP_long(0)),
					UserIdList({ kBobId }));
			},
			.alternative = Alternative<ActionInviteToGroupCall>(),
			.json = R"("id": 25, "date": "2023-12-31T03:25:00",
				"date_unixtime": "1703982300",
				"action": "invite_to_group_call",
				"members": ["Bob \"Quoted\" O'Example"])",
			.html = "Alice Example invited Bob &quot;Quoted&quot; "
				"O&apos;Example to the voice chat",
		},
		{
			.id = 26,
			.name = "setMessagesTTL",
			.type = mtpc_messageActionSetMessagesTTL,
			.action = [] {
				return MTP_messageActionSetMessagesTTL(
					MTP_flags(0),
					MTP_int(86400),
					MTPlong());
			},
			.alternative = Alternative<ActionSetMessagesTTL>(),
			.json = R"("id": 26, "date": "2023-12-31T03:26:00",
				"date_unixtime": "1703982360", "action": "set_messages_ttl",
				"period": 86400)",
			.html = "Alice Example has set messages to auto-delete in "
				"24 hours",
		},
		{
			.id = 27,
			.name = "groupCallScheduled",
			.type = mtpc_messageActionGroupCallScheduled,
			.action = [] {
				return MTP_messageActionGroupCallScheduled(
					MTP_inputGroupCall(MTP_long(5027), MTP_long(0)),
					MTP_int(1704153600));
			},
			.alternative = Alternative<ActionGroupCallScheduled>(),
			.json = R"("id": 27, "date": "2023-12-31T03:27:00",
				"date_unixtime": "1703982420",
				"action": "group_call_scheduled",
				"schedule_date": 1704153600)",
			.html = "Alice Example scheduled a voice chat for "
				"02.01.2024 03:00:00",
		},
		{
			.id = 28,
			.name = "setChatTheme",
			.type = mtpc_messageActionSetChatTheme,
			.action = [] {
				return MTP_messageActionSetChatTheme(
					MTP_chatTheme(MTP_string("\xE2\x9C\xA8")));
			},
			.alternative = Alternative<ActionSetChatTheme>(),
			.json = R"("id": 28, "date": "2023-12-31T03:28:00",
				"date_unixtime": "1703982480", "action": "edit_chat_theme",
				"emoticon": "\u2728")",
			.html = "Alice Example changed chat theme to \xE2\x9C\xA8",
		},
		{
			.id = 29,
			.name = "chatJoinedByRequest",
			.type = mtpc_messageActionChatJoinedByRequest,
			.action = [] { return MTP_messageActionChatJoinedByRequest(); },
			.alternative = Alternative<ActionChatJoinedByRequest>(),
			.json = R"("id": 29, "date": "2023-12-31T03:29:00",
				"date_unixtime": "1703982540",
				"action": "join_group_by_request")",
			.html = "Alice Example joined group by request",
		},
		{
			.id = 30,
			.name = "webViewDataSentMe",
			.type = mtpc_messageActionWebViewDataSentMe,
			.action = [] {
				return MTP_messageActionWebViewDataSentMe(
					MTP_string("Me button"),
					MTP_string("{}"));
			},
			.alternative = Alternative<v::null_t>(),
			.output = ActionOutput::Fallback,
			.json = R"("id": 30, "date": "2023-12-31T03:30:00",
				"date_unixtime": "1703982600")",
		},
		{
			.id = 31,
			.name = "webViewDataSent",
			.type = mtpc_messageActionWebViewDataSent,
			.action = [] {
				return MTP_messageActionWebViewDataSent(
					MTP_string("Open <Shop>"));
			},
			.alternative = Alternative<ActionWebViewDataSent>(),
			.output = ActionOutput::Service,
			.json = R"("id": 31, "date": "2023-12-31T03:31:00",
				"date_unixtime": "1703982660",
				"action": "send_webview_data", "button_text": "Open <Shop>")",
			.html = "You have just successfully transferred data from the "
				"&laquo;Open &lt;Shop&gt;&raquo; button to the bot",
			.extra = [](const Outputs &outputs, const Message &) {
				return FirstFailure({
					ExpectRawValues(
						u"raw JSON 31 text members"_q,
						RawJsonMembers(outputs, 31, "text"),
						{ "\"\"" }),
					ExpectRawValues(
						u"raw JSON 31 button_text members"_q,
						RawJsonMembers(outputs, 31, "button_text"),
						{ "\"Open <Shop>\"" }),
				});
			},
		},
		{
			.id = 32,
			.name = "giftPremium",
			.type = mtpc_messageActionGiftPremium,
			.action = [] {
				return MTP_messageActionGiftPremium(
					MTP_flags(0),
					MTP_string("USD"),
					MTP_long(499),
					MTP_int(90),
					MTPstring(),
					MTPlong(),
					MTPTextWithEntities());
			},
			.alternative = Alternative<ActionGiftPremium>(),
			.json = R"("id": 32, "date": "2023-12-31T03:32:00",
				"date_unixtime": "1703982720", "action": "send_premium_gift",
				"cost": "$4.99", "days": 90)",
			.html = "Alice Example sent you a gift for $4.99: "
				"Telegram Premium for 90 days.",
		},
	};
}

[[nodiscard]] std::vector<ActionRow> TopicActionRows() {
	return {
		{
			.id = 33,
			.name = "topicCreate",
			.type = mtpc_messageActionTopicCreate,
			.action = [] {
				return MTP_messageActionTopicCreate(
					MTP_flags(0),
					MTP_string("Release <Notes>"),
					MTP_int(0x6FB9F0),
					MTPlong());
			},
			.alternative = Alternative<ActionTopicCreate>(),
			.json = R"("id": 33, "date": "2023-12-31T03:33:00",
				"date_unixtime": "1703982780", "action": "topic_created",
				"title": "Release <Notes>")",
			.html = "Alice Example created topic "
				"&laquo;Release &lt;Notes&gt;&raquo;",
		},
		{
			.id = 34,
			.name = "topicEdit",
			.type = mtpc_messageActionTopicEdit,
			.action = [] {
				using Flag = MTPDmessageActionTopicEdit::Flag;
				return MTP_messageActionTopicEdit(
					MTP_flags(Flag::f_title | Flag::f_icon_emoji_id),
					MTP_string("Renamed"),
					MTP_long(5034),
					MTP_boolFalse(),
					MTP_boolFalse());
			},
			.alternative = Alternative<ActionTopicEdit>(),
			.json = R"("id": 34, "date": "2023-12-31T03:34:00",
				"date_unixtime": "1703982840", "action": "topic_edit",
				"new_title": "Renamed", "new_icon_emoji_id": 5034)",
			.html = "Alice Example changed topic title to &laquo;Renamed"
				"&raquo;,icon to &laquo;5034&raquo;",
		},
		{
			.id = 35,
			.name = "suggestProfilePhoto",
			.type = mtpc_messageActionSuggestProfilePhoto,
			.action = [] {
				return MTP_messageActionSuggestProfilePhoto(
					PhotoFixture(4035, 320, 320, 23456));
			},
			.alternative = Alternative<ActionSuggestProfilePhoto>(),
			.json = R"json("id": 35, "date": "2023-12-31T03:35:00",
				"date_unixtime": "1703982900",
				"action": "suggest_profile_photo",
				"photo": "(File unavailable, please try again later)",
				"photo_file_size": 23456, "width": 320, "height": 320)json",
			.html = "Alice Example suggests to use this photo",
			.skip = File::SkipReason::Unavailable,
			.extra = [](const Outputs &outputs, const Message &parsed) {
				return ExpectServicePhoto(
					outputs,
					parsed,
					"chats/chat_1/photos/photo_2@31-12-2023_03-35-00.jpg");
			},
		},
		{
			.id = 36,
			.name = "requestedPeer",
			.type = mtpc_messageActionRequestedPeer,
			.action = [] {
				return MTP_messageActionRequestedPeer(
					MTP_int(7),
					MTP_vector<MTPPeer>(QVector<MTPPeer>{
						MTP_peerUser(MTP_long(kBobId)),
						MTP_peerChannel(MTP_long(kChannelId)),
					}));
			},
			.alternative = Alternative<ActionRequestedPeer>(),
			.json = R"("id": 36, "date": "2023-12-31T03:36:00",
				"date_unixtime": "1703982960", "action": "requested_peer",
				"button_id": 7,
				"peers": "[\n       1002,\n       562949953424313\n      ]")",
			.html = "requested: ",
		},
		{
			.id = 37,
			.name = "setChatWallPaper",
			.type = mtpc_messageActionSetChatWallPaper,
			.action = [] {
				using Flag = MTPDmessageActionSetChatWallPaper::Flag;
				return MTP_messageActionSetChatWallPaper(
					MTP_flags(Flag::f_same | Flag::f_for_both),
					MTP_wallPaperNoFile(
						MTP_long(5037),
						MTP_flags(0),
						MTPWallPaperSettings()));
			},
			.alternative = Alternative<ActionSetChatWallPaper>(),
			.json = R"("id": 37, "date": "2023-12-31T03:37:00",
				"date_unixtime": "1703983020",
				"action": "set_same_chat_wallpaper", "message_id": 104)",
			.html = "Alice Example set <a href=\"#go_to_message104\" "
				"onclick=\"return GoToMessage(104)\">the same background</a> "
				"for this chat",
			.replyToId = 104,
			.extra = [](const Outputs &, const Message &parsed) {
				return ExpectParsed(
					parsed,
					"both",
					&ActionSetChatWallPaper::both,
					1);
			},
		},
		{
			.id = 38,
			.name = "giftCode",
			.type = mtpc_messageActionGiftCode,
			.action = [] {
				using Flag = MTPDmessageActionGiftCode::Flag;
				return MTP_messageActionGiftCode(
					MTP_flags(Flag::f_via_giveaway | Flag::f_boost_peer),
					MTP_peerChannel(MTP_long(kChannelId)),
					MTP_int(3),
					MTP_string("GIFT-CODE"),
					MTPstring(),
					MTPlong(),
					MTPstring(),
					MTPlong(),
					MTPTextWithEntities());
			},
			.alternative = Alternative<ActionGiftCode>(),
			.output = ActionOutput::Service,
			.json = R"("id": 38, "date": "2023-12-31T03:38:00",
				"date_unixtime": "1703983080", "action": "gift_code_prize",
				"gift_code": "GIFT-CODE", "boost_peer_id": "channel3001",
				"days": 3, "unclaimed": false, "via_giveaway": true)",
			.html = "You won a Telegram Premium for 3 days prize in a "
				"giveaway organized by a channel.",
		},
	};
}

[[nodiscard]] std::vector<ActionRow> PaymentActionRows() {
	return {
		{
			.id = 39,
			.name = "giveawayLaunch",
			.type = mtpc_messageActionGiveawayLaunch,
			.action = [] {
				return MTP_messageActionGiveawayLaunch(
					MTP_flags(0),
					MTPlong());
			},
			.alternative = Alternative<ActionGiveawayLaunch>(),
			.output = ActionOutput::Service,
			.json = R"("id": 39, "date": "2023-12-31T03:39:00",
				"date_unixtime": "1703983140", "action": "giveaway_launch")",
			.html = "Alice Example just started a giveaway of Telegram "
				"Premium subscriptions to its followers.",
		},
		{
			.id = 40,
			.name = "giveawayResults",
			.type = mtpc_messageActionGiveawayResults,
			.action = [] {
				using Flag = MTPDmessageActionGiveawayResults::Flag;
				return MTP_messageActionGiveawayResults(
					MTP_flags(Flag::f_stars),
					MTP_int(5),
					MTP_int(0));
			},
			.alternative = Alternative<ActionGiveawayResults>(),
			.output = ActionOutput::Service,
			.json = R"("id": 40, "date": "2023-12-31T03:40:00",
				"date_unixtime": "1703983200", "action": "giveaway_results",
				"winners": 5, "unclaimed": 0, "stars": true)",
			.html = "5 of the giveaway was randomly selected by Telegram "
				"and received their prize.",
		},
		{
			.id = 41,
			.name = "boostApply",
			.type = mtpc_messageActionBoostApply,
			.action = [] { return MTP_messageActionBoostApply(MTP_int(3)); },
			.alternative = Alternative<ActionBoostApply>(),
			.json = R"("id": 41, "date": "2023-12-31T03:41:00",
				"date_unixtime": "1703983260", "action": "boost_apply",
				"boosts": 3)",
			.html = "Alice Example boosted the group 3 times",
		},
		{
			.id = 42,
			.name = "requestedPeerSentMe",
			.type = mtpc_messageActionRequestedPeerSentMe,
			.action = [] {
				return MTP_messageActionRequestedPeerSentMe(
					MTP_int(1),
					MTP_vector<MTPRequestedPeer>(
						1,
						MTP_requestedPeerUser(
							MTP_flags(0),
							MTP_long(kBobId),
							MTPstring(),
							MTPstring(),
							MTPstring(),
							MTPPhoto())));
			},
			.alternative = Alternative<v::null_t>(),
			.output = ActionOutput::Fallback,
			.json = R"("id": 42, "date": "2023-12-31T03:42:00",
				"date_unixtime": "1703983320")",
		},
		{
			.id = 43,
			.name = "paymentRefunded",
			.type = mtpc_messageActionPaymentRefunded,
			.action = [] {
				return MTP_messageActionPaymentRefunded(
					MTP_flags(0),
					MTP_peerUser(MTP_long(kBobId)),
					MTP_string("USD"),
					MTP_long(2500),
					MTPbytes(),
					MTP_paymentCharge(
						MTP_string("charge-43"),
						MTP_string("provider-43")));
			},
			.alternative = Alternative<ActionPaymentRefunded>(),
			.output = ActionOutput::Service,
			.json = R"("id": 43, "date": "2023-12-31T03:43:00",
				"date_unixtime": "1703983380", "action": "refunded_payment",
				"amount": 2500, "currency": "USD",
				"peer_name": "Bob \"Quoted\" O'Example", "peer_id": "user1002",
				"charge_id": "charge-43")",
			.html = "Bob &quot;Quoted&quot; O&apos;Example refunded back "
				"$25.00",
		},
		{
			.id = 44,
			.name = "giftStars",
			.type = mtpc_messageActionGiftStars,
			.action = [] {
				return MTP_messageActionGiftStars(
					MTP_flags(0),
					MTP_string("USD"),
					MTP_long(1999),
					MTP_long(500),
					MTPstring(),
					MTPlong(),
					MTPstring());
			},
			.alternative = Alternative<ActionGiftCredits>(),
			.json = R"("id": 44, "date": "2023-12-31T03:44:00",
				"date_unixtime": "1703983440", "action": "send_stars_gift",
				"cost": "$19.99", "amount_whole": 500, "amount_nano": 0)",
			.html = "Alice Example sent you a gift for $19.99: "
				"500 Telegram Stars.",
		},
		{
			.id = 45,
			.name = "prizeStars",
			.type = mtpc_messageActionPrizeStars,
			.action = [] {
				using Flag = MTPDmessageActionPrizeStars::Flag;
				return MTP_messageActionPrizeStars(
					MTP_flags(Flag::f_unclaimed),
					MTP_long(2500),
					MTP_string("tx-45"),
					MTP_peerChannel(MTP_long(kChannelId)),
					MTP_int(105));
			},
			.alternative = Alternative<ActionPrizeStars>(),
			.json = R"("id": 45, "date": "2023-12-31T03:45:00",
				"date_unixtime": "1703983500", "action": "stars_prize",
				"boost_peer_id": "channel3001", "boost_peer_name": null,
				"stars": 2500, "is_unclaimed": true, "giveaway_msg_id": 105,
				"transaction_id": "tx-45")",
			.html = "You won a prize in a giveaway organized by Deleted.\n"
				" Your prize is 2500 Telegram Stars.",
		},
	};
}

[[nodiscard]] std::vector<ActionRow> GiftActionRows() {
	return {
		{
			.id = 46,
			.name = "starGift",
			.type = mtpc_messageActionStarGift,
			.action = [] {
				using Flag = MTPDmessageActionStarGift::Flag;
				return MTP_messageActionStarGift(
					MTP_flags(Flag::f_name_hidden | Flag::f_message),
					StarGiftFixture(5046, 250, true),
					MTP_textWithEntities(
						MTP_string("Thanks <3"),
						MTP_vector<MTPMessageEntity>(
							1,
							MTP_messageEntityBold(MTP_int(0), MTP_int(6)))),
					MTPlong(),
					MTPint(),
					MTPlong(),
					MTPPeer(),
					MTPPeer(),
					MTPlong(),
					MTPstring(),
					MTPint(),
					MTPPeer(),
					MTPint());
			},
			.alternative = Alternative<ActionStarGift>(),
			.json = R"("id": 46, "date": "2023-12-31T03:46:00",
				"date_unixtime": "1703983560", "action": "send_star_gift",
				"gift_id": 5046, "stars": 250, "is_limited": true,
				"is_anonymous": true, "gift_text": [
					{"type": "bold", "text": "Thanks"}, " <3"])",
			.html = "Alice Example sent you a gift of 250 Telegram Stars.",
		},
		{
			.id = 47,
			.name = "starGiftUnique",
			.type = mtpc_messageActionStarGiftUnique,
			.action = [] {
				return MTP_messageActionStarGiftUnique(
					MTP_flags(0),
					UniqueGiftFixture(5047, 6047),
					MTPint(),
					MTPlong(),
					MTPPeer(),
					MTPPeer(),
					MTPlong(),
					MTPStarsAmount(),
					MTPint(),
					MTPint(),
					MTPlong(),
					MTPint(),
					MTPTextWithEntities());
			},
			.alternative = Alternative<ActionStarGift>(),
			.json = R"("id": 47, "date": "2023-12-31T03:47:00",
				"date_unixtime": "1703983620", "action": "send_star_gift",
				"gift_id": 5047, "stars": 0, "is_limited": false,
				"is_anonymous": false, "gift_text": "")",
			.html = "Alice Example sent you a gift of 0 Telegram Stars.",
		},
		{
			.id = 48,
			.name = "paidMessagesRefunded",
			.type = mtpc_messageActionPaidMessagesRefunded,
			.action = [] {
				return MTP_messageActionPaidMessagesRefunded(
					MTP_int(3),
					MTP_long(150));
			},
			.alternative = Alternative<ActionPaidMessagesRefunded>(),
			.output = ActionOutput::Service,
			.json = R"("id": 48, "date": "2023-12-31T03:48:00",
				"date_unixtime": "1703983680", "actor": "Self Example",
				"actor_id": "user1000", "action": "paid_messages_refund",
				"messages_count": 3, "stars_count": 150)",
			.html = "You refunded 150 Stars for 3 messages to Fixture Group",
			.fromId = kSelfId,
			.out = true,
		},
		{
			.id = 49,
			.name = "paidMessagesPrice",
			.type = mtpc_messageActionPaidMessagesPrice,
			.action = [] {
				using Flag = MTPDmessageActionPaidMessagesPrice::Flag;
				return MTP_messageActionPaidMessagesPrice(
					MTP_flags(Flag::f_broadcast_messages_allowed),
					MTP_long(25));
			},
			.alternative = Alternative<ActionPaidMessagesPrice>(),
			.json = R"("id": 49, "date": "2023-12-31T03:49:00",
				"date_unixtime": "1703983740",
				"action": "paid_messages_price_change", "price_stars": 25,
				"is_broadcast_messages_allowed": true)",
			.html = "Price per message changed to 25 Telegram Stars.",
		},
		{
			.id = 50,
			.name = "conferenceCall",
			.type = mtpc_messageActionConferenceCall,
			.action = [] {
				using Flag = MTPDmessageActionConferenceCall::Flag;
				return MTP_messageActionConferenceCall(
					MTP_flags(Flag::f_active),
					MTP_long(5050),
					MTPint(),
					MTPVector<MTPPeer>());
			},
			.alternative = Alternative<ActionPhoneCall>(),
			.output = ActionOutput::AliceCall,
			.json = R"("id": 50, "date": "2023-12-31T03:50:00",
				"date_unixtime": "1703983800", "action": "conference_call",
				"is_active": true, "is_missed": false)",
			.extra = [](const Outputs &outputs, const Message &parsed) {
				return FirstFailure({
					ExpectParsed(
						parsed,
						"conferenceId",
						&ActionPhoneCall::conferenceId,
						5050),
					ExpectCallHtml(outputs, 50, "media_call", "Ongoing"),
				});
			},
		},
	};
}

[[nodiscard]] std::vector<ActionRow> TodoActionRows() {
	return {
		{
			.id = 51,
			.name = "todoCompletions",
			.type = mtpc_messageActionTodoCompletions,
			.action = [] {
				return MTP_messageActionTodoCompletions(
					MTP_vector<MTPint>(QVector<MTPint>{
						MTP_int(1),
						MTP_int(3),
					}),
					MTP_vector<MTPint>(1, MTP_int(2)));
			},
			.alternative = Alternative<ActionTodoCompletions>(),
			.json = R"("id": 51, "date": "2023-12-31T03:51:00",
				"date_unixtime": "1703983860", "action": "todo_completions",
				"completed": [1, 3], "incompleted": [2])",
			.html = "Alice Example marked 1 and 3 as done and 2 as not done "
				"yet in <a href=\"#go_to_message106\" onclick=\"return "
				"GoToMessage(106)\">this todo list</a>.",
			.replyToId = 106,
		},
		{
			.id = 52,
			.name = "todoAppendTasks",
			.type = mtpc_messageActionTodoAppendTasks,
			.action = [] {
				return MTP_messageActionTodoAppendTasks(
					MTP_vector<MTPTodoItem>(QVector<MTPTodoItem>{
						MTP_todoItem(MTP_int(4), WireText("Buy <milk>")),
						MTP_todoItem(MTP_int(5), WireText("Call & plan")),
					}));
			},
			.alternative = Alternative<ActionTodoAppendTasks>(),
			.json = R"("id": 52, "date": "2023-12-31T03:52:00",
				"date_unixtime": "1703983920", "action": "todo_append_tasks",
				"items": [{"text": "Buy <milk>", "id": 4},
					{"text": "Call & plan", "id": 5}])",
			.html = "Alice Example added tasks: &quot;Buy &lt;milk&gt;&quot;, "
				"&quot;Call &amp; plan&quot;",
		},
		{
			.id = 53,
			.name = "suggestedPostApproval",
			.type = mtpc_messageActionSuggestedPostApproval,
			.action = [] {
				using Flag = MTPDmessageActionSuggestedPostApproval::Flag;
				return MTP_messageActionSuggestedPostApproval(
					MTP_flags(Flag::f_schedule_date | Flag::f_price),
					MTPstring(),
					MTP_int(1704153600),
					MTP_starsAmount(MTP_long(25), MTP_int(125000000)));
			},
			.alternative = Alternative<ActionSuggestedPostApproval>(),
			.json = R"("id": 53, "date": "2023-12-31T03:53:00",
				"date_unixtime": "1703983980",
				"action": "process_suggested_post",
				"price_amount_whole": "25", "price_amount_nano": "125000000",
				"price_currency": "Stars", "scheduled_date": 1704153600)",
			.html = "Alice Example approved your suggested post, for 25.125 "
				"stars, 2 January 2024 at 03:00.",
			.extra = [](const Outputs &, const Message &parsed) {
				return ExpectParsed(
					parsed,
					"price",
					&ActionSuggestedPostApproval::price,
					CreditsAmount(25, 125000000, CreditsType::Stars));
			},
		},
		{
			.id = 54,
			.name = "suggestedPostSuccess",
			.type = mtpc_messageActionSuggestedPostSuccess,
			.action = [] {
				return MTP_messageActionSuggestedPostSuccess(
					MTP_starsTonAmount(MTP_long(1750000000)));
			},
			.alternative = Alternative<ActionSuggestedPostSuccess>(),
			.json = R"("id": 54, "date": "2023-12-31T03:54:00",
				"date_unixtime": "1703984040",
				"action": "suggested_post_success",
				"price_amount_whole": "1", "price_amount_nano": "750000000",
				"price_currency": "TON")",
			.html = "The paid post was shown for 24 hours and 1.75 TON "
				"were transferred to the channel.",
			.extra = [](const Outputs &, const Message &parsed) {
				return ExpectParsed(
					parsed,
					"price",
					&ActionSuggestedPostSuccess::price,
					CreditsAmount(1, 750000000, CreditsType::Ton));
			},
		},
		{
			.id = 55,
			.name = "suggestedPostRefund",
			.type = mtpc_messageActionSuggestedPostRefund,
			.action = [] {
				using Flag = MTPDmessageActionSuggestedPostRefund::Flag;
				return MTP_messageActionSuggestedPostRefund(
					MTP_flags(Flag::f_payer_initiated));
			},
			.alternative = Alternative<ActionSuggestedPostRefund>(),
			.json = R"("id": 55, "date": "2023-12-31T03:55:00",
				"date_unixtime": "1703984100",
				"action": "suggested_post_refund", "user_initiated": true)",
			.html = "The user refunded the payment, post was deleted.",
		},
		{
			.id = 56,
			.name = "giftTon",
			.type = mtpc_messageActionGiftTon,
			.action = [] {
				return MTP_messageActionGiftTon(
					MTP_flags(0),
					MTP_string("USD"),
					MTP_long(2999),
					MTP_string("TON"),
					MTP_long(1500000000),
					MTPstring());
			},
			.alternative = Alternative<ActionGiftCredits>(),
			.json = R"("id": 56, "date": "2023-12-31T03:56:00",
				"date_unixtime": "1703984160", "action": "send_ton_gift",
				"cost": "$29.99", "amount_whole": 1, "amount_nano": 500000000)",
			.html = "Alice Example sent you a gift for $29.99: "
				"1.5 TON.",
		},
		{
			.id = 57,
			.name = "suggestBirthday",
			.type = mtpc_messageActionSuggestBirthday,
			.action = [] {
				return MTP_messageActionSuggestBirthday(MTP_birthday(
					MTP_flags(MTPDbirthday::Flag::f_year),
					MTP_int(15),
					MTP_int(3),
					MTP_int(1990)));
			},
			.alternative = Alternative<ActionSuggestBirthday>(),
			.json = R"("id": 57, "date": "2023-12-31T03:57:00",
				"date_unixtime": "1703984220", "action": "suggest_birthday",
				"day": 15, "month": 3, "year": 1990)",
			.html = "Alice Example suggests to add a date of birth: "
				"15 March 1990",
		},
	};
}

[[nodiscard]] std::vector<ActionRow> OwnershipActionRows() {
	return {
		{
			.id = 58,
			.name = "starGiftPurchaseOffer",
			.type = mtpc_messageActionStarGiftPurchaseOffer,
			.action = [] {
				return MTP_messageActionStarGiftPurchaseOffer(
					MTP_flags(PurchaseOfferFlag::f_accepted),
					UniqueGiftFixture(5058, 6058),
					MTP_starsAmount(MTP_long(40), MTP_int(0)),
					MTP_int(1704153600));
			},
			.alternative = Alternative<ActionStarGift>(),
			.json = R"("id": 58, "date": "2023-12-31T03:58:00",
				"date_unixtime": "1703984280",
				"action": "star_gift_purchase_offer", "gift_id": 5058,
				"price_amount_whole": "40", "price_amount_nano": "0",
				"price_currency": "Stars", "expires": 1704153600,
				"accepted": true, "declined": false)",
			.html = "An offer to buy Unique Cap #1 for 40 stars. "
				"This offer was accepted.",
			.extra = [](const Outputs &, const Message &parsed) {
				return ExpectOffer(parsed, {
					.offerPrice = CreditsAmount(40, 0, CreditsType::Stars),
					.offerExpireAt = 1704153600,
					.offer = true, .offerAccepted = true,
					.offerDeclined = false, .offerExpired = false,
				});
			},
		},
		{
			.id = 59,
			.name = "starGiftPurchaseOfferDeclined",
			.type = mtpc_messageActionStarGiftPurchaseOfferDeclined,
			.action = [] {
				return MTP_messageActionStarGiftPurchaseOfferDeclined(
					MTP_flags(OfferDeclinedFlag::f_expired),
					UniqueGiftFixture(5059, 6059),
					MTP_starsTonAmount(MTP_long(2500000000)));
			},
			.alternative = Alternative<ActionStarGift>(),
			.json = R"("id": 59, "date": "2023-12-31T03:59:00",
				"date_unixtime": "1703984340",
				"action": "star_gift_purchase_offer_declined", "gift_id": 5059,
				"price_amount_whole": "2", "price_amount_nano": "500000000",
				"price_currency": "TON", "expired": true)",
			.html = "Your offer to buy Unique Cap #1 for 2.5 TON has expired.",
			.extra = [](const Outputs &, const Message &parsed) {
				return ExpectOffer(parsed, {
					.offerPrice = CreditsAmount(2, 500000000, CreditsType::Ton),
					.offerExpireAt = 0,
					.offer = true, .offerAccepted = false,
					.offerDeclined = true, .offerExpired = true,
				});
			},
		},
		{
			.id = 60,
			.name = "newCreatorPending",
			.type = mtpc_messageActionNewCreatorPending,
			.action = [] {
				return MTP_messageActionNewCreatorPending(MTP_long(kBobId));
			},
			.alternative = Alternative<ActionNewCreatorPending>(),
			.json = R"("id": 60, "date": "2023-12-31T04:00:00",
				"date_unixtime": "1703984400", "action": "new_creator_pending",
				"new_creator": "Bob \"Quoted\" O'Example")",
			.html = "Bob &quot;Quoted&quot; O&apos;Example will become the new "
				"main admin in 7 days if Alice Example does not return",
		},
		{
			.id = 61,
			.name = "changeCreator",
			.type = mtpc_messageActionChangeCreator,
			.action = [] {
				return MTP_messageActionChangeCreator(MTP_long(kSelfId));
			},
			.alternative = Alternative<ActionChangeCreator>(),
			.json = R"("id": 61, "date": "2023-12-31T04:01:00",
				"date_unixtime": "1703984460", "action": "change_creator",
				"new_creator": "Self Example")",
			.html = "Alice Example made Self Example the new main admin "
				"of the group",
		},
		{
			.id = 62,
			.name = "noForwardsToggle",
			.type = mtpc_messageActionNoForwardsToggle,
			.action = [] {
				return MTP_messageActionNoForwardsToggle(
					MTP_boolFalse(),
					MTP_boolTrue());
			},
			.alternative = Alternative<ActionNoForwardsToggle>(),
			.json = R"("id": 62, "date": "2023-12-31T04:02:00",
				"date_unixtime": "1703984520", "action": "no_forwards_toggle",
				"new_value": true)",
			.html = "Alice Example disabled sharing in this chat",
		},
		{
			.id = 63,
			.name = "noForwardsRequest",
			.type = mtpc_messageActionNoForwardsRequest,
			.action = [] {
				using Flag = MTPDmessageActionNoForwardsRequest::Flag;
				return MTP_messageActionNoForwardsRequest(
					MTP_flags(Flag::f_expired),
					MTP_boolTrue(),
					MTP_boolFalse());
			},
			.alternative = Alternative<ActionNoForwardsRequest>(),
			.json = R"("id": 63, "date": "2023-12-31T04:03:00",
				"date_unixtime": "1703984580", "action": "no_forwards_request",
				"expired": true, "new_value": false)",
			.html = "Alice Example requested to enable sharing in this chat",
		},
		{
			.id = 64,
			.name = "pollAppendAnswer",
			.type = mtpc_messageActionPollAppendAnswer,
			.action = [] {
				return MTP_messageActionPollAppendAnswer(
					PollAnswerFixture("Option <A>"));
			},
			.alternative = Alternative<ActionPollAppendAnswer>(),
			.json = R"("id": 64, "date": "2023-12-31T04:04:00",
				"date_unixtime": "1703984640",
				"action": "poll_append_answer", "option": "Option <A>")",
			.html = "Alice Example added &quot;Option &lt;A&gt;&quot; "
				"to the poll.",
		},
		{
			.id = 65,
			.name = "pollDeleteAnswer",
			.type = mtpc_messageActionPollDeleteAnswer,
			.action = [] {
				return MTP_messageActionPollDeleteAnswer(
					PollAnswerFixture("Option <B>"));
			},
			.alternative = Alternative<ActionPollDeleteAnswer>(),
			.json = R"("id": 65, "date": "2023-12-31T04:05:00",
				"date_unixtime": "1703984700",
				"action": "poll_delete_answer", "option": "Option <B>")",
			.html = "Alice Example removed &quot;Option &lt;B&gt;&quot; "
				"from the poll.",
		},
	};
}

[[nodiscard]] std::vector<ActionRow> WalletActionRows() {
	return {
		{
			.id = 66,
			.name = "managedBotCreated",
			.type = mtpc_messageActionManagedBotCreated,
			.action = [] {
				return MTP_messageActionManagedBotCreated(
					MTP_long(kMissingId));
			},
			.alternative = Alternative<ActionManagedBotCreated>(),
			.json = R"("id": 66, "date": "2023-12-31T04:06:00",
				"date_unixtime": "1703984760",
				"action": "managed_bot_created", "bot": null)",
			.html = "Alice Example created a bot Deleted Account",
		},
		{
			.id = 67,
			.name = "changeCommunity",
			.type = mtpc_messageActionChangeCommunity,
			.action = [] {
				using Flag = MTPDmessageActionChangeCommunity::Flag;
				return MTP_messageActionChangeCommunity(
					MTP_flags(Flag::f_community_id),
					MTP_long(5067));
			},
			.alternative = Alternative<v::null_t>(),
			.output = ActionOutput::Fallback,
			.json = R"("id": 67, "date": "2023-12-31T04:07:00",
				"date_unixtime": "1703984820")",
		},
		{
			.id = 68,
			.name = "chatJoinedViaCommunity",
			.type = mtpc_messageActionChatJoinedViaCommunity,
			.action = [] {
				return MTP_messageActionChatJoinedViaCommunity(
					MTP_long(5068));
			},
			.alternative = Alternative<ActionChatJoinedViaCommunity>(),
			.json = R"("id": 68, "date": "2023-12-31T04:08:00",
				"date_unixtime": "1703984880",
				"action": "join_group_via_community", "community_id": 5068)",
			.html = "Alice Example joined group via a community",
		},
		{
			.id = 69,
			.name = "gramTransfer",
			.type = mtpc_messageActionGramTransfer,
			.action = [] {
				using Flag = MTPDmessageActionGramTransfer::Flag;
				return MTP_messageActionGramTransfer(
					MTP_flags(Flag::f_comment),
					MTP_long(2500000000),
					MTP_string("EQ-addr"),
					MTP_string("tx-69"),
					MTP_string("Thanks & bye"));
			},
			.alternative = Alternative<ActionGramTransfer>(),
			.json = R"("id": 69, "date": "2023-12-31T04:09:00",
				"date_unixtime": "1703984940", "action": "gram_transfer",
				"amount_whole": 2, "amount_nano": 500000000,
				"peer_address": "EQ-addr", "transaction_id": "tx-69",
				"comment_encrypted": false, "comment": "Thanks & bye")",
			.html = "Alice Example (EQ-addr) sent you 2.5 Grams, transaction "
				"tx-69, with comment: &laquo;Thanks &amp; bye&raquo;",
		},
		{
			.id = 70,
			.name = "walletTonConnectRequest",
			.type = mtpc_messageActionWalletTonConnectRequest,
			.action = [] {
				using Flag = MTPDmessageActionWalletTonConnectRequest::Flag;
				return MTP_messageActionWalletTonConnectRequest(
					MTP_flags(Flag::f_accepted
						| Flag::f_topic
						| Flag::f_trace_id),
					MTP_long(5070),
					MTP_int(1704153600),
					MTP_string("<T>"),
					MTP_string("trace-70"),
					MTPstring());
			},
			.alternative = Alternative<ActionWalletTonConnectRequest>(),
			.json = R"("id": 70, "date": "2023-12-31T04:10:00",
				"date_unixtime": "1703985000", "action": "ton_connect_request",
				"session_id": 5070, "expires": 1704153600, "topic": "<T>",
				"trace_id": "trace-70", "accepted": true, "declined": false)",
			.html = "You approved a TON Connect request "
				"&laquo;&lt;T&gt;&raquo;.",
		},
		{
			.id = 71,
			.name = "noPaidMessagesTokenUsed",
			.type = mtpc_messageActionNoPaidMessagesTokenUsed,
			.action = [] {
				return MTP_messageActionNoPaidMessagesTokenUsed();
			},
			.alternative = Alternative<ActionNoPaidMessagesTokenUsed>(),
			.json = R"("id": 71, "date": "2023-12-31T04:11:00",
				"date_unixtime": "1703985060",
				"action": "no_paid_messages_token_used")",
			.html = "Alice Example started a chat via a free messages link",
		},
		{
			.id = 72,
			.name = "empty",
			.type = mtpc_messageActionEmpty,
			.action = [] { return MTP_messageActionEmpty(); },
			.alternative = Alternative<v::null_t>(),
			.output = ActionOutput::Fallback,
			.json = R"("id": 72, "date": "2023-12-31T04:12:00",
				"date_unixtime": "1703985120")",
		},
		{
			.id = 73,
			.name = "phoneNumberRequest",
			.direct = PhoneNumberRequestMessage,
			.alternative = Alternative<ActionPhoneNumberRequest>(),
			.json = R"("id": 73, "date": "2023-12-31T04:13:00",
				"date_unixtime": "1703985180",
				"action": "requested_phone_number")",
			.html = "Alice Example requested your phone number",
		},
		{
			.id = 74,
			.name = "botAllowedApp",
			.type = mtpc_messageActionBotAllowed,
			.action = BotAllowedAppAction,
			.alternative = Alternative<ActionBotAllowed>(),
			.output = ActionOutput::Service,
			.json = R"("id": 74, "date": "2023-12-31T04:14:00",
				"date_unixtime": "1703985240",
				"action": "allow_sending_messages",
				"reason_app_id": 5074, "reason_app_name": "Mini <App>")",
			.html = "You allowed this bot to message you when you opened "
				"Mini &lt;App&gt;",
		},
	};
}

[[nodiscard]] std::vector<ActionRow> GiftOfferActionRows() {
	return {
		{
			.id = 75,
			.name = "starGiftPurchaseOfferOwnPending",
			.type = mtpc_messageActionStarGiftPurchaseOffer,
			.action = [] {
				return MTP_messageActionStarGiftPurchaseOffer(
					MTP_flags(0),
					StarGiftFixture(5075, 100, false),
					MTP_starsAmount(MTP_long(20), MTP_int(125000000)),
					MTP_int(1704240000));
			},
			.alternative = Alternative<ActionStarGift>(),
			.output = ActionOutput::Service,
			.json = R"("id": 75, "date": "2023-12-31T04:15:00",
				"date_unixtime": "1703985300", "actor": "Self Example",
				"actor_id": "user1000", "action": "star_gift_purchase_offer",
				"gift_id": 5075, "price_amount_whole": "20",
				"price_amount_nano": "125000000", "price_currency": "Stars",
				"expires": 1704240000, "accepted": false, "declined": false)",
			.html = "You offered 20.125 stars for a gift. "
				"This offer expires on 3 January 2024 at 03:00.",
			.fromId = kSelfId,
			.out = true,
		},
		{
			.id = 76,
			.name = "starGiftPurchaseOfferRejected",
			.type = mtpc_messageActionStarGiftPurchaseOffer,
			.action = [] {
				return MTP_messageActionStarGiftPurchaseOffer(
					MTP_flags(PurchaseOfferFlag::f_declined),
					UniqueGiftFixture(5076, 6076),
					MTP_starsTonAmount(MTP_long(3000000001)),
					MTP_int(0));
			},
			.alternative = Alternative<ActionStarGift>(),
			.json = R"("id": 76, "date": "2023-12-31T04:16:00",
				"date_unixtime": "1703985360",
				"action": "star_gift_purchase_offer", "gift_id": 5076,
				"price_amount_whole": "3", "price_amount_nano": "1",
				"price_currency": "TON", "expires": 0,
				"accepted": false, "declined": true)",
			.html = "An offer to buy Unique Cap #1 for 3.000000001 TON. "
				"This offer was rejected.",
		},
		{
			.id = 77,
			.name = "starGiftPurchaseOfferDeclinedOwnExpired",
			.type = mtpc_messageActionStarGiftPurchaseOfferDeclined,
			.action = [] {
				return MTP_messageActionStarGiftPurchaseOfferDeclined(
					MTP_flags(OfferDeclinedFlag::f_expired),
					UniqueGiftFixture(5077, 6077),
					MTP_starsAmount(MTP_long(75), MTP_int(0)));
			},
			.alternative = Alternative<ActionStarGift>(),
			.output = ActionOutput::Service,
			.json = R"("id": 77, "date": "2023-12-31T04:17:00",
				"date_unixtime": "1703985420", "actor": "Self Example",
				"actor_id": "user1000",
				"action": "star_gift_purchase_offer_declined", "gift_id": 5077,
				"price_amount_whole": "75", "price_amount_nano": "0",
				"price_currency": "Stars", "expired": true)",
			.html = "The offer from Fixture Group to buy your Unique Cap #1 "
				"for 75 stars has expired.",
			.fromId = kSelfId,
			.out = true,
		},
		{
			.id = 78,
			.name = "starGiftPurchaseOfferDeclinedOwnRejected",
			.type = mtpc_messageActionStarGiftPurchaseOfferDeclined,
			.action = [] {
				return MTP_messageActionStarGiftPurchaseOfferDeclined(
					MTP_flags(0),
					StarGiftFixture(5078, 120, true),
					MTP_starsTonAmount(MTP_long(750000000)));
			},
			.alternative = Alternative<ActionStarGift>(),
			.output = ActionOutput::Service,
			.json = R"("id": 78, "date": "2023-12-31T04:18:00",
				"date_unixtime": "1703985480", "actor": "Self Example",
				"actor_id": "user1000",
				"action": "star_gift_purchase_offer_declined", "gift_id": 5078,
				"price_amount_whole": "0", "price_amount_nano": "750000000",
				"price_currency": "TON", "expired": false)",
			.html = "You rejected Fixture Group&apos;s offer to buy your gift "
				"for 0.75 TON.",
			.fromId = kSelfId,
			.out = true,
		},
		{
			.id = 79,
			.name = "starGiftPurchaseOfferDeclinedRejected",
			.type = mtpc_messageActionStarGiftPurchaseOfferDeclined,
			.action = [] {
				return MTP_messageActionStarGiftPurchaseOfferDeclined(
					MTP_flags(0),
					UniqueGiftFixture(5079, 6079),
					MTP_starsAmount(MTP_long(50), MTP_int(0)));
			},
			.alternative = Alternative<ActionStarGift>(),
			.json = R"("id": 79, "date": "2023-12-31T04:19:00",
				"date_unixtime": "1703985540",
				"action": "star_gift_purchase_offer_declined", "gift_id": 5079,
				"price_amount_whole": "50", "price_amount_nano": "0",
				"price_currency": "Stars", "expired": false)",
			.html = "Alice Example rejected your offer to buy Unique Cap #1 "
				"for 50 stars.",
		},
	};
}

} // namespace

MTPPhoto PhotoFixture(
		BareId id,
		int width,
		int height,
		int size) {
	return MTP_photo(
		MTP_flags(0),
		MTP_long(id),
		MTP_long(0),
		MTP_bytes(),
		MTP_int(kActionDate),
		MTP_vector<MTPPhotoSize>(
			1,
			MTP_photoSize(
				MTP_string("x"),
				MTP_int(width),
				MTP_int(height),
				MTP_int(size))),
		MTPVector<MTPVideoSize>(),
		MTP_int(2));
}

Message AliceGroupMessage(int32 id, TimeId date) {
	auto result = Message();
	result.id = id;
	result.date = date;
	result.fromId = peerFromUser(UserId(kAliceId));
	result.peerId = peerFromChat(ChatId(kGroupId));
	result.selfId = peerFromUser(UserId(kSelfId));
	return result;
}

std::vector<ActionRow> ActionRows() {
	const auto families = {
		&ChatActionRows,
		&HistoryActionRows,
		&NoticeActionRows,
		&ChatSettingsActionRows,
		&TopicActionRows,
		&PaymentActionRows,
		&GiftActionRows,
		&TodoActionRows,
		&OwnershipActionRows,
		&WalletActionRows,
		&GiftOfferActionRows,
	};
	auto result = std::vector<ActionRow>();
	for (const auto family : families) {
		auto rows = family();
		result.insert(end(result), begin(rows), end(rows));
	}
	return result;
}

ActionExecution Execution(const ActionRow &row) {
	return !row.direct
		? ActionExecution::Parsed
		: ActionExecution::WriterOnly;
}

SliceFixture ActionSlice() {
	auto result = SliceFixture();
	for (const auto &row : ActionRows()) {
		if (Execution(row) == ActionExecution::Parsed) {
			result.wire.push_back(ServiceMessage(row));
		} else {
			result.direct.push_back(row.direct);
		}
		if (row.skip != File::SkipReason::None) {
			result.skips.emplace(row.id, row.skip);
		}
	}
	return result;
}

QString CheckActionRow(
		const Outputs &outputs,
		const ActionRow &row) {
	const auto parsed = ParsedMessage(outputs, kGroupDialog, row.id);
	if (!parsed) {
		return u"message %1 is not parsed exactly once"_q.arg(row.id);
	}
	return FirstFailure({
		ExpectNumber(
			u"parsed variant"_q,
			qint64(parsed->action.content.index()),
			qint64(row.alternative)),
		ExpectObject(
			JsonMessage(outputs, kGroupDialog, row.id),
			ExpectedActionJson(row)),
		CheckActionHtml(outputs, row),
		row.extra ? row.extra(outputs, *parsed) : QString(),
	});
}

QString CheckActionInventory(const Outputs &) {
	using Content = decltype(ServiceAction::content);
	const auto rows = ActionRows();
	const auto slice = ActionSlice();
	const auto null = Alternative<v::null_t>();
	auto ids = std::set<int32>();
	auto constructors = std::set<ConstructorId>();
	auto alternatives = std::set<std::size_t>();
	auto writerOnly = QStringList();
	auto fixtures = 0;
	auto nulls = 0;
	auto mismatch = QString();
	for (const auto &row : rows) {
		ids.emplace(row.id);
		alternatives.emplace(row.alternative);
		if (Execution(row) == ActionExecution::WriterOnly) {
			writerOnly.push_back(QString::fromLatin1(row.name));
		}
		if (!row.action) {
			continue;
		}
		const auto built = row.action().type();
		if (built != mtpTypeId(row.type) && mismatch.isEmpty()) {
			mismatch = u"%1 builds constructor 0x%2"_q.arg(
				QString::fromLatin1(row.name),
				QString::number(built, 16));
		}
		++fixtures;
		constructors.emplace(row.type);
		nulls += (row.alternative == null) ? 1 : 0;
	}
	return FirstFailure({
		mismatch,
		ExpectNumber(u"fixture ids"_q, int(ids.size()), int(rows.size())),
		ExpectNames(u"writer-only rows"_q, writerOnly, {
			u"phoneNumberRequest"_q,
		}),
		ExpectNumber(u"TL fixtures"_q, fixtures, 78),
		ExpectNumber(u"TL constructors"_q, int(constructors.size()), 72),
		ExpectNumber(u"parsed TL fixtures"_q, int(slice.wire.size()), 78),
		ExpectNumber(u"direct fixtures"_q, int(slice.direct.size()), 1),
		ExpectNumber(u"null fallbacks"_q, nulls, 6),
		ExpectNumber(
			u"ServiceAction alternatives"_q,
			qint64(std::variant_size_v<Content>),
			63),
		ExpectNumber(
			u"covered alternatives"_q,
			int(alternatives.size()),
			63),
	});
}

} // namespace Export::Tests
