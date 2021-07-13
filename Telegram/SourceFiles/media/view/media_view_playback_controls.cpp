/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_playback_controls.h"

#include "media/audio/media_audio.h"
#include "media/view/media_view_playback_progress.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/effects/fade_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text/format_values.h"
#include "ui/cached_round_corners.h"
#include "lang/lang_keys.h"
#include "styles/style_media_view.h"

namespace Media {
namespace View {
namespace {

constexpr auto kMinSpeed = 50;
constexpr auto kMaxSpeed = 200;

constexpr float64 SpeedShiftToValue(float64 value) {
	const auto valueAsSpeedF = value * 100.;
	const auto valueAsSpeed = int(valueAsSpeedF + 0.5); // constexpr round.
	return float64(valueAsSpeed) / (kMaxSpeed - kMinSpeed);
};

constexpr float64 SpeedToValue(float64 value) {
	const auto valueAsSpeedF = value * 100.;
	const auto valueAsSpeed = int(valueAsSpeedF + 0.5); // constexpr round.
	return float64(valueAsSpeed - kMinSpeed) / (kMaxSpeed - kMinSpeed);
};

constexpr auto kSpeedStickedValues =
	std::array<std::pair<float64, float64>, 5>{{
		{ SpeedToValue(0.75), SpeedShiftToValue(0.03) },
		{ SpeedToValue(1.00), SpeedShiftToValue(0.05) },
		{ SpeedToValue(1.25), SpeedShiftToValue(0.03) },
		{ SpeedToValue(1.50), SpeedShiftToValue(0.03) },
		{ SpeedToValue(1.75), SpeedShiftToValue(0.03) },
	}};

class MenuSpeedItem final : public Ui::Menu::ItemBase {
public:
	MenuSpeedItem(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		float64 startSpeed);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

	[[nodiscard]] rpl::producer<float64> changeSpeedRequests() const;

protected:
	int contentHeight() const override;

private:
	float64 computeSpeed(float64 value) const;
	QString speedString(float64 value) const;

	QRect _itemRect;
	QRect _textRect;

	const style::MediaSlider &_sliderSt;
	const base::unique_qptr<Ui::MediaSlider> _slider;
	const not_null<QAction*> _dummyAction;
	const int _lineCount;
	const int _height;

	rpl::event_stream<float64> _changeSpeedRequests;

};

MenuSpeedItem::MenuSpeedItem(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	float64 startSpeed)
: Ui::Menu::ItemBase(parent, st)
, _sliderSt(st::mediaviewPlayback)
, _slider(base::make_unique_q<Ui::MediaSlider>(
	this,
	_sliderSt))
, _dummyAction(new QAction(parent))
, _lineCount(std::ceil(st.itemStyle.font->width(speedString(0.9))
	/ float(st.widthMax)))
, _height(st.itemPadding.top() * 2
	+ st.itemStyle.font->height * _lineCount
	+ _sliderSt.seekSize.height()
	+ st.itemPadding.bottom() * 2) {

	initResizeHook(parent->sizeValue());
	enableMouseSelecting();
	enableMouseSelecting(_slider.get());

	_slider->setAlwaysDisplayMarker(true);
	_slider->setValue((std::round(startSpeed * 100.) - kMinSpeed)
		/ (kMaxSpeed - kMinSpeed));

	for (const auto &sticked : kSpeedStickedValues) {
		_slider->addDivider(sticked.first, st::speedSliderDividerSize);
	}
	//_slider->addDivider(
	//	kSpeedStickedValues[1].first,
	//	st::speedSliderDividerSize);

	{
		const auto goodWidth = st.itemPadding.left()
			+ st.itemPadding.right()
			+ st.itemStyle.font->width(speedString(0.9));
		setMinWidth(std::clamp(goodWidth, st.widthMin, st.widthMax));
	}

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		const auto geometry = QRect(QPoint(), size);
		_itemRect = geometry - st.itemPadding;

		const auto height = _itemRect.height();
		const auto penWidth = QPen(st.itemBgOver).width();
		_textRect = _itemRect
			- style::margins(
				-penWidth,
				0,
				-penWidth,
				height - st.itemStyle.font->height * _lineCount);

		const auto sliderGeometry = _itemRect
			- style::margins(0, height - _sliderSt.seekSize.height(), 0, 0);
		_slider->setGeometry(sliderGeometry);
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);

