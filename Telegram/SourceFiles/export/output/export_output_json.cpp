/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_json.h"

#include "export/output/export_output_result.h"
#include "export/data/export_data_types.h"
#include "core/utils.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>

namespace Export {
namespace Output {
namespace {

using Context = details::JsonContext;

QByteArray SerializeString(const QByteArray &value) {
	const auto size = value.size();
	const auto begin = value.data();
	const auto end = begin + size;

	auto result = QByteArray();
	result.reserve(2 + size * 4);
	result.append('"');
	for (auto p = begin; p != end; ++p) {
		const auto ch = *p;
		if (ch == '\n') {
			result.append("\\n", 2);
		} else if (ch == '\r') {
			result.append("\\r", 2);
		} else if (ch == '\t') {
			result.append("\\t", 2);
		} else if (ch == '"') {
			result.append("\\\"", 2);
		} else if (ch == '\\') {
			result.append("\\\\", 2);
		} else if (ch >= 0 && ch < 32) {
			result.append("\\x", 2).append('0' + (ch >> 4));
			const auto left = (ch & 0x0F);
			if (left >= 10) {
				result.append('A' + (left - 10));
			} else {
				result.append('0' + left);
			}
		} else if (ch == char(0xE2)
			&& (p + 2 < end)
			&& *(p + 1) == char(0x80)) {
			if (*(p + 2) == char(0xA8)) { // Line separator.
				result.append("\\u2028", 6);
			} else if (*(p + 2) == char(0xA9)) { // Paragraph separator.
				result.append("\\u2029", 6);
			} else {
				result.append(ch);
			}
		} else {
			result.append(ch);
		}
	}
	result.append('"');
	return result;
}

QByteArray SerializeDate(TimeId date) {
	return SerializeString(
		QDateTime::fromSecsSinceEpoch(date).toString(Qt::ISODate).toUtf8());
}

QByteArray SerializeDateRaw(TimeId date) {
	return SerializeString(QString::number(date).toUtf8());
}

QByteArray StringAllowEmpty(const Data::Utf8String &data) {
	return data.isEmpty() ? data : SerializeString(data);
}

QByteArray StringAllowNull(const Data::Utf8String &data) {
	return data.isEmpty() ? QByteArray("null") : SerializeString(data);
}

QByteArray Indentation(int size) {
	return QByteArray(size, ' ');
}

QByteArray Indentation(const Context &context) {
	return Indentation(context.nesting.size());
}

QByteArray SerializeObject(
		Context &context,
		const std::vector<std::pair<QByteArray, QByteArray>> &values) {
	const auto indent = Indentation(context);

	context.nesting.push_back(Context::kObject);
	const auto guard = gsl::finally([&] { context.nesting.pop_back(); });
	const auto next = '\n' + Indentation(context);

	auto first = true;
	auto result = QByteArray();
	result.append('{');
	for (const auto &[key, value] : values) {
		if (value.isEmpty()) {
			continue;
		}
		if (first) {
			first = false;
		} else {
			result.append(',');
		}
		result.append(next).append(SerializeString(key)).append(": ", 2);
		result.append(value);
	}
	result.append('\n').append(indent).append("}");
	return result;
}

QByteArray SerializeArray(
		Context &context,
		const std::vector<QByteArray> &values) {
	const auto indent = Indentation(context.nesting.size());
	const auto next = '\n' + Indentation(context.nesting.size() + 1);

	auto first = true;
	auto result = QByteArray();
	result.append('[');
	for (const auto &value : values) {
		if (first) {
			first = false;
		} else {
			result.append(',');
		}
		result.append(next).append(value);
	}
	result.append('\n').append(indent).append("]");
	return result;
}

QByteArray SerializeText(
		Context &context,
		const std::vector<Data::TextPart> &data,
		bool serializeToObjects = false) {
	using Type = Data::TextPart::Type;

	if (data.empty()) {
		return serializeToObjects ? QByteArray("[]") : SerializeString("");
	}

	context.nesting.push_back(Context::kArray);

	const auto text = ranges::views::all(
		data
	) | ranges::views::transform([&](const Data::TextPart &part) {
		if ((part.type == Type::Text) && !serializeToObjects) {
			return SerializeString(part.text);
		}
		const auto typeString = [&] {
			switch (part.type) {
			case Type::Unknown: return "unknown";
			case Type::Mention: return "mention";
			case Type::Hashtag: return "hashtag";
			case Type::BotCommand: return "bot_command";
			case Type::Url: return "link";
			case Type::Email: return "email";
			case Type::Bold: return "bold";
			case Type::Italic: return "italic";
			case Type::Code: return "code";
			case Type::Pre: return "pre";
			case Type::Text: return "plain";
			case Type::TextUrl: return "text_link";
			case Type::MentionName: return "mention_name";
			case Type::Phone: return "phone";
			case Type::Cashtag: return "cashtag";
			case Type::Underline: return "underline";
			case Type::Strike: return "strikethrough";
			case Type::Blockquote: return "blockquote";
			case Type::BankCard: return "bank_card";
			case Type::Spoiler: return "spoiler";
			case Type::CustomEmoji: return "custom_emoji";
			}
			Unexpected("Type in SerializeText.");
		}();
		const auto additionalName = (part.type == Type::MentionName)
			? "user_id"
			: (part.type == Type::CustomEmoji)
			? "document_id"
			: (part.type == Type::Pre)
			? "language"
			: (part.type == Type::TextUrl)
			? "href"
			: (part.type == Type::Blockquote)
			? "collapsed"
			: "none";
		const auto additionalValue = (part.type == Type::MentionName)
			? part.additional
			: (part.type == Type::Pre
				|| part.type == Type::TextUrl
				|| part.type == Type::CustomEmoji)
			? SerializeString(part.additional)
			: (part.type == Type::Blockquote)
			? (part.additional.isEmpty() ? "false" : "true")
			: QByteArray();
		return SerializeObject(context, {
			{ "type", SerializeString(typeString) },
			{ "text", SerializeString(part.text) },
			{ additionalName, additionalValue },
		});
	}) | ranges::to_vector;

	context.nesting.pop_back();

	if (!serializeToObjects) {
		if (data.size() == 1 && data[0].type == Data::TextPart::Type::Text) {
			return text[0];
		}
	}
	return SerializeArray(context, text);
}

Data::Utf8String FormatUsername(const Data::Utf8String &username) {
	return username.isEmpty() ? username : ('@' + username);
}

QByteArray FormatFilePath(const Data::File &file) {
	return file.relativePath.toUtf8();
}

QByteArray SerializeMessage(
		Context &context,
		const Data::Message &message,
		const std::map<PeerId, Data::Peer> &peers,
		const QString &internalLinksDomain) {
	using namespace Data;

	if (v::is<UnsupportedMedia>(message.media.content)) {
		return SerializeObject(context, {
			{ "id", Data::NumberToString(message.id) },
			{ "type", SerializeString("unsupported") }
		});
	}

	const auto peer = [&](PeerId peerId) -> const Peer& {
		if (const auto i = peers.find(peerId); i != end(peers)) {
			return i->second;
		}
		static auto empty = Peer{ User() };
		return empty;
	};
	const auto user = [&](UserId userId) -> const User& {
		if (const auto result = peer(userId).user()) {
			return *result;
		}
		static auto empty = User();
		return empty;
	};

	auto values = std::vector<std::pair<QByteArray, QByteArray>>{
	{ "id", NumberToString(message.id) },
	{
		"type",
		SerializeString(!v::is_null(message.action.content)
			? "service"
			: "message")
	},
	{ "date", SerializeDate(message.date) },
	{ "date_unixtime", SerializeDateRaw(message.date) },
	};
	context.nesting.push_back(Context::kObject);
	const auto serialized = [&] {
		context.nesting.pop_back();
		return SerializeObject(context, values);
	};

	const auto pushBare = [&](
			const QByteArray &key,
			const QByteArray &value) {
		if (!value.isEmpty()) {
			values.emplace_back(key, value);
		}
	};
	if (message.edited) {
		pushBare("edited", SerializeDate(message.edited));
		pushBare("edited_unixtime", SerializeDateRaw(message.edited));
	}

	const auto wrapPeerId = [&](PeerId peerId) {
		if (const auto chat = peerToChat(peerId)) {
			return SerializeString("chat"
				+ Data::NumberToString(chat.bare));
		} else if (const auto channel = peerToChannel(peerId)) {
			return SerializeString("channel"
				+ Data::NumberToString(channel.bare));
		}
		return SerializeString("user"
			+ Data::NumberToString(peerToUser(peerId).bare));
	};
	const auto push = [&](const QByteArray &key, const auto &value) {
		using V = std::decay_t<decltype(value)>;
		if constexpr (std::is_same_v<V, bool>) {
			pushBare(key, value ? "true" : "false");
		} else if constexpr (std::is_arithmetic_v<V>) {
			pushBare(key, Data::NumberToString(value));
		} else if constexpr (std::is_same_v<V, PeerId>) {
			pushBare(key, wrapPeerId(value));
		} else {
			const auto wrapped = QByteArray(value);
			if (!wrapped.isEmpty()) {
				pushBare(key, SerializeString(wrapped));
			}
		}
	};
	const auto wrapPeerName = [&](PeerId peerId) {
		return StringAllowNull(peer(peerId).name());
	};
	const auto wrapUserName = [&](UserId userId) {
		return StringAllowNull(user(userId).name());
	};
	const auto pushFrom = [&](const QByteArray &label = "from") {
		if (message.fromId) {
			pushBare(label, wrapPeerName(message.fromId));
			push(label + "_id", message.fromId);
		}
	};
	const auto pushReplyToMsgId = [&](
			const QByteArray &label = "reply_to_message_id") {
		if (message.replyToMsgId) {
			push(label, message.replyToMsgId);
			if (message.replyToPeerId) {
				push("reply_to_peer_id", message.replyToPeerId);
			}
		}
	};
	const auto pushUserNames = [&](
			const std::vector<UserId> &data,
			const QByteArray &label = "members") {
		auto list = std::vector<QByteArray>();
		for (const auto &userId : data) {
			list.push_back(wrapUserName(userId));
		}
		pushBare(label, SerializeArray(context, list));
	};
	const auto pushActor = [&] {
		pushFrom("actor");
	};
	const auto pushAction = [&](const QByteArray &action) {
		push("action", action);
	};
	const auto pushTTL = [&](
			const QByteArray &label = "self_destruct_period_seconds") {
		if (const auto ttl = message.media.ttl) {
			push(label, ttl);
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
				return pre + "(File unavailable, please try again later)";
			case SkipReason::FileSize:
				return pre + "(File exceeds maximum size. "
					"Change data exporting settings to download.)";
			case SkipReason::FileType:
				return pre + "(File not included. "
					"Change data exporting settings to download.)";
			case SkipReason::None: return FormatFilePath(file);
			}
			Unexpected("Skip reason while writing file path.");
		}());
	};
	const auto pushPhoto = [&](const Image &image) {
		pushPath(image.file, "photo");
		push("photo_file_size", image.file.size);
		if (image.width && image.height) {
			push("width", image.width);
			push("height", image.height);
		}
	};
	const auto pushSpoiler = [&](const auto &media) {
		if (media.spoilered) {
			push("media_spoiler", true);
		}
	};

