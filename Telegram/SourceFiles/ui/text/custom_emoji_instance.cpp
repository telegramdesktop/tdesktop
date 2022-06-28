/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/custom_emoji_instance.h"

#include "ui/effects/frame_generator.h"

#include <crl/crl_async.h>

class QPainter;

namespace Ui::CustomEmoji {
namespace {

constexpr auto kMaxFrameDuration = 86400 * crl::time(1000);

struct CacheHelper {
	int version = 0;
	int size = 0;
	int frames = 0;
	int length = 0;
};

} // namespace

Preview::Preview(QPainterPath path, float64 scale)
: _data(ScaledPath{ std::move(path), scale }) {
}

Preview::Preview(QImage image) : _data(std::move(image)) {
}

void Preview::paint(QPainter &p, int x, int y, const QColor &preview) {
	if (const auto path = std::get_if<ScaledPath>(&_data)) {
		paintPath(p, x, y, preview, *path);
	} else if (const auto image = std::get_if<QImage>(&_data)) {
		p.drawImage(x, y, *image);
	}
}

bool Preview::image() const {
	return v::is<QImage>(_data);
}

void Preview::paintPath(
		QPainter &p,
		int x,
		int y,
		const QColor &preview,
		const ScaledPath &path) {
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(preview);
	p.setPen(Qt::NoPen);
	const auto scale = path.scale;
	const auto required = (scale != 1.);
	if (required) {
		p.save();
	}
	p.translate(x, y);
	if (required) {
		p.scale(scale, scale);
	}
	p.drawPath(path.path);
	if (required) {
		p.restore();
	} else {
		p.translate(-x, -y);
	}
}

Cache::Cache(int size) : _size(size) {
}

std::optional<Cache> Cache::FromSerialized(const QByteArray &serialized) {
	return {};
}

QByteArray Cache::serialize() {
	return {};
}

int Cache::frames() const {
	return _frames;
}

QImage Cache::frame(int index) const {
	Expects(index < _frames);

	const auto row = index / kPerRow;
	const auto inrow = index % kPerRow;
	const auto bytes = _bytes[row].data() + inrow * frameByteSize();
	const auto data = reinterpret_cast<const uchar*>(bytes);
	return QImage(data, _size, _size, QImage::Format_ARGB32_Premultiplied);
}

int Cache::size() const {
	return _size;
}

Preview Cache::makePreview() const {
	Expects(_frames > 0);

	auto image = frame(0);
	image.detach();
	return { std::move(image) };
}

void Cache::reserve(int frames) {
	const auto rows = (frames + kPerRow - 1) / kPerRow;
	if (const auto add = rows - int(_bytes.size()); add > 0) {
		_bytes.resize(rows);
		for (auto e = end(_bytes), i = e - add; i != e; ++i) {
			i->resize(kPerRow * frameByteSize());
		}
	}
	_durations.reserve(frames);
}

int Cache::frameRowByteSize() const {
	return _size * 4;
}

int Cache::frameByteSize() const {
	return _size * frameRowByteSize();
}

void Cache::add(crl::time duration, const QImage &frame) {
	Expects(duration < kMaxFrameDuration);
	Expects(frame.size() == QSize(_size, _size));
	Expects(frame.format() == QImage::Format_ARGB32_Premultiplied);

	const auto rowSize = frameRowByteSize();
	const auto frameSize = frameByteSize();
	const auto row = (_frames / kPerRow);
	const auto inrow = (_frames % kPerRow);
	const auto rows = row + 1;
	while (_bytes.size() < rows) {
		_bytes.emplace_back();
		_bytes.back().resize(kPerRow * frameSize);
	}
	const auto perLine = frame.bytesPerLine();
	auto dst = _bytes[row].data() + inrow * frameSize;
	auto src = frame.constBits();
	for (auto y = 0; y != _size; ++y) {
		memcpy(dst, src, rowSize);
		dst += rowSize;
		src += perLine;
	}
	++_frames;
	_durations.push_back(duration);
}

void Cache::finish() {
	_finished = true;
	if (_frame == _frames) {
		_frame = 0;
	}
}

PaintFrameResult Cache::paintCurrentFrame(
		QPainter &p,
		int x,
		int y,
		crl::time now) {
	if (!_frames) {
		return {};
	}
	const auto finishes = now ? currentFrameFinishes() : 0;
	if (finishes && now >= finishes) {
		++_frame;
		if (_finished && _frame == _frames) {
			_frame = 0;
		}
		_shown = now;
	} else if (!_shown) {
		_shown = now;
	}
	p.drawImage(
		QRect(x, y, _size, _size),
		frame(std::min(_frame, _frames - 1)));
	const auto next = currentFrameFinishes();
	const auto duration = next ? (next - _shown) : 0;
	return {
		.painted = true,
		.next = currentFrameFinishes(),
		.duration = duration,
	};
}

int Cache::currentFrame() const {
	return _frame;
}

crl::time Cache::currentFrameFinishes() const {
	if (!_shown || _frame >= _durations.size()) {
		return 0;
	} else if (const auto duration = _durations[_frame]) {
		return _shown + duration;
	}
	return 0;
}

Cached::Cached(
	const QString &entityData,
	Fn<std::unique_ptr<Loader>()> unloader,
	Cache cache)
: _unloader(std::move(unloader))
, _cache(std::move(cache))
, _entityData(entityData) {
}

QString Cached::entityData() const {
	return _entityData;
}

PaintFrameResult Cached::paint(QPainter &p, int x, int y, crl::time now) {
	return _cache.paintCurrentFrame(p, x, y, now);
}

Loading Cached::unload() {
	return Loading(_unloader(), _cache.makePreview());
}

Renderer::Renderer(RendererDescriptor &&descriptor)
: _cache(descriptor.size)
, _put(std::move(descriptor.put))
, _loader(std::move(descriptor.loader)) {
	Expects(_loader != nullptr);

	const auto size = _cache.size();
	const auto guard = base::make_weak(this);
	crl::async([=, factory = std::move(descriptor.generator)]() mutable {
		auto generator = factory();
		auto rendered = generator->renderNext(
			QImage(),
			QSize(size, size) * style::DevicePixelRatio(),
			Qt::KeepAspectRatio);
		if (rendered.image.isNull()) {
			return;
		}
		crl::on_main(guard, [
			=,
			frame = std::move(rendered),
			generator = std::move(generator)
		]() mutable {
			frameReady(
				std::move(generator),
				frame.duration,
				std::move(frame.image));
		});
	});
}

void Renderer::frameReady(
		std::unique_ptr<Ui::FrameGenerator> generator,
		crl::time duration,
		QImage frame) {
	if (frame.isNull()) {
		finish();
		return;
	}
	if (const auto count = generator->count()) {
		if (!_cache.frames()) {
			_cache.reserve(count);
		}
	}
	const auto explicitRepaint = (_cache.frames() == _cache.currentFrame());
	_cache.add(duration, frame);
	const auto size = _cache.size();
	const auto guard = base::make_weak(this);
	crl::async([
		=,
		frame = std::move(frame),
		generator = std::move(generator)
	]() mutable {
		auto rendered = generator->renderNext(
			std::move(frame),
			QSize(size, size) * style::DevicePixelRatio(),
			Qt::KeepAspectRatio);
		crl::on_main(guard, [
			=,
			frame = std::move(rendered),
			generator = std::move(generator)
		]() mutable {
			frameReady(
				std::move(generator),
				frame.duration,
				std::move(frame.image));
		});
	});
	if (explicitRepaint && _repaint) {
		_repaint();
	}
}

void Renderer::finish() {
	_finished = true;
	_cache.finish();
	if (_put) {
		_put(_cache.serialize());
	}
}

PaintFrameResult Renderer::paint(QPainter &p, int x, int y, crl::time now) {
	return _cache.paintCurrentFrame(p, x, y, now);
}

std::optional<Cached> Renderer::ready(const QString &entityData) {
	return _finished
		? Cached{ entityData, std::move(_loader), std::move(_cache) }
		: std::optional<Cached>();
}

std::unique_ptr<Loader> Renderer::cancel() {
	return _loader();
}

Preview Renderer::makePreview() const {
	return _cache.makePreview();
}

void Renderer::setRepaintCallback(Fn<void()> repaint) {
	_repaint = std::move(repaint);
}

Cache Renderer::takeCache() {
	return std::move(_cache);
}

Loading::Loading(std::unique_ptr<Loader> loader, Preview preview)
: _loader(std::move(loader))
, _preview(std::move(preview)) {
}

QString Loading::entityData() const {
	return _loader->entityData();
}

void Loading::load(Fn<void(Loader::LoadResult)> done) {
	_loader->load(crl::guard(this, [this, done = std::move(done)](
			Loader::LoadResult result) mutable {
		if (const auto caching = std::get_if<Caching>(&result)) {
			caching->preview = _preview
				? std::move(_preview)
				: _loader->preview();
		}
		done(std::move(result));
	}));
}

bool Loading::loading() const {
	return _loader->loading();
}

void Loading::paint(QPainter &p, int x, int y, const QColor &preview) {
	if (!_preview) {
		if (auto preview = _loader->preview()) {
			_preview = std::move(preview);
		}
	}
	_preview.paint(p, x, y, preview);
}

void Loading::cancel() {
	_loader->cancel();
	invalidate_weak_ptrs(this);
}

Instance::Instance(
	Loading loading,
	Fn<void(not_null<Instance*>, RepaintRequest)> repaintLater)
: _state(std::move(loading))
, _repaintLater(std::move(repaintLater)) {
}

QString Instance::entityData() const {
	if (const auto loading = std::get_if<Loading>(&_state)) {
		return loading->entityData();
	} else if (const auto caching = std::get_if<Caching>(&_state)) {
		return caching->entityData;
	} else if (const auto cached = std::get_if<Cached>(&_state)) {
		return cached->entityData();
	}
	Unexpected("State in Instance::entityData.");
}

void Instance::paint(
		QPainter &p,
		int x,
		int y,
		crl::time now,
		const QColor &preview,
		bool paused) {
	if (const auto loading = std::get_if<Loading>(&_state)) {
		loading->paint(p, x, y, preview);
		loading->load([=](Loader::LoadResult result) {
			if (auto caching = std::get_if<Caching>(&result)) {
				caching->renderer->setRepaintCallback([=] { repaint(); });
				_state = std::move(*caching);
			} else if (auto cached = std::get_if<Cached>(&result)) {
				_state = std::move(*cached);
			} else {
				Unexpected("Value in Loader::LoadResult.");
			}
		});
	} else if (const auto caching = std::get_if<Caching>(&_state)) {
		auto result = caching->renderer->paint(p, x, y, paused ? 0 : now);
		if (!result.painted) {
			caching->preview.paint(p, x, y, preview);
		} else {
			if (!caching->preview.image()) {
				caching->preview = caching->renderer->makePreview();
			}
			if (result.next > now) {
				_repaintLater(this, { result.next, result.duration });
			}
		}
		if (auto cached = caching->renderer->ready(caching->entityData)) {
			_state = std::move(*cached);
		}
	} else if (const auto cached = std::get_if<Cached>(&_state)) {
		const auto result = cached->paint(p, x, y, paused ? 0 : now);
		if (result.next > now) {
			_repaintLater(this, { result.next, result.duration });
		}
	}
}

void Instance::repaint() {
	for (const auto &object : _usage) {
		object->repaint();
	}
}

void Instance::incrementUsage(not_null<Object*> object) {
	_usage.emplace(object);
}

void Instance::decrementUsage(not_null<Object*> object) {
	_usage.remove(object);
	if (!_usage.empty()) {
		return;
	}
	if (const auto loading = std::get_if<Loading>(&_state)) {
		loading->cancel();
	} else if (const auto caching = std::get_if<Caching>(&_state)) {
		_state = Loading{
			caching->renderer->cancel(),
			std::move(caching->preview),
		};
	} else if (const auto cached = std::get_if<Cached>(&_state)) {
		_state = cached->unload();
	}
	_repaintLater(this, RepaintRequest());
}

Object::Object(not_null<Instance*> instance, Fn<void()> repaint)
: _instance(instance)
, _repaint(std::move(repaint)) {
}

Object::~Object() {
	unload();
}

QString Object::entityData() {
	return _instance->entityData();
}

void Object::paint(
		QPainter &p,
		int x,
		int y,
		crl::time now,
		const QColor &preview,
		bool paused) {
	if (!_using) {
		_using = true;
		_instance->incrementUsage(this);
	}
	_instance->paint(p, x, y, now, preview, paused);
}

void Object::unload() {
	if (_using) {
		_using = false;
		_instance->decrementUsage(this);
	}
}

void Object::repaint() {
	_repaint();
}

} // namespace Ui::CustomEmoji
