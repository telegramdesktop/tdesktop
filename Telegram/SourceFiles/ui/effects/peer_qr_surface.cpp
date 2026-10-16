/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/peer_qr_surface.h"

#include "base/weak_qptr.h"
#include "qr/qr_generate.h"
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
constexpr auto kGradientRadiusRatio = 0.62;
constexpr auto kGradientMiddleStop = 0.55;
constexpr auto kGradientMiddleMix = 0.5;
constexpr auto kSoftLightMix = 0.22;
constexpr auto kSoftLightAlpha = 0.35;
constexpr auto kAccentLightHueShift = 28.;
constexpr auto kAccentLightAlpha = 0.25;
constexpr auto kLightReach = 0.5;
constexpr auto kLightOrbit = 0.22;
constexpr auto kLightsAngle = 135.;
constexpr auto kLightsSpeed = 10.;
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
constexpr auto kPlaneMinScale = 0.61;
constexpr auto kPlaneRise = 0.12;
constexpr auto kPlaneTurn = 15.;
constexpr auto kFlyInOffset = 0.55;
constexpr auto kFlyInScale = 0.5;
constexpr auto kStreakAlpha = 0.35;
constexpr auto kStreakLength = 0.45;
constexpr auto kStreakWidth = 0.05;
constexpr auto kStreakLanes = std::array{ -0.45, 0.2, 0.57 };
constexpr auto kGradientStepDuration = crl::time(4500);
constexpr auto kGradientPhases = 8;
constexpr auto kGradientPhaseAngle = 45;
constexpr auto kBackgroundFrameDuration = crl::time(40);
constexpr auto kDriftHeading = 225.;
constexpr auto kWanderPeriod = crl::time(17000);
constexpr auto kWanderAngle = 30.;
constexpr auto kPatternFadeDuration = crl::time(200);
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
	std::vector<QColor> colors;
	QImage pattern;
	std::array<QImage, 4> corners;
	QSize size;
	QPointF offset;
	float64 patternOpacity = 0.;
	float64 progress = 1.;
	int rotation = 0;
	QColor center;
	QColor edge;
	QPointF gradientCenter;
	float64 gradientRadius = 0.;
	float64 lightsAngle = 0.;
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

[[nodiscard]] QColor MeanLab(const std::vector<QColor> &colors) {
	Expects(!colors.empty());

	auto sum = Lab();
	for (const auto &color : colors) {
		const auto lab = ToLab(color);
		sum.lightness += lab.lightness;
		sum.a += lab.a;
		sum.b += lab.b;
	}
	const auto count = float64(colors.size());
	const auto mean = Lab{
		.lightness = sum.lightness / count,
		.a = sum.a / count,
		.b = sum.b / count,
	};
	return FromLch(mean.lightness, Chroma(mean), Hue(mean));
}

[[nodiscard]] PeerQrColors MixColors(
		const PeerQrColors &a,
		const PeerQrColors &b,
		float64 ratio) {
	return {
		.ink = MixLab(a.ink, b.ink, ratio),
		.foreground = MixLab(a.foreground, b.foreground, ratio),
		.control = MixLab(a.control, b.control, ratio),
	};
}

[[nodiscard]] QColor ComputeWaveTint(const PeerQrAppearance &appearance) {
	const auto lightness = [](const QColor &color) {
		return ToLab(color).lightness;
	};
	const auto center = appearance.center.isValid()
		? appearance.center
		: appearance.colors.empty()
		? appearance.edge
		: *ranges::max_element(appearance.colors, ranges::less(), lightness);
	return MixLab(center, QColor(255, 255, 255), kWaveTintMix);
}

