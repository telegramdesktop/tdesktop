/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "media/player/media_player_instance.h"

#include "data/data_document.h"
#include "media/media_audio.h"
#include "media/media_audio_capture.h"
#include "messenger.h"
#include "auth_session.h"
#include "calls/calls_instance.h"
#include "history/history_media.h"

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
	subscribe(Global::RefSelfChanged(), [this] {
		if (!App::self()) {
			handleLogout();
		}
	});

	// While we have one Media::Player::Instance for all authsessions we have to do this.
	auto handleAuthSessionChange = [this] {
		if (AuthSession::Exists()) {
			subscribe(Auth().calls().currentCallChanged(), [this](Calls::Call *call) {
				if (call) {
					pause(AudioMsgId::Type::Voice);
					pause(AudioMsgId::Type::Song);
				}
			});
		}
	};
	subscribe(Messenger::Instance().authSessionChanged(), [handleAuthSessionChange] {
		handleAuthSessionChange();
	});
	handleAuthSessionChange();
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
			auto item = data->current ? App::histItemById(data->current.contextId()) : nullptr;
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
		data->playlistIndex = base::none;
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
					: base::optional<int>();
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
		data->playlistSlice = base::none;
		data->playlistSliceKey = data->playlistRequestedKey = base::none;
		playlistUpdated(data);
	}
}

auto Instance::playlistKey(not_null<Data*> data) const
-> base::optional<SliceKey> {
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
		if (const auto media = item->getMedia()) {
			if (const auto document = media->getDocument()) {
				if (autonext) {
					_switchToNextNotifier.notify({
						data->current,
						item->fullId()
					});
				}
				if (document->tryPlaySong()) {
					play(AudioMsgId(document, item->fullId()));
				} else {
					DocumentOpenClickHandler::doOpen(
						document,
						item,
						ActionOnLoadPlayInline);
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
}

void Instance::play(const AudioMsgId &audioId) {
	if (!audioId) {
		return;
	}
	if (audioId.audio()->tryPlaySong() || audioId.audio()->voice()) {
		mixer()->play(audioId);
		setCurrent(audioId);
		if (audioId.audio()->loading()) {
			documentLoadProgress(audioId.audio());
		}
	} else if (audioId.audio()->isRoundVideo()) {
		if (auto item = App::histItemById(audioId.contextId())) {
			if (auto media = item->getMedia()) {
				media->playInline();
			}
		}
	}
}

void Instance::pause(AudioMsgId::Type type) {
	auto state = mixer()->currentState(type);
	if (state.id) {
		mixer()->pause(state.id);
	}
}

void Instance::stop(AudioMsgId::Type type) {
	auto state = mixer()->currentState(type);
	if (state.id) {
		mixer()->stop(state.id);
	}
}

void Instance::playPause(AudioMsgId::Type type) {
	auto state = mixer()->currentState(type);
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
}

bool Instance::next(AudioMsgId::Type type) {
	if (auto data = getData(type)) {
		return moveInPlaylist(data, 1, false);
	}
	return false;
}

bool Instance::previous(AudioMsgId::Type type) {
	if (auto data = getData(type)) {
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
	const auto type = document->tryPlaySong()
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
		if (const auto media = item->getMedia()) {
			if (const auto document = media->getDocument()) {
				const auto isLoaded = document->loaded(
					DocumentData::FilePathResolveSaveFromDataSilent);
				if (!isLoaded) {
					DocumentOpenClickHandler::doOpen(
						document,
						nullptr,
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

} // namespace Player
} // namespace Media
