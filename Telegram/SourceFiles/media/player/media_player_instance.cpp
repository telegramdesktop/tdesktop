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
#include "stdafx.h"
#include "media/player/media_player_instance.h"

#include "media/media_audio.h"
#include "observer_peer.h"

namespace Media {
namespace Player {
namespace {

Instance *SingleInstance = nullptr;

} // namespace

void start() {
	audioInit();
	if (audioPlayer()) {
		SingleInstance = new Instance();
	}
}

bool exists() {
	return (audioPlayer() != nullptr);
}

void finish() {
	delete base::take(SingleInstance);

	audioFinish();
}

Instance::Instance() {
	subscribe(audioPlayer(), [this](const AudioMsgId &audioId) {
		if (audioId.type() == AudioMsgId::Type::Song) {
			handleSongUpdate(audioId);
		}
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
	emitUpdate([&audioId](const AudioMsgId &playing) {
		return (audioId == playing);
	});
}

void Instance::setCurrent(const AudioMsgId &audioId) {
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
	AudioMsgId playing;
	auto playbackState = audioPlayer()->currentState(&playing, AudioMsgId::Type::Song);
	if (playing) {
		if (playbackState.state & AudioPlayerStoppedMask) {
			audioPlayer()->play(playing);
		} else {
			if (playbackState.state == AudioPlayerPausing || playbackState.state == AudioPlayerPaused || playbackState.state == AudioPlayerPausedAtEnd) {
				audioPlayer()->pauseresume(AudioMsgId::Type::Song);
			}
		}
	} else if (_current) {
		audioPlayer()->play(_current);
	}
}

void Instance::play(const AudioMsgId &audioId) {
	if (!audioId || !audioId.audio()->song()) {
		return;
	}
	audioPlayer()->play(audioId);
	setCurrent(audioId);
	if (audioId.audio()->loading()) {
		documentLoadProgress(audioId.audio());
	}
}

void Instance::pause() {
	AudioMsgId playing;
	auto playbackState = audioPlayer()->currentState(&playing, AudioMsgId::Type::Song);
	if (playing) {
		if (!(playbackState.state & AudioPlayerStoppedMask)) {
			if (playbackState.state == AudioPlayerStarting || playbackState.state == AudioPlayerResuming || playbackState.state == AudioPlayerPlaying || playbackState.state == AudioPlayerFinishing) {
				audioPlayer()->pauseresume(AudioMsgId::Type::Song);
			}
		}
	}
}

void Instance::stop() {
	audioPlayer()->stop(AudioMsgId::Type::Song);
}

void Instance::playPause() {
	AudioMsgId playing;
	auto playbackState = audioPlayer()->currentState(&playing, AudioMsgId::Type::Song);
	if (playing) {
		if (playbackState.state & AudioPlayerStoppedMask) {
			audioPlayer()->play(playing);
		} else {
			audioPlayer()->pauseresume(AudioMsgId::Type::Song);
		}
	} else if (_current) {
		audioPlayer()->play(_current);
	}
}

void Instance::next() {
	moveInPlaylist(1);
}

void Instance::previous() {
	moveInPlaylist(-1);
}

void Instance::playPauseCancelClicked() {
	if (isSeeking()) {
		return;
	}

	AudioMsgId playing;
	auto playbackState = audioPlayer()->currentState(&playing, AudioMsgId::Type::Song);
	auto stopped = ((playbackState.state & AudioPlayerStoppedMask) || playbackState.state == AudioPlayerFinishing);
	auto showPause = !stopped && (playbackState.state == AudioPlayerPlaying || playbackState.state == AudioPlayerResuming || playbackState.state == AudioPlayerStarting);
	auto audio = playing.audio();
	if (audio && audio->loading()) {
		audio->cancel();
	} else if (showPause) {
		pause();
	} else {
		play();
	}
}

void Instance::startSeeking() {
	_seeking = _current;
	pause();
	emitUpdate([](const AudioMsgId &playing) { return true; });
}

void Instance::stopSeeking() {
	_seeking = AudioMsgId();
	emitUpdate([](const AudioMsgId &playing) { return true; });
}

void Instance::documentLoadProgress(DocumentData *document) {
	emitUpdate([document](const AudioMsgId &audioId) {
		return (audioId.audio() == document);
	});
}

template <typename CheckCallback>
void Instance::emitUpdate(CheckCallback check) {
	AudioMsgId playing;
	auto playbackState = audioPlayer()->currentState(&playing, AudioMsgId::Type::Song);
	if (!playing || !check(playing)) {
		return;
	}

	setCurrent(playing);
	_updatedNotifier.notify(UpdatedEvent(&playing, &playbackState), true);

	if (_isPlaying && playbackState.state == AudioPlayerStoppedAtEnd) {
		if (_repeatEnabled) {
			audioPlayer()->play(_current);
		} else {
			next();
		}
	}
	auto isPlaying = !(playbackState.state & AudioPlayerStoppedMask);
	if (_isPlaying != isPlaying) {
		_isPlaying = isPlaying;
		if (_isPlaying) {
			preloadNext();
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
