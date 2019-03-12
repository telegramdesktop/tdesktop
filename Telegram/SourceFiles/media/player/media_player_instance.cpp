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
	const auto voiceData = getData(AudioMsgId::Type::Voice);
	if (voiceData->current) {
		const auto state = getState(voiceData->type);
		if (!IsStoppedOrStopping(state.state)) {
			return voiceData->type;
		}
	}
	return AudioMsgId::Type::Song;
}

void Instance::handleSongUpdate(const AudioMsgId &audioId) {
	emitUpdate(audioId.type(), [&audioId](const AudioMsgId &playing) {
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

		const auto history = data->history;
		const auto migrated = data->migrated;
		const auto item = App::histItemById(data->current.contextId());
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
	emitUpdate(data->type);
	data->streamed = nullptr;
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
	return App::histItemById(fullId);
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
		const auto state = getState(type);
		if (state.id) {
			if (IsStopped(state.state)) {
				play(state.id);
			} else if (data->streamed) {
				if (data->streamed->player.active()) {
					data->streamed->player.resume();
				}
				emitUpdate(type);
			} else {
				mixer()->resume(state.id);
			}
		} else {
			play(data->current);
		}
		data->resumeOnCallEnd = false;
	}
}

void Instance::play(const AudioMsgId &audioId) {
	const auto document = audioId.audio();
	if (!document) {
		return;
	}
	if (document->isAudioFile() || document->isVoiceMessage()) {
		auto loader = document->createStreamingLoader(audioId.contextId());
		if (!loader) {
			return;
		}
		playStreamed(audioId, std::move(loader));
	} else if (document->isVideoMessage()) {
		if (const auto item = App::histItemById(audioId.contextId())) {
			setCurrent(audioId);
			App::wnd()->controller()->startRoundVideo(item);
		}
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
	if (data->streamed) {
		clearStreamed(data);
	}
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
	auto result = Streaming::PlaybackOptions();
	result.mode = Streaming::Mode::Audio;
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
		} else {
			const auto state = getState(type);
			if (state.id) {
				mixer()->pause(state.id);
			}
		}
	}
}

void Instance::stop(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		if (data->streamed) {
			data->streamed = nullptr;
		} else {
			const auto state = getState(type);
			if (state.id) {
				mixer()->stop(state.id);
			}
		}
		data->resumeOnCallEnd = false;
	}
}

void Instance::playPause(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		if (data->streamed) {
			if (!data->streamed->player.active()) {
				data->streamed->player.play(
					streamingOptions(data->streamed->id));
			} else if (data->streamed->player.paused()) {
				data->streamed->player.resume();
			} else {
				data->streamed->player.pause();
			}
			emitUpdate(type);
		} else {
			const auto state = getState(type);
			if (state.id
				&& state.id.audio() == data->current.audio()
				&& state.id.contextId() == data->current.contextId()) {
				if (IsStopped(state.state)) {
					play(state.id);
				} else if (IsPaused(state.state) || state.state == State::Pausing) {
					mixer()->resume(state.id);
				} else {
					mixer()->pause(state.id);
				}
			} else {
				play(data->current);
			}
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
		//
		// Right now all music is played in streaming player.
		//} else {
		//	const auto state = getState(type);
		//	if (state.id && state.length && state.frequency) {
		//		mixer()->seek(type, qRound(progress * state.length * 1000. / state.frequency));
		//	}
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
	return mixer()->currentState(type);
}

template <typename CheckCallback>
void Instance::emitUpdate(AudioMsgId::Type type, CheckCallback check) {
	if (const auto data = getData(type)) {
		const auto state = getState(type);
		if (!state.id || !check(state.id)) {
			return;
		}
		setCurrent(state.id);
		_updatedNotifier.notify(state, true);
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

	update.data.match([&](Information & update) {
		data->streamed->info = std::move(update);
		emitUpdate(data->type);
	}, [&](PreloadedVideo &update) {
	}, [&](UpdateVideo &update) {
	}, [&](PreloadedAudio & update) {
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
			data->streamed = nullptr;
		}
	});
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
		data->streamed = nullptr;
	}
}

} // namespace Player
} // namespace Media
