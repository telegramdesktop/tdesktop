/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/peer_qr_surface.h"

#include "base/qt/qt_common_adapters.h"
#include "base/weak_qptr.h"
#include "qr/qr_generate.h"
#include "ui/effects/animated_string.h"
#include "ui/image/image_prepare.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtSvg/QSvgRenderer>

namespace Ui {
namespace {

constexpr auto kCardHeightRatio = 1.07;
constexpr auto kCardRadiusRatio = 0.125;
constexpr auto kQrSideRatio = 0.70;
constexpr auto kQrTopRatio = 0.12;
constexpr auto kLogoRatio = 0.175;
constexpr auto kInkContrast = 4.4;
constexpr auto kForegroundContrast = 4.5;
constexpr auto kInkChromaScale = 1.25;
constexpr auto kInkChromaAdd = 0.02;
constexpr auto kInkChromaMax = 0.20;
constexpr auto kDarkInkGlassAlpha = 0.35;
constexpr auto kWhiteGlyphGlassAlpha = 0.12;
constexpr auto kWhiteGlyphContrast = 3.;
constexpr auto kGlassAlpha = 0.25;
constexpr auto kGradientRadiusToCard = 1.6;
constexpr auto kGradientSteps = 8;
constexpr auto kGradientMiddleMix = 0.5;
constexpr auto kDarkEdgeFrom = 7.;
constexpr auto kDarkEdgeTill = 11.;
constexpr auto kDarkInkContrast = 2.3;
constexpr auto kShadowAlpha = 0.13;
constexpr auto kImageScale = 4;
constexpr auto kQrOversample = 2;
constexpr auto kSolveIterations = 24;
constexpr auto kPlaneCenterX = 0.46;
constexpr auto kPlaneCenterY = 0.525;
constexpr auto kBloomDuration = crl::time(800);
constexpr auto kBloomSpread = 0.6;
constexpr auto kFlyInDuration = crl::time(500);
constexpr auto kLoopPeriod = crl::time(900);
constexpr auto kPlaneMinWidth = 0.44;
constexpr auto kPlaneAxisAngle = 35.;
constexpr auto kPlaneAxisX = 0.452;
constexpr auto kPlaneAxisY = 0.394;
constexpr auto kPlaneTurn = 8.6;
constexpr auto kFlyInOffset = 0.55;
constexpr auto kFlyInScale = 0.5;
constexpr auto kStreakAlpha = 0.18;
constexpr auto kStreakLength = 0.45;
constexpr auto kStreakWidth = 0.05;
constexpr auto kStreakTravel = 2.5;
constexpr auto kStreakSourceAngle = 39.;
constexpr auto kStreakSourceDistance = 2.5;
constexpr auto kStreakLanes = std::array{ -0.78, -0.58, 0.66, -0.38, 0.80 };
constexpr auto kSwapDelay = crl::time(40);
constexpr auto kSwapDuration = crl::time(450);
constexpr auto kSwapTravel = crl::time(350);
constexpr auto kDustLife = crl::time(100);
constexpr auto kDustPerModule = 3;
constexpr auto kDustSizeMin = 0.35;
constexpr auto kDustSizeMax = 0.7;
constexpr auto kDustRiseMin = 0.6;
constexpr auto kDustRiseMax = 1.2;
constexpr auto kDustDrift = 0.5;
constexpr auto kBounceDuration = crl::time(320);
constexpr auto kBouncePeak = 1.03;
constexpr auto kBounceTrough = 0.995;
constexpr auto kBouncePeakAt = 0.25;
constexpr auto kBounceTroughAt = 0.6;
constexpr auto kWaveDuration = crl::time(600);
constexpr auto kWavePower = 2.4;
constexpr auto kWaveGlowAlpha = 0.38;
constexpr auto kWaveTintMix = 0.55;
// Qt's flattened circle strays from the true one by less than this
constexpr auto kCircleSlack = 1.;
constexpr auto kCircleSlackPerRadius = 0.001;
constexpr auto kDipDuration = crl::time(360);
constexpr auto kDipDepth = 0.03;
constexpr auto kFinderSize = 7;

struct CardLayout {
	QSize size;
	QRect qr;
	QRect logo;
	QRect band;
	int radius = 0;
};

struct UsernameFit {
	QFont font;
	QStringList lines;
};

struct CardContent {
	QImage shadow;
	QImage qr;
	Qr::Data data;
	QImage plane;
	UsernameFit username;
	QColor ink;
	QPoint shadowPosition;
};

struct QrSwap {
	QImage qr;
	Qr::Data data;
	crl::time startedAt = 0;
};

struct PlaneState {
	QPointF shift;
	float64 scale = 1.;
	float64 width = 1.;
	float64 rotation = 0.;
	float64 opacity = 1.;
	float64 streaks = 0.;
	float64 travel = 0.;
};

struct CardFrame {
	PlaneState plane;
	float64 bloom = 1.;
	const QrSwap *swap = nullptr;
	crl::time swapElapsed = 0;
};

struct BackgroundRequest {
	std::array<QImage, 4> corners;
	QSize size;
	QColor center;
	QColor edge;
	QPointF gradientCenter;
	float64 gradientRadius = 0.;
};

[[nodiscard]] QColor SolveLightness(
		float64 hue,
		float64 chroma,
		const QColor &against,
		float64 contrast) {
	auto from = 0.;
	auto till = ToLab(against).lightness;
	for (auto i = 0; i != kSolveIterations; ++i) {
		const auto middle = (from + till) / 2.;
		const auto color = FromLch(middle, chroma, hue);
		if (CountContrast(color, against) >= contrast) {
			from = middle;
		} else {
			till = middle;
		}
	}
	return FromLch(from, chroma, hue);
}

[[nodiscard]] QColor MixLab(
		const QColor &a,
		const QColor &b,
		float64 ratio) {
	const auto mix = [&](float64 from, float64 till) {
		return from + (till - from) * ratio;
	};
	const auto from = ToLab(a);
	const auto till = ToLab(b);
	const auto lab = Lab{
		.lightness = mix(from.lightness, till.lightness),
		.a = mix(from.a, till.a),
		.b = mix(from.b, till.b),
	};
	auto result = FromLch(lab.lightness, Chroma(lab), Hue(lab));
	result.setAlphaF(mix(a.alphaF(), b.alphaF()));
	return result;
}

[[nodiscard]] PeerQrColors MixColors(
		const PeerQrColors &a,
		const PeerQrColors &b,
		float64 ratio) {
	return {
		.ink = MixLab(a.ink, b.ink, ratio),
		.foreground = MixLab(a.foreground, b.foreground, ratio),
		.control = MixLab(a.control, b.control, ratio),
		.glass = MixLab(a.glass, b.glass, ratio),
	};
}

[[nodiscard]] QColor ComputeWaveTint(const PeerQrAppearance &appearance) {
	const auto center = appearance.center.isValid()
		? appearance.center
		: appearance.edge;
	return MixLab(center, QColor(255, 255, 255), kWaveTintMix);
}

[[nodiscard]] float64 InkChroma(float64 chroma) {
	return std::min(chroma * kInkChromaScale + kInkChromaAdd, kInkChromaMax);
}

[[nodiscard]] QColor BackdropInk(const PeerQrAppearance &appearance) {
	const auto white = QColor(255, 255, 255);
	const auto edge = ToLab(appearance.edge);
	const auto hue = Hue(edge);
	const auto shipped = SolveLightness(
		hue,
		InkChroma(Chroma(edge)),
		white,
		kInkContrast);
	const auto darkness = std::clamp(
		(CountContrast(appearance.edge, white) - kDarkEdgeFrom)
			/ (kDarkEdgeTill - kDarkEdgeFrom),
		0.,
		1.);
	if (darkness <= 0.) {
		return shipped;
	}
	// The recorded dark neutrals' ink is that much darker than their centre.
	const auto shade = SolveLightness(
		hue,
		Chroma(edge),
		appearance.center,
		kDarkInkContrast);
	if (CountContrast(shade, white) <= CountContrast(shipped, white)) {
		return shipped;
	}
	return (darkness >= 1.) ? shade : MixLab(shipped, shade, darkness);
}

[[nodiscard]] PeerQrColors ComputePeerQrColors(
		const PeerQrAppearance &appearance) {
	const auto white = QColor(255, 255, 255);
	const auto edge = ToLab(appearance.edge);
	const auto hue = Hue(edge);
	const auto chroma = InkChroma(Chroma(edge));
	const auto reference = MixLab(
		appearance.center,
		appearance.edge,
		kGradientMiddleMix);
	const auto stops = std::vector{
		appearance.center,
		reference,
		appearance.edge,
	};
	const auto darkness = [&](const QColor &color) {
		return CountContrast(color, white);
	};
	const auto darkest = *ranges::max_element(
		stops,
		ranges::less(),
		darkness);
	const auto dark = SolveLightness(
		hue,
		chroma,
		darkest,
		kForegroundContrast);
	const auto glass = [&](float64 alpha) {
		return anim::color(reference, white, alpha);
	};
	const auto whiteContrast = CountContrast(
		white,
		glass(kWhiteGlyphGlassAlpha));
	const auto whiteGlyphs = (whiteContrast >= kWhiteGlyphContrast)
		|| (whiteContrast >= CountContrast(dark, glass(kDarkInkGlassAlpha)));
	return {
		.ink = BackdropInk(appearance),
		.foreground = whiteGlyphs ? white : dark,
		.control = anim::with_alpha(
			white,
			whiteGlyphs ? kWhiteGlyphGlassAlpha : kDarkInkGlassAlpha),
		.glass = anim::with_alpha(white, kGlassAlpha),
	};
}

[[nodiscard]] bool SameBackdrop(PeerQrAppearance a, PeerQrAppearance b) {
	a.emoji.clear();
	b.emoji.clear();
	return a == b;
}

[[nodiscard]] float64 DipScale(crl::time startedAt, crl::time now) {
	const auto progress = float64(now - startedAt) / kDipDuration;
	return (!startedAt || progress <= 0. || progress >= 1.)
		? 1.
		: (1. - kDipDepth * std::sin(M_PI * progress));
}

[[nodiscard]] CardLayout ComputeCardLayout(int width, bool withUsername) {
	const auto part = [&](float64 ratio) {
		return int(base::SafeRound(width * ratio));
	};
	const auto height = withUsername ? part(kCardHeightRatio) : width;
	const auto side = part(kQrSideRatio);
	const auto left = (width - side) / 2;
	const auto qr = QRect(
		left,
		withUsername ? part(kQrTopRatio) : left,
		side,
		side);
	const auto logo = int(base::SafeRound(side * kLogoRatio));
	const auto bottom = qr.y() + qr.height();
	return {
		.size = QSize(width, height),
		.qr = qr,
		.logo = QRect(
			qr.x() + (side - logo) / 2,
			qr.y() + (side - logo) / 2,
			logo,
			logo),
		.band = QRect(0, bottom, width, height - bottom),
		.radius = part(kCardRadiusRatio),
	};
}

[[nodiscard]] QStringList SplitInTwoLines(
		const QString &text,
		const QFontMetricsF &metrics) {
	auto best = 1;
	auto bestWidth = std::numeric_limits<float64>::max();
	for (auto i = 1; i < text.size(); ++i) {
		const auto wider = std::max(
			metrics.horizontalAdvance(text.left(i)),
			metrics.horizontalAdvance(text.mid(i)));
		if (wider < bestWidth) {
			best = i;
			bestWidth = wider;
		}
	}
	return { text.left(best), text.mid(best) };
}

[[nodiscard]] bool LinesFit(
		const QStringList &lines,
		const QFontMetricsF &metrics,
		int available,
		int bandHeight) {
	return (lines.size() * metrics.height() <= bandHeight)
		&& ranges::all_of(lines, [&](const QString &line) {
			return metrics.horizontalAdvance(line) <= available;
		});
}

[[nodiscard]] UsernameFit FitUsername(
		const QString &text,
		int available,
		int bandHeight,
		const std::array<QFont, 3> &steps,
		int floorPixelSize) {
	if (text.isEmpty()) {
		return {};
	}
	const auto single = QStringList{ text };
	const auto fitsLine = [&](const QFont &font) {
		return LinesFit(single, QFontMetricsF(font), available, bandHeight);
	};
	for (const auto &font : steps) {
		if (fitsLine(font)) {
			return { .font = font, .lines = single };
		}
	}
	auto font = steps.back();
	const auto small = font.pixelSize();
	for (auto size = small - 1; size >= floorPixelSize; --size) {
		font.setPixelSize(size);
		if (fitsLine(font)) {
			return { .font = font, .lines = single };
		}
	}
	for (auto size = small; ; --size) {
		font.setPixelSize(size);
		const auto metrics = QFontMetricsF(font);
		auto lines = SplitInTwoLines(text, metrics);
		const auto fits = LinesFit(lines, metrics, available, bandHeight);
		if (fits || size <= floorPixelSize) {
			return { .font = font, .lines = std::move(lines) };
		}
	}
}

[[nodiscard]] QImage GenerateFinalQr(
		const Qr::Data &data,
		int side,
		const QColor &ink) {
	const auto pixel = (kQrOversample * side + data.size - 1) / data.size;
	return Qr::Generate(data, pixel, ink, Qt::transparent).scaled(
		side,
		side,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
}

[[nodiscard]] QImage PrepareQrImage(
		const Qr::Data &data,
		int side,
		const QColor &ink,
		int ratio) {
	auto result = (data.size > 0)
		? GenerateFinalQr(data, side * ratio, ink)
		: QImage();
	result.setDevicePixelRatio(ratio);
	return result;
}

[[nodiscard]] QImage PrepareCardShadow(
		QSize size,
		int radius,
		int blur,
		float64 alpha,
		int ratio) {
	auto mask = QImage(
		(size + QSize(2 * blur, 2 * blur)) * ratio,
		QImage::Format_RGB32);
	mask.fill(Qt::black);
	mask.setDevicePixelRatio(ratio);
	{
		auto p = QPainter(&mask);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(QRect(QPoint(blur, blur), size), radius, radius);
	}
	return style::colorizeImage(
		Images::BlurLargeImage(std::move(mask), blur * ratio),
		anim::with_alpha(QColor(0, 0, 0), alpha));
}

[[nodiscard]] QImage PreparePlane(int side, int ratio) {
	auto result = QImage(
		QSize(side, side) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		auto svg = QSvgRenderer(u":/gui/plane_white.svg"_q);
		svg.render(&p, QRectF(QPointF(), QSizeF(result.size())));
	}
	result.setDevicePixelRatio(ratio);
	return result;
}

[[nodiscard]] CardContent PrepareCardContent(
		const CardLayout &layout,
		const QString &link,
		const QColor &ink,
		int shadowRadius,
		int shadowShift,
		int ratio) {
	auto data = link.isEmpty() ? Qr::Data() : Qr::Encode(link);
	auto qr = PrepareQrImage(data, layout.qr.width(), ink, ratio);
	return {
		.shadow = PrepareCardShadow(
			layout.size,
			layout.radius,
			shadowRadius,
			kShadowAlpha,
			ratio),
		.qr = std::move(qr),
		.data = std::move(data),
		.plane = PreparePlane(layout.logo.width(), ratio),
		.ink = ink,
		.shadowPosition = QPoint(-shadowRadius, shadowShift - shadowRadius),
	};
}

void PaintAppearanceGradient(
		QPainter &p,
		const QRect &rect,
		QPointF center,
		float64 radius,
		const QColor &centerColor,
		const QColor &edgeColor) {
	auto stops = QGradientStops();
	stops.reserve(kGradientSteps + 1);
	for (auto i = 0; i <= kGradientSteps; ++i) {
		const auto ratio = float64(i) / kGradientSteps;
		stops.push_back({ ratio, MixLab(centerColor, edgeColor, ratio) });
	}
	auto gradient = QRadialGradient(center, radius);
	gradient.setStops(stops);
	p.fillRect(rect, gradient);
}

[[nodiscard]] QImage ComposeColorBackground(
		const BackgroundRequest &request) {
	auto result = QImage(request.size, QImage::Format_ARGB32_Premultiplied);
	auto p = QPainter(&result);
	const auto rect = QRect(QPoint(), request.size);
	PaintAppearanceGradient(
		p,
		rect,
		request.gradientCenter,
		request.gradientRadius,
		request.center,
		request.edge);
	p.end();
	return result;
}

[[nodiscard]] QImage ComposeBackground(const BackgroundRequest &request) {
	if (request.size.isEmpty() || !request.center.isValid()) {
		return QImage();
	}
	auto result = ComposeColorBackground(request);
	return request.corners.front().isNull()
		? result
		: Images::Round(std::move(result), request.corners);
}

[[nodiscard]] float64 FarthestCorner(QPointF center, QSizeF size) {
	return std::hypot(
		std::max(center.x(), size.width() - center.x()),
		std::max(center.y(), size.height() - center.y()));
}

[[nodiscard]] float64 WaveRadius(float64 farthest, float64 progress) {
	return (farthest + st::peerQrWaveOvershoot)
		* (1. - std::pow(1. - progress, kWavePower));
}

enum class WaveSide {
	Outside,
	Inside,
	Edge,
};

[[nodiscard]] WaveSide CircleSide(
		QPointF center,
		float64 radius,
		const std::vector<QRect> &parts) {
	const auto slack = kCircleSlack + radius * kCircleSlackPerRadius;
	auto inside = true;
	auto outside = true;
	for (const auto &part : parts) {
		const auto box = QRectF(part);
		const auto nearest = std::hypot(
			std::max({ box.left() - center.x(), 0., center.x() - box.right() }),
			std::max({ box.top() - center.y(), 0., center.y() - box.bottom() }));
		const auto farthest = std::hypot(
			std::max(
				std::abs(box.left() - center.x()),
				std::abs(box.right() - center.x())),
			std::max(
				std::abs(box.top() - center.y()),
				std::abs(box.bottom() - center.y())));
		inside = inside && (farthest <= radius - slack);
		outside = outside && (nearest >= radius + slack);
	}
	return inside
		? WaveSide::Inside
		: outside
		? WaveSide::Outside
		: WaveSide::Edge;
}

[[nodiscard]] bool MeetsCorner(const QRect &box, const QRect &area) {
	const auto radius = st::boxRadius;
	return !box.marginsRemoved({ radius, 0, radius, 0 }).contains(area)
		&& !box.marginsRemoved({ 0, radius, 0, radius }).contains(area);
}

void PaintWaveGlow(
		QPainter &p,
		const QRect &rect,
		const QRect &area,
		QPointF center,
		float64 radius,
		float64 progress,
		const QColor &tint) {
	const auto alpha = kWaveGlowAlpha * (1. - progress);
	const auto half = (st::peerQrWaveGlowFrom
		+ (st::peerQrWaveGlowTill - st::peerQrWaveGlowFrom) * progress) / 2.;
	if (radius <= 0. || alpha <= 0. || half <= 0.) {
		return;
	}
	const auto outer = radius + half;
	auto transparent = tint;
	transparent.setAlpha(0);
	auto peak = tint;
	peak.setAlphaF(alpha);
	auto stops = QGradientStops();
	if (radius > half) {
		stops.push_back({ (radius - half) / outer, transparent });
	} else {
		auto inner = tint;
		inner.setAlphaF(alpha * (1. - radius / half));
		stops.push_back({ 0., inner });
	}
	stops.push_back({ radius / outer, peak });
	stops.push_back({ 1., transparent });
	auto gradient = QRadialGradient(center, outer);
	gradient.setStops(stops);
	const auto bounds = QRectF(
		center - QPointF(outer, outer),
		QSizeF(2. * outer, 2. * outer));
	const auto fill = bounds.intersected(QRectF(area));
	auto hq = PainterHighQualityEnabler(p);
	if (!MeetsCorner(rect, area)) {
		p.fillRect(fill, gradient);
		return;
	}
	auto rounded = QPainterPath();
	rounded.addRoundedRect(QRectF(rect), st::boxRadius, st::boxRadius);
	p.save();
	p.setClipPath(rounded, Qt::IntersectClip);
	p.fillRect(fill, gradient);
	p.restore();
}

void ClipOutside(
		QPainter &p,
		const QRectF &area,
		const QPainterPath &shapes) {
	if (shapes.isEmpty()) {
		return;
	}
	auto outside = QPainterPath();
	outside.setFillRule(Qt::OddEvenFill);
	outside.addRect(area);
	outside.addPath(shapes);
	p.setClipPath(outside, Qt::IntersectClip);
}

[[nodiscard]] PlaneState ComputePlaneState(
		crl::time elapsed,
		float64 radius) {
	const auto travel = float64(elapsed % kLoopPeriod) / kLoopPeriod;
	if (elapsed < kFlyInDuration) {
		const auto progress = float64(elapsed) / kFlyInDuration;
		const auto eased = anim::easeOutCubic(1., progress);
		const auto offset = kFlyInOffset * radius * (1. - eased) / M_SQRT2;
		return {
			.shift = QPointF(-offset, offset),
			.scale = kFlyInScale + (1. - kFlyInScale) * eased,
			.opacity = std::min(2. * progress, 1.),
			.streaks = progress,
			.travel = travel,
		};
	}
	const auto phase = float64((elapsed - kFlyInDuration) % kLoopPeriod)
		/ kLoopPeriod;
	const auto roll = (1. - std::cos(2. * M_PI * phase)) / 2.;
	return {
		.width = 1. - (1. - kPlaneMinWidth) * roll,
		.rotation = -kPlaneTurn * roll,
		.streaks = 1.,
		.travel = travel,
	};
}

[[nodiscard]] CardFrame ComputeCardFrame(
		crl::time elapsed,
		float64 radius) {
	return {
		.plane = ComputePlaneState(elapsed, radius),
		.bloom = std::min(float64(elapsed) / kBloomDuration, 1.),
	};
}

void PaintStreaks(QPainter &p, const QRect &logo, const PlaneState &state) {
	const auto radius = logo.width() / 2.;
	const auto center = QRectF(logo).center();
	const auto half = kStreakLength * radius / 2.;
	const auto angle = kStreakSourceAngle * M_PI / 180.;
	const auto distance = kStreakSourceDistance * radius;
	// Lanes share one vanishing point, so no two streaks cross in the circle.
	const auto source = center
		+ QPointF(std::cos(angle), -std::sin(angle)) * distance;
	auto color = QColor(255, 255, 255);
	color.setAlphaF(kStreakAlpha * state.streaks);
	p.setPen(QPen(
		color,
		kStreakWidth * radius,
		Qt::SolidLine,
		Qt::RoundCap));
	const auto count = int(kStreakLanes.size());
	for (auto i = 0; i != count; ++i) {
		const auto lane = angle
			+ std::asin(kStreakLanes[i] / kStreakSourceDistance);
		const auto along = QPointF(std::cos(lane), -std::sin(lane));
		const auto foot = source
			- along * QPointF::dotProduct(source - center, along);
		const auto travel = std::fmod(state.travel + float64(i) / count, 1.);
		const auto middle = foot
			+ along * (kStreakTravel * radius * (0.5 - travel));
		p.drawLine(middle - along * half, middle + along * half);
	}
}

void PaintPlane(
		QPainter &p,
		const QRect &logo,
		const QImage &plane,
		const PlaneState &state) {
	if (state.opacity <= 0.) {
		return;
	}
	const auto center = QPointF(
		logo.x() + logo.width() * kPlaneCenterX,
		logo.y() + logo.height() * kPlaneCenterY);
	const auto anchor = QPointF(
		logo.x() + logo.width() * kPlaneAxisX,
		logo.y() + logo.height() * kPlaneAxisY);
	auto circle = QPainterPath();
	circle.addEllipse(logo);
	p.save();
	p.setClipPath(circle);
	if (state.streaks > 0.) {
		PaintStreaks(p, logo, state);
	}
	p.translate(center + state.shift);
	p.scale(state.scale, state.scale);
	p.translate(anchor - center);
	p.rotate(state.rotation - kPlaneAxisAngle);
	p.scale(1., state.width);
	p.rotate(kPlaneAxisAngle);
	p.translate(-anchor);
	p.setOpacity(state.opacity);
	p.drawImage(logo, plane);
	p.restore();
}

void PaintUsername(
		QPainter &p,
		const QRect &band,
		const UsernameFit &username,
		const QColor &ink) {
	const auto metrics = QFontMetricsF(username.font);
	const auto height = metrics.height();
	const auto lines = int(username.lines.size());
	auto top = band.y() + (band.height() - lines * height) / 2.;
	p.setFont(username.font);
	p.setPen(ink);
	for (const auto &line : username.lines) {
		const auto width = metrics.horizontalAdvance(line);
		const auto left = band.x() + (band.width() - width) / 2.;
		p.drawText(QPointF(left, top + metrics.ascent()), line);
		top += height;
	}
}

void PaintBloom(
		QPainter &p,
		const QRect &qr,
		const CardContent &content,
		float64 bloom) {
	const auto &data = content.data;
	const auto count = data.size;
	const auto cell = qr.width() / float64(count);
	const auto middle = count / 2.;
	const auto corner = std::hypot(middle - 0.5, middle - 0.5);
	const auto blankFrom = (count - Qr::ReplaceSize(data, 1)) / 2;
	const auto blankTill = count - blankFrom;
	const auto blank = [&](int index) {
		return (index >= blankFrom) && (index < blankTill);
	};
	const auto dark = [&](int row, int column) {
		return data.values[row * count + column]
			&& !(blank(row) && blank(column));
	};
	const auto progress = [&](int row, int column) {
		const auto distance = std::hypot(
			column + 0.5 - middle,
			row + 0.5 - middle) / corner;
		return std::clamp(
			(bloom - kBloomSpread * distance) / (1. - kBloomSpread),
			0.,
			1.);
	};
	auto finished = QPainterPath();
	p.setBrush(content.ink);
	for (auto row = 0; row != count; ++row) {
		const auto top = qr.y() + row * cell;
		auto from = -1;
		for (auto column = 0; column <= count; ++column) {
			const auto value = (column < count) ? progress(row, column) : 0.;
			if (value >= 1.) {
				if (from < 0) {
					from = column;
				}
				continue;
			} else if (from >= 0) {
				finished.addRect(QRectF(
					qr.x() + from * cell,
					top,
					(column - from) * cell,
					cell));
				from = -1;
			}
			if (value > 0. && dark(row, column)) {
				const auto radius = cell * anim::easeOutCubic(1., value) / 2.;
				p.drawEllipse(
					QPointF(qr.x() + (column + 0.5) * cell, top + cell / 2.),
					radius,
					radius);
			}
		}
	}
	if (!finished.isEmpty()) {
		p.save();
		p.setClipPath(finished);
		p.drawImage(qr, content.qr);
		p.restore();
	}
}

[[nodiscard]] float64 DustRandom(uint32 &seed) {
	seed = PeerQrHash(seed + 0x9E3779B9U);
	return seed / 4294967296.;
}

void PaintSwap(
		QPainter &p,
		const QRect &qr,
		const CardContent &content,
		const QrSwap &swap,
		crl::time elapsed) {
	if (elapsed < 0) {
		p.drawImage(qr, swap.qr);
		return;
	}
	const auto travel = std::min(float64(elapsed) / kSwapTravel, 1.);
	const auto bottom = qr.y() + qr.height();
	const auto front = bottom - travel * qr.height();
	const auto paintPart = [&](
			const QImage &image,
			float64 from,
			float64 till) {
		if (till > from) {
			p.save();
			p.setClipRect(
				QRectF(qr.x(), from, qr.width(), till - from),
				Qt::IntersectClip);
			p.drawImage(qr, image);
			p.restore();
		}
	};
	paintPart(swap.qr, qr.y(), front);
	paintPart(content.qr, front, bottom);

	const auto &data = swap.data;
	const auto count = data.size;
	if (count <= 0) {
		return;
	}
	const auto cell = qr.width() / float64(count);
	const auto speed = qr.height() / float64(kSwapTravel);
	const auto blankFrom = (count - Qr::ReplaceSize(data, 1)) / 2;
	const auto blankTill = count - blankFrom;
	const auto blank = [&](int index) {
		return (index >= blankFrom) && (index < blankTill);
	};
	const auto leading = [&](int index) {
		return index < kFinderSize;
	};
	const auto trailing = [&](int index) {
		return index >= count - kFinderSize;
	};
	const auto dusty = [&](int row, int column) {
		return data.values[row * count + column]
			&& !(blank(row) && blank(column))
			&& !(leading(row) && (leading(column) || trailing(column)))
			&& !(trailing(row) && leading(column));
	};
	for (auto row = 0; row != count; ++row) {
		const auto release = kSwapTravel * (1. - (row + 0.5) / count);
		const auto age = elapsed - release;
		if (age < 0. || age >= kDustLife) {
			continue;
		}
		const auto progress = age / kDustLife;
		auto color = content.ink;
		color.setAlphaF(color.alphaF() * (1. - progress));
		p.setBrush(color);
		for (auto column = 0; column != count; ++column) {
			if (!dusty(row, column)) {
				continue;
			}
			for (auto k = 0; k != kDustPerModule; ++k) {
				auto seed = (uint32(row) << 16)
					| (uint32(column) << 4)
					| uint32(k);
				const auto x = column + DustRandom(seed);
				const auto y = row + DustRandom(seed);
				const auto size = kDustSizeMin
					+ (kDustSizeMax - kDustSizeMin) * DustRandom(seed);
				const auto rise = kDustRiseMin
					+ (kDustRiseMax - kDustRiseMin) * DustRandom(seed);
				const auto drift = kDustDrift * (2. * DustRandom(seed) - 1.);
				const auto radius = size * cell / 2.;
				p.drawEllipse(
					QPointF(
						qr.x() + (x + drift * progress) * cell,
						qr.y() + y * cell - rise * speed * age),
					radius,
					radius);
			}
		}
	}
}

void PaintCard(
		QPainter &p,
		const CardLayout &layout,
		const CardContent &content,
		const CardFrame &frame,
		bool paintUsername = true) {
	p.drawImage(content.shadowPosition, content.shadow);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	p.drawRoundedRect(
		QRect(QPoint(), layout.size),
		layout.radius,
		layout.radius);
	if (frame.swap || !content.qr.isNull()) {
		if (frame.swap) {
			p.save();
			p.setClipRect(QRect(QPoint(), layout.size));
			PaintSwap(p, layout.qr, content, *frame.swap, frame.swapElapsed);
			p.restore();
		} else if (frame.bloom >= 1.) {
			p.drawImage(layout.qr, content.qr);
		} else {
			PaintBloom(p, layout.qr, content, frame.bloom);
		}
		p.setBrush(content.ink);
		p.drawEllipse(layout.logo);
		PaintPlane(p, layout.logo, content.plane, frame.plane);
	}
	if (paintUsername && !content.username.lines.isEmpty()) {
		PaintUsername(p, layout.band, content.username, content.ink);
	}
}

} // namespace

struct PeerQrSurface::Card {
	CardLayout layout;
	CardContent content;
	std::optional<QrSwap> swap;
	QImage qrMask;
};

struct PeerQrSurface::Countdown {
	style::font font = st::peerQrUsernameLarge;
	std::vector<std::unique_ptr<AnimatedString>> glyphs;
};

struct PeerQrSurface::Control {
	base::weak_qptr<QWidget> widget;
	Fn<QPainterPath()> shape;
	Fn<float64()> opacity;
};

struct PeerQrSurface::ControlShapes {
	struct Fading {
		QPainterPath path;
		QPainterPath shown;
		std::vector<QRect> parts;
		float64 opacity = 0.;
	};
	QPainterPath shown;
	std::vector<Fading> fading;
};

int PeerQrCardHeight(int cardWidth, bool withUsername) {
	return ComputeCardLayout(cardWidth, withUsername).size.height();
}

float64 PeerQrBounceScale(float64 progress) {
	const auto phase = [&](float64 from, float64 till) {
		return (progress - from) / (till - from);
	};
	if (progress < 0. || progress >= 1.) {
		return 1.;
	} else if (progress <= kBouncePeakAt) {
		const auto rise = anim::easeOutCubic(1., phase(0., kBouncePeakAt));
		return 1. + (kBouncePeak - 1.) * rise;
	} else if (progress <= kBounceTroughAt) {
		const auto fall = anim::sineInOut(
			1.,
			phase(kBouncePeakAt, kBounceTroughAt));
		return kBouncePeak + (kBounceTrough - kBouncePeak) * fall;
	}
	const auto settle = anim::sineInOut(1., phase(kBounceTroughAt, 1.));
	return kBounceTrough + (1. - kBounceTrough) * settle;
}

PeerQrAppearance PeerQrDefaultAppearance() {
	return {
		.edge = QColor(0x0e, 0x0f, 0x0f),
		.center = QColor(0x36, 0x37, 0x38),
	};
}

QImage RenderPeerQrImage(PeerQrImageArgs args) {
	const auto layout = ComputeCardLayout(
		args.cardWidth,
		!args.usernameLines.isEmpty());
	auto content = PrepareCardContent(
		layout,
		args.link,
		args.colors.ink,
		args.shadowRadius,
		args.shadowShift,
		1);
	content.username = UsernameFit{
		.font = std::move(args.usernameFont),
		.lines = std::move(args.usernameLines),
	};
	const auto margin = QSize(2 * args.margin, 2 * args.margin);
	const auto size = layout.size + margin;
	const auto card = QRectF(
		QPointF(args.margin, args.margin),
		QSizeF(layout.size));
	const auto &appearance = args.appearance;
	auto result = ComposeBackground({
		.size = size,
		.center = appearance.center,
		.edge = appearance.edge,
		.gradientCenter = card.center(),
		.gradientRadius = kGradientRadiusToCard * args.cardWidth,
	});
	if (!result.isNull()) {
		const auto falloff = PeerQrLayerFalloff{
			.area = QRectF(QPointF(), QSizeF(size)),
			.band = args.layerBand,
		};
		auto p = QPainter(&result);
		PaintPeerQrEmojiRest(
			p,
			falloff,
			card.center() + args.layerOffset,
			card,
			args.emoji);
		PaintPeerQrGiftRest(
			p,
			falloff,
			card.center() + args.layerOffset,
			card,
			args.gift);
		p.translate(args.margin, args.margin);
		PaintCard(p, layout, content, CardFrame());
	}
	return result;
}

PeerQrSurface::PeerQrSurface(
	QWidget *parent,
	PeerQrAppearance appearance)
: RpWidget(parent)
, _appearance(std::move(appearance))
, _colors(ComputePeerQrColors(_appearance))
, _shownColors(_colors)
, _card(std::make_unique<Card>())
, _corners(Images::CornersMask(st::boxRadius))
, _animation([=](crl::time now) { return animationCallback(now); }) {
	setAttribute(Qt::WA_AcceptTouchEvents);
	sizeValue() | rpl::skip(1) | rpl::on_next([=] {
		composeNow();
	}, lifetime());
	PowerSaving::Changes() | rpl::on_next([=] {
		refreshFieldClock();
		checkAnimation();
		update();
	}, lifetime());
	if (!_appearance.emoji.empty()) {
		ensureField();
		_field->setPicks(
			_appearance.emoji,
			PeerQrEmojiChange::Instant,
			crl::now());
	}
	if (_appearance.gift) {
		_giftField = std::make_unique<PeerQrGiftField>(
			_giftSource,
			_appearance.gift,
			crl::guard(this, [=] { updateField(); }),
			true);
	}
}

PeerQrSurface::~PeerQrSurface() = default;

void PeerQrSurface::setLink(const QString &link, anim::type animated) {
	if (_link == link) {
		return;
	}
	const auto swap = (animated == anim::type::normal)
		&& !anim::Disabled()
		&& (_startedAt > 0)
		&& !_cardGeometry.isEmpty()
		&& !link.isEmpty();
	if (swap) {
		const auto startedAt = crl::now() + kSwapDelay;
		_card->swap = QrSwap{
			.qr = base::take(_card->content.qr),
			.data = base::take(_card->content.data),
			.startedAt = startedAt,
		};
		_bounceStartedAt = startedAt;
		_blooming = false;
	} else {
		_card->swap = std::nullopt;
	}
	_link = link;
	refreshQr();
	if (swap) {
		checkAnimation();
	}
}

void PeerQrSurface::setUsername(const QString &username) {
	const auto wasCountdown = (_countdown != nullptr);
	_countdown = nullptr;
	if (_username != username) {
		_username = username;
		refreshCard();
	} else if (wasCountdown) {
		update(labelUpdateRect());
	}
}

void PeerQrSurface::setCountdown(const QString &countdown) {
	if (_countdown && _username == countdown) {
		return;
	}
	const auto instant = !_countdown
		|| (_username == u"0:00"_q)
		|| (countdown == u"0:00"_q)
		|| anim::Disabled();
	const auto layoutChanged = _username.isEmpty() != countdown.isEmpty();
	if (!_countdown) {
		_countdown = std::make_unique<Countdown>();
	}
	_username = countdown;
	if (layoutChanged) {
		refreshCard();
	} else {
		refreshUsername(instant ? anim::type::instant : anim::type::normal);
		update(labelUpdateRect());
	}
}

void PeerQrSurface::setCardGeometry(QRect card) {
	if (_cardGeometry != card) {
		const auto moved = (_cardGeometry.center() != card.center());
		_cardGeometry = card;
		refreshCard();
		if (moved && _appearance.center.isValid()) {
			composeNow();
		}
	}
}

void PeerQrSurface::setCardPlacement(QPointF topLeft, float64 scale) {
	if (_cardPlacement
		&& _cardPlacement->topLeft == topLeft
		&& _cardPlacement->scale == scale) {
		return;
	}
	_cardPlacement = CardPlacement{ .topLeft = topLeft, .scale = scale };
	update();
}

void PeerQrSurface::setAppearance(
		PeerQrAppearance appearance,
		anim::type animated) {
	if (_appearance == appearance) {
		return;
	}
	using Change = PeerQrEmojiChange;
	const auto now = crl::now();
	const auto animatedNow = (animated == anim::type::normal)
		&& !anim::Disabled()
		&& (_startedAt > 0);
	const auto wasEmoji = !_appearance.emoji.empty();
	const auto isEmoji = !appearance.emoji.empty();
	const auto change = animatedNow ? Change::Swap : Change::Instant;
	if (isEmoji
		&& !_fieldLeaving
		&& SameBackdrop(_appearance, appearance)) {
		const auto entering = !_field;
		_appearance = std::move(appearance);
		ensureField();
		if (entering) {
			_field->setPicks(
				_appearance.emoji,
				animatedNow ? Change::Entry : Change::Instant,
				now);
			if (animatedNow) {
				_field->startEntry(now);
			}
		} else {
			_field->setPicks(_appearance.emoji, change, now);
		}
		if (animatedNow) {
			_dipStartedAt = now;
		}
		checkAnimation();
		return;
	}
	const auto wave = animatedNow && !_cardGeometry.isEmpty();
	auto from = wave ? currentPicture(now, false) : QImage();
	auto clean = (wave && _wave && !_wave->clean.isNull())
		? currentPicture(now, true)
		: QImage();
	if (wave && (_fieldLeaving || _giftLeaving)) {
		if (clean.isNull()) {
			clean = from;
		}
		auto p = QPainter(&from);
		if (_fieldLeaving) {
			paintField(p, now, QPainterPath());
		}
		if (_giftLeaving) {
			paintGift(p, _giftLeaving.get(), now, true, QPainterPath());
		}
		p.end();
		if (base::take(_fieldLeaving)) {
			_field = nullptr;
		}
		_giftLeaving = nullptr;
	}
	_appearance = std::move(appearance);
	if (isEmoji) {
		const auto entering = !wasEmoji || !_field;
		ensureField();
		if (entering) {
			_field->setPicks(
				_appearance.emoji,
				wave ? Change::Entry : Change::Instant,
				now);
			_entryPending = wave;
		} else {
			_field->setPicks(_appearance.emoji, change, now);
		}
		_fieldLeaving = false;
	} else if (_field) {
		_entryPending = false;
		_fieldLeaving = wave;
		if (!wave) {
			_field = nullptr;
		}
	}
	if (_giftField && _giftField->look() != _appearance.gift) {
		_giftLeaving = base::take(_giftField);
	}
	if (!wave) {
		_giftLeaving = nullptr;
	}
	if (_appearance.gift && !_giftField) {
		_giftField = std::make_unique<PeerQrGiftField>(
			_giftSource,
			_appearance.gift,
			crl::guard(this, [=] { updateField(); }),
			!wave);
		_entryPending = _entryPending || wave;
		checkAnimation();
	}
	_colors = ComputePeerQrColors(_appearance);
	composeNow();
	if (isEmoji) {
		_field->setTextColor(_colors.foreground);
	}
	if (!wave) {
		_wave = std::nullopt;
		if (base::take(_entryPending)) {
			startFieldEntry(now);
		}
		setShownColors(_colors);
		return;
	}
	const auto center = cardRect().center();
	_wave = Wave{
		.from = std::move(from),
		.clean = std::move(clean),
		.colorsFrom = _shownColors,
		.tint = ComputeWaveTint(_appearance),
		.center = center,
		.farthest = FarthestCorner(center, QSizeF(size())),
		.startedAt = now,
	};
	_bounceStartedAt = now;
	checkAnimation();
}

void PeerQrSurface::setEmojiSource(PeerQrEmojiSource source) {
	_emojiSource = source;
	if (_field) {
		_field->setSource(std::move(source));
	}
}

void PeerQrSurface::setGiftSource(PeerQrGiftSource source) {
	_giftSource = std::move(source);
	if (_giftField) {
		_giftField->setSource(_giftSource);
	}
	if (_giftLeaving) {
		_giftLeaving->setSource(_giftSource);
	}
}

void PeerQrSurface::startAnimations() {
	_startedAt = crl::now();
	_fieldFrozenAt = _fieldShift = 0;
	_blooming = true;
	refreshFieldClock();
	checkAnimation();
}

const PeerQrAppearance &PeerQrSurface::appearance() const {
	return _appearance;
}

const PeerQrColors &PeerQrSurface::colors() const {
	return _shownColors;
}

rpl::producer<PeerQrColors> PeerQrSurface::colorsValue() const {
	return _colorsChanges.events_starting_with_copy(_shownColors);
}

rpl::producer<not_null<QTouchEvent*>> PeerQrSurface::touchPresses() const {
	return _touchPresses.events();
}

PeerQrImageArgs PeerQrSurface::prepareImage() const {
	const auto scaled = [](int value) {
		const auto recovered = value * 100. / style::Scale();
		return kImageScale * int(base::SafeRound(recovered));
	};
	const auto scaledFont = [&](const style::font &font) {
		auto result = font->f;
		result.setPixelSize(scaled(font->f.pixelSize()));
		return result;
	};
	const auto metric = [&](int value) {
		return float64(scaled(value));
	};
	const auto width = scaled(st::peerQrCardWidth);
	const auto layout = ComputeCardLayout(width, !_username.isEmpty());
	const auto available = width - 2 * scaled(st::peerQrUsernameSkip);
	auto fit = UsernameFit();
	if (_cardGeometry.isEmpty()) {
		fit = FitUsername(
			_username,
			available,
			layout.band.height(),
			{
				scaledFont(st::peerQrUsernameLarge),
				scaledFont(st::peerQrUsernameMedium),
				scaledFont(st::peerQrUsernameSmall),
			},
			scaled(st::peerQrUsernameMin));
	} else if (!_card->content.username.lines.isEmpty()) {
		fit = _card->content.username;
		// WHY: the image keeps the screen's lines, and Qt advances do not scale
		// exactly, so a converted size that overflows shrinks until it fits.
		for (auto pixels = scaled(fit.font.pixelSize()); ; --pixels) {
			fit.font.setPixelSize(pixels);
			const auto metrics = QFontMetricsF(fit.font);
			const auto fits = LinesFit(
				fit.lines,
				metrics,
				available,
				layout.band.height());
			if (fits || pixels <= 1) {
				break;
			}
		}
	}
	return {
		.appearance = _appearance,
		.colors = _colors,
		.link = _link,
		.usernameFont = std::move(fit.font),
		.usernameLines = std::move(fit.lines),
		.cardWidth = width,
		.margin = (scaled(st::peerQrBoxWidth) - width) / 2,
		.shadowRadius = scaled(st::peerQrCardShadowRadius),
		.shadowShift = scaled(st::peerQrCardShadowShift),
		.emoji = ((_field && !_appearance.emoji.empty())
			? _field->prepareRest({
				.skipX = metric(st::peerQrEmojiSkipX),
				.skipY = metric(st::peerQrEmojiSkipY),
				.size = metric(st::peerQrEmojiSize),
				.offsetMin = metric(st::peerQrEmojiOffsetMin),
				.offsetMax = metric(st::peerQrEmojiOffsetMax),
				.bob = metric(st::peerQrEmojiBob),
			})
			: PeerQrEmojiRest()),
		.gift = (_giftField
			? _giftField->prepareRest({
				.modelSize = metric(st::peerQrGiftModelSize),
				.modelSkipX = metric(st::peerQrGiftModelSkipX),
				.modelSkipY = metric(st::peerQrGiftModelSkipY),
				.modelOffset = metric(st::peerQrGiftModelOffset),
				.symbolSize = metric(st::peerQrGiftSymbolSize),
				.symbolSkipX = metric(st::peerQrGiftSymbolSkipX),
				.symbolSkipY = metric(st::peerQrGiftSymbolSkipY),
				.symbolJitterX = metric(st::peerQrGiftSymbolJitterX),
				.symbolJitterY = metric(st::peerQrGiftSymbolJitterY),
			})
			: PeerQrGiftRest()),
		.layerOffset = _layer.offset()
			* (kImageScale * 100. / style::Scale()),
		.layerBand = metric(st::peerQrLayerBand),
	};
}

void PeerQrSurface::addControl(
		not_null<QWidget*> control,
		Fn<QPainterPath()> shape,
		Fn<float64()> opacity) {
	Expects(control->parentWidget() == this);

	_controls.push_back({
		.widget = base::make_weak(control),
		.shape = std::move(shape),
		.opacity = std::move(opacity),
	});
	update();
}

void PeerQrSurface::paintEvent(QPaintEvent *e) {
	const auto now = crl::now();
	if (backgroundStale()) {
		requestBackground();
	}
	const auto controls = controlShapes(e->rect());
	auto p = QPainter(this);
	if (_wave) {
		paintWave(p, now, controls.shown);
	} else {
		p.drawImage(0, 0, _background);
	}
	if (_cardGeometry.isEmpty()) {
		return;
	}
	if (layerShown()) {
		_layer.advance(now, fieldElapsed(now), st::peerQrDriftSpeed);
	}
	paintLayers(p, now, controls.shown);
	paintFadingUnderlays(p, now, controls);
	const auto still = anim::Disabled();
	const auto elapsed = (_startedAt > 0) ? (now - _startedAt) : 0;
	auto frame = still
		? CardFrame()
		: ComputeCardFrame(elapsed, _card->layout.logo.width() / 2.);
	const auto placed = cardScale();
	p.translate(cardRect().topLeft());
	if (placed != 1.) {
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.scale(placed, placed);
	}
	if (!still) {
		if (_startedAt > 0 && !_blooming) {
			frame.bloom = 1.;
		}
		if (const auto &swap = _card->swap) {
			frame.swap = &*swap;
			frame.swapElapsed = now - swap->startedAt;
		}
		const auto bounce = _bounceStartedAt
			? PeerQrBounceScale(
				float64(now - _bounceStartedAt) / kBounceDuration)
			: 1.;
		const auto scale = bounce * DipScale(_dipStartedAt, now);
		if (scale != 1.) {
			const auto center = QRectF(
				QPointF(),
				QSizeF(_card->layout.size)).center();
			p.translate(center);
			p.scale(scale, scale);
			p.translate(-center);
		}
	}
	PaintCard(p, _card->layout, _card->content, frame, !_countdown);
	if (_countdown) {
		paintCountdown(p);
	}
}

void PeerQrSurface::paintCountdown(QPainter &p) {
	const auto &fit = _card->content.username;
	if (fit.lines.isEmpty()) {
		return;
	}
	const auto &font = _countdown->font;
	const auto scale = float64(fit.font.pixelSize()) / font->f.pixelSize();
	const auto metrics = QFontMetricsF(fit.font);
	const auto height = metrics.height();
	const auto &band = _card->layout.band;
	auto top = band.y() + (band.height() - fit.lines.size() * height) / 2.;
	auto index = 0;
	p.save();
	p.setClipRect(band, Qt::IntersectClip);
	for (const auto &line : fit.lines) {
		const auto left = band.x()
			+ (band.width() - metrics.horizontalAdvance(line)) / 2.;
		for (auto i = 0; i != line.size(); ++i) {
			const auto &glyph = _countdown->glyphs[index++];
			if (anim::Disabled()) {
				glyph->finishAnimating();
			}
			p.save();
			p.translate(
				left + metrics.horizontalAdvance(line.left(i)),
				top + metrics.ascent() - font->ascent * scale);
			p.scale(scale, scale);
			glyph->draw(p, 0, 0, _shownColors.ink);
			p.restore();
		}
		top += height;
	}
	p.restore();
}

bool PeerQrSurface::eventHook(QEvent *e) {
	const auto type = e->type();
	if (type == QEvent::TouchBegin
		|| type == QEvent::TouchUpdate
		|| type == QEvent::TouchEnd
		|| type == QEvent::TouchCancel) {
		const auto touch = static_cast<QTouchEvent*>(e);
		const auto device = touch->device();
		if (device
			&& device->type() == base::TouchDevice::TouchScreen
			&& handleTouch(touch)) {
			touch->accept();
			return true;
		}
	} else if (type == QEvent::WindowDeactivate) {
		stopTouchSwipe();
	}
	return RpWidget::eventHook(e);
}

void PeerQrSurface::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton
		&& layerShown()
		&& !cardRect().contains(QPointF(e->pos()))
		&& !_touchId) {
		_layer.press(e->pos(), crl::now());
	} else {
		e->ignore();
	}
}

