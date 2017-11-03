/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "info/info_content_widget.h"

#include <rpl/never.h>
#include <rpl/combine.h>
#include <rpl/range.h>
#include "window/window_controller.h"
#include "ui/widgets/scroll_area.h"
#include "lang/lang_keys.h"
#include "info/profile/info_profile_widget.h"
#include "info/media/info_media_widget.h"
#include "info/info_common_groups_widget.h"
#include "info/info_layer_widget.h"
#include "info/info_section_widget.h"
#include "info/info_controller.h"
#include "styles/style_info.h"
#include "styles/style_profile.h"

namespace Info {

ContentWidget::ContentWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _scroll(this, st::infoScroll) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	_controller->wrapValue()
		| rpl::start_with_next([this](Wrap value) {
			_bg = (value == Wrap::Layer)
				? st::boxBg
				: st::profileBg;
			update();
		}, lifetime());
	_scrollTopSkip.changes()
		| rpl::start_with_next([this] {
			updateControlsGeometry();
		}, lifetime());
}

void ContentWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void ContentWidget::updateControlsGeometry() {
	auto newScrollTop = _scroll->scrollTop() + _topDelta;
	auto scrollGeometry = rect().marginsRemoved(
		QMargins(0, _scrollTopSkip.current(), 0, 0));
	if (_scroll->geometry() != scrollGeometry) {
		_scroll->setGeometry(scrollGeometry);
		_inner->resizeToWidth(_scroll->width());
	}

	if (!_scroll->isHidden()) {
		if (_topDelta) {
			_scroll->scrollToY(newScrollTop);
		}
		auto scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	}
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
		object_ptr<RpWidget> inner,
		int scrollTopSkip) {
	using namespace rpl::mappers;

	_inner = _scroll->setOwnedWidget(std::move(inner));
	_inner->move(0, 0);

	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		_inner->desiredHeightValue(),
		tuple($1, $1 + $2, $3))
		| rpl::start_with_next([inner = _inner](
				int top,
				int bottom,
				int desired) {
			inner->setVisibleTopBottom(top, bottom);
		}, _inner->lifetime());

	setScrollTopSkip(scrollTopSkip);

	return _inner;
}

void ContentWidget::setScrollTopSkip(int scrollTopSkip) {
	_scrollTopSkip = scrollTopSkip;
}

rpl::producer<Section> ContentWidget::sectionRequest() const {
	return rpl::never<Section>();
}

rpl::producer<int> ContentWidget::desiredHeightValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_inner->desiredHeightValue(),
		_scrollTopSkip.value())
		| rpl::map($1 + $2);
}

rpl::producer<bool> ContentWidget::desiredShadowVisibility() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_scroll->scrollTopValue(),
		_scrollTopSkip.value())
		| rpl::map(($1 > 0) || ($2 > 0));
}

bool ContentWidget::hasTopBarShadow() const {
	return (_scroll->scrollTop() > 0);
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

} // namespace Info