[[nodiscard]] PeerQrColors ComputePeerQrColors(
		const PeerQrAppearance &appearance) {
	const auto white = QColor(255, 255, 255);
	const auto edge = ToLab(appearance.edge);
	const auto hue = Hue(edge);
	const auto chroma = std::min(
		Chroma(edge) * kInkChromaScale + kInkChromaAdd,
		kInkChromaMax);
	const auto colored = appearance.center.isValid();
	const auto reference = colored
		? MixLab(appearance.center, appearance.edge, kGradientMiddleMix)
		: appearance.colors.empty()
		? appearance.edge
		: MeanLab(appearance.colors);
	const auto stops = colored
		? std::vector{ appearance.center, reference, appearance.edge }
		: appearance.colors;
	const auto darkness = [&](const QColor &color) {
		return CountContrast(color, white);
	};
	const auto darkest = stops.empty()
		? appearance.edge
		: *ranges::max_element(stops, ranges::less(), darkness);
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
	auto control = white;
	control.setAlphaF(whiteGlyphs
		? kWhiteGlyphGlassAlpha
		: kDarkInkGlassAlpha);
	return {
		.ink = SolveLightness(hue, chroma, white, kInkContrast),
		.foreground = whiteGlyphs ? white : dark,
		.control = control,
	};
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

[[nodiscard]] UsernameFit FitUsername(
		const QString &text,
		int available,
		int bandHeight,
		const std::array<QFont, 3> &steps,
		int floorPixelSize) {
	if (text.isEmpty()) {
		return {};
	}
	for (const auto &font : steps) {
		const auto metrics = QFontMetricsF(font);
		if (metrics.horizontalAdvance(text) <= available
			&& metrics.height() <= bandHeight) {
			return { .font = font, .lines = { text } };
		}
	}
	auto font = steps.back();
	for (auto size = font.pixelSize(); ; --size) {
		font.setPixelSize(size);
		const auto metrics = QFontMetricsF(font);
		auto lines = SplitInTwoLines(text, metrics);
		const auto fits = (2 * metrics.height() <= bandHeight)
			&& ranges::all_of(lines, [&](const QString &line) {
				return metrics.horizontalAdvance(line) <= available;
			});
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
	auto color = QColor(0, 0, 0);
	color.setAlphaF(kShadowAlpha);
	return style::colorizeImage(
		Images::BlurLargeImage(std::move(mask), blur * ratio),
		color);
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
			ratio),
		.qr = std::move(qr),
		.data = std::move(data),
		.plane = PreparePlane(layout.logo.width(), ratio),
		.ink = ink,
		.shadowPosition = QPoint(-shadowRadius, shadowShift - shadowRadius),
	};
}

[[nodiscard]] QImage ReadPattern(const QString &path, int height) {
	if (path.isEmpty() || height <= 0) {
		return QImage();
	}
	auto result = Images::Read({
		.path = path,
		.maxSize = QSize(height, height),
		.gzipSvg = true,
	}).image;
	return (result.isNull() || result.height() == height)
		? result
		: result.scaledToHeight(height, Qt::SmoothTransformation);
}

void PaintAppearanceGradient(
		QPainter &p,
		const QRect &rect,
		QPointF center,
		float64 radius,
		const QColor &centerColor,
		const QColor &edgeColor) {
	auto gradient = QRadialGradient(center, radius);
	gradient.setStops({
		{ 0., centerColor },
		{
			kGradientMiddleStop,
			MixLab(centerColor, edgeColor, kGradientMiddleMix),
		},
		{ 1., edgeColor },
	});
	p.fillRect(rect, gradient);
}

void PaintAppearanceLights(
		QPainter &p,
		const QRect &rect,
		QPointF center,
		float64 radius,
		const QColor &centerColor,
		float64 angle) {
	const auto radians = angle * M_PI / 180.;
	const auto orbit = QPointF(std::cos(radians), -std::sin(radians))
		* (kLightOrbit * radius);
	const auto reach = kLightReach * radius;
	const auto paintLight = [&](
			QPointF position,
			QColor color,
			float64 alpha) {
		auto transparent = color;
		transparent.setAlpha(0);
		color.setAlphaF(alpha);
		auto gradient = QRadialGradient(position, reach);
		gradient.setStops({ { 0., color }, { 1., transparent } });
		const auto bounds = QRectF(
			position - QPointF(reach, reach),
			QSizeF(2. * reach, 2. * reach));
		p.fillRect(bounds.intersected(QRectF(rect)), gradient);
	};
	const auto lab = ToLab(centerColor);
	paintLight(
		center + orbit,
		MixLab(centerColor, QColor(255, 255, 255), kSoftLightMix),
		kSoftLightAlpha);
	paintLight(
		center - orbit,
		FromLch(
			lab.lightness,
			Chroma(lab),
			Hue(lab) + kAccentLightHueShift),
		kAccentLightAlpha);
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
	PaintAppearanceLights(
		p,
		rect,
		request.gradientCenter,
		request.gradientRadius,
		request.center,
		request.lightsAngle);
	p.end();
	return result;
}

