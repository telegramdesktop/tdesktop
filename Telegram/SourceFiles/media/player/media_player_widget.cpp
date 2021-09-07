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
#include "ui/effects/ripple_animation.h"
#include "ui/text/format_values.h"
#include "ui/text/format_song_document_name.h"
#include "lang/lang_keys.h"
#include "media/audio/media_audio.h"
#include "media/view/media_view_playback_progress.h"
#include "media/player/media_player_button.h"
#include "media/player/media_player_instance.h"
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

class Widget::SpeedButton : public Ui::IconButton {
public:
	SpeedButton(QWidget *parent, const style::IconButton &st);

	[[nodiscard]] rpl::producer<> saved() const;

protected:
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	class SpeedController final {
	public:
		SpeedController() {
			setSpeed(Core::App().settings().voicePlaybackSpeed());
			_speed = Core::App().settings().voicePlaybackSpeed(true);
		}

		[[nodiscard]] rpl::producer<float64> speedValue() const {
			return _speedChanged.events_starting_with(speed());
		}
		[[nodiscard]] rpl::producer<> saved() const {
			return _saved.events();
		}
		[[nodiscard]] float64 speed() const {
			return _isDefault ? 1. : _speed;
		}
		[[nodiscard]] bool isDefault() const {
			return _isDefault;
		}
		[[nodiscard]] float64 lastNonDefaultSpeed() const {
			return _speed;
		}
		void toggleDefault() {
			_isDefault = !_isDefault;
			_speedChanged.fire(speed());
		}
		void setSpeed(float64 newSpeed) {
			if (!(_isDefault = (newSpeed == 1.))) {
				_speed = newSpeed;
			}
			_speedChanged.fire(speed());
		}
		void save() {
			Core::App().settings().setVoicePlaybackSpeed(speed());
			Core::App().saveSettingsDelayed();
			_saved.fire({});
		}

	private:
		float64 _speed = 2.;
		bool _isDefault = true;
		rpl::event_stream<float64> _speedChanged;
		rpl::event_stream<> _saved;
	};

	SpeedController _speed;

	base::unique_qptr<Ui::PopupMenu> _menu;

};

Widget::SpeedButton::SpeedButton(QWidget *parent, const style::IconButton &st)
: IconButton(parent, st) {
	setClickedCallback([=] {
		_speed.toggleDefault();
		_speed.save();
	});

	struct Icons {
		const style::icon *icon = nullptr;
		const style::icon *over = nullptr;
	};

	_speed.speedValue(
	) | rpl::start_with_next([=](float64 speed) {
		const auto isDefaultSpeed = _speed.isDefault();
		const auto nonDefaultSpeed = _speed.lastNonDefaultSpeed();

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

		setIconOverride(icons.icon, icons.over);
		setRippleColorOverride(isDefaultSpeed
			? &st::mediaPlayerSpeedDisabledRippleBg
			: nullptr);
	}, lifetime());
}

void Widget::SpeedButton::contextMenuEvent(QContextMenuEvent *e) {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::mediaPlayerPopupMenu);

	const auto setPlaybackSpeed = [=](float64 speed) {
		_speed.setSpeed(speed);
		_speed.save();
	};

	const auto currentSpeed = _speed.speed();
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

rpl::producer<> Widget::SpeedButton::saved() const {
	return _speed.saved();
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

Widget::Widget(QWidget *parent, not_null<Main::Session*> session)
: RpWidget(parent)
, _session(session)
, _nameLabel(this, st::mediaPlayerName)
, _timeLabel(this, st::mediaPlayerTime)
, _playPause(this)
, _volumeToggle(this, st::mediaPlayerVolumeToggle)
, _repeatTrack(this, st::mediaPlayerRepeatButton)
, _playbackSpeed(this, st::mediaPlayerSpeedButton)
, _close(this, st::mediaPlayerClose)
, _shadow(this)
, _playbackSlider(this, st::mediaPlayerPlayback)
, _playbackProgress(std::make_unique<View::PlaybackProgress>()) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	setMouseTracking(true);
	resize(width(), st::mediaPlayerHeight + st::lineWidth);

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

	updateRepeatTrackIcon();
	_repeatTrack->setClickedCallback([=] {
		instance()->toggleRepeat(AudioMsgId::Type::Song);
	});

	_playbackSpeed->saved(
	) | rpl::start_with_next([=] {
		instance()->updateVoicePlaybackSpeed();
	}, lifetime());

	subscribe(instance()->repeatChangedNotifier(), [this](AudioMsgId::Type type) {
		if (type == _type) {
			updateRepeatTrackIcon();
		}
	});
	subscribe(instance()->trackChangedNotifier(), [this](AudioMsgId::Type type) {
		if (type == _type) {
			handleSongChange();
			updateControlsVisibility();
			updateLabelsGeometry();
		}
	});
	subscribe(instance()->tracksFinishedNotifier(), [this](AudioMsgId::Type type) {
		if (type == AudioMsgId::Type::Voice) {
			_voiceIsActive = false;
			const auto currentSong = instance()->current(AudioMsgId::Type::Song);
			const auto songState = instance()->getState(AudioMsgId::Type::Song);
			if (currentSong == songState.id && !IsStoppedOrStopping(songState.state)) {
				setType(AudioMsgId::Type::Song);
			}
		}
	});

	instance()->updatedNotifier(
	) | rpl::start_with_next([=](const TrackState &state) {
		handleSongUpdate(state);
	}, lifetime());

	setType(AudioMsgId::Type::Song);
	_playPause->finishTransform();
}

