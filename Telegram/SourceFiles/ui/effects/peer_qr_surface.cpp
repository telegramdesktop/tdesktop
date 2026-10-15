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
constexpr auto kControlAlpha = 0.35;
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

[[nodiscard]] PeerQrColors ComputePeerQrColors(
		const PeerQrAppearance &appearance) {
	const auto white = QColor(255, 255, 255);
	const auto edge = ToLab(appearance.edge);
	const auto hue = Hue(edge);
	const auto chroma = std::min(
		Chroma(edge) * kInkChromaScale + kInkChromaAdd,
		kInkChromaMax);
	const auto darkness = [&](const QColor &color) {
		return CountContrast(color, white);
	};
	const auto darkest = appearance.colors.empty()
		? appearance.edge
		: *ranges::max_element(appearance.colors, ranges::less(), darkness);
	auto control = white;
	control.setAlphaF(kControlAlpha);
	return {
		.ink = SolveLightness(hue, chroma, white, kInkContrast),
		.foreground = SolveLightness(
			hue,
			chroma,
			darkest,
			kForegroundContrast),
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
	auto qr = (data.size > 0)
		? GenerateFinalQr(data, layout.qr.width() * ratio, ink)
		: QImage();
	qr.setDevicePixelRatio(ratio);
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

[[nodiscard]] QImage ComposeBackground(const BackgroundRequest &request) {
	if (request.size.isEmpty() || request.colors.empty()) {
		return QImage();
	}
	auto result = Images::GenerateGradient(
		request.size,
		request.colors,
		request.rotation,
		float(request.progress));
	if (!request.pattern.isNull()) {
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
	if (!content.qr.isNull()) {
		if (frame.bloom >= 1.) {
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
};

int PeerQrCardHeight(int cardWidth, bool withUsername) {
	return ComputeCardLayout(cardWidth, withUsername).size.height();
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
	auto result = ComposeBackground({
		.colors = args.appearance.colors,
		.pattern = ReadPattern(
			args.appearance.patternPath,
			args.patternHeight),
		.size = layout.size + margin,
		.patternOpacity = args.appearance.patternOpacity,
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

void PeerQrSurface::setLink(const QString &link) {
	if (_link != link) {
		_link = link;
		refreshCard();
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
		_cardGeometry = card;
		refreshCard();
	}
}

void PeerQrSurface::startAnimations() {
	_startedAt = crl::now();
	_driftTime = 0;
	_blooming = true;
	checkAnimation();
}

const PeerQrColors &PeerQrSurface::colors() const {
	return _colors;
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
	p.drawImage(0, 0, _background);
	if (!_cardGeometry.isEmpty()) {
		const auto elapsed = (_startedAt > 0) ? (now - _startedAt) : 0;
		const auto frame = anim::Disabled()
			? CardFrame()
			: ComputeCardFrame(elapsed, _card->layout.logo.width() / 2.);
		p.translate(_cardGeometry.topLeft());
		PaintCard(p, _card->layout, _card->content, frame);
	}
}

void PeerQrSurface::loadPattern() {
	const auto path = _appearance.patternPath;
	if (path.isEmpty()) {
		return;
	}
	const auto height = st::peerQrPatternHeight * style::DevicePixelRatio();
	const auto weak = base::make_weak(this);
	crl::async([=] {
		auto pattern = ReadPattern(path, height);
		crl::on_main(weak, [=] {
			_pattern = pattern;
			if (PowerSaving::On(PowerSaving::kChatBackground)) {
				composeNow();
			}
		});
	});
}

void PeerQrSurface::composeNow() {
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
	if (still && PowerSaving::On(PowerSaving::kChatBackground)) {
		return false;
	} else if (!still && _blooming) {
		_blooming = (now - _startedAt < kBloomDuration);
		update(_cardGeometry);
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
	if (!_pattern.isNull() && (live || frozen)) {
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
	const auto weak = base::make_weak(this);
	crl::async([=, compose = prepareBackground(now)] {
		auto image = compose();
		crl::on_main(weak, [=, image = std::move(image)]() mutable {
			_backgroundComposing = false;
			applyBackground(std::move(image));
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

QRect PeerQrSurface::logoRect() const {
	return _cardGeometry.isEmpty()
		? rect()
		: _card->layout.logo.translated(_cardGeometry.topLeft());
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
		_colors.ink,
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

} // namespace Ui