		const auto selected = isSelected();
		p.fillRect(clip, selected ? st.itemBgOver : st.itemBg);

		const auto value = _slider->value();

		p.setPen(selected ? st.itemFgOver : st.itemFg);
		p.setFont(st.itemStyle.font);
		p.drawText(_textRect, speedString(value), style::al_left);
	}, lifetime());

	_slider->setChangeProgressCallback([=](float64 value) {
		update(_textRect);
	});

	_slider->setChangeFinishedCallback([=](float64 value) {
		_changeSpeedRequests.fire_copy(computeSpeed(value));
	});

	_slider->setAdjustCallback([=](float64 value) {
		for (const auto &snap : kSpeedStickedValues) {
			if (value > (snap.first - snap.second)
				&& value < (snap.first + snap.second)) {
				return snap.first;
			}
		}
		return value;
	});
}

float64 MenuSpeedItem::computeSpeed(float64 value) const {
	return anim::interpolate(kMinSpeed, kMaxSpeed, std::clamp(value, 0., 1.))
		/ 100.;
}

QString MenuSpeedItem::speedString(float64 value) const {
	return tr::lng_mediaview_playback_speed(
		tr::now,
		lt_speed,
		QString::number(computeSpeed(value), 'f', 2) + 'x');
}

not_null<QAction*> MenuSpeedItem::action() const {
	return _dummyAction;
}

bool MenuSpeedItem::isEnabled() const {
	return true;
}

int MenuSpeedItem::contentHeight() const {
	return _height;
}

rpl::producer<float64> MenuSpeedItem::changeSpeedRequests() const {
	return _changeSpeedRequests.events();
}

} // namespace

PlaybackControls::PlaybackControls(
	QWidget *parent,
	not_null<Delegate*> delegate)
: RpWidget(parent)
, _delegate(delegate)
, _playPauseResume(this, st::mediaviewPlayButton)
, _playbackSlider(this, st::mediaviewPlayback)
, _playbackProgress(std::make_unique<PlaybackProgress>())
, _volumeToggle(this, st::mediaviewVolumeToggle)
, _volumeController(this, st::mediaviewPlayback)
, _menuToggle(this, st::mediaviewMenuToggle)
, _fullScreenToggle(this, st::mediaviewFullScreenButton)
, _pictureInPicture(this, st::mediaviewPipButton)
, _playedAlready(this, st::mediaviewPlayProgressLabel)
, _toPlayLeft(this, st::mediaviewPlayProgressLabel)
, _menuStyle(st::mediaviewControlsPopupMenu)
, _fadeAnimation(std::make_unique<Ui::FadeAnimation>(this)) {
	_fadeAnimation->show();
	_fadeAnimation->setFinishedCallback([=] {
		fadeFinished();
	});
	_fadeAnimation->setUpdatedCallback([=](float64 opacity) {
		fadeUpdated(opacity);
	});

	_pictureInPicture->addClickHandler([=] {
		_delegate->playbackControlsToPictureInPicture();
	});
	_menuToggle->addClickHandler([=] {
		showMenu();
	});

	_volumeController->setValue(_delegate->playbackControlsCurrentVolume());
	_volumeController->setChangeProgressCallback([=](float64 value) {
		_delegate->playbackControlsVolumeChanged(value);
		updateVolumeToggleIcon();
	});
	_volumeController->setChangeFinishedCallback([=](float64) {
		_delegate->playbackControlsVolumeChangeFinished();
	});
	updateVolumeToggleIcon();
	_volumeToggle->setClickedCallback([=] {
		_delegate->playbackControlsVolumeToggled();
		_volumeController->setValue(_delegate->playbackControlsCurrentVolume());
		updateVolumeToggleIcon();
	});

	_playPauseResume->addClickHandler([=] {
		if (_showPause) {
			_delegate->playbackControlsPause();
		} else {
			_delegate->playbackControlsPlay();
		}
	});
	_fullScreenToggle->addClickHandler([=] {
		if (_inFullScreen) {
			_delegate->playbackControlsFromFullScreen();
		} else {
			_delegate->playbackControlsToFullScreen();
		}
	});

	_playbackProgress->setValueChangedCallback([=](
			float64 value,
			float64 receivedTill) {
		_playbackSlider->setValue(value, receivedTill);
	});
	_playbackSlider->setChangeProgressCallback([=](float64 value) {
		_playbackProgress->setValue(value, false);

		// This may destroy PlaybackControls.
		handleSeekProgress(value);
	});
	_playbackSlider->setChangeFinishedCallback([=](float64 value) {
		_playbackProgress->setValue(value, false);
		handleSeekFinished(value);
	});
}

