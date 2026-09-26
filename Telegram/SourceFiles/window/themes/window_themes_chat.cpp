/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_themes_chat.h"

#include "base/crc32hash.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_document_resolver.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "ui/chat/chat_theme.h"
#include "ui/style/style_palette_colorizer.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "window/themes/window_theme_preview.h"
#include "window/themes/window_themes_embedded.h"
#include "window/window_session_controller.h"

namespace Window::Theme {
namespace {

[[nodiscard]] float64 PerceivedBrightness(const QColor &color) {
	return (0.299 * color.red()
		+ 0.587 * color.green()
		+ 0.114 * color.blue()) / 255.;
}

[[nodiscard]] QColor ScaleBrightness(const QColor &color, float64 amount) {
	const auto scale = [&](int value) {
		return std::clamp(int(base::SafeRound(value * amount)), 0, 255);
	};
	return QColor(
		scale(color.red()),
		scale(color.green()),
		scale(color.blue()),
		color.alpha());
}

[[nodiscard]] QColor FromHsvRounded(
		float64 hue,
		float64 saturation,
		float64 value,
		float64 alpha) {
	const auto sector = hue * 6.;
	const auto i = int(std::floor(sector)) % 6;
	const auto f = sector - std::floor(sector);
	const auto p = value * (1. - saturation);
	const auto q = value * (1. - f * saturation);
	const auto t = value * (1. - (1. - f) * saturation);
	auto red = 0., green = 0., blue = 0.;
	switch (i) {
	case 0: red = value; green = t; blue = p; break;
	case 1: red = q; green = value; blue = p; break;
	case 2: red = p; green = value; blue = t; break;
	case 3: red = p; green = q; blue = value; break;
	case 4: red = t; green = p; blue = value; break;
	case 5: red = value; green = p; blue = q; break;
	}
	const auto channel = [](float64 value) {
		return std::clamp(int(base::SafeRound(value * 255.)), 0, 255);
	};
	return QColor(
		channel(red),
		channel(green),
		channel(blue),
		channel(alpha));
}

struct HsvColor {
	float64 hue = 0.;
	float64 saturation = 0.;
	float64 value = 0.;
};

[[nodiscard]] HsvColor ToHsv(const QColor &color) {
	const auto hsv = color.toHsv();
	return {
		float64(hsv.hueF()),
		float64(hsv.saturationF()),
		float64(hsv.valueF()),
	};
}

[[nodiscard]] QColor DarkChromeShift(QColor color, const QColor &accent) {
	constexpr auto kHueGate = 30. / 360.;
	const auto base = ToHsv(QColor(0x3E, 0x88, 0xF7));
	const auto now = ToHsv(accent);
	const auto own = ToHsv(color);
	if (own.saturation <= 0.001 || own.hue < 0. || now.hue < 0.) {
		return color;
	}
	const auto difference = std::min(
		std::abs(own.hue - base.hue),
		std::abs(own.hue - base.hue - 1.));
	if (difference > kHueGate) {
		return color;
	}
	const auto distance = std::min(
		1.5 * own.saturation / base.saturation,
		1.);
	auto shiftedHue = own.hue + now.hue - base.hue;
	shiftedHue -= std::floor(shiftedHue);
	const auto shifted = FromHsvRounded(
		shiftedHue,
		std::clamp(
			own.saturation * now.saturation / base.saturation,
			0.,
			1.),
		std::clamp(
			own.value
				* (1. - distance + distance * now.value / base.value),
			0.,
			1.),
		color.alphaF());
	const auto wasBrightness = PerceivedBrightness(color);
	const auto nowBrightness = PerceivedBrightness(shifted);
	if (wasBrightness > nowBrightness && nowBrightness > 0.) {
		const auto amount = 0.4 * wasBrightness / nowBrightness + 0.6;
		return ScaleBrightness(shifted, amount);
	}
	return shifted;
}

[[nodiscard]] QColor NeutralizeNightSurface(const QColor &color) {
	const auto own = ToHsv(color);
	if (own.saturation > 0.55 || own.value > 0.35) {
		return color;
	}
	return QColor::fromHsvF(
		240. / 360.,
		own.saturation * 0.086,
		own.value * (25. / 43.),
		color.alphaF());
}

void AdjustDarkChromeDepth(
		style::palette &palette,
		const QColor &accent) {
	static const auto night = [] {
		auto result = std::make_unique<style::palette>();
		PreparePaletteCallback(true, std::nullopt)(*result);
		return result;
	}();
	const auto &embedded = EmbeddedThemes();
	const auto i = ranges::find(
		embedded,
		EmbeddedType::Night,
		&EmbeddedScheme::type);
	Assert(i != end(embedded));
	const auto ignored = ColorizerFrom(*i, accent).ignoreKeys;
	const auto rows = style::main_palette::data();
	for (const auto &row : rows) {
		const auto name = row.name;
		if (name.startsWith(QLatin1String("msg"))
			|| name.startsWith(QLatin1String("history"))
			|| ignored.contains(name)) {
			continue;
		}
		const auto index = style::internal::GetPaletteIndex(name);
		Assert(index >= 0);
		const auto original = night->colorAtIndex(index)->c;
		const auto shifted = DarkChromeShift(
			NeutralizeNightSurface(original),
			accent);
		if (shifted != original) {
			palette.setColor(name, shifted);
		}
	}
}

[[nodiscard]] QByteArray GeneratePaletteContent(
		const style::palette &palette) {
	auto result = QByteArray();
	const auto rows = style::main_palette::data();
	result.reserve(rows.size() * 32);
	for (const auto &row : rows) {
		const auto index = style::internal::GetPaletteIndex(row.name);
		Assert(index >= 0);
		result.append(row.name.data(), row.name.size());
		result.append(": ");
		result.append(ColorHexString(palette.colorAtIndex(index)->c));
		result.append(";\n");
	}
	return result;
}

struct WallPaperLoad {
	std::shared_ptr<Data::DocumentMedia> media;
	base::binary_guard generating;
	rpl::lifetime lifetime;
};

[[nodiscard]] std::unique_ptr<WallPaperLoad> &LoadingWallPaper() {
	static auto result = std::unique_ptr<WallPaperLoad>();
	return result;
}

void ApplyChatThemeWallPaper(
		not_null<SessionController*> controller,
		const Data::WallPaper &paper) {
	LoadingWallPaper() = nullptr;
	const auto document = paper.document();
	if (!document) {
		Background()->set(paper);
		return;
	}
	auto load = std::make_unique<WallPaperLoad>();
	load->media = document->createMediaView();
	document->save(paper.fileOrigin(), QString());
	const auto raw = load.get();
	const auto weak = base::make_weak(controller);
	const auto finish = [=] {
		if (LoadingWallPaper().get() != raw || !weak) {
			return;
		}
		raw->generating = Data::ReadBackgroundImageAsync(
			raw->media.get(),
			Ui::PreprocessBackgroundImage,
			[=](QImage &&image) {
				if (LoadingWallPaper().get() == raw) {
					Background()->set(paper, std::move(image));
					LoadingWallPaper() = nullptr;
				}
			});
	};
	if (load->media->loaded(true)) {
		LoadingWallPaper() = std::move(load);
		finish();
	} else {
		controller->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return raw->media->loaded(true);
		}) | rpl::take(1) | rpl::on_next(finish, raw->lifetime);
		LoadingWallPaper() = std::move(load);
	}
}

} // namespace