	v::match(message.action.content, [&](const ActionChatCreate &data) {
		pushActor();
		pushAction("create_group");
		push("title", data.title);
		pushUserNames(data.userIds);
	}, [&](const ActionChatEditTitle &data) {
		pushActor();
		pushAction("edit_group_title");
		push("title", data.title);
	}, [&](const ActionChatEditPhoto &data) {
		pushActor();
		pushAction("edit_group_photo");
		pushPhoto(data.photo.image);
		pushSpoiler(data.photo);
	}, [&](const ActionChatDeletePhoto &data) {
		pushActor();
		pushAction("delete_group_photo");
	}, [&](const ActionChatAddUser &data) {
		pushActor();
		pushAction("invite_members");
		pushUserNames(data.userIds);
	}, [&](const ActionChatDeleteUser &data) {
		pushActor();
		pushAction("remove_members");
		pushUserNames({ data.userId });
	}, [&](const ActionChatJoinedByLink &data) {
		pushActor();
		pushAction("join_group_by_link");
		pushBare("inviter", wrapUserName(data.inviterId));
	}, [&](const ActionChannelCreate &data) {
		pushActor();
		pushAction("create_channel");
		push("title", data.title);
	}, [&](const ActionChatMigrateTo &data) {
		pushActor();
		pushAction("migrate_to_supergroup");
	}, [&](const ActionChannelMigrateFrom &data) {
		pushActor();
		pushAction("migrate_from_group");
		push("title", data.title);
	}, [&](const ActionPinMessage &data) {
		pushActor();
		pushAction("pin_message");
		pushReplyToMsgId("message_id");
	}, [&](const ActionHistoryClear &data) {
		pushActor();
		pushAction("clear_history");
	}, [&](const ActionGameScore &data) {
		pushActor();
		pushAction("score_in_game");
		pushReplyToMsgId("game_message_id");
		push("score", data.score);
	}, [&](const ActionPaymentSent &data) {
		pushAction("send_payment");
		push("amount", data.amount);
		push("currency", data.currency);
		pushReplyToMsgId("invoice_message_id");
		if (data.recurringUsed) {
			push("recurring", "used");
		} else if (data.recurringInit) {
			push("recurring", "init");
		}
	}, [&](const ActionPhoneCall &data) {
		pushActor();
		pushAction("phone_call");
		if (data.duration) {
			push("duration_seconds", data.duration);
		}
		using Reason = ActionPhoneCall::DiscardReason;
		push("discard_reason", [&] {
			switch (data.discardReason) {
			case Reason::Busy: return "busy";
			case Reason::Disconnect: return "disconnect";
			case Reason::Hangup: return "hangup";
			case Reason::Missed: return "missed";
			}
			return "";
		}());
	}, [&](const ActionScreenshotTaken &data) {
		pushActor();
		pushAction("take_screenshot");
	}, [&](const ActionCustomAction &data) {
		pushActor();
		push("information_text", data.message);
	}, [&](const ActionBotAllowed &data) {
		if (data.attachMenu) {
			pushAction("attach_menu_bot_allowed");
		} else if (data.fromRequest) {
			pushAction("web_app_bot_allowed");
		} else if (data.appId) {
			pushAction("allow_sending_messages");
			push("reason_app_id", data.appId);
			push("reason_app_name", data.app);
		} else {
			pushAction("allow_sending_messages");
			push("reason_domain", data.domain);
		}
	}, [&](const ActionSecureValuesSent &data) {
		pushAction("send_passport_values");
		auto list = std::vector<QByteArray>();
		for (const auto type : data.types) {
			list.push_back(SerializeString([&] {
				using Type = ActionSecureValuesSent::Type;
				switch (type) {
				case Type::PersonalDetails: return "personal_details";
				case Type::Passport: return "passport";
				case Type::DriverLicense: return "driver_license";
				case Type::IdentityCard: return "identity_card";
				case Type::InternalPassport: return "internal_passport";
				case Type::Address: return "address_information";
				case Type::UtilityBill: return "utility_bill";
				case Type::BankStatement: return "bank_statement";
				case Type::RentalAgreement: return "rental_agreement";
				case Type::PassportRegistration:
					return "passport_registration";
				case Type::TemporaryRegistration:
					return "temporary_registration";
				case Type::Phone: return "phone_number";
				case Type::Email: return "email";
				}
				return "";
			}()));
		}
		pushBare("values", SerializeArray(context, list));
	}, [&](const ActionContactSignUp &data) {
		pushActor();
		pushAction("joined_telegram");
	}, [&](const ActionGeoProximityReached &data) {
		pushAction("proximity_reached");
		if (data.fromId) {
			pushBare("from", wrapPeerName(data.fromId));
			push("from_id", data.fromId);
		}
		if (data.toId) {
			pushBare("to", wrapPeerName(data.toId));
			push("to_id", data.toId);
		}
		push("distance", data.distance);
	}, [&](const ActionPhoneNumberRequest &data) {
		pushActor();
		pushAction("requested_phone_number");
	}, [&](const ActionGroupCall &data) {
		pushActor();
		pushAction("group_call");
		if (data.duration) {
			push("duration", data.duration);
		}
	}, [&](const ActionInviteToGroupCall &data) {
		pushActor();
		pushAction("invite_to_group_call");
		pushUserNames(data.userIds);
	}, [&](const ActionSetMessagesTTL &data) {
		pushActor();
		pushAction("set_messages_ttl");
		push("period", data.period);
	}, [&](const ActionGroupCallScheduled &data) {
		pushActor();
		pushAction("group_call_scheduled");
		push("schedule_date", data.date);
	}, [&](const ActionSetChatTheme &data) {
		pushActor();
		pushAction("edit_chat_theme");
		if (!data.emoji.isEmpty()) {
			push("emoticon", data.emoji.toUtf8());
		}
	}, [&](const ActionChatJoinedByRequest &data) {
		pushActor();
		pushAction("join_group_by_request");
	}, [&](const ActionWebViewDataSent &data) {
		pushAction("send_webview_data");
		push("text", data.text);
	}, [&](const ActionGiftPremium &data) {
		pushActor();
		pushAction("send_premium_gift");
		if (!data.cost.isEmpty()) {
			push("cost", data.cost);
		}
		if (data.months) {
			push("months", data.months);
		}
	}, [&](const ActionTopicCreate &data) {
		pushActor();
		pushAction("topic_created");
		push("title", data.title);
	}, [&](const ActionTopicEdit &data) {
		pushActor();
		pushAction("topic_edit");
		if (!data.title.isEmpty()) {
			push("new_title", data.title);
		}
		if (data.iconEmojiId) {
			push("new_icon_emoji_id", *data.iconEmojiId);
		}
	}, [&](const ActionSuggestProfilePhoto &data) {
		pushActor();
		pushAction("suggest_profile_photo");
		pushPhoto(data.photo.image);
		pushSpoiler(data.photo);
	}, [&](const ActionRequestedPeer &data) {
		pushActor();
		pushAction("requested_peer");
		push("button_id", data.buttonId);
		auto values = std::vector<QByteArray>();
		for (const auto &one : data.peers) {
			values.push_back(Data::NumberToString(one.value));
		}
		push("peers", SerializeArray(context, values));
	}, [&](const ActionGiftCode &data) {
		pushAction("gift_code_prize");
		push("gift_code", data.code);
		if (data.boostPeerId) {
			push("boost_peer_id", data.boostPeerId);
		}
		push("months", data.months);
		push("unclaimed", data.unclaimed);
		push("via_giveaway", data.viaGiveaway);
	}, [&](const ActionGiveawayLaunch &data) {
		pushAction("giveaway_launch");
	}, [&](const ActionGiveawayResults &data) {
		pushAction("giveaway_results");
		push("winners", data.winners);
		push("unclaimed", data.unclaimed);
		push("stars", data.credits);
	}, [&](const ActionSetChatWallPaper &data) {
		pushActor();
		pushAction(data.same
			? "set_same_chat_wallpaper"
			: "set_chat_wallpaper");
		pushReplyToMsgId("message_id");
	}, [&](const ActionBoostApply &data) {
		pushActor();
		pushAction("boost_apply");
		push("boosts", data.boosts);
	}, [&](const ActionPaymentRefunded &data) {
		pushAction("refunded_payment");
		push("amount", data.amount);
		push("currency", data.currency);
		pushBare("peer_name", wrapPeerName(data.peerId));
		push("peer_id", data.peerId);
		push("charge_id", data.transactionId);
	}, [&](const ActionGiftStars &data) {
		pushActor();
		pushAction("send_stars_gift");
		if (!data.cost.isEmpty()) {
			push("cost", data.cost);
		}
		if (data.credits) {
			push("stars", data.credits);
		}
	}, [&](const ActionPrizeStars &data) {
		pushActor();
		pushAction("stars_prize");
		push("boost_peer_id", data.peerId);
		pushBare("boost_peer_name", wrapPeerName(data.peerId));
		push("stars", data.amount);
		push("is_unclaimed", data.isUnclaimed);
		push("giveaway_msg_id", data.giveawayMsgId);
		push("transaction_id", data.transactionId);
	}, [&](const ActionStarGift &data) {
		pushActor();
		pushAction("send_star_gift");
		push("gift_id", data.giftId);
		push("stars", data.stars);
		push("is_limited", data.limited);
		push("is_anonymous", data.anonymous);
		pushBare("gift_text", SerializeText(context, data.text));
	}, [](v::null_t) {});

