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
#include "base/timer.h"

class QColor;
class QPainter;

namespace Ui {
class FrameGenerator;
} // namespace Ui

namespace Ui::CustomEmoji {

class Preview final {
public:
	Preview() = default;
	Preview(QImage image, bool exact);
	Preview(QPainterPath path, float64 scale);

	void paint(QPainter &p, int x, int y, const QColor &preview);
	[[nodiscard]] bool isImage() const;
	[[nodiscard]] bool isExactImage() const;
	[[nodiscard]] QImage image() const;

	[[nodiscard]] explicit operator bool() const {
		return !v::is_null(_data);
	}

private:
	struct ScaledPath {
		QPainterPath path;
		float64 scale = 1.;
	};
	struct Image {
		QImage data;
		bool exact = false;
	};

	void paintPath(
		QPainter &p,
		int x,
		int y,
		const QColor &preview,
		const ScaledPath &path);

	std::variant<v::null_t, ScaledPath, Image> _data;

};

struct PaintFrameResult {
	bool painted = false;
	crl::time next = 0;
	crl::time duration = 0;
};

class Cache final {
public:
	Cache(int size);

	struct Frame {
		not_null<const QImage*> image;
		QRect source;
	};

	[[nodiscard]] static std::optional<Cache> FromSerialized(
		const QByteArray &serialized,
		int requestedSize);
	[[nodiscard]] QByteArray serialize();

	[[nodiscard]] int size() const;
	[[nodiscard]] int frames() const;
	[[nodiscard]] Frame frame(int index) const;
	void reserve(int frames);
	void add(crl::time duration, const QImage &frame);
	void finish();

	[[nodiscard]] Preview makePreview() const;

	PaintFrameResult paintCurrentFrame(
		QPainter &p,
		int x,
		int y,
		crl::time now);
	[[nodiscard]] int currentFrame() const;

private:
	static constexpr auto kPerRow = 16;

	[[nodiscard]] int frameRowByteSize() const;
	[[nodiscard]] int frameByteSize() const;
	[[nodiscard]] crl::time currentFrameFinishes() const;

	std::vector<QImage> _images;
	std::vector<uint16> _durations;
	QImage _full;
	crl::time _shown = 0;
	int _frame = 0;
	int _size = 0;
	int _frames = 0;
	bool _finished = false;

};

class Loader;
class Loading;

class Cached final {
public:
	Cached(
		const QString &entityData,
		Fn<std::unique_ptr<Loader>()> unloader,
		Cache cache);

	[[nodiscard]] QString entityData() const;
	[[nodiscard]] Preview makePreview() const;
	PaintFrameResult paint(QPainter &p, int x, int y, crl::time now);
	[[nodiscard]] Loading unload();

private:
	Fn<std::unique_ptr<Loader>()> _unloader;
	Cache _cache;
	QString _entityData;

};

struct RendererDescriptor {
	Fn<std::unique_ptr<Ui::FrameGenerator>()> generator;
	Fn<void(QByteArray)> put;
	Fn<std::unique_ptr<Loader>()> loader;
	int size = 0;
};

class Renderer final : public base::has_weak_ptr {
public:
	explicit Renderer(RendererDescriptor &&descriptor);
	virtual ~Renderer();

	PaintFrameResult paint(QPainter &p, int x, int y, crl::time now);
	[[nodiscard]] std::optional<Cached> ready(const QString &entityData);
	[[nodiscard]] std::unique_ptr<Loader> cancel();

	[[nodiscard]] Preview makePreview() const;

	void setRepaintCallback(Fn<void()> repaint);
	[[nodiscard]] Cache takeCache();

private:
	void frameReady(
		std::unique_ptr<Ui::FrameGenerator> generator,
		crl::time duration,
		QImage frame);
	void renderNext(
		std::unique_ptr<Ui::FrameGenerator> generator,
		QImage storage);
	void finish();

	Cache _cache;
	std::unique_ptr<Ui::FrameGenerator> _generator;
	QImage _storage;
	Fn<void(QByteArray)> _put;
	Fn<void()> _repaint;
	Fn<std::unique_ptr<Loader>()> _loader;
	bool _finished = false;

};

struct Caching {
	std::unique_ptr<Renderer> renderer;
	QString entityData;
	Preview preview;
};

class Loader {
public:
	using LoadResult = std::variant<Caching, Cached>;
	[[nodiscard]] virtual QString entityData() = 0;
	virtual void load(Fn<void(LoadResult)> loaded) = 0;
	[[nodiscard]] virtual bool loading() = 0;
	virtual void cancel() = 0;
	[[nodiscard]] virtual Preview preview() = 0;
	virtual ~Loader() = default;
};

class Loading final : public base::has_weak_ptr {
public:
	Loading(std::unique_ptr<Loader> loader, Preview preview);

	[[nodiscard]] QString entityData() const;

	void load(Fn<void(Loader::LoadResult)> done);
	[[nodiscard]] bool loading() const;
	void paint(QPainter &p, int x, int y, const QColor &preview);
	[[nodiscard]] bool hasImagePreview() const;
	[[nodiscard]] Preview imagePreview() const;
	void updatePreview(Preview preview);
	void cancel();

private:
	std::unique_ptr<Loader> _loader;
	Preview _preview;

};

struct RepaintRequest {
	crl::time when = 0;
	crl::time duration = 0;
};

class Object;
class Instance final : public base::has_weak_ptr {
public:
	Instance(
		Loading loading,
		Fn<void(not_null<Instance*>, RepaintRequest)> repaintLater);
	Instance(const Instance&) = delete;
	Instance &operator=(const Instance&) = delete;

	[[nodiscard]] QString entityData() const;
	void paint(
		QPainter &p,
		int x,
		int y,
		crl::time now,
		const QColor &preview,
		bool paused);
	[[nodiscard]] bool hasImagePreview() const;
	[[nodiscard]] Preview imagePreview() const;
	void updatePreview(Preview preview);

	void incrementUsage(not_null<Object*> object);
	void decrementUsage(not_null<Object*> object);

	void repaint();

private:
	std::variant<Loading, Caching, Cached> _state;
	base::flat_set<not_null<Object*>> _usage;
	Fn<void(not_null<Instance*> that, RepaintRequest)> _repaintLater;

};

class Delegate {
public:
	[[nodiscard]] virtual bool paused() = 0;
	virtual ~Delegate() = default;
};

class Object final : public Ui::Text::CustomEmoji {
public:
	Object(not_null<Instance*> instance, Fn<void()> repaint);
	~Object();

	QString entityData() override;
	void paint(
		QPainter &p,
		int x,
		int y,
		crl::time now,
		const QColor &preview,
		bool paused) override;
	void unload() override;

	void repaint();

private:
	const not_null<Instance*> _instance;
	Fn<void()> _repaint;
	bool _using = false;

};

} // namespace Ui::CustomEmoji
