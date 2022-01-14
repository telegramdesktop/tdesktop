/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_volume_controller.h"

#include "media/audio/media_audio.h"
#include "media/player/media_player_dropdown.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "styles/style_media_player.h"
#include "styles/style_widgets.h"

namespace Media::Player {

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

void VolumeController::outerWheelEvent(not_null<QWheelEvent*> e) {
	QGuiApplication::sendEvent(_slider.data(), e);
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
		mixer()->setSongVolume(volume);
		Core::App().settings().setSongVolume(volume);
	}
}

void PrepareVolumeDropdown(
		not_null<Dropdown*> dropdown,
		not_null<Window::SessionController*> controller,
		rpl::producer<not_null<QWheelEvent*>> outerWheelEvents) {
	const auto volume = Ui::CreateChild<VolumeController>(
		dropdown.get(),
		controller);
	volume->show();
	volume->setIsVertical(true);

	dropdown->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto rect = QRect(QPoint(), size);
		const auto inner = rect.marginsRemoved(dropdown->getMargin());
		volume->setGeometry(
			inner.x(),
			inner.y() - st::lineWidth,
			inner.width(),
			(inner.height()
				+ st::lineWidth
				- ((st::mediaPlayerVolumeSize.width()
					- st::mediaPlayerPanelPlayback.width) / 2)));
	}, volume->lifetime());

	std::move(
		outerWheelEvents
	) | rpl::start_with_next([=](not_null<QWheelEvent*> e) {
		volume->outerWheelEvent(e);
	}, volume->lifetime());
}

} // namespace Media::Player
