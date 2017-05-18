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

Instance::Instance() {
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
			subscribe(AuthSession::Current().calls().currentCallChanged(), [this](Calls::Call *call) {
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

void Instance::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (!_history) {
		return;
	}
	if (!(update.mediaTypesMask & (1 << OverviewMusicFiles))) {
		return;
	}
	if (update.peer != _history->peer && (!_migrated || update.peer != _migrated->peer)) {
		return;
	}

	rebuildPlaylist();
}

void Instance::handleSongUpdate(const AudioMsgId &audioId) {
	emitUpdate(audioId.type(), [&audioId](const AudioMsgId &playing) {
		return (audioId == playing);
	});
}

void Instance::setCurrent(const AudioMsgId &audioId) {
	if (audioId.type() == AudioMsgId::Type::Song) {
		if (_current != audioId) {
			_current = audioId;
			_isPlaying = false;

			auto history = _history, migrated = _migrated;
			auto item = _current ? App::histItemById(_current.contextId()) : nullptr;
			if (item) {
				_history = item->history()->peer->migrateTo() ? App::history(item->history()->peer->migrateTo()) : item->history();
				_migrated = _history->peer->migrateFrom() ? App::history(_history->peer->migrateFrom()) : nullptr;
			} else {
				_history = _migrated = nullptr;
			}
			_songChangedNotifier.notify(true);
			if (_history != history || _migrated != migrated) {
				rebuildPlaylist();
			}
		}
	} else if (audioId.type() == AudioMsgId::Type::Voice) {
		if (_currentVoice != audioId) {
			_currentVoice = audioId;
		}
	}
}

void Instance::rebuildPlaylist() {
	_playlist.clear();
	if (_history && _history->loadedAtBottom()) {
		auto &historyOverview = _history->overview[OverviewMusicFiles];
		if (_migrated && _migrated->loadedAtBottom() && _history->loadedAtTop()) {
			auto &migratedOverview = _migrated->overview[OverviewMusicFiles];
			_playlist.reserve(migratedOverview.size() + historyOverview.size());
			for_const (auto msgId, migratedOverview) {
				_playlist.push_back(FullMsgId(_migrated->channelId(), msgId));
			}
		} else {
			_playlist.reserve(historyOverview.size());
		}
		for_const (auto msgId, historyOverview) {
			_playlist.push_back(FullMsgId(_history->channelId(), msgId));
		}
	}
	_playlistChangedNotifier.notify(true);
}

void Instance::moveInPlaylist(int delta) {
	auto index = _playlist.indexOf(_current.contextId());
	auto newIndex = index + delta;
	if (!_current || index < 0 || newIndex < 0 || newIndex >= _playlist.size()) {
		rebuildPlaylist();
		return;
	}

	auto msgId = _playlist[newIndex];
	if (auto item = App::histItemById(msgId)) {
		if (auto media = item->getMedia()) {
			if (auto document = media->getDocument()) {
				if (auto song = document->song()) {
					play(AudioMsgId(document, msgId));
				}
			}
		}
	}
}

Instance *instance() {
	t_assert(SingleInstance != nullptr);
	return SingleInstance;
}

void Instance::play() {
	auto state = mixer()->currentState(AudioMsgId::Type::Song);
	if (state.id) {
		if (IsStopped(state.state)) {
			mixer()->play(state.id);
		} else {
			mixer()->resume(state.id);
		}
	} else if (_current) {
		mixer()->play(_current);
	}
}

void Instance::play(const AudioMsgId &audioId) {
	if (!audioId || !audioId.audio()->song()) {
		return;
	}
	mixer()->play(audioId);
	setCurrent(audioId);
	if (audioId.audio()->loading()) {
		documentLoadProgress(audioId.audio());
	}
}

void Instance::pause(AudioMsgId::Type type) {
	auto state = mixer()->currentState(type);
	if (state.id) {
		mixer()->pause(state.id);
	}
}

void Instance::stop() {
	auto state = mixer()->currentState(AudioMsgId::Type::Song);
	if (state.id) {
		mixer()->stop(state.id);
	}
}

void Instance::playPause() {
	auto state = mixer()->currentState(AudioMsgId::Type::Song);
	if (state.id) {
		if (IsStopped(state.state)) {
			mixer()->play(state.id);
		} else if (IsPaused(state.state) || state.state == State::Pausing) {
			mixer()->resume(state.id);
		} else {
			mixer()->pause(state.id);
		}
	} else if (_current) {
		mixer()->play(_current);
	}
}

void Instance::next() {
	moveInPlaylist(1);
}

void Instance::previous() {
	moveInPlaylist(-1);
}

void Instance::playPauseCancelClicked() {
	if (isSeeking(AudioMsgId::Type::Song)) {
		return;
	}

	auto state = mixer()->currentState(AudioMsgId::Type::Song);
	auto stopped = (IsStopped(state.state) || state.state == State::Finishing);
	auto showPause = !stopped && (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
	auto audio = state.id.audio();
	if (audio && audio->loading()) {
		audio->cancel();
	} else if (showPause) {
		pause(AudioMsgId::Type::Song);
	} else {
		play();
	}
}

void Instance::startSeeking(AudioMsgId::Type type) {
	if (type == AudioMsgId::Type::Song) {
		_seeking = _current;
	} else if (type == AudioMsgId::Type::Voice) {
		_seekingVoice = _currentVoice;
	}
	pause(type);
	emitUpdate(type, [](const AudioMsgId &playing) { return true; });
}

void Instance::stopSeeking(AudioMsgId::Type type) {
	if (type == AudioMsgId::Type::Song) {
		_seeking = AudioMsgId();
	} else if (type == AudioMsgId::Type::Voice) {
		_seekingVoice = AudioMsgId();
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

	if (type == AudioMsgId::Type::Song) {
		if (_isPlaying && state.state == State::StoppedAtEnd) {
			if (_repeatEnabled) {
				mixer()->play(_current);
			} else {
				next();
			}
		}
		auto isPlaying = !IsStopped(state.state);
		if (_isPlaying != isPlaying) {
			_isPlaying = isPlaying;
			if (_isPlaying) {
				preloadNext();
			}
		}
	}
}

void Instance::preloadNext() {
	if (!_current) {
		return;
	}
	auto index = _playlist.indexOf(_current.contextId());
	if (index < 0) {
		return;
	}
	auto nextIndex = index + 1;
	if (nextIndex >= _playlist.size()) {
		return;
	}
	if (auto item = App::histItemById(_playlist[nextIndex])) {
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
	_current = _seeking = AudioMsgId();
	_history = nullptr;
	_migrated = nullptr;

	_repeatEnabled = _isPlaying = false;

	_playlist.clear();

	_usePanelPlayer.notify(false, true);
}

} // namespace Player
} // namespace Media
