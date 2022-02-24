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
#include "storage/storage_shared_media.h"
#include "layout/layout_selection.h"

namespace Info::Downloads {
namespace {

using namespace Media;

} // namespace

Provider::Provider(not_null<AbstractController*> controller)
: _controller(controller) {
	//_controller->session().data().itemRemoved(
	//) | rpl::start_with_next([this](auto item) {
	//	itemRemoved(item);
	//}, _lifetime);

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

bool Provider::isPossiblyMyItem(not_null<const HistoryItem*> item) {
	return true;
}

std::optional<int> Provider::fullCount() {
	return 0;
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

}

rpl::producer<> Provider::refreshed() {
	return _refreshed.events();
}

std::vector<ListSection> Provider::fillSections(
	not_null<Overview::Layout::Delegate*> delegate) {
	return {};
}

rpl::producer<not_null<BaseLayout*>> Provider::layoutRemoved() {
	return _layoutRemoved.events();
}

BaseLayout *Provider::lookupLayout(const HistoryItem *item) {
	return nullptr;
}

bool Provider::isMyItem(not_null<const HistoryItem*> item) {
	return false;
}

bool Provider::isAfter(
	not_null<const HistoryItem*> a,
	not_null<const HistoryItem*> b) {
	return a < b;
}

void Provider::applyDragSelection(
	ListSelectedMap &selected,
	not_null<const HistoryItem*> fromItem,
	bool skipFrom,
	not_null<const HistoryItem*> tillItem,
	bool skipTill) {

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
