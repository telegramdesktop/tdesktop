/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_text_entities.h"

#include "main/main_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers_set.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_user.h"

namespace Api {
namespace {

using namespace TextUtilities;

[[nodiscard]] QString CustomEmojiEntityData(
		const MTPDmessageEntityCustomEmoji &data) {
	return Data::SerializeCustomEmojiId(data.vdocument_id().v);
}

[[nodiscard]] std::optional<MTPMessageEntity> CustomEmojiEntity(
		MTPint offset,
		MTPint length,
		const QString &data) {
	const auto parsed = Data::ParseCustomEmojiData(data);
	if (!parsed) {
		return {};
	}
	return MTP_messageEntityCustomEmoji(
		offset,
		length,
		MTP_long(parsed));
}

[[nodiscard]] std::optional<MTPMessageEntity> MentionNameEntity(
		not_null<Main::Session*> session,
		MTPint offset,
		MTPint length,
		const QString &data) {
	const auto parsed = MentionNameDataToFields(data);
	if (!parsed.userId || parsed.selfId != session->userId().bare) {
		return {};
	}
	return MTP_inputMessageEntityMentionName(
		offset,
		length,
		(parsed.userId == parsed.selfId
			? MTP_inputUserSelf()
			: MTP_inputUser(
				MTP_long(parsed.userId),
				MTP_long(parsed.accessHash))));
}

} // namespace

EntitiesInText EntitiesFromMTP(
		Main::Session *session,
		const QVector<MTPMessageEntity> &entities) {
	auto result = EntitiesInText();
	if (!entities.isEmpty()) {
		result.reserve(entities.size());
		for (const auto &entity : entities) {
			switch (entity.type()) {
			case mtpc_messageEntityUrl: { auto &d = entity.c_messageEntityUrl(); result.push_back({ EntityType::Url, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityTextUrl: { auto &d = entity.c_messageEntityTextUrl(); result.push_back({ EntityType::CustomUrl, d.voffset().v, d.vlength().v, qs(d.vurl()) }); } break;
			case mtpc_messageEntityEmail: { auto &d = entity.c_messageEntityEmail(); result.push_back({ EntityType::Email, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityHashtag: { auto &d = entity.c_messageEntityHashtag(); result.push_back({ EntityType::Hashtag, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityCashtag: { auto &d = entity.c_messageEntityCashtag(); result.push_back({ EntityType::Cashtag, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityPhone: break; // Skipping phones.
			case mtpc_messageEntityMention: { auto &d = entity.c_messageEntityMention(); result.push_back({ EntityType::Mention, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityMentionName: if (session) {
				const auto &d = entity.c_messageEntityMentionName();
				const auto userId = UserId(d.vuser_id());
				const auto user = session->data().userLoaded(userId);
				const auto data = MentionNameDataFromFields({
					.selfId = session->userId().bare,
					.userId = userId.bare,
					.accessHash = user ? user->accessHash() : 0,
				});
				result.push_back({ EntityType::MentionName, d.voffset().v, d.vlength().v, data });
			} break;
			case mtpc_inputMessageEntityMentionName: if (session) {
				const auto &d = entity.c_inputMessageEntityMentionName();
				const auto data = d.vuser_id().match([&](
						const MTPDinputUserSelf &) {
					return MentionNameDataFromFields({
						.selfId = session->userId().bare,
						.userId = session->userId().bare,
						.accessHash = session->user()->accessHash(),
					});
				}, [&](const MTPDinputUser &data) {
					return MentionNameDataFromFields({
						.selfId = session->userId().bare,
						.userId = UserId(data.vuser_id()).bare,
						.accessHash = data.vaccess_hash().v,
					});
				}, [&](const auto &) {
					return QString();
				});
				if (!data.isEmpty()) {
					result.push_back({ EntityType::MentionName, d.voffset().v, d.vlength().v, data });
				}
			} break;
			case mtpc_messageEntityBotCommand: { auto &d = entity.c_messageEntityBotCommand(); result.push_back({ EntityType::BotCommand, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityBold: { auto &d = entity.c_messageEntityBold(); result.push_back({ EntityType::Bold, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityItalic: { auto &d = entity.c_messageEntityItalic(); result.push_back({ EntityType::Italic, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityUnderline: { auto &d = entity.c_messageEntityUnderline(); result.push_back({ EntityType::Underline, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityStrike: { auto &d = entity.c_messageEntityStrike(); result.push_back({ EntityType::StrikeOut, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityCode: { auto &d = entity.c_messageEntityCode(); result.push_back({ EntityType::Code, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityPre: { auto &d = entity.c_messageEntityPre(); result.push_back({ EntityType::Pre, d.voffset().v, d.vlength().v, qs(d.vlanguage()) }); } break;
			case mtpc_messageEntityBlockquote: { auto &d = entity.c_messageEntityBlockquote(); result.push_back({ EntityType::Blockquote, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityBankCard: break; // Skipping cards. // #TODO entities
			case mtpc_messageEntitySpoiler: { auto &d = entity.c_messageEntitySpoiler(); result.push_back({ EntityType::Spoiler, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityCustomEmoji: {
				const auto &d = entity.c_messageEntityCustomEmoji();
				result.push_back({ EntityType::CustomEmoji, d.voffset().v, d.vlength().v, CustomEmojiEntityData(d) });
			} break;
			}
		}
	}
	return result;
}

MTPVector<MTPMessageEntity> EntitiesToMTP(
		not_null<Main::Session*> session,
		const EntitiesInText &entities,
		ConvertOption option) {
	auto v = QVector<MTPMessageEntity>();
	v.reserve(entities.size());
	for (const auto &entity : entities) {
		if (entity.length() <= 0) continue;
		if (option == ConvertOption::SkipLocal
			&& entity.type() != EntityType::Bold
			//&& entity.type() != EntityType::Semibold // Not in API.
			&& entity.type() != EntityType::Italic
			&& entity.type() != EntityType::Underline
			&& entity.type() != EntityType::StrikeOut
			&& entity.type() != EntityType::Code // #TODO entities
			&& entity.type() != EntityType::Pre
			&& entity.type() != EntityType::Blockquote
			&& entity.type() != EntityType::Spoiler
			&& entity.type() != EntityType::MentionName
			&& entity.type() != EntityType::CustomUrl
			&& entity.type() != EntityType::CustomEmoji) {
			continue;
		}

		auto offset = MTP_int(entity.offset());
		auto length = MTP_int(entity.length());
		switch (entity.type()) {
		case EntityType::Url: v.push_back(MTP_messageEntityUrl(offset, length)); break;
		case EntityType::CustomUrl: v.push_back(MTP_messageEntityTextUrl(offset, length, MTP_string(entity.data()))); break;
		case EntityType::Email: v.push_back(MTP_messageEntityEmail(offset, length)); break;
		case EntityType::Hashtag: v.push_back(MTP_messageEntityHashtag(offset, length)); break;
		case EntityType::Cashtag: v.push_back(MTP_messageEntityCashtag(offset, length)); break;
		case EntityType::Mention: v.push_back(MTP_messageEntityMention(offset, length)); break;
		case EntityType::MentionName: {
			if (const auto valid = MentionNameEntity(session, offset, length, entity.data())) {
				v.push_back(*valid);
			}
		} break;
		case EntityType::BotCommand: v.push_back(MTP_messageEntityBotCommand(offset, length)); break;
		case EntityType::Bold: v.push_back(MTP_messageEntityBold(offset, length)); break;
		case EntityType::Italic: v.push_back(MTP_messageEntityItalic(offset, length)); break;
		case EntityType::Underline: v.push_back(MTP_messageEntityUnderline(offset, length)); break;
		case EntityType::StrikeOut: v.push_back(MTP_messageEntityStrike(offset, length)); break;
		case EntityType::Code: v.push_back(MTP_messageEntityCode(offset, length)); break; // #TODO entities
		case EntityType::Pre: v.push_back(MTP_messageEntityPre(offset, length, MTP_string(entity.data()))); break;
		case EntityType::Blockquote: v.push_back(MTP_messageEntityBlockquote(offset, length)); break;
		case EntityType::Spoiler: v.push_back(MTP_messageEntitySpoiler(offset, length)); break;
		case EntityType::CustomEmoji: {
			if (const auto valid = CustomEmojiEntity(offset, length, entity.data())) {
				v.push_back(*valid);
			}
		} break;
		}
	}
	return MTP_vector<MTPMessageEntity>(std::move(v));
}

} // namespace Api
