/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_theme_warning.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "window/themes/window_theme.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Window {
namespace Theme {
namespace {

constexpr int kWaitBeforeRevertMs = 15999;

} // namespace

WarningWidget::WarningWidget(QWidget *parent)
: TWidget(parent)
, _timer([=] { handleTimer(); })
, _secondsLeft(kWaitBeforeRevertMs / 1000)
, _keepChanges(this, tr::lng_theme_keep_changes(), st::defaultBoxButton)
, _revert(this, tr::lng_theme_revert(), st::defaultBoxButton) {
	_keepChanges->setClickedCallback([] { KeepApplied(); });
	_revert->setClickedCallback([] { Revert(); });
	updateText();
}

void WarningWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		Window::Theme::Revert();
	}
}

void WarningWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_cache.isNull()) {
		if (!_animation.animating()) {
			if (isHidden()) {
				return;
			}
		}
		p.setOpacity(_animation.value(_hiding ? 0. : 1.));
		p.drawPixmap(_outer.topLeft(), _cache);
		if (!_animation.animating()) {
			_cache = QPixmap();
			showChildren();
			_started = crl::now();
			_timer.callOnce(100);
		}
		return;
	}

	Ui::Shadow::paint(p, _inner, width(), st::boxRoundShadow);
	Ui::FillRoundRect(p, _inner, st::boxBg, Ui::BoxCorners);

	p.setFont(st::boxTitleFont);
	p.setPen(st::boxTitleFg);
	p.drawTextLeft(_inner.x() + st::boxTitlePosition.x(), _inner.y() + st::boxTitlePosition.y(), width(), tr::lng_theme_sure_keep(tr::now));

	p.setFont(st::boxTextFont);
	p.setPen(st::boxTextFg);
	p.drawTextLeft(_inner.x() + st::boxTitlePosition.x(), _inner.y() + st::themeWarningTextTop, width(), _text);
}

void WarningWidget::resizeEvent(QResizeEvent *e) {
	_inner = QRect((width() - st::themeWarningWidth) / 2, (height() - st::themeWarningHeight) / 2, st::themeWarningWidth, st::themeWarningHeight);
	_outer = _inner.marginsAdded(st::boxRoundShadow.extend);
	updateControlsGeometry();
	update();
}

void WarningWidget::updateControlsGeometry() {
	auto left = _inner.x() + _inner.width() - st::defaultBox.buttonPadding.right() - _keepChanges->width();
	_keepChanges->moveToLeft(left, _inner.y() + _inner.height() - st::defaultBox.buttonPadding.bottom() - _keepChanges->height());
	_revert->moveToLeft(left - st::defaultBox.buttonPadding.left() - _revert->width(), _keepChanges->y());
}

void WarningWidget::refreshLang() {
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

void WarningWidget::handleTimer() {
	auto msPassed = crl::now() - _started;
	setSecondsLeft((kWaitBeforeRevertMs - msPassed) / 1000);
}

void WarningWidget::setSecondsLeft(int secondsLeft) {
	if (secondsLeft <= 0) {
		Window::Theme::Revert();
	} else {
		if (_secondsLeft != secondsLeft) {
			_secondsLeft = secondsLeft;
			updateText();
			update();
		}
		_timer.callOnce(100);
	}
}

void WarningWidget::updateText() {
	_text = tr::lng_theme_reverting(tr::now, lt_count, _secondsLeft);
}

void WarningWidget::showAnimated() {
	startAnimation(false);
	show();
	setFocus();
}

void WarningWidget::hideAnimated() {
	startAnimation(true);
}

void WarningWidget::startAnimation(bool hiding) {
	_timer.cancel();
	_hiding = hiding;
	if (_cache.isNull()) {
		showChildren();
		Ui::SendPendingMoveResizeEvents(this);
		_cache = Ui::GrabWidget(this, _outer);
	}
	hideChildren();
	_animation.start([this] {
		update();
		if (_hiding) {
			hide();
			if (_hiddenCallback) {
				_hiddenCallback();
			}
		}
	}, _hiding ? 1. : 0., _hiding ? 0. : 1., st::boxDuration);
}

} // namespace Theme
} // namespace Window
