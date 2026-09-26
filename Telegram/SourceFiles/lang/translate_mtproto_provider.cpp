/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/translate_mtproto_provider.h"

#include "api/api_text_entities.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "spellcheck/platform/platform_language.h"

namespace Ui {
namespace {

class MTProtoTranslateProvider final : public TranslateProvider {
public:
	explicit MTProtoTranslateProvider(not_null<Main::Session*> session)
	: _session(session)
	, _api(&session->mtp()) {
	}

	[[nodiscard]] bool supportsMessageId() const override {
		return true;
	}

	void request(
			TranslateProviderRequest request,
			LanguageId to,
			Fn<void(TranslateProviderResult)> done) override {
		requestBatch(
			{ std::move(request) },
			to,
			[done = std::move(done)](
					int,
					TranslateProviderResult result) {
				done(std::move(result));
			},
			[] {});
	}

	void requestBatch(
			std::vector<TranslateProviderRequest> requests,
			const LanguageId &to,
			Fn<void(int, TranslateProviderResult)> doneOne,
			Fn<void()> doneAll) override {
		using Flag = MTPmessages_TranslateText::Flag;
		if (requests.empty()) {
			doneAll();
			return;
		}

		const auto failAll = [=] {
			for (auto i = 0; i != requests.size(); ++i) {
				doneOne(i, TranslateProviderResult{
					.error = TranslateProviderError::Unknown,
				});
			}
			doneAll();
		};
		const auto doneFromList = [=, session = _session](
				const QVector<MTPTextWithEntities> &list) {
			for (auto i = 0; i != requests.size(); ++i) {
				doneOne(
					i,
					(i < list.size())
						? TranslateProviderResult{
							.text = Api::ParseTextWithEntities(
								session,
								list[i]),
						}
						: TranslateProviderResult{
							.error = TranslateProviderError::Unknown,
						});
			}
			doneAll();
		};

		const auto firstPeer = PeerId(requests.front().peerId);
		const auto allWithIds = ranges::all_of(
			requests,
			[&](const TranslateProviderRequest &request) {
				return (PeerId(request.peerId) == firstPeer)
					&& (request.msgId != 0);
			});
		if (allWithIds) {
			const auto peer = _session->data().peerLoaded(firstPeer);
			if (!peer) {
				failAll();
				return;
			}
			auto ids = QVector<MTPint>();
			ids.reserve(requests.size());
			for (const auto &request : requests) {
				ids.push_back(MTP_int(MsgId(request.msgId)));
			}
			_api.request(MTPmessages_TranslateText(
				MTP_flags(Flag::f_peer | Flag::f_id),
				peer->input(),
				MTP_vector<MTPint>(ids),
				MTPVector<MTPTextWithEntities>(),
				MTP_string(to.twoLetterCode()),
				MTPstring() // tone
			)).done([=](const MTPmessages_TranslatedText &result) {
				doneFromList(result.data().vresult().v);
			}).fail([=](const MTP::Error &) {
				failAll();
			}).send();
			return;
		}

		const auto allWithText = ranges::all_of(
			requests,
			[](const TranslateProviderRequest &request) {
				return !request.text.text.isEmpty();
			});
		if (!allWithText) {
			TranslateProvider::requestBatch(
				std::move(requests),
				to,
				std::move(doneOne),
				std::move(doneAll));
			return;
		}

		auto text = QVector<MTPTextWithEntities>();
		text.reserve(requests.size());
		for (const auto &request : requests) {
			text.push_back(MTP_textWithEntities(
				MTP_string(request.text.text),
				Api::EntitiesToMTP(
					_session,
					request.text.entities,
					Api::ConvertOption::SkipLocal)));
		}
		_api.request(MTPmessages_TranslateText(
			MTP_flags(Flag::f_text),
			MTP_inputPeerEmpty(),
			MTPVector<MTPint>(),
			MTP_vector<MTPTextWithEntities>(text),
			MTP_string(to.twoLetterCode()),
			MTPstring() // tone
		)).done([=](const MTPmessages_TranslatedText &result) {
			doneFromList(result.data().vresult().v);
		}).fail([=](const MTP::Error &) {
			failAll();
		}).send();
	}

private:
	const not_null<Main::Session*> _session;
	MTP::Sender _api;

};

} // namespace

std::unique_ptr<TranslateProvider> CreateMTProtoTranslateProvider(
		not_null<Main::Session*> session) {
	return std::make_unique<MTProtoTranslateProvider>(session);
}

} // namespace Ui
