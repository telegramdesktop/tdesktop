/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_common.h"
#include "media/streaming/media_streaming_file_delegate.h"
#include "base/weak_ptr.h"
#include "base/timer.h"

namespace Media {
namespace Player {
struct TrackState;
} // namespace Player
} // namespace Media

namespace Media {
namespace Streaming {

class Reader;
class File;
class AudioTrack;
class VideoTrack;
class Instance;

class Player final : private FileDelegate {
public:
	// Public interfaces is used from the main thread.
	explicit Player(std::shared_ptr<Reader> reader);

	// Because we remember 'this' in calls to crl::on_main.
	Player(const Player &other) = delete;
	Player &operator=(const Player &other) = delete;

	void play(const PlaybackOptions &options);
	void pause();
	void resume();
	void stop();

	// Allow to irreversibly stop only audio track.
	void stopAudio();

	[[nodiscard]] bool active() const;
	[[nodiscard]] bool ready() const;

	[[nodiscard]] float64 speed() const;
	void setSpeed(float64 speed); // 0.5 <= speed <= 2.
	void setWaitForMarkAsShown(bool wait);

	[[nodiscard]] bool playing() const;
	[[nodiscard]] bool buffering() const;
	[[nodiscard]] bool paused() const;
	[[nodiscard]] std::optional<Error> failed() const;
	[[nodiscard]] bool finished() const;

	[[nodiscard]] rpl::producer<Update, Error> updates() const;
	[[nodiscard]] rpl::producer<bool> fullInCache() const;

	[[nodiscard]] QSize videoSize() const;
	[[nodiscard]] QImage frame(
		const FrameRequest &request,
		const Instance *instance = nullptr) const;

	[[nodiscard]] FrameWithInfo frameWithInfo(
		const Instance *instance = nullptr) const; // !requireARGB32

	[[nodiscard]] QImage currentFrameImage() const; // Converts if needed.

	void unregisterInstance(not_null<const Instance*> instance);
	bool markFrameShown();

	void setLoaderPriority(int priority);

	[[nodiscard]] Media::Player::TrackState prepareLegacyState() const;

	void lock();
	void unlock();
	[[nodiscard]] bool locked() const;

	[[nodiscard]] rpl::lifetime &lifetime();

	~Player();

private:
	enum class Stage {
		Uninitialized,
		Initializing,
		Ready,
		Started,
	};

	// Thread-safe.
	not_null<FileDelegate*> delegate();

	// FileDelegate methods are called only from the File thread.
	bool fileReady(int headerSize, Stream &&video, Stream &&audio) override;
	void fileError(Error error) override;
	void fileWaitingForData() override;
	void fileFullInCache(bool fullInCache) override;
	bool fileProcessPackets(
		base::flat_map<int, std::vector<FFmpeg::Packet>> &packets) override;
	void fileProcessEndOfFile() override;
	bool fileReadMore() override;

	// Called from the main thread.
	void streamReady(Information &&information);
	void streamFailed(Error error);
	void start();
	void stop(bool stillActive);
	void provideStartInformation();
	void fail(Error error);
	void checkVideoStep();
	void checkNextFrameRender();
	void checkNextFrameAvailability();
	void renderFrame(crl::time now);
	void audioReceivedTill(crl::time position);
	void audioPlayedTill(crl::time position);
	void videoReceivedTill(crl::time position);
	void videoPlayedTill(crl::time position);

	void updatePausedState();
	[[nodiscard]] bool trackReceivedEnough(
		const TrackState &state,
		crl::time amount) const;
	[[nodiscard]] bool bothReceivedEnough(crl::time amount) const;
	[[nodiscard]] bool receivedTillEnd() const;
	void checkResumeFromWaitingForData();
	[[nodiscard]] crl::time getCurrentReceivedTill(crl::time duration) const;
	void savePreviousReceivedTill(
		const PlaybackOptions &options,
		crl::time previousReceivedTill);
	[[nodiscard]] crl::time loadInAdvanceFor() const;

	template <typename Track>
	int durationByPacket(const Track &track, const FFmpeg::Packet &packet);

	// Valid after fileReady call ends. Thread-safe.
	[[nodiscard]] crl::time computeAudioDuration() const;
	[[nodiscard]] crl::time computeVideoDuration() const;
	[[nodiscard]] crl::time computeTotalDuration() const;
	void setDurationByPackets();

	template <typename Track>
	void trackReceivedTill(
		const Track &track,
		TrackState &state,
		crl::time position);

	template <typename Track>
	void trackSendReceivedTill(
		const Track &track,
		TrackState &state);

	template <typename Track>
	void trackPlayedTill(
		const Track &track,
		TrackState &state,
		crl::time position);

	const std::unique_ptr<File> _file;

	// Immutable while File is active after it is ready.
	AudioMsgId _audioId;
	std::unique_ptr<AudioTrack> _audio;
	std::unique_ptr<VideoTrack> _video;

	// Immutable while File is active.
	base::has_weak_ptr _sessionGuard;

	// Immutable while File is active except '.speed'.
	// '.speed' is changed from the main thread.
	PlaybackOptions _options;

	// Belongs to the File thread while File is active.
	bool _readTillEnd = false;
	bool _waitingForData = false;

	std::atomic<bool> _pauseReading = false;

	// Belongs to the main thread.
	Information _information;
	Stage _stage = Stage::Uninitialized;
	std::optional<Error> _lastFailure;
	bool _pausedByUser = false;
	bool _pausedByWaitingForData = false;
	bool _paused = false;
	bool _audioFinished = false;
	bool _videoFinished = false;
	bool _remoteLoader = false;

	crl::time _startedTime = kTimeUnknown;
	crl::time _pausedTime = kTimeUnknown;
	crl::time _currentFrameTime = kTimeUnknown;
	crl::time _nextFrameTime = kTimeUnknown;
	base::Timer _renderFrameTimer;
	rpl::event_stream<Update, Error> _updates;
	rpl::event_stream<bool> _fullInCache;
	std::optional<bool> _fullInCacheSinceStart;

	crl::time _totalDuration = kTimeUnknown;
	crl::time _loopingShift = 0;
	crl::time _previousReceivedTill = kTimeUnknown;
	std::atomic<int> _durationByPackets = 0;
	int _durationByLastAudioPacket = 0;
	int _durationByLastVideoPacket = 0;

	int _locks = 0;

	rpl::lifetime _lifetime;
	rpl::lifetime _sessionLifetime;

};

} // namespace Streaming
} // namespace Media