std::optional<Data::CloudThemeType> ChatThemeVariant(
		const Data::CloudTheme &theme,
		bool dark) {
	const auto type = dark
		? Data::CloudThemeType::Dark
		: Data::CloudThemeType::Light;
	const auto fallback = dark
		? Data::CloudThemeType::Light
		: Data::CloudThemeType::Dark;
	return theme.settings.contains(type)
		? std::make_optional(type)
		: theme.settings.contains(fallback)
		? std::make_optional(fallback)
		: std::nullopt;
}

Ui::ChatThemeBubblesData PrepareBubblesData(
		const Data::CloudTheme &theme,
		Data::CloudThemeType type) {
	const auto i = theme.settings.find(type);
	return {
		.colors = (i != end(theme.settings)
			? i->second.outgoingMessagesColors
			: std::vector<QColor>()),
		.accent = (i != end(theme.settings)
			? i->second.outgoingAccentColor
			: std::optional<QColor>()),
	};
}

std::unique_ptr<Preview> PreviewFromChatTheme(
		const Data::CloudTheme &theme,
		bool dark) {
	const auto used = ChatThemeVariant(theme, dark);
	if (!used) {
		return nullptr;
	}
	const auto &settings = theme.settings.find(*used)->second;
	auto descriptor = Ui::ChatThemeDescriptor{
		.key = { theme.id, dark },
		.preparePalette = PreparePaletteCallback(
			dark,
			settings.accentColor),
		.bubblesData = PrepareBubblesData(theme, *used),
		.basedOnDark = dark,
	};
	auto result = std::make_unique<Preview>();
	result->object.cloud = theme;
	result->object.pathRelative
		= result->object.pathAbsolute
		= CachedThemePath(theme.id);
	{
		const auto built = std::make_unique<Ui::ChatTheme>(
			std::move(descriptor));
		result->instance.palette.finalize();
		result->instance.palette = *built->palette();
	}
	if (dark) {
		AdjustDarkChromeDepth(
			result->instance.palette,
			settings.accentColor);
	}
	result->object.content = GeneratePaletteContent(
		result->instance.palette);
	auto &cache = result->instance.cached;
	cache.colors = result->instance.palette.save();
	cache.paletteChecksum = style::palette::Checksum();
	cache.contentChecksum = base::crc32(
		result->object.content.constData(),
		result->object.content.size());
	return result;
}

