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
		for (const auto id : manager.loadingList()) {
			if (!id->done) {
				const auto item = id->object.item;
				if (!copy.remove(item) && !_downloaded.contains(item)) {
					_downloading.emplace(item);
					_elements.push_back(Element{ item, id->started });
					trackItemSession(item);
					refreshPostponed(true);
				}
			}
		}
		for (const auto &item : copy) {
			Assert(!_downloaded.contains(item));
			remove(item);
		}
		if (!_fullCount.has_value()) {
			refreshPostponed(false);
		}
	}, _lifetime);

	for (const auto id : manager.loadedList()) {
		addPostponed(id);
	}

	manager.loadedAdded(
	) | rpl::start_with_next([=](not_null<const Data::DownloadedId*> entry) {
		addPostponed(entry);
	}, _lifetime);

	manager.loadedRemoved(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		if (!_downloading.contains(item)) {
			remove(item);
		} else {
			_downloaded.remove(item);
			_addPostponed.remove(item);
		}
	}, _lifetime);

	performAdd();
	performRefresh();
}

void Provider::addPostponed(not_null<const Data::DownloadedId*> entry) {
	Expects(entry->object != nullptr);

	const auto item = entry->object->item;
	trackItemSession(item);
	if (_addPostponed.emplace(item, entry->started).second
		&& _addPostponed.size() == 1) {
		Ui::PostponeCall(this, [=] {
			performAdd();
		});
	}
}

void Provider::performAdd() {
	if (_addPostponed.empty()) {
		return;
	}
	for (const auto &[item, started] : base::take(_addPostponed)) {
		_downloaded.emplace(item);
		if (!_downloading.remove(item)) {
			_elements.push_back(Element{ item, started });
		}
	}
	refreshPostponed(true);
}

void Provider::remove(not_null<const HistoryItem*> item) {
	_addPostponed.remove(item);
	_downloading.remove(item);
	_downloaded.remove(item);
	_elements.erase(
		ranges::remove(_elements, item, &Element::item),
		end(_elements));
	if (const auto i = _layouts.find(item); i != end(_layouts)) {
		_layoutRemoved.fire(i->second.item.get());
		_layouts.erase(i);
	}
	refreshPostponed(false);
}

void Provider::refreshPostponed(bool added) {
	if (added) {
		_postponedRefreshSort = true;
	}
	if (!_postponedRefresh) {
		_postponedRefresh = true;
		Ui::PostponeCall(this, [=] {
			performRefresh();
		});
	}
}

void Provider::performRefresh() {
	if (!_postponedRefresh) {
		return;
	}
	_postponedRefresh = false;
	_fullCount = _elements.size();
	if (base::take(_postponedRefreshSort)) {
		ranges::sort(_elements, ranges::greater(), &Element::started);
	}
	_refreshed.fire({});
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
	remove(item);
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

	using namespace Overview::Layout;
	auto &songSt = st::overviewFileLayout;
	if (const auto file = getFile()) {
		return std::make_unique<Document>(
			delegate,
			element.item,
			DocumentFields{
				.document = file,
				.dateOverride = Data::DateFromDownloadDate(element.started),
			},
			songSt);
	}
	return nullptr;
}

ListItemSelectionData Provider::computeSelectionData(
		not_null<const HistoryItem*> item,
		TextSelection selection) {
	auto result = ListItemSelectionData(selection);
	result.canDelete = true;
	result.canForward = item->allowsForward()
		&& (&item->history()->session() == &_controller->session());
	return result;
}

void Provider::applyDragSelection(
		ListSelectedMap &selected,
		not_null<const HistoryItem*> fromItem,
		bool skipFrom,
		not_null<const HistoryItem*> tillItem,
		bool skipTill) {
	// #TODO downloads selection
}

bool Provider::allowSaveFileAs(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) {
	return false;
}

std::optional<QString> Provider::deleteMenuPhrase() {
	return u"Delete from disk"_q;
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
