/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_block.h"
#include "base/weak_ptr.h"
#include "base/bytes.h"

class QColor;
class QPainter;

namespace Ui::CustomEmoji {

class Preview final {
public:
	Preview() = default;
	Preview(QImage image);
	Preview(QPainterPath path, float64 scale);

	void paint(QPainter &p, int x, int y, const QColor &preview);

	[[nodiscard]] explicit operator bool() const {
		return !v::is_null(_data);
	}

private:
	struct ScaledPath {
		QPainterPath path;
		float64 scale = 1.;
	};

	void paintPath(
		QPainter &p,
		int x,
		int y,
		const QColor &preview,
		const ScaledPath &path);

	std::variant<v::null_t, ScaledPath, QImage> _data;

};

class Cache final {
public:
	Cache(QSize size);

	[[nodiscard]] int frames() const;
	[[nodiscard]] QImage frame(int index) const;
	void reserve(int frames);

private:
	static constexpr auto kPerRow = 30;

	std::vector<bytes::vector> _bytes;
	std::vector<int> _durations;
	QSize _size;
	int _frames = 0;

};

class Loader;
class Loading;

class Unloader {
public:
	[[nodiscard]] virtual std::unique_ptr<Loader> unload() = 0;
	virtual ~Unloader() = default;
};

class Cached final {
public:
	Cached(std::unique_ptr<Unloader> unloader, Cache cache);

	void paint(QPainter &p, int x, int y);
	[[nodiscard]] Loading unload();

private:
	std::unique_ptr<Unloader> _unloader;
	Cache _cache;

};

class Cacher {
public:
	virtual bool paint(QPainter &p, int x, int y) = 0;
	[[nodiscard]] virtual std::optional<Cached> ready() = 0;
	[[nodiscard]] virtual std::unique_ptr<Loader> cancel() = 0;
	virtual ~Cacher() = default;

protected:
	void reserve(int frames);
	void add(crl::time duration, QImage frame);

	[[nodiscard]] Cache takeCache();

private:
	Cache _cache;

};

class Caching final {
public:
	Caching(std::unique_ptr<Cacher> cacher, Preview preview);
	void paint(QPainter &p, int x, int y, const QColor &preview);

	[[nodiscard]] std::optional<Cached> ready();
	[[nodiscard]] Loading cancel();

private:
	std::unique_ptr<Cacher> _cacher;
	Preview _preview;

};

class Loader {
public:
	virtual void load(Fn<void(Caching)> ready) = 0;
	virtual void cancel() = 0;
	[[nodiscard]] virtual Preview preview() = 0;
	virtual ~Loader() = default;
};

class Loading final : public base::has_weak_ptr {
public:
	Loading(std::unique_ptr<Loader> loader, Preview preview);

	void load(Fn<void(Caching)> done);
	void paint(QPainter &p, int x, int y, const QColor &preview);
	void cancel();

private:
	std::unique_ptr<Loader> _loader;
	Preview _preview;

};

class Instance final {
public:
	Instance(const QString &entityData, Loading loading);

	[[nodiscard]] QString entityData() const;
	void paint(QPainter &p, int x, int y, const QColor &preview);

	void incrementUsage();
	void decrementUsage();

private:
	std::variant<Loading, Caching, Cached> _state;
	QString _entityData;

	int _usage = 0;

};

class Delegate {
public:
	[[nodiscard]] virtual bool paused() = 0;
	virtual ~Delegate() = default;
};

class Object final : public Ui::Text::CustomEmoji {
public:
	Object(not_null<Instance*> instance);
	~Object();

	QString entityData() override;
	void paint(QPainter &p, int x, int y, const QColor &preview) override;
	void unload() override;

private:
	const not_null<Instance*> _instance;
	bool _using = false;

};

} // namespace Ui::CustomEmoji
