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

#include <QImage>
#include <QSize>
#include <crl/crl_time.h>
#include <crl/crl_object_on_queue.h>
#include <limits>

namespace rlottie {
class Animation;
} // namespace rlottie

namespace Lottie {

inline constexpr auto kMaxFrameRate = 120;
inline constexpr auto kMaxSize = 3096;
inline constexpr auto kMaxFramesCount = 600;
inline constexpr auto kFrameDisplayTimeAlreadyDone
	= std::numeric_limits<crl::time>::max();
inline constexpr auto kDisplayedInitial = crl::time(-1);

class Player;
class Cache;

struct Frame {
	QImage original;
	crl::time displayed = kDisplayedInitial;
	crl::time display = kTimeUnknown;
	int index = 0;

	FrameRequest request;
	QImage prepared;
};

QImage PrepareFrameByRequest(
	not_null<Frame*> frame,
	bool useExistingPrepared);

class SharedState {
public:
	SharedState(
		std::unique_ptr<rlottie::Animation> animation,
		const FrameRequest &request,
		Quality quality);
	SharedState(
		const QByteArray &content,
		std::unique_ptr<rlottie::Animation> animation,
		std::unique_ptr<Cache> cache,
		const FrameRequest &request,
		Quality quality);

	void start(
		not_null<Player*> owner,
		crl::time now,
		crl::time delay = 0,
		int skippedFrames = 0);

	[[nodiscard]] Information information() const;
	[[nodiscard]] bool initialized() const;

	[[nodiscard]] not_null<Frame*> frameForPaint();
	[[nodiscard]] crl::time nextFrameDisplayTime() const;
	void addTimelineDelay(crl::time delayed, int skippedFrames = 0);
	void markFrameDisplayed(crl::time now);
	bool markFrameShown();

	void renderFrame(QImage &image, const FrameRequest &request, int index);

	struct RenderResult {
		bool rendered = false;
		base::weak_ptr<Player> notify;
	};
	[[nodiscard]] RenderResult renderNextFrame(const FrameRequest &request);

	~SharedState();

private:
	void construct(const FrameRequest &request);
	void calculateProperties();
	bool isValid() const;
	void init(QImage cover, const FrameRequest &request);
	void renderNextFrame(
		not_null<Frame*> frame,
		const FrameRequest &request);
	[[nodiscard]] crl::time countFrameDisplayTime(int index) const;
	[[nodiscard]] not_null<Frame*> getFrame(int index);
	[[nodiscard]] not_null<const Frame*> getFrame(int index) const;
	[[nodiscard]] int counter() const;

	QByteArray _content;
	std::unique_ptr<rlottie::Animation> _animation;
	Quality _quality = Quality::Default;

	// crl::queue changes 0,2,4,6 to 1,3,5,7.
	// main thread changes 1,3,5,7 to 2,4,6,0.
	static constexpr auto kCounterUninitialized = -1;
	std::atomic<int> _counter = kCounterUninitialized;

	static constexpr auto kFramesCount = 4;
	std::array<Frame, kFramesCount> _frames;

	base::weak_ptr<Player> _owner;
	crl::time _started = kTimeUnknown;

	// (_counter % 2) == 1 main thread can write _delay.
	// (_counter % 2) == 0 crl::queue can read _delay.
	crl::time _delay = kTimeUnknown;

	int _frameIndex = 0;
	int _skippedFrames = 0;
	int _framesCount = 0;
	int _frameRate = 0;
	QSize _size;

	std::unique_ptr<Cache> _cache;

};

class FrameRendererObject;

class FrameRenderer final {
public:
	static std::shared_ptr<FrameRenderer> CreateIndependent();
	static std::shared_ptr<FrameRenderer> Instance();

	void append(
		std::unique_ptr<SharedState> entry,
		const FrameRequest &request);

	void updateFrameRequest(
		not_null<SharedState*> entry,
		const FrameRequest &request);
	void frameShown();
	void remove(not_null<SharedState*> state);

private:
	using Implementation = FrameRendererObject;
	crl::object_on_queue<Implementation> _wrapped;

};

} // namespace Lottie