void PeerQrSurface::mouseMoveEvent(QMouseEvent *e) {
	if (_layer.pressed() && !_touchId) {
		_layer.move(e->pos(), crl::now());
		update();
	} else {
		e->ignore();
	}
}

void PeerQrSurface::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && _layer.pressed() && !_touchId) {
		_layer.release(crl::now(), !anim::Disabled());
		checkAnimation();
		update();
	} else {
		e->ignore();
	}
}

bool PeerQrSurface::handleTouch(not_null<QTouchEvent*> e) {
	const auto &points = e->touchPoints();
	switch (e->type()) {
	case QEvent::TouchBegin: {
		stopTouchSwipe();
		_touchId = std::nullopt;
		if (points.isEmpty() || !layerShown() || _layer.pressed()) {
			return false;
		}
		const auto &point = points.front();
		const auto position = point.pos();
		if (cardRect().contains(position) || childAt(position.toPoint())) {
			return false;
		}
		_touchId = point.id();
		_layer.press(position, crl::now());
		_touchPresses.fire_copy(e);
		return true;
	}
	case QEvent::TouchUpdate:
	case QEvent::TouchEnd: {
		if (!_touchId) {
			return false;
		}
		const auto i = ranges::find(
			points,
			*_touchId,
			&QTouchEvent::TouchPoint::id);
		if (i != points.end() && _layer.pressed()) {
			const auto state = int(i->state());
			if (state == int(Qt::TouchPointMoved)) {
				_layer.move(i->pos(), crl::now());
				update();
			} else if (state == int(Qt::TouchPointReleased)) {
				_layer.release(crl::now(), !anim::Disabled());
				checkAnimation();
				update();
			}
		}
		if (e->type() == QEvent::TouchEnd) {
			stopTouchSwipe();
			_touchId = std::nullopt;
		}
		// Qt synthesizes mouse events from any touch event left unaccepted.
		return true;
	}
	case QEvent::TouchCancel:
		if (!_touchId) {
			return false;
		}
		stopTouchSwipe();
		_touchId = std::nullopt;
		return true;
	default:
		return false;
	}
}

