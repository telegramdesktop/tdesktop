/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_layer.h"

#include <rpl/mappers.h>
#include "settings/settings_inner_widget.h"
#include "settings/settings_fixed_bar.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "storage/localstorage.h"
#include "boxes/confirm_box.h"
#include "application.h"
#include "core/file_utilities.h"
#include "window/themes/window_theme.h"

namespace Settings {

Layer::Layer()
: _scroll(this, st::settingsScroll)
, _fixedBar(this)
, _fixedBarClose(this, st::settingsFixedBarClose)
, _fixedBarShadow(this) {
	_fixedBar->moveToLeft(0, st::boxRadius);
	_fixedBarClose->moveToRight(0, 0);
	_fixedBarShadow->entity()->resize(width(), st::lineWidth);
	_fixedBarShadow->moveToLeft(0, _fixedBar->y() + _fixedBar->height());
	_fixedBarShadow->hide(anim::type::instant);
	_scroll->moveToLeft(0, st::settingsFixedBarHeight);

	using namespace rpl::mappers;
	_fixedBarShadow->toggleOn(_scroll->scrollTopValue()
		| rpl::map(_1 > 0));
}

void Layer::setCloseClickHandler(Fn<void()> callback) {
	_fixedBarClose->setClickedCallback(std::move(callback));
}

void Layer::resizeToWidth(int newWidth, int newContentLeft) {
	// Widget height depends on InnerWidget height, so we
	// resize it here, not in the resizeEvent() handler.
	_inner->resizeToWidth(newWidth, newContentLeft);

	resizeUsingInnerHeight(newWidth, _inner->height());
}

void Layer::doSetInnerWidget(object_ptr<LayerInner> widget) {
	_inner = _scroll->setOwnedWidget(std::move(widget));
	_inner->heightValue(
	) | rpl::start_with_next([this](int innerHeight) {
		resizeUsingInnerHeight(width(), innerHeight);
	}, lifetime());
}

void Layer::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	if (_roundedCorners) {
		auto paintTopRounded = clip.intersects(QRect(0, 0, width(), st::boxRadius));
		auto paintBottomRounded = clip.intersects(QRect(0, height() - st::boxRadius, width(), st::boxRadius));
		if (paintTopRounded || paintBottomRounded) {
			auto parts = RectPart::None | 0;
			if (paintTopRounded) parts |= RectPart::FullTop;
			if (paintBottomRounded) parts |= RectPart::FullBottom;
			App::roundRect(p, rect(), st::boxBg, BoxCorners, nullptr, parts);
		}
		auto other = clip.intersected(QRect(0, st::boxRadius, width(), height() - 2 * st::boxRadius));
		if (!other.isEmpty()) {
			p.fillRect(other, st::boxBg);
		}
	} else {
		p.fillRect(e->rect(), st::boxBg);
	}
}

void Layer::resizeEvent(QResizeEvent *e) {
	LayerWidget::resizeEvent(e);
	if (!width() || !height()) {
		return;
	}

	_fixedBar->resizeToWidth(width());
	_fixedBar->moveToLeft(0, st::boxRadius);
	_fixedBar->update();
	_fixedBarClose->moveToRight(0, 0);
	auto shadowTop = _fixedBar->y() + _fixedBar->height();
	_fixedBarShadow->entity()->resize(width(), st::lineWidth);
	_fixedBarShadow->moveToLeft(0, shadowTop);

	auto scrollSize = QSize(width(), height() - shadowTop - (_roundedCorners ? st::boxRadius : 0));
	if (_scroll->size() != scrollSize) {
		_scroll->resize(scrollSize);
	}
	if (!_scroll->isHidden()) {
		auto scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	}
}

void Layer::setTitle(const QString &title) {
	_fixedBar->setText(title);
}

void Layer::scrollToY(int y) {
	_scroll->scrollToY(y);
}

} // namespace Settings
