/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_provider.h"

#include "info/media/info_media_widget.h"
#include "info/media/info_media_list_section.h"
#include "info/info_controller.h"
#include "layout/layout_selection.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_session.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_peer_values.h"
#include "data/data_document.h"
#include "styles/style_info.h"

namespace Info::Media {
namespace {

constexpr auto kPreloadedScreensCount = 4;
constexpr auto kPreloadedScreensCountFull
	= kPreloadedScreensCount + 1 + kPreloadedScreensCount;

[[nodiscard]] int MinItemHeight(Type type, int width) {
	auto &songSt = st::overviewFileLayout;

	switch (type) {
	case Type::Photo:
	case Type::GIF:
	case Type::Video:
	case Type::RoundFile: {
		auto itemsLeft = st::infoMediaSkip;
		auto itemsInRow = (width - itemsLeft)
			/ (st::infoMediaMinGridSize + st::infoMediaSkip);
		return (st::infoMediaMinGridSize + st::infoMediaSkip) / itemsInRow;
	} break;

	case Type::RoundVoiceFile:
		return songSt.songPadding.top() + songSt.songThumbSize + songSt.songPadding.bottom() + st::lineWidth;
	case Type::File:
		return songSt.filePadding.top() + songSt.fileThumbSize + songSt.filePadding.bottom() + st::lineWidth;
	case Type::MusicFile:
		return songSt.songPadding.top() + songSt.songThumbSize + songSt.songPadding.bottom();
	case Type::Link:
		return st::linksPhotoSize + st::linksMargin.top() + st::linksMargin.bottom() + st::linksBorder;
	}
	Unexpected("Type in MinItemHeight()");
}

} // namespace

Provider::Provider(not_null<AbstractController*> controller)
: _controller(controller)
, _peer(_controller->key().peer())
, _migrated(_controller->migrated())
, _type(_controller->section().mediaType())
, _slice(sliceKey(_universalAroundId)) {
	_controller->session().data().itemRemoved(
	) | rpl::start_with_next([this](auto item) {
		itemRemoved(item);
	}, _lifetime);

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &layout : _layouts) {
			layout.second.item->invalidateCache();
		}
	}, _lifetime);
}

Type Provider::type() {
	return _type;
}

bool Provider::hasSelectRestriction() {
	if (_peer->allowsForwarding()) {
		return false;
	} else if (const auto chat = _peer->asChat()) {
		return !chat->canDeleteMessages();
	} else if (const auto channel = _peer->asChannel()) {
		return !channel->canDeleteMessages();
	}
	return true;
}

rpl::producer<bool> Provider::hasSelectRestrictionChanges() {
	if (_peer->isUser()) {
		return rpl::never<bool>();
	}
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	auto noForwards = chat
		? Data::PeerFlagValue(chat, ChatDataFlag::NoForwards)
		: Data::PeerFlagValue(
			channel,
			ChannelDataFlag::NoForwards
		) | rpl::type_erased();

	auto rights = chat
		? chat->adminRightsValue()
		: channel->adminRightsValue();
	auto canDelete = std::move(
		rights
	) | rpl::map([=] {
		return chat
			? chat->canDeleteMessages()
			: channel->canDeleteMessages();
	});
	return rpl::combine(
		std::move(noForwards),
		std::move(canDelete)
	) | rpl::map([=] {
		return hasSelectRestriction();
	}) | rpl::distinct_until_changed() | rpl::skip(1);
}

bool Provider::sectionHasFloatingHeader() {
	switch (_type) {
	case Type::Photo:
	case Type::GIF:
	case Type::Video:
	case Type::RoundFile:
	case Type::RoundVoiceFile:
	case Type::MusicFile:
		return false;
	case Type::File:
	case Type::Link:
		return true;
	}
	Unexpected("Type in HasFloatingHeader()");
}

QString Provider::sectionTitle(not_null<const BaseLayout*> item) {
	switch (_type) {
	case Type::Photo:
	case Type::GIF:
	case Type::Video:
	case Type::RoundFile:
	case Type::RoundVoiceFile:
	case Type::File:
		return langMonthFull(item->dateTime().date());

	case Type::Link:
		return langDayOfMonthFull(item->dateTime().date());

	case Type::MusicFile:
		return QString();
	}
	Unexpected("Type in ListSection::setHeader()");
}