void PeerQrSurface::stopTouchSwipe() {
	if (_touchId && _layer.pressed()) {
		_layer.release(crl::now(), false);
		update();
	}
}

void PeerQrSurface::composeNow() {
	++_backgroundGeneration;
	applyBackground(prepareBackground()());
}

void PeerQrSurface::checkAnimation() {
	if (_startedAt > 0
		&& !_animation.animating()
		&& !(anim::Disabled()
			&& PowerSaving::On(PowerSaving::kChatBackground))) {
		_animation.start();
	}
}

bool PeerQrSurface::animationCallback(crl::time now) {
	const auto still = anim::Disabled();
	const auto frozen = PowerSaving::On(PowerSaving::kChatBackground);
	if (_dipStartedAt && (still || now >= _dipStartedAt + kDipDuration)) {
		_dipStartedAt = 0;
	}
	const auto whole = (_field && (!frozen || _field->animating(now)))
		|| (_giftField && (!frozen || _giftField->animating(now)))
		|| (layerShown() && _layer.animating());
	const auto refresh = [&](QRect region) {
		if (whole) {
			updateField();
		} else {
			update(region);
		}
	};
	if (_wave) {
		updateWave(now);
		update();
	} else if (still && !whole) {
		return false;
	} else if (!still && _blooming) {
		_blooming = (now - _startedAt < kBloomDuration);
		refresh(cardRect().toAlignedRect());
	} else if (!still && (_card->swap || _bounceStartedAt || _dipStartedAt)) {
		if (_card->swap && now >= _card->swap->startedAt + kSwapDuration) {
			_card->swap = std::nullopt;
		}
		if (_bounceStartedAt && now >= _bounceStartedAt + kBounceDuration) {
			_bounceStartedAt = 0;
		}
		refresh(cardUpdateRect());
	} else {
		refresh(logoRect());
	}
	return true;
}

