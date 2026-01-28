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
#include "data/data_document_media.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/history_item.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "ui/text/format_song_document_name.h"

namespace Media {
namespace {

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

} // namespace

bool SystemMediaControlsManager::Supported() {
	return base::Platform::SystemMediaControls::Supported();
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
		_controls->setPlaybackStatus(status);
	}, _lifetime);

	rpl::merge(
		mediaPlayer->stops(AudioMsgId::Type::Song) | rpl::map_to(false),
		mediaPlayer->startsPlay(AudioMsgId::Type::Song) | rpl::map_to(true),
		mediaPlayer->stops(AudioMsgId::Type::Voice) | rpl::map_to(false),
		mediaPlayer->startsPlay(AudioMsgId::Type::Voice) | rpl::map_to(true)
	) | rpl::distinct_until_changed() | rpl::on_next([=](bool audio) {
		const auto type = mediaPlayer->current(AudioMsgId::Type::Song)
			? AudioMsgId::Type::Song
			: AudioMsgId::Type::Voice;
		_controls->setEnabled(audio);
		if (audio) {
			_controls->setIsNextEnabled(mediaPlayer->nextAvailable(type));
			_controls->setIsPreviousEnabled(
				mediaPlayer->previousAvailable(type));
			_controls->setIsPlayPauseEnabled(true);
			_controls->setIsStopEnabled(true);
			_controls->setPlaybackStatus(PlaybackStatus::Playing);
			_controls->updateDisplay();
		} else {
			_cachedMediaView.clear();
			_streamed = nullptr;
			_controls->clearMetadata();
		}
		_lifetimeDownload.destroy();
	}, _lifetime);

	auto trackChanged = mediaPlayer->trackChanged(
	) | rpl::filter([=](AudioMsgId::Type audioType) {
		return (audioType == AudioMsgId::Type::Song)
			|| (audioType == AudioMsgId::Type::Voice);
	});

	auto unlocked = Core::App().passcodeLockChanges(
	) | rpl::filter([=](bool locked) {
		return !locked && (mediaPlayer->current(AudioMsgId::Type::Song)
			|| mediaPlayer->current(AudioMsgId::Type::Voice));
	}) | rpl::map([=]() -> AudioMsgId::Type {
		return mediaPlayer->current(AudioMsgId::Type::Song)
			? AudioMsgId::Type::Song
			: AudioMsgId::Type::Voice;
	}) | rpl::before_next([=] {
		_controls->setEnabled(true);
		_controls->updateDisplay();
	});

	rpl::merge(
		std::move(trackChanged),
		std::move(unlocked)
	) | rpl::on_next([=](AudioMsgId::Type audioType) {
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
			_cachedMediaView.push_back(view);
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
	}, _lifetime);

	rpl::merge(
		mediaPlayer->playlistChanges(AudioMsgId::Type::Song)
			| rpl::map_to(AudioMsgId::Type::Song),
		mediaPlayer->playlistChanges(AudioMsgId::Type::Voice)
			| rpl::map_to(AudioMsgId::Type::Voice)
	) | rpl::on_next([=](AudioMsgId::Type type) {
		_controls->setIsNextEnabled(mediaPlayer->nextAvailable(type));
		_controls->setIsPreviousEnabled(mediaPlayer->previousAvailable(type));
	}, _lifetime);

	using Media::RepeatMode;
	using Media::OrderMode;

	Core::App().settings().playerRepeatModeValue(
	) | rpl::on_next([=](RepeatMode mode) {
		_controls->setLoopStatus(RepeatModeToLoopStatus(mode));
	}, _lifetime);

	Core::App().settings().playerOrderModeValue(
	) | rpl::on_next([=](OrderMode mode) {
		if (mode != OrderMode::Shuffle) {
			_lastOrderMode = mode;
		}
		_controls->setShuffle(mode == OrderMode::Shuffle);
	}, _lifetime);

	_controls->commandRequests(
	) | rpl::on_next([=](Command command) {
		const auto type = mediaPlayer->current(AudioMsgId::Type::Song)
			? AudioMsgId::Type::Song
			: AudioMsgId::Type::Voice;
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
		}
	}, _lifetime);

	if (_controls->seekingSupported()) {
		rpl::merge(
			mediaPlayer->seekingChanges(AudioMsgId::Type::Song),
			mediaPlayer->seekingChanges(AudioMsgId::Type::Voice)
		) | rpl::filter([](Media::Player::Instance::Seeking seeking) {
			return (seeking == Media::Player::Instance::Seeking::Finish);
		}) | rpl::map([=] {
			const auto type = mediaPlayer->current(AudioMsgId::Type::Song)
				? AudioMsgId::Type::Song
				: AudioMsgId::Type::Voice;
			return mediaPlayer->getState(type).position;
		}) | rpl::distinct_until_changed(
		) | rpl::on_next([=](int position) {
			_controls->setPosition(position);
			_controls->updateDisplay();
		}, _lifetime);

		_controls->seekRequests(
		) | rpl::on_next([=](float64 progress) {
			const auto type = mediaPlayer->current(AudioMsgId::Type::Song)
				? AudioMsgId::Type::Song
				: AudioMsgId::Type::Voice;
			mediaPlayer->finishSeeking(type, progress);
		}, _lifetime);

		_controls->updatePositionRequests(
		) | rpl::on_next([=] {
			const auto type = mediaPlayer->current(AudioMsgId::Type::Song)
				? AudioMsgId::Type::Song
				: AudioMsgId::Type::Voice;
			_controls->setPosition(mediaPlayer->getState(type).position);
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
			_controls->setVolume(volume);
		}, _lifetime);

		_controls->volumeChangeRequests(
		) | rpl::on_next([](float64 volume) {
			Player::mixer()->setSongVolume(volume);
			if (volume > 0) {
				Core::App().settings().setRememberedSongVolume(volume);
			}
			Core::App().settings().setSongVolume(volume);
		}, _lifetime);
	}

}

SystemMediaControlsManager::~SystemMediaControlsManager() = default;

} // namespace Media
