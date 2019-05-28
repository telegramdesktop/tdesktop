/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_frame_renderer.h"

#include "lottie/lottie_animation.h"
#include "rasterrenderer/rasterrenderer.h"
#include "logs.h"

#include <range/v3/algorithm/find.hpp>
#include <range/v3/algorithm/count_if.hpp>
#include <QPainter>

namespace Images {
QImage prepareColored(QColor add, QImage image);
} // namespace Images

namespace Lottie {
namespace {

constexpr auto kDisplaySkipped = crl::time(-1);

std::weak_ptr<FrameRenderer> GlobalInstance;

constexpr auto kImageFormat = QImage::Format_ARGB32_Premultiplied;

bool GoodStorageForFrame(const QImage &storage, QSize size) {
	return !storage.isNull()
		&& (storage.format() == kImageFormat)
		&& (storage.size() == size)
		&& storage.isDetached();
}

QImage CreateFrameStorage(QSize size) {
	return QImage(size, kImageFormat);
}

} // namespace

class FrameRendererObject final {
public:
	explicit FrameRendererObject(
		crl::weak_on_queue<FrameRendererObject> weak);

	void append(std::unique_ptr<SharedState> entry);
	void frameShown(not_null<SharedState*> entry);
	void updateFrameRequest(
		not_null<SharedState*> entry,
		const FrameRequest &request);
	void remove(not_null<SharedState*> entry);

private:
	struct Entry {
		std::unique_ptr<SharedState> state;
		FrameRequest request;
	};

	static not_null<SharedState*> StateFromEntry(const Entry &entry) {
		return entry.state.get();
	}

	void queueGenerateFrames();
	void generateFrames();