QPointF PeerQrSurface::gradientCenter() const {
	return _cardGeometry.isEmpty()
		? QRectF(rect()).center()
		: cardRect().center();
}

bool PeerQrSurface::backgroundStale() const {
	return !_backgroundComposing
		&& _appearance.center.isValid()
		&& (gradientCenter() != _backgroundCenter);
}

Fn<QImage()> PeerQrSurface::prepareBackground() {
	const auto ratio = style::DevicePixelRatio();
	auto request = BackgroundRequest{
		.corners = _corners,
		.size = size() * ratio,
	};
	_backgroundCenter = gradientCenter();
	if (_appearance.center.isValid()) {
		const auto cardWidth = _cardGeometry.isEmpty()
			? st::peerQrCardWidth
			: _cardGeometry.width();
		request.center = _appearance.center;
		request.edge = _appearance.edge;
		request.gradientCenter = _backgroundCenter * ratio;
		request.gradientRadius = kGradientRadiusToCard * cardWidth * ratio;
	}
	return [request = std::move(request)] {
		return ComposeBackground(request);
	};
}

void PeerQrSurface::requestBackground() {
	_backgroundComposing = true;
	const auto generation = _backgroundGeneration;
	const auto weak = base::make_weak(this);
	crl::async([=, compose = prepareBackground()] {
		auto image = compose();
		crl::on_main(weak, [=, image = std::move(image)]() mutable {
			_backgroundComposing = false;
			if (_backgroundGeneration == generation) {
				applyBackground(std::move(image));
			} else {
				update();
			}
		});
	});
}

