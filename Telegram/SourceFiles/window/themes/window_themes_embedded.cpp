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

QColor Color(str_const hex) {
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

} // namespace

Colorizer ColorizerFrom(const EmbeddedScheme &scheme, const QColor &color) {
	auto result = Colorizer();
	result.ignoreKeys = kColorizeIgnoredKeys;
	result.hueThreshold = 10;
	scheme.accentColor.getHsv(
		&result.wasHue,
		&result.wasSaturation,
		&result.wasValue);
	color.getHsv(
		&result.nowHue,
		&result.nowSaturation,
		&result.nowValue);
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
	auto value = 0;
	color.getHsv(&hue, &saturation, &value);
	const auto changeColor = std::abs(hue - colorizer->wasHue)
		<= colorizer->hueThreshold;
	const auto nowHue = hue + (colorizer->nowHue - colorizer->wasHue);
	const auto nowSaturation = ((saturation > colorizer->wasSaturation)
		&& (colorizer->nowSaturation > colorizer->wasSaturation))
		? (((colorizer->nowSaturation * (255 - colorizer->wasSaturation))
			+ ((saturation - colorizer->wasSaturation)
				* (255 - colorizer->nowSaturation)))
			/ (255 - colorizer->wasSaturation))
		: ((saturation != colorizer->wasSaturation)
			&& (colorizer->wasSaturation != 0))
		? ((saturation * colorizer->nowSaturation)
			/ colorizer->wasSaturation)
		: colorizer->nowSaturation;
	const auto nowValue = (value > colorizer->wasValue)
		? (((colorizer->nowValue * (255 - colorizer->wasValue))
			+ ((value - colorizer->wasValue)
				* (255 - colorizer->nowValue)))
			/ (255 - colorizer->wasValue))
		: (value < colorizer->wasValue)
		? ((value * colorizer->nowValue)
			/ colorizer->wasValue)
		: colorizer->nowValue;
	auto nowR = 0;
	auto nowG = 0;
	auto nowB = 0;
	QColor::fromHsv(
		changeColor ? ((nowHue + 360) % 360) : hue,
		changeColor ? nowSaturation : saturation,
		nowValue
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
			Color("7ec4ea"),
			Color("d7f0ff"),
			Color("ffffff"),
			Color("d7f0ff"),
			Color("ffffff"),
			tr::lng_settings_theme_blue,
			":/gui/day-blue.tdesktop-theme",
			Color("40a7e3")
		},
		EmbeddedScheme{
			EmbeddedType::Default,
			Color("90ce89"),
			Color("eaffdc"),
			Color("ffffff"),
			Color("eaffdc"),
			Color("ffffff"),
			tr::lng_settings_theme_classic,
			QString()
		},
		EmbeddedScheme{
			EmbeddedType::Night,
			Color("485761"),
			Color("5ca7d4"),
			Color("6b808d"),
			Color("6b808d"),
			Color("5ca7d4"),
			tr::lng_settings_theme_midnight,
			":/gui/night.tdesktop-theme",
			Color("5288c1")
		},
		EmbeddedScheme{
			EmbeddedType::NightGreen,
			Color("485761"),
			Color("75bfab"),
			Color("6b808d"),
			Color("6b808d"),
			Color("75bfab"),
			tr::lng_settings_theme_matrix,
			":/gui/night-green.tdesktop-theme",
			Color("3fc1b0")
		},
	};
}

std::vector<QColor> AccentColors(EmbeddedType type) {
	switch (type) {
	case EmbeddedType::DayBlue:
		return {
			//Color("3478f5"),
			Color("58bfe8"),
			Color("58b040"),
			Color("da73a2"),
			Color("e28830"),
			Color("9073e7"),
			Color("9073e7"),
			Color("e3b63e"),
			Color("71829c")
		};
	case EmbeddedType::Default:
		return {};
	case EmbeddedType::Night:
		return {
			//Color("3478f5"),
			Color("58bfe8"),
			Color("58b040"),
			Color("da73a2"),
			Color("e28830"),
			Color("9073e7"),
			Color("9073e7"),
			Color("e3b63e"),
			Color("71829c")
		};
	case EmbeddedType::NightGreen:
		return {
			Color("3478f5"),
			//Color("58bfe8"),
			Color("58b040"),
			Color("da73a2"),
			Color("e28830"),
			Color("9073e7"),
			Color("9073e7"),
			Color("e3b63e"),
			Color("71829c")
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