bool Provider::sectionItemBelongsHere(
		not_null<const BaseLayout*> item,
		not_null<const BaseLayout*> previous) {
	const auto date = item->dateTime().date();
	const auto sectionDate = previous->dateTime().date();

	switch (_type) {
	case Type::Photo:
	case Type::GIF:
	case Type::Video:
	case Type::RoundFile:
	case Type::RoundVoiceFile:
	case Type::File:
		return date.year() == sectionDate.year()
			&& date.month() == sectionDate.month();

	case Type::Link:
		return date == sectionDate;

	case Type::MusicFile:
		return true;
	}
	Unexpected("Type in ListSection::belongsHere()");
}

bool Provider::isPossiblyMyItem(not_null<const HistoryItem*> item) {
	return isPossiblyMyPeerId(item->history()->peer->id);
}

bool Provider::isPossiblyMyPeerId(PeerId peerId) const {
	return (peerId == _peer->id) || (_migrated && peerId == _migrated->id);
}

std::optional<int> Provider::fullCount() {
	return _slice.fullCount();
}

void Provider::restart() {
	_layouts.clear();
	_universalAroundId = kDefaultAroundId;
	_idsLimit = kMinimalIdsLimit;
	_slice = SparseIdsMergedSlice(sliceKey(_universalAroundId));
	refreshViewer();
}

void Provider::checkPreload(
		QSize viewport,
		not_null<BaseLayout*> topLayout,
		not_null<BaseLayout*> bottomLayout,
		bool preloadTop,
		bool preloadBottom) {
	const auto visibleWidth = viewport.width();
	const auto visibleHeight = viewport.height();
	const auto preloadedHeight = kPreloadedScreensCountFull * visibleHeight;
	const auto minItemHeight = MinItemHeight(_type, visibleWidth);
	const auto preloadedCount = preloadedHeight / minItemHeight;
	const auto preloadIdsLimitMin = (preloadedCount / 2) + 1;
	const auto preloadIdsLimit = preloadIdsLimitMin
		+ (visibleHeight / minItemHeight);
	const auto after = _slice.skippedAfter();
	const auto topLoaded = after && (*after == 0);
	const auto before = _slice.skippedBefore();
	const auto bottomLoaded = before && (*before == 0);

	const auto minScreenDelta = kPreloadedScreensCount
		- kPreloadIfLessThanScreens;
	const auto minUniversalIdDelta = (minScreenDelta * visibleHeight)
		/ minItemHeight;
	const auto preloadAroundItem = [&](not_null<BaseLayout*> layout) {
		auto preloadRequired = false;
		auto universalId = GetUniversalId(layout);
		if (!preloadRequired) {
			preloadRequired = (_idsLimit < preloadIdsLimitMin);
		}
		if (!preloadRequired) {
			auto delta = _slice.distance(
				sliceKey(_universalAroundId),
				sliceKey(universalId));
			Assert(delta != std::nullopt);
			preloadRequired = (qAbs(*delta) >= minUniversalIdDelta);
		}
		if (preloadRequired) {
			_idsLimit = preloadIdsLimit;
			_universalAroundId = universalId;
			refreshViewer();
		}
	};

	if (preloadTop && !topLoaded) {
		preloadAroundItem(topLayout);
	} else if (preloadBottom && !bottomLoaded) {
		preloadAroundItem(bottomLayout);
	}
}

void Provider::refreshViewer() {
	_viewerLifetime.destroy();
	const auto idForViewer = sliceKey(_universalAroundId).universalId;
	_controller->mediaSource(
		idForViewer,
		_idsLimit,
		_idsLimit
	) | rpl::start_with_next([=](SparseIdsMergedSlice &&slice) {
		if (!slice.fullCount()) {
			// Don't display anything while full count is unknown.
			return;
		}
		_slice = std::move(slice);
		if (auto nearest = _slice.nearest(idForViewer)) {
			_universalAroundId = GetUniversalId(*nearest);
		}
		_refreshed.fire({});
	}, _viewerLifetime);
}

rpl::producer<> Provider::refreshed() {
	return _refreshed.events();
}

