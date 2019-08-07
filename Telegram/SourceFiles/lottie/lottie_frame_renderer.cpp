/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_frame_renderer.h"

#include "lottie/lottie_player.h"
#include "lottie/lottie_animation.h"
#include "lottie/lottie_cache.h"
#include "base/flat_map.h"
#include "logs.h"

#include <QPainter>
#include <rlottie.h>
#include <range/v3/algorithm/find.hpp>
#include <range/v3/algorithm/count_if.hpp>

namespace Images {
QImage prepareColored(QColor add, QImage image);
} // namespace Images

namespace Lottie {
namespace {

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

int GetLottieFrameRate(not_null<rlottie::Animation*> animation, Quality quality) {
	const auto rate = int(qRound(animation->frameRate()));
	return (quality == Quality::Default && rate == 60) ? (rate / 2) : rate;
}

int GetLottieFramesCount(not_null<rlottie::Animation*> animation, Quality quality) {
	const auto rate = int(qRound(animation->frameRate()));
	const auto count = int(animation->totalFrame());
	return (quality == Quality::Default && rate == 60) ? (count / 2) : count;
}

int GetLottieFrameIndex(not_null<rlottie::Animation*> animation, Quality quality, int index) {
	const auto rate = int(qRound(animation->frameRate()));
	return (quality == Quality::Default && rate == 60) ? (index * 2) : index;
}

} // namespace

class FrameRendererObject final {
public:
	explicit FrameRendererObject(
		crl::weak_on_queue<FrameRendererObject> weak);

