/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/sponsored_messages.h"

#include "api/api_text_entities.h"
#include "api/api_peer_search.h" // SponsoredSearchResult
#include "apiwrap.h"
#include "core/click_handler_types.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_media_preload.h"
#include "data/data_peer_values.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/sponsored_message_bar.h"
#include "ui/text/text_utilities.h" // Ui::Text::RichLangValue.

namespace Data {
namespace {

constexpr auto kMs = crl::time(1000);
constexpr auto kRequestTimeLimit = 5 * 60 * crl::time(1000);

const auto kFlaggedPreload = ((MediaPreload*)quintptr(0x01));

[[nodiscard]] bool TooEarlyForRequest(crl::time received) {
	return (received > 0) && (received + kRequestTimeLimit > crl::now());
}

template <typename Fields>
[[nodiscard]] std::vector<TextWithEntities> Prepare(const Fields &fields) {
	using InfoList = std::vector<TextWithEntities>;
	return (!fields.sponsorInfo.text.isEmpty()
		&& !fields.additionalInfo.text.isEmpty())
		? InfoList{ fields.sponsorInfo, fields.additionalInfo }
		: !fields.sponsorInfo.text.isEmpty()
		? InfoList{ fields.sponsorInfo }
		: !fields.additionalInfo.text.isEmpty()
		? InfoList{ fields.additionalInfo }
		: InfoList{};
}

} // namespace

SponsoredMessages::SponsoredMessages(not_null<Main::Session*> session)
: _session(session)
, _clearTimer([=] { clearOldRequests(); }) {
	Data::AmPremiumValue(
		_session
	) | rpl::start_with_next([=](bool premium) {
		if (premium) {
			clear();
		}
	}, _lifetime);
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
	const auto clear = [&](auto &requests) {
		while (true) {
			const auto i = ranges::find_if(requests, [&](const auto &value) {
				const auto &request = value.second;
				return !request.requestId
					&& (request.lastReceived + kRequestTimeLimit <= now);
			});
			if (i == end(requests)) {
				break;
			}
			requests.erase(i);
		}
	};
	clear(_requests);
	clear(_requestsForVideo);
}

SponsoredMessages::AppendResult SponsoredMessages::append(
		not_null<History*> history) {
	if (isTopBarFor(history)) {
		return SponsoredMessages::AppendResult::None;
	}
	const auto it = _data.find(history);
	if (it == end(_data)) {
		return SponsoredMessages::AppendResult::None;
	}
	auto &list = it->second;
	if (list.showedAll
		|| !TooEarlyForRequest(list.received)
		|| list.postsBetween) {
		return SponsoredMessages::AppendResult::None;
	}

	const auto entryIt = ranges::find_if(list.entries, [](const Entry &e) {
		return e.item == nullptr;
	});
	if (entryIt == end(list.entries)) {
		list.showedAll = true;
		return SponsoredMessages::AppendResult::None;
	} else if (entryIt->preload) {
		return SponsoredMessages::AppendResult::MediaLoading;
	}
	entryIt->item.reset(history->addSponsoredMessage(
		entryIt->itemFullId.msg,
		entryIt->sponsored.from,
		entryIt->sponsored.textWithEntities));

	return SponsoredMessages::AppendResult::Appended;
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
		const auto messages = [&]() -> const std::vector<ViewPtr>& {
			return (*blockIt)->messages;
		};
		auto lastViewIt = ranges::find(messages(), lastView, &ViewPtr::get);
		auto appendAtLeastToEnd = false;
		while ((summaryBetween < list.postsBetween)
			|| (summaryHeight < betweenHeight)) {
			lastViewIt++;
			if (lastViewIt == end(messages())) {
				blockIt++;
				if (blockIt != end(history->blocks)) {
					lastViewIt = begin(messages());
				} else {
					if (!list.injectedCount) {
						appendAtLeastToEnd = true;
						break;
					}
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
		if (appendAtLeastToEnd) {
			entryIt->item.reset(history->addSponsoredMessage(
				entryIt->itemFullId.msg,
				entryIt->sponsored.from,
				entryIt->sponsored.textWithEntities));
		} else {
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
		}
		list.injectedCount++;
	}
}

bool SponsoredMessages::canHaveFor(not_null<History*> history) const {
	if (history->peer->isChannel()) {
		return true;
	} else if (const auto user = history->peer->asUser()) {
		return user->isBot();
	}
	return false;
}

bool SponsoredMessages::canHaveFor(not_null<HistoryItem*> item) const {
	return item->history()->peer->isBroadcast()
		&& item->isRegular();
}

bool SponsoredMessages::isTopBarFor(not_null<History*> history) const {
	if (peerIsUser(history->peer->id)) {
		if (const auto user = history->peer->asUser()) {
			return user->isBot();
		}
	}
	return false;
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
	request.requestId = _session->api().request(
		MTPmessages_GetSponsoredMessages(
			MTP_flags(0),
			history->peer->input,
			MTPint()) // msg_id
	).done([=](const MTPmessages_sponsoredMessages &result) {
		parse(history, result);
		if (done) {
			done();
		}
	}).fail([=] {
		_requests.remove(history);
	}).send();
}

void SponsoredMessages::requestForVideo(
		not_null<HistoryItem*> item,
		Fn<void(SponsoredForVideo)> done) {
	Expects(done != nullptr);

	if (!canHaveFor(item)) {
		done({});
		return;
	}
	const auto peer = item->history()->peer;
	auto &request = _requestsForVideo[peer];
	if (TooEarlyForRequest(request.lastReceived)) {
		auto prepared = prepareForVideo(peer);
		if (prepared.list.empty()
			|| prepared.state.itemIndex < prepared.list.size()
			|| prepared.state.leftTillShow > 0) {
			done(std::move(prepared));
			return;
		}
	}
	request.callbacks.push_back(std::move(done));
	if (request.requestId) {
		return;
	}
	{
		const auto it = _dataForVideo.find(peer);
		if (it != end(_dataForVideo)) {
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
	const auto finish = [=] {
		const auto i = _requestsForVideo.find(peer);
		if (i != end(_requestsForVideo)) {
			for (const auto &callback : base::take(i->second.callbacks)) {
				callback(prepareForVideo(peer));
			}
		}
	};
	using Flag = MTPmessages_GetSponsoredMessages::Flag;
	request.requestId = _session->api().request(
		MTPmessages_GetSponsoredMessages(
			MTP_flags(Flag::f_msg_id),
			peer->input,
			MTP_int(item->id.bare))
	).done([=](const MTPmessages_sponsoredMessages &result) {
		parseForVideo(peer, result);
		finish();
	}).fail([=] {
		_requestsForVideo.remove(peer);
		finish();
	}).send();
}

void SponsoredMessages::updateForVideo(
		FullMsgId itemId,
		SponsoredForVideoState state) {
	if (state.initial()) {
		return;
	}
	const auto i = _dataForVideo.find(_session->data().peer(itemId.peer));
	if (i != end(_dataForVideo)) {
		i->second.state = state;
	}
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
		auto &list = _data.emplace(history).first->second;
		list.entries.clear();
		list.received = crl::now();
		if (const auto postsBetween = data.vposts_between()) {
			list.postsBetween = postsBetween->v;
			list.state = State::InjectToMiddle;
		} else {
			list.state = history->peer->isChannel()
				? State::AppendToEnd
				: State::AppendToTopBar;
		}
		for (const auto &message : messages) {
			append([=] {
				return &_data[history].entries;
			}, history, message);
		}
	}, [](const MTPDmessages_sponsoredMessagesEmpty &) {
	});
}

void SponsoredMessages::parseForVideo(
		not_null<PeerData*> peer,
		const MTPmessages_sponsoredMessages &list) {
	auto &request = _requestsForVideo[peer];
	request.lastReceived = crl::now();
	request.requestId = 0;
	if (!_clearTimer.isActive()) {
		_clearTimer.callOnce(kRequestTimeLimit * 2);
	}

	list.match([&](const MTPDmessages_sponsoredMessages &data) {
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());

		const auto history = _session->data().history(peer);
		const auto &messages = data.vmessages().v;
		auto &list = _dataForVideo.emplace(peer).first->second;
		list.entries.clear();
		list.received = crl::now();
		list.startDelay = data.vstart_delay().value_or_empty() * kMs;
		list.betweenDelay = data.vbetween_delay().value_or_empty() * kMs;
		for (const auto &message : messages) {
			append([=] {
				return &_dataForVideo[peer].entries;
			}, history, message);
		}
	}, [](const MTPDmessages_sponsoredMessagesEmpty &) {
	});
}

SponsoredForVideo SponsoredMessages::prepareForVideo(
		not_null<PeerData*> peer) {
	const auto i = _dataForVideo.find(peer);
	if (i == end(_dataForVideo) || i->second.entries.empty()) {
		return {};
	}
	return SponsoredForVideo{
		.list = i->second.entries | ranges::views::transform(
			&Entry::sponsored
		) | ranges::to_vector,
		.startDelay = i->second.startDelay,
		.betweenDelay = i->second.betweenDelay,
		.state = i->second.state,
	};
}

FullMsgId SponsoredMessages::fillTopBar(
		not_null<History*> history,
		not_null<Ui::RpWidget*> widget) {
	const auto it = _data.find(history);
	if (it != end(_data)) {
		auto &list = it->second;
		if (!list.entries.empty()) {
			const auto &entry = list.entries.front();
			const auto fullId = entry.itemFullId;
			Ui::FillSponsoredMessageBar(
				widget,
				_session,
				fullId,
				entry.sponsored.from,
				entry.sponsored.textWithEntities);
			return fullId;
		}
	}
	return {};
}

rpl::producer<> SponsoredMessages::itemRemoved(const FullMsgId &fullId) {
	if (IsServerMsgId(fullId.msg) || !fullId) {
		return rpl::never<>();
	}
	const auto history = _session->data().history(fullId.peer);
	const auto it = _data.find(history);
	if (it == end(_data)) {
		return rpl::never<>();
	}
	auto &list = it->second;
	const auto entryIt = ranges::find_if(list.entries, [&](const Entry &e) {
		return e.itemFullId == fullId;
	});
	if (entryIt == end(list.entries)) {
		return rpl::never<>();
	}
	if (!entryIt->optionalDestructionNotifier) {
		entryIt->optionalDestructionNotifier
			= std::make_unique<rpl::lifetime>();
		entryIt->optionalDestructionNotifier->add([this, fullId] {
			_itemRemoved.fire_copy(fullId);
		});
	}
	return _itemRemoved.events(
	) | rpl::filter(rpl::mappers::_1 == fullId) | rpl::to_empty;
}

void SponsoredMessages::append(
		Fn<not_null<std::vector<Entry>*>()> entries,
		not_null<History*> history,
		const MTPSponsoredMessage &message) {
	const auto &data = message.data();
	const auto randomId = data.vrandom_id().v;
	auto mediaPhoto = (PhotoData*)nullptr;
	auto mediaDocument = (DocumentData*)nullptr;
	{
		if (data.vmedia()) {
			data.vmedia()->match([&](const MTPDmessageMediaPhoto &media) {
				if (const auto tlPhoto = media.vphoto()) {
					tlPhoto->match([&](const MTPDphoto &data) {
						mediaPhoto = _session->data().processPhoto(data);
					}, [](const MTPDphotoEmpty &) {
					});
				}
			}, [&](const MTPDmessageMediaDocument &media) {
				if (const auto tlDocument = media.vdocument()) {
					tlDocument->match([&](const MTPDdocument &data) {
						const auto d = _session->data().processDocument(
							data,
							media.valt_documents());
						if (d->isVideoFile()
							|| d->isSilentVideo()
							|| d->isAnimation()
							|| d->isGifv()) {
							mediaDocument = d;
						}
					}, [](const MTPDdocumentEmpty &) {
					});
				}
			}, [](const auto &) {
			});
		}
	};
	const auto from = SponsoredFrom{
		.title = qs(data.vtitle()),
		.link = qs(data.vurl()),
		.buttonText = qs(data.vbutton_text()),
		.photoId = data.vphoto()
			? _session->data().processPhoto(*data.vphoto())->id
			: PhotoId(0),
		.mediaPhotoId = (mediaPhoto ? mediaPhoto->id : 0),
		.mediaDocumentId = (mediaDocument ? mediaDocument->id : 0),
		.backgroundEmojiId = BackgroundEmojiIdFromColor(data.vcolor()),
		.colorIndex = ColorIndexFromColor(data.vcolor()),
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
		.durationMin = data.vmin_display_duration().value_or_empty() * kMs,
		.durationMax = data.vmax_display_duration().value_or_empty() * kMs,
	};
	const auto itemId = FullMsgId(
		history->peer->id,
		_session->data().nextLocalMessageId());
	const auto list = entries();
	list->push_back({
		.itemFullId = itemId,
		.sponsored = std::move(sharedMessage),
	});
	auto &entry = list->back();
	const auto fileOrigin = FileOrigin(); // No way to refresh in ads.

	const auto preloaded = [=] {
		const auto list = entries();
		const auto j = ranges::find(*list, itemId, &Entry::itemFullId);
		if (j == end(*list)) {
			return;
		}
		auto &entry = *j;
		if (entry.preload.get() == kFlaggedPreload) {
			entry.preload.release();
		} else {
			entry.preload = nullptr;
		}
	};

	auto preload = std::unique_ptr<MediaPreload>();
	entry.preload.reset(kFlaggedPreload);
	if (mediaPhoto) {
		preload = std::make_unique<PhotoPreload>(
			mediaPhoto,
			fileOrigin,
			preloaded);
	} else if (mediaDocument && VideoPreload::Can(mediaDocument)) {
		preload = std::make_unique<VideoPreload>(
			mediaDocument,
			fileOrigin,
			preloaded);
	}
	// Preload constructor may have called preloaded(), which zero-ed
	// entry.preload, that way we're ready and don't need to save it.
	// Otherwise we're preloading and need to save the task.
	if (entry.preload.get() == kFlaggedPreload) {
		entry.preload.release();
		if (preload) {
			entry.preload = std::move(preload);
		}
	}
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
	if (!peerIsChannel(fullId.peer) && !peerIsUser(fullId.peer)) {
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
	view(entryPtr->sponsored.randomId);
}

void SponsoredMessages::view(const QByteArray &randomId) {
	auto &request = _viewRequests[randomId];
	if (request.requestId || TooEarlyForRequest(request.lastReceived)) {
		return;
	}
	request.requestId = _session->api().request(
		MTPmessages_ViewSponsoredMessage(MTP_bytes(randomId))
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
	return lookupDetails(entryPtr->sponsored);
}

SponsoredMessages::Details SponsoredMessages::lookupDetails(
		const SponsoredMessage &data) const {
	return {
		.info = Prepare(data),
		.link = data.link,
		.buttonText = data.from.buttonText,
		.photoId = data.from.photoId,
		.mediaPhotoId = data.from.mediaPhotoId,
		.mediaDocumentId = data.from.mediaDocumentId,
		.backgroundEmojiId = data.from.backgroundEmojiId,
		.colorIndex = data.from.colorIndex,
		.isLinkInternal = data.from.isLinkInternal,
		.canReport = data.from.canReport,
	};
}

SponsoredMessages::Details SponsoredMessages::lookupDetails(
		const Api::SponsoredSearchResult &data) const {
	return {
		.info = Prepare(data),
		.canReport = true,
	};
}

void SponsoredMessages::clicked(
		const FullMsgId &fullId,
		bool isMedia,
		bool isFullscreen) {
	const auto entryPtr = find(fullId);
	if (!entryPtr) {
		return;
	}
	clicked(entryPtr->sponsored.randomId, isMedia, isFullscreen);
}

void SponsoredMessages::clicked(
		const QByteArray &randomId,
		bool isMedia,
		bool isFullscreen) {
	using Flag = MTPmessages_ClickSponsoredMessage::Flag;
	_session->api().request(MTPmessages_ClickSponsoredMessage(
		MTP_flags(Flag(0)
			| (isMedia ? Flag::f_media : Flag(0))
			| (isFullscreen ? Flag::f_fullscreen : Flag(0))),
		MTP_bytes(randomId)
	)).send();
}

SponsoredReportAction SponsoredMessages::createReportCallback(
		const FullMsgId &fullId) {
	const auto entry = find(fullId);
	if (!entry) {
		return { .callback = [=](const auto &...) {} };
	}
	const auto history = _session->data().history(fullId.peer);
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
	return createReportCallback(entry->sponsored.randomId, erase);
}

SponsoredReportAction SponsoredMessages::createReportCallback(
		const QByteArray &randomId,
		Fn<void()> erase) {
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

	return { .callback = [=](Result::Id optionId, Fn<void(Result)> done) {
		if (optionId == Result::Id("-1")) {
			erase();
			return;
		}

		state->requestId = _session->api().request(
			MTPmessages_ReportSponsoredMessage(
				MTP_bytes(randomId),
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
	} };
}

SponsoredMessages::State SponsoredMessages::state(
		not_null<History*> history) const {
	const auto it = _data.find(history);
	return (it == end(_data)) ? State::None : it->second.state;
}

} // namespace Data
