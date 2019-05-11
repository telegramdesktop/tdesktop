/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_instance.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_audio_capture.h"
#include "media/streaming/media_streaming_player.h"
#include "media/streaming/media_streaming_loader.h"
#include "media/view/media_view_playback_progress.h"
#include "calls/calls_instance.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_media_types.h"
#include "data/data_file_origin.h"
#include "window/window_controller.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "mainwindow.h"
#include "auth_session.h"

namespace Media {
namespace Player {
namespace {

Instance *SingleInstance = nullptr;

constexpr auto kVoicePlaybackSpeedMultiplier = 1.7;

// Preload X message ids before and after current.
constexpr auto kIdsLimit = 32;

// Preload next messages if we went further from current than that.
constexpr auto kIdsPreloadAfter = 28;

} // namespace

void start(not_null<Audio::Instance*> instance) {
	Audio::Start(instance);
	Capture::Start();

	SingleInstance = new Instance();
}

void finish(not_null<Audio::Instance*> instance) {
	delete base::take(SingleInstance);

	Capture::Finish();
	Audio::Finish(instance);
}

struct Instance::Streamed {
	Streamed(
		AudioMsgId id,
		not_null<::Data::Session*> owner,
		std::unique_ptr<Streaming::Loader> loader);

	AudioMsgId id;
	Streaming::Player player;
	Streaming::Information info;
	View::PlaybackProgress progress;
};

Instance::Streamed::Streamed(
	AudioMsgId id,
	not_null<::Data::Session*> owner,
	std::unique_ptr<Streaming::Loader> loader)
: id(id)
, player(owner, std::move(loader)) {
}

Instance::Data::Data(AudioMsgId::Type type, SharedMediaType overview)
: type(type)
, overview(overview) {
}

Instance::Data::Data(Data &&other) = default;
Instance::Data &Instance::Data::operator=(Data &&other) = default;
Instance::Data::~Data() = default;

Instance::Instance()
: _songData(AudioMsgId::Type::Song, SharedMediaType::MusicFile)
, _voiceData(AudioMsgId::Type::Voice, SharedMediaType::RoundVoiceFile) {
	subscribe(Media::Player::Updated(), [this](const AudioMsgId &audioId) {
		handleSongUpdate(audioId);
	});

	// While we have one Media::Player::Instance for all authsessions we have to do this.
	const auto handleAuthSessionChange = [=] {
		if (AuthSession::Exists()) {
			subscribe(Auth().calls().currentCallChanged(), [=](Calls::Call *call) {
				if (call) {
					pauseOnCall(AudioMsgId::Type::Voice);
					pauseOnCall(AudioMsgId::Type::Song);
				} else {
					resumeOnCall(AudioMsgId::Type::Voice);
					resumeOnCall(AudioMsgId::Type::Song);
				}
			});
		}
	};
	subscribe(
		Core::App().authSessionChanged(),
		handleAuthSessionChange);
	handleAuthSessionChange();

	setupShortcuts();
}

Instance::~Instance() = default;

AudioMsgId::Type Instance::getActiveType() const {
	if (const auto data = getData(AudioMsgId::Type::Voice)) {
		if (data->current) {
			const auto state = getState(data->type);
			if (!IsStoppedOrStopping(state.state)) {
				return data->type;
			}
		}
	}
	return AudioMsgId::Type::Song;
}

void Instance::handleSongUpdate(const AudioMsgId &audioId) {
	emitUpdate(audioId.type(), [&](const AudioMsgId &playing) {
		return (audioId == playing);
	});
}

void Instance::setCurrent(const AudioMsgId &audioId) {
	if (const auto data = getData(audioId.type())) {
		if (data->current == audioId) {
			return;
		}
		const auto changed = [&](const AudioMsgId & check) {
			return (check.audio() != audioId.audio())
				|| (check.contextId() != audioId.contextId());
		};
		if (changed(data->current)
			&& data->streamed
			&& changed(data->streamed->id)) {
			clearStreamed(data);
		}
		data->current = audioId;
		data->isPlaying = false;

		const auto item = Auth().data().message(data->current.contextId());
		if (item) {
			data->history = item->history()->migrateToOrMe();
			data->migrated = data->history->migrateFrom();
		} else {
			data->history = nullptr;
			data->migrated = nullptr;
		}
		_trackChangedNotifier.notify(data->type, true);
		refreshPlaylist(data);
	}
}

void Instance::clearStreamed(not_null<Data*> data) {
	if (!data->streamed) {
		return;
	}
	data->streamed->player.stop();
	data->isPlaying = false;
	requestRoundVideoResize();
	emitUpdate(data->type);
	data->streamed = nullptr;
	App::wnd()->controller()->disableGifPauseReason(
		Window::GifPauseReason::RoundPlaying);
}

void Instance::refreshPlaylist(not_null<Data*> data) {
	if (!validPlaylist(data)) {
		validatePlaylist(data);
	}
	playlistUpdated(data);
}

void Instance::playlistUpdated(not_null<Data*> data) {
	if (data->playlistSlice) {
		const auto fullId = data->current.contextId();
		data->playlistIndex = data->playlistSlice->indexOf(fullId);
	} else {
		data->playlistIndex = std::nullopt;
	}
	data->playlistChanges.fire({});
}

bool Instance::validPlaylist(not_null<Data*> data) {
	if (const auto key = playlistKey(data)) {
		if (!data->playlistSlice) {
			return false;
		}
		using Key = SliceKey;
		const auto inSameDomain = [](const Key &a, const Key &b) {
			return (a.peerId == b.peerId)
				&& (a.migratedPeerId == b.migratedPeerId);
		};
		const auto countDistanceInData = [&](const Key &a, const Key &b) {
			return [&](const SparseIdsMergedSlice &data) {
				return inSameDomain(a, b)
					? data.distance(a, b)
					: std::optional<int>();
			};
		};

		if (key == data->playlistRequestedKey) {
			return true;
		} else if (!data->playlistSliceKey
			|| !data->playlistRequestedKey
			|| *data->playlistRequestedKey != *data->playlistSliceKey) {
			return false;
		}
		auto distance = data->playlistSlice
			| countDistanceInData(*key, *data->playlistRequestedKey)
			| func::abs;
		if (distance) {
			return (*distance < kIdsPreloadAfter);
		}
	}
	return !data->playlistSlice;
}

void Instance::validatePlaylist(not_null<Data*> data) {
	if (const auto key = playlistKey(data)) {
		data->playlistRequestedKey = key;
		SharedMediaMergedViewer(
			SharedMediaMergedKey(*key, data->overview),
			kIdsLimit,
			kIdsLimit
		) | rpl::start_with_next([=](SparseIdsMergedSlice &&update) {
			data->playlistSlice = std::move(update);
			data->playlistSliceKey = key;
			playlistUpdated(data);
		}, data->playlistLifetime);
	} else {
		data->playlistSlice = std::nullopt;
		data->playlistSliceKey = data->playlistRequestedKey = std::nullopt;
		playlistUpdated(data);
	}
}

auto Instance::playlistKey(not_null<Data*> data) const
-> std::optional<SliceKey> {
	const auto contextId = data->current.contextId();
	const auto history = data->history;
	if (!contextId || !history || !IsServerMsgId(contextId.msg)) {
		return {};
	}

	const auto universalId = (contextId.channel == history->channelId())
		? contextId.msg
		: (contextId.msg - ServerMaxMsgId);
	return SliceKey(
		data->history->peer->id,
		data->migrated ? data->migrated->peer->id : 0,
		universalId);
}

HistoryItem *Instance::itemByIndex(not_null<Data*> data, int index) {
	if (!data->playlistSlice
		|| index < 0
		|| index >= data->playlistSlice->size()) {
		return nullptr;
	}
	const auto fullId = (*data->playlistSlice)[index];
	return Auth().data().message(fullId);
}

bool Instance::moveInPlaylist(
		not_null<Data*> data,
		int delta,
		bool autonext) {
	if (!data->playlistIndex) {
		return false;
	}
	const auto newIndex = *data->playlistIndex + delta;
	if (const auto item = itemByIndex(data, newIndex)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				if (autonext) {
					_switchToNextNotifier.notify({
						data->current,
						item->fullId()
					});
				}
				if (document->isAudioFile()
					|| document->isVoiceMessage()
					|| document->isVideoMessage()) {
					play(AudioMsgId(document, item->fullId()));
				}
				return true;
			}
		}
	}
	return false;
}

