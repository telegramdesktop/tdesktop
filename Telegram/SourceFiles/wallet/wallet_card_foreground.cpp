/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_foreground.h"

#include "ui/image/image_prepare.h"
#include "ui/widgets/shadow.h"
#include "ui/painter.h"

#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Wallet {
namespace {

constexpr auto kFadeDuration = crl::time(300);

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

} // namespace

CardForeground::CardForeground(CardForegroundArgs &&args)
: _paint(std::move(args.paint))
, _repaint(std::move(args.repaint)) {
	std::move(args.hidden) | rpl::on_next([=](bool hidden) {
		setHidden(hidden);
	}, _lifetime);
	std::move(args.changes) | rpl::on_next([=] {
		invalidate();
	}, _lifetime);
	anim::Disables() | rpl::filter([=](bool disabled) {
		return disabled && _animation.animating();
	}) | rpl::on_next([=] {
		_animation.stop();
		_repaint();
	}, _lifetime);
}

void CardForeground::invalidate() {
	_valid = false;
	_repaint();
}

void CardForeground::paint(QPainter &p, const CardFold &fold) {
	const auto opacity = _animation.value(_target);
	if (opacity <= 0. || !fold.valid || fold.opacity <= 0.) {
		return;
	}
	validate(fold);
	const auto was = p.opacity();
	p.setOpacity(was * opacity);
	p.drawImage(_origin, _cache);
	p.setOpacity(was);
}

void CardForeground::setHidden(bool hidden) {
	const auto target = hidden ? 0. : 1.;
	if (_target == target) {
		return;
	}
	const auto from = _animation.value(_target);
	_target = target;
	if (anim::Disabled() || from == target) {
		_animation.stop();
		_repaint();
		return;
	}
	const auto duration = std::abs(target - from) * kFadeDuration;
	_animation.start(
		[=] { _repaint(); },
		from,
		target,
		crl::time(std::ceil(duration)),
		anim::sineInOut);
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

	auto outside = QPainterPath();
	outside.addRect(QRectF(QPointF(), QSizeF(bounds.size())));
	outside.addPolygon(CardFoldOutline(_fold).translated(-QPointF(_origin)));
	auto q = QPainter(&_cache);
	auto hq = PainterHighQualityEnabler(q);
	q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	q.drawImage(QPoint(), _content);
	q.setCompositionMode(QPainter::CompositionMode_SourceOver);
	q.drawImage(QPoint(), _content);
	q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	q.fillPath(outside, Qt::black);
}

} // namespace Wallet
