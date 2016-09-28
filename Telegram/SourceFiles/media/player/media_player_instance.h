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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

namespace Notify {
struct PeerUpdate;
} // namespace Notify
struct AudioPlaybackState;
class AudioMsgId;

namespace Media {
namespace Player {

void start();
void finish();

// We use this method instead of checking for instance() != nullptr
// because audioPlayer() can be destroyed at any time by an
// error in audio playback, so we check it each time.
bool exists();

class Instance;
Instance *instance();

class Widget;
struct CreatedEvent {
	explicit CreatedEvent(Widget *widget) : widget(widget) {
	}
	Widget *widget;
};
struct UpdatedEvent {
	UpdatedEvent(const AudioMsgId *audioId, const AudioPlaybackState *playbackState) : audioId(audioId), playbackState(playbackState) {
	}
	const AudioMsgId *audioId;
	const AudioPlaybackState *playbackState;
};

class Instance : private base::Subscriber {
public:
	void play();
	void pause();
	void stop();
	void playPause();
	void next();
	void previous();

	void play(const AudioMsgId &audioId);
	const AudioMsgId &current() const {
		return _current;
	}

	bool repeatEnabled() const {
		return _repeatEnabled;
	}
	void toggleRepeat() {
		_repeatEnabled = !_repeatEnabled;
	}

	bool isSeeking() const {
		return (_seeking == _current);
	}
	void startSeeking();
	void stopSeeking();

	const QList<FullMsgId> &playlist() const {
		return _playlist;
	}

	base::Observable<CreatedEvent> &createdNotifier() {
		return _createdNotifier;
	}
	base::Observable<UpdatedEvent> &updatedNotifier() {
		return _updatedNotifier;
	}
	base::Observable<void> &playlistChangedNotifier() {
		return _playlistChangedNotifier;
	}
	base::Observable<void> &songChangedNotifier() {
		return _songChangedNotifier;
	}

	void documentLoadProgress(DocumentData *document);

private:
	Instance();
	friend void start();

	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);
	void handleSongUpdate(const AudioMsgId &audioId);

	void setCurrent(const AudioMsgId &audioId);
	void rebuildPlaylist();
	void moveInPlaylist(int delta);

	template <typename CheckCallback>
	void emitUpdate(CheckCallback check);

	AudioMsgId _current, _seeking;
	History *_history = nullptr;
	History *_migrated = nullptr;

	bool _repeatEnabled = false;

	QList<FullMsgId> _playlist;
	bool _isPlaying = false;

	base::Observable<CreatedEvent> _createdNotifier;
	base::Observable<UpdatedEvent> _updatedNotifier;
	base::Observable<void> _playlistChangedNotifier;
	base::Observable<void> _songChangedNotifier;

};

} // namespace Clip
} // namespace Media