bool Instance::previousAvailable(AudioMsgId::Type type) const {
	const auto data = getData(type);
	Assert(data != nullptr);
	return data->playlistIndex
		&& data->playlistSlice
		&& (*data->playlistIndex > 0);
}

bool Instance::nextAvailable(AudioMsgId::Type type) const {
	const auto data = getData(type);
	Assert(data != nullptr);
	return data->playlistIndex
		&& data->playlistSlice
		&& (*data->playlistIndex + 1 < data->playlistSlice->size());
}

rpl::producer<> Media::Player::Instance::playlistChanges(
		AudioMsgId::Type type) const {
	const auto data = getData(type);
	Assert(data != nullptr);
	return data->playlistChanges.events();
}

Instance *instance() {
	Expects(SingleInstance != nullptr);
	return SingleInstance;
}

void Instance::play(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		if (!data->streamed || IsStopped(getState(type).state)) {
			play(data->current);
		} else {
			if (data->streamed->player.active()) {
				data->streamed->player.resume();
			}
			emitUpdate(type);
		}
		data->resumeOnCallEnd = false;
	}
}

void Instance::play(const AudioMsgId &audioId) {
	const auto document = audioId.audio();
	if (!document) {
		return;
	}
	if (document->isAudioFile()
		|| document->isVoiceMessage()
		|| document->isVideoMessage()) {
		auto loader = document->createStreamingLoader(audioId.contextId());
		if (!loader) {
			return;
		}
		playStreamed(audioId, std::move(loader));
	}
	if (document->isVoiceMessage() || document->isVideoMessage()) {
		document->owner().markMediaRead(document);
	}
}

