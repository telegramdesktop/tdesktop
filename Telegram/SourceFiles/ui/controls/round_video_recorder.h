/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/crl_object_on_queue.h>

namespace Media::Capture {
struct Chunk;
} // namespace Media::Capture

namespace tgcalls {
class VideoCaptureInterface;
} // namespace tgcalls

namespace Webrtc {
class VideoTrack;
} // namespace Webrtc

namespace Ui {

class RpWidget;
class RoundVideoRecorder;

struct RoundVideoRecorderDescriptor {
	not_null<RpWidget*> container;
	Fn<void(not_null<RoundVideoRecorder*>)> hidden;
	std::shared_ptr<tgcalls::VideoCaptureInterface> capturer;
	std::shared_ptr<Webrtc::VideoTrack> track;
};

struct RoundVideoResult {
	QByteArray content;
	QByteArray waveform;
	crl::time duration = 0;
};

class RoundVideoRecorder final {
public:
	explicit RoundVideoRecorder(RoundVideoRecorderDescriptor &&descriptor);
	~RoundVideoRecorder();

	[[nodiscard]] Fn<void(Media::Capture::Chunk)> audioChunkProcessor();

	void setPaused(bool paused);
	void hide(Fn<void(RoundVideoResult)> done = nullptr);

private:
	class Private;

	void setup();

	const RoundVideoRecorderDescriptor _descriptor;
	std::unique_ptr<RpWidget> _preview;
	crl::object_on_queue<Private> _private;
	int _lastAddedIndex = 0;
	bool _paused = false;

};

} // namespace Ui
