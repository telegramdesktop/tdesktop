/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/sponsored_messages.h"

#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "core/click_handler_types.h"
#include "data/data_channel.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h" // Ui::Text::RichLangValue.

namespace Data {
namespace {

constexpr auto kRequestTimeLimit = 5 * 60 * crl::time(1000);

[[nodiscard]] bool TooEarlyForRequest(crl::time received) {
	return (received > 0) && (received + kRequestTimeLimit > crl::now());
}

} // namespace

SponsoredMessages::SponsoredMessages(not_null<Main::Session*> session)
: _session(session)
, _clearTimer([=] { clearOldRequests(); }) {
}

SponsoredMessages::~SponsoredMessages() {
	Expects(_data.empty());
	Expects(_requests.empty());
	Expects(_viewRequests.empty());
}

void SponsoredMessages::clear() {
	_lifetime.destroy();
	for (const auto &request : base::take(_requests)) {
		_session->api().request(request.second.requestId).cancel();
	}
	for (const auto &request : base::take(_viewRequests)) {
		_session->api().request(request.second.requestId).cancel();
	}
	base::take(_data);
}

void SponsoredMessages::clearOldRequests() {
	const auto now = crl::now();
	while (true) {
		const auto i = ranges::find_if(_requests, [&](const auto &value) {
			const auto &request = value.second;
			return !request.requestId
				&& (request.lastReceived + kRequestTimeLimit <= now);
		});
		if (i == end(_requests)) {
			break;
		}
		_requests.erase(i);
	}
}

bool SponsoredMessages::append(not_null<History*> history) {
	const auto it = _data.find(history);
	if (it == end(_data)) {
		return false;
	}
	auto &list = it->second;
	if (list.showedAll
		|| !TooEarlyForRequest(list.received)
		|| list.postsBetween) {
		return false;
	}

	const auto entryIt = ranges::find_if(list.entries, [](const Entry &e) {
		return e.item == nullptr;
	});
	if (entryIt == end(list.entries)) {
		list.showedAll = true;
		return false;
	}
	// SponsoredMessages::Details can be requested within
	// the constructor of HistoryItem, so itemFullId is used as a key.
	entryIt->itemFullId = FullMsgId(
		history->peer->id,
		_session->data().nextLocalMessageId());
	entryIt->item.reset(history->addSponsoredMessage(
		entryIt->itemFullId.msg,
		entryIt->sponsored.from,
		entryIt->sponsored.textWithEntities));

	return true;
}

void SponsoredMessages::inject(
		not_null<History*> history,
		MsgId injectAfterMsgId,
		int betweenHeight,
		int fallbackWidth) {
	if (!canHaveFor(history)) {
		return;
	}
	const auto it = _data.find(history);
	if (it == end(_data)) {
		return;
	}
	auto &list = it->second;
	if (!list.postsBetween || (list.entries.size() == list.injectedCount)) {
		return;
	}

	while (true) {
		const auto entryIt = ranges::find_if(list.entries, [](const auto &e) {
			return e.item == nullptr;
		});
		if (entryIt == end(list.entries)) {
			list.showedAll = true;
			return;
		}
		const auto lastView = (entryIt != begin(list.entries))
			? (entryIt - 1)->item->mainView()
			: (injectAfterMsgId == ShowAtUnreadMsgId)
			? history->firstUnreadMessage()
			: [&] {
				const auto message = history->peer->owner().message(
					history->peer->id,
					injectAfterMsgId);
				return message ? message->mainView() : nullptr;
			}();
		if (!lastView || !lastView->block()) {
			return;
		}

		auto summaryBetween = 0;
		auto summaryHeight = 0;

		using BlockPtr = std::unique_ptr<HistoryBlock>;
		using ViewPtr = std::unique_ptr<HistoryView::Element>;
		auto blockIt = ranges::find(
			history->blocks,
			lastView->block(),
			&BlockPtr::get);
		if (blockIt == end(history->blocks)) {
			return;
		}
		const auto messages = [&]() -> const std::vector<ViewPtr> & {
			return (*blockIt)->messages;
		};
		auto lastViewIt = ranges::find(messages(), lastView, &ViewPtr::get);
		while ((summaryBetween < list.postsBetween)
			|| (summaryHeight < betweenHeight)) {
			lastViewIt++;
			if (lastViewIt == end(messages())) {
				blockIt++;
				if (blockIt != end(history->blocks)) {
					lastViewIt = begin(messages());
				} else {
					return;
				}
			}
			summaryBetween++;
			const auto viewHeight = (*lastViewIt)->height();
			summaryHeight += viewHeight
				? viewHeight
				: (*lastViewIt)->resizeGetHeight(fallbackWidth);
		}
		// SponsoredMessages::Details can be requested within
		// the constructor of HistoryItem, so itemFullId is used as a key.
		entryIt->itemFullId = FullMsgId(
			history->peer->id,
			_session->data().nextLocalMessageId());
		const auto makedMessage = history->makeMessage(
			entryIt->itemFullId.msg,
			entryIt->sponsored.from,
			entryIt->sponsored.textWithEntities,
			(*lastViewIt)->data());
		entryIt->item.reset(makedMessage.get());
		history->addNewInTheMiddle(
			makedMessage.get(),
			std::distance(begin(history->blocks), blockIt),
			std::distance(begin(messages()), lastViewIt) + 1);
		messages().back().get()->setPendingResize();
		list.injectedCount++;
	}
}

bool SponsoredMessages::canHaveFor(not_null<History*> history) const {
	return history->peer->isChannel();
}

void SponsoredMessages::request(not_null<History*> history, Fn<void()> done) {
	if (!canHaveFor(history)) {
		return;
	}
	auto &request = _requests[history];
	if (request.requestId || TooEarlyForRequest(request.lastReceived)) {
		return;
	}
	{
		const auto it = _data.find(history);
		if (it != end(_data)) {
			auto &list = it->second;
			// Don't rebuild currently displayed messages.
			const auto proj = [](const Entry &e) {
				return e.item != nullptr;
			};
			if (ranges::any_of(list.entries, proj)) {
				return;
			}
		}
	}
	const auto channel = history->peer->asChannel();
	Assert(channel != nullptr);
	request.requestId = _session->api().request(
		MTPchannels_GetSponsoredMessages(
			channel->inputChannel)
	).done([=](const MTPmessages_sponsoredMessages &result) {
		parse(history, result);
		if (done) {
			done();
		}
	}).fail([=] {
		_requests.remove(history);
	}).send();
}

void SponsoredMessages::parse(
		not_null<History*> history,
		const MTPmessages_sponsoredMessages &list) {
	auto &request = _requests[history];
	request.lastReceived = crl::now();
	request.requestId = 0;
	if (!_clearTimer.isActive()) {
		_clearTimer.callOnce(kRequestTimeLimit * 2);
	}

	list.match([&](const MTPDmessages_sponsoredMessages &data) {
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());

		const auto &messages = data.vmessages().v;
		auto &list = _data.emplace(history, List()).first->second;
		list.entries.clear();
		list.received = crl::now();
		for (const auto &message : messages) {
			append(history, list, message);
		}
		if (const auto postsBetween = data.vposts_between()) {
			list.postsBetween = postsBetween->v;
			list.state = State::InjectToMiddle;
		} else {
			list.state = State::AppendToEnd;
		}
	}, [](const MTPDmessages_sponsoredMessagesEmpty &) {
	});
}

