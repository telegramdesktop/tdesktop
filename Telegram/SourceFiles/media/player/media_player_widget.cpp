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
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/menu/menu_action.h"
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
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "storage/storage_account.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "styles/style_media_player.h"
#include "styles/style_media_view.h"
#include "styles/style_chat.h" // expandedMenuSeparator.

namespace Media {
namespace Player {

Widget::Widget(
	QWidget *parent,
	not_null<Ui::RpWidget*> dropdownsParent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _orderMenuParent(dropdownsParent)
, _nameLabel(this, st::mediaPlayerName)
, _rightControls(this, object_ptr<Ui::RpWidget>(this))
, _timeLabel(rightControls(), st::mediaPlayerTime)
, _playPause(this, st::mediaPlayerPlayButton)
, _volumeToggle(rightControls(), st::mediaPlayerVolumeToggle)
, _repeatToggle(rightControls(), st::mediaPlayerRepeatButton)
, _orderToggle(rightControls(), st::mediaPlayerOrderButton)
, _speedToggle(rightControls(), st::mediaPlayerSpeedButton)
, _close(this, st::mediaPlayerClose)
, _shadow(this)
, _playbackSlider(this, st::mediaPlayerPlayback)
, _volume(std::in_place, dropdownsParent.get())
, _playbackProgress(std::make_unique<View::PlaybackProgress>())
, _orderController(
	std::make_unique<OrderController>(
		_orderToggle.data(),
		dropdownsParent,
		[=](bool over) { markOver(over); },
		Core::App().settings().playerOrderModeValue(),
		[=](OrderMode value) { saveOrder(value); }))
, _speedController(
	std::make_unique<SpeedController>(
		_speedToggle.data(),
		dropdownsParent,
		[=](bool over) { markOver(over); },
		[=](bool lastNonDefault) { return speedLookup(lastNonDefault); },
		[=](float64 speed) { saveSpeed(speed); })) {
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

	PrepareVolumeDropdown(_volume.get(), controller, _volumeToggle->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Wheel);
	}) | rpl::map([=](not_null<QEvent*> e) {
		return not_null{ static_cast<QWheelEvent*>(e.get()) };
	}));
	_volumeToggle->installEventFilter(_volume.get());
	_volume->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Enter) {
			markOver(true);
		} else if (e->type() == QEvent::Leave) {
			markOver(false);
		}
	}, _volume->lifetime());

	hidePlaylistOn(_playPause);
	hidePlaylistOn(_close);
	hidePlaylistOn(_rightControls);

	setType(AudioMsgId::Type::Song);
}

void Widget::hidePlaylistOn(not_null<Ui::RpWidget*> widget) {
	widget->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::start_with_next([=] {
		updateOverLabelsState(false);
	}, widget->lifetime());
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
	_speedController->showBack();
	_orderController->showBack();
}

void Widget::updateDropdownsGeometry() {
	const auto dropdownWidth = st::mediaPlayerVolumeSize.width();
	const auto position = _volume->parentWidget()->mapFromGlobal(
		_volumeToggle->mapToGlobal(
			QPoint(
				(_volumeToggle->width() - dropdownWidth) / 2,
				height())));
	const auto playerMargins = _volume->getMargin();
	const auto shift = QPoint(playerMargins.left(), playerMargins.top());
	_volume->move(position - shift);

	_orderController->updateDropdownGeometry();
	_speedController->updateDropdownGeometry();
}

void Widget::hideShadowAndDropdowns() {
	_shadow->hide();
	_playbackSlider->hide();
	if (!_volume->isHidden()) {
		_volumeHidden = true;
		_volume->hide();
	}
	_speedController->hideTemporarily();
	_orderController->hideTemporarily();
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
		_speedToggle->moveToRight(right, 0); right += _speedToggle->width();
	}
	if (_type == AudioMsgId::Type::Song) {
		_repeatToggle->moveToRight(right, 0); right += _repeatToggle->width();
		_orderToggle->moveToRight(right, 0); right += _orderToggle->width();
	}
	_volumeToggle->moveToRight(right, 0); right += _volumeToggle->width();

	updateControlsWrapGeometry();

	updatePlayPrevNextPositions();

	_playbackSlider->setGeometry(
		0,
		height() - st::mediaPlayerPlayback.fullWidth,
		width(),
		st::mediaPlayerPlayback.fullWidth);

	updateDropdownsGeometry();
}