	crl::weak_on_queue<FrameRendererObject> _weak;
	std::vector<Entry> _entries;
	bool _queued = false;

};

[[nodiscard]] bool GoodForRequest(
		const QImage &image,
		const FrameRequest &request) {
	if (request.resize.isEmpty()) {
		return true;
	} else if (request.colored.has_value()) {
		return false;
	}
	return (request.resize == image.size());
}

[[nodiscard]] QImage PrepareByRequest(
		const QImage &original,
		const FrameRequest &request,
		QImage storage) {
	Expects(!request.resize.isEmpty());

	if (!GoodStorageForFrame(storage, request.resize)) {
		storage = CreateFrameStorage(request.resize);
	}
	storage.fill(Qt::transparent);

	{
		QPainter p(&storage);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.drawImage(QRect(QPoint(), request.resize), original);
	}
	if (request.colored.has_value()) {
		storage = Images::prepareColored(*request.colored, std::move(storage));
	}
	return storage;
}

QImage PrepareFrameByRequest(
		not_null<Frame*> frame,
		bool useExistingPrepared = false) {
	Expects(!frame->original.isNull());

	if (GoodForRequest(frame->original, frame->request)) {
		return frame->original;
	} else if (frame->prepared.isNull() || !useExistingPrepared) {
		frame->prepared = PrepareByRequest(
			frame->original,
			frame->request,
			std::move(frame->prepared));
	}
	return frame->prepared;
}

FrameRendererObject::FrameRendererObject(
	crl::weak_on_queue<FrameRendererObject> weak)
: _weak(std::move(weak)) {
}

void FrameRendererObject::append(std::unique_ptr<SharedState> state) {
	_entries.push_back({ std::move(state) });
	queueGenerateFrames();
}

void FrameRendererObject::frameShown(not_null<SharedState*> entry) {
	queueGenerateFrames();
}

void FrameRendererObject::updateFrameRequest(
		not_null<SharedState*> entry,
		const FrameRequest &request) {
	const auto i = ranges::find(_entries, entry, &StateFromEntry);
	Assert(i != end(_entries));
	i->request = request;
}

void FrameRendererObject::remove(not_null<SharedState*> entry) {
	const auto i = ranges::find(_entries, entry, &StateFromEntry);
	Assert(i != end(_entries));
	_entries.erase(i);
}

void FrameRendererObject::generateFrames() {
	const auto renderOne = [&](const Entry & entry) {
		return entry.state->renderNextFrame(entry.request);
	};
	if (ranges::count_if(_entries, renderOne) > 0) {
		queueGenerateFrames();
	}
}

void FrameRendererObject::queueGenerateFrames() {
	if (_queued) {
		return;
	}
	_queued = true;
	_weak.with([](FrameRendererObject &that) {
		that._queued = false;
		that.generateFrames();
	});
}

SharedState::SharedState(const JsonObject &definition)
: _scene(definition) {
	if (_scene.endFrame() > _scene.startFrame()) {
		auto cover = QImage();
		renderFrame(cover, FrameRequest::NonStrict(), 0);
		init(std::move(cover));
	}
}

void SharedState::renderFrame(
		QImage &image,
		const FrameRequest &request,
		int index) {
	const auto realSize = QSize(_scene.width(), _scene.height());
	if (realSize.isEmpty() || _scene.endFrame() <= _scene.startFrame()) {
		return;
	}

	const auto size = request.resize.isEmpty() ? realSize : request.resize;
	if (!GoodStorageForFrame(image, size)) {
		image = CreateFrameStorage(size);
	}
	image.fill(Qt::transparent);

	QPainter p(&image);
	p.setRenderHints(QPainter::Antialiasing);
	p.setRenderHints(QPainter::SmoothPixmapTransform);
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setRenderHints(QPainter::HighQualityAntialiasing);
	if (realSize != size) {
		p.scale(
			size.width() / float64(realSize.width()),
			size.height() / float64(realSize.height()));
	}

	const auto frame = std::clamp(
		_scene.startFrame() + index,
		_scene.startFrame(),
		_scene.endFrame() - 1);
	_scene.updateProperties(frame);

	RasterRenderer renderer(&p);
	_scene.render(renderer, frame);
}

void SharedState::init(QImage cover) {
	Expects(!initialized());

	_frameRate = _scene.frameRate();
	_framesCount = _scene.endFrame() - _scene.startFrame();
	_duration = crl::time(1000) * _framesCount / _frameRate;

	_frames[0].original = std::move(cover);
	_frames[0].position = 0;

	// Usually main thread sets displayed time before _counter increment.
	// But in this case we update _counter, so we set a fake displayed time.
	_frames[0].displayed = kDisplaySkipped;

	_counter.store(0, std::memory_order_release);
}

void SharedState::start(not_null<Animation*> owner, crl::time now) {
	_owner = owner;
	_started = now;
}

bool IsRendered(not_null<const Frame*> frame) {
	return (frame->position != kTimeUnknown)
		&& (frame->displayed == kTimeUnknown);
}

void SharedState::renderNextFrame(
		not_null<Frame*> frame,
		const FrameRequest &request) {
	Expects(_framesCount > 0);

	renderFrame(frame->original, request, (++_frameIndex) % _framesCount);
	PrepareFrameByRequest(frame);
	frame->position = crl::time(1000) * _frameIndex / _frameRate;
	frame->displayed = kTimeUnknown;
}

bool SharedState::renderNextFrame(const FrameRequest &request) {
	const auto prerender = [&](int index) {
		const auto frame = getFrame(index);
		const auto next = getFrame((index + 1) % kFramesCount);
		if (!IsRendered(frame)) {
			renderNextFrame(frame, request);
			return true;
		} else if (!IsRendered(next)) {
			renderNextFrame(next, request);
			return true;
		}
		return false;
	};
	const auto present = [&](int counter, int index) {
		const auto frame = getFrame(index);
		if (!IsRendered(frame)) {
			renderNextFrame(frame, request);
		}
		frame->display = _started + _accumulatedDelayMs + frame->position;

		// Release this frame to the main thread for rendering.
		_counter.store(
			(counter + 1) % (2 * kFramesCount),
			std::memory_order_release);
		crl::on_main(_owner, [=] {
			_owner->checkStep();
		});
		return true;
	};

	switch (counter()) {
	case 0: return present(0, 1);
	case 1: return prerender(2);
	case 2: return present(2, 2);
	case 3: return prerender(3);
	case 4: return present(4, 3);
	case 5: return prerender(0);
	case 6: return present(6, 0);
	case 7: return prerender(1);
	}
	Unexpected("Counter value in VideoTrack::Shared::prepareState.");

}

int SharedState::counter() const {
	return _counter.load(std::memory_order_acquire);
}

bool SharedState::initialized() const {
	return (counter() != kCounterUninitialized);
}

not_null<Frame*> SharedState::getFrame(int index) {
	Expects(index >= 0 && index < kFramesCount);

	return &_frames[index];
}

not_null<const Frame*> SharedState::getFrame(int index) const {
	Expects(index >= 0 && index < kFramesCount);

	return &_frames[index];
}

Information SharedState::information() const {
	auto result = Information();
	result.frameRate = _scene.frameRate();
	result.size = QSize(_scene.width(), _scene.height());
	result.framesCount = _scene.endFrame() - _scene.startFrame();
	return result;
}

not_null<Frame*> SharedState::frameForPaint() {
	const auto result = getFrame(counter() / 2);
	Assert(!result->original.isNull());
	Assert(result->position != kTimeUnknown);
	Assert(result->displayed != kTimeUnknown);

	return result;
}

crl::time SharedState::nextFrameDisplayTime() const {
	const auto frameDisplayTime = [&](int counter) {
		const auto next = (counter + 1) % (2 * kFramesCount);
		const auto index = next / 2;
		const auto frame = getFrame(index);
		if (frame->displayed != kTimeUnknown) {
			// Frame already displayed, but not yet shown.
			return kTimeUnknown;
		}
		Assert(IsRendered(frame));
		Assert(frame->display != kTimeUnknown);

		return frame->display;
	};

	switch (counter()) {
	case 0: return kTimeUnknown;
	case 1: return frameDisplayTime(1);
	case 2: return kTimeUnknown;
	case 3: return frameDisplayTime(3);
	case 4: return kTimeUnknown;
	case 5: return frameDisplayTime(5);
	case 6: return kTimeUnknown;
	case 7: return frameDisplayTime(7);
	}
	Unexpected("Counter value in VideoTrack::Shared::nextFrameDisplayTime.");
}

crl::time SharedState::markFrameDisplayed(crl::time now) {
	const auto mark = [&](int counter) {
		const auto next = (counter + 1) % (2 * kFramesCount);
		const auto index = next / 2;
		const auto frame = getFrame(index);
		Assert(frame->position != kTimeUnknown);
		Assert(frame->displayed == kTimeUnknown);

		frame->displayed = now;
		_accumulatedDelayMs += (frame->displayed - frame->display);

		return frame->position;
	};

	switch (counter()) {
	case 0: Unexpected("Value 0 in SharedState::markFrameDisplayed.");
	case 1: return mark(1);
	case 2: Unexpected("Value 2 in SharedState::markFrameDisplayed.");
	case 3: return mark(3);
	case 4: Unexpected("Value 4 in SharedState::markFrameDisplayed.");
	case 5: return mark(5);
	case 6: Unexpected("Value 6 in SharedState::markFrameDisplayed.");
	case 7: return mark(7);
	}
	Unexpected("Counter value in Lottie::SharedState::markFrameDisplayed.");
}

crl::time SharedState::markFrameShown() {
	const auto jump = [&](int counter) {
		const auto next = (counter + 1) % (2 * kFramesCount);
		const auto index = next / 2;
		const auto frame = getFrame(index);
		Assert(frame->position != kTimeUnknown);
		if (frame->displayed == kTimeUnknown) {
			return kTimeUnknown;
		}
		_counter.store(
			next,
			std::memory_order_release);
		return frame->position;
	};

	switch (counter()) {
	case 0: return kTimeUnknown;
	case 1: return jump(1);
	case 2: return kTimeUnknown;
	case 3: return jump(3);
	case 4: return kTimeUnknown;
	case 5: return jump(5);
	case 6: return kTimeUnknown;
	case 7: return jump(7);
	}
	Unexpected("Counter value in Lottie::SharedState::markFrameShown.");
}

std::shared_ptr<FrameRenderer> FrameRenderer::Instance() {
	if (auto result = GlobalInstance.lock()) {
		return result;
	}
	auto result = std::make_shared<FrameRenderer>();
	GlobalInstance = result;
	return result;
}

void FrameRenderer::append(std::unique_ptr<SharedState> entry) {
	_wrapped.with([entry = std::move(entry)](
			FrameRendererObject &unwrapped) mutable {
		unwrapped.append(std::move(entry));
	});
}

void FrameRenderer::frameShown(not_null<SharedState*> entry) {
	_wrapped.with([=](FrameRendererObject &unwrapped) {
		unwrapped.frameShown(entry);
	});
}

void FrameRenderer::updateFrameRequest(
		not_null<SharedState*> entry,
		const FrameRequest &request) {
	_wrapped.with([=](FrameRendererObject &unwrapped) {
		unwrapped.updateFrameRequest(entry, request);
	});
}

void FrameRenderer::remove(not_null<SharedState*> entry) {
	_wrapped.with([=](FrameRendererObject &unwrapped) {
		unwrapped.remove(entry);
	});
}

} // namespace Lottie
