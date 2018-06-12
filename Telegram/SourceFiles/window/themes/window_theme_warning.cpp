/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_theme_warning.h"

#include "styles/style_boxes.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "window/themes/window_theme.h"
#include "lang/lang_keys.h"

namespace Window {
namespace Theme {
namespace {

constexpr int kWaitBeforeRevertMs = 15999;

} // namespace

WarningWidget::WarningWidget(QWidget *parent) : TWidget(parent)
, _secondsLeft(kWaitBeforeRevertMs / 1000)
, _keepChanges(this, langFactory(lng_theme_keep_changes), st::defaultBoxButton)
, _revert(this, langFactory(lng_theme_revert), st::defaultBoxButton) {
	_keepChanges->setClickedCallback([] { Window::Theme::KeepApplied(); });
	_revert->setClickedCallback([] { Window::Theme::Revert(); });
	_timer.setTimeoutHandler([this] { handleTimer(); });
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
		if (!_animation.animating(getms())) {
			if (isHidden()) {
				return;
			}
		}
		p.setOpacity(_animation.current(_hiding ? 0. : 1.));
		p.drawPixmap(_outer.topLeft(), _cache);
		if (!_animation.animating()) {
			_cache = QPixmap();
			showChildren();
			_started = getms(true);
			_timer.start(100);
		}
		return;
	}

	Ui::Shadow::paint(p, _inner, width(), st::boxRoundShadow);
	App::roundRect(p, _inner, st::boxBg, BoxCorners);

	p.setFont(st::boxTitleFont);
	p.setPen(st::boxTitleFg);
	p.drawTextLeft(_inner.x() + st::boxTitlePosition.x(), _inner.y() + st::boxTitlePosition.y(), width(), lang(lng_theme_sure_keep));

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
	auto left = _inner.x() + _inner.width() - st::boxButtonPadding.right() - _keepChanges->width();
	_keepChanges->moveToLeft(left, _inner.y() + _inner.height() - st::boxButtonPadding.bottom() - _keepChanges->height());
	_revert->moveToLeft(left - st::boxButtonPadding.left() - _revert->width(), _keepChanges->y());
}

void WarningWidget::refreshLang() {
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

void WarningWidget::handleTimer() {
	auto msPassed = getms(true) - _started;
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
		_timer.start(100);
	}
}

void WarningWidget::updateText() {
	_text = lng_theme_reverting(lt_count, _secondsLeft);
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
	_timer.stop();
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