	if (v::is_null(message.action.content)) {
		pushFrom();
		push("author", message.signature);
		if (message.forwardedFromId) {
			pushBare(
				"forwarded_from",
				wrapPeerName(message.forwardedFromId));
		} else if (!message.forwardedFromName.isEmpty()) {
			pushBare(
				"forwarded_from",
				StringAllowNull(message.forwardedFromName));
		}
		if (message.savedFromChatId) {
			pushBare("saved_from", wrapPeerName(message.savedFromChatId));
		}
		pushReplyToMsgId();
		if (message.viaBotId) {
			const auto username = FormatUsername(
				user(message.viaBotId).username);
			if (!username.isEmpty()) {
				push("via_bot", username);
			}
		}
	}

	v::match(message.media.content, [&](const Photo &photo) {
		pushPhoto(photo.image);
		pushSpoiler(photo);
		pushTTL();
	}, [&](const Document &data) {
		pushPath(data.file, "file");
		push("file_name", data.name);
		push("file_size", data.file.size);
		if (data.thumb.width > 0) {
			pushPath(data.thumb.file, "thumbnail");
			push("thumbnail_file_size", data.thumb.file.size);
		}
		const auto pushType = [&](const QByteArray &value) {
			push("media_type", value);
		};
		if (data.isSticker) {
			pushType("sticker");
			push("sticker_emoji", data.stickerEmoji);
		} else if (data.isVideoMessage) {
			pushType("video_message");
		} else if (data.isVoiceMessage) {
			pushType("voice_message");
		} else if (data.isAnimated) {
			pushType("animation");
		} else if (data.isVideoFile) {
			pushType("video_file");
		} else if (data.isAudioFile) {
			pushType("audio_file");
			push("performer", data.songPerformer);
			push("title", data.songTitle);
		}
		push("mime_type", data.mime);
		if (data.duration) {
			push("duration_seconds", data.duration);
		}
		if (data.width && data.height) {
			push("width", data.width);
			push("height", data.height);
		}
		pushSpoiler(data);
		pushTTL();
	}, [&](const SharedContact &data) {
		pushBare("contact_information", SerializeObject(context, {
			{ "first_name", SerializeString(data.info.firstName) },
			{ "last_name", SerializeString(data.info.lastName) },
			{
				"phone_number",
				SerializeString(FormatPhoneNumber(data.info.phoneNumber))
			}
		}));
		if (!data.vcard.content.isEmpty()) {
			pushPath(data.vcard, "contact_vcard");
			push("contact_vcard_file_size", data.vcard.size);
		}
	}, [&](const GeoPoint &data) {
		pushBare(
			"location_information",
			data.valid ? SerializeObject(context, {
			{ "latitude", NumberToString(data.latitude) },
			{ "longitude", NumberToString(data.longitude) },
			}) : QByteArray("null"));
		pushTTL("live_location_period_seconds");
	}, [&](const Venue &data) {
		push("place_name", data.title);
		push("address", data.address);
		if (data.point.valid) {
			pushBare("location_information", SerializeObject(context, {
				{ "latitude", NumberToString(data.point.latitude) },
				{ "longitude", NumberToString(data.point.longitude) },
			}));
		}
	}, [&](const Game &data) {
		push("game_title", data.title);
		push("game_description", data.description);
		if (data.botId != 0 && !data.shortName.isEmpty()) {
			const auto bot = user(data.botId);
			if (bot.isBot && !bot.username.isEmpty()) {
				push("game_link", internalLinksDomain.toUtf8()
					+ bot.username
					+ "?game="
					+ data.shortName);
			}
		}
	}, [&](const Invoice &data) {
		push("invoice_information", SerializeObject(context, {
			{ "title", SerializeString(data.title) },
			{ "description", SerializeString(data.description) },
			{ "amount", NumberToString(data.amount) },
			{ "currency", SerializeString(data.currency) },
			{ "receipt_message_id", (data.receiptMsgId
				? NumberToString(data.receiptMsgId)
				: QByteArray()) }
		}));
	}, [&](const Poll &data) {
		context.nesting.push_back(Context::kObject);
		const auto answers = ranges::views::all(
			data.answers
		) | ranges::views::transform([&](const Poll::Answer &answer) {
			context.nesting.push_back(Context::kArray);
			auto result = SerializeObject(context, {
				{ "text", SerializeString(answer.text) },
				{ "voters", NumberToString(answer.votes) },
				{ "chosen", answer.my ? "true" : "false" },
			});
			context.nesting.pop_back();
			return result;
		}) | ranges::to_vector;
		const auto serialized = SerializeArray(context, answers);
		context.nesting.pop_back();

		pushBare("poll", SerializeObject(context, {
			{ "question", SerializeString(data.question) },
			{ "closed", data.closed ? "true" : "false" },
			{ "total_voters", NumberToString(data.totalVotes) },
			{ "answers", serialized }
		}));
	}, [&](const GiveawayStart &data) {
		context.nesting.push_back(Context::kArray);
		const auto channels = ranges::views::all(
			data.channels
		) | ranges::views::transform([&](ChannelId id) {
			return NumberToString(id.bare);
		}) | ranges::to_vector;
		const auto serialized = SerializeArray(context, channels);
		context.nesting.pop_back();

		context.nesting.push_back(Context::kArray);
		const auto countries = ranges::views::all(
			data.countries
		) | ranges::views::transform([&](const QString &code) {
			return SerializeString(code.toUtf8());
		}) | ranges::to_vector;
		const auto serializedCountries = SerializeArray(context, countries);
		context.nesting.pop_back();

		const auto additionalPrize = data.additionalPrize.toUtf8();

		pushBare("giveaway_information", SerializeObject(context, {
			{ "quantity", NumberToString(data.quantity) },
			{ "months", NumberToString(data.months) },
			{ "until_date", SerializeDate(data.untilDate) },
			{ "channels", serialized },
			{ "countries", serializedCountries },
			{ "additional_prize", SerializeString(additionalPrize) },
			{ "stars", NumberToString(data.credits) },
			{ "is_only_new_subscribers", (!data.all) ? "true" : "false" },
		}));
	}, [&](const GiveawayResults &data) {
		context.nesting.push_back(Context::kArray);
		const auto winners = ranges::views::all(
			data.winners
		) | ranges::views::transform([&](PeerId id) {
			return NumberToString(id.value);
		}) | ranges::to_vector;
		const auto serialized = SerializeArray(context, winners);
		context.nesting.pop_back();

		const auto additionalPrize = data.additionalPrize.toUtf8();
		const auto peersCount = data.additionalPeersCount;

		pushBare("giveaway_results", SerializeObject(context, {
			{ "channel", NumberToString(data.channel.bare) },
			{ "winners", serialized },
			{ "additional_prize", SerializeString(additionalPrize) },
			{ "until_date", SerializeDate(data.untilDate) },
			{ "launch_message_id", NumberToString(data.launchId) },
			{ "additional_peers_count", NumberToString(peersCount) },
			{ "winners_count", NumberToString(data.winnersCount) },
			{ "unclaimed_count", NumberToString(data.unclaimedCount) },
			{ "months", NumberToString(data.months) },
			{ "stars", NumberToString(data.credits) },
			{ "is_refunded", data.refunded ? "true" : "false" },
			{ "is_only_new_subscribers", (!data.all) ? "true" : "false" },
		}));
	}, [&](const PaidMedia &data) {
		push("paid_stars_amount", data.stars);
	}, [](const UnsupportedMedia &data) {
		Unexpected("Unsupported message.");
	}, [](v::null_t) {});

