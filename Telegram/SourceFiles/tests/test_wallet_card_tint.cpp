/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_tint.h"

#include <cstdio>
#include <initializer_list>

namespace {

using namespace Wallet;

constexpr auto kExact = 1e-12;
constexpr auto kDecoded = 1e-9;
constexpr auto kKeptLuminance = 0.005;
constexpr auto kKeptRatio = 0.03;
constexpr auto kTextFloor = 3.;
constexpr auto kSurfaceFloor = 1.5;
constexpr auto kPremultiplied = QImage::Format_ARGB32_Premultiplied;

struct Check {
	const char *name = nullptr;
	QString (*run)() = nullptr;
};

struct Tinted {
	QImage frame;
	QImage result;
	QString failure;
};

[[nodiscard]] QString Failures(std::initializer_list<QString> results) {
	auto joined = QString();
	for (const auto &result : results) {
		if (!result.isEmpty()) {
			joined += (joined.isEmpty() ? QString() : u"; "_q) + result;
		}
	}
	return joined;
}

[[nodiscard]] QString Expect(bool condition, const QString &failure) {
	return condition ? QString() : failure;
}

[[nodiscard]] QString Text(const QColor &color) {
	if (!color.isValid()) {
		return u"invalid"_q;
	}
	return u"(%1,%2,%3,%4)"_q
		.arg(color.red())
		.arg(color.green())
		.arg(color.blue())
		.arg(color.alpha());
}

[[nodiscard]] QString PixelText(QRgb pixel) {
	return Text(QColor::fromRgba(pixel));
}

[[nodiscard]] QString MarkText(const CardMarkTint &tint) {
	return u"%1 x %2"_q.arg(Text(tint.hue)).arg(tint.amount, 0, 'g', 17);
}

[[nodiscard]] QString ExpectWithin(
		float64 got,
		float64 from,
		float64 till,
		const QString &what) {
	return (std::isfinite(got) && got >= from && got <= till)
		? QString()
		: u"%1: got %2, expected [%3, %4]"_q
			.arg(what)
			.arg(got, 0, 'g', 17)
			.arg(from, 0, 'g', 17)
			.arg(till, 0, 'g', 17);
}

[[nodiscard]] QString ExpectNear(
		float64 got,
		float64 expected,
		float64 tolerance,
		const QString &what) {
	return ExpectWithin(got, expected - tolerance, expected + tolerance, what);
}

[[nodiscard]] float64 RelativeLuminance(const QColor &color) {
	return CardLuminance(color.rgb());
}

[[nodiscard]] float64 Contrast(const QColor &a, const QColor &b) {
	const auto first = RelativeLuminance(a) + 0.05;
	const auto second = RelativeLuminance(b) + 0.05;
	return std::max(first, second) / std::min(first, second);
}

[[nodiscard]] QString ExpectRatio(
		const QColor &a,
		const QColor &b,
		float64 from,
		float64 till) {
	return ExpectWithin(
		Contrast(a, b),
		from,
		till,
		u"%1 against %2"_q.arg(Text(a), Text(b)));
}

[[nodiscard]] QString ExpectRatioAtLeast(
		const QColor &a,
		const QColor &b,
		float64 least) {
	return ExpectRatio(a, b, least, std::numeric_limits<float64>::infinity());
}

[[nodiscard]] QString ExpectColor(const QColor &got, const QColor &expected) {
	return (got == expected)
		? QString()
		: u"got %1, expected %2"_q.arg(Text(got), Text(expected));
}

[[nodiscard]] QString ExpectTint(
		const CardTint &got,
		const CardTint &expected) {
	return (got == expected)
		? QString()
		: u"got %1 / %2, expected %3 / %4"_q.arg(
			Text(got.light),
			Text(got.dark),
			Text(expected.light),
			Text(expected.dark));
}

[[nodiscard]] QString ExpectDarker(const QColor &darker, const QColor &than) {
	return (RelativeLuminance(darker) < RelativeLuminance(than))
		? QString()
		: u"%1 is not darker than %2"_q.arg(Text(darker), Text(than));
}

[[nodiscard]] std::array<int, 3> ChannelOrder(const QColor &color) {
	const auto order = [](int a, int b) {
		return (a > b) - (a < b);
	};
	return {
		order(color.red(), color.green()),
		order(color.green(), color.blue()),
		order(color.red(), color.blue()),
	};
}

[[nodiscard]] QString ExpectChannelOrder(
		const QColor &got,
		const QColor &like) {
	return (ChannelOrder(got) == ChannelOrder(like))
		? QString()
		: u"%1 lost the channel order of %2"_q.arg(Text(got), Text(like));
}

[[nodiscard]] QString ExpectMark(
		const CardMarkTint &got,
		const QColor &hue,
		float64 amount) {
	return Failures({
		ExpectColor(got.hue, hue),
		ExpectNear(got.amount, amount, kExact, u"amount"_q),
	});
}

[[nodiscard]] QString ExpectSameMark(
		const CardMarkTint &got,
		const CardMarkTint &expected) {
	return (got == expected)
		? QString()
		: u"got %1, expected %2"_q.arg(MarkText(got), MarkText(expected));
}

[[nodiscard]] CardTint BlackAndWhite() {
	return { QColor(Qt::white), QColor(Qt::black) };
}

[[nodiscard]] CardTint MidGrays() {
	return { QColor(192, 192, 192), QColor(64, 64, 64) };
}

[[nodiscard]] CardTint Blues() {
	return { QColor(90, 140, 200), QColor(30, 60, 110) };
}

[[nodiscard]] CardTint Amber() {
	return { QColor(250, 200, 120), QColor(200, 140, 60) };
}

[[nodiscard]] CardTint Navy() {
	return { QColor(30, 40, 60), QColor(10, 12, 20) };
}

[[nodiscard]] std::vector<QColor> NightSurfaces() {
	return { QColor(24, 34, 45), QColor(32, 43, 55) };
}

[[nodiscard]] CardTint Computed(
		const CardTint &pair,
		const std::vector<QColor> &surfaces) {
	return ComputeCardTint(pair.light, pair.dark, surfaces);
}

[[nodiscard]] QString ExpectGray(int value, float64 expected) {
	return ExpectNear(
		CardLuminance(qRgb(value, value, value)),
		expected,
		kDecoded,
		u"gray %1"_q.arg(value));
}

[[nodiscard]] QString ExpectPairRatio(
		const CardTint &tint,
		const CardTint &pair) {
	return ExpectNear(
		Contrast(tint.light, tint.dark),
		Contrast(pair.light, pair.dark),
		kKeptRatio,
		u"pair ratio"_q);
}

[[nodiscard]] QString ExpectWhiteText(const CardTint &tint) {
	const auto white = QColor(Qt::white);
	for (auto i = 0; i != 11; ++i) {
		const auto sample = CardTintAt(tint, i / 10.);
		const auto failure = ExpectRatioAtLeast(white, sample, kTextFloor);
		if (!failure.isEmpty()) {
			return u"level %1: %2"_q.arg(i / 10.).arg(failure);
		}
	}
	return QString();
}

[[nodiscard]] QString ExpectApart(
		const CardTint &tint,
		const std::vector<QColor> &surfaces) {
	for (const auto &surface : surfaces) {
		const auto failure = Failures({
			ExpectRatioAtLeast(tint.light, surface, kSurfaceFloor),
			ExpectRatioAtLeast(tint.dark, surface, kSurfaceFloor),
		});
		if (!failure.isEmpty()) {
			return failure;
		}
	}
	return QString();
}

[[nodiscard]] QString ExpectSurround(const QColor &card, const QColor &ink) {
	const auto surround = CardInkSurround(card, ink);
	return Failures({
		ExpectRatio(surround, ink, 2.75, 2.85),
		ExpectDarker(surround, card),
		ExpectChannelOrder(surround, card),
	});
}

[[nodiscard]] CardMarkTint Red(float64 amount = 1.) {
	return { QColor(Qt::red), amount };
}

[[nodiscard]] CardMarkTint Blue(float64 amount = 1.) {
	return { QColor(Qt::blue), amount };
}

[[nodiscard]] QImage MarkFrame() {
	auto result = QImage(4, 2, kPremultiplied);
	result.fill(Qt::transparent);
	result.setPixel(0, 0, qRgb(255, 255, 255));
	result.setPixel(1, 0, qRgb(128, 128, 128));
	result.setPixel(2, 0, qRgb(0, 0, 0));
	result.setPixel(3, 0, qRgb(60, 120, 230));
	result.setPixel(0, 1, qPremultiply(qRgba(60, 120, 230, 128)));
	result.setPixel(1, 1, qRgb(200, 160, 120));
	result.setPixel(2, 1, qRgb(0, 0, 255));
	return result;
}

[[nodiscard]] QImage StraightFrame() {
	auto result = QImage(1, 1, QImage::Format_ARGB32);
	result.setPixel(0, 0, qRgba(60, 120, 230, 128));
	return result;
}

[[nodiscard]] QImage TintedOnce(
		const QImage &frame,
		const CardMarkTint &tint) {
	return CardMarkTinter().apply(frame, tint);
}

[[nodiscard]] QString Layout(
		QSize size,
		QImage::Format format,
		float64 ratio) {
	return u"%1x%2 format %3 ratio %4"_q
		.arg(size.width())
		.arg(size.height())
		.arg(int(format))
		.arg(ratio, 0, 'g', 17);
}

[[nodiscard]] QString ExpectLayout(
		const QImage &image,
		QSize size,
		QImage::Format format,
		float64 ratio) {
	const auto same = (image.size() == size)
		&& (image.format() == format)
		&& (image.devicePixelRatio() == ratio);
	return same
		? QString()
		: u"got %1, expected %2"_q.arg(
			Layout(image.size(), image.format(), image.devicePixelRatio()),
			Layout(size, format, ratio));
}

[[nodiscard]] Tinted MarkFrameTinted(const CardMarkTint &tint) {
	const auto frame = MarkFrame();
	const auto result = TintedOnce(frame, tint);
	return {
		.frame = frame,
		.result = result,
		.failure = ExpectLayout(result, frame.size(), kPremultiplied, 1.),
	};
}

[[nodiscard]] QString ExpectPixel(
		const QImage &image,
		int x,
		int y,
		QRgb expected) {
	const auto got = image.pixel(x, y);
	return (got == expected)
		? QString()
		: u"pixel %1,%2: got %3, expected %4"_q
			.arg(x)
			.arg(y)
			.arg(PixelText(got), PixelText(expected));
}

[[nodiscard]] QString ExpectKept(const Tinted &tinted, int x, int y) {
	return ExpectPixel(tinted.result, x, y, tinted.frame.pixel(x, y));
}

[[nodiscard]] QString ExpectLuminanceKept(
		const Tinted &tinted,
		int x,
		int y) {
	return ExpectNear(
		CardLuminance(tinted.result.pixel(x, y)),
		CardLuminance(tinted.frame.pixel(x, y)),
		kKeptLuminance,
		u"luminance at %1,%2"_q.arg(x).arg(y));
}

[[nodiscard]] QString ExpectAlpha(QRgb pixel, int expected) {
	return (qAlpha(pixel) == expected)
		? QString()
		: u"alpha %1, expected %2"_q.arg(qAlpha(pixel)).arg(expected);
}

[[nodiscard]] QString ExpectClose(QRgb got, QRgb expected, int tolerance) {
	const auto close = [&](int a, int b) {
		return std::abs(a - b) <= tolerance;
	};
	const auto near = close(qRed(got), qRed(expected))
		&& close(qGreen(got), qGreen(expected))
		&& close(qBlue(got), qBlue(expected));
	return near
		? QString()
		: u"%1 is not within %2 of %3"_q.arg(
			PixelText(got),
			QString::number(tolerance),
			PixelText(expected));
}

[[nodiscard]] QString ExpectBetween(QRgb got, QRgb from, QRgb till) {
	const auto between = [](int value, int a, int b) {
		return (value > std::min(a, b)) && (value < std::max(a, b));
	};
	const auto inside = between(qRed(got), qRed(from), qRed(till))
		&& between(qGreen(got), qGreen(from), qGreen(till))
		&& between(qBlue(got), qBlue(from), qBlue(till));
	return inside
		? QString()
		: u"%1 is not strictly between %2 and %3"_q.arg(
			PixelText(got),
			PixelText(from),
			PixelText(till));
}

[[nodiscard]] QString ExpectBypass(
		CardMarkTinter &tinter,
		const QImage &frame,
		const CardMarkTint &tint) {
	const auto result = tinter.apply(frame, tint);
	const auto same = (result.cacheKey() == frame.cacheKey())
		&& (result == frame)
		&& (result.format() == frame.format())
		&& (result.devicePixelRatio() == frame.devicePixelRatio());
	return same
		? QString()
		: u"%1 did not return the frame itself"_q.arg(MarkText(tint));
}

[[nodiscard]] QString ExpectRetinted(
		const QImage &frame,
		const CardMarkTint &was,
		const CardMarkTint &now) {
	auto tinter = CardMarkTinter();
	const auto first = tinter.apply(frame, was);
	const auto kept = first.copy();
	const auto second = tinter.apply(frame, now);
	const auto change = u"%1 -> %2"_q.arg(MarkText(was), MarkText(now));
	return Failures({
		Expect(
			second == TintedOnce(frame, now),
			change + u": differs from a fresh tinter"_q),
		Expect(second != kept, change + u": kept the earlier pixels"_q),
		Expect(first == kept, change + u": the earlier image changed"_q),
	});
}

[[nodiscard]] QString ExpectReplaced(
		CardMarkTinter &tinter,
		const QImage &frame,
		const QImage &replacement) {
	const auto before = tinter.apply(frame, Red()).copy();
	const auto after = tinter.apply(replacement, Red());
	return Failures({
		ExpectLayout(after, replacement.size(), kPremultiplied, 1.),
		Expect(
			after == TintedOnce(replacement, Red()),
			u"a replaced frame differs from a fresh tinter"_q),
		Expect(after != before, u"a replaced frame kept the earlier pixels"_q),
	});
}

[[nodiscard]] QString ExpectEditedInPlace() {
	auto frame = MarkFrame();
	auto tinter = CardMarkTinter();
	const auto before = tinter.apply(frame, Red()).copy();
	frame.setPixel(3, 0, qRgb(230, 120, 60));
	const auto after = tinter.apply(frame, Red());
	return Failures({
		Expect(
			after == TintedOnce(frame, Red()),
			u"an edited frame differs from a fresh tinter"_q),
		Expect(after != before, u"an edited frame kept the earlier pixels"_q),
	});
}

[[nodiscard]] QString CheckPrimaries() {
	return Failures({
		ExpectNear(CardLuminance(qRgb(255, 0, 0)), 0.2126, kExact, u"red"_q),
		ExpectNear(CardLuminance(qRgb(0, 255, 0)), 0.7152, kExact, u"green"_q),
		ExpectNear(CardLuminance(qRgb(0, 0, 255)), 0.0722, kExact, u"blue"_q),
		ExpectNear(CardLuminance(qRgb(255, 255, 255)), 1., kExact, u"white"_q),
		ExpectNear(CardLuminance(qRgb(0, 0, 0)), 0., kExact, u"black"_q),
	});
}

[[nodiscard]] QString CheckGrayTransfer() {
	return Failures({
		ExpectGray(10, 0.0030352698),
		ExpectGray(64, 0.0512694584),
		ExpectGray(128, 0.2158605001),
		ExpectGray(188, 0.5028864580),
	});
}

[[nodiscard]] QString CheckChannelsAdd() {
	const auto sum = CardLuminance(qRgb(10, 0, 0))
		+ CardLuminance(qRgb(0, 64, 0))
		+ CardLuminance(qRgb(0, 0, 200));
	return Failures({
		ExpectNear(CardLuminance(qRgb(10, 64, 200)), sum, kExact, u"sum"_q),
		ExpectNear(
			CardLuminance(qRgba(255, 0, 0, 0)),
			0.2126,
			kExact,
			u"transparent red"_q),
	});
}

[[nodiscard]] QString CheckTintMidpoint() {
	const auto tint = BlackAndWhite();
	return Failures({
		ExpectColor(CardTintAt(tint, 0.5), QColor(188, 188, 188)),
		ExpectColor(CardTintAt(tint, 0.25), QColor(137, 137, 137)),
	});
}

[[nodiscard]] QString CheckTintChannels() {
	const auto tint = CardTint{ QColor(Qt::blue), QColor(Qt::red) };
	return ExpectColor(CardTintAt(tint, 0.5), QColor(188, 0, 188));
}

[[nodiscard]] QString CheckTintEndpoints() {
	const auto grays = MidGrays();
	const auto blues = Blues();
	return Failures({
		ExpectColor(CardTintAt(grays, 0.), grays.dark),
		ExpectColor(CardTintAt(grays, 1.), grays.light),
		ExpectColor(CardTintAt(blues, 0.), blues.dark),
		ExpectColor(CardTintAt(blues, 1.), blues.light),
	});
}

[[nodiscard]] QString CheckTintOutOfRange() {
	const auto grays = MidGrays();
	return Failures({
		ExpectColor(CardTintAt(grays, -1.), QColor(64, 64, 64)),
		ExpectColor(CardTintAt(grays, 2.), QColor(192, 192, 192)),
	});
}

[[nodiscard]] QString CheckTintMonotone() {
	const auto tint = BlackAndWhite();
	auto previous = RelativeLuminance(CardTintAt(tint, 0.));
	for (auto i = 1; i != 21; ++i) {
		const auto current = RelativeLuminance(CardTintAt(tint, i / 20.));
		if (!(current >= previous)) {
			return u"luminance %1 after %2 at step %3"_q
				.arg(current, 0, 'g', 17)
				.arg(previous, 0, 'g', 17)
				.arg(i);
		}
		previous = current;
	}
	return QString();
}

[[nodiscard]] QString CheckTintOrder() {
	const auto blues = Blues();
	const auto amber = Amber();
	const auto night = NightSurfaces();
	return Failures({
		ExpectTint(
			ComputeCardTint(blues.dark, blues.light, {}),
			ComputeCardTint(blues.light, blues.dark, {})),
		ExpectTint(
			ComputeCardTint(amber.dark, amber.light, night),
			ComputeCardTint(amber.light, amber.dark, night)),
	});
}

[[nodiscard]] QString CheckInsideUnchanged() {
	const auto red = CardTint{ QColor(Qt::red), QColor(Qt::black) };
	return Failures({
		ExpectTint(Computed(Blues(), {}), Blues()),
		ExpectTint(Computed(red, {}), red),
	});
}

[[nodiscard]] QString CheckWhiteText() {
	const auto yellow = CardTint{ QColor(255, 255, 0), QColor(0, 255, 255) };
	const auto pale = CardTint{ QColor(200, 200, 200), QColor(160, 160, 160) };
	const auto surfaces = std::vector<QColor>{
		QColor(38, 38, 38),
		QColor(95, 95, 95),
	};
	const auto amber = Computed(Amber(), {});
	return Failures({
		ExpectWhiteText(Computed(BlackAndWhite(), {})),
		ExpectWhiteText(amber),
		ExpectWhiteText(Computed(yellow, {})),
		ExpectWhiteText(Computed(pale, {})),
		ExpectWhiteText(Computed(BlackAndWhite(), NightSurfaces())),
		ExpectWhiteText(Computed(Navy(), surfaces)),
		ExpectRatio(QColor(Qt::white), amber.light, 3.05, 3.15),
	});
}

[[nodiscard]] QString CheckWidePair() {
	return ExpectTint(
		Computed(BlackAndWhite(), {}),
		{ QColor(146, 146, 146), QColor(Qt::black) });
}

[[nodiscard]] QString CheckShiftKeepsRatio() {
	const auto pair = Amber();
	const auto tint = Computed(pair, {});
	return Failures({
		ExpectDarker(tint.light, pair.light),
		ExpectPairRatio(tint, pair),
		ExpectChannelOrder(tint.light, pair.light),
		ExpectChannelOrder(tint.dark, pair.dark),
	});
}

[[nodiscard]] QString CheckDarkSurfaces() {
	const auto pair = Navy();
	const auto night = NightSurfaces();
	const auto tint = Computed(pair, night);
	return Failures({
		ExpectTint(Computed(pair, {}), pair),
		ExpectApart(tint, night),
		ExpectPairRatio(tint, pair),
		ExpectRatioAtLeast(QColor(Qt::white), tint.light, kTextFloor),
	});
}

[[nodiscard]] QString CheckSplitInterval() {
	const auto middle = QColor(89, 89, 89);
	const auto surfaces = std::vector<QColor>{ middle };
	const auto below = Computed(
		{ QColor(70, 70, 90), QColor(50, 50, 66) },
		surfaces);
	const auto above = Computed(
		{ QColor(120, 120, 150), QColor(100, 100, 128) },
		surfaces);
	return Failures({
		ExpectApart(below, surfaces),
		ExpectApart(above, surfaces),
		ExpectDarker(below.light, middle),
		ExpectDarker(middle, above.dark),
	});
}

[[nodiscard]] QString CheckDaySurfaces() {
	const auto day = std::vector<QColor>{
		QColor(Qt::white),
		QColor(240, 240, 240),
	};
	return ExpectTint(Computed(Amber(), day), Computed(Amber(), {}));
}

[[nodiscard]] QString CheckInfeasibleSurfaces() {
	const auto pair = Navy();
	const auto blocking = QColor(38, 38, 38);
	if (Computed(pair, { blocking }) == pair) {
		return u"control: one blocking surface left the pair in place"_q;
	}
	const auto surfaces = std::vector<QColor>{
		blocking,
		QColor(95, 95, 95),
		QColor(150, 150, 150),
	};
	return ExpectTint(Computed(pair, surfaces), pair);
}

[[nodiscard]] QString CheckDarkerStrip() {
	const auto gray = QColor(128, 128, 128);
	const auto blue = QColor(30, 60, 110);
	const auto grayInk = CardStripInk({ QColor(Qt::white), gray });
	const auto blueInk = CardStripInk({ QColor(Qt::white), blue });
	return Failures({
		ExpectDarker(grayInk, gray),
		ExpectRatio(grayInk, gray, 1.57, 1.63),
		ExpectDarker(blueInk, blue),
		ExpectRatio(blueInk, blue, 1.57, 1.63),
		ExpectChannelOrder(blueInk, blue),
	});
}

[[nodiscard]] QString CheckLighterStrip() {
	const auto black = CardStripInk({ QColor(Qt::white), QColor(Qt::black) });
	const auto gray = QColor(40, 40, 40);
	const auto ink = CardStripInk({ QColor(Qt::white), gray });
	return Failures({
		ExpectColor(black, QColor(89, 89, 89)),
		ExpectDarker(gray, ink),
		ExpectRatio(ink, gray, 2.95, 3.05),
	});
}

[[nodiscard]] QString CheckSurroundKept() {
	const auto white = QColor(Qt::white);
	const auto blue = QColor(40, 90, 160);
	const auto gray = QColor(128, 128, 128);
	return Failures({
		ExpectColor(CardInkSurround(blue, white), blue),
		ExpectColor(CardInkSurround(gray, white), gray),
	});
}

[[nodiscard]] QString CheckSurroundDarkened() {
	const auto white = QColor(Qt::white);
	return Failures({
		ExpectSurround(QColor(200, 220, 250), white),
		ExpectSurround(QColor(200, 200, 200), white),
		ExpectSurround(QColor(100, 150, 200), QColor(230, 235, 240)),
	});
}

[[nodiscard]] QString CheckSurroundBlackInk() {
	return ExpectColor(
		CardInkSurround(QColor(40, 90, 160), QColor(Qt::black)),
		QColor(Qt::black));
}

[[nodiscard]] QString CheckMarkEndpoints() {
	const auto from = CardMarkTint{ QColor(64, 64, 64), 0.4 };
	const auto to = CardMarkTint{ QColor(192, 192, 192), 0.9 };
	return Failures({
		ExpectSameMark(MixCardMarkTint(from, to, -1.), from),
		ExpectSameMark(MixCardMarkTint(from, to, 0.), from),
		ExpectSameMark(MixCardMarkTint(from, to, 1.), to),
		ExpectSameMark(MixCardMarkTint(from, to, 2.), to),
	});
}

[[nodiscard]] QString CheckMarkMidpoint() {
	const auto black = QColor(Qt::black);
	const auto white = QColor(Qt::white);
	const auto gray = QColor(188, 188, 188);
	return Failures({
		ExpectMark(
			MixCardMarkTint({ black, 1. }, { white, 1. }, 0.5),
			gray,
			1.),
		ExpectMark(
			MixCardMarkTint(Red(), Blue(), 0.5),
			QColor(188, 0, 188),
			1.),
		ExpectMark(
			MixCardMarkTint({ black, 0.2 }, { white, 1. }, 0.5),
			gray,
			0.6),
	});
}

[[nodiscard]] QString CheckMarkInactiveEnd() {
	const auto red = QColor(Qt::red);
	return Failures({
		ExpectMark(MixCardMarkTint(Red(), Blue(0.), 0.5), red, 0.5),
		ExpectMark(MixCardMarkTint(Blue(0.), Red(), 0.25), red, 0.25),
		ExpectMark(MixCardMarkTint({ QColor(), 0. }, Red(), 0.5), red, 0.5),
		ExpectMark(MixCardMarkTint(Red(0.), Blue(0.), 0.5), Blue().hue, 0.),
	});
}

[[nodiscard]] QString CheckTinterNull() {
	return Expect(
		TintedOnce(QImage(), Red()).isNull(),
		u"a null frame gave a non-null image"_q);
}

[[nodiscard]] QString CheckTinterInactive() {
	const auto frame = MarkFrame();
	auto straight = StraightFrame();
	straight.setDevicePixelRatio(2.);
	auto tinter = CardMarkTinter();
	return Failures({
		ExpectBypass(tinter, frame, Red(0.)),
		ExpectBypass(tinter, frame, Red(-1.)),
		ExpectBypass(tinter, frame, { QColor(), 1. }),
		ExpectBypass(tinter, straight, Red(0.)),
		ExpectBypass(tinter, straight, { QColor(), 1. }),
	});
}

[[nodiscard]] QString CheckTinterNeutral() {
	const auto tinted = MarkFrameTinted(Red());
	if (!tinted.failure.isEmpty()) {
		return tinted.failure;
	}
	return Failures({
		ExpectKept(tinted, 0, 0),
		ExpectKept(tinted, 1, 0),
		ExpectKept(tinted, 2, 0),
		ExpectKept(tinted, 3, 1),
	});
}

[[nodiscard]] QString CheckTinterColored() {
	const auto tinted = MarkFrameTinted(Red());
	if (!tinted.failure.isEmpty()) {
		return tinted.failure;
	}
	return Failures({
		ExpectPixel(tinted.result, 3, 0, qRgba(174, 105, 105, 255)),
		ExpectLuminanceKept(tinted, 3, 0),
		ExpectLuminanceKept(tinted, 1, 1),
		ExpectLuminanceKept(tinted, 2, 1),
	});
}

[[nodiscard]] QString CheckTinterAmount() {
	const auto full = MarkFrameTinted(Red());
	const auto half = MarkFrameTinted(Red(0.5));
	const auto layout = Failures({ full.failure, half.failure });
	if (!layout.isEmpty()) {
		return layout;
	}
	return Failures({
		Expect(
			TintedOnce(full.frame, Red(2.)) == full.result,
			u"amount 2 differs from amount 1"_q),
		ExpectBetween(
			half.result.pixel(3, 0),
			half.frame.pixel(3, 0),
			full.result.pixel(3, 0)),
	});
}

[[nodiscard]] QString CheckTinterAlpha() {
	const auto tinted = MarkFrameTinted(Red());
	if (!tinted.failure.isEmpty()) {
		return tinted.failure;
	}
	const auto pixel = tinted.result.pixel(0, 1);
	const auto alpha = ExpectAlpha(pixel, 128);
	if (!alpha.isEmpty()) {
		return alpha;
	}
	return ExpectClose(qUnpremultiply(pixel), qRgb(174, 105, 105), 2);
}

[[nodiscard]] QString CheckTinterFormat() {
	auto opaque = QImage(1, 1, QImage::Format_RGB32);
	opaque.setPixel(0, 0, qRgb(60, 120, 230));
	const auto straight = TintedOnce(StraightFrame(), Red());
	const auto rgb = TintedOnce(opaque, Red());
	const auto layout = Failures({
		ExpectLayout(straight, QSize(1, 1), kPremultiplied, 1.),
		ExpectLayout(rgb, QSize(1, 1), kPremultiplied, 1.),
	});
	if (!layout.isEmpty()) {
		return layout;
	}
	return Failures({
		ExpectAlpha(straight.pixel(0, 0), 128),
		ExpectPixel(rgb, 0, 0, qRgba(174, 105, 105, 255)),
	});
}

[[nodiscard]] QString CheckTinterRatio() {
	auto frame = MarkFrame();
	frame.setDevicePixelRatio(2.);
	auto straight = StraightFrame();
	straight.setDevicePixelRatio(2.);
	const auto tinted = TintedOnce(frame, Red());
	const auto converted = TintedOnce(straight, Red());
	return Failures({
		ExpectLayout(tinted, frame.size(), kPremultiplied, 2.),
		ExpectLayout(converted, straight.size(), kPremultiplied, 2.),
	});
}

[[nodiscard]] QString CheckTinterRepeat() {
	const auto frame = MarkFrame();
	auto tinter = CardMarkTinter();
	const auto first = tinter.apply(frame, Red());
	const auto second = tinter.apply(frame, Red());
	return Failures({
		Expect(
			second.cacheKey() == first.cacheKey(),
			u"the repeat was not served from the cache"_q),
		Expect(second == first, u"the repeat changed the pixels"_q),
		Expect(
			second == TintedOnce(frame, Red()),
			u"the repeat differs from a fresh tinter"_q),
	});
}

[[nodiscard]] QString CheckTinterRetint() {
	const auto frame = MarkFrame();
	return Failures({
		ExpectRetinted(frame, Red(), Blue()),
		ExpectRetinted(frame, Red(), Red(0.5)),
	});
}

[[nodiscard]] QString CheckTinterNewFrame() {
	const auto frame = MarkFrame();
	auto replacement = frame.copy();
	replacement.setPixel(3, 0, qRgb(230, 120, 60));
	auto single = QImage(1, 1, kPremultiplied);
	single.setPixel(0, 0, qRgb(60, 120, 230));
	auto tinter = CardMarkTinter();
	return Failures({
		ExpectReplaced(tinter, frame, replacement),
		ExpectReplaced(tinter, replacement, single),
		ExpectEditedInPlace(),
	});
}

[[nodiscard]] std::vector<Check> Checks() {
	return {
		{ "luminance.primaries", CheckPrimaries },
		{ "luminance.gray-transfer", CheckGrayTransfer },
		{ "luminance.channels-add", CheckChannelsAdd },
		{ "tint-at.black-white-midpoint", CheckTintMidpoint },
		{ "tint-at.channels-mix-separately", CheckTintChannels },
		{ "tint-at.endpoints", CheckTintEndpoints },
		{ "tint-at.out-of-range-gray-endpoints", CheckTintOutOfRange },
		{ "tint-at.monotone", CheckTintMonotone },
		{ "tint.order-independent", CheckTintOrder },
		{ "tint.inside-interval-unchanged", CheckInsideUnchanged },
		{ "tint.white-text-contrast", CheckWhiteText },
		{ "tint.wide-pair-spans-interval", CheckWidePair },
		{ "tint.shift-keeps-pair-ratio", CheckShiftKeepsRatio },
		{ "tint.dark-surfaces-lift-pair", CheckDarkSurfaces },
		{ "tint.split-interval-nearest-side", CheckSplitInterval },
		{ "tint.day-surfaces-ignored", CheckDaySurfaces },
		{ "tint.infeasible-surfaces-fall-back", CheckInfeasibleSurfaces },
		{ "strip.darker-by-1-6", CheckDarkerStrip },
		{ "strip.lighter-by-3-when-too-dark", CheckLighterStrip },
		{ "surround.dark-enough-card-kept", CheckSurroundKept },
		{ "surround.light-card-darkened-to-2-8", CheckSurroundDarkened },
		{ "surround.black-ink-gives-black", CheckSurroundBlackInk },
		{ "mark-mix.endpoints", CheckMarkEndpoints },
		{ "mark-mix.linear-light-midpoint", CheckMarkMidpoint },
		{ "mark-mix.inactive-end-keeps-active-hue", CheckMarkInactiveEnd },
		{ "tinter.null-frame", CheckTinterNull },
		{ "tinter.inactive-tint-returns-frame", CheckTinterInactive },
		{ "tinter.neutral-pixels-unchanged", CheckTinterNeutral },
		{ "tinter.colored-pixel-moves-to-hue", CheckTinterColored },
		{ "tinter.amount-clamps-at-one", CheckTinterAmount },
		{ "tinter.partial-alpha", CheckTinterAlpha },
		{ "tinter.format", CheckTinterFormat },
		{ "tinter.device-pixel-ratio", CheckTinterRatio },
		{ "tinter.repeat-served-from-cache", CheckTinterRepeat },
		{ "tinter.changed-tint-matches-fresh", CheckTinterRetint },
		{ "tinter.replaced-frame-matches-fresh", CheckTinterNewFrame },
	};
}

[[nodiscard]] bool Passes(const Check &check) {
	const auto failure = check.run();
	if (!failure.isEmpty()) {
		std::printf("FAIL %s: %s\n", check.name, failure.toUtf8().constData());
		return false;
	}
	std::printf("PASS %s\n", check.name);
	return true;
}

} // namespace

int main() {
	const auto checks = Checks();
	const auto total = int(checks.size());
	auto passed = 0;
	for (const auto &check : checks) {
		if (Passes(check)) {
			++passed;
		}
	}
	std::printf("%d of %d checks passed.\n", passed, total);
	return (passed == total) ? 0 : 1;
}
