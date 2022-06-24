/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/custom_emoji_instance.h"

class QPainter;

namespace Ui::CustomEmoji {

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

Cache::Cache(QSize size) : _size(size) {
}

int Cache::frames() const {
	return _frames;
}

QImage Cache::frame(int index) const {
	return QImage();
}

void Cache::reserve(int frames) {
}

Cached::Cached(std::unique_ptr<Unloader> unloader, Cache cache)
: _unloader(std::move(unloader))
, _cache(cache) {
}

void Cached::paint(QPainter &p, int x, int y) {
	p.drawImage(x, y, _cache.frame(0));
}

Loading Cached::unload() {
	return Loading(_unloader->unload(), Preview(_cache.frame(0)));
}

void Cacher::reserve(int frames) {
	_cache.reserve(frames);
}

void Cacher::add(crl::time duration, QImage frame) {
}

Cache Cacher::takeCache() {
	return std::move(_cache);
}

Caching::Caching(std::unique_ptr<Cacher> cacher, Preview preview)
: _cacher(std::move(cacher))
, _preview(std::move(preview)) {
}

void Caching::paint(QPainter &p, int x, int y, const QColor &preview) {
	if (!_cacher->paint(p, x, y)) {
		_preview.paint(p, x, y, preview);
	}
}

std::optional<Cached> Caching::ready() {
	return _cacher->ready();
}

Loading Caching::cancel() {
	return Loading(_cacher->cancel(), std::move(_preview));
}

Loading::Loading(std::unique_ptr<Loader> loader, Preview preview)
: _loader(std::move(loader))
, _preview(std::move(preview)) {
}

void Loading::load(Fn<void(Caching)> done) {
	_loader->load(crl::guard(this, std::move(done)));
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

Instance::Instance(const QString &entityData, Loading loading)
: _state(std::move(loading))
, _entityData(entityData) {
}

QString Instance::entityData() const {
	return _entityData;
}

void Instance::paint(QPainter &p, int x, int y, const QColor &preview) {
	if (const auto loading = std::get_if<Loading>(&_state)) {
		loading->paint(p, x, y, preview);
		loading->load([=](Caching caching) {
			_state = std::move(caching);
		});
	} else if (const auto caching = std::get_if<Caching>(&_state)) {
		caching->paint(p, x, y, preview);
		if (auto cached = caching->ready()) {
			_state = std::move(*cached);
		}
	} else if (const auto cached = std::get_if<Cached>(&_state)) {
		cached->paint(p, x, y);
	}
}

void Instance::incrementUsage() {
	++_usage;
}

void Instance::decrementUsage() {
	Expects(_usage > 0);

	if (--_usage > 0) {
		return;
	}
	if (const auto loading = std::get_if<Loading>(&_state)) {
		loading->cancel();
	} else if (const auto caching = std::get_if<Caching>(&_state)) {
		_state = caching->cancel();
	} else if (const auto cached = std::get_if<Cached>(&_state)) {
		_state = cached->unload();
	}
}

Object::Object(not_null<Instance*> instance)
: _instance(instance) {
}

Object::~Object() {
	unload();
}

QString Object::entityData() {
	return _instance->entityData();
}

void Object::paint(QPainter &p, int x, int y, const QColor &preview) {
	if (!_using) {
		_using = true;
		_instance->incrementUsage();
	}
	_instance->paint(p, x, y, preview);
}

void Object::unload() {
	if (_using) {
		_using = false;
		_instance->decrementUsage();
	}
}

} // namespace Ui::CustomEmoji