	pushBare("text", SerializeText(context, message.text));
	pushBare("text_entities", SerializeText(context, message.text, true));

	if (!message.inlineButtonRows.empty()) {
		const auto serializeRow = [&](
				const std::vector<HistoryMessageMarkupButton> &row) {
			context.nesting.push_back(Context::kArray);
			const auto buttons = ranges::views::all(
				row
			) | ranges::views::transform([&](
					const HistoryMessageMarkupButton &entry) {
				auto pairs = std::vector<std::pair<QByteArray, QByteArray>>();
				pairs.push_back({
					"type",
					SerializeString(
						HistoryMessageMarkupButton::TypeToString(entry)),
				});
				if (!entry.text.isEmpty()) {
					pairs.push_back({
						"text",
						SerializeString(entry.text.toUtf8()),
					});
				}
				if (!entry.data.isEmpty()) {
					using Type = HistoryMessageMarkupButton::Type;
					const auto isCallback = (entry.type == Type::Callback)
						|| (entry.type == Type::CallbackWithPassword);
					const auto data = isCallback
						? entry.data.toBase64(QByteArray::Base64UrlEncoding
							| QByteArray::OmitTrailingEquals)
						: entry.data;
					if (isCallback) {
						pairs.push_back({
							"dataBase64",
							SerializeString(data),
						});
						pairs.push_back({ "data", SerializeString({}) });
					} else {
						pairs.push_back({ "data", SerializeString(data) });
					}
				}
				if (!entry.forwardText.isEmpty()) {
					pairs.push_back({
						"forward_text",
						SerializeString(entry.forwardText.toUtf8()),
					});
				}
				if (entry.buttonId) {
					pairs.push_back({
						"button_id",
						NumberToString(entry.buttonId),
					});
				}
				return SerializeObject(context, pairs);
			}) | ranges::to_vector;
			context.nesting.pop_back();
			return SerializeArray(context, buttons);
		};
		context.nesting.push_back(Context::kArray);
		const auto rows = ranges::views::all(
			message.inlineButtonRows
		) | ranges::views::transform(serializeRow) | ranges::to_vector;
		context.nesting.pop_back();
		pushBare("inline_bot_buttons", SerializeArray(context, rows));
	}

