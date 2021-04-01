/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_text_entities.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_user.h"

namespace Api {
namespace {

using namespace TextUtilities;

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
			case mtpc_messageEntityTextUrl: { auto &d = entity.c_messageEntityTextUrl(); result.push_back({ EntityType::CustomUrl, d.voffset().v, d.vlength().v, Clean(qs(d.vurl())) }); } break;
			case mtpc_messageEntityEmail: { auto &d = entity.c_messageEntityEmail(); result.push_back({ EntityType::Email, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityHashtag: { auto &d = entity.c_messageEntityHashtag(); result.push_back({ EntityType::Hashtag, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityCashtag: { auto &d = entity.c_messageEntityCashtag(); result.push_back({ EntityType::Cashtag, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityPhone: break; // Skipping phones.
			case mtpc_messageEntityMention: { auto &d = entity.c_messageEntityMention(); result.push_back({ EntityType::Mention, d.voffset().v, d.vlength().v }); } break;
			case mtpc_messageEntityMentionName: {
				const auto &d = entity.c_messageEntityMentionName();
				const auto userId = UserId(d.vuser_id());
				const auto data = [&] {
					if (session) {
						if (const auto user = session->data().userLoaded(userId)) {
							return MentionNameDataFromFields({
								userId.bare,
								user->accessHash()
							});
						}
					}
					return MentionNameDataFromFields(userId.bare);
				}();
				result.push_back({ EntityType::MentionName, d.voffset().v, d.vlength().v, data });
			} break;
			case mtpc_inputMessageEntityMentionName: {
				const auto &d = entity.c_inputMessageEntityMentionName();
				const auto data = [&] {
					if (session && d.vuser_id().type() == mtpc_inputUserSelf) {
						return MentionNameDataFromFields(session->userId().bare);
					} else if (d.vuser_id().type() == mtpc_inputUser) {
						auto &user = d.vuser_id().c_inputUser();
						const auto userId = UserId(user.vuser_id());
						return MentionNameDataFromFields({ userId.bare, user.vaccess_hash().v });
					}
					return QString();
				}();
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
			case mtpc_messageEntityPre: { auto &d = entity.c_messageEntityPre(); result.push_back({ EntityType::Pre, d.voffset().v, d.vlength().v, Clean(qs(d.vlanguage())) }); } break;
			case mtpc_messageEntityBankCard: break; // Skipping cards.
				// #TODO entities
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
			&& entity.type() != EntityType::MentionName
			&& entity.type() != EntityType::CustomUrl) {
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
			auto inputUser = [&](const QString &data) -> MTPInputUser {
				auto fields = MentionNameDataToFields(data);
				if (session && fields.userId == session->userId().bare) {
					return MTP_inputUserSelf();
				} else if (fields.userId) {
					return MTP_inputUser(MTP_int(fields.userId), MTP_long(fields.accessHash));
				}
				return MTP_inputUserEmpty();
			}(entity.data());
			if (inputUser.type() != mtpc_inputUserEmpty) {
				v.push_back(MTP_inputMessageEntityMentionName(offset, length, inputUser));
			}
		} break;
		case EntityType::BotCommand: v.push_back(MTP_messageEntityBotCommand(offset, length)); break;
		case EntityType::Bold: v.push_back(MTP_messageEntityBold(offset, length)); break;
		case EntityType::Italic: v.push_back(MTP_messageEntityItalic(offset, length)); break;
		case EntityType::Underline: v.push_back(MTP_messageEntityUnderline(offset, length)); break;
		case EntityType::StrikeOut: v.push_back(MTP_messageEntityStrike(offset, length)); break;
		case EntityType::Code: v.push_back(MTP_messageEntityCode(offset, length)); break; // #TODO entities
		case EntityType::Pre: v.push_back(MTP_messageEntityPre(offset, length, MTP_string(entity.data()))); break;
		}
	}
	return MTP_vector<MTPMessageEntity>(std::move(v));
}

} // namespace Api
