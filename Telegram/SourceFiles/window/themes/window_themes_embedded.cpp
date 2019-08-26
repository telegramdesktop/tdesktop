/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_themes_embedded.h"

#include "window/themes/window_theme.h"
#include "storage/serialize_common.h"

namespace Window {
namespace Theme {
namespace {

constexpr auto kMaxAccentColors = 3;

const auto kColorizeIgnoredKeys = base::flat_set<QLatin1String>{ {
	qstr("boxTextFgGood"),
	qstr("boxTextFgError"),
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

QColor qColor(str_const hex) {
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

Colorizer::Color cColor(str_const hex) {
	const auto q = qColor(hex);
	auto hue = int();
	auto saturation = int();
	auto lightness = int();
	q.getHsl(&hue, &saturation, &lightness);
	return Colorizer::Color{ hue, saturation, lightness };
}

} // namespace

Colorizer ColorizerFrom(const EmbeddedScheme &scheme, const QColor &color) {
	using Color = Colorizer::Color;

	auto result = Colorizer();
	result.ignoreKeys = kColorizeIgnoredKeys;
	result.hueThreshold = 15;
	scheme.accentColor.getHsl(
		&result.was.hue,
		&result.was.saturation,
		&result.was.lightness);
	color.getHsl(
		&result.now.hue,
		&result.now.saturation,
		&result.now.lightness);
	switch (scheme.type) {
	case EmbeddedType::DayBlue:
		//result.keepContrast = base::flat_map<QLatin1String, Color>{ {
		//	{ qstr("test"), cColor("aaaaaa") },
		//} };
		break;
	}
	return result;
}

void Colorize(
		uchar &r,
		uchar &g,
		uchar &b,
		not_null<const Colorizer*> colorizer) {
	auto color = QColor(int(r), int(g), int(b));
	auto hue = 0;
	auto saturation = 0;
	auto lightness = 0;
	color.getHsl(&hue, &saturation, &lightness);
	const auto changeColor = std::abs(hue - colorizer->was.hue)
		<= colorizer->hueThreshold;
	if (!changeColor) {
		return;
	}
	const auto nowHue = hue + (colorizer->now.hue - colorizer->was.hue);
	const auto nowSaturation = ((saturation > colorizer->was.saturation)
		&& (colorizer->now.saturation > colorizer->was.saturation))
		? (((colorizer->now.saturation * (255 - colorizer->was.saturation))
			+ ((saturation - colorizer->was.saturation)
				* (255 - colorizer->now.saturation)))
			/ (255 - colorizer->was.saturation))
		: ((saturation != colorizer->was.saturation)
			&& (colorizer->was.saturation != 0))
		? ((saturation * colorizer->now.saturation)
			/ colorizer->was.saturation)
		: colorizer->now.saturation;
	const auto nowLightness = (lightness > colorizer->was.lightness)
		? (((colorizer->now.lightness * (255 - colorizer->was.lightness))
			+ ((lightness - colorizer->was.lightness)
				* (255 - colorizer->now.lightness)))
			/ (255 - colorizer->was.lightness))
		: (lightness < colorizer->was.lightness)
		? ((lightness * colorizer->now.lightness)
			/ colorizer->was.lightness)
		: colorizer->now.lightness;
	auto nowR = 0;
	auto nowG = 0;
	auto nowB = 0;
	QColor::fromHsl(
		((nowHue + 360) % 360),
		nowSaturation,
		nowLightness
	).getRgb(&nowR, &nowG, &nowB);
	r = uchar(nowR);
	g = uchar(nowG);
	b = uchar(nowB);
}

void Colorize(uint32 &pixel, not_null<const Colorizer*> colorizer) {
	const auto chars = reinterpret_cast<uchar*>(&pixel);
	Colorize(
		chars[2],
		chars[1],
		chars[0],
		colorizer);
}

void Colorize(QColor &color, not_null<const Colorizer*> colorizer) {
	auto r = uchar(color.red());
	auto g = uchar(color.green());
	auto b = uchar(color.blue());
	Colorize(r, g, b, colorizer);
	color = QColor(r, g, b, color.alpha());
}

void Colorize(QImage &image, not_null<const Colorizer*> colorizer) {
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

void Colorize(EmbeddedScheme &scheme, not_null<const Colorizer*> colorizer) {
	const auto colors = {
		&EmbeddedScheme::background,
		&EmbeddedScheme::sent,
		&EmbeddedScheme::received,
		&EmbeddedScheme::radiobuttonActive,
		&EmbeddedScheme::radiobuttonInactive
	};
	for (const auto color : colors) {
		Colorize(scheme.*color, colorizer);
	}
}

std::vector<EmbeddedScheme> EmbeddedThemes() {
	return {
		EmbeddedScheme{
			EmbeddedType::DayBlue,
			qColor("7ec4ea"),
			qColor("d7f0ff"),
			qColor("ffffff"),
			qColor("d7f0ff"),
			qColor("ffffff"),
			tr::lng_settings_theme_blue,
			":/gui/day-blue.tdesktop-theme",
			qColor("40a7e3")
		},
		EmbeddedScheme{
			EmbeddedType::Default,
			qColor("90ce89"),
			qColor("eaffdc"),
			qColor("ffffff"),
			qColor("eaffdc"),
			qColor("ffffff"),
			tr::lng_settings_theme_classic,
			QString()
		},
		EmbeddedScheme{
			EmbeddedType::Night,
			qColor("485761"),
			qColor("5ca7d4"),
			qColor("6b808d"),
			qColor("6b808d"),
			qColor("5ca7d4"),
			tr::lng_settings_theme_midnight,
			":/gui/night.tdesktop-theme",
			qColor("5288c1")
		},
		EmbeddedScheme{
			EmbeddedType::NightGreen,
			qColor("485761"),
			qColor("75bfb5"),
			qColor("6b808d"),
			qColor("6b808d"),
			qColor("75bfb5"),
			tr::lng_settings_theme_matrix,
			":/gui/night-green.tdesktop-theme",
			qColor("3fc1b0")
		},
	};
}

std::vector<QColor> DefaultAccentColors(EmbeddedType type) {
	switch (type) {
	case EmbeddedType::DayBlue:
		return {
			//qColor("3478f5"),
			qColor("58bfe8"),
			qColor("58b040"),
			qColor("da73a2"),
			qColor("e28830"),
			qColor("9073e7"),
			qColor("c14126"),
			qColor("71829c"),
			qColor("e3b63e"),
		};
	case EmbeddedType::Default:
		return {};
	case EmbeddedType::Night:
		return {
			//qColor("3478f5"),
			qColor("58bfe8"),
			qColor("58b040"),
			qColor("da73a2"),
			qColor("e28830"),
			qColor("9073e7"),
			qColor("c14126"),
			qColor("71829c"),
			qColor("e3b63e"),
		};
	case EmbeddedType::NightGreen:
		return {
			qColor("3478f5"),
			//qColor("58bfe8"),
			qColor("58b040"),
			qColor("da73a2"),
			qColor("e28830"),
			qColor("9073e7"),
			qColor("c14126"),
			qColor("71829c"),
			qColor("e3b63e"),
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
