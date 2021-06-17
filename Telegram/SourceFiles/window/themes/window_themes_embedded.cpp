/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_themes_embedded.h"

#include "window/themes/window_theme.h"
#include "storage/serialize_common.h"
#include "core/application.h"
#include "core/core_settings.h"

namespace Window {
namespace Theme {
namespace {

constexpr auto kMaxAccentColors = 3;
constexpr auto kEnoughLightnessForContrast = 64;

const auto kColorizeIgnoredKeys = base::flat_set<QLatin1String>{ {
	qstr("boxTextFgGood"),
	qstr("boxTextFgError"),
	qstr("callIconFg"),
	qstr("historyPeer1NameFg"),
	qstr("historyPeer1NameFgSelected"),
	qstr("historyPeer1UserpicBg"),
	qstr("historyPeer2NameFg"),
	qstr("historyPeer2NameFgSelected"),
	qstr("historyPeer2UserpicBg"),
	qstr("historyPeer3NameFg"),
	qstr("historyPeer3NameFgSelected"),
	qstr("historyPeer3UserpicBg"),
	qstr("historyPeer4NameFg"),
	qstr("historyPeer4NameFgSelected"),
	qstr("historyPeer4UserpicBg"),
	qstr("historyPeer5NameFg"),
	qstr("historyPeer5NameFgSelected"),
	qstr("historyPeer5UserpicBg"),
	qstr("historyPeer6NameFg"),
	qstr("historyPeer6NameFgSelected"),
	qstr("historyPeer6UserpicBg"),
	qstr("historyPeer7NameFg"),
	qstr("historyPeer7NameFgSelected"),
	qstr("historyPeer7UserpicBg"),
	qstr("historyPeer8NameFg"),
	qstr("historyPeer8NameFgSelected"),
	qstr("historyPeer8UserpicBg"),
	qstr("msgFile1Bg"),
	qstr("msgFile1BgDark"),
	qstr("msgFile1BgOver"),
	qstr("msgFile1BgSelected"),
	qstr("msgFile2Bg"),
	qstr("msgFile2BgDark"),
	qstr("msgFile2BgOver"),
	qstr("msgFile2BgSelected"),
	qstr("msgFile3Bg"),
	qstr("msgFile3BgDark"),
	qstr("msgFile3BgOver"),
	qstr("msgFile3BgSelected"),
	qstr("msgFile4Bg"),
	qstr("msgFile4BgDark"),
	qstr("msgFile4BgOver"),
	qstr("msgFile4BgSelected"),
	qstr("mediaviewFileRedCornerFg"),
	qstr("mediaviewFileYellowCornerFg"),
	qstr("mediaviewFileGreenCornerFg"),
	qstr("mediaviewFileBlueCornerFg"),
} };

QColor qColor(std::string_view hex) {
	Expects(hex.size() == 6);

	const auto component = [](char a, char b) {
		const auto convert = [](char ch) {
			Expects((ch >= '0' && ch <= '9')
				|| (ch >= 'A' && ch <= 'F')
				|| (ch >= 'a' && ch <= 'f'));

			return (ch >= '0' && ch <= '9')
				? int(ch - '0')
				: int(ch - ((ch >= 'A' && ch <= 'F') ? 'A' : 'a') + 10);
		};
		return convert(a) * 16 + convert(b);
	};

	return QColor(
		component(hex[0], hex[1]),
		component(hex[2], hex[3]),
		component(hex[4], hex[5]));
};

Colorizer::Color cColor(std::string_view hex) {
	const auto q = qColor(hex);
	auto hue = int();
	auto saturation = int();
	auto value = int();
	q.getHsv(&hue, &saturation, &value);
	return Colorizer::Color{ hue, saturation, value };
}

} // namespace

Colorizer ColorizerFrom(const EmbeddedScheme &scheme, const QColor &color) {
	using Color = Colorizer::Color;
	using Pair = std::pair<Color, Color>;

	auto result = Colorizer();
	result.ignoreKeys = kColorizeIgnoredKeys;
	result.hueThreshold = 15;
	scheme.accentColor.getHsv(
		&result.was.hue,
		&result.was.saturation,
		&result.was.value);
	color.getHsv(
		&result.now.hue,
		&result.now.saturation,
		&result.now.value);
	switch (scheme.type) {
	case EmbeddedType::DayBlue:
		result.lightnessMax = 160;
		break;
	case EmbeddedType::Night:
		result.keepContrast = base::flat_map<QLatin1String, Pair>{ {
			//{ qstr("windowFgActive"), Pair{ cColor("5288c1"), cColor("17212b") } }, // windowBgActive
			{ qstr("activeButtonFg"), Pair{ cColor("2f6ea5"), cColor("17212b") } }, // activeButtonBg
			{ qstr("profileVerifiedCheckFg"), Pair{ cColor("5288c1"), cColor("17212b") } }, // profileVerifiedCheckBg
			{ qstr("overviewCheckFgActive"), Pair{ cColor("5288c1"), cColor("17212b") } }, // overviewCheckBgActive
			{ qstr("historyFileInIconFg"), Pair{ cColor("3f96d0"), cColor("182533") } }, // msgFileInBg, msgInBg
			{ qstr("historyFileInIconFgSelected"), Pair{ cColor("6ab4f4"), cColor("2e70a5") } }, // msgFileInBgSelected, msgInBgSelected
			{ qstr("historyFileInRadialFg"), Pair{ cColor("3f96d0"), cColor("182533") } }, // msgFileInBg, msgInBg
			{ qstr("historyFileInRadialFgSelected"), Pair{ cColor("6ab4f4"), cColor("2e70a5") } }, // msgFileInBgSelected, msgInBgSelected
			{ qstr("historyFileOutIconFg"), Pair{ cColor("4c9ce2"), cColor("2b5278") } }, // msgFileOutBg, msgOutBg
			{ qstr("historyFileOutIconFgSelected"), Pair{ cColor("58abf3"), cColor("2e70a5") } }, // msgFileOutBgSelected, msgOutBgSelected
			{ qstr("historyFileOutRadialFg"), Pair{ cColor("4c9ce2"), cColor("2b5278") } }, // msgFileOutBg, msgOutBg
			{ qstr("historyFileOutRadialFgSelected"), Pair{ cColor("58abf3"), cColor("2e70a5") } }, // msgFileOutBgSelected, msgOutBgSelected
		} };
		result.lightnessMin = 64;
		break;
	case EmbeddedType::NightGreen:
		result.keepContrast = base::flat_map<QLatin1String, Pair>{ {
			//{ qstr("windowFgActive"), Pair{ cColor("3fc1b0"), cColor("282e33") } }, // windowBgActive, windowBg
			{ qstr("activeButtonFg"), Pair{ cColor("2da192"), cColor("282e33") } }, // activeButtonBg, windowBg
			{ qstr("profileVerifiedCheckFg"), Pair{ cColor("3fc1b0"), cColor("282e33") } }, // profileVerifiedCheckBg, windowBg
			{ qstr("overviewCheckFgActive"), Pair{ cColor("3fc1b0"), cColor("282e33") } }, // overviewCheckBgActive
			// callIconFg is used not only over callAnswerBg,
			// so this contrast-forcing breaks other buttons.
			//{ qstr("callIconFg"), Pair{ cColor("5ad1c1"), cColor("1b1f23") } }, // callAnswerBg, callBgOpaque
		} };
		result.lightnessMin = 64;
		break;
	}
	const auto nowLightness = color.lightness();
	const auto limitedLightness = std::clamp(
		nowLightness,
		result.lightnessMin,
		result.lightnessMax);
	if (limitedLightness != nowLightness) {
		QColor::fromHsl(
			color.hslHue(),
			color.hslSaturation(),
			limitedLightness).getHsv(
				&result.now.hue,
				&result.now.saturation,
				&result.now.value);
	}
	return result;
}

Colorizer ColorizerForTheme(const QString &absolutePath) {
	if (absolutePath.isEmpty() || !IsEmbeddedTheme(absolutePath)) {
		return Colorizer();
	}
	const auto schemes = EmbeddedThemes();
	const auto i = ranges::find(
		schemes,
		absolutePath,
		&EmbeddedScheme::path);
	if (i == end(schemes)) {
		return Colorizer();
	}
	const auto &colors = Core::App().settings().themesAccentColors();
	if (const auto accent = colors.get(i->type)) {
		return ColorizerFrom(*i, *accent);
	}
	return Colorizer();
}

[[nodiscard]] std::optional<Colorizer::Color> Colorize(
		const Colorizer::Color &color,
		const Colorizer &colorizer) {
	const auto changeColor = std::abs(color.hue - colorizer.was.hue)
		< colorizer.hueThreshold;
	if (!changeColor) {
		return std::nullopt;
	}
	const auto nowHue = color.hue + (colorizer.now.hue - colorizer.was.hue);
	const auto nowSaturation = ((color.saturation > colorizer.was.saturation)
		&& (colorizer.now.saturation > colorizer.was.saturation))
		? (((colorizer.now.saturation * (255 - colorizer.was.saturation))
			+ ((color.saturation - colorizer.was.saturation)
				* (255 - colorizer.now.saturation)))
			/ (255 - colorizer.was.saturation))
		: ((color.saturation != colorizer.was.saturation)
			&& (colorizer.was.saturation != 0))
		? ((color.saturation * colorizer.now.saturation)
			/ colorizer.was.saturation)
		: colorizer.now.saturation;
	const auto nowValue = (color.value > colorizer.was.value)
		? (((colorizer.now.value * (255 - colorizer.was.value))
			+ ((color.value - colorizer.was.value)
				* (255 - colorizer.now.value)))
			/ (255 - colorizer.was.value))
		: (color.value < colorizer.was.value)
		? ((color.value * colorizer.now.value)
			/ colorizer.was.value)
		: colorizer.now.value;
	return Colorizer::Color{
		((nowHue + 360) % 360),
		nowSaturation,
		nowValue
	};
}

[[nodiscard]] std::optional<QColor> Colorize(
		const QColor &color,
		const Colorizer &colorizer) {
	auto hue = 0;
	auto saturation = 0;
	auto lightness = 0;
	color.getHsv(&hue, &saturation, &lightness);
	const auto result = Colorize(
		Colorizer::Color{ hue, saturation, lightness },
		colorizer);
	if (!result) {
		return std::nullopt;
	}
	const auto &fields = *result;
	return QColor::fromHsv(fields.hue, fields.saturation, fields.value);
}

void FillColorizeResult(uchar &r, uchar &g, uchar &b, const QColor &color) {
	auto nowR = 0;
	auto nowG = 0;
	auto nowB = 0;
	color.getRgb(&nowR, &nowG, &nowB);
	r = uchar(nowR);
	g = uchar(nowG);
	b = uchar(nowB);
}

void Colorize(uchar &r, uchar &g, uchar &b, const Colorizer &colorizer) {
	const auto changed = Colorize(QColor(int(r), int(g), int(b)), colorizer);
	if (changed) {
		FillColorizeResult(r, g, b, *changed);
	}
}

void Colorize(
		QLatin1String name,
		uchar &r,
		uchar &g,
		uchar &b,
		const Colorizer &colorizer) {
	if (colorizer.ignoreKeys.contains(name)) {
		return;
	}

	const auto i = colorizer.keepContrast.find(name);
	if (i == end(colorizer.keepContrast)) {
		Colorize(r, g, b, colorizer);
		return;
	}
	const auto check = i->second.first;
	const auto rgb = QColor(int(r), int(g), int(b));
	const auto changed = Colorize(rgb, colorizer);
	const auto checked = Colorize(check, colorizer).value_or(check);
	const auto lightness = [](QColor hsv) {
		return hsv.value() - (hsv.value() * hsv.saturation()) / 511;
	};
	const auto changedLightness = lightness(changed.value_or(rgb).toHsv());
	const auto checkedLightness = lightness(
		QColor::fromHsv(checked.hue, checked.saturation, checked.value));
	const auto delta = std::abs(changedLightness - checkedLightness);
	if (delta >= kEnoughLightnessForContrast) {
		if (changed) {
			FillColorizeResult(r, g, b, *changed);
		}
		return;
	}
	const auto replace = i->second.second;
	const auto result = Colorize(replace, colorizer).value_or(replace);
	FillColorizeResult(
		r,
		g,
		b,
		QColor::fromHsv(result.hue, result.saturation, result.value));
}

void Colorize(uint32 &pixel, const Colorizer &colorizer) {
	const auto chars = reinterpret_cast<uchar*>(&pixel);
	Colorize(
		chars[2],
		chars[1],
		chars[0],
		colorizer);
}

void Colorize(QImage &image, const Colorizer &colorizer) {
	image = std::move(image).convertToFormat(QImage::Format_ARGB32);
	const auto bytes = image.bits();
	const auto bytesPerLine = image.bytesPerLine();
	for (auto line = 0; line != image.height(); ++line) {
		const auto ints = reinterpret_cast<uint32*>(
			bytes + line * bytesPerLine);
		const auto end = ints + image.width();
		for (auto p = ints; p != end; ++p) {
			Colorize(*p, colorizer);
		}
	}
}

void Colorize(EmbeddedScheme &scheme, const Colorizer &colorizer) {
	const auto colors = {
		&EmbeddedScheme::background,
		&EmbeddedScheme::sent,
		&EmbeddedScheme::received,
		&EmbeddedScheme::radiobuttonActive,
		&EmbeddedScheme::radiobuttonInactive
	};
	for (const auto color : colors) {
		if (const auto changed = Colorize(scheme.*color, colorizer)) {
			scheme.*color = changed->toRgb();
		}
	}
}

QByteArray Colorize(
		QLatin1String hexColor,
		const Colorizer &colorizer) {
	Expects(hexColor.size() == 7 || hexColor.size() == 9);

	auto color = qColor(std::string_view(hexColor.data() + 1, 6));
	const auto changed = Colorize(color, colorizer).value_or(color).toRgb();

	auto result = QByteArray();
	result.reserve(hexColor.size());
	result.append(hexColor.data()[0]);
	const auto addHex = [&](int code) {
		if (code >= 0 && code < 10) {
			result.append('0' + code);
		} else if (code >= 10 && code < 16) {
			result.append('a' + (code - 10));
		}
	};
	const auto addValue = [&](int code) {
		addHex(code / 16);
		addHex(code % 16);
	};
	addValue(changed.red());
	addValue(changed.green());
	addValue(changed.blue());
	if (hexColor.size() == 9) {
		result.append(hexColor.data()[7]);
		result.append(hexColor.data()[8]);
	}
	return result;
}

std::vector<EmbeddedScheme> EmbeddedThemes() {
	return {
		EmbeddedScheme{
			EmbeddedType::Default,
			qColor("9bd494"),
			qColor("eaffdc"),
			qColor("ffffff"),
			qColor("eaffdc"),
			qColor("ffffff"),
			tr::lng_settings_theme_classic,
			QString()
		},
		EmbeddedScheme{
			EmbeddedType::DayBlue,
			qColor("7ec4ea"),
			qColor("d7f0ff"),
			qColor("ffffff"),
			qColor("d7f0ff"),
			qColor("ffffff"),
			tr::lng_settings_theme_day,
			":/gui/day-blue.tdesktop-theme",
			qColor("40a7e3")
		},
		EmbeddedScheme{
			EmbeddedType::Night,
			qColor("485761"),
			qColor("5ca7d4"),
			qColor("6b808d"),
			qColor("6b808d"),
			qColor("5ca7d4"),
			tr::lng_settings_theme_tinted,
			":/gui/night.tdesktop-theme",
			qColor("5288c1")
		},
		EmbeddedScheme{
			EmbeddedType::NightGreen,
			qColor("485761"),
			qColor("6b808d"),
			qColor("6b808d"),
			qColor("6b808d"),
			qColor("75bfb5"),
			tr::lng_settings_theme_night,
			":/gui/night-green.tdesktop-theme",
			qColor("3fc1b0")
		},
	};
}

std::vector<QColor> DefaultAccentColors(EmbeddedType type) {
	switch (type) {
	case EmbeddedType::DayBlue:
		return {
			qColor("45bce7"),
			qColor("52b440"),
			qColor("d46c99"),
			qColor("df8a49"),
			qColor("9978c8"),
			qColor("c55245"),
			qColor("687b98"),
			qColor("dea922"),
		};
	case EmbeddedType::Default:
		return {};
	case EmbeddedType::Night:
		return {
			qColor("58bfe8"),
			qColor("466f42"),
			qColor("aa6084"),
			qColor("a46d3c"),
			qColor("917bbd"),
			qColor("ab5149"),
			qColor("697b97"),
			qColor("9b834b"),
		};
	case EmbeddedType::NightGreen:
		return {
			qColor("60a8e7"),
			qColor("4e9c57"),
			qColor("ca7896"),
			qColor("cc925c"),
			qColor("a58ed2"),
			qColor("d27570"),
			qColor("7b8799"),
			qColor("cbac67"),
		};
	}
	Unexpected("Type in Window::Theme::AccentColors.");
}

QByteArray AccentColors::serialize() const {
	auto result = QByteArray();
	if (_data.empty()) {
		return result;
	}

	const auto count = _data.size();
	auto size = sizeof(qint32) * (count + 1)
		+ Serialize::colorSize() * count;
	result.reserve(size);

	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << qint32(_data.size());
	for (const auto &[type, color] : _data) {
		stream << static_cast<qint32>(type);
		Serialize::writeColor(stream, color);
	}
	stream.device()->close();

	return result;
}

bool AccentColors::setFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		_data.clear();
		return true;
	}
	auto copy = QByteArray(serialized);
	auto stream = QDataStream(&copy, QIODevice::ReadOnly);
	stream.setVersion(QDataStream::Qt_5_1);

	auto count = qint32();
	stream >> count;
	if (stream.status() != QDataStream::Ok) {
		return false;
	} else if (count <= 0 || count > kMaxAccentColors) {
		return false;
	}
	auto data = base::flat_map<EmbeddedType, QColor>();
	for (auto i = 0; i != count; ++i) {
		auto type = qint32();
		stream >> type;
		const auto color = Serialize::readColor(stream);
		const auto uncheckedType = static_cast<EmbeddedType>(type);
		switch (uncheckedType) {
		case EmbeddedType::DayBlue:
		case EmbeddedType::Night:
		case EmbeddedType::NightGreen:
			data.emplace(uncheckedType, color);
			break;
		default:
			return false;
		}
	}
	if (stream.status() != QDataStream::Ok) {
		return false;
	}
	_data = std::move(data);
	return true;
}

void AccentColors::set(EmbeddedType type, const QColor &value) {
	_data.emplace_or_assign(type, value);
}

void AccentColors::clear(EmbeddedType type) {
	_data.remove(type);
}

std::optional<QColor> AccentColors::get(EmbeddedType type) const {
	const auto i = _data.find(type);
	return (i != end(_data)) ? std::make_optional(i->second) : std::nullopt;
}

} // namespace Theme
} // namespace Window
