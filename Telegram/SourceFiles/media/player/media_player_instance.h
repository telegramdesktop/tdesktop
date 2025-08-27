/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_audio_msg_id.h"
#include "data/data_shared_media.h"

class AudioMsgId;
class DocumentData;
class History;

namespace Media {
enum class RepeatMode;
enum class OrderMode;
} // namespace Media

namespace Media {
namespace Audio {
class Instance;
} // namespace Audio
} // namespace Media

namespace Media {
namespace View {
class PlaybackProgress;
} // namespace View
} // namespace Media

namespace Media {
namespace Streaming {
class Document;
class Instance;
struct PlaybackOptions;
struct Update;
enum class Error;
} // namespace Streaming
} // namespace Media

namespace base {
class PowerSaveBlocker;
} // namespace base

namespace Media {
namespace Player {

extern const char kOptionDisableAutoplayNext[];

class Instance;
struct TrackState;

void start(not_null<Audio::Instance*> instance);
void finish(not_null<Audio::Instance*> instance);

void SaveLastPlaybackPosition(
	not_null<DocumentData*> document,
	const TrackState &state);

not_null<Instance*> instance();

class Instance final {
public:
	enum class Seeking {
		Start,
		Finish,
		Cancel,
	};

	void play(AudioMsgId::Type type);
	void pause(AudioMsgId::Type type);
	void stop(AudioMsgId::Type type, bool asFinished = false);
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
	void playPause(const AudioMsgId &audioId);
	[[nodiscard]] TrackState getState(AudioMsgId::Type type) const;

	[[nodiscard]] Streaming::Instance *roundVideoStreamed(
		HistoryItem *item) const;
	[[nodiscard]] View::PlaybackProgress *roundVideoPlayback(
		HistoryItem *item) const;

	[[nodiscard]] Streaming::Instance *roundVideoPreview(
		not_null<DocumentData*> document) const;

	[[nodiscard]] AudioMsgId current(AudioMsgId::Type type) const {
		if (const auto data = getData(type)) {
			return data->current;
		}
		return AudioMsgId();
	}

	[[nodiscard]] bool isSeeking(AudioMsgId::Type type) const {
		if (const auto data = getData(type)) {
			return (data->seeking == data->current);
		}
		return false;
	}
	void startSeeking(AudioMsgId::Type type);
	void finishSeeking(AudioMsgId::Type type, float64 progress);
	void cancelSeeking(AudioMsgId::Type type);

	void updateVoicePlaybackSpeed();

	[[nodiscard]] bool nextAvailable(AudioMsgId::Type type) const;
	[[nodiscard]] bool previousAvailable(AudioMsgId::Type type) const;

	struct Switch {
		AudioMsgId from;
		FullMsgId to;
	};

	[[nodiscard]] rpl::producer<Switch> switchToNextEvents() const {
		return _switchToNext.events();
	}
	[[nodiscard]] rpl::producer<AudioMsgId::Type> tracksFinished() const {
		return _tracksFinished.events();
	}
	[[nodiscard]] rpl::producer<AudioMsgId::Type> trackChanged() const {
		return _trackChanged.events();
	}

	[[nodiscard]] rpl::producer<> playlistChanges(
		AudioMsgId::Type type) const;

	[[nodiscard]] rpl::producer<TrackState> updatedNotifier() const {
		return _updatedNotifier.events();
	}

	[[nodiscard]] rpl::producer<> stops(AudioMsgId::Type type) const;
	[[nodiscard]] rpl::producer<> startsPlay(AudioMsgId::Type type) const;

	[[nodiscard]] rpl::producer<Seeking> seekingChanges(
		AudioMsgId::Type type) const;

	[[nodiscard]] rpl::producer<> closePlayerRequests() const {
		return _closePlayerRequests.events();
	}
	void stopAndClose();

private:
	using SharedMediaType = Storage::SharedMediaType;
	using SliceKey = SparseIdsMergedSlice::Key;
	struct Streamed;
	struct ShuffleData;
	struct Data {
		Data(AudioMsgId::Type type, SharedMediaType overview);
		Data(Data &&other);
		Data &operator=(Data &&other);
		~Data();

