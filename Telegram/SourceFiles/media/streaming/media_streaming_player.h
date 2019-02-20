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

namespace Data {
class Session;
} // namespace Data

namespace Media {
namespace Streaming {

class Loader;
class File;
class AudioTrack;
class VideoTrack;

class Player final : private FileDelegate {
public:
	// Public interfaces is used from the main thread.
	Player(not_null<Data::Session*> owner, std::unique_ptr<Loader> loader);

	// Because we remember 'this' in calls to crl::on_main.
	Player(const Player &other) = delete;
	Player &operator=(const Player &other) = delete;

	void init(Mode mode, crl::time position);
	void start();
	void pause();
	void resume();
	void stop();

	[[nodiscard]] bool failed() const;
	[[nodiscard]] bool playing() const;
	[[nodiscard]] bool paused() const;

	[[nodiscard]] rpl::producer<Update, Error> updates() const;

	[[nodiscard]] QImage frame(const FrameRequest &request) const;

	[[nodiscard]] rpl::lifetime &lifetime();

	~Player();

private:
	enum class Stage {
		Uninitialized,
		Initializing,
		Ready,
		Started,
		Failed
	};

	// Thread-safe.
	not_null<FileDelegate*> delegate();

	// FileDelegate methods are called only from the File thread.
	void fileReady(Stream &&video, Stream &&audio) override;
	void fileError() override;
	bool fileProcessPacket(Packet &&packet) override;
	bool fileReadMore() override;

	// Called from the main thread.
	void streamReady(Information &&information);
	void streamFailed();
	void provideStartInformation();
	void fail();
	void checkNextFrame();
	void renderFrame(crl::time now);
	void audioReceivedTill(crl::time position);
	void audioPlayedTill(crl::time position);
	void videoReceivedTill(crl::time position);
	void videoPlayedTill(crl::time position);

	template <typename Track>
	void trackReceivedTill(
		const Track &track,
		TrackState &state,
		crl::time position);

	template <typename Track>
	void trackPlayedTill(
		const Track &track,
		TrackState &state,
		crl::time position);

	const std::unique_ptr<File> _file;

	static constexpr auto kReceivedTillEnd
		= std::numeric_limits<crl::time>::max();

	// Immutable while File is active.
	std::unique_ptr<AudioTrack> _audio;
	std::unique_ptr<VideoTrack> _video;
	base::has_weak_ptr _sessionGuard;
	Mode _mode = Mode::Both;

	// Belongs to the File thread while File is active.
	bool _readTillEnd = false;

	// Belongs to the main thread.
	Information _information;
	Stage _stage = Stage::Uninitialized;
	bool _paused = false;

	crl::time _nextFrameTime = kTimeUnknown;
	base::Timer _renderFrameTimer;
	rpl::event_stream<Update, Error> _updates;
	rpl::lifetime _lifetime;

};

} // namespace Streaming
} // namespace Media