std::vector<ListSection> Provider::fillSections(
		not_null<Overview::Layout::Delegate*> delegate) {
	markLayoutsStale();
	const auto guard = gsl::finally([&] { clearStaleLayouts(); });

	auto result = std::vector<ListSection>();
	auto section = ListSection(_type, sectionDelegate());
	auto count = _slice.size();
	for (auto i = count; i != 0;) {
		auto universalId = GetUniversalId(_slice[--i]);
		if (auto layout = getLayout(universalId, delegate)) {
			if (!section.addItem(layout)) {
				section.finishSection();
				result.push_back(std::move(section));
				section = ListSection(_type, sectionDelegate());
				section.addItem(layout);
			}
		}
	}
	if (!section.empty()) {
		section.finishSection();
		result.push_back(std::move(section));
	}
	return result;
}

void Provider::markLayoutsStale() {
	for (auto &layout : _layouts) {
		layout.second.stale = true;
	}
}

void Provider::clearStaleLayouts() {
	for (auto i = _layouts.begin(); i != _layouts.end();) {
		if (i->second.stale) {
			_layoutRemoved.fire(i->second.item.get());
			i = _layouts.erase(i);
		} else {
			++i;
		}
	}
}

rpl::producer<not_null<BaseLayout*>> Provider::layoutRemoved() {
	return _layoutRemoved.events();
}

BaseLayout *Provider::lookupLayout(
		const HistoryItem *item) {
	const auto i = _layouts.find(GetUniversalId(item));
	return (i != _layouts.end()) ? i->second.item.get() : nullptr;
}

bool Provider::isMyItem(not_null<const HistoryItem*> item) {
	const auto peer = item->history()->peer;
	return (_peer == peer) || (_migrated == peer);
}

bool Provider::isAfter(
		not_null<const HistoryItem*> a,
		not_null<const HistoryItem*> b) {
	return (GetUniversalId(a) < GetUniversalId(b));
}

SparseIdsMergedSlice::Key Provider::sliceKey(
		UniversalMsgId universalId) const {
	using Key = SparseIdsMergedSlice::Key;
	if (_migrated) {
		return Key(_peer->id, _migrated->id, universalId);
	}
	if (universalId < 0) {
		// Convert back to plain id for non-migrated histories.
		universalId = universalId + ServerMaxMsgId;
	}
	return Key(_peer->id, 0, universalId);
}

void Provider::itemRemoved(not_null<const HistoryItem*> item) {
	const auto id = GetUniversalId(item);
	if (const auto i = _layouts.find(id); i != end(_layouts)) {
		_layoutRemoved.fire(i->second.item.get());
		_layouts.erase(i);
	}
}

FullMsgId Provider::computeFullId(
		UniversalMsgId universalId) const {
	Expects(universalId != 0);

	return (universalId > 0)
		? FullMsgId(_peer->id, universalId)
		: FullMsgId(
			(_migrated ? _migrated : _peer.get())->id,
			ServerMaxMsgId + universalId);
}

BaseLayout *Provider::getLayout(
		UniversalMsgId universalId,
		not_null<Overview::Layout::Delegate*> delegate) {
	auto it = _layouts.find(universalId);
	if (it == _layouts.end()) {
		if (auto layout = createLayout(universalId, delegate, _type)) {
			layout->initDimensions();
			it = _layouts.emplace(
				universalId,
				std::move(layout)).first;
		} else {
			return nullptr;
		}
	}
	it->second.stale = false;
	return it->second.item.get();
}

