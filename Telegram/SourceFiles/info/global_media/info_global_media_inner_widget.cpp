/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/global_media/info_global_media_inner_widget.h"

#include "info/global_media/info_global_media_provider.h"
#include "info/global_media/info_global_media_widget.h"
#include "info/media/info_media_empty_widget.h"
#include "info/media/info_media_list_widget.h"
#include "info/info_controller.h"
#include "ui/widgets/labels.h"
#include "ui/search_field_controller.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"

namespace Info::GlobalMedia {

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _empty(this) {
	_empty->setType(type());
	_empty->heightValue(
	) | rpl::start_with_next(
		[this] { refreshHeight(); },
		_empty->lifetime());
	_list = setupList();
}

object_ptr<Media::ListWidget> InnerWidget::setupList() {
	auto result = object_ptr<Media::ListWidget>(this, _controller);

	// Setup list widget connections
	result->heightValue(
	) | rpl::start_with_next([this] {
		refreshHeight();
	}, result->lifetime());

	using namespace rpl::mappers;
	result->scrollToRequests(
	) | rpl::map([widget = result.data()](int to) {
		return Ui::ScrollToRequest{
			widget->y() + to,
			-1
		};
	}) | rpl::start_to_stream(
		_scrollToRequests,
		result->lifetime());

	_controller->searchQueryValue(
	) | rpl::start_with_next([this](const QString &query) {
		_empty->setSearchQuery(query);
	}, result->lifetime());

	return result;
}

Storage::SharedMediaType InnerWidget::type() const {
	return _controller->section().mediaType();
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

bool InnerWidget::showInternal(not_null<Memento*> memento) {
	if (memento->section().type() == Section::Type::GlobalMedia
		&& memento->section().mediaType() == type()) {
		restoreState(memento);
		return true;
	}
	return false;
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	_list->saveState(&memento->media());
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_list->restoreState(&memento->media());
}

rpl::producer<SelectedItems> InnerWidget::selectedListValue() const {
	return _selectedLists.events_starting_with(
		_list->selectedListValue()
	) | rpl::flatten_latest();
}

void InnerWidget::selectionAction(SelectionAction action) {
	_list->selectionAction(action);
}

InnerWidget::~InnerWidget() = default;

int InnerWidget::resizeGetHeight(int newWidth) {
	_inResize = true;
	auto guard = gsl::finally([this] { _inResize = false; });

	_list->resizeToWidth(newWidth);
	_empty->resizeToWidth(newWidth);
	return recountHeight();
}

void InnerWidget::refreshHeight() {
	if (_inResize) {
		return;
	}
	resize(width(), recountHeight());
}

int InnerWidget::recountHeight() {
	auto top = 0;
	auto listHeight = 0;
	if (_list) {
		_list->moveToLeft(0, top);
		listHeight = _list->heightNoMargins();
		top += listHeight;
	}
	if (listHeight > 0) {
		_empty->hide();
	} else {
		_empty->show();
		_empty->moveToLeft(0, top);
		top += _empty->heightNoMargins();
	}
	return top;
}

void InnerWidget::setScrollHeightValue(rpl::producer<int> value) {
	using namespace rpl::mappers;
	_empty->setFullHeight(rpl::combine(
		std::move(value),
		_listTops.events_starting_with(
			_list->topValue()
		) | rpl::flatten_latest(),
		_1 - _2));
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

} // namespace Info::GlobalMedia