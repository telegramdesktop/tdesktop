/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_foreground.h"

#include "ui/effects/animation_value.h"
#include "ui/image/image_prepare.h"
#include "ui/widgets/shadow.h"
#include "ui/painter.h"
#include "wallet/wallet_card_gradient.h"

#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Wallet {
namespace {

constexpr auto kSecondaryLift = 0.7;
constexpr auto kHaloShade = 0.15;
constexpr auto kHaloSpread = 8U;

[[nodiscard]] bool SameFold(const CardFold &a, const CardFold &b) {
	return (a.rest == b.rest)
		&& (a.quad == b.quad)
		&& (a.transform == b.transform)
		&& (a.fold == b.fold)
		&& (a.opacity == b.opacity);
}

void MakeShadowMask(QImage &image, float64 opacity) {
	const auto alpha = uint32(std::clamp(
		int(base::SafeRound(opacity * 255.)),
		0,
		255));
	const auto width = image.width();
	for (auto y = 0; y != image.height(); ++y) {
		const auto row = reinterpret_cast<uint32*>(image.scanLine(y));
		for (auto x = 0; x != width; ++x) {
			row[x] = (((row[x] & 0xFFU) * alpha / 255) << 24);
		}
	}
}

// WHY: a plain blur leaves the ring next to the glyphs half transparent, so
// a bright gift shows through it; the blurred coverage is amplified to keep
// the halo solid next to the glyphs and soft only at its outer edge.
void MakeSecondary(
		QImage &image,
		QColor ink,
		QColor halo,
		float64 opacity) {
	const auto alpha = uint32(std::clamp(
		int(base::SafeRound(opacity * 255.)),
		0,
		255));
	const auto inkRgb = ink.rgb();
	const auto haloRgb = halo.rgb();
	const auto channel = [&](int shift, uint32 glyph, uint32 under) {
		const auto from = (inkRgb >> shift) & 0xFFU;
		const auto to = (haloRgb >> shift) & 0xFFU;
		return ((from * glyph + to * under) / 255U) << shift;
	};
	const auto width = image.width();
	for (auto y = 0; y != image.height(); ++y) {
		const auto row = reinterpret_cast<uint32*>(image.scanLine(y));
		for (auto x = 0; x != width; ++x) {
			const auto glyph = row[x] >> 24;
			const auto spread = std::min((row[x] & 0xFFU) * kHaloSpread, 255U);
			const auto cover = (glyph + spread * (255U - glyph) / 255U)
				* alpha
				/ 255U;
			const auto under = cover * (255U - glyph) / 255U;
			row[x] = ((glyph + under) << 24)
				| channel(16, glyph, under)
				| channel(8, glyph, under)
				| channel(0, glyph, under);
		}
	}
}

} // namespace

CardForeground::CardForeground(CardForegroundArgs &&args)
: _paint(std::move(args.paint))
, _halo(std::move(args.halo))
, _repaint(std::move(args.repaint)) {
	std::move(args.changes) | rpl::on_next([=] {
		invalidate();
	}, _lifetime);
}

void CardForeground::invalidate() {
	_valid = false;
	_repaint();
}

void CardForeground::paint(QPainter &p, const CardFold &fold) {
	if (!fold.valid || fold.opacity <= 0.) {
		return;
	}
	validate(fold);
	p.drawImage(_origin, _cache);
}

void CardForeground::validate(const CardFold &fold) {
	const auto ratio = style::DevicePixelRatio();
	if (_valid && _ratio == ratio && SameFold(_fold, fold)) {
		return;
	}
	_valid = true;
	_fold = fold;
	_ratio = ratio;
	compose();
}

void CardForeground::compose() {
	const auto &st = st::walletCardForegroundShadow;
	const auto card = _fold.quad.boundingRect().toAlignedRect();
	const auto bounds = card.marginsAdded(Ui::BoxShadow::ExtendFor(st));
	const auto size = bounds.size() * _ratio;
	_origin = bounds.topLeft();
	for (const auto image : { &_content, &_cache }) {
		if (image->size() != size || image->devicePixelRatio() != _ratio) {
			*image = QImage(size, QImage::Format_ARGB32_Premultiplied);
			image->setDevicePixelRatio(_ratio);
		}
		image->fill(Qt::transparent);
	}
	{
		auto q = Painter(&_content);
		auto hq = PainterHighQualityEnabler(q);
		q.translate(-_origin);
		_paint(q, _fold, bounds);
	}
	{
		auto q = QPainter(&_cache);
		q.drawImage(QPointF(st.offset), _content);
		q.setCompositionMode(QPainter::CompositionMode_SourceIn);
		q.fillRect(QRect(QPoint(), bounds.size()), Qt::white);
	}
	_cache = Images::BlurLargeImage(std::move(_cache), st.blurRadius * _ratio);
	MakeShadowMask(_cache, st.opacity);
	composeSecondary(bounds);

	auto outside = QPainterPath();
	outside.addRect(QRectF(QPointF(), QSizeF(bounds.size())));
	outside.addPolygon(CardFoldOutline(_fold).translated(-QPointF(_origin)));
	auto q = QPainter(&_cache);
	auto hq = PainterHighQualityEnabler(q);
	if (!_secondary.isNull()) {
		q.drawImage(_secondaryOrigin - _origin, _secondary);
	}
	q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	q.drawImage(QPoint(), _content);
	q.setCompositionMode(QPainter::CompositionMode_SourceOver);
	q.drawImage(QPoint(), _content);
	q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	q.fillPath(outside, Qt::black);
}

void CardForeground::composeSecondary(QRect bounds) {
	const auto radius = st::walletCardForegroundHalo;
	const auto area = _halo.area(_fold).marginsAdded(
		{ radius, radius, radius, radius }
	).intersected(bounds);
	if (area.isEmpty()) {
		_secondary = QImage();
		return;
	}
	const auto size = area.size() * _ratio;
	_secondaryOrigin = area.topLeft();
	if (_secondary.size() != size || _secondary.devicePixelRatio() != _ratio) {
		_secondary = QImage(size, QImage::Format_ARGB32_Premultiplied);
		_secondary.setDevicePixelRatio(_ratio);
	}
	_secondary.fill(Qt::transparent);
	{
		auto q = Painter(&_secondary);
		auto hq = PainterHighQualityEnabler(q);
		q.translate(-_secondaryOrigin);
		_halo.paint(q, _fold, area);
		q.resetTransform();
		q.setCompositionMode(QPainter::CompositionMode_SourceIn);
		q.fillRect(QRect(QPoint(), area.size()), Qt::white);
	}
	_secondary = Images::BlurLargeImage(
		std::move(_secondary),
		radius * _ratio);
	const auto tint = _halo.tint();
	const auto card = MixCardMarkTint(
		{ .hue = CardReferenceColor(), .amount = 1. },
		{ .hue = tint.hue, .amount = 1. },
		tint.amount).hue;
	MakeSecondary(
		_secondary,
		anim::color(card, st::activeButtonFg, kSecondaryLift),
		anim::color(card, QColor(0, 0, 0), kHaloShade),
		_fold.opacity);
}

} // namespace Wallet