std::unique_ptr<BaseLayout> Provider::createLayout(
		UniversalMsgId universalId,
		not_null<Overview::Layout::Delegate*> delegate,
		Type type) {
	const auto item = _controller->session().data().message(
		computeFullId(universalId));
	if (!item) {
		return nullptr;
	}
	const auto getPhoto = [&]() -> PhotoData* {
		if (const auto media = item->media()) {
			return media->photo();
		}
		return nullptr;
	};
	const auto getFile = [&]() -> DocumentData* {
		if (auto media = item->media()) {
			return media->document();
		}
		return nullptr;
	};

	const auto &songSt = st::overviewFileLayout;
	using namespace Overview::Layout;
	switch (type) {
	case Type::Photo:
		if (const auto photo = getPhoto()) {
			return std::make_unique<Photo>(delegate, item, photo);
		}
		return nullptr;
	case Type::GIF:
		if (const auto file = getFile()) {
			return std::make_unique<Gif>(delegate, item, file);
		}
		return nullptr;
	case Type::Video:
		if (const auto file = getFile()) {
			return std::make_unique<Video>(delegate, item, file);
		}
		return nullptr;
	case Type::File:
		if (const auto file = getFile()) {
			return std::make_unique<Document>(
				delegate,
				item,
				DocumentFields{ .document = file },
				songSt);
		}
		return nullptr;
	case Type::MusicFile:
		if (const auto file = getFile()) {
			return std::make_unique<Document>(
				delegate,
				item,
				DocumentFields{ .document = file },
				songSt);
		}
		return nullptr;
	case Type::RoundVoiceFile:
		if (const auto file = getFile()) {
			return std::make_unique<Voice>(delegate, item, file, songSt);
		}
		return nullptr;
	case Type::Link:
		return std::make_unique<Link>(delegate, item, item->media());
	case Type::RoundFile:
		return nullptr;
	}
	Unexpected("Type in ListWidget::createLayout()");
}

ListItemSelectionData Provider::computeSelectionData(
		not_null<const HistoryItem*> item,
		TextSelection selection) {
	auto result = ListItemSelectionData(selection);
	result.canDelete = item->canDelete();
	result.canForward = item->allowsForward();
	return result;
}

bool Provider::allowSaveFileAs(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) {
	return item->allowsForward();
}

QString Provider::showInFolderPath(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) {
	return document->filepath(true);
}

void Provider::applyDragSelection(
		ListSelectedMap &selected,
		not_null<const HistoryItem*> fromItem,
		bool skipFrom,
		not_null<const HistoryItem*> tillItem,
		bool skipTill) {
	const auto fromId = GetUniversalId(fromItem) - (skipFrom ? 1 : 0);
	const auto tillId = GetUniversalId(tillItem) - (skipTill ? 0 : 1);
	for (auto i = selected.begin(); i != selected.end();) {
		const auto itemId = GetUniversalId(i->first);
		if (itemId > fromId || itemId <= tillId) {
			i = selected.erase(i);
		} else {
			++i;
		}
	}
	for (auto &layoutItem : _layouts) {
		auto &&universalId = layoutItem.first;
		if (universalId <= fromId && universalId > tillId) {
			const auto item = layoutItem.second.item->getItem();
			ChangeItemSelection(
				selected,
				item,
				computeSelectionData(item, FullSelection));
		}
	}
}

int64 Provider::scrollTopStatePosition(not_null<HistoryItem*> item) {
	return GetUniversalId(item).bare;
}

HistoryItem *Provider::scrollTopStateItem(ListScrollTopState state) {
	if (state.item && _slice.indexOf(state.item->fullId())) {
		return state.item;
	} else if (const auto id = _slice.nearest(state.position)) {
		if (const auto item = _controller->session().data().message(*id)) {
			return item;
		}
	}
	return state.item;
}

void Provider::saveState(
		not_null<Memento*> memento,
		ListScrollTopState scrollState) {
	if (_universalAroundId != kDefaultAroundId && scrollState.item) {
		memento->setAroundId(computeFullId(_universalAroundId));
		memento->setIdsLimit(_idsLimit);
		memento->setScrollTopItem(scrollState.item->globalId());
		memento->setScrollTopItemPosition(scrollState.position);
		memento->setScrollTopShift(scrollState.shift);
	}
}

void Provider::restoreState(
		not_null<Memento*> memento,
		Fn<void(ListScrollTopState)> restoreScrollState) {
	if (const auto limit = memento->idsLimit()) {
		auto wasAroundId = memento->aroundId();
		if (isPossiblyMyPeerId(wasAroundId.peer)) {
			_idsLimit = limit;
			_universalAroundId = GetUniversalId(wasAroundId);
			restoreScrollState({
				.position = memento->scrollTopItemPosition(),
				.item = MessageByGlobalId(memento->scrollTopItem()),
				.shift = memento->scrollTopShift(),
			});
			refreshViewer();
		}
	}
}

} // namespace Info::Media