void PeerQrSurface::applyBackground(QImage image) {
	const auto ratio = style::DevicePixelRatio();
	if (image.size() == size() * ratio) {
		_background = std::move(image);
		_background.setDevicePixelRatio(ratio);
	}
	update();
}

void PeerQrSurface::setCovered(Fn<QRegion()> covered) {
	_covered = std::move(covered);
	update();
}

QRegion PeerQrSurface::covered() const {
	return _covered ? _covered() : QRegion();
}

void PeerQrSurface::updateField() {
	if (_wave) {
		update();
		return;
	}
	const auto region = covered();
	if (region.isEmpty()) {
		update();
	} else {
		update(QRegion(rect()) - region);
	}
}

QRectF PeerQrSurface::cardRect() const {
	return _cardPlacement
		? QRectF(
			_cardPlacement->topLeft,
			QSizeF(_cardGeometry.size()) * _cardPlacement->scale)
		: QRectF(_cardGeometry);
}

float64 PeerQrSurface::cardScale() const {
	return _cardPlacement ? _cardPlacement->scale : 1.;
}

QRect PeerQrSurface::logoRect() const {
	if (_cardGeometry.isEmpty()) {
		return rect();
	}
	const auto scale = cardScale();
	const auto logo = QRectF(_card->layout.logo);
	return QRectF(
		cardRect().topLeft() + logo.topLeft() * scale,
		logo.size() * scale).toAlignedRect();
}

