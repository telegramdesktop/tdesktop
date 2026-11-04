/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_link_pill.h"

#include "ui/painter.h"
#include "styles/style_editor.h"

namespace Editor {
namespace {

constexpr auto kDpPerWidth = 360.;
constexpr auto kPaddingLeft = 4.;
constexpr auto kPaddingTop = 4.33;
constexpr auto kPaddingRight = 7.66;
constexpr auto kPaddingBottom = 3.;
constexpr auto kIconSize = 30.;
constexpr auto kIconPadding = 3.25;
constexpr auto kFontSize = 24.;
constexpr auto kRadiusRatio = 0.2;
constexpr auto kSemiTransparentAlpha = 0x4C;
constexpr auto kMinTextScale = 0.4;

[[nodiscard]] QString WithoutScheme(const QString &url) {
	auto result = url;
	const auto scheme = result.indexOf(u"://"_q);
	if (scheme >= 0) {
		result = result.mid(scheme + 3);
	}
	while (result.endsWith('/')) {
		result.chop(1);
	}
	return result;
}

[[nodiscard]] QStringList SplitInHalf(const QString &text) {
	const auto mid = text.size() / 2;
	auto best = -1;
	for (auto i = 0; i != text.size(); ++i) {
		if (text[i].isSpace()
			&& (best < 0 || std::abs(i - mid) < std::abs(best - mid))) {
			best = i;
		}
	}
	if (best < 0) {
		return { text };
	}
	return { text.left(best).trimmed(), text.mid(best + 1).trimmed() };
}

} // namespace

LinkPill::LinkPill(
	const LinkPreview &link,
	float64 density,
	int maxWidth,
	const style::icon *customIcon,
	PillTextFit fit)
: _density(density)
, _font(
	int(base::SafeRound(kFontSize * density)),
	style::FontFlag::Bold,
	QString())
, _customIcon(customIcon) {
	const auto text = link.name.isEmpty()
		? WithoutScheme(link.url).toUpper()
		: link.name;
	const auto fixed = (kPaddingLeft
		+ kIconSize
		+ kIconPadding
		+ kPaddingRight) * _density;
	const auto maxTextWidth = std::max(maxWidth - int(fixed), 1);
	if (fit == PillTextFit::Scale) {
		auto lines = (_font->width(text) * kMinTextScale > maxTextWidth)
			? SplitInHalf(text)
			: QStringList{ text };
		auto lineWidth = 0;
		for (const auto &line : lines) {
			lineWidth = std::max(lineWidth, _font->width(line));
		}
		_textScale = std::clamp(
			maxTextWidth / float64(lineWidth),
			0.,
			1.);
		_lines = lines;
		_size = QSizeF(
			fixed + lineWidth * _textScale,
			(kPaddingTop + kPaddingBottom) * _density
				+ std::max(
					kIconSize * _density,
					_font->height * lines.size() * _textScale));
	} else {
		const auto elided = _font->elided(text, maxTextWidth);
		_lines = { elided };
		_size = QSizeF(
			fixed + _font->width(elided),
			(kPaddingTop + kPaddingBottom) * _density
				+ std::max(kIconSize * _density, float64(_font->height)));
	}
	updateIcon();
}

QSizeF LinkPill::size() const {
	return _size;
}

LinkStyle LinkPill::NextStyle(LinkStyle style) {
	switch (style) {
	case LinkStyle::Framed: return LinkStyle::SemiTransparent;
	case LinkStyle::SemiTransparent: return LinkStyle::Opaque;
	case LinkStyle::Opaque: return LinkStyle::Plain;
	case LinkStyle::Plain: return LinkStyle::Framed;
	}
	Unexpected("Link style in LinkPill::NextStyle.");
}

LinkStyle LinkPill::PreviousStyle(LinkStyle style) {
	switch (style) {
	case LinkStyle::Framed: return LinkStyle::Plain;
	case LinkStyle::SemiTransparent: return LinkStyle::Framed;
	case LinkStyle::Opaque: return LinkStyle::SemiTransparent;
	case LinkStyle::Plain: return LinkStyle::Opaque;
	}
	Unexpected("Link style in LinkPill::PreviousStyle.");
}

LinkStyle LinkPill::style() const {
	return _style;
}

void LinkPill::setStyle(LinkStyle style) {
	_style = style;
	updateIcon();
}

QColor LinkPill::background() const {
	switch (_style) {
	case LinkStyle::Framed:
		return QColor(255, 255, 255);
	case LinkStyle::SemiTransparent:
		return QColor(0, 0, 0, kSemiTransparentAlpha);
	case LinkStyle::Opaque:
		return QColor(0, 0, 0);
	case LinkStyle::Plain:
		return QColor(0, 0, 0, 0);
	}
	Unexpected("Link style in LinkPill::background.");
}

QColor LinkPill::foreground() const {
	return (_style == LinkStyle::Framed)
		? QColor(0x33, 0x91, 0xD4)
		: QColor(255, 255, 255);
}

float64 LinkPill::DensityFor(int canvasWidth) {
	return canvasWidth / kDpPerWidth;
}

void LinkPill::updateIcon() {
	_icon = (_customIcon
		? _customIcon
		: &st::photoEditorLinkPillIcon)->instance(
		foreground(),
		style::kScaleMax);
}

void LinkPill::paint(QPainter &p, QPointF origin, float64 scale) const {
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.translate(origin);
	p.scale(scale, scale);

	const auto pill = QRectF(QPointF(), _size);
	if (_style != LinkStyle::Plain) {
		const auto radius = pill.height() * kRadiusRatio;
		p.setPen(Qt::NoPen);
		p.setBrush(background());
		p.drawRoundedRect(pill, radius, radius);
	}

	const auto iconSide = kIconSize * _density;
	const auto icon = QRectF(
		kPaddingLeft * _density,
		(pill.height() - iconSide) / 2.,
		iconSide,
		iconSide);
	p.drawImage(icon, _icon);

	p.setFont(_font);
	p.setPen(foreground());
	const auto textLeft = icon.x() + icon.width() + kIconPadding * _density;
	const auto block = _font->height * _lines.size() * _textScale;
	p.save();
	p.translate(textLeft, (pill.height() - block) / 2.);
	p.scale(_textScale, _textScale);
	for (auto i = 0; i != int(_lines.size()); ++i) {
		p.drawText(QPointF(0, i * _font->height + _font->ascent), _lines[i]);
	}
	p.restore();
	p.restore();
}

} // namespace Editor
