/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_palette.h"

#include "base/flat_map.h"
#include "ui/style/style_palette_colorizer.h"
#include "ui/window_palette.h"
#include "wallet/wallet_card_gradient.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"

namespace Wallet {
namespace {

using Window::Theme::EmbeddedType;

struct WindowPaletteData {
	int version = -1;
	std::unique_ptr<style::palette> palette;
};

[[nodiscard]] std::array<double, 3> ToOklab(const QColor &color) {
	const auto linear = [](float channel) {
		const auto value = double(channel);
		return (value <= 0.04045)
			? (value / 12.92)
			: std::pow((value + 0.055) / 1.055, 2.4);
	};
	const auto r = linear(color.redF());
	const auto g = linear(color.greenF());
	const auto b = linear(color.blueF());
	const auto l = std::cbrt(
		0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
	const auto m = std::cbrt(
		0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
	const auto s = std::cbrt(
		0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
	return {
		0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
		1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
		0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
	};
}

[[nodiscard]] double OklabDistance(const QColor &a, const QColor &b) {
	const auto first = ToOklab(a);
	const auto second = ToOklab(b);
	const auto dl = first[0] - second[0];
	const auto da = first[1] - second[1];
	const auto db = first[2] - second[2];
	return std::sqrt(dl * dl + da * da + db * db);
}

[[nodiscard]] QColor ComputeCardMatchedAccent(EmbeddedType type) {
	const auto target = CardReferenceColor();
	const auto schemes = Window::Theme::EmbeddedThemes();
	const auto scheme = ranges::find(
		schemes,
		type,
		&Window::Theme::EmbeddedScheme::type);
	Assert(scheme != end(schemes));

	const auto raw = Window::Theme::PrepareEmbeddedPalette(
		type,
		std::nullopt);
	const auto rawFill = raw->colorAtIndex(
		style::main_palette::indexOfColor(st::activeButtonBg))->c;
	const auto fill = [&](const QColor &candidate) {
		auto r = uchar(rawFill.red());
		auto g = uchar(rawFill.green());
		auto b = uchar(rawFill.blue());
		style::colorize(
			QLatin1String("activeButtonBg"),
			r,
			g,
			b,
			Window::Theme::ColorizerFrom(*scheme, candidate));
		return QColor(r, g, b);
	};

	auto best = scheme->accentColor;
	auto bestDistance = OklabDistance(fill(best), target);
	const auto consider = [&](const QColor &candidate) {
		const auto distance = OklabDistance(fill(candidate), target);
		if (distance < bestDistance) {
			best = candidate;
			bestDistance = distance;
		}
	};
	for (const auto &preset : Window::Theme::DefaultAccentColors(type)) {
		consider(preset);
	}
	consider(target);

	const auto search = [&](
			int hue,
			int hueRange,
			int hueStep,
			QPoint saturation,
			QPoint value,
			int step) {
		for (auto h = hue - hueRange; h <= hue + hueRange; h += hueStep) {
			for (auto s = saturation.x(); s <= saturation.y(); s += step) {
				for (auto v = value.x(); v <= value.y(); v += step) {
					consider(QColor::fromHsv((h + 360) % 360, s, v));
				}
			}
		}
	};
	const auto around = [](int value, int range) {
		return QPoint(
			std::max(value - range, 0),
			std::min(value + range, 255));
	};
	search(target.hue(), 40, 4, { 40, 255 }, { 40, 255 }, 16);
	const auto coarse = best;
	search(
		coarse.hue(),
		3,
		1,
		around(coarse.saturation(), 8),
		around(coarse.value(), 8),
		1);
	return best;
}

} // namespace

QColor CardMatchedAccent(EmbeddedType type) {
	static auto cache = base::flat_map<EmbeddedType, QColor>();
	const auto i = cache.find(type);
	if (i != end(cache)) {
		return i->second;
	}
	return cache.emplace(type, ComputeCardMatchedAccent(type)).first->second;
}

const style::palette *WindowPalette() {
	static auto data = WindowPaletteData();
	if (data.version != style::PaletteVersion()) {
		data.version = style::PaletteVersion();
		const auto type = Window::Theme::CurrentEmbeddedType();
		data.palette = type
			? Window::Theme::PrepareEmbeddedPalette(
				*type,
				CardMatchedAccent(*type))
			: nullptr;
	}
	return data.palette.get();
}

void UseWindowPalette(not_null<Ui::RpWidget*> window) {
	Ui::SetWindowPalette(window, [] { return WindowPalette(); });
}

style::main_palette::Override WindowPaletteScope(
		not_null<const QWidget*> widget) {
	return style::main_palette::Override(Ui::WindowPaletteFor(widget));
}

} // namespace Wallet