void SponsoredMessages::append(
		not_null<History*> history,
		List &list,
		const MTPSponsoredMessage &message) {
	const auto &data = message.data();
	const auto randomId = data.vrandom_id().v;
	const auto from = SponsoredFrom{
		.title = qs(data.vtitle()),
		.link = qs(data.vurl()),
		.buttonText = qs(data.vbutton_text()),
		.photoId = data.vphoto()
			? history->session().data().processPhoto(*data.vphoto())->id
			: PhotoId(0),
		.backgroundEmojiId = data.vcolor().has_value()
			? data.vcolor()->data().vbackground_emoji_id().value_or_empty()
			: uint64(0),
		.colorIndex = uint8(data.vcolor().has_value()
			? data.vcolor()->data().vcolor().value_or_empty()
			: 0),
		.isLinkInternal = !UrlRequiresConfirmation(qs(data.vurl())),
		.isRecommended = data.is_recommended(),
		.canReport = data.is_can_report(),
	};
	auto sponsorInfo = data.vsponsor_info()
		? tr::lng_sponsored_info_submenu(
			tr::now,
			lt_text,
			{ .text = qs(*data.vsponsor_info()) },
			Ui::Text::RichLangValue)
		: TextWithEntities();
	auto additionalInfo = TextWithEntities::Simple(
		data.vadditional_info() ? qs(*data.vadditional_info()) : QString());
	auto sharedMessage = SponsoredMessage{
		.randomId = randomId,
		.from = from,
		.textWithEntities = {
			.text = qs(data.vmessage()),
			.entities = Api::EntitiesFromMTP(
				_session,
				data.ventities().value_or_empty()),
		},
		.history = history,
		.link = from.link,
		.sponsorInfo = std::move(sponsorInfo),
		.additionalInfo = std::move(additionalInfo),
	};
	list.entries.push_back({ nullptr, {}, std::move(sharedMessage) });
}

