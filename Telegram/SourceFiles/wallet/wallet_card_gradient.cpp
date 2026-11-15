/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_gradient.h"

#include "ui/painter.h"

#include <QtCore/QtMath>
#include <QtGui/QConicalGradient>
#include <QtGui/QLinearGradient>
#include <QtGui/QRadialGradient>

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kSweepStep = 15.;
constexpr auto kSweep = std::array<QRgb, 24>{
	0x0773fe, 0x046bfe, 0x0368fe, 0x046cff, 0x0872ff, 0x0b7aff,
	0x1084ff, 0x1594ff, 0x1ea3fe, 0x1da4fe, 0x169cfe, 0x0e94ff,
	0x098cff, 0x0483ff, 0x027eff, 0x057efe, 0x0980ff, 0x0c82ff,
	0x1083fe, 0x178efe, 0x1b9afe, 0x1a96fe, 0x1488ff, 0x0d7dff,
};
constexpr auto kQrPlateAngle = 119.72;
constexpr auto kQrPlateFrom = 0.0799;
constexpr auto kQrPlateTill = 0.922;
constexpr auto kQrPlateAlpha = 224; // 0.88 * 255, rounded.
constexpr auto kTintRampSteps = 4096;

struct TintLevels {
	float64 red[256] = {};
	float64 green[256] = {};
	float64 blue[256] = {};
	float64 offset = 0.;
};

struct SoftnessStop {
	float64 at = 0.;
	float64 alpha = 0.;
};

constexpr auto kSoftness = std::array<SoftnessStop, 12>{ {
	{ 0., 1. },
	{ 0.09, 0.87 },
	{ 0.18, 0.69 },
	{ 0.27, 0.46 },
	{ 0.36, 0.33 },
	{ 0.45, 0.24 },
	{ 0.55, 0.18 },
	{ 0.64, 0.15 },
	{ 0.73, 0.11 },
	{ 0.82, 0.05 },
	{ 0.91, 0.03 },
	{ 1., 0. },
} };

[[nodiscard]] QColor CardCenterColor() {
	return QColor(14, 127, 255);
}

[[nodiscard]] int CellsTill(int distance, int step) {
	return (distance + step - 1) / step + 1;
}

void RenderField(
		QImage &field,
		QSize cells,
		QPoint center,
		float64 angle) {
	if (field.size() != cells) {
		field = QImage(cells, QImage::Format_ARGB32_Premultiplied);
	}
	auto p = QPainter(&field);
	auto hq = PainterHighQualityEnabler(p);
	auto start = std::fmod(90. - angle, 360.);
	if (start < 0.) {
		start += 360.;
	}
	auto sweep = QConicalGradient(QPointF(center), start);
	for (auto i = 0; i != int(kSweep.size()); ++i) {
		const auto at = i ? (1. - i * kSweepStep / 360.) : 0.;
		sweep.setColorAt(at, QColor(kSweep[i]));
	}
	sweep.setColorAt(1., QColor(kSweep[0]));
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(field.rect(), sweep);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	auto soft = QRadialGradient(
		QPointF(center),
		st::walletCardSoftness / float64(st::walletCardFieldStep));
	for (const auto &stop : kSoftness) {
		auto color = CardCenterColor();
		color.setAlphaF(stop.alpha);
		soft.setColorAt(stop.at, color);
	}
	p.fillRect(field.rect(), soft);
}

void RenderBackground(
		QImage &image,
		QImage &field,
		QSize size,
		int ratio,
		float64 angle) {
	const auto step = st::walletCardFieldStep * ratio;
	const auto anchor = QPoint(size.width() / 2, size.height() / 2);
	const auto before = QPoint(
		CellsTill(anchor.x(), step),
		CellsTill(anchor.y(), step));
	const auto cells = QSize(
		before.x() + CellsTill(size.width() - anchor.x(), step),
		before.y() + CellsTill(size.height() - anchor.y(), step));
	RenderField(field, cells, before, angle);
	auto brush = QBrush(field);
	brush.setTransform(QTransform::fromTranslate(
		anchor.x() - before.x() * step,
		anchor.y() - before.y() * step).scale(step, step));
	if (image.size() != size) {
		image = QImage(size, QImage::Format_ARGB32_Premultiplied);
	}
	auto p = QPainter(&image);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(image.rect(), brush);
	p.end();
}

[[nodiscard]] std::vector<QRgb> MakeTintRamp(const CardTint &tint) {
	auto result = std::vector<QRgb>();
	result.reserve(kTintRampSteps + 1);
	for (auto i = 0; i <= kTintRampSteps; ++i) {
		result.push_back(CardTintAt(tint, i / float64(kTintRampSteps)).rgb());
	}
	return result;
}

