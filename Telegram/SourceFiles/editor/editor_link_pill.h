/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/style/style_core_types.h"

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
	Plain,
};

enum class PillTextFit : uchar {
	Truncate,
	Scale,
};

class LinkPill final {
public:
	LinkPill(
		const LinkPreview &link,
		float64 density,
		int maxWidth,
		const style::icon *customIcon = nullptr,
		PillTextFit fit = PillTextFit::Truncate);

	[[nodiscard]] static float64 DensityFor(int canvasWidth);
	[[nodiscard]] static LinkStyle NextStyle(LinkStyle style);
	[[nodiscard]] static LinkStyle PreviousStyle(LinkStyle style);

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
	QStringList _lines;
	float64 _textScale = 1.;
	QSizeF _size;
	QImage _icon;
	LinkStyle _style = LinkStyle::Framed;
	const style::icon *_customIcon = nullptr;

};

} // namespace Editor
