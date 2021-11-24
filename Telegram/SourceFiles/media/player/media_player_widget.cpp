/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_widget.h"

#include "platform/platform_specific.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/format_values.h"
#include "ui/text/format_song_document_name.h"
#include "lang/lang_keys.h"
#include "media/audio/media_audio.h"
#include "media/view/media_view_playback_progress.h"
#include "media/player/media_player_button.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_dropdown.h"
#include "media/player/media_player_volume_controller.h"
#include "styles/style_media_player.h"
#include "styles/style_media_view.h"
#include "history/history_item.h"
#include "storage/storage_account.h"
#include "main/main_session.h"
#include "facades.h"

namespace Media {
namespace Player {

using ButtonState = PlayButtonLayout::State;

class Widget::PlayButton : public Ui::RippleButton {
public:
	PlayButton(QWidget *parent);

	void setState(PlayButtonLayout::State state) {
		_layout.setState(state);
	}
	void finishTransform() {
		_layout.finishTransform();
	}

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	PlayButtonLayout _layout;

};

class Widget::SpeedController final {
public:
	explicit SpeedController(not_null<Ui::IconButton*> button);

	[[nodiscard]] rpl::producer<> saved() const;

private:
	[[nodiscard]] float64 speed() const;
	[[nodiscard]] bool isDefault() const;
	[[nodiscard]] float64 lastNonDefaultSpeed() const;
	void toggleDefault();
	void setSpeed(float64 newSpeed);
	void save();
	void showContextMenu(not_null<QContextMenuEvent*> e);

	const not_null<Ui::IconButton*> _button;
	base::unique_qptr<Ui::PopupMenu> _menu;
	float64 _speed = 2.;
	bool _isDefault = true;
	rpl::event_stream<float64> _speedChanged;
	rpl::event_stream<> _saved;

};

Widget::SpeedController::SpeedController(not_null<Ui::IconButton*> button)
: _button(button) {
	setSpeed(Core::App().settings().voicePlaybackSpeed());
	_speed = Core::App().settings().voicePlaybackSpeed(true);

	button->setClickedCallback([=] {
		toggleDefault();
		save();
	});

	struct Icons {
		const style::icon *icon = nullptr;
		const style::icon *over = nullptr;
	};

	_speedChanged.events_starting_with(
		speed()
	) | rpl::start_with_next([=](float64 speed) {
		const auto isDefaultSpeed = isDefault();
		const auto nonDefaultSpeed = lastNonDefaultSpeed();

		const auto icons = [&]() -> Icons {
			if (nonDefaultSpeed == .5) {
				return {
					.icon = isDefaultSpeed
						? &st::mediaPlayerSpeedSlowDisabledIcon
						: &st::mediaPlayerSpeedSlowIcon,
					.over = isDefaultSpeed
						? &st::mediaPlayerSpeedSlowDisabledIconOver
						: &st::mediaPlayerSpeedSlowIcon,
				};
			} else if (nonDefaultSpeed == 1.5) {
				return {
					.icon = isDefaultSpeed
						? &st::mediaPlayerSpeedFastDisabledIcon
						: &st::mediaPlayerSpeedFastIcon,
					.over = isDefaultSpeed
						? &st::mediaPlayerSpeedFastDisabledIconOver
						: &st::mediaPlayerSpeedFastIcon,
				};
			} else {
				return {
					.icon = isDefaultSpeed
						? &st::mediaPlayerSpeedDisabledIcon
						: nullptr, // 2x icon.
					.over = isDefaultSpeed
						? &st::mediaPlayerSpeedDisabledIconOver
						: nullptr, // 2x icon.
				};
			}
		}();

		button->setIconOverride(icons.icon, icons.over);
		button->setRippleColorOverride(isDefaultSpeed
			? &st::mediaPlayerSpeedDisabledRippleBg
			: nullptr);
	}, button->lifetime());

	button->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::ContextMenu);
	}) | rpl::start_with_next([=](not_null<QEvent*> e) {
		showContextMenu(static_cast<QContextMenuEvent*>(e.get()));
	}, button->lifetime());
}

rpl::producer<> Widget::SpeedController::saved() const {
	return _saved.events();
}

float64 Widget::SpeedController::speed() const {
	return _isDefault ? 1. : _speed;
}