QRect PeerQrSurface::cardUpdateRect() const {
	if (_cardGeometry.isEmpty()) {
		return rect();
	}
	const auto shadow = st::peerQrCardShadowRadius
		+ st::peerQrCardShadowShift;
	const auto half = std::max(
		_cardGeometry.width(),
		_cardGeometry.height()) / 2.;
	const auto grow = (shadow + (half + shadow) * (kBouncePeak - 1.))
		* cardScale();
	return cardRect().marginsAdded(
		{ grow, grow, grow, grow }).toAlignedRect();
}

QRect PeerQrSurface::labelUpdateRect() const {
	if (_cardGeometry.isEmpty()) {
		return rect();
	}
	const auto now = crl::now();
	const auto bounce = (!anim::Disabled() && _bounceStartedAt)
		? PeerQrBounceScale(float64(now - _bounceStartedAt) / kBounceDuration)
		: 1.;
	const auto scale = anim::Disabled()
		? 1.
		: bounce * DipScale(_dipStartedAt, now);
	const auto center = QRectF(QPointF(), QSizeF(_card->layout.size)).center();
	auto transform = QTransform();
	transform.translate(cardRect().x(), cardRect().y());
	transform.scale(cardScale(), cardScale());
	transform.translate(center.x(), center.y());
	transform.scale(scale, scale);
	transform.translate(-center.x(), -center.y());
	return transform.mapRect(QRectF(_card->layout.band)).toAlignedRect();
}

