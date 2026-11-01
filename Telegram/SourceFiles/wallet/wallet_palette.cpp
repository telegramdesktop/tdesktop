/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_palette.h"

#include "base/flat_map.h"
#include "ui/style/style_palette_colorizer.h"
#include "ui/rp_widget.h"
#include "ui/window_palette.h"
#include "wallet/wallet_card_gradient.h"
#include "wallet/wallet_card_look.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"

namespace Wallet {
namespace {

using Window::Theme::EmbeddedType;

constexpr auto kDerivedLimit = 8;

using Oklab = std::array<double, 3>;

[[nodiscard]] Oklab ToOklab(const QColor &color) {
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

[[nodiscard]] double OklabDistance(const Oklab &first, const Oklab &second) {
	const auto dl = first[0] - second[0];
	const auto da = first[1] - second[1];
	const auto db = first[2] - second[2];
	return std::sqrt(dl * dl + da * da + db * db);
}

[[nodiscard]] QColor RawActiveButtonBg(EmbeddedType type) {
	static auto cache = base::flat_map<EmbeddedType, QColor>();
	const auto i = cache.find(type);
	if (i != end(cache)) {
		return i->second;
	}
	const auto raw = Window::Theme::PrepareEmbeddedPalette(
		type,
		std::nullopt);
	const auto fill = raw->colorAtIndex(
		style::main_palette::indexOfColor(st::activeButtonBg))->c;
	return cache.emplace(type, fill).first->second;
}

[[nodiscard]] rpl::event_stream<const style::palette*> &Recolored() {
	static auto result = rpl::event_stream<const style::palette*>();
	return result;
}

[[nodiscard]] std::optional<QColor> LookTarget(const CardLook &look) {
	if (look.brand()) {
		return std::nullopt;
	}
	return CardReferenceColor(ComputeCardTint(look.center, look.edge, {}));
}

} // namespace

QColor ComputeMatchedAccent(EmbeddedType type, const QColor &target) {
	const auto schemes = Window::Theme::EmbeddedThemes();
	const auto scheme = ranges::find(
		schemes,
		type,
		&Window::Theme::EmbeddedScheme::type);
	Assert(scheme != end(schemes));

	const auto rawFill = RawActiveButtonBg(type);
	const auto goal = ToOklab(target);
	auto colorizer = Window::Theme::ColorizerFrom(
		*scheme,
		scheme->accentColor);
	const auto measure = [&](const QColor &candidate) {
		auto r = uchar(rawFill.red());
		auto g = uchar(rawFill.green());
		auto b = uchar(rawFill.blue());
		Window::Theme::ApplyColorizerAccent(colorizer, candidate);
		style::colorize(
			QLatin1String("activeButtonBg"),
			r,
			g,
			b,
			colorizer);
		return OklabDistance(ToOklab(QColor(r, g, b)), goal);
	};

	auto best = scheme->accentColor;
	auto bestDistance = measure(best);
	const auto consider = [&](const QColor &candidate) {
		const auto distance = measure(candidate);
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

QColor CardMatchedAccent(EmbeddedType type) {
	static auto cache = base::flat_map<EmbeddedType, QColor>();
	const auto i = cache.find(type);
	if (i != end(cache)) {
		return i->second;
	}
	return cache.emplace(
		type,
		ComputeMatchedAccent(type, CardReferenceColor())).first->second;
}

WindowPalette::WindowPalette(not_null<QWidget*> window)
: _window(window) {
}

const style::palette *WindowPalette::resolve(bool child) {
	if (_version != style::PaletteVersion()) {
		rebuild();
	}
	return !_type
		? nullptr
		: (child && _childrenHeld)
		? _children.get()
		: _palette.get();
}

void WindowPalette::prepare(const CardLook &look) {
	const auto target = LookTarget(look);
	const auto type = Window::Theme::CurrentEmbeddedType();
	if (target && type) {
		derived(*type, *target);
	}
}

void WindowPalette::setLook(const CardLook &look) {
	auto target = LookTarget(look);
	if (_target == target) {
		return;
	}
	_target = std::move(target);
	if (!_type || _version != style::PaletteVersion()) {
		return;
	}
	if (_recipe->apply(*_palette, accent(*_type))) {
		notify();
	}
}

void WindowPalette::holdChildren() {
	if (_childrenHeld || !resolve(false)) {
		return;
	}
	if (!_children) {
		_children = std::make_unique<style::palette>();
		_children->finalize();
	}
	*_children = *_palette;
	_childrenHeld = true;
}

void WindowPalette::releaseChildren() {
	if (!base::take(_childrenHeld)) {
		return;
	}
	notify();
	if (const auto window = _window.get()) {
		Ui::UpdateWindowPaletteChildren(window);
	}
}

void WindowPalette::notify() {
	const auto scope = style::main_palette::Override(_palette.get());
	Recolored().fire(_palette.get());
}

void WindowPalette::rebuild() {
	_version = style::PaletteVersion();
	_childrenHeld = false;
	_recipe = nullptr;
	_type = Window::Theme::CurrentEmbeddedType();
	if (!_type) {
		return;
	}
	_recipe = &Window::Theme::AccentRecipe(*_type);
	auto built = Window::Theme::PrepareEmbeddedPalette(
		*_type,
		accent(*_type));
	if (_palette) {
		*_palette = *built;
	} else {
		_palette = std::move(built);
	}
}

QColor WindowPalette::accent(EmbeddedType type) {
	return _target ? derived(type, *_target) : CardMatchedAccent(type);
}

QColor WindowPalette::derived(EmbeddedType type, const QColor &target) {
	const auto key = target.rgba();
	const auto i = ranges::find_if(_derived, [&](const Derived &entry) {
		return (entry.type == type) && (entry.target == key);
	});
	if (i != end(_derived)) {
		std::rotate(begin(_derived), i, i + 1);
		return _derived.front().accent;
	}
	if (int(_derived.size()) == kDerivedLimit) {
		_derived.pop_back();
	}
	_derived.insert(begin(_derived), Derived{
		.type = type,
		.target = key,
		.accent = ComputeMatchedAccent(type, target),
	});
	return _derived.front().accent;
}

std::shared_ptr<WindowPalette> UseWindowPalette(
		not_null<Ui::RpWidget*> window) {
	const auto result = std::make_shared<WindowPalette>(window);
	Ui::SetWindowPalette(window, [=](bool child) {
		return result->resolve(child);
	});
	return result;
}

style::main_palette::Override WindowPaletteScope(
		not_null<const QWidget*> widget) {
	return style::main_palette::Override(Ui::WindowPaletteFor(widget));
}

rpl::producer<> WindowPaletteChanged(not_null<const QWidget*> widget) {
	return rpl::merge(
		style::PaletteChanged(),
		Recolored().events(
		) | rpl::filter([=](const style::palette *palette) {
			return (Ui::WindowPaletteFor(widget) == palette);
		}) | rpl::to_empty);
}

} // namespace Wallet