bool Widget::SpeedController::isDefault() const {
	return _isDefault;
}

float64 Widget::SpeedController::lastNonDefaultSpeed() const {
	return _speed;
}

void Widget::SpeedController::toggleDefault() {
	_isDefault = !_isDefault;
	_speedChanged.fire(speed());
}

void Widget::SpeedController::setSpeed(float64 newSpeed) {
	if (!(_isDefault = (newSpeed == 1.))) {
		_speed = newSpeed;
	}
	_speedChanged.fire(speed());
}

void Widget::SpeedController::save() {
	Core::App().settings().setVoicePlaybackSpeed(speed());
	Core::App().saveSettingsDelayed();
	_saved.fire({});
}

void Widget::SpeedController::showContextMenu(
		not_null<QContextMenuEvent*> e) {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		_button,
		st::mediaPlayerPopupMenu);

	const auto setPlaybackSpeed = [=](float64 speed) {
		setSpeed(speed);
		save();
	};

	const auto currentSpeed = speed();
	const auto addSpeed = [&](float64 speed, QString text = QString()) {
		if (text.isEmpty()) {
			text = QString::number(speed);
		}
		_menu->addAction(
			text,
			[=] { setPlaybackSpeed(speed); },
			(speed == currentSpeed) ? &st::mediaPlayerMenuCheck : nullptr);
	};
	addSpeed(0.5, tr::lng_voice_speed_slow(tr::now));
	addSpeed(1., tr::lng_voice_speed_normal(tr::now));
	addSpeed(1.5, tr::lng_voice_speed_fast(tr::now));
	addSpeed(2., tr::lng_voice_speed_very_fast(tr::now));

	_menu->popup(e->globalPos());
}

Widget::PlayButton::PlayButton(QWidget *parent) : Ui::RippleButton(parent, st::mediaPlayerButton.ripple)
, _layout(st::mediaPlayerButton, [this] { update(); }) {
	resize(st::mediaPlayerButtonSize);
	setCursor(style::cur_pointer);
}

void Widget::PlayButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	paintRipple(p, st::mediaPlayerButton.rippleAreaPosition.x(), st::mediaPlayerButton.rippleAreaPosition.y());
	p.translate(st::mediaPlayerButtonPosition.x(), st::mediaPlayerButtonPosition.y());
	_layout.paint(p, st::mediaPlayerActiveFg);
}

QImage Widget::PlayButton::prepareRippleMask() const {
	auto size = QSize(st::mediaPlayerButton.rippleAreaSize, st::mediaPlayerButton.rippleAreaSize);
	return Ui::RippleAnimation::ellipseMask(size);
}

QPoint Widget::PlayButton::prepareRippleStartPosition() const {
	return QPoint(mapFromGlobal(QCursor::pos()) - st::mediaPlayerButton.rippleAreaPosition);
}