[[nodiscard]] TintLevels ComputeTintLevels() {
	const auto [from, till] = ranges::minmax(
		kSweep | ranges::views::transform(CardLuminance));
	const auto scale = kTintRampSteps / (till - from);
	auto result = TintLevels{ .offset = 0.5 - from * scale };
	for (auto i = 0; i != 256; ++i) {
		result.red[i] = CardLuminance(qRgb(i, 0, 0)) * scale;
		result.green[i] = CardLuminance(qRgb(0, i, 0)) * scale;
		result.blue[i] = CardLuminance(qRgb(0, 0, i)) * scale;
	}
	return result;
}

[[nodiscard]] const TintLevels &Levels() {
	static const auto result = ComputeTintLevels();
	return result;
}

[[nodiscard]] TG_FORCE_INLINE int TintRampIndex(
		const TintLevels &levels,
		QRgb color) {
	const auto level = levels.red[qRed(color)]
		+ levels.green[qGreen(color)]
		+ levels.blue[qBlue(color)]
		+ levels.offset;
	return (level <= 0.)
		? 0
		: (level >= kTintRampSteps)
		? kTintRampSteps
		: int(level);
}

[[nodiscard]] QColor AverageColor(
		const std::array<QRgb, kSweep.size()> &colors) {
	auto red = 0.;
	auto green = 0.;
	auto blue = 0.;
	for (const auto rgb : colors) {
		const auto color = QColor(rgb);
		red += color.redF();
		green += color.greenF();
		blue += color.blueF();
	}
	const auto count = float64(colors.size());
	return QColor::fromRgbF(red / count, green / count, blue / count);
}

void TintImage(QImage &image, const std::vector<QRgb> &ramp) {
	const auto &levels = Levels();
	const auto colors = ramp.data();
	const auto stride = image.bytesPerLine();
	auto bytes = image.bits();
	for (auto y = 0; y != image.height(); ++y, bytes += stride) {
		auto pixel = reinterpret_cast<QRgb*>(bytes);
		for (const auto end = pixel + image.width(); pixel != end; ++pixel) {
			const auto color = *pixel;
			if (qAlpha(color) != 255) {
				continue;
			}
			*pixel = colors[TintRampIndex(levels, color)];
		}
	}
}

} // namespace

void CardBackground::paint(QPainter &p, const QRect &card, float64 angle) {
	if (card.isEmpty()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto device = card.size() * ratio;
	if (_image.isNull()
		|| _ratio != ratio
		|| _angle != angle
		|| _image.width() < device.width() + 2
		|| _image.height() < device.height() + 2) {
		const auto drawn = (_ratio == ratio) ? _drawn : QSize();
		const auto covered = drawn.expandedTo(card.size());
		RenderBackground(
			_image,
			_field,
			covered * ratio + QSize(2, 2),
			ratio,
			angle);
		if (_tint) {
			TintImage(_image, _tintRamp);
		}
		_drawn = card.size();
		_angle = angle;
		_ratio = ratio;
	} else {
		_drawn = _drawn.expandedTo(card.size());
	}
	const auto shift = QPointF(
		device.width() / 2 - _image.width() / 2,
		device.height() / 2 - _image.height() / 2) / ratio;
	auto brush = QBrush(_image);
	brush.setTransform(QTransform::fromTranslate(
		card.x() + shift.x(),
		card.y() + shift.y()).scale(1. / ratio, 1. / ratio));
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(brush);
	p.drawRoundedRect(card, st::walletCardRadius, st::walletCardRadius);
	p.setBrush(Qt::NoBrush);
}

void CardBackground::setTint(std::optional<CardTint> tint) {
	if (_tint == tint) {
		return;
	}
	_tint = std::move(tint);
	_tintRamp = _tint ? MakeTintRamp(*_tint) : std::vector<QRgb>();
	_image = QImage();
}

void CardBackground::clear() {
	_image = QImage();
	_field = QImage();
	_drawn = QSize();
	_ratio = 0;
}

QColor CardReferenceColor() {
	return AverageColor(kSweep);
}

QColor CardReferenceColor(const CardTint &tint) {
	const auto &levels = Levels();
	auto tinted = kSweep;
	for (auto &rgb : tinted) {
		const auto index = TintRampIndex(levels, rgb);
		rgb = CardTintAt(tint, index / float64(kTintRampSteps)).rgb();
	}
	return AverageColor(tinted);
}

void PaintCardQrPlate(QPainter &p, const QRect &plate) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radians = kQrPlateAngle * M_PI / 180.;
	const auto direction = QPointF(std::sin(radians), -std::cos(radians));
	const auto rect = QRectF(plate);
	const auto length = std::abs(rect.width() * direction.x())
		+ std::abs(rect.height() * direction.y());
	const auto half = direction * (length / 2.);
	auto gradient = QLinearGradient(
		rect.center() - half,
		rect.center() + half);
	gradient.setColorAt(kQrPlateFrom, QColor(255, 255, 255, kQrPlateAlpha));
	gradient.setColorAt(kQrPlateTill, QColor(187, 189, 192, kQrPlateAlpha));
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawRoundedRect(
		rect,
		st::walletCardQrRadius,
		st::walletCardQrRadius);
}

} // namespace Wallet