void Widget::updateControlsWrapGeometry() {
	const auto fade = st::mediaPlayerControlsFade.width();
	const auto controls = getTimeRight() + _timeLabel->width() + fade;
	rightControls()->resize(controls, _repeatToggle->height());
	_rightControls->move(
		width() - st::mediaPlayerCloseRight - _close->width() - controls,
		st::mediaPlayerPlayTop);
}

void Widget::updateControlsWrapVisibility() {
	_rightControls->toggle(
		_over || !_narrow,
		isHidden() ? anim::type::instant : anim::type::normal);
}

void Widget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto fill = e->rect().intersected(QRect(0, 0, width(), st::mediaPlayerHeight));
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::mediaPlayerBg);
	}
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
		InvokeQueued(this, [=] {
			updateControlsWrapVisibility();
		});
	} else {
		_wontBeOver = true;
		InvokeQueued(this, [=] {
			if (!_wontBeOver) {
				return;
			}
			_wontBeOver = false;
			_over = false;
			updateControlsWrapVisibility();
		});
		updateOverLabelsState(false);
	}
}

void Widget::saveOrder(OrderMode mode) {
	Core::App().settings().setPlayerOrderMode(mode);
	Core::App().saveSettingsDelayed();
}

float64 Widget::speedLookup(bool lastNonDefault) const {
	return Core::App().settings().voicePlaybackSpeed(lastNonDefault);
}

void Widget::saveSpeed(float64 speed) {
	Core::App().settings().setVoicePlaybackSpeed(speed);
	Core::App().saveSettingsDelayed();
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
		if ((_type == AudioMsgId::Type::Voice)
				|| _lastSongFromAnotherSession) {
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
	const auto pressShowsItem = _labelsOver
		&& ((_type == AudioMsgId::Type::Voice)
			|| _lastSongFromAnotherSession);
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
	result += _volumeToggle->width();
	if (_type == AudioMsgId::Type::Song) {
		result += _repeatToggle->width()
			+ _orderToggle->width();
	}
	if (hasPlaybackSpeedControl()) {
		result += _speedToggle->width();
	}
	result += st::mediaPlayerPadding;
	return result;
}

void Widget::updateLabelsGeometry() {
	const auto left = getNameLeft();
	const auto widthForName = width()
		- left
		- getNameRight();
	_nameLabel->resizeToNaturalWidth(widthForName);
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
	_orderToggle->setVisible(_type == AudioMsgId::Type::Song);
	_speedToggle->setVisible(hasPlaybackSpeedControl());
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
		display = (document->duration() * frequency) / 1000;
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
	_lastSongFromAnotherSession = document
		&& (document->session().uniqueId()
			!= _controller->session().uniqueId());
	if (!current
		|| !document
		|| ((_lastSongId.audio() == document)
			&& (_lastSongId.contextId() == current.contextId()))) {
		return;
	}
	_lastSongId = current;

	auto textWithEntities = TextWithEntities();
	if (document->isVoiceMessage() || document->isVideoMessage()) {
		if (const auto item = document->owner().message(current.contextId())) {
			const auto name = (!item->out() || item->isPost())
				? item->fromOriginal()->name()
				: tr::lng_from_you(tr::now);
			const auto date = [item] {
				const auto parsed = ItemDateTime(item);
				const auto date = parsed.date();
				const auto time = QLocale().toString(parsed.time(), QLocale::ShortFormat);
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
	updateLabelsGeometry();
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
			instance()->previous(_type);
		});
		_nextTrack.create(this, st::mediaPlayerNextButton);
		_nextTrack->show();
		_nextTrack->setClickedCallback([=]() {
			instance()->next(_type);
		});
		hidePlaylistOn(_previousTrack);
		hidePlaylistOn(_nextTrack);
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