Widget::Widget(
	QWidget *parent,
	not_null<Ui::RpWidget*> dropdownsParent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _nameLabel(this, st::mediaPlayerName)
, _rightControls(this, object_ptr<Ui::RpWidget>(this))
, _timeLabel(rightControls(), st::mediaPlayerTime)
, _playPause(this, st::mediaPlayerPlayButton)
, _volumeToggle(rightControls(), st::mediaPlayerVolumeToggle)
, _repeatToggle(rightControls(), st::mediaPlayerRepeatButton)
, _orderToggle(rightControls(), st::mediaPlayerRepeatButton)
, _playbackSpeed(rightControls(), st::mediaPlayerSpeedButton)
, _close(this, st::mediaPlayerClose)
, _shadow(this)
, _playbackSlider(this, st::mediaPlayerPlayback)
, _volume(dropdownsParent.get())
, _playbackProgress(std::make_unique<View::PlaybackProgress>())
, _speedController(
	std::make_unique<SpeedController>(
		_playbackSpeed.data())) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	setMouseTracking(true);
	resize(width(), st::mediaPlayerHeight + st::lineWidth);

	setupRightControls();

	_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	_timeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	_playbackProgress->setInLoadingStateChangedCallback([=](bool loading) {
		_playbackSlider->setDisabled(loading);
	});
	_playbackProgress->setValueChangedCallback([=](float64 value, float64) {
		_playbackSlider->setValue(value);
	});
	_playbackSlider->setChangeProgressCallback([=](float64 value) {
		if (_type != AudioMsgId::Type::Song) {
			return; // Round video seek is not supported for now :(
		}
		_playbackProgress->setValue(value, false);
		handleSeekProgress(value);
	});
	_playbackSlider->setChangeFinishedCallback([=](float64 value) {
		if (_type != AudioMsgId::Type::Song) {
			return; // Round video seek is not supported for now :(
		}
		_playbackProgress->setValue(value, false);
		handleSeekFinished(value);
	});
	_playPause->setClickedCallback([=] {
		instance()->playPauseCancelClicked(_type);
	});

	updateVolumeToggleIcon();
	_volumeToggle->setClickedCallback([=] {
		const auto volume = (Core::App().settings().songVolume() > 0)
			? 0.
			: Core::App().settings().rememberedSongVolume();
		Core::App().settings().setSongVolume(volume);
		Core::App().saveSettingsDelayed();
		mixer()->setSongVolume(volume);
	});
	Core::App().settings().songVolumeChanges(
	) | rpl::start_with_next([=] {
		updateVolumeToggleIcon();
	}, lifetime());

	Core::App().settings().playerRepeatModeValue(
	) | rpl::start_with_next([=] {
		updateRepeatToggleIcon();
	}, lifetime());

	Core::App().settings().playerOrderModeValue(
	) | rpl::start_with_next([=] {
		updateOrderToggleIcon();
	}, lifetime());

	_repeatToggle->setClickedCallback([=] {
		auto &settings = Core::App().settings();
		settings.setPlayerRepeatMode([&] {
			switch (settings.playerRepeatMode()) {
			case RepeatMode::None: return RepeatMode::One;
			case RepeatMode::One: return RepeatMode::All;
			case RepeatMode::All: return RepeatMode::None;
			}
			Unexpected("Repeat mode in Settings.");
		}());
		Core::App().saveSettingsDelayed();
	});

	_speedController->saved(
	) | rpl::start_with_next([=] {
		instance()->updateVoicePlaybackSpeed();
	}, lifetime());

	instance()->trackChanged(
	) | rpl::filter([=](AudioMsgId::Type type) {
		return (type == _type);
	}) | rpl::start_with_next([=](AudioMsgId::Type type) {
		handleSongChange();
		updateControlsVisibility();
		updateLabelsGeometry();
	}, lifetime());

	instance()->tracksFinished(
	) | rpl::filter([=](AudioMsgId::Type type) {
		return (type == AudioMsgId::Type::Voice);
	}) | rpl::start_with_next([=](AudioMsgId::Type type) {
		_voiceIsActive = false;
		const auto currentSong = instance()->current(AudioMsgId::Type::Song);
		const auto songState = instance()->getState(AudioMsgId::Type::Song);
		if (currentSong == songState.id && !IsStoppedOrStopping(songState.state)) {
			setType(AudioMsgId::Type::Song);
		}
	}, lifetime());

	instance()->updatedNotifier(
	) | rpl::start_with_next([=](const TrackState &state) {
		handleSongUpdate(state);
	}, lifetime());

	PrepareVolumeDropdown(_volume.data(), controller);
	_volumeToggle->installEventFilter(_volume.data());
	_volume->installEventFilter(this);

	setType(AudioMsgId::Type::Song);
}

void Widget::setupRightControls() {
	const auto raw = rightControls();
	raw->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(raw);
		const auto &icon = st::mediaPlayerControlsFade;
		const auto fade = QRect(0, 0, icon.width(), raw->height());
		if (fade.intersects(clip)) {
			icon.fill(p, fade);
		}
		const auto fill = clip.intersected(
			{ icon.width(), 0, raw->width() - icon.width(), raw->height() });
		if (!fill.isEmpty()) {
			p.fillRect(fill, st::mediaPlayerBg);
		}
	}, raw->lifetime());
	_rightControls->show(anim::type::instant);
}

void Widget::updateVolumeToggleIcon() {
	_volumeToggle->setIconOverride([] {
		const auto volume = Core::App().settings().songVolume();
		return (volume == 0.)
			? &st::mediaPlayerVolumeIcon0
			: (volume < 0.66)
			? &st::mediaPlayerVolumeIcon1
			: nullptr;
	}());
}

