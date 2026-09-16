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

} // namespace

LinkPill::LinkPill(const LinkPreview &link, float64 density, int maxWidth)
: _density(density)
, _font(
	int(base::SafeRound(kFontSize * density)),
	style::FontFlag::Bold,
	QString()) {
	const auto text = link.name.isEmpty()
		? WithoutScheme(link.url).toUpper()
		: link.name;
	const auto fixed = (kPaddingLeft
		+ kIconSize
		+ kIconPadding
		+ kPaddingRight) * _density;
	const auto maxTextWidth = std::max(maxWidth - int(fixed), 1);
	_text = _font->elided(text, maxTextWidth);
	_textWidth = _font->width(_text);
	_size = QSizeF(
		fixed + _textWidth,
		(kPaddingTop + kPaddingBottom) * _density
			+ std::max(kIconSize * _density, float64(_font->height)));
	updateIcon();
}

QSizeF LinkPill::size() const {
	return _size;
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
	_icon = st::photoEditorLinkPillIcon.instance(
		foreground(),
		style::kScaleMax);
}

void LinkPill::paint(QPainter &p, QPointF origin, float64 scale) const {
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.translate(origin);
	p.scale(scale, scale);

	const auto pill = QRectF(QPointF(), _size);
	const auto radius = pill.height() * kRadiusRatio;
	p.setPen(Qt::NoPen);
	p.setBrush(background());
	p.drawRoundedRect(pill, radius, radius);

	const auto iconSide = kIconSize * _density;
	const auto icon = QRectF(
		kPaddingLeft * _density,
		(pill.height() - iconSide) / 2.,
		iconSide,
		iconSide);
	p.drawImage(icon, _icon);

	p.setFont(_font);
	p.setPen(foreground());
	p.drawText(
		QPointF(
			icon.x() + icon.width() + kIconPadding * _density,
			(pill.height() - _font->height) / 2. + _font->ascent),
		_text);
	p.restore();
}

} // namespace Editor