void PeerQrSurface::refreshCard() {
	if (_cardGeometry.isEmpty()) {
		*_card = Card();
		update();
		return;
	}
	const auto width = _cardGeometry.width();
	const auto layout = ComputeCardLayout(width, !_username.isEmpty());
	auto content = PrepareCardContent(
		layout,
		_link,
		_shownColors.ink,
		st::peerQrCardShadowRadius,
		st::peerQrCardShadowShift,
		style::DevicePixelRatio());
	*_card = Card{ layout, std::move(content) };
	refreshUsername();
	update();
}

void PeerQrSurface::refreshUsername(anim::type animated) {
	if (_cardGeometry.isEmpty()) {
		return;
	}
	const auto &previous = _card->content.username;
	auto fit = FitUsername(
		_username,
		_cardGeometry.width() - 2 * st::peerQrUsernameSkip,
		_card->layout.band.height(),
		{
			st::peerQrUsernameLarge->f,
			st::peerQrUsernameMedium->f,
			st::peerQrUsernameSmall->f,
		},
		st::peerQrUsernameMin);
	const auto animate = (animated == anim::type::normal)
		&& !anim::Disabled()
		&& (fit.font == previous.font)
		&& (fit.lines.size() == previous.lines.size());
	_card->content.username = std::move(fit);
	if (!_countdown) {
		return;
	}
	const auto &fitted = _card->content.username;
	_countdown->font = st::peerQrUsernameSmall;
	for (const auto &font : {
		st::peerQrUsernameLarge,
		st::peerQrUsernameMedium,
		st::peerQrUsernameSmall,
	}) {
		if (font->f == fitted.font) {
			_countdown->font = font;
			break;
		}
	}
	const auto text = _card->content.username.lines.join(QString());
	auto &glyphs = _countdown->glyphs;
	const auto sameSize = (text.size() == glyphs.size());
	glyphs.resize(text.size());
	for (auto i = 0; i != text.size(); ++i) {
		if (!glyphs[i]) {
			glyphs[i] = std::make_unique<AnimatedString>(
				_countdown->font,
				[=] { update(labelUpdateRect()); },
				AnimatedString::Options{
					.preserveIndex = true,
					.enforceByLetter = true,
				});
		}
		glyphs[i]->setText(text.mid(i, 1), animate && sameSize);
	}
}

void PeerQrSurface::refreshQr() {
	if (_cardGeometry.isEmpty()) {
		refreshCard();
		return;
	}
	auto &content = _card->content;
	content.data = _link.isEmpty() ? Qr::Data() : Qr::Encode(_link);
	content.qr = PrepareQrImage(
		content.data,
		_card->layout.qr.width(),
		_shownColors.ink,
		style::DevicePixelRatio());
	_card->qrMask = QImage();
	update(cardUpdateRect());
}

void PeerQrSurface::recolorQr() {
	auto &content = _card->content;
	content.ink = _shownColors.ink;
	update(cardUpdateRect());
	if (content.data.size <= 0) {
		return;
	}
	const auto side = _card->layout.qr.width();
	const auto ratio = style::DevicePixelRatio();
	if (_shownColors == _colors) {
		_card->qrMask = QImage();
		content.qr = PrepareQrImage(content.data, side, content.ink, ratio);
		return;
	} else if (_card->qrMask.isNull()) {
		_card->qrMask = PrepareQrImage(
			content.data,
			side,
			QColor(255, 255, 255),
			ratio);
	}
	content.qr = style::colorizeImage(_card->qrMask, content.ink);
	content.qr.setDevicePixelRatio(ratio);
}

void PeerQrSurface::setShownColors(const PeerQrColors &colors) {
	if (_shownColors == colors) {
		return;
	}
	_shownColors = colors;
	recolorQr();
	_colorsChanges.fire_copy(_shownColors);
}

void PeerQrSurface::ensureField() {
	if (_field) {
		return;
	}
	_field = std::make_unique<PeerQrEmojiField>(
		_emojiSource,
		crl::guard(this, [=] { updateField(); }));
	_field->setTextColor(_colors.foreground);
	checkAnimation();
}

void PeerQrSurface::refreshFieldClock() {
	if (!_startedAt) {
		return;
	}
	const auto now = crl::now();
	const auto frozen = PowerSaving::On(PowerSaving::kChatBackground);
	if (frozen && !_fieldFrozenAt) {
		_fieldFrozenAt = now;
	} else if (!frozen && _fieldFrozenAt) {
		_fieldShift += now - base::take(_fieldFrozenAt);
	}
}

crl::time PeerQrSurface::fieldElapsed(crl::time now) const {
	if (!_startedAt) {
		return 0;
	}
	const auto till = _fieldFrozenAt ? _fieldFrozenAt : now;
	return std::max(till - _startedAt - _fieldShift, crl::time());
}

PeerQrLayerFalloff PeerQrSurface::layerFalloff() const {
	return {
		.area = QRectF(rect()),
		.band = float64(st::peerQrLayerBand),
	};
}

bool PeerQrSurface::layerShown() const {
	return (_field && !_fieldLeaving) || (_giftField != nullptr);
}