	if (!message.reactions.empty()) {
		const auto serializeReaction = [&](const Reaction &reaction) {
			context.nesting.push_back(Context::kObject);
			const auto guard = gsl::finally([&] {
				context.nesting.pop_back();
			});

			auto pairs = std::vector<std::pair<QByteArray, QByteArray>>();
			pairs.push_back({
				"type",
				SerializeString(Reaction::TypeToString(reaction)),
			});
			pairs.push_back({
				"count",
				NumberToString(reaction.count),
			});
			switch (reaction.type) {
				case Reaction::Type::Emoji:
					pairs.push_back({
						"emoji",
						SerializeString(reaction.emoji.toUtf8()),
					});
					break;
				case Reaction::Type::CustomEmoji:
					pairs.push_back({
						"document_id",
						SerializeString(reaction.documentId),
					});
					break;
			}

			if (!reaction.recent.empty()) {
				context.nesting.push_back(Context::kArray);
				const auto recents = ranges::views::all(
					reaction.recent
				) | ranges::views::transform([&](
						const Reaction::Recent &recent) {
					context.nesting.push_back(Context::kArray);
					const auto guard = gsl::finally([&] {
						context.nesting.pop_back();
					});
					return SerializeObject(context, {
						{ "from", wrapPeerName(recent.peerId) },
						{ "from_id", wrapPeerId(recent.peerId) },
						{ "date", SerializeDate(recent.date) },
					});
				}) | ranges::to_vector;
				pairs.push_back({"recent", SerializeArray(context, recents)});
				context.nesting.pop_back();
			}

			return SerializeObject(context, pairs);
		};

		context.nesting.push_back(Context::kArray);
		const auto reactions = ranges::views::all(
			message.reactions
		) | ranges::views::transform(serializeReaction) | ranges::to_vector;
		pushBare("reactions", SerializeArray(context, reactions));
		context.nesting.pop_back();
	}