void Instance::playPause(const AudioMsgId &audioId) {
	const auto now = current(audioId.type());
	if (now.audio() == audioId.audio()
		&& now.contextId() == audioId.contextId()) {
		playPause(audioId.type());
	} else {
		play(audioId);
	}
}

void Instance::playStreamed(
		const AudioMsgId &audioId,
		std::unique_ptr<Streaming::Loader> loader) {
	Expects(audioId.audio() != nullptr);

	const auto data = getData(audioId.type());
	Assert(data != nullptr);

	clearStreamed(data);
	data->streamed = std::make_unique<Streamed>(
		audioId,
		&audioId.audio()->owner(),
		std::move(loader));

	data->streamed->player.updates(
	) | rpl::start_with_next_error([=](Streaming::Update &&update) {
		handleStreamingUpdate(data, std::move(update));
	}, [=](Streaming::Error && error) {
		handleStreamingError(data, std::move(error));
	}, data->streamed->player.lifetime());

	data->streamed->player.play(streamingOptions(audioId));

	emitUpdate(audioId.type());
}

Streaming::PlaybackOptions Instance::streamingOptions(
		const AudioMsgId &audioId,
		crl::time position) {
	const auto document = audioId.audio();
	auto result = Streaming::PlaybackOptions();
	result.mode = (document && document->isVideoMessage())
		? Streaming::Mode::Both
		: Streaming::Mode::Audio;
	result.speed = (document
		&& (document->isVoiceMessage() || document->isVideoMessage())
		&& Global::VoiceMsgPlaybackDoubled())
		? kVoicePlaybackSpeedMultiplier
		: 1.;
	result.audioId = audioId;
	result.position = position;
	return result;
}

void Instance::pause(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		if (data->streamed) {
			if (data->streamed->player.active()) {
				data->streamed->player.pause();
			}
			emitUpdate(type);
		}
	}
}

void Instance::stop(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		if (data->streamed) {
			clearStreamed(data);
		}
		data->resumeOnCallEnd = false;
	}
}

void Instance::playPause(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		if (!data->streamed) {
			play(data->current);
		} else {
			if (!data->streamed->player.active()) {
				data->streamed->player.play(
					streamingOptions(data->streamed->id));
			} else if (data->streamed->player.paused()) {
				data->streamed->player.resume();
			} else {
				data->streamed->player.pause();
			}
			emitUpdate(type);
		}
		data->resumeOnCallEnd = false;
	}
}

