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
#pragma once

namespace Notify {
struct PeerUpdate;
} // namespace Notify
class AudioMsgId;

namespace Media {
namespace Player {

void start();
void finish();

class Instance;
Instance *instance();

struct TrackState;

class Instance : private base::Subscriber {
public:
	void play();
	void pause(AudioMsgId::Type type);
	void stop();
	void playPause();
	void next();
	void previous();

	void playPauseCancelClicked();

	void play(const AudioMsgId &audioId);
	const AudioMsgId &current() const {
		return _current;
	}

	bool repeatEnabled() const {
		return _repeatEnabled;
	}
	void toggleRepeat() {
		_repeatEnabled = !_repeatEnabled;
		_repeatChangedNotifier.notify();
	}

	bool isSeeking(AudioMsgId::Type type) const {
		if (type == AudioMsgId::Type::Song) {
			return (_seeking == _current);
		} else if (type == AudioMsgId::Type::Voice) {
			return (_seekingVoice == _currentVoice);
		}
		return false;
	}
	void startSeeking(AudioMsgId::Type type);
	void stopSeeking(AudioMsgId::Type type);

	const QList<FullMsgId> &playlist() const {
		return _playlist;
	}

	base::Observable<bool> &usePanelPlayer() {
		return _usePanelPlayer;
	}
	base::Observable<bool> &titleButtonOver() {
		return _titleButtonOver;
	}
	base::Observable<bool> &playerWidgetOver() {
		return _playerWidgetOver;
	}
	base::Observable<TrackState> &updatedNotifier() {
		return _updatedNotifier;
	}
	base::Observable<void> &playlistChangedNotifier() {
		return _playlistChangedNotifier;
	}
	base::Observable<void> &songChangedNotifier() {
		return _songChangedNotifier;
	}
	base::Observable<void> &repeatChangedNotifier() {
		return _repeatChangedNotifier;
	}

	void documentLoadProgress(DocumentData *document);

	void clear();

private:
	Instance();
	friend void start();

	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);
	void handleSongUpdate(const AudioMsgId &audioId);

	void setCurrent(const AudioMsgId &audioId);
	void rebuildPlaylist();
	void moveInPlaylist(int delta);
	void preloadNext();
	void handleLogout();

	template <typename CheckCallback>
	void emitUpdate(AudioMsgId::Type type, CheckCallback check);

	AudioMsgId _current, _seeking;
	History *_history = nullptr;
	History *_migrated = nullptr;

	bool _repeatEnabled = false;

	QList<FullMsgId> _playlist;
	bool _isPlaying = false;

	AudioMsgId _currentVoice, _seekingVoice;

	base::Observable<bool> _usePanelPlayer;
	base::Observable<bool> _titleButtonOver;
	base::Observable<bool> _playerWidgetOver;
	base::Observable<TrackState> _updatedNotifier;
	base::Observable<void> _playlistChangedNotifier;
	base::Observable<void> _songChangedNotifier;
	base::Observable<void> _repeatChangedNotifier;

};

} // namespace Clip
} // namespace Media
