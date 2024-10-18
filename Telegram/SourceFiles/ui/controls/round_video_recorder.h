/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "ui/effects/animations.h"

#include <crl/crl_object_on_queue.h>

namespace Media::Capture {
struct Chunk;
struct Update;
enum class Error : uchar;
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

class RoundVideoRecorder final : public base::has_weak_ptr {
public:
	explicit RoundVideoRecorder(RoundVideoRecorderDescriptor &&descriptor);
	~RoundVideoRecorder();

	[[nodiscard]] Fn<void(Media::Capture::Chunk)> audioChunkProcessor();

	void setPaused(bool paused);
	void hide(Fn<void(RoundVideoResult)> done = nullptr);

	using Update = Media::Capture::Update;
	using Error = Media::Capture::Error;
	[[nodiscard]] rpl::producer<Update, Error> updated();

private:
	class Private;

	void setup();
	void prepareFrame();
	void createImages();
	void progressTo(float64 progress);
	void fade(bool visible);

	const RoundVideoRecorderDescriptor _descriptor;
	std::unique_ptr<RpWidget> _preview;
	crl::object_on_queue<Private> _private;
	Ui::Animations::Simple _progressAnimation;
	Ui::Animations::Simple _fadeAnimation;
	Ui::Animations::Simple _fadeContentAnimation;
	float64 _progress = 0.;
	QImage _frameOriginal;
	QImage _framePlaceholder;
	QImage _framePrepared;
	QImage _shadow;
	int _lastAddedIndex = 0;
	int _preparedIndex = 0;
	int _side = 0;
	int _progressStroke = 0;
	int _extent = 0;
	bool _visible = false;
	bool _paused = false;

};

} // namespace Ui
