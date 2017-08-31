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

#include "media/media_audio.h"
#include "media/media_audio_capture.h"
#include "observer_peer.h"
#include "messenger.h"
#include "auth_session.h"
#include "calls/calls_instance.h"
#include "history/history_media.h"

namespace Media {
namespace Player {
namespace {

Instance *SingleInstance = nullptr;

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
: _songData(AudioMsgId::Type::Song, OverviewMusicFiles)
, _voiceData(AudioMsgId::Type::Voice, OverviewRoundVoiceFiles) {
	subscribe(Media::Player::Updated(), [this](const AudioMsgId &audioId) {
		handleSongUpdate(audioId);
	});
	auto observeEvents = Notify::PeerUpdate::Flag::SharedMediaChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));
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

void Instance::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	checkPeerUpdate(AudioMsgId::Type::Song, update);
	checkPeerUpdate(AudioMsgId::Type::Voice, update);
}

void Instance::checkPeerUpdate(AudioMsgId::Type type, const Notify::PeerUpdate &update) {
	if (auto data = getData(type)) {
		if (!data->history) {
			return;
		}
		if (!(update.mediaTypesMask & (1 << data->overview))) {
			return;
		}
		if (update.peer != data->history->peer && (!data->migrated || update.peer != data->migrated->peer)) {
			return;
		}

		rebuildPlaylist(data);
	}
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
			if (data->history != history || data->migrated != migrated) {
				rebuildPlaylist(data);
			}
		}
	}
}

void Instance::rebuildPlaylist(Data *data) {
	Expects(data != nullptr);

	data->playlist.clear();
	if (data->history && data->history->loadedAtBottom()) {
		auto &historyOverview = data->history->overview(data->overview);
		if (data->migrated && data->migrated->loadedAtBottom() && data->history->loadedAtTop()) {
			auto &migratedOverview = data->migrated->overview(data->overview);
			data->playlist.reserve(migratedOverview.size() + historyOverview.size());
			for_const (auto msgId, migratedOverview) {
				data->playlist.push_back(FullMsgId(data->migrated->channelId(), msgId));
			}
		} else {
			data->playlist.reserve(historyOverview.size());
		}
		for_const (auto msgId, historyOverview) {
			data->playlist.push_back(FullMsgId(data->history->channelId(), msgId));
		}
	}
	_playlistChangedNotifier.notify(data->type, true);
}

bool Instance::moveInPlaylist(Data *data, int delta, bool autonext) {
	Expects(data != nullptr);

	auto index = data->playlist.indexOf(data->current.contextId());
	auto newIndex = index + delta;
	if (!data->current || index < 0 || newIndex < 0 || newIndex >= data->playlist.size()) {
		rebuildPlaylist(data);
		return false;
	}

	auto msgId = data->playlist[newIndex];
	if (auto item = App::histItemById(msgId)) {
		if (auto media = item->getMedia()) {
			if (auto document = media->getDocument()) {
				if (autonext) {
					_switchToNextNotifier.notify({ data->current, msgId });
				}
				DocumentOpenClickHandler::doOpen(media->getDocument(), item, ActionOnLoadPlayInline);
				return true;
			}
		}
	}
	return false;
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
	if (audioId.audio()->song() || audioId.audio()->voice()) {
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
	emitUpdate(document->song() ? AudioMsgId::Type::Song : AudioMsgId::Type::Voice, [document](const AudioMsgId &audioId) {
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

void Instance::preloadNext(Data *data) {
	Expects(data != nullptr);

	if (!data->current) {
		return;
	}
	auto index = data->playlist.indexOf(data->current.contextId());
	if (index < 0) {
		return;
	}
	auto nextIndex = index + 1;
	if (nextIndex >= data->playlist.size()) {
		return;
	}
	if (auto item = App::histItemById(data->playlist[nextIndex])) {
		if (auto media = item->getMedia()) {
			if (auto document = media->getDocument()) {
				if (!document->loaded(DocumentData::FilePathResolveSaveFromDataSilent)) {
					DocumentOpenClickHandler::doOpen(document, nullptr, ActionOnLoadNone);
				}
			}
		}
	}
}

void Instance::handleLogout() {
	*getData(AudioMsgId::Type::Voice) = Data(AudioMsgId::Type::Voice, OverviewRoundVoiceFiles);
	*getData(AudioMsgId::Type::Song) = Data(AudioMsgId::Type::Song, OverviewMusicFiles);
	_usePanelPlayer.notify(false, true);
}

} // namespace Player
} // namespace Media