void Widget::setCloseCallback(Fn<void()> callback) {
	_closeCallback = std::move(callback);
	_close->setClickedCallback([this] { stopAndClose(); });
}

void Widget::setShowItemCallback(
		Fn<void(not_null<const HistoryItem*>)> callback) {
	_showItemCallback = std::move(callback);
}

void Widget::stopAndClose() {
	_voiceIsActive = false;
	if (_type == AudioMsgId::Type::Voice) {
		const auto songData = instance()->current(AudioMsgId::Type::Song);
		const auto songState = instance()->getState(AudioMsgId::Type::Song);
		if (songData == songState.id && !IsStoppedOrStopping(songState.state)) {
			instance()->stop(AudioMsgId::Type::Voice);
			return;
		}
	}
	if (_closeCallback) {
		_closeCallback();
	}
}

void Widget::setShadowGeometryToLeft(int x, int y, int w, int h) {
	_shadow->setGeometryToLeft(x, y, w, h);
}

void Widget::showShadowAndDropdowns() {
	_shadow->show();
	_playbackSlider->setVisible(_type == AudioMsgId::Type::Song);
	if (_volumeHidden) {
		_volumeHidden = false;
		_volume->show();
	}
}

void Widget::updateDropdownsGeometry() {
	const auto position = _volume->parentWidget()->mapFromGlobal(
		_volumeToggle->mapToGlobal(
			QPoint(
				(_volumeToggle->width() - st::mediaPlayerVolumeSize.width()) / 2,
				height())));
	const auto playerMargins = _volume->getMargin();
	_volume->move(position - QPoint(playerMargins.left(), playerMargins.top()));
}

void Widget::hideShadowAndDropdowns() {
	_shadow->hide();
	_playbackSlider->hide();
	if (!_volume->isHidden()) {
		_volumeHidden = true;
		_volume->hide();
	}
}

void Widget::raiseDropdowns() {
	_volume->raise();
}

Widget::~Widget() = default;

not_null<Ui::RpWidget*> Widget::rightControls() {
	return _rightControls->entity();
}

void Widget::handleSeekProgress(float64 progress) {
	if (!_lastDurationMs) return;

	const auto positionMs = std::clamp(
		static_cast<crl::time>(progress * _lastDurationMs),
		crl::time(0),
		_lastDurationMs);
	if (_seekPositionMs != positionMs) {
		_seekPositionMs = positionMs;
		updateTimeLabel();

		instance()->startSeeking(_type);
	}
}

void Widget::handleSeekFinished(float64 progress) {
	if (!_lastDurationMs) return;

	_seekPositionMs = -1;

	instance()->finishSeeking(_type, progress);
}

void Widget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
	_narrow = (width() < st::mediaPlayerWideWidth);
	updateControlsWrapVisibility();
}

void Widget::updateControlsGeometry() {
	_close->moveToRight(st::mediaPlayerCloseRight, st::mediaPlayerPlayTop);
	auto right = 0;
	if (hasPlaybackSpeedControl()) {
		_playbackSpeed->moveToRight(right, 0); right += _playbackSpeed->width();
	}
	_repeatToggle->moveToRight(right, 0); right += _repeatToggle->width();
	_orderToggle->moveToRight(right, 0); right += _orderToggle->width();
	_volumeToggle->moveToRight(right, 0); right += _volumeToggle->width();

	updateControlsWrapGeometry();

	updatePlayPrevNextPositions();

	_playbackSlider->setGeometry(0, height() - st::mediaPlayerPlayback.fullWidth, width(), st::mediaPlayerPlayback.fullWidth);

	updateDropdownsGeometry();
}

void Widget::updateControlsWrapGeometry() {
	const auto fade = st::mediaPlayerControlsFade.width();
	rightControls()->resize(
		getTimeRight() + _timeLabel->width() + fade,
		_repeatToggle->height());
	_rightControls->moveToRight(
		st::mediaPlayerCloseRight + _close->width(),
		st::mediaPlayerPlayTop);
}

void Widget::updateControlsWrapVisibility() {
	_rightControls->toggle(
		_over || !_narrow,
		isHidden() ? anim::type::instant : anim::type::normal);
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto fill = e->rect().intersected(QRect(0, 0, width(), st::mediaPlayerHeight));
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::mediaPlayerBg);
	}
}

