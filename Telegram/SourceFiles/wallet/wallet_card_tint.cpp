/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_tint.h"

namespace Wallet {
namespace {

constexpr auto kTextContrast = 3.1;
constexpr auto kSurfaceContrast = 1.55;
constexpr auto kStripDarker = 1.6;
constexpr auto kStripLighter = 3.;
constexpr auto kMaxLuminance = 1.05 / kTextContrast - 0.05;
constexpr auto kLinearKnee = 0.03928;

struct Linear {
	float64 red = 0.;
	float64 green = 0.;
	float64 blue = 0.;
};

struct Interval {
	float64 from = 0.;
	float64 till = 0.;
};

struct Placement {
	float64 dark = 0.;
	float64 light = 0.;
	float64 cost = 0.;
};

[[nodiscard]] float64 ToLinear(float64 channel) {
	return (channel <= kLinearKnee)
		? (channel / 12.92)
		: std::pow((channel + 0.055) / 1.055, 2.4);
}

[[nodiscard]] float64 FromLinear(float64 value) {
	const auto clamped = std::clamp(value, 0., 1.);
	return (clamped <= kLinearKnee / 12.92)
		? (clamped * 12.92)
		: (1.055 * std::pow(clamped, 1. / 2.4) - 0.055);
}

[[nodiscard]] const std::array<float64, 256> &LinearChannels() {
	static const auto result = [] {
		auto channels = std::array<float64, 256>();
		for (auto i = 0; i != 256; ++i) {
			channels[i] = ToLinear(i / 255.);
		}
		return channels;
	}();
	return result;
}

[[nodiscard]] Linear ToLinear(const QColor &color) {
	const auto &channels = LinearChannels();
	const auto rgb = color.rgb();
	return { channels[qRed(rgb)], channels[qGreen(rgb)], channels[qBlue(rgb)] };
}

[[nodiscard]] QColor FromLinear(const Linear &linear) {
	const auto channel = [](float64 value) {
		return int(base::SafeRound(FromLinear(value) * 255.));
	};
	return QColor(
		channel(linear.red),
		channel(linear.green),
		channel(linear.blue));
}

[[nodiscard]] float64 Luminance(const Linear &linear) {
	return 0.2126 * linear.red
		+ 0.7152 * linear.green
		+ 0.0722 * linear.blue;
}

[[nodiscard]] QColor WithLuminance(const QColor &color, float64 target) {
	const auto goal = std::clamp(target, 0., 1.);
	const auto linear = ToLinear(color);
	const auto luminance = Luminance(linear);
	const auto top = std::max({ linear.red, linear.green, linear.blue });
	if (luminance > 0. && top * goal <= luminance) {
		const auto scale = goal / luminance;
		return FromLinear({
			linear.red * scale,
			linear.green * scale,
			linear.blue * scale,
		});
	}
	const auto full = (top > 0.)
		? Linear{ linear.red / top, linear.green / top, linear.blue / top }
		: Linear();
	const auto from = Luminance(full);
	const auto mix = std::clamp((goal - from) / (1. - from), 0., 1.);
	return FromLinear({
		full.red + (1. - full.red) * mix,
		full.green + (1. - full.green) * mix,
		full.blue + (1. - full.blue) * mix,
	});
}

[[nodiscard]] std::vector<Interval> AllowedIntervals(
		const std::vector<QColor> &surfaces) {
	auto result = std::vector<Interval>{ { 0., kMaxLuminance } };
	for (const auto &surface : surfaces) {
		const auto shifted = CardLuminance(surface.rgb()) + 0.05;
		const auto from = shifted / kSurfaceContrast - 0.05;
		const auto till = shifted * kSurfaceContrast - 0.05;
		auto clipped = std::vector<Interval>();
		clipped.reserve(result.size() + 1);
		for (const auto &interval : result) {
			if (till <= interval.from || from >= interval.till) {
				clipped.push_back(interval);
				continue;
			}
			if (from >= interval.from) {
				clipped.push_back({ interval.from, from });
			}
			if (till <= interval.till) {
				clipped.push_back({ till, interval.till });
			}
		}
		result = std::move(clipped);
	}
	return result;
}

[[nodiscard]] Placement Place(
		float64 dark,
		float64 light,
		const Interval &interval) {
	const auto darkLog = std::log(dark + 0.05);
	const auto lightLog = std::log(light + 0.05);
	const auto fromLog = std::log(interval.from + 0.05);
	const auto tillLog = std::log(interval.till + 0.05);
	if (lightLog - darkLog > tillLog - fromLog) {
		return {
			interval.from,
			interval.till,
			std::max(
				std::abs(fromLog - darkLog),
				std::abs(tillLog - lightLog)),
		};
	}
	const auto shift = (darkLog < fromLog)
		? (fromLog - darkLog)
		: (lightLog > tillLog)
		? (tillLog - lightLog)
		: 0.;
	const auto scale = std::exp(shift);
	return {
		(dark + 0.05) * scale - 0.05,
		(light + 0.05) * scale - 0.05,
		std::abs(shift),
	};
}

} // namespace

float64 CardLuminance(QRgb color) {
	const auto &channels = LinearChannels();
	return 0.2126 * channels[qRed(color)]
		+ 0.7152 * channels[qGreen(color)]
		+ 0.0722 * channels[qBlue(color)];
}

CardTint ComputeCardTint(
		const QColor &first,
		const QColor &second,
		const std::vector<QColor> &surfaces) {
	const auto swap = CardLuminance(second.rgb())
		> CardLuminance(first.rgb());
	const auto &light = swap ? second : first;
	const auto &dark = swap ? first : second;
	const auto lightLuminance = CardLuminance(light.rgb());
	const auto darkLuminance = CardLuminance(dark.rgb());
	auto intervals = AllowedIntervals(surfaces);
	if (intervals.empty()) {
		intervals.push_back({ 0., kMaxLuminance });
	}
	auto best = Placement{ .cost = std::numeric_limits<float64>::infinity() };
	for (const auto &interval : intervals) {
		const auto placement = Place(darkLuminance, lightLuminance, interval);
		if (placement.cost < best.cost) {
			best = placement;
		}
	}
	return {
		WithLuminance(light, best.light),
		WithLuminance(dark, best.dark),
	};
}

QColor CardTintAt(const CardTint &tint, float64 level) {
	const auto dark = ToLinear(tint.dark);
	const auto light = ToLinear(tint.light);
	const auto progress = std::clamp(level, 0., 1.);
	const auto mix = [&](float64 from, float64 till) {
		return from + (till - from) * progress;
	};
	return FromLinear({
		mix(dark.red, light.red),
		mix(dark.green, light.green),
		mix(dark.blue, light.blue),
	});
}

QColor CardStripInk(const CardTint &tint) {
	const auto shifted = CardLuminance(tint.dark.rgb()) + 0.05;
	const auto darker = shifted / kStripDarker - 0.05;
	return WithLuminance(
		tint.dark,
		(darker >= 0.) ? darker : (shifted * kStripLighter - 0.05));
}

} // namespace Wallet
