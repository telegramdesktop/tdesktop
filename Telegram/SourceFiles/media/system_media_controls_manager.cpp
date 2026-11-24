/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/system_media_controls_manager.h"

#include "base/platform/base_platform_system_media_controls.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/version.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "media/system_media_controls_video.h"
#include "ui/text/format_song_document_name.h"

namespace Media {
namespace {

constexpr auto kSkipMs = crl::time(15 * 1000);

[[nodiscard]] auto RepeatModeToLoopStatus(Media::RepeatMode mode) {
	using Mode = Media::RepeatMode;
	using Status = base::Platform::SystemMediaControls::LoopStatus;
	switch (mode) {
	case Mode::None: return Status::None;
	case Mode::One: return Status::Track;
	case Mode::All: return Status::Playlist;
	}
	Unexpected("RepeatModeToLoopStatus in SystemMediaControlsManager");
}

void SetPlaybackRateRange(
		not_null<base::Platform::SystemMediaControls*> controls,
		bool controllable) {
	controls->setPlaybackRateRange(
		controllable ? kSpeedMin : 1.,
		controllable ? kSpeedMax : 1.);
}

[[nodiscard]] std::optional<AudioMsgId::Type> ActivePlayerType() {
	using namespace Media::Player;
	const auto player = instance();
	using Type = AudioMsgId::Type;
	for (const auto type : { Type::Voice, Type::Song }) {
		if (player->current(type)
			&& !IsStoppedOrStopping(player->getState(type).state)) {
			return type;
		}
	}
	return std::nullopt;
}

} // namespace

bool SystemMediaControlsManager::Supported() {
	return base::Platform::SystemMediaControls::Supported();
}

float64 SystemMediaControlsManager::lookupPlaybackRate() const {
	return (_streamed && _streamed->valid()) ? _streamed->speed() : 1.;
}

void SystemMediaControlsManager::applyAudioPlaybackSpeed(float64 speed) {
	const auto mediaPlayer = Media::Player::instance();
	const auto current = mediaPlayer->current(mediaPlayer->getActiveType());
	if (!current.changeablePlaybackSpeed()
		|| !Media::Audio::SupportsSpeedControl()) {
		return;
	}
	const auto document = current.audio();
	const auto music = document
		&& !document->isVoiceMessage()
		&& !document->isVideoMessage();
	auto &settings = Core::App().settings();
	const auto saved = music
		? settings.audioPlaybackSpeed()
		: settings.voicePlaybackSpeed();
	// a speed boost overrides the saved speed while held
	if (lookupPlaybackRate() != saved) {
		return;
	} else if (music) {
		settings.setAudioPlaybackSpeed(speed);
	} else {
		settings.setVoicePlaybackSpeed(speed);
	}
	Core::App().saveSettingsDelayed();
	mediaPlayer->updatePlaybackSpeed();
}

void SystemMediaControlsManager::applyPlayerTrack(AudioMsgId::Type audioType) {
	const auto mediaPlayer = Media::Player::instance();
	_lifetimeDownload.destroy();

	const auto current = mediaPlayer->current(audioType);
	if (!current) {
		return;
	}
	if ((_lastAudioMsgId.contextId() == current.contextId())
		&& (_lastAudioMsgId.audio() == current.audio())
		&& (_lastAudioMsgId.type() == current.type())) {
		return;
	}
	const auto document = current.audio();

	const auto &[title, performer] = (audioType
			== AudioMsgId::Type::Voice)
		? Ui::Text::FormatVoiceName(
			document,
			current.contextId()).composedName()
		: Ui::Text::FormatSongNameFor(document).composedName();
	_controls->setArtist(performer);
	_controls->setTitle(title);

	if (_controls->seekingSupported()) {
		const auto state = mediaPlayer->getState(audioType);
		_controls->setDuration(state.length);
		// macOS NowPlaying and Linux MPRIS update the track position
		// according to the rate property
		// while the playback status is "playing",
		// so we should change the track position only when
		// the track is changed
		// or when the position is changed by the user.
		_controls->setPosition(state.position);

		_streamed = std::make_unique<Media::Streaming::Instance>(
			document,
			current.contextId(),
			nullptr);
		SetPlaybackRateRange(
			_controls.get(),
			current.changeablePlaybackSpeed()
				&& Media::Audio::SupportsSpeedControl());
		_controls->setPlaybackRate(lookupPlaybackRate());
	}

	// Setting a thumbnail can take a long time,
	// so we need to update the display before that.
	_controls->updateDisplay();

	if (audioType == AudioMsgId::Type::Voice) {
		if (const auto item = document->owner().message(
				current.contextId())) {
			constexpr auto kUserpicSize = 50;
			const auto forwarded = item->Get<HistoryMessageForwarded>();
			const auto peer = (forwarded && forwarded->originalSender)
				? forwarded->originalSender
				: (!item->out() || item->isPost())
				? item->fromOriginal().get()
				: item->history()->peer.get();
			const auto userpic = PeerData::GenerateUserpicImage(
				peer,
				_cachedUserpicView,
				kUserpicSize,
				0);
			if (!userpic.isNull()) {
				_controls->setThumbnail(userpic);
			} else {
				peer->session().downloaderTaskFinished(
				) | rpl::on_next([=] {
					const auto userpic = PeerData::GenerateUserpicImage(
						peer,
						_cachedUserpicView,
						kUserpicSize,
						0);
					if (!userpic.isNull()) {
						_controls->setThumbnail(userpic);
						_lifetimeDownload.destroy();
					}
				}, _lifetimeDownload);
				_controls->clearThumbnail();
			}
		} else {
			_controls->clearThumbnail();
		}
	} else if (document && document->isSongWithCover()) {
		const auto view = document->createMediaView();
		view->thumbnailWanted(current.contextId());
		_cachedMediaView = view;
		if (const auto imagePtr = view->thumbnail()) {
			_controls->setThumbnail(imagePtr->original());
		} else {
			document->session().downloaderTaskFinished(
			) | rpl::on_next([=] {
				if (const auto imagePtr = view->thumbnail()) {
					_controls->setThumbnail(imagePtr->original());
					_lifetimeDownload.destroy();
				}
			}, _lifetimeDownload);
			_controls->clearThumbnail();
		}
	} else {
		_controls->clearThumbnail();
	}

	_lastAudioMsgId = current;
}

void SystemMediaControlsManager::syncPlayerStateToControls() {
	using PlaybackStatus
		= base::Platform::SystemMediaControls::PlaybackStatus;
	using namespace Media::Player;
	const auto mediaPlayer = Media::Player::instance();
	const auto type = ActivePlayerType();
	if (!type) {
		_cachedMediaView = nullptr;
		_streamed = nullptr;
		_controls->setEnabled(false);
		_controls->clearMetadata();
		return;
	}
	_controls->setEnabled(true);
	_controls->setIsPlayPauseEnabled(true);
	_controls->setIsStopEnabled(true);
	_controls->setIsSeekEnabled(true);
	_controls->setIsNextEnabled(mediaPlayer->nextAvailable(*type));
	_controls->setIsPreviousEnabled(mediaPlayer->previousAvailable(*type));
	syncLoopAndShuffleToControls();
	if (_controls->volumeSupported()) {
		_controls->setVolume(Core::App().settings().songVolume());
	}
	const auto state = mediaPlayer->getState(*type);
	_controls->setPlaybackStatus(IsStoppedOrStopping(state.state)
		? PlaybackStatus::Stopped
		: IsPausedOrPausing(state.state)
		? PlaybackStatus::Paused
		: PlaybackStatus::Playing);
	_lastAudioMsgId = AudioMsgId();
	applyPlayerTrack(*type);
	_controls->updateDisplay();
}

void SystemMediaControlsManager::syncLoopAndShuffleToControls() {
	const auto &settings = Core::App().settings();
	_controls->setLoopStatus(
		RepeatModeToLoopStatus(settings.playerRepeatMode()));
	_controls->setShuffle(settings.playerOrderMode() == OrderMode::Shuffle);
}

SystemMediaControlsManager::SystemMediaControlsManager()
: _controls(std::make_unique<base::Platform::SystemMediaControls>()) {

	using PlaybackStatus
		= base::Platform::SystemMediaControls::PlaybackStatus;
	using Command = base::Platform::SystemMediaControls::Command;

	_controls->setApplicationName(AppName.utf16());
	const auto inited = _controls->init();
	if (!inited) {
		LOG(("SystemMediaControlsManager failed to init."));
		return;
	}
	using TrackState = Media::Player::TrackState;
	const auto mediaPlayer = Media::Player::instance();

	auto trackFilter = rpl::filter([=](const TrackState &state) {
		const auto type = state.id.type();
		return (type == AudioMsgId::Type::Song)
			|| (type == AudioMsgId::Type::Voice);
	});

	mediaPlayer->updatedNotifier(
	) | trackFilter | rpl::map([=](const TrackState &state) {
		using namespace Media::Player;
		if (_streamed) {
			const auto &player = _streamed->player();
			if (player.buffering() || !player.playing()) {
				return PlaybackStatus::Paused;
			}
		}
		if (IsStoppedOrStopping(state.state)) {
			return PlaybackStatus::Stopped;
		} else if (IsPausedOrPausing(state.state)) {
			return PlaybackStatus::Paused;
		}
		return PlaybackStatus::Playing;
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](PlaybackStatus status) {
		if (_videoDelegate) {
			return;
		}
		_controls->setPlaybackStatus(status);
	}, _lifetime);

	mediaPlayer->updatedNotifier(
	) | trackFilter | rpl::map([=] {
		return lookupPlaybackRate();
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](float64 rate) {
		if (_videoDelegate) {
			return;
		}
		_controls->setPlaybackRate(rate);
	}, _lifetime);

	rpl::merge(
		mediaPlayer->stops(AudioMsgId::Type::Song) | rpl::map_to(false),
		mediaPlayer->startsPlay(AudioMsgId::Type::Song) | rpl::map_to(true),
		mediaPlayer->stops(AudioMsgId::Type::Voice) | rpl::map_to(false),
		mediaPlayer->startsPlay(AudioMsgId::Type::Voice) | rpl::map_to(true)
	) | rpl::distinct_until_changed() | rpl::on_next([=](bool audio) {
		if (_videoDelegate) {
			return;
		}
		const auto type = mediaPlayer->getActiveType();
		_controls->setEnabled(audio);
		if (audio) {
			_controls->setIsNextEnabled(mediaPlayer->nextAvailable(type));
			_controls->setIsPreviousEnabled(
				mediaPlayer->previousAvailable(type));
			_controls->setIsPlayPauseEnabled(true);
			_controls->setIsStopEnabled(true);
			_controls->setIsSeekEnabled(true);
			_controls->setPlaybackStatus(PlaybackStatus::Playing);
			_controls->updateDisplay();
		} else {
			_cachedMediaView = nullptr;
			_streamed = nullptr;
			_controls->clearMetadata();
		}
		_lifetimeDownload.destroy();
	}, _lifetime);

	// _streamed and _cachedMediaView hold session-owned objects (the
	// big file cache Database, the mtproto loader, DocumentData), so
	// they must be dropped on every stop, even while a video delegate
	// is active or when distinct_until_changed() swallows the event:
	// on logout the player calls stopAndClear() while the session is
	// still alive, and destroying them later would touch freed memory.
	rpl::merge(
		mediaPlayer->stops(AudioMsgId::Type::Song),
		mediaPlayer->stops(AudioMsgId::Type::Voice)
	) | rpl::on_next([=] {
		_cachedMediaView = nullptr;
		_streamed = nullptr;
	}, _lifetime);

	auto trackChanged = mediaPlayer->trackChanged(
	) | rpl::filter([=](AudioMsgId::Type audioType) {
		return (audioType == AudioMsgId::Type::Song)
			|| (audioType == AudioMsgId::Type::Voice);
	});

	auto unlocked = Core::App().passcodeLockChanges(
	) | rpl::filter([=](bool locked) {
		return !locked && ActivePlayerType().has_value();
	}) | rpl::map([=] {
		return *ActivePlayerType();
	}) | rpl::before_next([=] {
		if (_videoDelegate) {
			return;
		}
		_controls->setEnabled(true);
		_controls->updateDisplay();
	});

	rpl::merge(
		std::move(trackChanged),
		std::move(unlocked)
	) | rpl::on_next([=](AudioMsgId::Type audioType) {
		if (_videoDelegate) {
			return;
		}
		applyPlayerTrack(audioType);
	}, _lifetime);

	rpl::merge(
		mediaPlayer->playlistChanges(AudioMsgId::Type::Song)
			| rpl::map_to(AudioMsgId::Type::Song),
		mediaPlayer->playlistChanges(AudioMsgId::Type::Voice)
			| rpl::map_to(AudioMsgId::Type::Voice)
	) | rpl::on_next([=](AudioMsgId::Type type) {
		if (_videoDelegate) {
			return;
		}
		_controls->setIsNextEnabled(mediaPlayer->nextAvailable(type));
		_controls->setIsPreviousEnabled(mediaPlayer->previousAvailable(type));
	}, _lifetime);

	using Media::RepeatMode;
	using Media::OrderMode;

	Core::App().settings().playerRepeatModeValue(
	) | rpl::on_next([=](RepeatMode mode) {
		if (_videoDelegate) {
			return;
		}
		_controls->setLoopStatus(RepeatModeToLoopStatus(mode));
	}, _lifetime);

	Core::App().settings().playerOrderModeValue(
	) | rpl::on_next([=](OrderMode mode) {
		if (mode != OrderMode::Shuffle) {
			_lastOrderMode = mode;
		}
		if (_videoDelegate) {
			return;
		}
		_controls->setShuffle(mode == OrderMode::Shuffle);
	}, _lifetime);

	_controls->commandRequests(
	) | rpl::on_next([=](Command command) {
		if (_videoDelegate) {
			switch (command) {
			case Command::PlayPause: _videoDelegate->smtcPlayPause(); break;
			case Command::Play: _videoDelegate->smtcPlay(); break;
			case Command::Pause: _videoDelegate->smtcPause(); break;
			case Command::Stop: _videoDelegate->smtcStop(); break;
			case Command::Next: _videoDelegate->smtcNext(); break;
			case Command::Previous: _videoDelegate->smtcPrevious(); break;
			case Command::Raise: Core::App().activate(); break;
			case Command::Quit: _videoDelegate->smtcStop(); break;
			case Command::SkipForward:
			case Command::SkipBackward: {
				if (_videoState.seekable && _videoState.duration > 0) {
					const auto delta = (command == Command::SkipForward)
						? kSkipMs
						: -kSkipMs;
					const auto position = std::clamp(
						crl::time(_videoState.position + delta),
						crl::time(0),
						_videoState.duration);
					_videoDelegate->smtcSeek(position);
				}
				break;
			}
			default: break;
			}
			return;
		}
		const auto type = mediaPlayer->getActiveType();
		switch (command) {
		case Command::PlayPause: mediaPlayer->playPause(type); break;
		case Command::Play: mediaPlayer->play(type); break;
		case Command::Pause: mediaPlayer->pause(type); break;
		case Command::Next: mediaPlayer->next(type); break;
		case Command::Previous: mediaPlayer->previous(type); break;
		case Command::Stop: mediaPlayer->stop(type); break;
		case Command::Raise: Core::App().activate(); break;
		case Command::LoopNone: {
			Core::App().settings().setPlayerRepeatMode(RepeatMode::None);
			Core::App().saveSettingsDelayed();
			break;
		}
		case Command::LoopTrack: {
			Core::App().settings().setPlayerRepeatMode(RepeatMode::One);
			Core::App().saveSettingsDelayed();
			break;
		}
		case Command::LoopPlaylist: {
			Core::App().settings().setPlayerRepeatMode(RepeatMode::All);
			Core::App().saveSettingsDelayed();
			break;
		}
		case Command::Shuffle: {
			const auto current = Core::App().settings().playerOrderMode();
			Core::App().settings().setPlayerOrderMode((current == OrderMode::Shuffle)
				? _lastOrderMode
				: OrderMode::Shuffle);
			Core::App().saveSettingsDelayed();
			break;
		}
		case Command::Quit: {
			Media::Player::instance()->stopAndClose();
			break;
		}
		case Command::SkipForward:
		case Command::SkipBackward: {
			const auto state = mediaPlayer->getState(type);
			if (state.length > 0) {
				const auto delta = (command == Command::SkipForward)
					? kSkipMs
					: -kSkipMs;
				const auto position = std::clamp(
					crl::time(state.position + delta),
					crl::time(0),
					crl::time(state.length));
				mediaPlayer->finishSeeking(
					type,
					position / float64(state.length));
			}
			break;
		}
		}
	}, _lifetime);

	// revert what a client wrote if it wasn't applied
	_controls->commandRequests(
	) | rpl::on_next([=](Command) {
		syncLoopAndShuffleToControls();
	}, _lifetime);

	if (_controls->seekingSupported()) {
		rpl::merge(
			mediaPlayer->seekingChanges(AudioMsgId::Type::Song),
			mediaPlayer->seekingChanges(AudioMsgId::Type::Voice)
		) | rpl::filter([](Media::Player::Instance::Seeking seeking) {
			return (seeking == Media::Player::Instance::Seeking::Finish);
		}) | rpl::map([=] {
			const auto type = mediaPlayer->getActiveType();
			return mediaPlayer->getState(type).position;
		}) | rpl::on_next([=](int position) {
			if (_videoDelegate) {
				return;
			}
			_controls->setPosition(position);
			_controls->updateDisplay();
		}, _lifetime);

		_controls->seekRequests(
		) | rpl::on_next([=](float64 progress) {
			if (_videoDelegate) {
				if (_videoState.seekable) {
					_videoDelegate->smtcSeek(
						crl::time(progress * _videoState.duration));
				}
				return;
			}
			const auto type = mediaPlayer->getActiveType();
			mediaPlayer->finishSeeking(type, progress);
		}, _lifetime);

		_controls->updatePositionRequests(
		) | rpl::on_next([=] {
			if (_videoDelegate) {
				_controls->setPosition(_videoState.position);
				return;
			}
			const auto type = mediaPlayer->getActiveType();
			_controls->setPosition(mediaPlayer->getState(type).position);
		}, _lifetime);

		_controls->playbackRateChangeRequests(
		) | rpl::on_next([=](float64 rate) {
			const auto actual = [=] {
				return _videoDelegate
					? _videoState.speed
					: lookupPlaybackRate();
			};
			if (rate == actual()) {
				return;
			} else if (!std::isfinite(rate) || rate <= 0.) {
			} else if (!_videoDelegate) {
				applyAudioPlaybackSpeed(
					std::clamp(rate, kSpeedMin, kSpeedMax));
			} else if (_videoState.speedControllable) {
				_videoDelegate->smtcSetSpeed(
					std::clamp(rate, kSpeedMin, kSpeedMax));
			}
			// the value written by a client stays otherwise
			_controls->setPlaybackRate(actual());
		}, _lifetime);
	}

	Core::App().passcodeLockValue(
	) | rpl::filter([=](bool locked) {
		return locked && Core::App().maybePrimarySession();
	}) | rpl::on_next([=] {
		_controls->setEnabled(false);
	}, _lifetime);

	if (_controls->volumeSupported()) {
		rpl::single(
			Core::App().settings().songVolume()
		) | rpl::then(
			Core::App().settings().songVolumeChanges()
		) | rpl::on_next([=](float64 volume) {
			if (_videoDelegate) {
				return;
			}
			_controls->setVolume(volume);
		}, _lifetime);

		Core::App().settings().videoVolumeChanges(
		) | rpl::on_next([=](float64 volume) {
			if (_videoDelegate) {
				_controls->setVolume(volume);
			}
		}, _lifetime);

		_controls->volumeChangeRequests(
		) | rpl::on_next([=](float64 volume) {
			const auto current = [=] {
				return _videoDelegate
					? Core::App().settings().videoVolume()
					: Core::App().settings().songVolume();
			};
			const auto clamped = std::isnan(volume)
				? current()
				: std::clamp(volume, 0., 1.);
			if (clamped == current()) {
			} else if (_videoDelegate) {
				_videoDelegate->smtcSetVolume(clamped);
			} else {
				Player::mixer()->setSongVolume(clamped);
				if (clamped > 0) {
					Core::App().settings().setRememberedSongVolume(clamped);
				}
				Core::App().settings().setSongVolume(clamped);
				Core::App().saveSettingsDelayed();
			}
			// volume changes are silent if the value is the same
			_controls->setVolume(current());
		}, _lifetime);
	}

	_inited = true;
}

void SystemMediaControlsManager::videoStart(
		not_null<SystemMediaControlsVideoDelegate*> delegate,
		VideoState state) {
	if (!_inited) {
		return;
	}
	using PlaybackStatus
		= base::Platform::SystemMediaControls::PlaybackStatus;
	_videoDelegate = delegate;
	_videoState = state;
	_lifetimeDownload.destroy();
	_controls->setEnabled(true);
	_controls->setIsPlayPauseEnabled(true);
	_controls->setIsStopEnabled(true);
	_controls->setIsSeekEnabled(state.seekable);
	_controls->setIsNextEnabled(state.nextAvailable);
	_controls->setIsPreviousEnabled(state.previousAvailable);
	_controls->setTitle(state.title);
	_controls->setArtist(state.artist);
	_controls->clearThumbnail();
	if (_controls->seekingSupported()) {
		_controls->setDuration(state.duration);
		_controls->setPosition(state.position);
		SetPlaybackRateRange(_controls.get(), state.speedControllable);
		_controls->setPlaybackRate(state.speed);
	}
	if (_controls->volumeSupported()) {
		_controls->setVolume(Core::App().settings().videoVolume());
	}
	_controls->setPlaybackStatus(state.playing
		? PlaybackStatus::Playing
		: PlaybackStatus::Paused);
	_controls->updateDisplay();
}

void SystemMediaControlsManager::videoUpdate(VideoState state) {
	if (!_inited || !_videoDelegate) {
		return;
	}
	using PlaybackStatus
		= base::Platform::SystemMediaControls::PlaybackStatus;
	// position requests from inside the calls below read _videoState
	const auto was = std::exchange(_videoState, state);
	auto display = false;
	if (state.title != was.title) {
		_controls->setTitle(state.title);
		display = true;
	}
	if (state.artist != was.artist) {
		_controls->setArtist(state.artist);
		display = true;
	}
	if (state.nextAvailable != was.nextAvailable) {
		_controls->setIsNextEnabled(state.nextAvailable);
	}
	if (state.previousAvailable != was.previousAvailable) {
		_controls->setIsPreviousEnabled(state.previousAvailable);
	}
	if (state.seekable != was.seekable) {
		_controls->setIsSeekEnabled(state.seekable);
	}
	const auto statusChanged = (state.playing != was.playing);
	if (_controls->seekingSupported()) {
		if (state.duration != was.duration) {
			_controls->setDuration(state.duration);
		}
		if (state.speedControllable != was.speedControllable) {
			SetPlaybackRateRange(_controls.get(), state.speedControllable);
		}
		if (state.speed != was.speed) {
			_controls->setPlaybackRate(state.speed);
		}
	}
	if (statusChanged) {
		_controls->setPlaybackStatus(state.playing
			? PlaybackStatus::Playing
			: PlaybackStatus::Paused);
	}
	if (display) {
		_controls->updateDisplay();
	}
}

void SystemMediaControlsManager::videoSeeked() {
	if (!_inited || !_videoDelegate || !_controls->seekingSupported()) {
		return;
	}
	_controls->setPosition(_videoState.position);
	_controls->updateDisplay();
}

void SystemMediaControlsManager::videoSetThumbnail(const QImage &thumbnail) {
	if (!_inited || !_videoDelegate) {
		return;
	}
	if (thumbnail.isNull()) {
		_controls->clearThumbnail();
	} else {
		_controls->setThumbnail(thumbnail);
	}
	_controls->updateDisplay();
}

void SystemMediaControlsManager::videoFinish(
		not_null<SystemMediaControlsVideoDelegate*> delegate) {
	if (!_inited || (_videoDelegate != delegate)) {
		return;
	}
	_videoDelegate = nullptr;
	_videoState = VideoState();
	syncPlayerStateToControls();
}

SystemMediaControlsManager::~SystemMediaControlsManager() = default;

} // namespace Media