void PlaybackControls::handleSeekProgress(float64 progress) {
	if (!_lastDurationMs) return;

	const auto positionMs = std::clamp(
		static_cast<crl::time>(progress * _lastDurationMs),
		crl::time(0),
		_lastDurationMs);
	if (_seekPositionMs != positionMs) {
		_seekPositionMs = positionMs;
		refreshTimeTexts();

		// This may destroy PlaybackControls.
		_delegate->playbackControlsSeekProgress(positionMs);
	}
}

void PlaybackControls::handleSeekFinished(float64 progress) {
	if (!_lastDurationMs) return;

	const auto positionMs = std::clamp(
		static_cast<crl::time>(progress * _lastDurationMs),
		crl::time(0),
		_lastDurationMs);
	_seekPositionMs = -1;
	_delegate->playbackControlsSeekFinished(positionMs);
	refreshTimeTexts();
}

template <typename Callback>
void PlaybackControls::startFading(Callback start) {
	if (!_fadeAnimation->animating()) {
		showChildren();
		_playbackSlider->disablePaint(true);
		_volumeController->disablePaint(true);
		_childrenHidden = false;
	}
	start();
	if (_fadeAnimation->animating()) {
		for (const auto child : children()) {
			if (child->isWidgetType()
				&& child != _playbackSlider
				&& child != _volumeController) {
				static_cast<QWidget*>(child)->hide();
			}
		}
		_childrenHidden = true;
	} else {
		fadeFinished();
	}
	_playbackSlider->disablePaint(false);
	_volumeController->disablePaint(false);
}

void PlaybackControls::showAnimated() {
	startFading([this]() {
		_fadeAnimation->fadeIn(st::mediaviewShowDuration);
	});
}

void PlaybackControls::hideAnimated() {
	startFading([this]() {
		_fadeAnimation->fadeOut(st::mediaviewHideDuration);
	});
}

void PlaybackControls::fadeFinished() {
	fadeUpdated(_fadeAnimation->visible() ? 1. : 0.);
}

void PlaybackControls::fadeUpdated(float64 opacity) {
	_playbackSlider->setFadeOpacity(opacity);
	_volumeController->setFadeOpacity(opacity);
}

void PlaybackControls::showMenu() {
	if (_menu) {
		_menu = nullptr;
		return;
	}

	_menu.emplace(this, _menuStyle);

	{
		auto speedItem = base::make_unique_q<MenuSpeedItem>(
			_menu,
			_menuStyle.menu,
			_delegate->playbackControlsCurrentSpeed());
		speedItem->changeSpeedRequests(
		) | rpl::start_with_next([=](float64 speed) {
			updatePlaybackSpeed(speed);
		}, speedItem->lifetime());
		_menu->addAction(std::move(speedItem));
	}

	_menu->addSeparator();

	_menu->addAction(tr::lng_mediaview_rotate_video(tr::now), [=] {
		_delegate->playbackControlsRotate();
	});

	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::BottomLeft);
	_menu->popup(mapToGlobal(_menuToggle->geometry().topLeft()));
}

void PlaybackControls::updatePlaybackSpeed(float64 speed) {
	DEBUG_LOG(("Media playback speed: update to %1.").arg(speed));
	_delegate->playbackControlsSpeedChanged(speed);
	resizeEvent(nullptr);
}