bool Widget::eventFilter(QObject *o, QEvent *e) {
	const auto type = e->type();
	if (type == QEvent::Enter) {
		markOver(true);
	} else if (type == QEvent::Leave) {
		markOver(false);
	}
	return RpWidget::eventFilter(o, e);
}

void Widget::enterEventHook(QEnterEvent *e) {
	markOver(true);
}

void Widget::leaveEventHook(QEvent *e) {
	markOver(false);
}

void Widget::markOver(bool over) {
	if (over) {
		_over = true;
		_wontBeOver = false;
		updateControlsWrapVisibility();
	} else {
		_wontBeOver = true;
		InvokeQueued(this, [=] {
			if (!_wontBeOver) {
				return;
			}
			_wontBeOver = false;
			_over = false;
			updateControlsWrapVisibility();
			updateOverLabelsState(false);
		});
	}
}

void Widget::mouseMoveEvent(QMouseEvent *e) {
	updateOverLabelsState(e->pos());
}

void Widget::mousePressEvent(QMouseEvent *e) {
	_labelsDown = _labelsOver;
}

void Widget::mouseReleaseEvent(QMouseEvent *e) {
	if (auto downLabels = base::take(_labelsDown)) {
		if (_labelsOver != downLabels) {
			return;
		}
		if (_type == AudioMsgId::Type::Voice) {
			const auto current = instance()->current(_type);
			const auto document = current.audio();
			const auto context = current.contextId();
			if (document && context && _showItemCallback) {
				if (const auto item = document->owner().message(context)) {
					_showItemCallback(item);
				}
			}
		}
	}
}

void Widget::updateOverLabelsState(QPoint pos) {
	const auto left = getNameLeft();
	const auto right = width()
		- _rightControls->x()
		- _rightControls->width()
		+ getTimeRight();
	const auto labels = myrtlrect(left, 0, width() - right - left, height() - st::mediaPlayerPlayback.fullWidth);
	const auto over = labels.contains(pos);
	updateOverLabelsState(over);
}

void Widget::updateOverLabelsState(bool over) {
	_labelsOver = over;
	auto pressShowsItem = _labelsOver && (_type == AudioMsgId::Type::Voice);
	setCursor(pressShowsItem ? style::cur_pointer : style::cur_default);
	_togglePlaylistRequests.fire(over && (_type == AudioMsgId::Type::Song));
}

void Widget::updatePlayPrevNextPositions() {
	auto left = st::mediaPlayerPlayLeft;
	auto top = st::mediaPlayerPlayTop;
	if (_previousTrack) {
		_previousTrack->moveToLeft(left, top); left += _previousTrack->width() + st::mediaPlayerPlaySkip;
		_playPause->moveToLeft(left, top); left += _playPause->width() + st::mediaPlayerPlaySkip;
		_nextTrack->moveToLeft(left, top);
	} else {
		_playPause->moveToLeft(left, top);
	}
	updateLabelsGeometry();
}

int Widget::getNameLeft() const {
	auto result = st::mediaPlayerPlayLeft + _playPause->width();
	if (_previousTrack) {
		result += _previousTrack->width() + st::mediaPlayerPlaySkip + _nextTrack->width() + st::mediaPlayerPlaySkip;
	}
	result += st::mediaPlayerPadding;
	return result;
}

int Widget::getNameRight() const {
	return st::mediaPlayerCloseRight
		+ _close->width()
		+ st::mediaPlayerPadding;
}

int Widget::getTimeRight() const {
	auto result = 0;
	if (_type == AudioMsgId::Type::Song) {
		result += _repeatToggle->width()
			+ _orderToggle->width()
			+ _volumeToggle->width();
	}
	if (hasPlaybackSpeedControl()) {
		result += _playbackSpeed->width();
	}
	result += st::mediaPlayerPadding;
	return result;
}

void Widget::updateLabelsGeometry() {
	const auto left = getNameLeft();
	const auto widthForName = width()
		- left
		- getNameRight();
	_nameLabel->resizeToWidth(widthForName);
	_nameLabel->moveToLeft(left, st::mediaPlayerNameTop - st::mediaPlayerName.style.font->ascent);

	const auto right = getTimeRight();
	_timeLabel->moveToRight(right, st::mediaPlayerNameTop - st::mediaPlayerTime.font->ascent);

	updateControlsWrapGeometry();
}

