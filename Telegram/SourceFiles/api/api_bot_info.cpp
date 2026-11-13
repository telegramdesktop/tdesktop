/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_bot_info.h"

#include "apiwrap.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "mtproto/sender.h"

namespace Api {

Data::BotInfoTransport MakeBotInfoTransport(not_null<UserData*> bot) {
	const auto api = std::make_shared<MTP::Sender>(&bot->session().mtp());
	const auto fail = [](Fn<void(QString)> callback) {
		return [=](const MTP::Error &error) {
			callback(error.type());
		};
	};
	return {
		.read = [=](
				QString langCode,
				Fn<void(Data::BotInfoTexts)> done,
				Fn<void(QString)> failed) {
			api->request(MTPbots_GetBotInfo(
				MTP_flags(MTPbots_GetBotInfo::Flag::f_bot),
				bot->inputUser(),
				MTP_string(langCode)
			)).done([=](const MTPbots_BotInfo &result) {
				const auto &data = result.data();
				done({
					.name = qs(data.vname()),
					.about = qs(data.vabout()),
					.description = qs(data.vdescription()),
				});
			}).fail(fail(failed)).handleFloodErrors().send();
		},
		.write = [=](
				QString langCode,
				Data::BotInfoChange change,
				Fn<void()> done,
				Fn<void(QString)> failed) {
			using Flag = MTPbots_SetBotInfo::Flag;
			api->request(MTPbots_SetBotInfo(
				MTP_flags(Flag::f_bot
					| (change.name ? Flag::f_name : Flag())
					| (change.about ? Flag::f_about : Flag())
					| (change.description ? Flag::f_description : Flag())),
				bot->inputUser(),
				MTP_string(langCode),
				MTP_string(change.name.value_or(QString())),
				MTP_string(change.about.value_or(QString())),
				MTP_string(change.description.value_or(QString()))
			)).done([=] {
				if (langCode.isEmpty() && change.about) {
					bot->setAbout(*change.about);
				}
				done();
			}).fail(fail(failed)).handleFloodErrors().send();
		},
		.translate = [=](
				std::vector<QString> texts,
				QString langCode,
				Fn<void(std::vector<QString>)> done,
				Fn<void(QString)> failed) {
			auto list = QVector<MTPTextWithEntities>();
			list.reserve(texts.size());
			for (const auto &text : texts) {
				list.push_back(MTP_textWithEntities(
					MTP_string(text),
					MTP_vector<MTPMessageEntity>()));
			}
			api->request(MTPmessages_TranslateText(
				MTP_flags(MTPmessages_TranslateText::Flag::f_text),
				MTP_inputPeerEmpty(),
				MTPVector<MTPint>(),
				MTP_vector<MTPTextWithEntities>(list),
				MTP_string(langCode),
				MTPstring() // tone
			)).done([=](const MTPmessages_TranslatedText &result) {
				done(ranges::views::all(
					result.data().vresult().v
				) | ranges::views::transform([](
						const MTPTextWithEntities &entry) {
					return qs(entry.data().vtext());
				}) | ranges::to_vector);
			}).fail(fail(failed)).handleFloodErrors().send();
		},
		.refresh = [=] {
			bot->session().api().requestFullPeer(bot);
		},
	};
}

not_null<Data::BotInfoLocalization*> BotInfoLocalizations::get(
		not_null<UserData*> bot) {
	const auto i = _localizations.find(bot);
	if (i != end(_localizations)) {
		return i->second.get();
	}
	return _localizations.emplace(
		bot,
		std::make_unique<Data::BotInfoLocalization>(
			MakeBotInfoTransport(bot))
	).first->second.get();
}

} // namespace Api