	return serialized();
}

} // namespace

Result JsonWriter::start(
		const Settings &settings,
		const Environment &environment,
		Stats *stats) {
	Expects(_output == nullptr);
	Expects(settings.path.endsWith('/'));

	_settings = base::duplicate(settings);
	_environment = environment;
	_stats = stats;
	_output = fileWithRelativePath(mainFileRelativePath());
	if (_settings.onlySinglePeer()) {
		return Result::Success();
	}
	auto block = pushNesting(Context::kObject);
	block.append(prepareObjectItemStart("about"));
	block.append(SerializeString(_environment.aboutTelegram));
	return _output->writeBlock(block);
}

QByteArray JsonWriter::pushNesting(Context::Type type) {
	Expects(_output != nullptr);

	_context.nesting.push_back(type);
	_currentNestingHadItem = false;
	return (type == Context::kObject ? "{" : "[");
}

QByteArray JsonWriter::prepareObjectItemStart(const QByteArray &key) {
	const auto guard = gsl::finally([&] { _currentNestingHadItem = true; });
	return (_currentNestingHadItem ? ",\n" : "\n")
		+ Indentation(_context)
		+ SerializeString(key)
		+ ": ";
}

QByteArray JsonWriter::prepareArrayItemStart() {
	const auto guard = gsl::finally([&] { _currentNestingHadItem = true; });
	return (_currentNestingHadItem ? ",\n" : "\n") + Indentation(_context);
}

QByteArray JsonWriter::popNesting() {
	Expects(_output != nullptr);
	Expects(!_context.nesting.empty());

	const auto type = Context::Type(_context.nesting.back());
	_context.nesting.pop_back();

	_currentNestingHadItem = true;
	return '\n'
		+ Indentation(_context)
		+ (type == Context::kObject ? '}' : ']');
}

Result JsonWriter::writePersonal(const Data::PersonalInfo &data) {
	Expects(_output != nullptr);

	const auto &info = data.user.info;
	return _output->writeBlock(
		prepareObjectItemStart("personal_information")
		+ SerializeObject(_context, {
		{ "user_id", Data::NumberToString(data.user.bareId) },
		{ "first_name", SerializeString(info.firstName) },
		{ "last_name", SerializeString(info.lastName) },
		{
			"phone_number",
			SerializeString(Data::FormatPhoneNumber(info.phoneNumber))
		},
		{
			"username",
			(!data.user.username.isEmpty()
				? SerializeString(FormatUsername(data.user.username))
				: QByteArray())
		},
		{
			"bio",
			(!data.bio.isEmpty()
				? SerializeString(data.bio)
				: QByteArray())
		},
	}));
}

Result JsonWriter::writeUserpicsStart(const Data::UserpicsInfo &data) {
	Expects(_output != nullptr);

	auto block = prepareObjectItemStart("profile_pictures");
	return _output->writeBlock(block + pushNesting(Context::kArray));
}

Result JsonWriter::writeUserpicsSlice(const Data::UserpicsSlice &data) {
	Expects(_output != nullptr);
	Expects(!data.list.empty());

	auto block = QByteArray();
	for (const auto &userpic : data.list) {
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
		block.append(prepareArrayItemStart());
		block.append(SerializeObject(_context, {
			{
				"date",
				userpic.date ? SerializeDate(userpic.date) : QByteArray()
			},
			{
				"date_unixtime",
				userpic.date ? SerializeDateRaw(userpic.date) : QByteArray()
			},
			{
				"photo",
				SerializeString(path)
			},
		}));
	}
	return _output->writeBlock(block);
}

Result JsonWriter::writeUserpicsEnd() {
	Expects(_output != nullptr);

	return _output->writeBlock(popNesting());
}

Result JsonWriter::writeStoriesStart(const Data::StoriesInfo &data) {
	Expects(_output != nullptr);

	auto block = prepareObjectItemStart("stories");
	return _output->writeBlock(block + pushNesting(Context::kArray));
}

Result JsonWriter::writeStoriesSlice(const Data::StoriesSlice &data) {
	Expects(_output != nullptr);

	if (data.list.empty()) {
		return Result::Success();
	}

	auto block = QByteArray();
	for (const auto &story : data.list) {
		using SkipReason = Data::File::SkipReason;
		const auto &file = story.file();
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
			Unexpected("Skip reason while writing story path.");
		}();
		block.append(prepareArrayItemStart());
		block.append(SerializeObject(_context, {
			{
				"date",
				story.date ? SerializeDate(story.date) : QByteArray()
			},
			{
				"date_unixtime",
				story.date ? SerializeDateRaw(story.date) : QByteArray()
			},
			{
				"expires",
				story.expires ? SerializeDate(story.expires) : QByteArray()
			},
			{
				"expires_unixtime",
				story.expires ? SerializeDateRaw(story.expires) : QByteArray()
			},
			{
				"pinned",
				story.pinned ? "true" : "false"
			},
			{
				"media",
				SerializeString(path)
			},
		}));
	}
	return _output->writeBlock(block);
}

Result JsonWriter::writeStoriesEnd() {
	Expects(_output != nullptr);

	return _output->writeBlock(popNesting());
}

Result JsonWriter::writeContactsList(const Data::ContactsList &data) {
	Expects(_output != nullptr);

	if (const auto result = writeSavedContacts(data); !result) {
		return result;
	} else if (const auto result = writeFrequentContacts(data); !result) {
		return result;
	}
	return Result::Success();
}