void SponsoredMessages::clearItems(not_null<History*> history) {
	const auto it = _data.find(history);
	if (it == end(_data)) {
		return;
	}
	auto &list = it->second;
	for (auto &entry : list.entries) {
		entry.item.reset();
	}
	list.showedAll = false;
	list.injectedCount = 0;
}

const SponsoredMessages::Entry *SponsoredMessages::find(
		const FullMsgId &fullId) const {
	if (!peerIsChannel(fullId.peer)) {
		return nullptr;
	}
	const auto history = _session->data().history(fullId.peer);
	const auto it = _data.find(history);
	if (it == end(_data)) {
		return nullptr;
	}
	auto &list = it->second;
	const auto entryIt = ranges::find_if(list.entries, [&](const Entry &e) {
		return e.itemFullId == fullId;
	});
	if (entryIt == end(list.entries)) {
		return nullptr;
	}
	return &*entryIt;
}

void SponsoredMessages::view(const FullMsgId &fullId) {
	const auto entryPtr = find(fullId);
	if (!entryPtr) {
		return;
	}
	const auto randomId = entryPtr->sponsored.randomId;
	auto &request = _viewRequests[randomId];
	if (request.requestId || TooEarlyForRequest(request.lastReceived)) {
		return;
	}
	const auto channel = entryPtr->item->history()->peer->asChannel();
	Assert(channel != nullptr);
	request.requestId = _session->api().request(
		MTPchannels_ViewSponsoredMessage(
			channel->inputChannel,
			MTP_bytes(randomId))
	).done([=] {
		auto &request = _viewRequests[randomId];
		request.lastReceived = crl::now();
		request.requestId = 0;
	}).fail([=] {
		_viewRequests.remove(randomId);
	}).send();
}

SponsoredMessages::Details SponsoredMessages::lookupDetails(
		const FullMsgId &fullId) const {
	const auto entryPtr = find(fullId);
	if (!entryPtr) {
		return {};
	}
	const auto &data = entryPtr->sponsored;

	using InfoList = std::vector<TextWithEntities>;
	auto info = (!data.sponsorInfo.text.isEmpty()
			&& !data.additionalInfo.text.isEmpty())
		? InfoList{ data.sponsorInfo, data.additionalInfo }
		: !data.sponsorInfo.text.isEmpty()
		? InfoList{ data.sponsorInfo }
		: !data.additionalInfo.text.isEmpty()
		? InfoList{ data.additionalInfo }
		: InfoList{};
	return {
		.info = std::move(info),
		.link = data.link,
		.buttonText = data.from.buttonText,
		.photoId = data.from.photoId,
		.backgroundEmojiId = data.from.backgroundEmojiId,
		.colorIndex = data.from.colorIndex,
		.isLinkInternal = data.from.isLinkInternal,
		.canReport = data.from.canReport,
	};
}

