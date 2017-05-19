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
	void play(AudioMsgId::Type type);
	void pause(AudioMsgId::Type type);
	void stop(AudioMsgId::Type type);
	void playPause(AudioMsgId::Type type);
	void next(AudioMsgId::Type type);
	void previous(AudioMsgId::Type type);

	void play() {
		play(getActiveType());
	}
	void pause() {
		pause(getActiveType());
	}
	void stop() {
		stop(getActiveType());
	}
	void playPause() {
		playPause(getActiveType());
	}
	void next() {
		next(getActiveType());
	}
	void previous() {
		previous(getActiveType());
	}

	void playPauseCancelClicked(AudioMsgId::Type type);

	void play(const AudioMsgId &audioId);
	AudioMsgId current(AudioMsgId::Type type) const {
		if (auto data = getData(type)) {
			return data->current;
		}
		return AudioMsgId();
	}

	bool repeatEnabled(AudioMsgId::Type type) const {
		if (auto data = getData(type)) {
			return data->repeatEnabled;
		}
		return false;
	}
	void toggleRepeat(AudioMsgId::Type type) {
		if (auto data = getData(type)) {
			data->repeatEnabled = !data->repeatEnabled;
			_repeatChangedNotifier.notify();
		}
	}

	bool isSeeking(AudioMsgId::Type type) const {
		if (auto data = getData(type)) {
			return (data->seeking == data->current);
		}
		return false;
	}
	void startSeeking(AudioMsgId::Type type);
	void stopSeeking(AudioMsgId::Type type);

	QList<FullMsgId> playlist(AudioMsgId::Type type) const {
		if (auto data = getData(type)) {
			return data->playlist;
		}
		return QList<FullMsgId>();
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

	struct Data {
		Data(AudioMsgId::Type type, MediaOverviewType overview) : type(type), overview(overview) {
		}

		AudioMsgId::Type type;
		MediaOverviewType overview;
		AudioMsgId current;
		AudioMsgId seeking;
		History *history = nullptr;
		History *migrated = nullptr;
		bool repeatEnabled = false;
		QList<FullMsgId> playlist;
		bool isPlaying = false;
	};

	AudioMsgId::Type getActiveType() const;

	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);
	void handleSongUpdate(const AudioMsgId &audioId);

	void checkPeerUpdate(AudioMsgId::Type type, const Notify::PeerUpdate &update);
	void setCurrent(const AudioMsgId &audioId);
	void rebuildPlaylist(Data *data);
	void moveInPlaylist(Data *data, int delta);
	void preloadNext(Data *data);
	void handleLogout();

	template <typename CheckCallback>
	void emitUpdate(AudioMsgId::Type type, CheckCallback check);

	Data *getData(AudioMsgId::Type type) {
		if (type == AudioMsgId::Type::Song) {
			return &_songData;
		} else if (type == AudioMsgId::Type::Voice) {
			return &_voiceData;
		}
		return nullptr;
	}

	const Data *getData(AudioMsgId::Type type) const {
		if (type == AudioMsgId::Type::Song) {
			return &_songData;
		} else if (type == AudioMsgId::Type::Voice) {
			return &_voiceData;
		}
		return nullptr;
	}

	Data _songData;
	Data _voiceData;

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