Result JsonWriter::writeSavedContacts(const Data::ContactsList &data) {
	Expects(_output != nullptr);

	auto block = prepareObjectItemStart("contacts");
	block.append(pushNesting(Context::kObject));
	block.append(prepareObjectItemStart("about"));
	block.append(SerializeString(_environment.aboutContacts));
	block.append(prepareObjectItemStart("list"));
	block.append(pushNesting(Context::kArray));
	for (const auto index : Data::SortedContactsIndices(data)) {
		const auto &contact = data.list[index];
		block.append(prepareArrayItemStart());

		if (contact.firstName.isEmpty()
			&& contact.lastName.isEmpty()
			&& contact.phoneNumber.isEmpty()) {
			block.append(SerializeObject(_context, {
				{ "date", SerializeDate(contact.date) },
				{ "date_unixtime", SerializeDateRaw(contact.date) },
			}));
		} else {
			block.append(SerializeObject(_context, {
				{
					"user_id",
					(contact.userId
						? Data::NumberToString(contact.userId.bare)
						: QByteArray())
				},
				{ "first_name", SerializeString(contact.firstName) },
				{ "last_name", SerializeString(contact.lastName) },
				{
					"phone_number",
					SerializeString(
						Data::FormatPhoneNumber(contact.phoneNumber))
				},
				{ "date", SerializeDate(contact.date) },
				{ "date_unixtime", SerializeDateRaw(contact.date) },
			}));
		}
	}
	block.append(popNesting());
	return _output->writeBlock(block + popNesting());
}

Result JsonWriter::writeFrequentContacts(const Data::ContactsList &data) {
	Expects(_output != nullptr);

	auto block = prepareObjectItemStart("frequent_contacts");
	block.append(pushNesting(Context::kObject));
	block.append(prepareObjectItemStart("about"));
	block.append(SerializeString(_environment.aboutFrequent));
	block.append(prepareObjectItemStart("list"));
	block.append(pushNesting(Context::kArray));
	const auto writeList = [&](
			const std::vector<Data::TopPeer> &peers,
			Data::Utf8String category) {
		for (const auto &top : peers) {
			const auto type = [&] {
				if (const auto chat = top.peer.chat()) {
					return chat->username.isEmpty()
						? (chat->isBroadcast
							? "private_channel"
							: (chat->isSupergroup
								? "private_supergroup"
								: "private_group"))
						: (chat->isBroadcast
							? "public_channel"
							: "public_supergroup");
				}
				return "user";
			}();
			block.append(prepareArrayItemStart());
			block.append(SerializeObject(_context, {
				{ "id", Data::NumberToString(Data::PeerToBareId(top.peer.id())) },
				{ "category", SerializeString(category) },
				{ "type", SerializeString(type) },
				{ "name", StringAllowNull(top.peer.name()) },
				{ "rating", Data::NumberToString(top.rating) },
			}));
		}
	};
	writeList(data.correspondents, "people");
	writeList(data.inlineBots, "inline_bots");
	writeList(data.phoneCalls, "calls");
	block.append(popNesting());
	return _output->writeBlock(block + popNesting());
}

Result JsonWriter::writeSessionsList(const Data::SessionsList &data) {
	Expects(_output != nullptr);

	if (const auto result = writeSessions(data); !result) {
		return result;
	} else if (const auto result = writeWebSessions(data); !result) {
		return result;
	}
	return Result::Success();
}

Result JsonWriter::writeOtherData(const Data::File &data) {
	Expects(_output != nullptr);
	Expects(data.skipReason == Data::File::SkipReason::None);
	Expects(!data.relativePath.isEmpty());

	QFile f(pathWithRelativePath(data.relativePath));
	if (!f.open(QIODevice::ReadOnly)) {
		return Result(Result::Type::FatalError, f.fileName());
	}
	const auto content = f.readAll();
	if (content.isEmpty()) {
		return Result::Success();
	}
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(content, &error);
	if (error.error != QJsonParseError::NoError) {
		return Result(Result::Type::FatalError, f.fileName());
	}
	auto block = prepareObjectItemStart("other_data");
	Fn<void(const QJsonObject &data)> pushObject;
	Fn<void(const QJsonArray &data)> pushArray;
	Fn<void(const QJsonValue &data)> pushValue;
	pushObject = [&](const QJsonObject &data) {
		block.append(pushNesting(Context::kObject));
		for (auto i = data.begin(); i != data.end(); ++i) {
			if ((*i).type() != QJsonValue::Undefined) {
				block.append(prepareObjectItemStart(i.key().toUtf8()));
				pushValue(*i);
			}
		}
		block.append(popNesting());
	};
	pushArray = [&](const QJsonArray &data) {
		block.append(pushNesting(Context::kArray));
		for (auto i = data.begin(); i != data.end(); ++i) {
			if ((*i).type() != QJsonValue::Undefined) {
				block.append(prepareArrayItemStart());
				pushValue(*i);
			}
		}
		block.append(popNesting());
	};
	pushValue = [&](const QJsonValue &data) {
		switch (data.type()) {
		case QJsonValue::Null:
			block.append("null");
			return;
		case QJsonValue::Bool:
			block.append(data.toBool() ? "true" : "false");
			return;
		case QJsonValue::Double:
			block.append(Data::NumberToString(data.toDouble()));
			return;
		case QJsonValue::String:
			block.append(SerializeString(data.toString().toUtf8()));
			return;
		case QJsonValue::Array:
			return pushArray(data.toArray());
		case QJsonValue::Object:
			return pushObject(data.toObject());
		}
		Unexpected("Type of json valuein JsonWriter::writeOtherData.");
	};
	if (document.isObject()) {
		pushObject(document.object());
	} else {
		pushArray(document.array());
	}
	return _output->writeBlock(block);
}

