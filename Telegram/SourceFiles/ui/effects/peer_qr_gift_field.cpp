/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/peer_qr_gift_field.h"

#include "ui/effects/animation_value.h"
#include "ui/text/text_custom_emoji.h"

#include "styles/style_boxes.h"

namespace Ui {
namespace {

constexpr auto kModelScaleMin = 0.93;
constexpr auto kModelScaleMax = 1.07;
constexpr auto kModelTilt = 11.;
constexpr auto kSymbolScaleMin = 0.75;
constexpr auto kSymbolScaleRange = 0.5;
constexpr auto kSymbolEmptyShare = 0.39;
constexpr auto kSymbolAlpha = 0.37;
constexpr auto kClearance = 0.8;
constexpr auto kModelPopDuration = crl::time(315);
constexpr auto kModelStagger = crl::time(200);
constexpr auto kSymbolFadeDuration = crl::time(300);
constexpr auto kSymbolStagger = crl::time(300);
constexpr auto kEntryDuration = std::max(
	kSymbolStagger + kSymbolFadeDuration,
	kModelStagger + kModelPopDuration);

enum class Salt : uint32 {
	ModelRadius = 101,
	ModelAngle,
	ModelScale,
	ModelTilt,
	ModelDelay,
	SymbolEmpty,
	SymbolX,
	SymbolY,
	SymbolHeight,
	SymbolDelay,
};

struct Item {
	QPointF center;
	float64 size = 0.;
	float64 rotation = 0.;
	float64 delay = 0.;
};

[[nodiscard]] float64 NodeRandom(int row, int column, Salt salt) {
	return PeerQrNodeHash(row, column, uint32(salt)) / 4294967296.;
}

[[nodiscard]] std::pair<QPointF, float64> ModelFlat(
		int row,
		int column,
		QPointF origin,
		const PeerQrGiftMetrics &metrics) {
	const auto random = [&](Salt salt) {
		return NodeRandom(row, column, salt);
	};
	const auto shift = (row & 1) ? 0.5 : 0.;
	const auto radius = metrics.modelOffset * random(Salt::ModelRadius);
	const auto angle = 2. * M_PI * random(Salt::ModelAngle);
	const auto flat = origin + QPointF(
		(column + shift) * metrics.modelSkipX + radius * std::cos(angle),
		row * metrics.modelSkipY + radius * std::sin(angle));
	const auto size = metrics.modelSize * (kModelScaleMin
		+ (kModelScaleMax - kModelScaleMin) * random(Salt::ModelScale));
	return { flat, size };
}

[[nodiscard]] bool NearModel(
		QPointF flat,
		float64 size,
		QPointF origin,
		const PeerQrGiftMetrics &metrics) {
	const auto relative = flat - origin;
	const auto rowNear = int(std::round(relative.y() / metrics.modelSkipY));
	for (auto row = rowNear - 1; row <= rowNear + 1; ++row) {
		const auto shift = (row & 1) ? 0.5 : 0.;
		const auto columnNear = int(std::round(
			relative.x() / metrics.modelSkipX - shift));
		for (auto column = columnNear - 1; column <= columnNear + 1; ++column) {
			const auto [model, modelSize] = ModelFlat(
				row,
				column,
				origin,
				metrics);
			const auto delta = flat - model;
			const auto clearance = kClearance * (modelSize + size) / 2.;
			if (QPointF::dotProduct(delta, delta) < clearance * clearance) {
				return true;
			}
		}
	}
	return false;
}

template <typename Callback>
void EnumerateModels(
		const PeerQrLayerFalloff &falloff,
		QPointF origin,
		const PeerQrGiftMetrics &metrics,
		Callback &&callback) {
	if (metrics.modelSkipX <= 0. || metrics.modelSkipY <= 0.) {
		return;
	}
	const auto reach = falloff.reach(metrics.modelOffset
		+ metrics.modelSize * kModelScaleMax / M_SQRT2);
	const auto rowFrom = int(std::ceil(
		(reach.top() - origin.y()) / metrics.modelSkipY));
	const auto rowTill = int(std::floor(
		(reach.bottom() - origin.y()) / metrics.modelSkipY));
	for (auto row = rowFrom; row <= rowTill; ++row) {
		const auto shift = (row & 1) ? 0.5 : 0.;
		const auto columnFrom = int(std::ceil(
			(reach.left() - origin.x()) / metrics.modelSkipX - shift));
		const auto columnTill = int(std::floor(
			(reach.right() - origin.x()) / metrics.modelSkipX - shift));
		for (auto column = columnFrom; column <= columnTill; ++column) {
			const auto [flat, size] = ModelFlat(row, column, origin, metrics);
			const auto place = PeerQrPlaceItem(falloff, flat, size);
			if (!place) {
				continue;
			}
			const auto random = [&](Salt salt) {
				return NodeRandom(row, column, salt);
			};
			callback(Item{
				.center = place->position,
				.size = size * place->scale,
				.rotation = kModelTilt * (2. * random(Salt::ModelTilt) - 1.),
				.delay = random(Salt::ModelDelay),
			});
		}
	}
}

template <typename Callback>
void EnumerateSymbols(
		const PeerQrLayerFalloff &falloff,
		QPointF origin,
		const PeerQrGiftMetrics &metrics,
		Callback &&callback) {
	if (metrics.symbolSkipX <= 0.
		|| metrics.symbolSkipY <= 0.
		|| metrics.modelSkipX <= 0.
		|| metrics.modelSkipY <= 0.) {
		return;
	}
	const auto reach = falloff.reach(
		std::max(metrics.symbolJitterX, metrics.symbolJitterY)
		+ metrics.symbolSize * (kSymbolScaleMin + kSymbolScaleRange) / 2.);
	const auto rowFrom = int(std::ceil(
		(reach.top() - origin.y()) / metrics.symbolSkipY));
	const auto rowTill = int(std::floor(
		(reach.bottom() - origin.y()) / metrics.symbolSkipY));
	for (auto row = rowFrom; row <= rowTill; ++row) {
		const auto shift = (row & 1) ? 0.5 : 0.;
		const auto columnFrom = int(std::ceil(
			(reach.left() - origin.x()) / metrics.symbolSkipX - shift));
		const auto columnTill = int(std::floor(
			(reach.right() - origin.x()) / metrics.symbolSkipX - shift));
		for (auto column = columnFrom; column <= columnTill; ++column) {
			const auto random = [&](Salt salt) {
				return NodeRandom(row, column, salt);
			};
			if (random(Salt::SymbolEmpty) < kSymbolEmptyShare) {
				continue;
			}
			const auto jitter = QPointF(
				metrics.symbolJitterX * (2. * random(Salt::SymbolX) - 1.),
				metrics.symbolJitterY * (2. * random(Salt::SymbolY) - 1.));
			const auto flat = origin + jitter + QPointF(
				(column + shift) * metrics.symbolSkipX,
				row * metrics.symbolSkipY);
			const auto height = random(Salt::SymbolHeight);
			const auto size = metrics.symbolSize
				* (kSymbolScaleMin + kSymbolScaleRange * height * height);
			const auto place = PeerQrPlaceItem(falloff, flat, size);
			if (!place || NearModel(flat, size, origin, metrics)) {
				continue;
			}
			callback(Item{
				.center = place->position,
				.size = size * place->scale,
				.delay = random(Salt::SymbolDelay),
			});
		}
	}
}

[[nodiscard]] QRectF ItemRect(const Item &item) {
	const auto half = item.size / 2.;
	return QRectF(
		item.center - QPointF(half, half),
		QSizeF(item.size, item.size));
}

[[nodiscard]] float64 EntryProgress(
		crl::time now,
		crl::time start,
		crl::time duration) {
	return std::clamp(float64(now - start) / duration, 0., 1.);
}

} // namespace

PeerQrGiftMetrics PeerQrGiftMetrics::Live() {
	return {
		.modelSize = float64(st::peerQrGiftModelSize),
		.modelSkipX = float64(st::peerQrGiftModelSkipX),
		.modelSkipY = float64(st::peerQrGiftModelSkipY),
		.modelOffset = float64(st::peerQrGiftModelOffset),
		.symbolSize = float64(st::peerQrGiftSymbolSize),
		.symbolSkipX = float64(st::peerQrGiftSymbolSkipX),
		.symbolSkipY = float64(st::peerQrGiftSymbolSkipY),
		.symbolJitterX = float64(st::peerQrGiftSymbolJitterX),
		.symbolJitterY = float64(st::peerQrGiftSymbolJitterY),
	};
}

void PaintPeerQrGiftRest(
		QPainter &p,
		const PeerQrLayerFalloff &falloff,
		QPointF origin,
		QRectF card,
		const PeerQrGiftRest &rest) {
	if (rest.model.isNull() && rest.symbol.isNull()) {
		return;
	}
	const auto cull = PeerQrCardCull(card);
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	if (!rest.symbol.isNull()) {
		p.setOpacity(kSymbolAlpha);
		EnumerateSymbols(falloff, origin, rest.metrics, [&](const Item &item) {
			if (!PeerQrCovered(cull, item.center, item.size)) {
				p.drawImage(ItemRect(item), rest.symbol);
			}
		});
		p.setOpacity(1.);
	}
	if (!rest.model.isNull()) {
		const auto base = p.transform();
		const auto frame = QRectF(QPointF(), QSizeF(rest.model.size()));
		EnumerateModels(falloff, origin, rest.metrics, [&](const Item &item) {
			if (!PeerQrCovered(cull, item.center, item.size)) {
				PeerQrApplyItemTransform(
					p,
					base,
					item.center,
					item.rotation,
					item.size,
					frame.width());
				p.drawImage(frame, rest.model);
			}
		});
	}
	p.restore();
}

PeerQrGiftField::PeerQrGiftField(
	PeerQrGiftSource source,
	PeerQrGiftLook look,
	Fn<void()> repaint,
	bool instant)
: _source(std::move(source))
, _look(std::move(look))
, _repaint(std::move(repaint)) {
	if (instant) {
		const auto settled = crl::now() - kSymbolStagger - kSymbolFadeDuration;
		_entryAt = settled;
		createArt();
		validateSymbol(settled);
		if (_model && _model->ready()) {
			_modelReadyAt = settled;
		}
	}
}

PeerQrGiftField::~PeerQrGiftField() = default;

const PeerQrGiftLook &PeerQrGiftField::look() const {
	return _look;
}

void PeerQrGiftField::setSource(PeerQrGiftSource source) {
	if (_source.patternFrame != source.patternFrame) {
		_symbol = QImage();
	}
	_source = std::move(source);
	if (_repaint) {
		_repaint();
	}
}

void PeerQrGiftField::startEntry(crl::time now) {
	if (!_entryAt) {
		_entryAt = now;
	}
}

void PeerQrGiftField::paint(QPainter &p, const PeerQrEmojiPaint &args) {
	const auto now = args.now;
	createArt();
	validateSymbol(now);
	if (_model && !_modelReadyAt && _model->ready()) {
		_modelReadyAt = now;
	}
	if (!_entryAt) {
		return;
	}
	const auto metrics = PeerQrGiftMetrics::Live();
	const auto entryAt = *_entryAt;
	const auto instant = anim::Disabled();
	const auto cull = PeerQrCardCull(args.card);
	p.save();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	if (!_symbol.isNull()) {
		const auto start = std::max(entryAt, _symbolReadyAt);
		const auto paintSymbol = [&](const Item &item) {
			const auto fade = instant ? 1. : anim::easeOutCubic(
				1.,
				EntryProgress(
					now,
					start + crl::time(kSymbolStagger * item.delay),
					kSymbolFadeDuration));
			if (fade <= 0. || PeerQrCovered(cull, item.center, item.size)) {
				return;
			}
			p.setOpacity(kSymbolAlpha * fade);
			p.drawImage(ItemRect(item), _symbol);
		};
		EnumerateSymbols(args.falloff, args.origin, metrics, paintSymbol);
		p.setOpacity(1.);
	}
	const auto frame = _modelReadyAt ? float64(_model->width()) : 0.;
	if (frame > 0.) {
		const auto start = std::max(entryAt, _modelReadyAt);
		const auto base = p.transform();
		const auto paintModel = [&](const Item &item) {
			const auto scale = instant ? 1. : anim::easeOutBack(
				1.,
				EntryProgress(
					now,
					start + crl::time(kModelStagger * item.delay),
					kModelPopDuration));
			const auto size = item.size * scale;
			if (size <= 0. || PeerQrCovered(cull, item.center, size)) {
				return;
			}
			PeerQrApplyItemTransform(
				p,
				base,
				item.center,
				item.rotation,
				size,
				frame);
			_model->paint(p, {
				.textColor = QColor(255, 255, 255),
				.now = now,
				.paused = args.paused,
			});
		};
		EnumerateModels(args.falloff, args.origin, metrics, paintModel);
	}
	p.restore();
}

bool PeerQrGiftField::animating(crl::time now) const {
	if (anim::Disabled()) {
		return false;
	} else if (!_entryAt) {
		return true;
	}
	const auto entryAt = *_entryAt;
	const auto entering = [&](crl::time readyAt) {
		return readyAt && (now < std::max(entryAt, readyAt) + kEntryDuration);
	};
	return entering(_modelReadyAt) || entering(_symbolReadyAt);
}

PeerQrGiftRest PeerQrGiftField::prepareRest(PeerQrGiftMetrics metrics) const {
	const auto frame = (_model && _model->ready()) ? _model->width() : 0;
	return {
		.model = ((frame > 0)
			? PeerQrRenderStill(_model.get(), frame, QColor(255, 255, 255))
			: QImage()),
		.symbol = _symbol,
		.metrics = metrics,
	};
}

void PeerQrGiftField::createArt() {
	if (!_model && _source.model && !_look.model.isEmpty()) {
		_model = _source.model(_look.model, _repaint);
	}
	if (!_pattern && _source.pattern && !_look.pattern.isEmpty()) {
		_pattern = _source.pattern(_look.pattern, _repaint);
	}
}

void PeerQrGiftField::validateSymbol(crl::time now) {
	if (!_symbol.isNull()
		|| !_pattern
		|| _source.patternFrame <= 0
		|| !_pattern->ready()) {
		return;
	}
	_symbol = PeerQrRenderStill(
		_pattern.get(),
		_source.patternFrame,
		_look.patternColor);
	if (!_symbolReadyAt) {
		_symbolReadyAt = now;
	}
}

} // namespace Ui
