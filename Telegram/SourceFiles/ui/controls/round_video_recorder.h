/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "ui/effects/animations.h"
#include "ui/effects/path_shift_gradient.h"

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
class DynamicImage;
class RoundVideoRecorder;

struct RoundVideoRecorderDescriptor {
	not_null<RpWidget*> container;
	Fn<void(not_null<RoundVideoRecorder*>)> hiding;
	Fn<void(not_null<RoundVideoRecorder*>)> hidden;
	std::shared_ptr<tgcalls::VideoCaptureInterface> capturer;
	std::shared_ptr<Webrtc::VideoTrack> track;
	QImage placeholder;
};

struct RoundVideoResult {
	QByteArray content;
	QVector<signed char> waveform;
	crl::time duration = 0;
	QImage minithumbs;
	int minithumbsCount = 0;
	int minithumbSize = 0;
};

struct RoundVideoPartial {
	RoundVideoResult video;
	crl::time from = 0;
	crl::time till = 0;
};

class RoundVideoRecorder final : public base::has_weak_ptr {
public:
	explicit RoundVideoRecorder(RoundVideoRecorderDescriptor &&descriptor);
	~RoundVideoRecorder();

	[[nodiscard]] int previewSize() const;
	[[nodiscard]] Fn<void(Media::Capture::Chunk)> audioChunkProcessor();
	[[nodiscard]] rpl::producer<QImage> placeholderUpdates() const;

	void pause(Fn<void(RoundVideoResult)> done = nullptr);
	void resume(RoundVideoPartial partial);
	void hide(Fn<void(RoundVideoResult)> done = nullptr);

	void showPreview(
		std::shared_ptr<Ui::DynamicImage> silent,
		std::shared_ptr<Ui::DynamicImage> sounded);

	using Update = Media::Capture::Update;
	using Error = Media::Capture::Error;
	[[nodiscard]] rpl::producer<Update, Error> updated();

private:
	class Private;
	struct PreviewFrame {
		QImage image;
		bool silent = false;
	};

	void setup();
	void prepareFrame(bool blurred = false);
	void preparePlaceholder(const QImage &placeholder);
	void createImages();
	void progressTo(float64 progress);
	void fade(bool visible);

	[[nodiscard]] Fn<void()> updater() const;
	[[nodiscard]] PreviewFrame lookupPreviewFrame() const;

	const RoundVideoRecorderDescriptor _descriptor;
	style::owned_color _gradientBg;
	style::owned_color _gradientFg;
	PathShiftGradient _gradient;
	std::unique_ptr<RpWidget> _preview;
	crl::object_on_queue<Private> _private;
	Ui::Animations::Simple _progressAnimation;
	Ui::Animations::Simple _fadeAnimation;
	Ui::Animations::Simple _fadeContentAnimation;
	rpl::event_stream<QImage> _placeholderUpdates;

	std::shared_ptr<Ui::DynamicImage> _silentPreview;
	std::shared_ptr<Ui::DynamicImage> _soundedPreview;
	Ui::Animations::Simple _fadePreviewAnimation;
	PreviewFrame _cachedPreviewFrame;

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
	int _skipFrames = 0;
	bool _progressReceived = false;
	bool _visible = false;
	bool _paused = false;

};

} // namespace Ui
