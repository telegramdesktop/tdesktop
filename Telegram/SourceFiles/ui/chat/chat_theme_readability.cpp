/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chat_theme_readability.h"

#include "ui/style/style_core_palette.h"
#include "ui/color_contrast.h"

#include <QtCore/QtMath>

namespace Ui {
namespace {

constexpr auto kRoles = 6;
constexpr auto kStep = 0.005;
constexpr auto kSteps = 160;
constexpr auto kOutgoingLift = 0.08;
constexpr auto kMinChroma = 0.03;
constexpr auto kOutgoingChromaScale = 1.15;
constexpr auto kOutgoingChromaMin = 0.05;
constexpr auto kOutgoingChromaMax = 0.16;
constexpr auto kGamutIterations = 30;
constexpr auto kGamutEpsilon = 1e-4;
constexpr auto kOliveFrom = 95.;
constexpr auto kOliveSplit = 110.;
constexpr auto kOliveTo = 125.;
constexpr auto kOliveWarm = 75.;
constexpr auto kOliveGreen = 140.;
constexpr auto kOliveLightness = 0.66;
constexpr auto kOliveRange = 0.18;
constexpr auto kWhiteTextContrast = 4.5;

using Getter = const style::color &(style::palette_data::*)() const;

struct Lab {
	float64 lightness = 0.;
	float64 a = 0.;
	float64 b = 0.;
};

struct Floors {
	float64 bubbleText = 0.;
	float64 text = 0.;
	float64 separation = 0.;
	std::array<float64, kRoles> selected = {};
	std::array<float64, kRoles> unselected = {};
	float64 names = 0.;
	float64 namesUnselected = 0.;
};

struct Side {
	Getter bg = nullptr;
	Getter bgSelected = nullptr;
	Getter text = nullptr;
	Getter textSelected = nullptr;
	std::array<Getter, kRoles> selected = {};
	std::array<Getter, kRoles> unselected = {};
	std::vector<Getter> bgDependents;
	std::vector<Getter> dependents;
	bool names = false;
};

using P = style::palette_data;

[[nodiscard]] const Side &Incoming() {
	static const auto result = Side{
		.bg = &P::msgInBg,
		.bgSelected = &P::msgInBgSelected,
		.text = &P::historyTextInFg,
		.textSelected = &P::historyTextInFgSelected,
		.selected = {
			&P::historyLinkInFgSelected,
			&P::msgInServiceFgSelected,
			&P::msgInDateFgSelected,
			&P::msgInMonoFgSelected,
			&P::msgInReplyBarSelColor,
			nullptr,
		},
		.unselected = {
			&P::historyLinkInFg,
			&P::msgInServiceFg,
			&P::msgInDateFg,
			&P::msgInMonoFg,
			&P::msgInReplyBarColor,
			nullptr,
		},
		.bgDependents = {
			&P::historyFileInIconFg,
			&P::historyFileThumbIconFg,
			&P::historyFileInRadialFg,
		},
		.dependents = {
			&P::historyFileInIconFgSelected,
			&P::historyFileThumbIconFgSelected,
			&P::historyFileInRadialFgSelected,
		},
		.names = true,
	};
	return result;
}

[[nodiscard]] const Side &Outgoing() {
	static const auto result = Side{
		.bg = &P::msgOutBg,
		.bgSelected = &P::msgOutBgSelected,
		.text = &P::historyTextOutFg,
		.textSelected = &P::historyTextOutFgSelected,
		.selected = {
			&P::historyLinkOutFgSelected,
			&P::msgOutServiceFgSelected,
			&P::msgOutDateFgSelected,
			&P::msgOutMonoFgSelected,
			&P::msgOutReplyBarSelColor,
			&P::historyOutIconFgSelected,
		},
		.unselected = {
			&P::historyLinkOutFg,
			&P::msgOutServiceFg,
			&P::msgOutDateFg,
			&P::msgOutMonoFg,
			&P::msgOutReplyBarColor,
			&P::historyOutIconFg,
		},
		.bgDependents = {
			&P::historyFileOutIconFg,
			&P::historyFileOutRadialFg,
		},
		.dependents = {
			&P::historyFileOutIconFgSelected,
			&P::historyFileOutRadialFgSelected,
		},
	};
	return result;
}

[[nodiscard]] const std::array<Getter, 8> &PeerNames() {
	static const auto result = std::array<Getter, 8>{
		&P::historyPeer1NameFg,
		&P::historyPeer2NameFg,
		&P::historyPeer3NameFg,
		&P::historyPeer4NameFg,
		&P::historyPeer5NameFg,
		&P::historyPeer6NameFg,
		&P::historyPeer7NameFg,
		&P::historyPeer8NameFg,
	};
	return result;
}

[[nodiscard]] const std::array<Getter, 8> &PeerNamesSelected() {
	static const auto result = std::array<Getter, 8>{
		&P::historyPeer1NameFgSelected,
		&P::historyPeer2NameFgSelected,
		&P::historyPeer3NameFgSelected,
		&P::historyPeer4NameFgSelected,
		&P::historyPeer5NameFgSelected,
		&P::historyPeer6NameFgSelected,
		&P::historyPeer7NameFgSelected,
		&P::historyPeer8NameFgSelected,
	};
	return result;
}

[[nodiscard]] Floors GlobalFloors(bool dark) {
	auto result = dark
		? Floors{
			.bubbleText = 4.5,
			.text = 4.5,
			.separation = 0.10,
			.selected = { 3.0, 3.0, 3.0, 3.0, 2.5, 3.0 },
			.names = 3.0,
		}
		: Floors{
			.bubbleText = 4.5,
			.text = 4.5,
			.separation = 0.06,
			.selected = { 2.5, 2.2, 1.7, 2.4, 2.0, 1.7 },
			.names = 1.8,
		};
	result.unselected = result.selected;
	result.namesUnselected = result.names;
	return result;
}

[[nodiscard]] float64 ToLinear(int channel) {
	const auto value = channel / 255.;
	return (value <= 0.04045)
		? (value / 12.92)
		: std::pow((value + 0.055) / 1.055, 2.4);
}

[[nodiscard]] float64 FromLinear(float64 value) {
	value = std::clamp(value, 0., 1.);
	return (value <= 0.0031308)
		? (12.92 * value)
		: (1.055 * std::pow(value, 1. / 2.4) - 0.055);
}

[[nodiscard]] Lab ToLab(const QColor &color) {
	const auto r = ToLinear(color.red());
	const auto g = ToLinear(color.green());
	const auto b = ToLinear(color.blue());
	const auto l = std::cbrt(
		0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
	const auto m = std::cbrt(
		0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
	const auto s = std::cbrt(
		0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
	return {
		.lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
		.a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
		.b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
	};
}

[[nodiscard]] float64 Chroma(const Lab &lab) {
	return std::hypot(lab.a, lab.b);
}

[[nodiscard]] float64 Hue(const Lab &lab) {
	const auto result = std::fmod(
		std::atan2(lab.b, lab.a) * 180. / M_PI,
		360.);
	return (result < 0.) ? (result + 360.) : result;
}

[[nodiscard]] std::array<float64, 3> LinearFromLch(
		float64 lightness,
		float64 chroma,
		float64 hue) {
	const auto radians = hue * M_PI / 180.;
	const auto a = chroma * std::cos(radians);
	const auto b = chroma * std::sin(radians);
	const auto l = lightness + 0.3963377774 * a + 0.2158037573 * b;
	const auto m = lightness - 0.1055613458 * a - 0.0638541728 * b;
	const auto s = lightness - 0.0894841775 * a - 1.2914855480 * b;
	const auto l3 = l * l * l;
	const auto m3 = m * m * m;
	const auto s3 = s * s * s;
	return {
		4.0767416621 * l3 - 3.3077115913 * m3 + 0.2309699292 * s3,
		-1.2684380046 * l3 + 2.6097574011 * m3 - 0.3413193965 * s3,
		-0.0041960863 * l3 - 0.7034186147 * m3 + 1.7076147010 * s3,
	};
}

[[nodiscard]] bool InGamut(float64 lightness, float64 chroma, float64 hue) {
	for (const auto value : LinearFromLch(lightness, chroma, hue)) {
		if (value < -kGamutEpsilon || value > 1. + kGamutEpsilon) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QColor FromLch(float64 lightness, float64 chroma, float64 hue) {
	if (!InGamut(lightness, chroma, hue)) {
		auto low = 0.;
		auto high = chroma;
		for (auto i = 0; i != kGamutIterations; ++i) {
			const auto middle = (low + high) / 2.;
			if (InGamut(lightness, middle, hue)) {
				low = middle;
			} else {
				high = middle;
			}
		}
		chroma = low;
	}
	const auto rgb = LinearFromLch(lightness, chroma, hue);
	const auto channel = [](float64 value) {
		return int(std::round(FromLinear(value) * 255.));
	};
	return QColor(channel(rgb[0]), channel(rgb[1]), channel(rgb[2]));
}

// WHY: darkened yellows read as olive; amber or green keeps them clean.
[[nodiscard]] float64 NudgedHue(float64 hue, float64 lightness) {
	if (hue < kOliveFrom || hue > kOliveTo || lightness >= kOliveLightness) {
		return hue;
	}
	const auto progress = std::min(
		(kOliveLightness - lightness) / kOliveRange,
		1.);
	const auto target = (hue < kOliveSplit) ? kOliveWarm : kOliveGreen;
	return hue + (target - hue) * progress;
}

[[nodiscard]] QColor Make(
		float64 lightness,
		float64 chroma,
		float64 hue,
		bool nudge) {
	return FromLch(
		lightness,
		chroma,
		nudge ? NudgedHue(hue, lightness) : hue);
}

[[nodiscard]] QColor WithAlpha(QColor color, int alpha) {
	color.setAlpha(alpha);
	return color;
}

[[nodiscard]] QColor Composite(const QColor &over, const QColor &under) {
	if (over.alpha() == 255) {
		return over;
	}
	const auto alpha = over.alpha() / 255.;
	const auto mix = [&](int a, int b) {
		return int(std::round(a * alpha + b * (1. - alpha)));
	};
	return QColor(
		mix(over.red(), under.red()),
		mix(over.green(), under.green()),
		mix(over.blue(), under.blue()));
}

[[nodiscard]] float64 ContrastOver(const QColor &over, const QColor &under) {
	return CountContrast(Composite(over, under), under);
}

[[nodiscard]] float64 Luminance(const QColor &color) {
	const auto map = [](float64 value) {
		return (value <= 0.03928)
			? (value / 12.92)
			: std::pow((value + 0.055) / 1.055, 2.4);
	};
	return map(color.redF()) * 0.2126
		+ map(color.greenF()) * 0.7152
		+ map(color.blueF()) * 0.0722;
}

[[nodiscard]] float64 DeltaE(const QColor &a, const QColor &b) {
	const auto first = ToLab(a);
	const auto second = ToLab(b);
	return std::sqrt(
		std::pow(first.lightness - second.lightness, 2)
		+ std::pow(first.a - second.a, 2)
		+ std::pow(first.b - second.b, 2));
}

[[nodiscard]] std::optional<QColor> NearestFeasible(
		float64 lightness,
		float64 chroma,
		float64 hue,
		Fn<bool(const QColor&)> ok,
		bool nudge) {
	if (const auto color = Make(lightness, chroma, hue, nudge); ok(color)) {
		return color;
	}
	for (auto step = 1; step <= kSteps; ++step) {
		for (const auto sign : { 1, -1 }) {
			const auto now = lightness + sign * step * kStep;
			if (now < 0.05 || now > 0.98) {
				continue;
			}
			const auto color = Make(now, chroma, hue, nudge);
			if (ok(color)) {
				return color;
			}
		}
	}
	return std::nullopt;
}

[[nodiscard]] QColor EnsureContrast(
		const QColor &color,
		const std::vector<QColor> &backgrounds,
		float64 floor,
		bool nudge) {
	const auto ok = [&](const QColor &now) {
		for (const auto &background : backgrounds) {
			if (ContrastOver(now, background) < floor) {
				return false;
			}
		}
		return true;
	};
	if (ok(color)) {
		return color;
	}
	const auto lab = ToLab(color);
	const auto chroma = Chroma(lab);
	const auto hue = Hue(lab);
	const auto up = Luminance(Composite(color, backgrounds.front()))
		>= Luminance(backgrounds.front());
	for (const auto direction : { up ? 1 : -1, up ? -1 : 1 }) {
		for (auto step = 1; step <= kSteps; ++step) {
			const auto lightness = lab.lightness + direction * step * kStep;
			if (lightness < 0. || lightness > 1.) {
				break;
			}
			const auto now = WithAlpha(
				Make(lightness, chroma, hue, nudge),
				color.alpha());
			if (ok(now)) {
				return now;
			}
		}
	}
	return (color.alpha() != 255)
		? EnsureContrast(WithAlpha(color, 255), backgrounds, floor, nudge)
		: color;
}

[[nodiscard]] QColor ChooseSelectedBg(
		const std::vector<QColor> &stops,
		const QColor &selected,
		const QColor &text,
		bool dark,
		const Floors &floors,
		bool derive) {
	const auto ok = [&](const QColor &color) {
		if (ContrastOver(text, color) < floors.text) {
			return false;
		}
		for (const auto &stop : stops) {
			if (DeltaE(color, stop) < floors.separation) {
				return false;
			}
		}
		return true;
	};
	if (!derive && ok(selected)) {
		return selected;
	}
	const auto lab = ToLab(selected);
	auto lightness = lab.lightness;
	auto chroma = Chroma(lab);
	auto hue = Hue(lab);
	if (derive) {
		auto mean = Lab();
		auto lightest = 0.;
		for (const auto &stop : stops) {
			const auto value = ToLab(stop);
			mean.lightness += value.lightness;
			mean.a += value.a;
			mean.b += value.b;
			lightest = std::max(lightest, value.lightness);
		}
		mean.lightness /= stops.size();
		mean.a /= stops.size();
		mean.b /= stops.size();
		if (const auto value = Chroma(mean); value >= kMinChroma) {
			chroma = std::clamp(
				value * kOutgoingChromaScale,
				kOutgoingChromaMin,
				kOutgoingChromaMax);
			hue = Hue(mean);
		}
		lightness = lightest + kOutgoingLift;
	}
	const auto found = NearestFeasible(lightness, chroma, hue, ok, dark);
	return found ? WithAlpha(*found, selected.alpha()) : selected;
}

[[nodiscard]] std::vector<QColor> SideStops(
		const style::palette &palette,
		const Side &side,
		const std::vector<QColor> &gradient) {
	return (gradient.size() > 1)
		? gradient
		: std::vector<QColor>{ (palette.*side.bg)()->c };
}

[[nodiscard]] Floors Measure(
		const style::palette &palette,
		const Side &side,
		const std::vector<QColor> &stops) {
	const auto selected = (palette.*side.bgSelected)()->c;
	const auto max = std::numeric_limits<float64>::max();
	auto result = Floors{
		.bubbleText = max,
		.text = ContrastOver((palette.*side.textSelected)()->c, selected),
		.separation = max,
	};
	for (const auto &stop : stops) {
		result.bubbleText = std::min(
			result.bubbleText,
			ContrastOver((palette.*side.text)()->c, stop));
		result.separation = std::min(
			result.separation,
			DeltaE(selected, stop));
	}
	for (auto i = 0; i != kRoles; ++i) {
		if (!side.selected[i]) {
			result.selected[i] = result.unselected[i] = max;
			continue;
		}
		result.selected[i] = ContrastOver(
			(palette.*side.selected[i])()->c,
			selected);
		result.unselected[i] = max;
		for (const auto &stop : stops) {
			result.unselected[i] = std::min(
				result.unselected[i],
				ContrastOver((palette.*side.unselected[i])()->c, stop));
		}
	}
	if (side.names) {
		result.names = result.namesUnselected = max;
		for (auto i = 0; i != int(PeerNames().size()); ++i) {
			const auto &name = (palette.*PeerNames()[i])();
			const auto &nameSelected = (palette.*PeerNamesSelected()[i])();
			result.names = std::min(
				result.names,
				ContrastOver(nameSelected->c, selected));
			result.namesUnselected = std::min(
				result.namesUnselected,
				ContrastOver(name->c, stops.front()));
		}
	}
	return result;
}

[[nodiscard]] Floors Lowest(Floors a, const Floors &b) {
	a.bubbleText = std::min(a.bubbleText, b.bubbleText);
	a.text = std::min(a.text, b.text);
	a.separation = std::min(a.separation, b.separation);
	for (auto i = 0; i != kRoles; ++i) {
		a.selected[i] = std::min(a.selected[i], b.selected[i]);
		a.unselected[i] = std::min(a.unselected[i], b.unselected[i]);
	}
	a.names = std::min(a.names, b.names);
	a.namesUnselected = std::min(a.namesUnselected, b.namesUnselected);
	return a;
}

void Set(const style::color &color, const QColor &value) {
	color.set(
		uchar(value.red()),
		uchar(value.green()),
		uchar(value.blue()),
		uchar(value.alpha()));
}

void Replace(
		const style::palette &palette,
		const style::color &color,
		const QColor &now,
		const std::vector<Getter> &dependents) {
	const auto was = color->c;
	if (now == was) {
		return;
	}
	Set(color, now);
	for (const auto getter : dependents) {
		if (const auto &other = (palette.*getter)(); other->c == was) {
			Set(other, now);
		}
	}
}

void SetContrasted(
		const style::color &color,
		const std::vector<QColor> &backgrounds,
		float64 floor) {
	const auto now = EnsureContrast(color->c, backgrounds, floor, true);
	if (now != color->c) {
		Set(color, now);
	}
}

void EnsureSideReadable(
		const style::palette &palette,
		const Side &side,
		const BubblesReadabilityArgs &args,
		bool outgoing) {
	const auto gradient = outgoing
		? args.outgoingGradient
		: std::vector<QColor>();
	const auto global = GlobalFloors(args.dark);
	const auto floors = args.reference
		? Lowest(global, Measure(
			*args.reference,
			side,
			SideStops(*args.reference, side, {})))
		: global;
	if (gradient.size() < 2) {
		const auto &bubble = (palette.*side.bg)();
		const auto now = EnsureContrast(
			bubble->c,
			{ (palette.*side.text)()->c },
			floors.bubbleText,
			true);
		Replace(palette, bubble, now, side.bgDependents);
	}
	const auto stops = SideStops(palette, side, gradient);
	const auto &selected = (palette.*side.bgSelected)();
	const auto now = ChooseSelectedBg(
		stops,
		selected->c,
		(palette.*side.textSelected)()->c,
		args.dark,
		floors,
		outgoing && args.deriveOutgoingSelection);
	Replace(palette, selected, now, side.dependents);
	for (auto i = 0; i != kRoles; ++i) {
		if (side.selected[i]) {
			SetContrasted(
				(palette.*side.selected[i])(),
				{ now },
				floors.selected[i]);
		}
	}
	for (auto i = 0; i != kRoles; ++i) {
		if (side.unselected[i]) {
			SetContrasted(
				(palette.*side.unselected[i])(),
				stops,
				floors.unselected[i]);
		}
	}
	if (side.names) {
		for (auto i = 0; i != int(PeerNames().size()); ++i) {
			SetContrasted(
				(palette.*PeerNamesSelected()[i])(),
				{ now },
				floors.names);
			SetContrasted(
				(palette.*PeerNames()[i])(),
				stops,
				floors.namesUnselected);
		}
	}
}

} // namespace

void EnsureBubblesReadable(
		const style::palette &palette,
		const BubblesReadabilityArgs &args) {
	EnsureSideReadable(palette, Incoming(), args, false);
	EnsureSideReadable(palette, Outgoing(), args, true);
}

bool IsDarkPalette(const style::palette &palette) {
	const auto background = palette.windowBg()->c;
	return CountContrast(background, QColor(0, 0, 0))
		< CountContrast(background, QColor(255, 255, 255));
}

QColor EnsurePeerNameReadable(
		const QColor &color,
		const QColor &background,
		bool dark) {
	return EnsureContrast(
		color,
		{ background },
		GlobalFloors(dark).names,
		true);
}

std::vector<QColor> DarkenUnderWhiteText(std::vector<QColor> colors) {
	const auto white = QColor(255, 255, 255);
	for (auto &color : colors) {
		if (ContrastOver(white, color) >= kWhiteTextContrast) {
			continue;
		}
		const auto lab = ToLab(color);
		const auto chroma = Chroma(lab);
		const auto hue = Hue(lab);
		for (auto step = 1; step <= kSteps; ++step) {
			const auto now = Make(
				lab.lightness - step * kStep,
				chroma,
				hue,
				true);
			if (ContrastOver(white, now) >= kWhiteTextContrast) {
				color = now;
				break;
			}
		}
	}
	return colors;
}

} // namespace Ui