PeerQrSurface::ControlShapes PeerQrSurface::controlShapes(QRect clip) const {
	auto result = ControlShapes();
	auto shown = std::vector<QPainterPath>();
	for (const auto &control : _controls) {
		const auto widget = control.widget.get();
		if (!widget || widget->isHidden()) {
			continue;
		}
		const auto opacity = control.opacity ? control.opacity() : 1.;
		if (opacity <= 0.) {
			continue;
		}
		auto shape = control.shape();
		if (shape.isEmpty()) {
			continue;
		}
		shape.translate(widget->pos());
		if (!shape.boundingRect().intersects(QRectF(clip))) {
			continue;
		}
		if (!control.opacity) {
			result.shown.addPath(shape);
			shown.push_back(std::move(shape));
			continue;
		}
		const auto part = shape.boundingRect().toAlignedRect();
		const auto i = ranges::find(
			result.fading,
			opacity,
			&ControlShapes::Fading::opacity);
		if (i != end(result.fading)) {
			i->path.addPath(shape);
			i->parts.push_back(part);
		} else {
			result.fading.push_back({
				.path = std::move(shape),
				.parts = { part },
				.opacity = opacity,
			});
		}
	}
	for (auto &fading : result.fading) {
		for (const auto &shape : shown) {
			const auto bounds = shape.boundingRect().toAlignedRect();
			const auto meets = [&](QRect part) {
				return part.intersects(bounds);
			};
			if (ranges::any_of(fading.parts, meets)) {
				fading.shown.addPath(shape);
			}
		}
	}
	return result;
}

QPainterPath PeerQrSurface::fieldClip(crl::time now, bool leaving) const {
	auto result = QPainterPath();
	result.addRoundedRect(QRectF(rect()), st::boxRadius, st::boxRadius);
	if (leaving && _wave) {
		const auto progress = std::clamp(
			float64(now - _wave->startedAt) / kWaveDuration,
			0.,
			1.);
		const auto radius = WaveRadius(_wave->farthest, progress);
		auto circle = QPainterPath();
		circle.addEllipse(_wave->center, radius, radius);
		result = result.subtracted(circle);
	}
	return result;
}

PeerQrEmojiPaint PeerQrSurface::fieldArgs(crl::time now) const {
	return {
		.falloff = layerFalloff(),
		.origin = QRectF(_cardGeometry).center() + _layer.offset(),
		.card = cardRect(),
		.now = now,
		.elapsed = fieldElapsed(now),
		.paused = PowerSaving::On(PowerSaving::kEmojiReactions),
	};
}

void PeerQrSurface::clipField(
		QPainter &p,
		crl::time now,
		bool leaving,
		const QPainterPath &shown) const {
	auto clip = fieldClip(now, leaving);
	if (!shown.isEmpty()) {
		const auto radius = float64(st::boxRadius);
		const auto inner = QRectF(rect()).marginsRemoved(
			{ radius, radius, radius, radius });
		// WHY: the shapes become even-odd holes in the field's own clip, which
		// needs them off its rounded corners; a leaving layer's clip has the
		// wave circle cut out, where such a hole would turn back into field.
		if ((leaving && _wave) || !inner.contains(shown.boundingRect())) {
			ClipOutside(p, QRectF(rect()), shown);
		} else {
			clip.addPath(shown);
		}
	}
	p.setClipPath(clip, Qt::IntersectClip);
}

void PeerQrSurface::paintField(
		QPainter &p,
		crl::time now,
		const QPainterPath &shown) {
	auto hq = PainterHighQualityEnabler(p);
	p.save();
	clipField(p, now, _fieldLeaving, shown);
	_field->paint(p, fieldArgs(now));
	p.restore();
}

void PeerQrSurface::paintGift(
		QPainter &p,
		not_null<PeerQrGiftField*> field,
		crl::time now,
		bool leaving,
		const QPainterPath &shown) {
	auto args = fieldArgs(now);
	args.paused = PowerSaving::On(PowerSaving::kStickersChat);
	auto hq = PainterHighQualityEnabler(p);
	p.save();
	clipField(p, now, leaving, shown);
	field->paint(p, args);
	p.restore();
}

void PeerQrSurface::paintLayers(
		QPainter &p,
		crl::time now,
		const QPainterPath &shown) {
	if (_field) {
		paintField(p, now, shown);
	}
	if (_giftLeaving) {
		paintGift(p, _giftLeaving.get(), now, true, shown);
	}
	if (_giftField) {
		paintGift(p, _giftField.get(), now, false, shown);
	}
}

void PeerQrSurface::paintFadingUnderlays(
		QPainter &p,
		crl::time now,
		const ControlShapes &controls) const {
	const auto behind = _field
		|| _giftLeaving
		|| _giftField
		|| (_wave && !_wave->clean.isNull());
	if (controls.fading.empty() || !behind) {
		return;
	}
	const auto tint = _shownColors.control.alphaF();
	auto hq = PainterHighQualityEnabler(p);
	for (const auto &fading : controls.fading) {
		const auto area = fading.path.boundingRect().toAlignedRect();
		// WHY: the control paints its tint at opacity * tint alpha after this,
		// so the clean backdrop goes once over the finished picture at the alpha
		// that makes the backdrop and the tint fade together as one layer.
		const auto alpha = fading.opacity * (1. - tint)
			/ (1. - fading.opacity * tint);
		p.save();
		p.setClipPath(fading.path, Qt::IntersectClip);
		ClipOutside(p, QRectF(area), fading.shown);
		p.setOpacity(alpha);
		paintClean(p, now, area, fading.parts);
		p.restore();
	}
}

void PeerQrSurface::startFieldEntry(crl::time now) {
	if (_field && !_fieldLeaving) {
		_field->startEntry(now);
	}
	if (_giftField) {
		_giftField->startEntry(now);
	}
}

void PeerQrSurface::updateWave(crl::time now) {
	const auto progress = anim::Disabled()
		? 1.
		: std::clamp(
			float64(now - _wave->startedAt) / kWaveDuration,
			0.,
			1.);
	const auto radius = WaveRadius(_wave->farthest, progress);
	if (_entryPending && radius >= _wave->farthest) {
		_entryPending = false;
		startFieldEntry(now);
	}
	if (progress >= 1.) {
		_wave = std::nullopt;
		if (base::take(_fieldLeaving)) {
			_field = nullptr;
		}
		_giftLeaving = nullptr;
		setShownColors(_colors);
		return;
	}
	const auto blend = anim::easeOutCubic(
		1.,
		std::clamp(radius / st::peerQrWaveBlend, 0., 1.));
	setShownColors((blend >= 1.)
		? _colors
		: MixColors(_wave->colorsFrom, _colors, blend));
}

void PeerQrSurface::paintWave(
		QPainter &p,
		crl::time now,
		const QPainterPath &clean) const {
	const auto &wave = *_wave;
	const auto progress = std::clamp(
		float64(now - wave.startedAt) / kWaveDuration,
		0.,
		1.);
	const auto radius = WaveRadius(wave.farthest, progress);
	if (radius < wave.farthest) {
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(0, 0, wave.from);
		if (!wave.clean.isNull() && !clean.isEmpty()) {
			p.save();
			p.setClipPath(clean, Qt::IntersectClip);
			p.drawImage(0, 0, wave.clean);
			p.restore();
		}
		auto circle = QPainterPath();
		circle.addEllipse(wave.center, radius, radius);
		p.save();
		p.setClipPath(circle);
		p.drawImage(0, 0, _background);
		p.restore();
	} else {
		p.drawImage(0, 0, _background);
	}
	PaintWaveGlow(p, rect(), rect(), wave.center, radius, progress, wave.tint);
}

void PeerQrSurface::paintClean(
		QPainter &p,
		crl::time now,
		QRect area,
		const std::vector<QRect> &parts) const {
	if (!_wave) {
		p.drawImage(0, 0, _background);
		return;
	}
	const auto &wave = *_wave;
	const auto progress = std::clamp(
		float64(now - wave.startedAt) / kWaveDuration,
		0.,
		1.);
	const auto radius = WaveRadius(wave.farthest, progress);
	const auto &old = wave.clean.isNull() ? wave.from : wave.clean;
	const auto side = (radius < wave.farthest)
		? CircleSide(wave.center, radius, parts)
		: WaveSide::Inside;
	if (side == WaveSide::Inside) {
		p.drawImage(0, 0, _background);
	} else if (side == WaveSide::Outside) {
		p.drawImage(0, 0, old);
	} else {
		auto circle = QPainterPath();
		circle.addEllipse(wave.center, radius, radius);
		p.save();
		// each pixel gets exactly one of the two pictures
		p.setRenderHint(QPainter::Antialiasing, false);
		p.save();
		ClipOutside(p, QRectF(area), circle);
		p.drawImage(0, 0, old);
		p.restore();
		p.setClipPath(circle, Qt::IntersectClip);
		p.drawImage(0, 0, _background);
		p.restore();
	}
	PaintWaveGlow(p, rect(), area, wave.center, radius, progress, wave.tint);
}

QImage PeerQrSurface::currentPicture(crl::time now, bool clean) const {
	if (!_wave) {
		return _background;
	}
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto area = QPainterPath();
	if (clean) {
		area.addRect(QRectF(rect()));
	}
	auto p = QPainter(&result);
	paintWave(p, now, area);
	p.end();
	return result;
}

} // namespace Ui
