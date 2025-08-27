/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/global_media/info_global_media_provider.h"

#include "apiwrap.h"
#include "info/media/info_media_widget.h"
#include "info/media/info_media_list_section.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "ui/text/format_song_document_name.h"
#include "ui/ui_utility.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history.h"
#include "core/application.h"
#include "storage/storage_shared_media.h"
#include "layout/layout_selection.h"
#include "styles/style_overview.h"

namespace Info::GlobalMedia {
namespace {

constexpr auto kPerPage = 50;
constexpr auto kPreloadedScreensCount = 4;
constexpr auto kPreloadedScreensCountFull
	= kPreloadedScreensCount + 1 + kPreloadedScreensCount;

} // namespace

GlobalMediaSlice::GlobalMediaSlice(
	Key key,
	std::vector<Data::MessagePosition> items,
	std::optional<int> fullCount,
	int skippedAfter)
: _key(key)
, _items(std::move(items))
, _fullCount(fullCount)
, _skippedAfter(skippedAfter) {
}

std::optional<int> GlobalMediaSlice::fullCount() const {
	return _fullCount;
}

std::optional<int> GlobalMediaSlice::skippedBefore() const {
	return _fullCount
		? int(*_fullCount - _skippedAfter - _items.size())
		: std::optional<int>();
}

std::optional<int> GlobalMediaSlice::skippedAfter() const {
	return _skippedAfter;
}

std::optional<int> GlobalMediaSlice::indexOf(Value position) const {
	const auto it = ranges::find(_items, position);
	return (it != end(_items))
		? std::make_optional(int(it - begin(_items)))
		: std::nullopt;
}

int GlobalMediaSlice::size() const {
	return _items.size();
}

GlobalMediaSlice::Value GlobalMediaSlice::operator[](int index) const {
	Expects(index >= 0 && index < size());

	return _items[index];
}

std::optional<int> GlobalMediaSlice::distance(
		const Key &a,
		const Key &b) const {
	const auto i = indexOf(a.aroundId);
	const auto j = indexOf(b.aroundId);
	return (i && j) ? std::make_optional(*j - *i) : std::nullopt;
}

std::optional<GlobalMediaSlice::Value> GlobalMediaSlice::nearest(
		Value position) const {
	if (_items.empty()) {
		return std::nullopt;
	}

	const auto it = ranges::lower_bound(
		_items,
		position,
		std::greater<>{});

	if (it == end(_items)) {
		return _items.back();
	} else if (it == begin(_items)) {
		return _items.front();
	}
	return *it;
}

Provider::Provider(not_null<AbstractController*> controller)
: _controller(controller)
, _type(_controller->section().mediaType())
, _slice(sliceKey(_aroundId)) {
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

Provider::Type Provider::type() {
	return _type;
}

bool Provider::hasSelectRestriction() {
	return true;
}

rpl::producer<bool> Provider::hasSelectRestrictionChanges() {
	return rpl::never<bool>();
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
	return QString();
}

bool Provider::sectionItemBelongsHere(
		not_null<const BaseLayout*> item,
		not_null<const BaseLayout*> previous) {
	return true;
}

bool Provider::isPossiblyMyItem(not_null<const HistoryItem*> item) {
	return item->media() != nullptr;
}

std::optional<int> Provider::fullCount() {
	return _slice.fullCount();
}

void Provider::restart() {
	_layouts.clear();
	_aroundId = Data::MaxMessagePosition;
	_idsLimit = kMinimalIdsLimit;
	_slice = GlobalMediaSlice(sliceKey(_aroundId));
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
	const auto minItemHeight = Media::MinItemHeight(_type, visibleWidth);
	const auto preloadedCount = preloadedHeight / minItemHeight;
	const auto preloadIdsLimitMin = (preloadedCount / 2) + 1;
	const auto preloadIdsLimit = preloadIdsLimitMin
		+ (visibleHeight / minItemHeight);
	const auto after = _slice.skippedAfter();
	const auto topLoaded = after && (*after == 0);
	const auto before = _slice.skippedBefore();
	const auto bottomLoaded = before && (*before == 0);

	const auto minScreenDelta = kPreloadedScreensCount
		- Media::kPreloadIfLessThanScreens;
	const auto minUniversalIdDelta = (minScreenDelta * visibleHeight)
		/ minItemHeight;
	const auto preloadAroundItem = [&](not_null<BaseLayout*> layout) {
		auto preloadRequired = false;
		auto aroundId = layout->getItem()->position();
		if (!preloadRequired) {
			preloadRequired = (_idsLimit < preloadIdsLimitMin);
		}
		if (!preloadRequired) {
			auto delta = _slice.distance(
				sliceKey(_aroundId),
				sliceKey(aroundId));
			Assert(delta != std::nullopt);
			preloadRequired = (qAbs(*delta) >= minUniversalIdDelta);
		}
		if (preloadRequired) {
			_idsLimit = preloadIdsLimit;
			_aroundId = aroundId;
			refreshViewer();
		}
	};

	if (preloadTop && !topLoaded) {
		preloadAroundItem(topLayout);
	} else if (preloadBottom && !bottomLoaded) {
		preloadAroundItem(bottomLayout);
	}
}

rpl::producer<GlobalMediaSlice> Provider::source(
		Type type,
		Data::MessagePosition aroundId,
		QString query,
		int limitBefore,
		int limitAfter) {
	Expects(_type == type);

	_totalListQuery = query;
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto session = &_controller->session();

		struct State : base::has_weak_ptr {
			State(not_null<Main::Session*> session) : session(session) {
			}
			~State() {
				session->api().request(requestId).cancel();
			}

			const not_null<Main::Session*> session;
			Fn<void()> pushAndLoadMore;
			mtpRequestId requestId = 0;
		};
		const auto state = lifetime.make_state<State>(session);
		const auto guard = base::make_weak(state);

		state->pushAndLoadMore = [=] {
			auto result = fillRequest(aroundId, limitBefore, limitAfter);

			// May destroy 'state' by calling source() with different args.
			consumer.put_next(std::move(result.slice));

			if (guard && !currentList()->loaded && result.notEnough) {
				state->requestId = requestMore(state->pushAndLoadMore);
			}
		};
		state->pushAndLoadMore();

		return lifetime;
	};
}

mtpRequestId Provider::requestMore(Fn<void()> loaded) {
	const auto done = [=](const Api::GlobalMediaResult &result) {
		const auto list = currentList();
		if (result.messageIds.empty()) {
			list->loaded = true;
			list->fullCount = list->list.size();
		} else {
			list->list.reserve(list->list.size() + result.messageIds.size());
			list->fullCount = result.fullCount;
			for (const auto &position : result.messageIds) {
				_seenIds.emplace(position.fullId);
				list->offsetPosition = position;
				list->list.push_back(position);
			}
		}
		if (!result.offsetRate) {
			list->loaded = true;
		} else {
			list->offsetRate = result.offsetRate;
		}
		loaded();
	};
	const auto list = currentList();
	return _controller->session().api().requestGlobalMedia(
		_type,
		_totalListQuery,
		list->offsetRate,
		list->offsetPosition,
		done);
}

Provider::FillResult Provider::fillRequest(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto list = currentList();
	const auto i = ranges::lower_bound(
		list->list,
		aroundId,
		std::greater<>());
	const auto hasAfter = int(i - begin(list->list));
	const auto hasBefore = int(end(list->list) - i);
	const auto takeAfter = std::min(limitAfter, hasAfter);
	const auto takeBefore = std::min(limitBefore, hasBefore);
	auto messages = std::vector<Data::MessagePosition>{
		i - takeAfter,
		i + takeBefore,
	};
	return FillResult{
		.slice = GlobalMediaSlice(
			GlobalMediaKey{ aroundId },
			std::move(messages),
			((!list->list.empty() || list->loaded)
				? list->fullCount
				: std::optional<int>()),
			hasAfter - takeAfter),
		.notEnough = (takeBefore < limitBefore),
	};
}

void Provider::refreshViewer() {
	_viewerLifetime.destroy();
	_controller->searchQueryValue(
	) | rpl::map([=](QString query) {
		return source(
			_type,
			sliceKey(_aroundId).aroundId,
			query,
			_idsLimit,
			_idsLimit);
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=](GlobalMediaSlice &&slice) {
		if (!slice.fullCount()) {
			// Don't display anything while full count is unknown.
			return;
		}
		_slice = std::move(slice);
		if (auto nearest = _slice.nearest(_aroundId)) {
			_aroundId = *nearest;
		}
		_refreshed.fire({});
	}, _viewerLifetime);
}

rpl::producer<> Provider::refreshed() {
	return _refreshed.events();
}

std::vector<Media::ListSection> Provider::fillSections(
		not_null<Overview::Layout::Delegate*> delegate) {
	markLayoutsStale();
	const auto guard = gsl::finally([&] { clearStaleLayouts(); });

	auto result = std::vector<Media::ListSection>();
	result.emplace_back(_type, sectionDelegate());
	auto &section = result.back();
	for (auto i = 0, count = int(_slice.size()); i != count; ++i) {
		auto position = _slice[i];
		if (auto layout = getLayout(position.fullId, delegate)) {
			section.addItem(layout);
		}
	}
	if (section.empty()) {
		result.pop_back();
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

Provider::List *Provider::currentList() {
	return &_totalLists[_totalListQuery];
}

rpl::producer<not_null<Media::BaseLayout*>> Provider::layoutRemoved() {
	return _layoutRemoved.events();
}

Media::BaseLayout *Provider::lookupLayout(
		const HistoryItem *item) {
	const auto i = _layouts.find(item ? item->fullId() : FullMsgId());
	return (i != _layouts.end()) ? i->second.item.get() : nullptr;
}

bool Provider::isMyItem(not_null<const HistoryItem*> item) {
	return _seenIds.contains(item->fullId());
}

bool Provider::isAfter(
		not_null<const HistoryItem*> a,
		not_null<const HistoryItem*> b) {
	return (a->fullId() < b->fullId());
}

void Provider::setSearchQuery(QString query) {
	Unexpected("Media::Provider::setSearchQuery.");
}

GlobalMediaKey Provider::sliceKey(Data::MessagePosition aroundId) const {
	return GlobalMediaKey{ aroundId };
}

void Provider::itemRemoved(not_null<const HistoryItem*> item) {
	const auto id = item->fullId();
	if (const auto i = _layouts.find(id); i != end(_layouts)) {
		_layoutRemoved.fire(i->second.item.get());
		_layouts.erase(i);
	}
}

Media::BaseLayout *Provider::getLayout(
		FullMsgId itemId,
		not_null<Overview::Layout::Delegate*> delegate) {
	auto it = _layouts.find(itemId);
	if (it == _layouts.end()) {
		if (auto layout = createLayout(itemId, delegate, _type)) {
			layout->initDimensions();
			it = _layouts.emplace(
				itemId,
				std::move(layout)).first;
		} else {
			return nullptr;
		}
	}
	it->second.stale = false;
	return it->second.item.get();
}

std::unique_ptr<Media::BaseLayout> Provider::createLayout(
		FullMsgId itemId,
		not_null<Overview::Layout::Delegate*> delegate,
		Type type) {
	const auto item = _controller->session().data().message(itemId);
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
		if (const auto media = item->media()) {
			return media->document();
		}
		return nullptr;
	};

	const auto &songSt = st::overviewFileLayout;
	using namespace Overview::Layout;
	const auto options = [&] {
		const auto media = item->media();
		return MediaOptions{ .spoiler = media && media->hasSpoiler() };
	};
	switch (type) {
	case Type::Photo:
		if (const auto photo = getPhoto()) {
			return std::make_unique<Photo>(
				delegate,
				item,
				photo,
				options());
		}
		return nullptr;
	case Type::GIF:
		if (const auto file = getFile()) {
			return std::make_unique<Gif>(delegate, item, file);
		}
		return nullptr;
	case Type::Video:
		if (const auto file = getFile()) {
			return std::make_unique<Video>(delegate, item, file, options());
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

Media::ListItemSelectionData Provider::computeSelectionData(
		not_null<const HistoryItem*> item,
		TextSelection selection) {
	auto result = Media::ListItemSelectionData(selection);
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
		Media::ListSelectedMap &selected,
		not_null<const HistoryItem*> fromItem,
		bool skipFrom,
		not_null<const HistoryItem*> tillItem,
		bool skipTill) {
#if 0 // not used for now
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
#endif // todo global media
}

int64 Provider::scrollTopStatePosition(not_null<HistoryItem*> item) {
	return item->position().date;
}

HistoryItem *Provider::scrollTopStateItem(Media::ListScrollTopState state) {
	const auto maybe = Data::MessagePosition{
		.date = TimeId(state.position),
	};
	if (state.item && _slice.indexOf(state.item->position())) {
		return state.item;
	} else if (const auto position = _slice.nearest(maybe)) {
		const auto id = position->fullId;
		if (const auto item = _controller->session().data().message(id)) {
			return item;
		}
	}
	return state.item;
}

void Provider::saveState(
		not_null<Media::Memento*> memento,
		Media::ListScrollTopState scrollState) {
	if (_aroundId != Data::MaxMessagePosition && scrollState.item) {
		memento->setAroundId(_aroundId.fullId);
		memento->setIdsLimit(_idsLimit);
		memento->setScrollTopItem(scrollState.item->globalId());
		memento->setScrollTopItemPosition(scrollState.position);
		memento->setScrollTopShift(scrollState.shift);
	}
}

void Provider::restoreState(
		not_null<Media::Memento*> memento,
		Fn<void(Media::ListScrollTopState)> restoreScrollState) {
	if (const auto limit = memento->idsLimit()) {
		_idsLimit = limit;
		_aroundId = { memento->aroundId() };
		restoreScrollState({
			.position = memento->scrollTopItemPosition(),
			.item = MessageByGlobalId(memento->scrollTopItem()),
			.shift = memento->scrollTopShift(),
		});
		refreshViewer();
	}
}

} // namespace Info::GlobalMedia