void PlaybackControls::updatePlayback(const Player::TrackState &state) {
	updatePlayPauseResumeState(state);
	_playbackProgress->updateState(state, countDownloadedTillPercent(state));
	updateTimeTexts(state);
}

void PlaybackControls::updateVolumeToggleIcon() {
	const auto volume = _delegate->playbackControlsCurrentVolume();
	_volumeToggle->setIconOverride([&] {
		return (volume <= 0.)
			? nullptr
			: (volume < 1 / 2.)
			? &st::mediaviewVolumeIcon1
			: &st::mediaviewVolumeIcon2;
	}(), [&] {
		return (volume <= 0.)
			? nullptr
			: (volume < 1 / 2.)
			? &st::mediaviewVolumeIcon1Over
			: &st::mediaviewVolumeIcon2Over;
	}());
}

float64 PlaybackControls::countDownloadedTillPercent(
		const Player::TrackState &state) const {
	if (_loadingReady > 0 && _loadingReady == _loadingTotal) {
		return 1.;
	}
	const auto header = state.fileHeaderSize;
	if (!header || _loadingReady <= header || _loadingTotal <= header) {
		return 0.;
	}
	return (_loadingReady - header) / float64(_loadingTotal - header);
}

void PlaybackControls::setLoadingProgress(int ready, int total) {
	if (_loadingReady == ready && _loadingTotal == total) {
		return;
	}
	_loadingReady = ready;
	_loadingTotal = total;
	if (_loadingReady != 0 && _loadingReady != _loadingTotal) {
		if (!_downloadProgress) {
			_downloadProgress.create(this, st::mediaviewPlayProgressLabel);
			_downloadProgress->setVisible(!_fadeAnimation->animating());
			_loadingPercent = -1;
		}
		const auto progress = total ? (ready / float64(total)) : 0.;
		const auto percent = int(std::round(progress * 100));
		if (_loadingPercent != percent) {
			_loadingPercent = percent;
			_downloadProgress->setText(QString::number(percent) + '%');
			updateDownloadProgressPosition();
			refreshFadeCache();
		}
	} else {
		_downloadProgress.destroy();
	}
}

void PlaybackControls::refreshFadeCache() {
	if (!_fadeAnimation->animating()) {
		return;
	}
	startFading([&] {
		_fadeAnimation->refreshCache();
	});
}

void PlaybackControls::updatePlayPauseResumeState(const Player::TrackState &state) {
	auto showPause = ShowPauseIcon(state.state) || (_seekPositionMs >= 0);
	if (showPause != _showPause) {
		_showPause = showPause;
		_playPauseResume->setIconOverride(_showPause ? &st::mediaviewPauseIcon : nullptr, _showPause ? &st::mediaviewPauseIconOver : nullptr);
	}
}

void PlaybackControls::updateTimeTexts(const Player::TrackState &state) {
	qint64 position = 0;

	if (Player::IsStoppedAtEnd(state.state)) {
		position = state.length;
	} else if (!Player::IsStoppedOrStopping(state.state)) {
		position = state.position;
	} else {
		position = 0;
	}
	auto playFrequency = state.frequency;
	auto playAlready = position / playFrequency;
	auto playLeft = (state.length / playFrequency) - playAlready;

	_lastDurationMs = (state.length * crl::time(1000)) / playFrequency;

	_timeAlready = Ui::FormatDurationText(playAlready);
	auto minus = QChar(8722);
	_timeLeft = minus + Ui::FormatDurationText(playLeft);

	if (_seekPositionMs < 0) {
		refreshTimeTexts();
	}
}