void Instance::pauseOnCall(AudioMsgId::Type type) {
	const auto state = getState(type);
	if (!state.id
		|| IsStopped(state.state)
		|| IsPaused(state.state)
		|| state.state == State::Pausing) {
		return;
	}
	pause(type);
	if (const auto data = getData(type)) {
		data->resumeOnCallEnd = true;
	}
}

void Instance::resumeOnCall(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		if (data->resumeOnCallEnd) {
			data->resumeOnCallEnd = false;
			play(type);
		}
	}
}

bool Instance::next(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		return moveInPlaylist(data, 1, false);
	}
	return false;
}

bool Instance::previous(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		return moveInPlaylist(data, -1, false);
	}
	return false;
}

void Instance::playPauseCancelClicked(AudioMsgId::Type type) {
	if (isSeeking(type)) {
		return;
	}

	const auto data = getData(type);
	if (!data) {
		return;
	}
	const auto state = getState(type);
	const auto stopped = IsStoppedOrStopping(state.state);
	const auto showPause = ShowPauseIcon(state.state);
	const auto audio = state.id.audio();
	if (audio && audio->loading() && !data->streamed) {
		audio->cancel();
	} else if (showPause) {
		pause(type);
	} else {
		play(type);
	}
}

void Instance::startSeeking(AudioMsgId::Type type) {
	if (auto data = getData(type)) {
		data->seeking = data->current;
	}
	pause(type);
	emitUpdate(type);
}

void Instance::finishSeeking(AudioMsgId::Type type, float64 progress) {
	if (const auto data = getData(type)) {
		if (data->streamed) {
			const auto duration = data->streamed->info.audio.state.duration;
			if (duration != kTimeUnknown) {
				const auto position = crl::time(std::round(
					std::clamp(progress, 0., 1.) * duration));
				data->streamed->player.play(streamingOptions(
					data->streamed->id,
					position));
				emitUpdate(type);
			}
		}
	}
	cancelSeeking(type);
}

void Instance::cancelSeeking(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		data->seeking = AudioMsgId();
	}
	emitUpdate(type);
}

void Instance::updateVoicePlaybackSpeed() {
	if (const auto data = getData(AudioMsgId::Type::Voice)) {
		if (const auto streamed = data->streamed.get()) {
			streamed->player.setSpeed(Global::VoiceMsgPlaybackDoubled()
				? kVoicePlaybackSpeedMultiplier
				: 1.);
		}
	}
}

void Instance::documentLoadProgress(DocumentData *document) {
	const auto type = document->isAudioFile()
		? AudioMsgId::Type::Song
		: AudioMsgId::Type::Voice;
	emitUpdate(type, [&](const AudioMsgId &audioId) {
		return (audioId.audio() == document);
	});
}

void Instance::emitUpdate(AudioMsgId::Type type) {
	emitUpdate(type, [](const AudioMsgId &playing) { return true; });
}

TrackState Instance::getState(AudioMsgId::Type type) const {
	if (const auto data = getData(type)) {
		if (data->streamed) {
			return data->streamed->player.prepareLegacyState();
		}
	}
	return TrackState();
}

Streaming::Player *Instance::roundVideoPlayer(HistoryItem *item) const {
	if (!item) {
		return nullptr;
	} else if (const auto data = getData(AudioMsgId::Type::Voice)) {
		if (const auto streamed = data->streamed.get()) {
			if (streamed->id.contextId() == item->fullId()) {
				const auto player = &streamed->player;
				if (player->ready() && !player->videoSize().isEmpty()) {
					return player;
				}
			}
		}
	}
	return nullptr;
}

View::PlaybackProgress *Instance::roundVideoPlayback(
		HistoryItem *item) const {
	return roundVideoPlayer(item)
		? &getData(AudioMsgId::Type::Voice)->streamed->progress
		: nullptr;
}

template <typename CheckCallback>
void Instance::emitUpdate(AudioMsgId::Type type, CheckCallback check) {
	if (const auto data = getData(type)) {
		const auto state = getState(type);
		if (!state.id || !check(state.id)) {
			return;
		}
		setCurrent(state.id);
		if (data->streamed && !data->streamed->info.video.size.isEmpty()) {
			data->streamed->progress.updateState(state);
		}
		_updatedNotifier.fire_copy({state});
		if (data->isPlaying && state.state == State::StoppedAtEnd) {
			if (data->repeatEnabled) {
				play(data->current);
			} else if (!moveInPlaylist(data, 1, true)) {
				_tracksFinishedNotifier.notify(type);
			}
		}
		data->isPlaying = !IsStopped(state.state);
	}
}