void Widget::updateRepeatToggleIcon() {
	switch (Core::App().settings().playerRepeatMode()) {
	case RepeatMode::None:
		_repeatToggle->setIconOverride(
			&st::mediaPlayerRepeatDisabledIcon,
			&st::mediaPlayerRepeatDisabledIconOver);
		_repeatToggle->setRippleColorOverride(
			&st::mediaPlayerRepeatDisabledRippleBg);
		break;
	case RepeatMode::One:
		_repeatToggle->setIconOverride(&st::mediaPlayerRepeatOneIcon);
		_repeatToggle->setRippleColorOverride(nullptr);
		break;
	case RepeatMode::All:
		_repeatToggle->setIconOverride(nullptr);
		_repeatToggle->setRippleColorOverride(nullptr);
		break;
	}
}

void Widget::updateOrderToggleIcon() {
	switch (Core::App().settings().playerOrderMode()) {
	case OrderMode::Default:
		_orderToggle->setIconOverride(
			&st::mediaPlayerReverseDisabledIcon,
			&st::mediaPlayerReverseDisabledIconOver);
		_orderToggle->setRippleColorOverride(
			&st::mediaPlayerRepeatDisabledRippleBg);
		break;
	case OrderMode::Reverse:
		_orderToggle->setIconOverride(nullptr);
		_orderToggle->setRippleColorOverride(nullptr);
		break;
	case OrderMode::Shuffle:
		_orderToggle->setIconOverride(&st::mediaPlayerShuffleIcon);
		_orderToggle->setRippleColorOverride(nullptr);
		break;
	}
}

void Widget::checkForTypeChange() {
	auto hasActiveType = [](AudioMsgId::Type type) {
		const auto current = instance()->current(type);
		const auto state = instance()->getState(type);
		return (current == state.id && !IsStoppedOrStopping(state.state));
	};
	if (hasActiveType(AudioMsgId::Type::Voice)) {
		_voiceIsActive = true;
		setType(AudioMsgId::Type::Voice);
	} else if (!_voiceIsActive && hasActiveType(AudioMsgId::Type::Song)) {
		setType(AudioMsgId::Type::Song);
	}
}

bool Widget::hasPlaybackSpeedControl() const {
	return _lastSongId.changeablePlaybackSpeed()
		&& Media::Audio::SupportsSpeedControl();
}

void Widget::updateControlsVisibility() {
	_repeatToggle->setVisible(_type == AudioMsgId::Type::Song);
	_volumeToggle->setVisible(_type == AudioMsgId::Type::Song);
	_playbackSpeed->setVisible(hasPlaybackSpeedControl());
	if (!_shadow->isHidden()) {
		_playbackSlider->setVisible(_type == AudioMsgId::Type::Song);
	}
	updateControlsGeometry();
}

void Widget::setType(AudioMsgId::Type type) {
	if (_type != type) {
		_type = type;
		handleSongChange();
		updateControlsVisibility();
		updateLabelsGeometry();
		handleSongUpdate(instance()->getState(_type));
		updateOverLabelsState(_labelsOver);
		_playlistChangesLifetime = instance()->playlistChanges(
			_type
		) | rpl::start_with_next([=] {
			handlePlaylistUpdate();
		});
		// maybe the type change causes a change of the button layout
		QResizeEvent event = { size(), size() };
		resizeEvent(&event);
	}
}

void Widget::handleSongUpdate(const TrackState &state) {
	checkForTypeChange();
	if (state.id.type() != _type || !state.id.audio()) {
		return;
	}

	if (state.id.audio()->loading()) {
		_playbackProgress->updateLoadingState(state.id.audio()->progress());
	} else {
		_playbackProgress->updateState(state);
	}

	auto showPause = ShowPauseIcon(state.state);
	if (instance()->isSeeking(_type)) {
		showPause = true;
	}
	_playPause->setIconOverride(state.id.audio()->loading()
		? &st::mediaPlayerCancelIcon
		: showPause
		? &st::mediaPlayerPauseIcon
		: nullptr);

	updateTimeText(state);
}

