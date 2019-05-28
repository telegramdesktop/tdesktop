/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/weak_ptr.h"
#include "lottie/lottie_common.h"
#include "bmscene.h"

#include <QImage>
#include <crl/crl_time.h>
#include <crl/crl_object_on_queue.h>
#include <limits>

class BMBase;
class QImage;

namespace Lottie {

class Animation;
class JsonObject;

struct Frame {
	QImage original;
	crl::time position = kTimeUnknown;
	crl::time displayed = kTimeUnknown;
	crl::time display = kTimeUnknown;

	FrameRequest request = FrameRequest::NonStrict();
	QImage prepared;
};

QImage PrepareFrameByRequest(
	not_null<Frame*> frame,
	bool useExistingPrepared);

class SharedState {
public:
	explicit SharedState(const JsonObject &definition);

	void start(not_null<Animation*> owner, crl::time now);

	[[nodiscard]] Information information() const;
	[[nodiscard]] bool initialized() const;

	[[nodiscard]] not_null<Frame*> frameForPaint();
	[[nodiscard]] crl::time nextFrameDisplayTime() const;
	crl::time markFrameDisplayed(crl::time now);
	crl::time markFrameShown();

	void renderFrame(QImage &image, const FrameRequest &request, int index);
	[[nodiscard]] bool renderNextFrame(const FrameRequest &request);

private:
	void init(QImage cover);
	void renderNextFrame(
		not_null<Frame*> frame,
		const FrameRequest &request);
	[[nodiscard]] not_null<Frame*> getFrame(int index);
	[[nodiscard]] not_null<const Frame*> getFrame(int index) const;
	[[nodiscard]] int counter() const;

	BMScene _scene;

	static constexpr auto kCounterUninitialized = -1;
	std::atomic<int> _counter = kCounterUninitialized;

	static constexpr auto kFramesCount = 4;
	std::array<Frame, kFramesCount> _frames;

	base::weak_ptr<Animation> _owner;
	crl::time _started = kTimeUnknown;
	crl::time _duration = kTimeUnknown;
	int _frameIndex = 0;
	int _framesCount = 0;
	int _frameRate;
	std::atomic<int> _accumulatedDelayMs = 0;

};

class FrameRendererObject;

class FrameRenderer final {
public:
	static std::shared_ptr<FrameRenderer> Instance();

	void append(std::unique_ptr<SharedState> entry);
	void updateFrameRequest(
		not_null<SharedState*> entry,
		const FrameRequest &request);
	void frameShown(not_null<SharedState*> entry);
	void remove(not_null<SharedState*> state);

private:
	using Implementation = FrameRendererObject;
	crl::object_on_queue<Implementation> _wrapped;

};

} // namespace Lottie
