/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/style/style_core_color.h"
#include <vector>

namespace style {
namespace internal {

class IconMask {
public:
	template <int N>
	IconMask(const uchar (&data)[N]) : _data(data), _size(N) {
		static_assert(N > 0, "invalid image data");
	}

	const uchar *data() const {
		return _data;
	}
	int size() const {
		return _size;
	}

private:
	const uchar *_data;
	const int _size;

};

class MonoIcon {
public:
	MonoIcon() = default;
	MonoIcon(const MonoIcon &other) = delete;
	MonoIcon &operator=(const MonoIcon &other) = delete;
	MonoIcon(MonoIcon &&other) = default;
	MonoIcon &operator=(MonoIcon &&other) = default;
	MonoIcon(const IconMask *mask, Color color, QPoint offset);

	void reset() const;
	int width() const;
	int height() const;
	QSize size() const;

	QPoint offset() const;

	void paint(QPainter &p, const QPoint &pos, int outerw) const;
	void fill(QPainter &p, const QRect &rect) const;

	void paint(QPainter &p, const QPoint &pos, int outerw, QColor colorOverride) const;
	void fill(QPainter &p, const QRect &rect, QColor colorOverride) const;

	void paint(QPainter &p, const QPoint &pos, int outerw, const style::palette &paletteOverride) const;
	void fill(QPainter &p, const QRect &rect, const style::palette &paletteOverride) const;

	QImage instance(QColor colorOverride, int scale) const;

	~MonoIcon() {
	}

private:
	void ensureLoaded() const;
	void createCachedPixmap() const;
	void ensureColorizedImage(QColor color) const;

	const IconMask *_mask = nullptr;
	Color _color;
	QPoint _offset = { 0, 0 };
	mutable QImage _maskImage, _colorizedImage;
	mutable QPixmap _pixmap; // for pixmaps
	mutable QSize _size; // for rects

};

class IconData {
public:
	template <typename ...MonoIcons>
	IconData(MonoIcons &&...icons) {
		created();
		_parts.reserve(sizeof...(MonoIcons));
		addIcons(std::forward<MonoIcons>(icons)...);
	}

	void reset() {
		for_const (auto &part, _parts) {
			part.reset();
		}
	}
	bool empty() const {
		return _parts.empty();
	}

	void paint(QPainter &p, const QPoint &pos, int outerw) const {
		for_const (auto &part, _parts) {
			part.paint(p, pos, outerw);
		}
	}
	void fill(QPainter &p, const QRect &rect) const;

	void paint(QPainter &p, const QPoint &pos, int outerw, QColor colorOverride) const {
		for_const (auto &part, _parts) {
			part.paint(p, pos, outerw, colorOverride);
		}
	}
	void fill(QPainter &p, const QRect &rect, QColor colorOverride) const;

	void paint(QPainter &p, const QPoint &pos, int outerw, const style::palette &paletteOverride) const {
		for_const (auto &part, _parts) {
			part.paint(p, pos, outerw, paletteOverride);
		}
	}
	void fill(QPainter &p, const QRect &rect, const style::palette &paletteOverride) const;

	QImage instance(QColor colorOverride, int scale) const;

	int width() const;
	int height() const;

private:
	void created();

	template <typename ... MonoIcons>
	void addIcons() {
	}

	template <typename ... MonoIcons>
	void addIcons(MonoIcon &&icon, MonoIcons&&... icons) {
		_parts.push_back(std::move(icon));
		addIcons(std::forward<MonoIcons>(icons)...);
	}

	std::vector<MonoIcon> _parts;
	mutable int _width = -1;
	mutable int _height = -1;

};

class Icon {
public:
	Icon(Qt::Initialization) {
	}

	template <typename ... MonoIcons>
	Icon(MonoIcons&&... icons) : _data(new IconData(std::forward<MonoIcons>(icons)...)), _owner(true) {
	}
	Icon(const Icon &other) : _data(other._data) {
	}
	Icon(Icon &&other) : _data(base::take(other._data)), _owner(base::take(_owner)) {
	}
	Icon &operator=(const Icon &other) {
		Assert(!_owner);
		_data = other._data;
		_owner = false;
		return *this;
	}
	Icon &operator=(Icon &&other) {
		Assert(!_owner);
		_data = base::take(other._data);
		_owner = base::take(other._owner);
		return *this;
	}

