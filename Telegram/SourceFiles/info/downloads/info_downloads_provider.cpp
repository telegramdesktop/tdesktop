/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/downloads/info_downloads_provider.h"

#include "info/media/info_media_widget.h"
#include "info/media/info_media_list_section.h"
#include "info/info_controller.h"
#include "data/data_download_manager.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "history/history_item.h"
#include "history/history.h"
#include "core/application.h"
#include "storage/storage_shared_media.h"
#include "layout/layout_selection.h"

namespace Info::Downloads {
namespace {

using namespace Media;

} // namespace

Provider::Provider(not_null<AbstractController*> controller)
: _controller(controller) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &layout : _layouts) {
			layout.second.item->invalidateCache();
		}
	}, _lifetime);
}

Type Provider::type() {
	return Type::File;
}

bool Provider::hasSelectRestriction() {
	return false;
}

rpl::producer<bool> Provider::hasSelectRestrictionChanges() {
	return rpl::never<bool>();
}

bool Provider::sectionHasFloatingHeader() {
	return false;
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
	return true;
}

std::optional<int> Provider::fullCount() {
	return _fullCount;
}

void Provider::restart() {
}

void Provider::checkPreload(
	QSize viewport,
	not_null<BaseLayout*> topLayout,
	not_null<BaseLayout*> bottomLayout,
	bool preloadTop,
	bool preloadBottom) {
}

void Provider::refreshViewer() {
	if (_fullCount) {
		return;
	}
	auto &manager = Core::App().downloadManager();
	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		manager.loadingListChanges() | rpl::to_empty
	) | rpl::start_with_next([=, &manager] {
		auto copy = _downloading;
		auto added = false;
		for (const auto &id : manager.loadingList()) {
			if (!id.done) {
				const auto item = id.object.item;
				if (!copy.remove(item)) {
					added = true;
					_downloading.emplace(item);
					_elements.push_back(Element{ item, id.started });
					trackItemSession(item);
				}
			}
		}
		for (const auto &item : copy) {
			_downloading.remove(item);
			// #TODO downloads check if downloaded
			_elements.erase(
				ranges::remove(_elements, item, &Element::item),
				end(_elements));
		}
		_fullCount = _elements.size();
		if (added) {
			ranges::sort(_elements, ranges::less(), &Element::started);
			_refreshed.fire({});
		} else if (!copy.empty()) {
			_refreshed.fire({});
		}
	}, _lifetime);
}

void Provider::trackItemSession(not_null<const HistoryItem*> item) {
	const auto session = &item->history()->session();
	if (_trackedSessions.contains(session)) {
		return;
	}
	auto &lifetime = _trackedSessions.emplace(session).first->second;

	session->data().itemRemoved(
	) | rpl::start_with_next([this](auto item) {
		itemRemoved(item);
	}, lifetime);

	session->account().sessionChanges(
	) | rpl::take(1) | rpl::start_with_next([=] {
		_trackedSessions.remove(session);
	}, lifetime);
}

rpl::producer<> Provider::refreshed() {
	return _refreshed.events();
}

std::vector<ListSection> Provider::fillSections(
		not_null<Overview::Layout::Delegate*> delegate) {
	markLayoutsStale();
	const auto guard = gsl::finally([&] { clearStaleLayouts(); });

	if (_elements.empty()) {
		return {};
	}

	auto result = std::vector<ListSection>(
		1,
		ListSection(Type::File, sectionDelegate()));
	auto &section = result.back();
	for (const auto &element : _elements) {
		if (auto layout = getLayout(element, delegate)) {
			section.addItem(layout);
		}
	}
	section.finishSection();
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

BaseLayout *Provider::lookupLayout(const HistoryItem *item) {
	return nullptr;
}

bool Provider::isMyItem(not_null<const HistoryItem*> item) {
	return _downloading.contains(item) || _downloaded.contains(item);
}

bool Provider::isAfter(
		not_null<const HistoryItem*> a,
		not_null<const HistoryItem*> b) {
	if (a != b) {
		for (const auto &element : _elements) {
			if (element.item == a) {
				return false;
			} else if (element.item == b) {
				return true;
			}
		}
	}
	return false;
}

void Provider::itemRemoved(not_null<const HistoryItem*> item) {
	if (const auto i = _layouts.find(item); i != end(_layouts)) {
		_layoutRemoved.fire(i->second.item.get());
		_layouts.erase(i);
	}
}

BaseLayout *Provider::getLayout(
		Element element,
		not_null<Overview::Layout::Delegate*> delegate) {
	auto it = _layouts.find(element.item);
	if (it == _layouts.end()) {
		if (auto layout = createLayout(element, delegate)) {
			layout->initDimensions();
			it = _layouts.emplace(element.item, std::move(layout)).first;
		} else {
			return nullptr;
		}
	}
	it->second.stale = false;
	return it->second.item.get();
}

std::unique_ptr<BaseLayout> Provider::createLayout(
		Element element,
		not_null<Overview::Layout::Delegate*> delegate) {
	const auto getFile = [&]() -> DocumentData* {
		if (auto media = element.item->media()) {
			return media->document();
		}
		return nullptr;
	};

	auto &songSt = st::overviewFileLayout;
	if (const auto file = getFile()) {
		return std::make_unique<Overview::Layout::Document>(
			delegate,
			element.item,
			file,
			songSt);
	}
	return nullptr;
}

void Provider::applyDragSelection(
		ListSelectedMap &selected,
		not_null<const HistoryItem*> fromItem,
		bool skipFrom,
		not_null<const HistoryItem*> tillItem,
		bool skipTill) {
	// #TODO downloads
}

void Provider::saveState(
		not_null<Media::Memento*> memento,
		ListScrollTopState scrollState) {

}

void Provider::restoreState(
		not_null<Media::Memento*> memento,
		Fn<void(ListScrollTopState)> restoreScrollState) {

}

} // namespace Info::Downloads