bool ChatThemeOwnsPaper(const Data::CloudTheme &theme) {
	const auto &current = Background()->paper();
	return Data::IsDefaultWallPaper(current)
		|| Data::IsThemeWallPaper(current)
		|| ranges::any_of(theme.settings, [&](const auto &entry) {
			return entry.second.paper
				&& (entry.second.paper->key() == current.key());
		});
}

void ApplyChatTheme(
		not_null<SessionController*> controller,
		const Data::CloudTheme &theme,
		bool dark,
		bool replacePaper) {
	const auto used = ChatThemeVariant(theme, dark);
	if (!used) {
		return;
	}
	const auto paper = theme.settings.find(*used)->second.paper;
	const auto weak = base::make_weak(controller);
	crl::async([=] {
		auto result = PreviewFromChatTheme(theme, dark);
		if (!result) {
			return;
		}
		crl::on_main(weak, [=, result = std::move(result)]() mutable {
			Apply(std::move(result));
			KeepApplied();
			if (paper && replacePaper) {
				ApplyChatThemeWallPaper(controller, *paper);
			}
		});
	});
}

void CheckChatThemeWallPaper(not_null<SessionController*> controller) {
	if (!controller->widget()->sessionContent()) {
		return;
	}
	const auto background = Background();
	auto cloud = background->themeObject().cloud;
	if (cloud.settings.empty()) {
		return;
	}
	auto used = ChatThemeVariant(cloud, IsNightMode());
	if (!used) {
		return;
	}
	auto paper = cloud.settings.find(*used)->second.paper;
	if (!paper) {
		return;
	}
	if (!paper->document() && paper->isPattern()) {
		const auto &themes = controller->session().data().cloudThemes();
		const auto fresh = themes.themeForToken(cloud.emoticon);
		if (fresh) {
			auto updated = background->themeObject();
			updated.cloud = *fresh;
			background->setThemeObject(updated);
			cloud = *fresh;
			used = ChatThemeVariant(cloud, IsNightMode());
			if (!used) {
				return;
			}
			const auto &live = cloud.settings.find(*used)->second.paper;
			if (live) {
				paper = live;
			}
		}
	}
	const auto &current = background->paper();
	const auto degraded = current.isPattern()
		&& !current.document()
		&& background->prepared().isNull()
		&& (paper->document() != nullptr);
	if (!degraded && current.key() == paper->key()) {
		return;
	}
	if (ChatThemeOwnsPaper(cloud)) {
		ApplyChatThemeWallPaper(controller, *paper);
	}
}

} // namespace Window::Theme