	bool empty() const {
		return _data->empty();
	}

	int width() const {
		return _data->width();
	}
	int height() const {
		return _data->height();
	}
	QSize size() const {
		return QSize(width(), height());
	}

	void paint(QPainter &p, const QPoint &pos, int outerw) const {
		return _data->paint(p, pos, outerw);
	}
	void paint(QPainter &p, int x, int y, int outerw) const {
		return _data->paint(p, QPoint(x, y), outerw);
	}
	void paintInCenter(QPainter &p, const QRect &outer) const {
		return _data->paint(p, QPoint(outer.x() + (outer.width() - width()) / 2, outer.y() + (outer.height() - height()) / 2), outer.x() * 2 + outer.width());
	}
	void fill(QPainter &p, const QRect &rect) const {
		return _data->fill(p, rect);
	}

	void paint(QPainter &p, const QPoint &pos, int outerw, QColor colorOverride) const {
		return _data->paint(p, pos, outerw, colorOverride);
	}
	void paint(QPainter &p, int x, int y, int outerw, QColor colorOverride) const {
		return _data->paint(p, QPoint(x, y), outerw, colorOverride);
	}
	void paintInCenter(QPainter &p, const QRect &outer, QColor colorOverride) const {
		return _data->paint(p, QPoint(outer.x() + (outer.width() - width()) / 2, outer.y() + (outer.height() - height()) / 2), outer.x() * 2 + outer.width(), colorOverride);
	}
	void fill(QPainter &p, const QRect &rect, QColor colorOverride) const {
		return _data->fill(p, rect, colorOverride);
	}

	QImage instance(QColor colorOverride, int scale = kInterfaceScaleAuto) const {
		return _data->instance(colorOverride, scale);
	}

	class Proxy {
	public:
		Proxy(const Icon &icon, const style::palette &palette) : _icon(icon), _palette(palette) {
		}
		Proxy(const Proxy &other) = default;

		bool empty() const { return _icon.empty(); }
		int width() const { return _icon.width(); }
		int height() const { return _icon.height(); }
		QSize size() const { return _icon.size(); }

		void paint(QPainter &p, const QPoint &pos, int outerw) const {
			return _icon.paintWithPalette(p, pos, outerw, _palette);
		}
		void paint(QPainter &p, int x, int y, int outerw) const {
			return _icon.paintWithPalette(p, x, y, outerw, _palette);
		}
		void paintInCenter(QPainter &p, const QRect &outer) const {
			return _icon.paintInCenterWithPalette(p, outer, _palette);
		}
		void fill(QPainter &p, const QRect &rect) const {
			return _icon.fillWithPalette(p, rect, _palette);
		}

	private:
		const Icon &_icon;
		const style::palette &_palette;

	};
	Proxy operator[](const style::palette &paletteOverride) const {
		return Proxy(*this, paletteOverride);
	}

	~Icon() {
		if (auto data = base::take(_data)) {
			if (_owner) {
				delete data;
			}
		}
	}

private:
	friend class Proxy;

	void paintWithPalette(QPainter &p, const QPoint &pos, int outerw, const style::palette &paletteOverride) const {
		return _data->paint(p, pos, outerw, paletteOverride);
	}
	void paintWithPalette(QPainter &p, int x, int y, int outerw, const style::palette &paletteOverride) const {
		return _data->paint(p, QPoint(x, y), outerw, paletteOverride);
	}
	void paintInCenterWithPalette(QPainter &p, const QRect &outer, const style::palette &paletteOverride) const {
		return _data->paint(p, QPoint(outer.x() + (outer.width() - width()) / 2, outer.y() + (outer.height() - height()) / 2), outer.x() * 2 + outer.width(), paletteOverride);
	}
	void fillWithPalette(QPainter &p, const QRect &rect, const style::palette &paletteOverride) const {
		return _data->fill(p, rect, paletteOverride);
	}

	IconData *_data = nullptr;
	bool _owner = false;

};

void resetIcons();
void destroyIcons();

} // namespace internal
} // namespace style