void Widget::updateTimeText(const TrackState &state) {
	qint64 display = 0;
	const auto frequency = state.frequency;
	const auto document = state.id.audio();
	if (!IsStoppedOrStopping(state.state)) {
		display = state.position;
	} else if (state.length) {
		display = state.length;
	} else if (const auto song = document->song()) {
		display = (song->duration * frequency);
	}

	_lastDurationMs = (state.length * 1000LL) / frequency;

	if (document->loading()) {
		_time = QString::number(qRound(document->progress() * 100)) + '%';
		_playbackSlider->setDisabled(true);
	} else {
		display = display / frequency;
		_time = Ui::FormatDurationText(display);
		_playbackSlider->setDisabled(false);
	}
	if (_seekPositionMs < 0) {
		updateTimeLabel();
	}
}

void Widget::updateTimeLabel() {
	auto timeLabelWidth = _timeLabel->width();
	if (_seekPositionMs >= 0) {
		auto playAlready = _seekPositionMs / 1000LL;
		_timeLabel->setText(Ui::FormatDurationText(playAlready));
	} else {
		_timeLabel->setText(_time);
	}
	if (timeLabelWidth != _timeLabel->width()) {
		updateLabelsGeometry();
	}
}

void Widget::handleSongChange() {
	const auto current = instance()->current(_type);
	const auto document = current.audio();
	if (!current
		|| !document
		|| ((_lastSongId.audio() == document)
			&& (_lastSongId.contextId() == current.contextId()))) {
		return;
	}
	_lastSongId = current;

	TextWithEntities textWithEntities;
	if (document->isVoiceMessage() || document->isVideoMessage()) {
		if (const auto item = document->owner().message(current.contextId())) {
			const auto name = item->fromOriginal()->name;
			const auto date = [item] {
				const auto parsed = ItemDateTime(item);
				const auto date = parsed.date();
				const auto time = parsed.time().toString(cTimeFormat());
				const auto today = QDateTime::currentDateTime().date();
				if (date == today) {
					return tr::lng_player_message_today(
						tr::now,
						lt_time,
						time);
				} else if (date.addDays(1) == today) {
					return tr::lng_player_message_yesterday(
						tr::now,
						lt_time,
						time);
				}
				return tr::lng_player_message_date(
					tr::now,
					lt_date,
					langDayOfMonthFull(date),
					lt_time,
					time);
			};

			textWithEntities.text = name + ' ' + date();
			textWithEntities.entities.append(EntityInText(
				EntityType::Semibold,
				0,
				name.size(),
				QString()));
		} else {
			textWithEntities.text = tr::lng_media_audio(tr::now);
		}
	} else {
		textWithEntities = Ui::Text::FormatSongNameFor(document)
			.textWithEntities(true);
	}
	_nameLabel->setMarkedText(textWithEntities);

	handlePlaylistUpdate();
}

void Widget::handlePlaylistUpdate() {
	const auto previousEnabled = instance()->previousAvailable(_type);
	const auto nextEnabled = instance()->nextAvailable(_type);
	if (!previousEnabled && !nextEnabled) {
		destroyPrevNextButtons();
	} else {
		createPrevNextButtons();
		_previousTrack->setIconOverride(previousEnabled ? nullptr : &st::mediaPlayerPreviousDisabledIcon);
		_previousTrack->setRippleColorOverride(previousEnabled ? nullptr : &st::mediaPlayerBg);
		_previousTrack->setPointerCursor(previousEnabled);
		_nextTrack->setIconOverride(nextEnabled ? nullptr : &st::mediaPlayerNextDisabledIcon);
		_nextTrack->setRippleColorOverride(nextEnabled ? nullptr : &st::mediaPlayerBg);
		_nextTrack->setPointerCursor(nextEnabled);
	}
}

void Widget::createPrevNextButtons() {
	if (!_previousTrack) {
		_previousTrack.create(this, st::mediaPlayerPreviousButton);
		_previousTrack->show();
		_previousTrack->setClickedCallback([=]() {
			instance()->previous();
		});
		_nextTrack.create(this, st::mediaPlayerNextButton);
		_nextTrack->show();
		_nextTrack->setClickedCallback([=]() {
			instance()->next();
		});
		updatePlayPrevNextPositions();
	}
}

void Widget::destroyPrevNextButtons() {
	if (_previousTrack) {
		_previousTrack.destroy();
		_nextTrack.destroy();
		updatePlayPrevNextPositions();
	}
}

} // namespace Player
} // namespace Media
