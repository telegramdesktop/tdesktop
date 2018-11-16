/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_instance.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "media/media_audio.h"
#include "media/media_audio_capture.h"
#include "calls/calls_instance.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_media_types.h"
#include "window/window_controller.h"
#include "core/shortcuts.h"
#include "messenger.h"
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

void start() {
	Audio::Start();
	Capture::Start();

	SingleInstance = new Instance();
}

void finish() {
	delete base::take(SingleInstance);

	Capture::Finish();
	Audio::Finish();
}

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
		} else {
			handleLogout();
		}
	};
	subscribe(
		Messenger::Instance().authSessionChanged(),
		handleAuthSessionChange);
	handleAuthSessionChange();

	setupShortcuts();
}

AudioMsgId::Type Instance::getActiveType() const {
	auto voiceData = getData(AudioMsgId::Type::Voice);
	if (voiceData->current) {
		auto state = mixer()->currentState(voiceData->type);
		if (voiceData->current == state.id && !IsStoppedOrStopping(state.state)) {
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
	if (auto data = getData(audioId.type())) {
		if (data->current != audioId) {
			data->current = audioId;
			data->isPlaying = false;

			auto history = data->history;
			auto migrated = data->migrated;
			auto item = data->current
				? App::histItemById(data->current.contextId())
				: nullptr;
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
				} else {
					//DocumentOpenClickHandler::Open(
					//	item->fullId(),
					//	document,
					//	item,
					//	ActionOnLoadPlayInline);
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
	auto state = mixer()->currentState(type);
	if (state.id) {
		if (IsStopped(state.state)) {
			play(state.id);
		} else {
			mixer()->resume(state.id);
		}
	} else if (auto data = getData(type)) {
		if (data->current) {
			play(data->current);
		}
	}
	if (const auto data = getData(type)) {
		data->resumeOnCallEnd = false;
	}
}

void Instance::play(const AudioMsgId &audioId) {
	const auto document = audioId.audio();
	if (!audioId || !document) {
		return;
	}
	if (document->isAudioFile() || document->isVoiceMessage()) {
		mixer()->play(audioId);
		setCurrent(audioId);
		if (document->loading()) {
			documentLoadProgress(document);
		}
	} else if (document->isVideoMessage()) {
		if (const auto item = App::histItemById(audioId.contextId())) {
			App::wnd()->controller()->startRoundVideo(item);
		}
	}
	if (document->isVoiceMessage() || document->isVideoMessage()) {
		document->session()->data().markMediaRead(document);
	}
}

void Instance::pause(AudioMsgId::Type type) {
	const auto state = mixer()->currentState(type);
	if (state.id) {
		mixer()->pause(state.id);
	}
}

void Instance::stop(AudioMsgId::Type type) {
	const auto state = mixer()->currentState(type);
	if (state.id) {
		mixer()->stop(state.id);
	}
	if (const auto data = getData(type)) {
		data->resumeOnCallEnd = false;
	}
}

void Instance::playPause(AudioMsgId::Type type) {
	const auto state = mixer()->currentState(type);
	if (state.id) {
		if (IsStopped(state.state)) {
			play(state.id);
		} else if (IsPaused(state.state) || state.state == State::Pausing) {
			mixer()->resume(state.id);
		} else {
			mixer()->pause(state.id);
		}
	} else if (auto data = getData(type)) {
		if (data->current) {
			play(data->current);
		}
	}
	if (const auto data = getData(type)) {
		data->resumeOnCallEnd = false;
	}
}

void Instance::pauseOnCall(AudioMsgId::Type type) {
	const auto state = mixer()->currentState(type);
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

	auto state = mixer()->currentState(type);
	auto stopped = IsStoppedOrStopping(state.state);
	auto showPause = !stopped && (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
	auto audio = state.id.audio();
	if (audio && audio->loading()) {
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
	emitUpdate(type, [](const AudioMsgId &playing) { return true; });
}

void Instance::stopSeeking(AudioMsgId::Type type) {
	if (auto data = getData(type)) {
		data->seeking = AudioMsgId();
	}
	emitUpdate(type, [](const AudioMsgId &playing) { return true; });
}

void Instance::documentLoadProgress(DocumentData *document) {
	const auto type = document->isAudioFile()
		? AudioMsgId::Type::Song
		: AudioMsgId::Type::Voice;
	emitUpdate(type, [document](const AudioMsgId &audioId) {
		return (audioId.audio() == document);
	});
}

template <typename CheckCallback>
void Instance::emitUpdate(AudioMsgId::Type type, CheckCallback check) {
	auto state = mixer()->currentState(type);
	if (!state.id || !check(state.id)) {
		return;
	}

	setCurrent(state.id);
	_updatedNotifier.notify(state, true);

	if (auto data = getData(type)) {
		if (data->isPlaying && state.state == State::StoppedAtEnd) {
			if (data->repeatEnabled) {
				play(data->current);
			} else if (!moveInPlaylist(data, 1, true)) {
				_tracksFinishedNotifier.notify(type);
			}
		}
		auto isPlaying = !IsStopped(state.state);
		if (data->isPlaying != isPlaying) {
			data->isPlaying = isPlaying;
			if (data->isPlaying) {
				preloadNext(data);
			}
		}
	}
}

void Instance::preloadNext(not_null<Data*> data) {
	if (!data->current || !data->playlistSlice || !data->playlistIndex) {
		return;
	}
	const auto nextIndex = *data->playlistIndex + 1;
	if (const auto item = itemByIndex(data, nextIndex)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				const auto isLoaded = document->loaded(
					DocumentData::FilePathResolveSaveFromDataSilent);
				if (!isLoaded) {
					DocumentOpenClickHandler::Open(
						item->fullId(),
						document,
						item,
						ActionOnLoadNone);
				}
			}
		}
	}
}

void Instance::handleLogout() {
	const auto reset = [&](AudioMsgId::Type type) {
		const auto data = getData(type);
		*data = Data(type, data->overview);
	};
	reset(AudioMsgId::Type::Voice);
	reset(AudioMsgId::Type::Song);
	_usePanelPlayer.notify(false, true);
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

} // namespace Player
} // namespace Media