Result JsonWriter::writeSessions(const Data::SessionsList &data) {
	Expects(_output != nullptr);

	auto block = prepareObjectItemStart("sessions");
	block.append(pushNesting(Context::kObject));
	block.append(prepareObjectItemStart("about"));
	block.append(SerializeString(_environment.aboutSessions));
	block.append(prepareObjectItemStart("list"));
	block.append(pushNesting(Context::kArray));
	for (const auto &session : data.list) {
		block.append(prepareArrayItemStart());
		block.append(SerializeObject(_context, {
			{ "last_active", SerializeDate(session.lastActive) },
			{ "last_active_unixtime", SerializeDateRaw(session.lastActive) },
			{ "last_ip", SerializeString(session.ip) },
			{ "last_country", SerializeString(session.country) },
			{ "last_region", SerializeString(session.region) },
			{
				"application_name",
				StringAllowNull(session.applicationName)
			},
			{
				"application_version",
				StringAllowEmpty(session.applicationVersion)
			},
			{ "device_model", SerializeString(session.deviceModel) },
			{ "platform", SerializeString(session.platform) },
			{ "system_version", SerializeString(session.systemVersion) },
			{ "created", SerializeDate(session.created) },
			{ "created_unixtime", SerializeDateRaw(session.created) },
		}));
	}
	block.append(popNesting());
	return _output->writeBlock(block + popNesting());
}

Result JsonWriter::writeWebSessions(const Data::SessionsList &data) {
	Expects(_output != nullptr);

	auto block = prepareObjectItemStart("web_sessions");
	block.append(pushNesting(Context::kObject));
	block.append(prepareObjectItemStart("about"));
	block.append(SerializeString(_environment.aboutWebSessions));
	block.append(prepareObjectItemStart("list"));
	block.append(pushNesting(Context::kArray));
	for (const auto &session : data.webList) {
		block.append(prepareArrayItemStart());
		block.append(SerializeObject(_context, {
			{ "last_active", SerializeDate(session.lastActive) },
			{ "last_active_unixtime", SerializeDateRaw(session.lastActive) },
			{ "last_ip", SerializeString(session.ip) },
			{ "last_region", SerializeString(session.region) },
			{ "bot_username", StringAllowNull(session.botUsername) },
			{ "domain_name", StringAllowNull(session.domain) },
			{ "browser", SerializeString(session.browser) },
			{ "platform", SerializeString(session.platform) },
			{ "created", SerializeDate(session.created) },
			{ "created_unixtime", SerializeDateRaw(session.created) },
		}));
	}
	block.append(popNesting());
	return _output->writeBlock(block + popNesting());
}

Result JsonWriter::writeDialogsStart(const Data::DialogsInfo &data) {
	return Result::Success();
}

Result JsonWriter::writeDialogStart(const Data::DialogInfo &data) {
	Expects(_output != nullptr);

	if (!_settings.onlySinglePeer()) {
		const auto result = validateDialogsMode(data.isLeftChannel);
		if (!result) {
			return result;
		}
	}

	using Type = Data::DialogInfo::Type;
	const auto TypeString = [](Type type) {
		switch (type) {
		case Type::Unknown: return "";
		case Type::Self: return "saved_messages";
		case Type::Replies: return "replies";
		case Type::VerifyCodes: return "verification_codes";
		case Type::Personal: return "personal_chat";
		case Type::Bot: return "bot_chat";
		case Type::PrivateGroup: return "private_group";
		case Type::PrivateSupergroup: return "private_supergroup";
		case Type::PublicSupergroup: return "public_supergroup";
		case Type::PrivateChannel: return "private_channel";
		case Type::PublicChannel: return "public_channel";
		}
		Unexpected("Dialog type in TypeString.");
	};

	auto block = _settings.onlySinglePeer()
		? QByteArray()
		: prepareArrayItemStart();
	block.append(pushNesting(Context::kObject));
	if (data.type != Type::Self
		&& data.type != Type::Replies
		&& data.type != Type::VerifyCodes) {
		block.append(prepareObjectItemStart("name")
			+ StringAllowNull(data.name));
	}
	block.append(prepareObjectItemStart("type")
		+ StringAllowNull(TypeString(data.type)));
	block.append(prepareObjectItemStart("id")
		+ Data::NumberToString(Data::PeerToBareId(data.peerId)));
	block.append(prepareObjectItemStart("messages"));
	block.append(pushNesting(Context::kArray));
	return _output->writeBlock(block);
}

Result JsonWriter::validateDialogsMode(bool isLeftChannel) {
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
		isLeftChannel ? "left_chats" : "chats",
		(isLeftChannel
			? _environment.aboutLeftChats
			: _environment.aboutChats));
}

Result JsonWriter::writeDialogSlice(const Data::MessagesSlice &data) {
	Expects(_output != nullptr);

	auto block = QByteArray();
	for (const auto &message : data.list) {
		if (Data::SkipMessageByDate(message, _settings)) {
			continue;
		}
		block.append(prepareArrayItemStart() + SerializeMessage(
			_context,
			message,
			data.peers,
			_environment.internalLinksDomain));
	}
	return block.isEmpty() ? Result::Success() : _output->writeBlock(block);
}

Result JsonWriter::writeDialogEnd() {
	Expects(_output != nullptr);

	auto block = popNesting();
	return _output->writeBlock(block + popNesting());
}

Result JsonWriter::writeDialogsEnd() {
	if (_settings.onlySinglePeer()) {
		return Result::Success();
	}
	return writeChatsEnd();
}

Result JsonWriter::writeChatsStart(
		const QByteArray &listName,
		const QByteArray &about) {
	Expects(_output != nullptr);

	auto block = prepareObjectItemStart(listName);
	block.append(pushNesting(Context::kObject));
	block.append(prepareObjectItemStart("about"));
	block.append(SerializeString(about));
	block.append(prepareObjectItemStart("list"));
	return _output->writeBlock(block + pushNesting(Context::kArray));
}

Result JsonWriter::writeChatsEnd() {
	Expects(_output != nullptr);

	auto block = popNesting();
	return _output->writeBlock(block + popNesting());
}

Result JsonWriter::finish() {
	Expects(_output != nullptr);

	if (_settings.onlySinglePeer()) {
		Assert(_context.nesting.empty());
		return Result::Success();
	}
	auto block = popNesting();
	Assert(_context.nesting.empty());
	return _output->writeBlock(block);
}

QString JsonWriter::mainFilePath() {
	return pathWithRelativePath(mainFileRelativePath());
}

QString JsonWriter::mainFileRelativePath() const {
	return "result.json";
}

QString JsonWriter::pathWithRelativePath(const QString &path) const {
	return _settings.path + path;
}

std::unique_ptr<File> JsonWriter::fileWithRelativePath(
		const QString &path) const {
	return std::make_unique<File>(pathWithRelativePath(path), _stats);
}

} // namespace Output
} // namespace Export
