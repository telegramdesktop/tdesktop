/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Editor {

struct LinkPreview {
	QString url;
	QString name;
	bool captionAbove = true;
	bool largePhoto = false;
	bool preview = true;
	bool dark = false;

	friend inline bool operator==(
		const LinkPreview &,
		const LinkPreview &) = default;
};

enum class LinkStyle : uchar {
	Framed,
	SemiTransparent,
	Opaque,
};

class LinkPill final {
public:
	LinkPill(const LinkPreview &link, float64 density, int maxWidth);

	[[nodiscard]] static float64 DensityFor(int canvasWidth);

	[[nodiscard]] QSizeF size() const;
	[[nodiscard]] LinkStyle style() const;
	void setStyle(LinkStyle style);

	void paint(QPainter &p, QPointF origin, float64 scale) const;

private:
	[[nodiscard]] QColor background() const;
	[[nodiscard]] QColor foreground() const;
	void updateIcon();

	float64 _density = 1.;
	style::font _font;
	QString _text;
	int _textWidth = 0;
	QSizeF _size;
	QImage _icon;
	LinkStyle _style = LinkStyle::Framed;

};

} // namespace Editor