void Widget::updateVolumeToggleIcon() {
	auto icon = []() -> const style::icon * {
		auto volume = Core::App().settings().songVolume();
		if (volume > 0) {
			if (volume < 1 / 3.) {
				return &st::mediaPlayerVolumeIcon1;
			} else if (volume < 2 / 3.) {
				return &st::mediaPlayerVolumeIcon2;
			}
			return &st::mediaPlayerVolumeIcon3;
		}
		return nullptr;
	};
	_volumeToggle->setIconOverride(icon());
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

void Widget::showShadow() {
	_shadow->show();
	_playbackSlider->setVisible(_type == AudioMsgId::Type::Song);
}

void Widget::hideShadow() {
	_shadow->hide();
	_playbackSlider->hide();
}

QPoint Widget::getPositionForVolumeWidget() const {
	auto x = _volumeToggle->x();
	x += (_volumeToggle->width() - st::mediaPlayerVolumeSize.width()) / 2;
	if (rtl()) x = width() - x - st::mediaPlayerVolumeSize.width();
	return QPoint(x, height());
}

void Widget::volumeWidgetCreated(VolumeWidget *widget) {
	_volumeToggle->installEventFilter(widget);
}

Widget::~Widget() = default;

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
}

void Widget::updateControlsGeometry() {
	auto right = st::mediaPlayerCloseRight;
	_close->moveToRight(right, st::mediaPlayerPlayTop); right += _close->width();
	if (hasPlaybackSpeedControl()) {
		_playbackSpeed->moveToRight(right, st::mediaPlayerPlayTop); right += _playbackSpeed->width();
	}
	_repeatTrack->moveToRight(right, st::mediaPlayerPlayTop); right += _repeatTrack->width();
	_volumeToggle->moveToRight(right, st::mediaPlayerPlayTop); right += _volumeToggle->width();

	updatePlayPrevNextPositions();

	_playbackSlider->setGeometry(0, height() - st::mediaPlayerPlayback.fullWidth, width(), st::mediaPlayerPlayback.fullWidth);
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto fill = e->rect().intersected(QRect(0, 0, width(), st::mediaPlayerHeight));
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::mediaPlayerBg);
	}
}

void Widget::leaveEventHook(QEvent *e) {
	updateOverLabelsState(false);
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
	auto left = getLabelsLeft();
	auto right = getLabelsRight();
	auto labels = myrtlrect(left, 0, width() - right - left, height() - st::mediaPlayerPlayback.fullWidth);
	auto over = labels.contains(pos);
	updateOverLabelsState(over);
}

void Widget::updateOverLabelsState(bool over) {
	_labelsOver = over;
	auto pressShowsItem = _labelsOver && (_type == AudioMsgId::Type::Voice);
	setCursor(pressShowsItem ? style::cur_pointer : style::cur_default);
	auto showPlaylist = over && (_type == AudioMsgId::Type::Song);
	instance()->playerWidgetOver().notify(showPlaylist, true);
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

int Widget::getLabelsLeft() const {
	auto result = st::mediaPlayerPlayLeft + _playPause->width();
	if (_previousTrack) {
		result += _previousTrack->width() + st::mediaPlayerPlaySkip + _nextTrack->width() + st::mediaPlayerPlaySkip;
	}
	result += st::mediaPlayerPadding;
	return result;
}

int Widget::getLabelsRight() const {
	auto result = st::mediaPlayerCloseRight + _close->width();
	if (_type == AudioMsgId::Type::Song) {
		result += _repeatTrack->width() + _volumeToggle->width();
	}
	if (hasPlaybackSpeedControl()) {
		result += _playbackSpeed->width();
	}
	result += st::mediaPlayerPadding;
	return result;
}

void Widget::updateLabelsGeometry() {
	auto left = getLabelsLeft();
	auto right = getLabelsRight();

	auto widthForName = width() - left - right;
	widthForName -= _timeLabel->width() + 2 * st::normalFont->spacew;
	_nameLabel->resizeToWidth(widthForName);

	_nameLabel->moveToLeft(left, st::mediaPlayerNameTop - st::mediaPlayerName.style.font->ascent);
	_timeLabel->moveToRight(right, st::mediaPlayerNameTop - st::mediaPlayerTime.font->ascent);
}

void Widget::updateRepeatTrackIcon() {
	auto repeating = instance()->repeatEnabled(AudioMsgId::Type::Song);
	_repeatTrack->setIconOverride(repeating ? nullptr : &st::mediaPlayerRepeatDisabledIcon, repeating ? nullptr : &st::mediaPlayerRepeatDisabledIconOver);
	_repeatTrack->setRippleColorOverride(repeating ? nullptr : &st::mediaPlayerRepeatDisabledRippleBg);
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
	_repeatTrack->setVisible(_type == AudioMsgId::Type::Song);
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
	auto buttonState = [audio = state.id.audio(), showPause] {
		if (audio->loading()) {
			return ButtonState::Cancel;
		} else if (showPause) {
			return ButtonState::Pause;
		}
		return ButtonState::Play;
	};
	_playPause->setState(buttonState());

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
		_previousTrack->setCursor(previousEnabled ? style::cur_pointer : style::cur_default);
		_nextTrack->setIconOverride(nextEnabled ? nullptr : &st::mediaPlayerNextDisabledIcon);
		_nextTrack->setRippleColorOverride(nextEnabled ? nullptr : &st::mediaPlayerBg);
		_nextTrack->setCursor(nextEnabled ? style::cur_pointer : style::cur_default);
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