void PlaybackControls::refreshTimeTexts() {
	auto alreadyChanged = false, leftChanged = false;
	auto timeAlready = _timeAlready;
	auto timeLeft = _timeLeft;
	if (_seekPositionMs >= 0) {
		auto playAlready = _seekPositionMs / crl::time(1000);
		auto playLeft = (_lastDurationMs / crl::time(1000)) - playAlready;

		timeAlready = Ui::FormatDurationText(playAlready);
		auto minus = QChar(8722);
		timeLeft = minus + Ui::FormatDurationText(playLeft);
	}

	_playedAlready->setText(timeAlready, &alreadyChanged);
	_toPlayLeft->setText(timeLeft, &leftChanged);
	if (alreadyChanged || leftChanged) {
		resizeEvent(nullptr);
		refreshFadeCache();
	}
}

void PlaybackControls::setInFullScreen(bool inFullScreen) {
	if (_inFullScreen != inFullScreen) {
		_inFullScreen = inFullScreen;
		_fullScreenToggle->setIconOverride(
			_inFullScreen ? &st::mediaviewFullScreenOutIcon : nullptr,
			_inFullScreen ? &st::mediaviewFullScreenOutIconOver : nullptr);
	}
}

void PlaybackControls::resizeEvent(QResizeEvent *e) {
	const auto textSkip = st::mediaviewPlayProgressSkip;
	const auto textLeft = st::mediaviewPlayProgressLeft;
	const auto textTop = st::mediaviewPlaybackTop + (_playbackSlider->height() - _playedAlready->height()) / 2;
	_playedAlready->moveToLeft(textLeft + textSkip, textTop);
	_toPlayLeft->moveToRight(textLeft + textSkip, textTop);
	const auto remove = 2 * textLeft + 4 * textSkip + _playedAlready->width() + _toPlayLeft->width();
	auto playbackWidth = width() - remove;
	_playbackSlider->resize(playbackWidth, st::mediaviewPlayback.seekSize.height());
	_playbackSlider->moveToLeft(textLeft + 2 * textSkip + _playedAlready->width(), st::mediaviewPlaybackTop);

	_playPauseResume->moveToLeft(
		(width() - _playPauseResume->width()) / 2,
		st::mediaviewPlayButtonTop);

	auto right = st::mediaviewMenuToggleSkip;
	_menuToggle->moveToRight(right, st::mediaviewButtonsTop);
	right += _menuToggle->width() + st::mediaviewPipButtonSkip;
	_pictureInPicture->moveToRight(right, st::mediaviewButtonsTop);
	right += _pictureInPicture->width() + st::mediaviewFullScreenButtonSkip;
	_fullScreenToggle->moveToRight(right, st::mediaviewButtonsTop);

	updateDownloadProgressPosition();

	auto left = st::mediaviewVolumeToggleSkip;
	_volumeToggle->moveToLeft(left, st::mediaviewVolumeTop);
	left += _volumeToggle->width() + st::mediaviewVolumeSkip;
	_volumeController->resize(
		st::mediaviewVolumeWidth,
		st::mediaviewPlayback.seekSize.height());
	_volumeController->moveToLeft(left, st::mediaviewVolumeTop + (_volumeToggle->height() - _volumeController->height()) / 2);
}

void PlaybackControls::updateDownloadProgressPosition() {
	if (!_downloadProgress) {
		return;
	}
	const auto left = _playPauseResume->x() + _playPauseResume->width();
	const auto right = _fullScreenToggle->x();
	const auto available = right - left;
	const auto x = left + (available - _downloadProgress->width()) / 2;
	const auto y = _playPauseResume->y() + (_playPauseResume->height() - _downloadProgress->height()) / 2;
	_downloadProgress->move(x, y);
}

void PlaybackControls::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_fadeAnimation->paint(p)) {
		return;
	}
	if (_childrenHidden) {
		showChildren();
		_playbackSlider->setFadeOpacity(1.);
		_volumeController->setFadeOpacity(1.);
		_childrenHidden = false;
	}
	Ui::FillRoundRect(p, rect(), st::mediaviewSaveMsgBg, Ui::MediaviewSaveCorners);
}

void PlaybackControls::mousePressEvent(QMouseEvent *e) {
	e->accept(); // Don't pass event to the Media::View::OverlayWidget.
}

bool PlaybackControls::hasMenu() const {
	return _menu != nullptr;
}

PlaybackControls::~PlaybackControls() = default;

} // namespace View
} // namespace Media