	void append(
		std::unique_ptr<SharedState> entry,
		const FrameRequest &request);
	void frameShown();
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
	if (request.box.isEmpty()) {
		return true;
	} else if (request.colored.has_value()) {
		return false;
	}
	const auto size = image.size();
	return (request.box.width() == size.width())
		|| (request.box.height() == size.height());
}

[[nodiscard]] QImage PrepareByRequest(
		const QImage &original,
		const FrameRequest &request,
		QImage storage) {
	Expects(!request.box.isEmpty());

	const auto size = request.size(original.size());
	if (!GoodStorageForFrame(storage, size)) {
		storage = CreateFrameStorage(size);
	}
	storage.fill(Qt::transparent);

	{
		QPainter p(&storage);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.drawImage(QRect(QPoint(), size), original);
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

void FrameRendererObject::append(
		std::unique_ptr<SharedState> state,
		const FrameRequest &request) {
	_entries.push_back({ std::move(state), request });
	queueGenerateFrames();
}

void FrameRendererObject::frameShown() {
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
	auto players = base::flat_map<Player*, base::weak_ptr<Player>>();
	const auto renderOne = [&](const Entry &entry) {
		const auto result = entry.state->renderNextFrame(entry.request);
		if (const auto player = result.notify.get()) {
			players.emplace(player, result.notify);
		}
		return result.rendered;
	};
	const auto rendered = ranges::count_if(_entries, renderOne);
	if (rendered) {
		if (!players.empty()) {
			crl::on_main([players = std::move(players)] {
				for (const auto &[player, weak] : players) {
					if (weak) {
						weak->checkStep();
					}
				}
			});
		}
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


Information SharedState::CalculateInformation(
		Quality quality,
		rlottie::Animation *animation,
		Cache *cache) {
	Expects(animation != nullptr || cache != nullptr);

	auto width = size_t(0);
	auto height = size_t(0);
	if (animation) {
		animation->size(width, height);
	} else {
		width = cache->originalSize().width();
		height = cache->originalSize().height();
	}
	const auto rate = animation
		? GetLottieFrameRate(animation, quality)
		: cache->frameRate();
	const auto count = animation
		? GetLottieFramesCount(animation, quality)
		: cache->framesCount();

	auto result = Information();
	result.size = QSize(
		(width > 0 && width <= kMaxSize) ? int(width) : 0,
		(height > 0 && height <= kMaxSize) ? int(height) : 0);
	result.frameRate = (rate > 0 && rate <= kMaxFrameRate) ? int(rate) : 0;
	result.framesCount = (count > 0 && count <= kMaxFramesCount)
		? int(count)
		: 0;
	return result;
}

SharedState::SharedState(
	std::unique_ptr<rlottie::Animation> animation,
	const FrameRequest &request,
	Quality quality)
: _info(CalculateInformation(quality, animation.get(), nullptr))
, _quality(quality)
, _animation(std::move(animation)) {
	construct(request);
}

SharedState::SharedState(
	const QByteArray &content,
	const ColorReplacements *replacements,
	std::unique_ptr<rlottie::Animation> animation,
	std::unique_ptr<Cache> cache,
	const FrameRequest &request,
	Quality quality)
: _info(CalculateInformation(quality, animation.get(), cache.get()))
, _quality(quality)
, _cache(std::move(cache))
, _animation(std::move(animation))
, _content(content)
, _replacements(replacements) {
	construct(request);
}

void SharedState::construct(const FrameRequest &request) {
	if (!isValid()) {
		return;
	}
	auto cover = _cache ? _cache->takeFirstFrame() : QImage();
	if (!cover.isNull()) {
		init(std::move(cover), request);
		return;
	}
	if (_cache) {
		_cache->init(
			_info.size,
			_info.frameRate,
			_info.framesCount,
			request);
	}
	renderFrame(cover, request, 0);
	init(std::move(cover), request);
}

bool SharedState::isValid() const {
	return (_info.framesCount > 0)
		&& (_info.frameRate > 0)
		&& !_info.size.isEmpty();
}

void SharedState::renderFrame(
		QImage &image,
		const FrameRequest &request,
		int index) {
	if (!isValid()) {
		return;
	}

	const auto size = request.box.isEmpty()
		? _info.size
		: request.size(_info.size);
	if (!GoodStorageForFrame(image, size)) {
		image = CreateFrameStorage(size);
	}
	if (_cache && _cache->renderFrame(image, request, index)) {
		return;
	} else if (!_animation) {
		_animation = details::CreateFromContent(_content, _replacements);
	}

	image.fill(Qt::transparent);
	auto surface = rlottie::Surface(
		reinterpret_cast<uint32_t*>(image.bits()),
		image.width(),
		image.height(),
		image.bytesPerLine());
	_animation->renderSync(
		GetLottieFrameIndex(_animation.get(), _quality, index),
		surface);
	if (_cache) {
		_cache->appendFrame(image, request, index);
		if (_cache->framesReady() == _cache->framesCount()) {
			_animation = nullptr;
		}
	}
}

void SharedState::init(QImage cover, const FrameRequest &request) {
	Expects(!initialized());

	_frames[0].request = request;
	_frames[0].original = std::move(cover);
}

void SharedState::start(
		not_null<Player*> owner,
		crl::time started,
		crl::time delay,
		int skippedFrames) {
	_owner = owner;
	_started = started;
	_delay = delay;
	_skippedFrames = skippedFrames;
	_counter.store(0, std::memory_order_release);
}

bool IsRendered(not_null<const Frame*> frame) {
	return (frame->displayed == kTimeUnknown);
}

void SharedState::renderNextFrame(
		not_null<Frame*> frame,
		const FrameRequest &request) {
	Expects(_info.framesCount > 0);

	renderFrame(
		frame->original,
		request,
		(++_frameIndex) % _info.framesCount);
	frame->request = request;
	PrepareFrameByRequest(frame);
	frame->index = _frameIndex;
	frame->displayed = kTimeUnknown;
}

auto SharedState::renderNextFrame(const FrameRequest &request)
-> RenderResult {
	const auto prerender = [&](int index) -> RenderResult {
		const auto frame = getFrame(index);
		const auto next = getFrame((index + 1) % kFramesCount);
		if (!IsRendered(frame)) {
			renderNextFrame(frame, request);
			return { true };
		} else if (!IsRendered(next)) {
			renderNextFrame(next, request);
			return { true };
		}
		return { false };
	};
	const auto present = [&](int counter, int index) -> RenderResult {
		const auto frame = getFrame(index);
		if (!IsRendered(frame)) {
			renderNextFrame(frame, request);
		}
		frame->display = countFrameDisplayTime(frame->index);

		// Release this frame to the main thread for rendering.
		_counter.store(
			(counter + 1) % (2 * kFramesCount),
			std::memory_order_release);
		return { true, _owner };
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
	Unexpected("Counter value in Lottie::SharedState::renderNextFrame.");
}

crl::time SharedState::countFrameDisplayTime(int index) const {
	return _started
		+ _delay
		+ crl::time(1000) * (_skippedFrames + index) / _info.frameRate;
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
	return isValid() ? _info : Information();
}

not_null<Frame*> SharedState::frameForPaint() {
	const auto result = getFrame(counter() / 2);
	Assert(!result->original.isNull());
	Assert(result->displayed != kTimeUnknown);

	return result;
}

int SharedState::framesCount() const {
	return _info.framesCount;
}

crl::time SharedState::nextFrameDisplayTime() const {
	const auto frameDisplayTime = [&](int counter) {
		const auto next = (counter + 1) % (2 * kFramesCount);
		const auto index = next / 2;
		const auto frame = getFrame(index);
		if (frame->displayed != kTimeUnknown) {
			// Frame already displayed, but not yet shown.
			return kFrameDisplayTimeAlreadyDone;
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

void SharedState::addTimelineDelay(crl::time delayed, int skippedFrames) {
	if (!delayed && !skippedFrames) {
		return;
	}

	const auto recountCurrentFrame = [&](int counter) {
		_delay += delayed;
		_skippedFrames += skippedFrames;

		const auto next = (counter + 1) % (2 * kFramesCount);
		const auto index = next / 2;
		const auto frame = getFrame(index);
		if (frame->displayed != kTimeUnknown) {
			// Frame already displayed.
			return;
		}
		Assert(IsRendered(frame));
		Assert(frame->display != kTimeUnknown);
		frame->display = countFrameDisplayTime(frame->index);
	};

	switch (counter()) {
	case 0: Unexpected("Value 0 in SharedState::addTimelineDelay.");
	case 1: return recountCurrentFrame(1);
	case 2: Unexpected("Value 2 in SharedState::addTimelineDelay.");
	case 3: return recountCurrentFrame(3);
	case 4: Unexpected("Value 4 in SharedState::addTimelineDelay.");
	case 5: return recountCurrentFrame(5);
	case 6: Unexpected("Value 6 in SharedState::addTimelineDelay.");
	case 7: return recountCurrentFrame(7);
	}
	Unexpected("Counter value in VideoTrack::Shared::nextFrameDisplayTime.");
}

void SharedState::markFrameDisplayed(crl::time now) {
	const auto mark = [&](int counter) {
		const auto next = (counter + 1) % (2 * kFramesCount);
		const auto index = next / 2;
		const auto frame = getFrame(index);
		if (frame->displayed == kTimeUnknown) {
			frame->displayed = now;
		}
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

bool SharedState::markFrameShown() {
	const auto jump = [&](int counter) {
		const auto next = (counter + 1) % (2 * kFramesCount);
		const auto index = next / 2;
		const auto frame = getFrame(index);
		if (frame->displayed == kTimeUnknown) {
			return false;
		}
		_counter.store(
			next,
			std::memory_order_release);
		return true;
	};

	switch (counter()) {
	case 0: return false;
	case 1: return jump(1);
	case 2: return false;
	case 3: return jump(3);
	case 4: return false;
	case 5: return jump(5);
	case 6: return false;
	case 7: return jump(7);
	}
	Unexpected("Counter value in Lottie::SharedState::markFrameShown.");
}

SharedState::~SharedState() = default;

std::shared_ptr<FrameRenderer> FrameRenderer::CreateIndependent() {
	return std::make_shared<FrameRenderer>();
}

std::shared_ptr<FrameRenderer> FrameRenderer::Instance() {
	if (auto result = GlobalInstance.lock()) {
		return result;
	}
	auto result = CreateIndependent();
	GlobalInstance = result;
	return result;
}

void FrameRenderer::append(
		std::unique_ptr<SharedState> entry,
		const FrameRequest &request) {
	_wrapped.with([=, entry = std::move(entry)](
			FrameRendererObject &unwrapped) mutable {
		unwrapped.append(std::move(entry), request);
	});
}

void FrameRenderer::frameShown() {
	_wrapped.with([=](FrameRendererObject &unwrapped) {
		unwrapped.frameShown();
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