[[nodiscard]] QImage ComposeBackground(const BackgroundRequest &request) {
	const auto colored = request.center.isValid();
	if (request.size.isEmpty() || (!colored && request.colors.empty())) {
		return QImage();
	}
	auto result = colored
		? ComposeColorBackground(request)
		: Images::GenerateGradient(
			request.size,
			request.colors,
			request.rotation,
			float(request.progress));
	if (!colored && !request.pattern.isNull()) {
		auto p = QPainter(&result);
		p.setCompositionMode(QPainter::CompositionMode_SoftLight);
		p.setOpacity(request.patternOpacity);
		const auto width = request.pattern.width();
		const auto height = request.pattern.height();
		const auto wrap = [](float64 value, int period) {
			const auto rest = std::fmod(value, float64(period));
			return (rest < 0.) ? (rest + period) : rest;
		};
		const auto left = -wrap(request.offset.x(), width);
		const auto top = -wrap(request.offset.y(), height);
		for (auto y = top; y < request.size.height(); y += height) {
			for (auto x = left; x < request.size.width(); x += width) {
				p.drawImage(QPointF(x, y), request.pattern);
			}
		}
	}
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

void PaintWaveGlow(
		QPainter &p,
		const QRect &rect,
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
	auto rounded = QPainterPath();
	rounded.addRoundedRect(QRectF(rect), st::boxRadius, st::boxRadius);
	auto hq = PainterHighQualityEnabler(p);
	p.save();
	p.setClipPath(rounded);
	p.fillRect(bounds.intersected(QRectF(rect)), gradient);
	p.restore();
}

[[nodiscard]] PlaneState ComputePlaneState(
		crl::time elapsed,
		float64 radius) {
	if (elapsed < kFlyInDuration) {
		const auto progress = float64(elapsed) / kFlyInDuration;
		const auto eased = anim::easeOutCubic(1., progress);
		const auto offset = kFlyInOffset * radius * (1. - eased) / M_SQRT2;
		return {
			.shift = QPointF(-offset, offset),
			.scale = kFlyInScale + (1. - kFlyInScale) * eased,
			.opacity = std::min(2. * progress, 1.),
		};
	}
	const auto phase = float64((elapsed - kFlyInDuration) % kLoopPeriod)
		/ kLoopPeriod;
	const auto shrink = (1. - std::cos(2. * M_PI * phase)) / 2.;
	return {
		.shift = QPointF(0., -kPlaneRise * radius * shrink),
		.scale = 1. - (1. - kPlaneMinScale) * shrink,
		.rotation = -kPlaneTurn * shrink,
		.streaks = shrink,
		.travel = phase,
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
	const auto along = QPointF(1., -1.) / M_SQRT2;
	const auto across = QPointF(1., 1.) / M_SQRT2;
	const auto half = kStreakLength * radius / 2.;
	auto color = QColor(255, 255, 255);
	color.setAlphaF(kStreakAlpha * state.streaks);
	p.setPen(QPen(
		color,
		kStreakWidth * radius,
		Qt::SolidLine,
		Qt::RoundCap));
	const auto count = int(kStreakLanes.size());
	for (auto i = 0; i != count; ++i) {
		const auto travel = std::fmod(state.travel + float64(i) / count, 1.);
		const auto middle = center
			+ across * (kStreakLanes[i] * radius)
			+ along * (radius * (1. - 2. * travel));
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
	auto circle = QPainterPath();
	circle.addEllipse(logo);
	p.save();
	p.setClipPath(circle);
	if (state.streaks > 0.) {
		PaintStreaks(p, logo, state);
	}
	p.translate(center + state.shift);
	p.rotate(state.rotation);
	p.scale(state.scale, state.scale);
	p.translate(-center);
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

[[nodiscard]] uint32 DustHash(uint32 value) {
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	value ^= value >> 16;
	return value;
}

[[nodiscard]] float64 DustRandom(uint32 &seed) {
	seed = DustHash(seed + 0x9E3779B9U);
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
		const CardFrame &frame) {
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
	if (!content.username.lines.isEmpty()) {
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
		.colors = appearance.colors,
		.pattern = ReadPattern(appearance.patternPath, args.patternHeight),
		.size = size,
		.patternOpacity = appearance.patternOpacity,
		.center = appearance.center,
		.edge = appearance.edge,
		.gradientCenter = card.center(),
		.gradientRadius = kGradientRadiusRatio
			* std::hypot(size.width(), size.height()),
		.lightsAngle = kLightsAngle,
	});
	if (!result.isNull()) {
		auto p = QPainter(&result);
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
	sizeValue() | rpl::skip(1) | rpl::on_next([=] {
		composeNow();
	}, lifetime());
	PowerSaving::Changes() | rpl::on_next([=] {
		checkAnimation();
		update();
	}, lifetime());
	loadPattern();
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
		&& !_card->content.qr.isNull();
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
	if (_username != username) {
		_username = username;
		refreshCard();
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
	const auto now = crl::now();
	const auto wave = (animated == anim::type::normal)
		&& !anim::Disabled()
		&& (_startedAt > 0)
		&& !_cardGeometry.isEmpty();
	auto from = wave ? currentPicture(now) : QImage();
	_appearance = std::move(appearance);
	_colors = ComputePeerQrColors(_appearance);
	loadPattern();
	composeNow();
	if (!wave) {
		_wave = std::nullopt;
		setShownColors(_colors);
		return;
	}
	const auto center = cardRect().center();
	_wave = Wave{
		.from = std::move(from),
		.colorsFrom = _shownColors,
		.tint = ComputeWaveTint(_appearance),
		.center = center,
		.farthest = FarthestCorner(center, QSizeF(size())),
		.startedAt = now,
	};
	_bounceStartedAt = now;
	checkAnimation();
}

void PeerQrSurface::startAnimations() {
	_startedAt = crl::now();
	_driftTime = 0;
	_blooming = true;
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
	const auto width = scaled(st::peerQrCardWidth);
	const auto layout = ComputeCardLayout(width, !_username.isEmpty());
	auto fit = FitUsername(
		_username,
		width - 2 * scaled(st::peerQrUsernameSkip),
		layout.band.height(),
		{
			scaledFont(st::peerQrUsernameLarge),
			scaledFont(st::peerQrUsernameMedium),
			scaledFont(st::peerQrUsernameSmall),
		},
		scaled(st::peerQrUsernameMin));
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
		.patternHeight = scaled(st::peerQrPatternHeight),
	};
}

void PeerQrSurface::paintEvent(QPaintEvent *e) {
	const auto now = crl::now();
	if (backgroundDue(now)) {
		requestBackground(now);
	}
	auto p = QPainter(this);
	if (_wave) {
		paintWave(p, now);
	} else {
		p.drawImage(0, 0, _background);
	}
	if (_cardGeometry.isEmpty()) {
		return;
	}
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
		const auto scale = _bounceStartedAt
			? PeerQrBounceScale(
				float64(now - _bounceStartedAt) / kBounceDuration)
			: 1.;
		if (scale != 1.) {
			const auto center = QRectF(
				QPointF(),
				QSizeF(_card->layout.size)).center();
			p.translate(center);
			p.scale(scale, scale);
			p.translate(-center);
		}
	}
	PaintCard(p, _card->layout, _card->content, frame);
}

void PeerQrSurface::loadPattern() {
	const auto path = _appearance.patternPath;
	if (path.isEmpty() || path == _patternPath) {
		return;
	}
	_patternPath = path;
	_pattern = QImage();
	_patternShownAt = 0;
	const auto height = st::peerQrPatternHeight * style::DevicePixelRatio();
	const auto weak = base::make_weak(this);
	crl::async([=] {
		auto pattern = ReadPattern(path, height);
		crl::on_main(weak, [=] {
			if (_patternPath != path) {
				return;
			}
			_pattern = pattern;
			if (PowerSaving::On(PowerSaving::kChatBackground)) {
				composeNow();
			}
		});
	});
}

void PeerQrSurface::composeNow() {
	++_backgroundGeneration;
	applyBackground(prepareBackground(crl::now())());
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
	if (_wave) {
		updateWave(now);
		update();
	} else if (still && PowerSaving::On(PowerSaving::kChatBackground)) {
		return false;
	} else if (!still && _blooming) {
		_blooming = (now - _startedAt < kBloomDuration);
		update(cardRect().toAlignedRect());
	} else if (!still && (_card->swap || _bounceStartedAt)) {
		if (_card->swap && now >= _card->swap->startedAt + kSwapDuration) {
			_card->swap = std::nullopt;
		}
		if (_bounceStartedAt && now >= _bounceStartedAt + kBounceDuration) {
			_bounceStartedAt = 0;
		}
		update(cardUpdateRect());
	} else if (!still || backgroundDue(now)) {
		update(logoRect());
	}
	return true;
}

bool PeerQrSurface::backgroundDue(crl::time now) const {
	return (_startedAt > 0)
		&& !_backgroundComposing
		&& !PowerSaving::On(PowerSaving::kChatBackground)
		&& (now - _backgroundRequestedAt >= kBackgroundFrameDuration);
}

Fn<QImage()> PeerQrSurface::prepareBackground(crl::time now) {
	const auto ratio = style::DevicePixelRatio();
	const auto frozen = PowerSaving::On(PowerSaving::kChatBackground);
	const auto live = (_startedAt > 0) && !frozen;
	auto request = BackgroundRequest{
		.colors = _appearance.colors,
		.corners = _corners,
		.size = size() * ratio,
	};
	if (_appearance.center.isValid()) {
		const auto card = _cardGeometry.isEmpty()
			? QRectF(rect())
			: live
			? cardRect()
			: QRectF(_cardGeometry);
		const auto elapsed = live ? (now - _startedAt) : crl::time();
		request.center = _appearance.center;
		request.edge = _appearance.edge;
		request.gradientCenter = card.center() * ratio;
		request.gradientRadius = kGradientRadiusRatio
			* std::hypot(width(), height())
			* ratio;
		request.lightsAngle = kLightsAngle + kLightsSpeed * elapsed / 1000.;
	} else if (!_pattern.isNull() && (live || frozen)) {
		if (live && !_patternShownAt) {
			_patternShownAt = now;
		}
		const auto fade = live
			? (float64(now - _patternShownAt) / kPatternFadeDuration)
			: 1.;
		request.patternOpacity = _appearance.patternOpacity
			* std::clamp(fade, 0., 1.);
		if (request.patternOpacity > 0.) {
			request.pattern = _pattern;
		}
	}
	if (live) {
		const auto elapsed = now - _startedAt;
		if (elapsed > _driftTime) {
			const auto wander = std::sin(
				2. * M_PI * float64(elapsed) / kWanderPeriod);
			const auto heading = (kDriftHeading + kWanderAngle * wander)
				* M_PI
				/ 180.;
			const auto distance = st::peerQrDriftSpeed
				* ratio
				* float64(elapsed - _driftTime)
				/ 1000.;
			_driftOffset -= QPointF(std::cos(heading), std::sin(heading))
				* distance;
			_driftTime = elapsed;
		}
		const auto step = int((elapsed / kGradientStepDuration)
			% kGradientPhases);
		request.offset = _driftOffset;
		request.rotation = kGradientPhaseAngle
			* (kGradientPhases - 1 - step);
		request.progress = float64(elapsed % kGradientStepDuration)
			/ kGradientStepDuration;
	}
	return [request = std::move(request)] {
		return ComposeBackground(request);
	};
}

void PeerQrSurface::requestBackground(crl::time now) {
	_backgroundComposing = true;
	_backgroundRequestedAt = now;
	const auto generation = _backgroundGeneration;
	const auto weak = base::make_weak(this);
	crl::async([=, compose = prepareBackground(now)] {
		auto image = compose();
		crl::on_main(weak, [=, image = std::move(image)]() mutable {
			_backgroundComposing = false;
			if (_backgroundGeneration == generation) {
				applyBackground(std::move(image));
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
	content.username = FitUsername(
		_username,
		width - 2 * st::peerQrUsernameSkip,
		layout.band.height(),
		{
			st::peerQrUsernameLarge->f,
			st::peerQrUsernameMedium->f,
			st::peerQrUsernameSmall->f,
		},
		st::peerQrUsernameMin);
	*_card = Card{ layout, std::move(content) };
	update();
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

void PeerQrSurface::updateWave(crl::time now) {
	const auto progress = anim::Disabled()
		? 1.
		: std::clamp(
			float64(now - _wave->startedAt) / kWaveDuration,
			0.,
			1.);
	if (progress >= 1.) {
		_wave = std::nullopt;
		setShownColors(_colors);
		return;
	}
	const auto radius = WaveRadius(_wave->farthest, progress);
	const auto blend = anim::easeOutCubic(
		1.,
		std::clamp(radius / st::peerQrWaveBlend, 0., 1.));
	setShownColors((blend >= 1.)
		? _colors
		: MixColors(_wave->colorsFrom, _colors, blend));
}

void PeerQrSurface::paintWave(QPainter &p, crl::time now) const {
	const auto &wave = *_wave;
	const auto progress = std::clamp(
		float64(now - wave.startedAt) / kWaveDuration,
		0.,
		1.);
	const auto radius = WaveRadius(wave.farthest, progress);
	if (radius < wave.farthest) {
		p.drawImage(0, 0, wave.from);
		auto circle = QPainterPath();
		circle.addEllipse(wave.center, radius, radius);
		auto hq = PainterHighQualityEnabler(p);
		p.save();
		p.setClipPath(circle);
		p.drawImage(0, 0, _background);
		p.restore();
	} else {
		p.drawImage(0, 0, _background);
	}
	PaintWaveGlow(p, rect(), wave.center, radius, progress, wave.tint);
}

QImage PeerQrSurface::currentPicture(crl::time now) const {
	if (!_wave) {
		return _background;
	}
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	paintWave(p, now);
	p.end();
	return result;
}

} // namespace Ui
