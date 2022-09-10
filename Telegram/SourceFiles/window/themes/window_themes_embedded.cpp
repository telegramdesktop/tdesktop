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
#include "ui/style/style_palette_colorizer.h"

namespace Window {
namespace Theme {
namespace {

constexpr auto kMaxAccentColors = 3;

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
	qstr("settingsIconBg1"),
	qstr("settingsIconBg2"),
	qstr("settingsIconBg3"),
	qstr("settingsIconBg4"),
	qstr("settingsIconBg5"),
	qstr("settingsIconBg6"),
	qstr("settingsIconBg8"),
	qstr("settingsIconBgArchive"),
	qstr("premiumButtonBg1"),
	qstr("premiumButtonBg2"),
	qstr("premiumButtonBg3"),
	qstr("premiumIconBg1"),
	qstr("premiumIconBg2"),
} };

style::colorizer::Color cColor(std::string_view hex) {
	const auto q = style::ColorFromHex(hex);
	auto hue = int();
	auto saturation = int();
	auto value = int();
	q.getHsv(&hue, &saturation, &value);
	return style::colorizer::Color{ hue, saturation, value };
}

} // namespace

style::colorizer ColorizerFrom(
		const EmbeddedScheme &scheme,
		const QColor &color) {
	using Color = style::colorizer::Color;
	using Pair = std::pair<Color, Color>;

	auto result = style::colorizer();
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
	case EmbeddedType::Default:
		result.lightnessMax = 160;
		break;
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

style::colorizer ColorizerForTheme(const QString &absolutePath) {
	if (!IsEmbeddedTheme(absolutePath)) {
		return {};
	}
	const auto schemes = EmbeddedThemes();
	const auto i = ranges::find(
		schemes,
		absolutePath,
		&EmbeddedScheme::path);
	if (i == end(schemes)) {
		return {};
	}
	const auto &colors = Core::App().settings().themesAccentColors();
	if (const auto accent = colors.get(i->type)) {
		return ColorizerFrom(*i, *accent);
	}
	return {};
}

void Colorize(EmbeddedScheme &scheme, const style::colorizer &colorizer) {
	const auto colors = {
		&EmbeddedScheme::background,
		&EmbeddedScheme::sent,
		&EmbeddedScheme::received,
		&EmbeddedScheme::radiobuttonActive,
		&EmbeddedScheme::radiobuttonInactive
	};
	for (const auto color : colors) {
		if (const auto changed = style::colorize(scheme.*color, colorizer)) {
			scheme.*color = changed->toRgb();
		}
	}
}

std::vector<EmbeddedScheme> EmbeddedThemes() {
	const auto qColor = [](auto hex) {
		return style::ColorFromHex(hex);
	};
	return {
		EmbeddedScheme{
			EmbeddedType::Default,
			qColor("9bd494"),
			qColor("eaffdc"),
			qColor("ffffff"),
			qColor("eaffdc"),
			qColor("ffffff"),
			tr::lng_settings_theme_classic,
			QString(),
			qColor("40a7e3")
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
	const auto qColor = [](auto hex) {
		return style::ColorFromHex(hex);
	};
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
		case EmbeddedType::Default:
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
