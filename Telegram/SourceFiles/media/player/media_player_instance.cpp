/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_instance.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_streaming.h"
#include "data/data_file_click_handler.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_audio_capture.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "media/view/media_view_playback_progress.h"
#include "calls/calls_instance.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_media_types.h"
#include "data/data_file_origin.h"
#include "window/window_session_controller.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "main/main_domain.h" // Domain::activeSessionValue.
#include "mainwindow.h"
#include "main/main_session.h"
#include "main/main_account.h" // session->account().sessionChanges().
#include "main/main_session_settings.h"

namespace Media {
namespace Player {
namespace {

Instance *SingleInstance = nullptr;

constexpr auto kVoicePlaybackSpeedMultiplier = 1.7;

// Preload X message ids before and after current.
constexpr auto kIdsLimit = 32;

// Preload next messages if we went further from current than that.
constexpr auto kIdsPreloadAfter = 28;

constexpr auto kMinLengthForSavePosition = 20 * TimeId(60); // 20 minutes.

} // namespace

struct Instance::Streamed {
	Streamed(
		AudioMsgId id,
		std::shared_ptr<Streaming::Document> document);

	AudioMsgId id;
	Streaming::Instance instance;
	View::PlaybackProgress progress;
	bool clearing = false;
	rpl::lifetime lifetime;
};

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

void SaveLastPlaybackPosition(
		not_null<DocumentData*> document,
		const TrackState &state) {
	const auto time = (state.position == kTimeUnknown
		|| state.length == kTimeUnknown
		|| state.state == State::PausedAtEnd
		|| IsStopped(state.state))
		? TimeId(0)
		: (state.length >= kMinLengthForSavePosition * state.frequency)
		? (state.position / state.frequency) * crl::time(1000)
		: TimeId(0);
	auto &session = document->session();
	if (session.settings().mediaLastPlaybackPosition(document->id) != time) {
		session.settings().setMediaLastPlaybackPosition(document->id, time);
		session.saveSettingsDelayed();
	}
}

Instance::Streamed::Streamed(
	AudioMsgId id,
	std::shared_ptr<Streaming::Document> document)
: id(id)
, instance(std::move(document), nullptr) {
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

	using namespace rpl::mappers;
	rpl::combine(
		Core::App().calls().currentCallValue(),
		Core::App().calls().currentGroupCallValue(),
		_1 || _2
	) | rpl::start_with_next([=](bool call) {
		if (call) {
			pauseOnCall(AudioMsgId::Type::Voice);
			pauseOnCall(AudioMsgId::Type::Song);
		} else {
			resumeOnCall(AudioMsgId::Type::Voice);
			resumeOnCall(AudioMsgId::Type::Song);
		}
	}, _lifetime);

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

		const auto item = (audioId.audio() && audioId.contextId())
			? audioId.audio()->owner().message(audioId.contextId())
			: nullptr;
		if (item) {
			setHistory(data, item->history());
		} else {
			data->history = nullptr;
			data->migrated = nullptr;
			data->session = nullptr;
		}
		_trackChangedNotifier.notify(data->type, true);
		refreshPlaylist(data);
	}
}

void Instance::setHistory(not_null<Data*> data, History *history) {
	if (history) {
		data->history = history->migrateToOrMe();
		data->migrated = data->history->migrateFrom();
		setSession(data, &history->session());
	} else {
		data->history = data->migrated = nullptr;
		setSession(data, nullptr);
	}
}

void Instance::setSession(not_null<Data*> data, Main::Session *session) {
	if (data->session == session) {
		return;
	}
	data->playlistLifetime.destroy();
	data->sessionLifetime.destroy();
	data->session = session;
	if (session) {
		session->account().sessionChanges(
		) | rpl::start_with_next([=] {
			setSession(data, nullptr);
		}, data->sessionLifetime);
		session->data().itemRemoved(
		) | rpl::filter([=](not_null<const HistoryItem*> item) {
			return (data->current.contextId() == item->fullId());
		}) | rpl::start_with_next([=] {
			stopAndClear(data);
		}, data->sessionLifetime);
	} else {
		stopAndClear(data);
	}
}

void Instance::clearStreamed(not_null<Data*> data, bool savePosition) {
	if (!data->streamed || data->streamed->clearing) {
		return;
	}
	data->streamed->clearing = true;
	if (savePosition) {
		SaveLastPlaybackPosition(
			data->current.audio(),
			data->streamed->instance.player().prepareLegacyState());
	}
	data->streamed->instance.stop();
	data->isPlaying = false;
	requestRoundVideoResize();
	emitUpdate(data->type);
	data->streamed = nullptr;

	_roundPlaying = false;
	if (const auto window = App::wnd()) {
		if (const auto controller = window->sessionController()) {
			controller->disableGifPauseReason(
				Window::GifPauseReason::RoundPlaying);
		}
	}
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
				&& (a.migratedPeerId == b.migratedPeerId)
				&& (a.scheduled == b.scheduled);
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
	data->playlistLifetime.destroy();
	if (const auto key = playlistKey(data)) {
		data->playlistRequestedKey = key;

		const auto sharedMediaViewer = key->scheduled
			? SharedScheduledMediaViewer
			: SharedMediaMergedViewer;
		sharedMediaViewer(
			&data->history->session(),
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
	if (!contextId || !history) {
		return {};
	}
	const auto item = data->history->owner().message(contextId);
	if (!item || (!IsServerMsgId(contextId.msg) && !item->isScheduled())) {
		return {};
	}

	const auto universalId = (contextId.channel == history->channelId())
		? contextId.msg
		: (contextId.msg - ServerMaxMsgId);
	return SliceKey(
		data->history->peer->id,
		data->migrated ? data->migrated->peer->id : 0,
		universalId,
		item->isScheduled());
}

HistoryItem *Instance::itemByIndex(not_null<Data*> data, int index) {
	if (!data->playlistSlice
		|| index < 0
		|| index >= data->playlistSlice->size()) {
		return nullptr;
	}
	Assert(data->history != nullptr);
	const auto fullId = (*data->playlistSlice)[index];
	return data->history->owner().message(fullId);
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

rpl::producer<> Media::Player::Instance::stops(AudioMsgId::Type type) const {
	return _playerStopped.events(
	) | rpl::filter([=](auto t) {
		return t == type;
	}) | rpl::to_empty;
}

rpl::producer<> Media::Player::Instance::startsPlay(
		AudioMsgId::Type type) const {
	return _playerStartedPlay.events(
	) | rpl::filter([=](auto t) {
		return t == type;
	}) | rpl::to_empty;
}

auto Media::Player::Instance::seekingChanges(AudioMsgId::Type type) const
-> rpl::producer<Media::Player::Instance::Seeking> {
	return _seekingChanges.events(
	) | rpl::filter([=](SeekingChanges data) {
		return data.type == type;
	}) | rpl::map([](SeekingChanges data) {
		return data.seeking;
	});
}

not_null<Instance*> instance() {
	Expects(SingleInstance != nullptr);
	return SingleInstance;
}

void Instance::play(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		if (!data->streamed || IsStopped(getState(type).state)) {
			play(data->current);
		} else {
			if (data->streamed->instance.active()) {
				data->streamed->instance.resume();
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
		auto shared = document->owner().streaming().sharedDocument(
			document,
			audioId.contextId());
		if (!shared) {
			return;
		}
		playStreamed(audioId, std::move(shared));
	}
	if (document->isVoiceMessage() || document->isVideoMessage()) {
		document->owner().markMediaRead(document);
	}
	_playerStartedPlay.fire_copy({audioId.type()});
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
		std::shared_ptr<Streaming::Document> shared) {
	Expects(audioId.audio() != nullptr);

	const auto data = getData(audioId.type());
	Assert(data != nullptr);

	clearStreamed(data, data->current.audio() != audioId.audio());
	data->streamed = std::make_unique<Streamed>(
		audioId,
		std::move(shared));
	data->streamed->instance.lockPlayer();

	data->streamed->instance.player().updates(
	) | rpl::start_with_next_error([=](Streaming::Update &&update) {
		handleStreamingUpdate(data, std::move(update));
	}, [=](Streaming::Error &&error) {
		handleStreamingError(data, std::move(error));
	}, data->streamed->lifetime);

	data->streamed->instance.play(streamingOptions(audioId));

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
		&& Core::App().settings().voiceMsgPlaybackDoubled())
		? kVoicePlaybackSpeedMultiplier
		: 1.;
	result.audioId = audioId;
	if (position >= 0) {
		result.position = position;
	} else if (document) {
		auto &settings = document->session().settings();
		result.position = settings.mediaLastPlaybackPosition(document->id);
		settings.setMediaLastPlaybackPosition(document->id, 0);
	} else {
		result.position = 0;
	}
	return result;
}

void Instance::pause(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		if (data->streamed) {
			if (data->streamed->instance.active()) {
				data->streamed->instance.pause();
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
		_playerStopped.fire_copy({type});
	}
}

void Instance::stopAndClear(not_null<Data*> data) {
	stop(data->type);
	_tracksFinishedNotifier.notify(data->type);
	*data = Data(data->type, data->overview);
}

void Instance::playPause(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		if (!data->streamed) {
			play(data->current);
		} else {
			auto &streamed = data->streamed->instance;
			if (!streamed.active()) {
				streamed.play(streamingOptions(data->streamed->id));
			} else if (streamed.paused()) {
				streamed.resume();
			} else {
				streamed.pause();
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
	_seekingChanges.fire({ .seeking = Seeking::Start, .type = type });
}

void Instance::finishSeeking(AudioMsgId::Type type, float64 progress) {
	if (const auto data = getData(type)) {
		if (const auto streamed = data->streamed.get()) {
			const auto &info = streamed->instance.info();
			const auto duration = info.audio.state.duration;
			if (duration != kTimeUnknown) {
				const auto position = crl::time(std::round(
					std::clamp(progress, 0., 1.) * duration));
				streamed->instance.play(streamingOptions(
					streamed->id,
					position));
				emitUpdate(type);
			}
		}
	}
	cancelSeeking(type);
	_seekingChanges.fire({ .seeking = Seeking::Finish, .type = type });
}

void Instance::cancelSeeking(AudioMsgId::Type type) {
	if (const auto data = getData(type)) {
		data->seeking = AudioMsgId();
	}
	emitUpdate(type);
	_seekingChanges.fire({ .seeking = Seeking::Cancel, .type = type });
}

void Instance::updateVoicePlaybackSpeed() {
	if (const auto data = getData(AudioMsgId::Type::Voice)) {
		if (const auto streamed = data->streamed.get()) {
			streamed->instance.setSpeed(Core::App().settings().voiceMsgPlaybackDoubled()
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
			return data->streamed->instance.player().prepareLegacyState();
		}
	}
	return TrackState();
}

Streaming::Instance *Instance::roundVideoStreamed(HistoryItem *item) const {
	if (!item) {
		return nullptr;
	} else if (const auto data = getData(AudioMsgId::Type::Voice)) {
		if (const auto streamed = data->streamed.get()) {
			if (streamed->id.contextId() == item->fullId()) {
				const auto player = &streamed->instance.player();
				if (player->ready() && !player->videoSize().isEmpty()) {
					return &streamed->instance;
				}
			}
		}
	}
	return nullptr;
}

View::PlaybackProgress *Instance::roundVideoPlayback(
		HistoryItem *item) const {
	return roundVideoStreamed(item)
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
		if (const auto streamed = data->streamed.get()) {
			if (!streamed->instance.info().video.size.isEmpty()) {
				streamed->progress.updateState(state);
			}
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

void Instance::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::MediaPlay) && request->handle([=] {
			playPause();
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

bool Instance::pauseGifByRoundVideo() const {
	return _roundPlaying;
}

void Instance::handleStreamingUpdate(
		not_null<Data*> data,
		Streaming::Update &&update) {
	using namespace Streaming;

	v::match(update.data, [&](Information &update) {
		if (!update.video.size.isEmpty()) {
			data->streamed->progress.setValueChangedCallback([=](
					float64,
					float64) {
				requestRoundVideoRepaint();
			});
			_roundPlaying = true;
			if (const auto window = App::wnd()) {
				if (const auto controller = window->sessionController()) {
					controller->enableGifPauseReason(
						Window::GifPauseReason::RoundPlaying);
				}
			}
			requestRoundVideoResize();
		}
		emitUpdate(data->type);
	}, [&](PreloadedVideo &update) {
		//emitUpdate(data->type, [](AudioMsgId) { return true; });
	}, [&](UpdateVideo &update) {
		emitUpdate(data->type);
	}, [&](PreloadedAudio &update) {
		//emitUpdate(data->type, [](AudioMsgId) { return true; });
	}, [&](UpdateAudio &update) {
		emitUpdate(data->type);
	}, [&](WaitingForData) {
	}, [&](MutedByOther) {
	}, [&](Finished) {
		emitUpdate(data->type);
		if (data->streamed && data->streamed->instance.player().finished()) {
			clearStreamed(data);
		}
	});
}

HistoryItem *Instance::roundVideoItem() const {
	const auto data = getData(AudioMsgId::Type::Voice);
	return (data->streamed
		&& !data->streamed->instance.info().video.size.isEmpty()
		&& data->history)
		? data->history->owner().message(data->streamed->id.contextId())
		: nullptr;
}

void Instance::requestRoundVideoResize() const {
	if (const auto item = roundVideoItem()) {
		item->history()->owner().requestItemResize(item);
	}
}

void Instance::requestRoundVideoRepaint() const {
	if (const auto item = roundVideoItem()) {
		item->history()->owner().requestItemRepaint(item);
	}
}

void Instance::handleStreamingError(
		not_null<Data*> data,
		Streaming::Error &&error) {
	Expects(data->streamed != nullptr);

	const auto document = data->streamed->id.audio();
	const auto contextId = data->streamed->id.contextId();
	if (error == Streaming::Error::NotStreamable) {
		DocumentSaveClickHandler::Save(
			(contextId ? contextId : ::Data::FileOrigin()),
			document);
	} else if (error == Streaming::Error::OpenFailed) {
		DocumentSaveClickHandler::Save(
			(contextId ? contextId : ::Data::FileOrigin()),
			document,
			DocumentSaveClickHandler::Mode::ToFile);
	}
	emitUpdate(data->type);
	if (data->streamed && data->streamed->instance.player().failed()) {
		clearStreamed(data);
	}
}

} // namespace Player
} // namespace Media
