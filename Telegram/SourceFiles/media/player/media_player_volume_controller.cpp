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
#include "media/player/media_player_volume_controller.h"

#include "media/media_audio.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/continuous_sliders.h"
#include "styles/style_media_player.h"
#include "styles/style_widgets.h"
#include "mainwindow.h"

namespace Media {
namespace Player {

VolumeController::VolumeController(QWidget *parent) : TWidget(parent)
, _slider(this, st::mediaPlayerPanelPlayback) {
	_slider->setMoveByWheel(true);
	_slider->setChangeProgressCallback([this](float64 volume) {
		applyVolumeChange(volume);
	});
	_slider->setChangeFinishedCallback([this](float64 volume) {
		if (volume > 0) {
			Global::SetRememberedSongVolume(volume);
		}
		applyVolumeChange(volume);
	});
	subscribe(Global::RefSongVolumeChanged(), [this] {
		if (!_slider->isChanging()) {
			_slider->setValue(Global::SongVolume());
		}
	});
	setVolume(Global::SongVolume());

	resize(st::mediaPlayerPanelVolumeWidth, 2 * st::mediaPlayerPanelPlaybackPadding + st::mediaPlayerPanelPlayback.width);
}

void VolumeController::setIsVertical(bool vertical) {
	using Direction = Ui::MediaSlider::Direction;
	_slider->setDirection(vertical ? Direction::Vertical : Direction::Horizontal);
	_slider->setAlwaysDisplayMarker(vertical);
}

void VolumeController::resizeEvent(QResizeEvent *e) {
	_slider->setGeometry(rect());
}

void VolumeController::setVolume(float64 volume) {
	_slider->setValue(volume);
	if (volume > 0) {
		Global::SetRememberedSongVolume(volume);
	}
	applyVolumeChange(volume);
}

void VolumeController::applyVolumeChange(float64 volume) {
	if (volume != Global::SongVolume()) {
		Global::SetSongVolume(volume);
		mixer()->setSongVolume(Global::SongVolume());
		Global::RefSongVolumeChanged().notify();
	}
}

VolumeWidget::VolumeWidget(QWidget *parent) : TWidget(parent)
, _controller(this) {
	hide();
	_controller->setIsVertical(true);

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(onHideStart()));

	_showTimer.setSingleShot(true);
	connect(&_showTimer, SIGNAL(timeout()), this, SLOT(onShowStart()));

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWindowActiveChanged()));
	}

	hide();
	auto margin = getMargin();
	resize(margin.left() + st::mediaPlayerVolumeSize.width() + margin.right(), margin.top() + st::mediaPlayerVolumeSize.height() + margin.bottom());
}

QMargins VolumeWidget::getMargin() const {
	return QMargins(st::mediaPlayerVolumeMargin, st::mediaPlayerPlayback.fullWidth, st::mediaPlayerVolumeMargin, st::mediaPlayerVolumeMargin);
}

bool VolumeWidget::overlaps(const QRect &globalRect) {
	if (isHidden() || _a_appearance.animating()) return false;

	return rect().marginsRemoved(getMargin()).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
}

void VolumeWidget::onWindowActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(nullptr);
	}
}

void VolumeWidget::resizeEvent(QResizeEvent *e) {
	auto inner = rect().marginsRemoved(getMargin());
	_controller->setGeometry(inner.x(), inner.y() - st::lineWidth, inner.width(), inner.height() + st::lineWidth - ((st::mediaPlayerVolumeSize.width() - st::mediaPlayerPanelPlayback.width) / 2));
}

void VolumeWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_cache.isNull()) {
		bool animating = _a_appearance.animating(getms());
		if (animating) {
			p.setOpacity(_a_appearance.current(_hiding));
		} else if (_hiding || isHidden()) {
			hidingFinished();
			return;
		}
		p.drawPixmap(0, 0, _cache);
		if (!animating) {
			showChildren();
			_cache = QPixmap();
		}
		return;
	}

	// draw shadow
	auto shadowedRect = rect().marginsRemoved(getMargin());
	auto shadowedSides = RectPart::Left | RectPart::Right | RectPart::Bottom;
	Ui::Shadow::paint(p, shadowedRect, width(), st::defaultRoundShadow, shadowedSides);
	auto parts = RectPart::NoTopBottom | RectPart::FullBottom;
	App::roundRect(p, QRect(shadowedRect.x(), -st::buttonRadius, shadowedRect.width(), shadowedRect.y() + shadowedRect.height() + st::buttonRadius), st::menuBg, MenuCorners, nullptr, parts);
}

void VolumeWidget::enterEventHook(QEvent *e) {
	_hideTimer.stop();
	if (_a_appearance.animating(getms())) {
		onShowStart();
	} else {
		_showTimer.start(0);
	}
	return TWidget::enterEventHook(e);
}

void VolumeWidget::leaveEventHook(QEvent *e) {
	_showTimer.stop();
	if (_a_appearance.animating(getms())) {
		onHideStart();
	} else {
		_hideTimer.start(300);
	}
	return TWidget::leaveEventHook(e);
}

void VolumeWidget::otherEnter() {
	_hideTimer.stop();
	if (_a_appearance.animating(getms())) {
		onShowStart();
	} else {
		_showTimer.start(0);
	}
}

void VolumeWidget::otherLeave() {
	_showTimer.stop();
	if (_a_appearance.animating(getms())) {
		onHideStart();
	} else {
		_hideTimer.start(0);
	}
}

void VolumeWidget::onShowStart() {
	if (isHidden()) {
		show();
	} else if (!_hiding) {
		return;
	}
	_hiding = false;
	startAnimation();
}

void VolumeWidget::onHideStart() {
	if (_hiding) return;

	_hiding = true;
	startAnimation();
}

void VolumeWidget::startAnimation() {
	auto from = _hiding ? 1. : 0.;
	auto to = _hiding ? 0. : 1.;
	if (_cache.isNull()) {
		showChildren();
		_cache = Ui::GrabWidget(this);
	}
	hideChildren();
	_a_appearance.start([this] { appearanceCallback(); }, from, to, st::defaultInnerDropdown.duration);
}

void VolumeWidget::appearanceCallback() {
	if (!_a_appearance.animating() && _hiding) {
		_hiding = false;
		hidingFinished();
	} else {
		update();
	}
}

void VolumeWidget::hidingFinished() {
	hide();
	_cache = QPixmap();
}

bool VolumeWidget::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		otherEnter();
	} else if (e->type() == QEvent::Leave) {
		otherLeave();
	}
	return false;
}

} // namespace Player
} // namespace Media
