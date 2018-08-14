/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_content_widget.h"

#include <rpl/never.h>
#include <rpl/combine.h>
#include <rpl/range.h>
#include "window/window_controller.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/input_fields.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/search_field_controller.h"
#include "lang/lang_keys.h"
#include "info/profile/info_profile_widget.h"
#include "info/media/info_media_widget.h"
#include "info/common_groups/info_common_groups_widget.h"
#include "info/info_layer_widget.h"
#include "info/info_section_widget.h"
#include "info/info_controller.h"
#include "boxes/peer_list_box.h"
#include "styles/style_info.h"
#include "styles/style_profile.h"

namespace Info {

ContentWidget::ContentWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _scroll(this, st::infoScroll) {
	using namespace rpl::mappers;

	setAttribute(Qt::WA_OpaquePaintEvent);
	_controller->wrapValue(
	) | rpl::start_with_next([this](Wrap value) {
		if (value != Wrap::Layer) {
			applyAdditionalScroll(0);
		}
		_bg = (value == Wrap::Layer)
			? st::boxBg
			: st::profileBg;
		update();
	}, lifetime());
	if (_controller->section().type() != Section::Type::Profile) {
		rpl::combine(
			_controller->wrapValue(),
			_controller->searchEnabledByContent(),
			(_1 == Wrap::Layer) && _2
		) | rpl::distinct_until_changed(
		) | rpl::start_with_next([this](bool shown) {
			refreshSearchField(shown);
		}, lifetime());
	}
	_scrollTopSkip.changes(
	) | rpl::start_with_next([this] {
		updateControlsGeometry();
	}, lifetime());
}

void ContentWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void ContentWidget::updateControlsGeometry() {
	if (!_innerWrap) {
		return;
	}
	auto newScrollTop = _scroll->scrollTop() + _topDelta;
	auto scrollGeometry = rect().marginsRemoved(
		QMargins(0, _scrollTopSkip.current(), 0, 0));
	if (_scroll->geometry() != scrollGeometry) {
		_scroll->setGeometry(scrollGeometry);
		_innerWrap->resizeToWidth(_scroll->width());
	}

	if (!_scroll->isHidden()) {
		if (_topDelta) {
			_scroll->scrollToY(newScrollTop);
		}
		auto scrollTop = _scroll->scrollTop();
		_innerWrap->setVisibleTopBottom(
			scrollTop,
			scrollTop + _scroll->height());
	}
}

std::unique_ptr<ContentMemento> ContentWidget::createMemento() {
	auto result = doCreateMemento();
	_controller->saveSearchState(result.get());
	return result;
}

void ContentWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), _bg);
}

void ContentWidget::setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta) {
	_topDelta = topDelta;
	auto willBeResized = (size() != newGeometry.size());
	if (geometry() != newGeometry) {
		setGeometry(newGeometry);
	}
	if (!willBeResized) {
		QResizeEvent fake(size(), size());
		QApplication::sendEvent(this, &fake);
	}
	_topDelta = 0;
}

Ui::RpWidget *ContentWidget::doSetInnerWidget(
		object_ptr<RpWidget> inner) {
	using namespace rpl::mappers;

	_innerWrap = _scroll->setOwnedWidget(
		object_ptr<Ui::PaddingWrap<Ui::RpWidget>>(
			this,
			std::move(inner),
			_innerWrap ? _innerWrap->padding() : style::margins()));
	_innerWrap->move(0, 0);

	// MSVC BUG + REGRESSION rpl::mappers::tuple :(
	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		_innerWrap->entity()->desiredHeightValue()
	) | rpl::start_with_next([this](
			int top,
			int height,
			int desired) {
		const auto bottom = top + height;
		_innerDesiredHeight = desired;
		_innerWrap->setVisibleTopBottom(top, bottom);
		_scrollTillBottomChanges.fire_copy(std::max(desired - bottom, 0));
	}, _innerWrap->lifetime());

	return _innerWrap->entity();
}

int ContentWidget::scrollTillBottom(int forHeight) const {
	auto scrollHeight = forHeight - _scrollTopSkip.current();
	auto scrollBottom = _scroll->scrollTop() + scrollHeight;
	auto desired = _innerDesiredHeight;
	return std::max(desired - scrollBottom, 0);
}

rpl::producer<int> ContentWidget::scrollTillBottomChanges() const {
	return _scrollTillBottomChanges.events();
}

void ContentWidget::setScrollTopSkip(int scrollTopSkip) {
	_scrollTopSkip = scrollTopSkip;
}

rpl::producer<Section> ContentWidget::sectionRequest() const {
	return rpl::never<Section>();
}

rpl::producer<int> ContentWidget::scrollHeightValue() const {
	return _scroll->heightValue();
}

void ContentWidget::applyAdditionalScroll(int additionalScroll) {
	if (_innerWrap) {
		_innerWrap->setPadding({ 0, 0, 0, additionalScroll });
	}
}

rpl::producer<int> ContentWidget::desiredHeightValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_innerWrap->entity()->desiredHeightValue(),
		_scrollTopSkip.value()
	) | rpl::map(_1 + _2);
}

rpl::producer<bool> ContentWidget::desiredShadowVisibility() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_scroll->scrollTopValue(),
		_scrollTopSkip.value()
	) | rpl::map((_1 > 0) || (_2 > 0));
}

bool ContentWidget::hasTopBarShadow() const {
	return (_scroll->scrollTop() > 0);
}

void ContentWidget::setInnerFocus() {
	if (_searchField) {
		_searchField->setFocus();
	} else {
		_innerWrap->entity()->setFocus();
	}
}

int ContentWidget::scrollTopSave() const {
	return _scroll->scrollTop();
}

void ContentWidget::scrollTopRestore(int scrollTop) {
	_scroll->scrollToY(scrollTop);
}

void ContentWidget::scrollTo(const Ui::ScrollToRequest &request) {
	_scroll->scrollTo(request);
}

bool ContentWidget::wheelEventFromFloatPlayer(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect ContentWidget::rectForFloatPlayer() const {
	return mapToGlobal(_scroll->geometry());
}

rpl::producer<SelectedItems> ContentWidget::selectedListValue() const {
	return rpl::single(SelectedItems(Storage::SharedMediaType::Photo));
}

void ContentWidget::refreshSearchField(bool shown) {
	auto search = _controller->searchFieldController();
	if (search && shown) {
		auto rowView = search->createRowView(
			this,
			st::infoLayerMediaSearch);
		_searchWrap = std::move(rowView.wrap);
		_searchField = rowView.field;

		const auto view = _searchWrap.get();
		widthValue(
		) | rpl::start_with_next([=](int newWidth) {
			view->resizeToWidth(newWidth);
			view->moveToLeft(0, 0);
		}, view->lifetime());
		view->show();
		_searchField->setFocus();
		setScrollTopSkip(view->heightNoMargins() - st::lineWidth);
	} else {
		if (Ui::InFocusChain(this)) {
			setFocus();
		}
		_searchWrap = nullptr;
		setScrollTopSkip(0);
	}
}

} // namespace Info