void SponsoredMessages::clicked(const FullMsgId &fullId) {
	const auto entryPtr = find(fullId);
	if (!entryPtr) {
		return;
	}
	const auto randomId = entryPtr->sponsored.randomId;
	const auto channel = entryPtr->item->history()->peer->asChannel();
	Assert(channel != nullptr);
	_session->api().request(MTPchannels_ClickSponsoredMessage(
		channel->inputChannel,
		MTP_bytes(randomId)
	)).send();
}


auto SponsoredMessages::createReportCallback(const FullMsgId &fullId)
-> Fn<void(SponsoredReportResult::Id, Fn<void(SponsoredReportResult)>)> {
	using TLChoose = MTPDchannels_sponsoredMessageReportResultChooseOption;
	using TLAdsHidden = MTPDchannels_sponsoredMessageReportResultAdsHidden;
	using TLReported = MTPDchannels_sponsoredMessageReportResultReported;
	using Result = SponsoredReportResult;

	struct State final {
#ifdef _DEBUG
		~State() {
			qDebug() << "SponsoredMessages Report ~State().";
		}
#endif
		mtpRequestId requestId = 0;
	};
	const auto state = std::make_shared<State>();

	return [=](Result::Id optionId, Fn<void(Result)> done) {
		const auto entry = find(fullId);
		if (!entry) {
			return;
		}

		const auto history = entry->item->history();
		const auto channel = history->peer->asChannel();
		if (!channel) {
			return;
		}

		const auto erase = [=] {
			const auto it = _data.find(history);
			if (it != end(_data)) {
				auto &list = it->second.entries;
				const auto proj = [&](const Entry &e) {
					return e.itemFullId == fullId;
				};
				list.erase(ranges::remove_if(list, proj), end(list));
			}
		};

		if (optionId == Result::Id("-1")) {
			erase();
			return;
		}

		state->requestId = _session->api().request(
			MTPchannels_ReportSponsoredMessage(
				channel->inputChannel,
				MTP_bytes(entry->sponsored.randomId),
				MTP_bytes(optionId))
		).done([=](
				const MTPchannels_SponsoredMessageReportResult &result,
				mtpRequestId requestId) {
			if (state->requestId != requestId) {
				return;
			}
			state->requestId = 0;
			done(result.match([&](const TLChoose &data) {
				const auto t = qs(data.vtitle());
				auto list = Result::Options();
				list.reserve(data.voptions().v.size());
				for (const auto &tl : data.voptions().v) {
					list.emplace_back(Result::Option{
						.id = tl.data().voption().v,
						.text = qs(tl.data().vtext()),
					});
				}
				return Result{ .options = std::move(list), .title = t };
			}, [](const TLAdsHidden &data) -> Result {
				return { .result = Result::FinalStep::Hidden };
			}, [&](const TLReported &data) -> Result {
				erase();
				if (optionId == Result::Id("1")) { // I don't like it.
					return { .result = Result::FinalStep::Silence };
				}
				return { .result = Result::FinalStep::Reported };
			}));
		}).fail([=](const MTP::Error &error) {
			state->requestId = 0;
			if (error.type() == u"PREMIUM_ACCOUNT_REQUIRED"_q) {
				done({ .result = Result::FinalStep::Premium });
			} else {
				done({ .error = error.type() });
			}
		}).send();
	};
}

SponsoredMessages::State SponsoredMessages::state(
		not_null<History*> history) const {
	const auto it = _data.find(history);
	return (it == end(_data)) ? State::None : it->second.state;
}

} // namespace Data
