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

#include "data/data_shared_media.h"

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
	bool next(AudioMsgId::Type type);
	bool previous(AudioMsgId::Type type);

	AudioMsgId::Type getActiveType() const;

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
	bool next() {
		return next(getActiveType());
	}
	bool previous() {
		return previous(getActiveType());
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
			_repeatChangedNotifier.notify(type);
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

	bool nextAvailable(AudioMsgId::Type type) const;
	bool previousAvailable(AudioMsgId::Type type) const;

	struct Switch {
		AudioMsgId from;
		FullMsgId to;
	};

	base::Observable<Switch> &switchToNextNotifier() {
		return _switchToNextNotifier;
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
	base::Observable<AudioMsgId::Type> &tracksFinishedNotifier() {
		return _tracksFinishedNotifier;
	}
	base::Observable<AudioMsgId::Type> &trackChangedNotifier() {
		return _trackChangedNotifier;
	}
	base::Observable<AudioMsgId::Type> &repeatChangedNotifier() {
		return _repeatChangedNotifier;
	}

	rpl::producer<> playlistChanges(AudioMsgId::Type type) const;

	void documentLoadProgress(DocumentData *document);

	void clear();

private:
	Instance();
	friend void start();

	using SharedMediaType = Storage::SharedMediaType;
	using SliceKey = SparseIdsMergedSlice::Key;
	struct Data {
		Data(AudioMsgId::Type type, SharedMediaType overview)
		: type(type)
		, overview(overview) {
		}

		AudioMsgId::Type type;
		Storage::SharedMediaType overview;
		AudioMsgId current;
		AudioMsgId seeking;
		base::optional<SparseIdsMergedSlice> playlistSlice;
		base::optional<SliceKey> playlistSliceKey;
		base::optional<SliceKey> playlistRequestedKey;
		base::optional<int> playlistIndex;
		rpl::lifetime playlistLifetime;
		rpl::event_stream<> playlistChanges;
		History *history = nullptr;
		History *migrated = nullptr;
		bool repeatEnabled = false;
		bool isPlaying = false;
	};

	// Observed notifications.
	void handleSongUpdate(const AudioMsgId &audioId);

	void setCurrent(const AudioMsgId &audioId);
	void refreshPlaylist(not_null<Data*> data);
	base::optional<SliceKey> playlistKey(not_null<Data*> data) const;
	bool validPlaylist(not_null<Data*> data);
	void validatePlaylist(not_null<Data*> data);
	void playlistUpdated(not_null<Data*> data);
	bool moveInPlaylist(not_null<Data*> data, int delta, bool autonext);
	void preloadNext(not_null<Data*> data);
	HistoryItem *itemByIndex(not_null<Data*> data, int index);
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

	base::Observable<Switch> _switchToNextNotifier;
	base::Observable<bool> _usePanelPlayer;
	base::Observable<bool> _titleButtonOver;
	base::Observable<bool> _playerWidgetOver;
	base::Observable<TrackState> _updatedNotifier;
	base::Observable<AudioMsgId::Type> _tracksFinishedNotifier;
	base::Observable<AudioMsgId::Type> _trackChangedNotifier;
	base::Observable<AudioMsgId::Type> _repeatChangedNotifier;

};

} // namespace Clip
} // namespace Media
