/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_volume_controller.h"

#include "media/audio/media_audio.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "base/object_ptr.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "styles/style_media_player.h"
#include "styles/style_widgets.h"

namespace Media {
namespace Player {

VolumeController::VolumeController(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _slider(this, st::mediaPlayerPanelPlayback) {
	_slider->setMoveByWheel(true);
	_slider->setChangeProgressCallback([=](float64 volume) {
		applyVolumeChange(volume);
	});
	_slider->setChangeFinishedCallback([=](float64 volume) {
		if (volume > 0) {
			Core::App().settings().setRememberedSongVolume(volume);
		}
		applyVolumeChange(volume);
		Core::App().saveSettingsDelayed();
	});
	Core::App().settings().songVolumeChanges(
	) | rpl::start_with_next([=](float64 volume) {
		if (!_slider->isChanging()) {
			_slider->setValue(volume);
		}
	}, lifetime());
	setVolume(Core::App().settings().songVolume());

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
		Core::App().settings().setRememberedSongVolume(volume);
	}
	applyVolumeChange(volume);
}

void VolumeController::applyVolumeChange(float64 volume) {
	if (volume != Core::App().settings().songVolume()) {
		Core::App().settings().setSongVolume(volume);
		mixer()->setSongVolume(Core::App().settings().songVolume());
	}
}

VolumeWidget::VolumeWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(this, controller) {
	hide();
	_controller->setIsVertical(true);

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(onHideStart()));

	_showTimer.setSingleShot(true);
	connect(&_showTimer, SIGNAL(timeout()), this, SLOT(onShowStart()));

	macWindowDeactivateEvents(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=] {
		leaveEvent(nullptr);
	}, lifetime());

	hide();
	auto margin = getMargin();
	resize(margin.left() + st::mediaPlayerVolumeSize.width() + margin.right(), margin.top() + st::mediaPlayerVolumeSize.height() + margin.bottom());
}

QMargins VolumeWidget::getMargin() const {
	const auto top = st::mediaPlayerHeight
		+ st::lineWidth
		- st::mediaPlayerPlayTop
		- st::mediaPlayerVolumeToggle.height;
	return QMargins(st::mediaPlayerVolumeMargin, top, st::mediaPlayerVolumeMargin, st::mediaPlayerVolumeMargin);
}

bool VolumeWidget::overlaps(const QRect &globalRect) {
	if (isHidden() || _a_appearance.animating()) return false;

	return rect().marginsRemoved(getMargin()).contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
}

void VolumeWidget::resizeEvent(QResizeEvent *e) {
	auto inner = rect().marginsRemoved(getMargin());
	_controller->setGeometry(inner.x(), inner.y() - st::lineWidth, inner.width(), inner.height() + st::lineWidth - ((st::mediaPlayerVolumeSize.width() - st::mediaPlayerPanelPlayback.width) / 2));
}

void VolumeWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_cache.isNull()) {
		bool animating = _a_appearance.animating();
		if (animating) {
			p.setOpacity(_a_appearance.value(_hiding ? 0. : 1.));
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
	Ui::FillRoundRect(p, QRect(shadowedRect.x(), -st::roundRadiusSmall, shadowedRect.width(), shadowedRect.y() + shadowedRect.height() + st::roundRadiusSmall), st::menuBg, Ui::MenuCorners, nullptr, parts);
}

void VolumeWidget::enterEventHook(QEvent *e) {
	_hideTimer.stop();
	if (_a_appearance.animating()) {
		onShowStart();
	} else {
		_showTimer.start(0);
	}
	return RpWidget::enterEventHook(e);
}

void VolumeWidget::leaveEventHook(QEvent *e) {
	_showTimer.stop();
	if (_a_appearance.animating()) {
		onHideStart();
	} else {
		_hideTimer.start(300);
	}
	return RpWidget::leaveEventHook(e);
}

void VolumeWidget::otherEnter() {
	_hideTimer.stop();
	if (_a_appearance.animating()) {
		onShowStart();
	} else {
		_showTimer.start(0);
	}
}

void VolumeWidget::otherLeave() {
	_showTimer.stop();
	if (_a_appearance.animating()) {
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
	if (_cache.isNull()) {
		showChildren();
		_cache = Ui::GrabWidget(this);
	}
	hideChildren();
	_a_appearance.start(
		[=] { appearanceCallback(); },
		_hiding ? 1. : 0.,
		_hiding ? 0. : 1.,
		st::defaultInnerDropdown.duration);
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