void Instance::handleLogout() {
	const auto reset = [&](AudioMsgId::Type type) {
		const auto data = getData(type);
		*data = Data(type, data->overview);
	};
	reset(AudioMsgId::Type::Voice);
	reset(AudioMsgId::Type::Song);
}

void Instance::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::MediaPlay) && request->handle([=] {
			play();
			return true;
		});
		request->check(Command::MediaPause) && request->handle([=] {
			pause();
			return true;
		});
		request->check(Command::MediaPlayPause) && request->handle([=] {
			playPause();
			return true;
		});
		request->check(Command::MediaStop) && request->handle([=] {
			stop();
			return true;
		});
		request->check(Command::MediaPrevious) && request->handle([=] {
			previous();
			return true;
		});
		request->check(Command::MediaNext) && request->handle([=] {
			next();
			return true;
		});
	}, _lifetime);
}

void Instance::handleStreamingUpdate(
		not_null<Data*> data,
		Streaming::Update &&update) {
	using namespace Streaming;

	update.data.match([&](Information &update) {
		data->streamed->info = std::move(update);
		if (!data->streamed->info.video.size.isEmpty()) {
			data->streamed->progress.setValueChangedCallback([=](
					float64,
					float64) {
				requestRoundVideoRepaint();
			});
			App::wnd()->controller()->enableGifPauseReason(
				Window::GifPauseReason::RoundPlaying);
			requestRoundVideoResize();
		}
		emitUpdate(data->type);
	}, [&](PreloadedVideo &update) {
		data->streamed->info.video.state.receivedTill = update.till;
		//emitUpdate(data->type, [](AudioMsgId) { return true; });
	}, [&](UpdateVideo &update) {
		data->streamed->info.video.state.position = update.position;
		emitUpdate(data->type);
	}, [&](PreloadedAudio &update) {
		data->streamed->info.audio.state.receivedTill = update.till;
		//emitUpdate(data->type, [](AudioMsgId) { return true; });
	}, [&](UpdateAudio &update) {
		data->streamed->info.audio.state.position = update.position;
		emitUpdate(data->type);
	}, [&](WaitingForData) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
		const auto finishTrack = [](Media::Streaming::TrackState &state) {
			state.position = state.receivedTill = state.duration;
		};
		finishTrack(data->streamed->info.audio.state);
		emitUpdate(data->type);
		if (data->streamed && data->streamed->player.finished()) {
			clearStreamed(data);
		}
	});
}

HistoryItem *Instance::roundVideoItem() const {
	const auto data = getData(AudioMsgId::Type::Voice);
	return (data->streamed
		&& !data->streamed->info.video.size.isEmpty())
		? Auth().data().message(data->streamed->id.contextId())
		: nullptr;

}

void Instance::requestRoundVideoResize() const {
	if (const auto item = roundVideoItem()) {
		Auth().data().requestItemResize(item);
	}
}

void Instance::requestRoundVideoRepaint() const {
	if (const auto item = roundVideoItem()) {
		Auth().data().requestItemRepaint(item);
	}
}

void Instance::handleStreamingError(
		not_null<Data*> data,
		Streaming::Error &&error) {
	Expects(data->streamed != nullptr);

	const auto document = data->streamed->id.audio();
	const auto contextId = data->streamed->id.contextId();
	if (error == Streaming::Error::NotStreamable) {
		document->setNotSupportsStreaming();
		DocumentSaveClickHandler::Save(
			(contextId ? contextId : ::Data::FileOrigin()),
			document);
	} else if (error == Streaming::Error::OpenFailed) {
		document->setInappPlaybackFailed();
		DocumentSaveClickHandler::Save(
			(contextId ? contextId : ::Data::FileOrigin()),
			document,
			DocumentSaveClickHandler::Mode::ToFile);
	}
	emitUpdate(data->type);
	if (data->streamed && data->streamed->player.failed()) {
		clearStreamed(data);
	}
}

} // namespace Player
} // namespace Media