		AudioMsgId::Type type;
		Storage::SharedMediaType overview;
		AudioMsgId current;
		AudioMsgId seeking;
		std::optional<SparseIdsMergedSlice> playlistSlice;
		std::optional<SliceKey> playlistSliceKey;
		std::optional<SliceKey> playlistRequestedKey;
		std::optional<SparseIdsMergedSlice> playlistOtherSlice;
		std::optional<SliceKey> playlistOtherRequestedKey;
		std::optional<int> playlistIndex;
		rpl::lifetime playlistLifetime;
		rpl::lifetime playlistOtherLifetime;
		rpl::lifetime sessionLifetime;
		rpl::event_stream<> playlistChanges;
		History *history = nullptr;
		MsgId topicRootId = 0;
		PeerId monoforumPeerId = 0;
		History *migrated = nullptr;
		Main::Session *session = nullptr;
		bool isPlaying = false;
		bool resumeOnCallEnd = false;
		std::unique_ptr<Streamed> streamed;
		std::unique_ptr<ShuffleData> shuffleData;
		std::unique_ptr<base::PowerSaveBlocker> powerSaveBlocker;
		std::unique_ptr<base::PowerSaveBlocker> powerSaveBlockerVideo;
	};

	struct SeekingChanges {
		Seeking seeking;
		AudioMsgId::Type type;
	};

	Instance();
	~Instance();

	friend void start(not_null<Audio::Instance*> instance);
	friend void finish(not_null<Audio::Instance*> instance);

	void setupShortcuts();
	void playStreamed(
		const AudioMsgId &audioId,
		std::shared_ptr<Streaming::Document> shared);
	Streaming::PlaybackOptions streamingOptions(
		const AudioMsgId &audioId,
		crl::time position = -1);

	// Observed notifications.
	void handleSongUpdate(const AudioMsgId &audioId);

	void pauseOnCall(AudioMsgId::Type type);
	void resumeOnCall(AudioMsgId::Type type);

	void setCurrent(const AudioMsgId &audioId);
	void refreshPlaylist(not_null<Data*> data);
	void refreshOtherPlaylist(not_null<Data*> data);
	std::optional<SliceKey> playlistKey(not_null<const Data*> data) const;
	bool validPlaylist(not_null<const Data*> data) const;
	void validatePlaylist(not_null<Data*> data);
	std::optional<SliceKey> playlistOtherKey(
		not_null<const Data*> data) const;
	bool validOtherPlaylist(not_null<const Data*> data) const;
	void validateOtherPlaylist(not_null<Data*> data);
	void playlistUpdated(not_null<Data*> data);
	bool moveInPlaylist(not_null<Data*> data, int delta, bool autonext);
	void updatePowerSaveBlocker(
		not_null<Data*> data,
		const TrackState &state);
	HistoryItem *itemByIndex(not_null<Data*> data, int index);
	void stopAndClear(not_null<Data*> data);

	[[nodiscard]] MsgId computeCurrentUniversalId(
		not_null<const Data*> data) const;
	void validateShuffleData(not_null<Data*> data);
	void setupShuffleData(not_null<Data*> data);
	void ensureShuffleMove(not_null<Data*> data, int delta);

	void handleStreamingUpdate(
		not_null<Data*> data,
		Streaming::Update &&update);
	void handleStreamingError(
		not_null<Data*> data,
		Streaming::Error &&error);

	void clearStreamed(not_null<Data*> data, bool savePosition = true);
	void emitUpdate(AudioMsgId::Type type);
	template <typename CheckCallback>
	void emitUpdate(AudioMsgId::Type type, CheckCallback check);

	[[nodiscard]] RepeatMode repeat(not_null<const Data*> data) const;
	[[nodiscard]] rpl::producer<RepeatMode> repeatChanges(
		not_null<const Data*> data) const;
	[[nodiscard]] OrderMode order(not_null<const Data*> data) const;
	[[nodiscard]] rpl::producer<OrderMode> orderChanges(
		not_null<const Data*> data) const;

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

	HistoryItem *roundVideoItem() const;
	void requestRoundVideoResize() const;
	void requestRoundVideoRepaint() const;

	void setHistory(
		not_null<Data*> data,
		History *history,
		Main::Session *sessionFallback = nullptr);
	void setSession(not_null<Data*> data, Main::Session *session);

	Data _songData;
	Data _voiceData;
	bool _roundPlaying = false;

	rpl::event_stream<Switch> _switchToNext;
	rpl::event_stream<AudioMsgId::Type> _tracksFinished;
	rpl::event_stream<AudioMsgId::Type> _trackChanged;
	rpl::event_stream<AudioMsgId::Type> _playerStopped;
	rpl::event_stream<AudioMsgId::Type> _playerStartedPlay;
	rpl::event_stream<TrackState> _updatedNotifier;
	rpl::event_stream<SeekingChanges> _seekingChanges;
	rpl::event_stream<> _closePlayerRequests;
	rpl::lifetime _lifetime;

};

} // namespace Player
} // namespace Media
