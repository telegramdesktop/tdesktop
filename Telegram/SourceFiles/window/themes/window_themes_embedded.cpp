/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_themes_embedded.h"

#include "base/platform/base_platform_info.h"
#include "lang/lang_keys.h"
#include "storage/serialize_common.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_wall_paper.h"
#include "ui/chat/chat_theme.h"
#include "ui/style/style_palette_colorizer.h"
#include "window/themes/window_theme.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QPalette>

namespace Window {
namespace Theme {
namespace {

constexpr auto kMaxAccentColors = 3;
constexpr auto kDayBaseFile = ":/gui/day-custom-base.tdesktop-theme"_cs;
constexpr auto kNightBaseFile = ":/gui/night-custom-base.tdesktop-theme"_cs;

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
	qstr("historyPeer1UserpicBg2"),
	qstr("historyPeer2UserpicBg2"),
	qstr("historyPeer3UserpicBg2"),
	qstr("historyPeer4UserpicBg2"),
	qstr("historyPeer5UserpicBg2"),
	qstr("historyPeer6UserpicBg2"),
	qstr("historyPeer7UserpicBg2"),
	qstr("historyPeer8UserpicBg2"),
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

// Mirrors kMustBeContrast of the codegen style generator, in its order.
const auto kContrastPairs = std::array{
	std::pair(qstr("dialogsMenuIconFg"), qstr("dialogsBg")),
	std::pair(qstr("windowBoldFg"), qstr("windowBg")),
};

enum class SlotStatus {
	Initial,
	Created,
	Loaded,
};

struct SlotSource {
	SlotStatus status = SlotStatus::Initial;
	int leaf = -1;
};

struct SourceLeaf {
	QLatin1String rule;
	QColor raw;
	bool dependent = false;
};

struct PaletteDefault {
	QLatin1String name;
	QColor raw;
	int fallback = -1;
};

style::colorizer::Color cColor(std::string_view hex) {
	const auto q = style::ColorFromHex(hex);
	auto hue = int();
	auto saturation = int();
	auto value = int();
	q.getHsv(&hue, &saturation, &value);
	return style::colorizer::Color{ hue, saturation, value };
}

[[nodiscard]] EmbeddedScheme EmbeddedSchemeOf(EmbeddedType type) {
	const auto embedded = EmbeddedThemes();
	const auto i = ranges::find(embedded, type, &EmbeddedScheme::type);
	Assert(i != end(embedded));
	return *i;
}

[[nodiscard]] int HexDigit(char ch) {
	return (ch >= '0' && ch <= '9')
		? (ch - '0')
		: (ch >= 'a' && ch <= 'f')
		? (ch - 'a' + 10)
		: (ch >= 'A' && ch <= 'F')
		? (ch - 'A' + 10)
		: -1;
}

[[nodiscard]] bool IsHexValue(QLatin1String value) {
	return (value.size() == 7 || value.size() == 9)
		&& (value.data()[0] == '#');
}

[[nodiscard]] std::optional<QColor> ParseHexValue(QLatin1String value) {
	auto parts = std::array<int, 4>{ 0, 0, 0, 255 };
	for (auto i = 0; 2 * i + 1 < value.size(); ++i) {
		const auto high = HexDigit(value.data()[2 * i + 1]);
		const auto low = HexDigit(value.data()[2 * i + 2]);
		if (high < 0 || low < 0) {
			return std::nullopt;
		}
		parts[i] = high * 16 + low;
	}
	return QColor(parts[0], parts[1], parts[2], parts[3]);
}

[[nodiscard]] std::vector<PaletteDefault> PaletteDefaults() {
	using style::internal::GetPaletteIndex;

	auto result = std::vector<PaletteDefault>(style::palette::kCount);
	result[0].raw = QColor(255, 255, 255, 0);
	result[1].raw = QColor(255, 255, 255, 255);
	for (const auto &row : style::main_palette::data()) {
		const auto index = GetPaletteIndex(row.name);
		Assert(index > 1);
		auto &entry = result[index];
		entry.name = row.name;
		if (IsHexValue(row.value)) {
			const auto raw = ParseHexValue(row.value);
			Assert(raw.has_value());
			entry.raw = *raw;
			entry.fallback = row.fallback.isEmpty()
				? -1
				: GetPaletteIndex(row.fallback);
		} else {
			entry.fallback = GetPaletteIndex(row.value);
			Assert(entry.fallback > 1 && entry.fallback < index);
			entry.raw = result[entry.fallback].raw;
		}
	}
	return result;
}

[[nodiscard]] bool FollowsAccent(
		const style::colorizer &colorizer,
		QLatin1String rule,
		const QColor &raw) {
	if (colorizer.ignoreKeys.contains(rule)) {
		return false;
	} else if (colorizer.keepContrast.contains(rule)) {
		return true;
	}
	auto hue = 0;
	auto saturation = 0;
	auto value = 0;
	raw.getHsv(&hue, &saturation, &value);
	return std::abs(hue - colorizer.was.hue) < colorizer.hueThreshold;
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
	ApplyColorizerAccent(result, color);
	return result;
}

void ApplyColorizerAccent(style::colorizer &colorizer, const QColor &color) {
	color.getHsv(
		&colorizer.now.hue,
		&colorizer.now.saturation,
		&colorizer.now.value);
	const auto nowLightness = color.lightness();
	const auto limitedLightness = std::clamp(
		nowLightness,
		colorizer.lightnessMin,
		colorizer.lightnessMax);
	if (limitedLightness != nowLightness) {
		QColor::fromHsl(
			color.hslHue(),
			color.hslSaturation(),
			limitedLightness).getHsv(
				&colorizer.now.hue,
				&colorizer.now.saturation,
				&colorizer.now.value);
	}
}

std::optional<QColor> SystemAccentColor() {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	if (Platform::IsWindows() && Platform::IsWindows8OrGreater()) {
		return std::nullopt;
	}
#endif // Qt < 6.0.0
	const auto accent = QPalette().color(QPalette::Highlight);
	return accent.isValid() ? std::make_optional(accent) : std::nullopt;
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
	const auto &settings = Core::App().settings();
	if (settings.systemAccentColorEnabled()) {
		if (const auto accent = SystemAccentColor()) {
			return ColorizerFrom(*i, *accent);
		}
	}
	const auto &colors = settings.themesAccentColors();
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
	const auto name = [](auto key) {
		return rpl::deferred([=] { return key(); });
	};
	return {
		EmbeddedScheme{
			EmbeddedType::Default,
			qColor("9bd494"),
			qColor("eaffdc"),
			qColor("ffffff"),
			qColor("eaffdc"),
			qColor("ffffff"),
			name(tr::lng_settings_theme_classic),
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
			name(tr::lng_settings_theme_day),
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
			name(tr::lng_settings_theme_tinted),
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
			name(tr::lng_settings_theme_night),
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

Fn<void(style::palette&)> PreparePaletteCallback(
		bool dark,
		std::optional<QColor> accent) {
	return [=](style::palette &palette) {
		using namespace Theme;
		const auto &embedded = EmbeddedThemes();
		const auto i = ranges::find(
			embedded,
			dark ? EmbeddedType::Night : EmbeddedType::Default,
			&EmbeddedScheme::type);
		Assert(i != end(embedded));
		const auto colorizer = accent
			? ColorizerFrom(*i, *accent)
			: style::colorizer();

		auto instance = Instance();
		const auto loaded = LoadFromFile(
			(dark ? kNightBaseFile : kDayBaseFile).utf16(),
			&instance,
			nullptr,
			nullptr,
			colorizer);
		Assert(loaded);
		palette.finalize();
		palette = instance.palette;
	};
}

Fn<void(style::palette&)> PrepareCurrentPaletteCallback() {
	return [=, data = style::main_palette::save()](style::palette &palette) {
		palette.load(data);
	};
}

std::unique_ptr<style::palette> PrepareEmbeddedPalette(
		EmbeddedType type,
		std::optional<QColor> accent) {
	const auto embedded = EmbeddedThemes();
	const auto i = ranges::find(embedded, type, &EmbeddedScheme::type);
	Assert(i != end(embedded));
	const auto colorizer = accent
		? ColorizerFrom(*i, *accent)
		: style::colorizer();

	auto result = std::make_unique<style::palette>();
	if (i->path.isEmpty()) {
		result->reset(colorizer);
		return result;
	}
	auto instance = Instance();
	const auto loaded = LoadFromFile(
		i->path,
		&instance,
		nullptr,
		nullptr,
		colorizer);
	Assert(loaded);
	result->finalize();
	*result = instance.palette;
	return result;
}

EmbeddedAccentRecipe::EmbeddedAccentRecipe(EmbeddedType type)
: _scheme(EmbeddedSchemeOf(type)) {
	using style::internal::GetPaletteIndex;

	const auto colorizer = ColorizerFrom(_scheme, _scheme.accentColor);
	const auto defaults = PaletteDefaults();
	auto leaves = std::vector<SourceLeaf>();
	auto slots = std::vector<SlotSource>(style::palette::kCount);
	const auto add = [&](int index, QLatin1String rule, const QColor &raw) {
		slots[index].leaf = int(leaves.size());
		leaves.push_back({
			.rule = rule,
			.raw = raw,
			.dependent = (index > 0) && FollowsAccent(colorizer, rule, raw),
		});
	};
	if (!_scheme.path.isEmpty()) {
		const auto content = ReadPaletteContent(_scheme.path);
		Assert(content.has_value());
		auto unsupported = QMap<QLatin1String, QLatin1String>();
		const auto read = ReadPaletteValues(*content, [&](
				QLatin1String name,
				QLatin1String value) {
			value = unsupported.value(value, value);
			const auto index = GetPaletteIndex(name);
			if (IsHexValue(value)) {
				const auto raw = ParseHexValue(value);
				if (!raw) {
					return true;
				} else if (index < 0) {
					unsupported.insert(name, value);
				} else {
					slots[index].status = SlotStatus::Loaded;
					add(index, defaults[index].name, *raw);
				}
			} else if (index < 0) {
				unsupported.insert(name, value);
			} else {
				const auto from = GetPaletteIndex(value);
				if (from >= 0 && slots[from].status == SlotStatus::Loaded) {
					slots[index] = slots[from];
				}
			}
			return true;
		});
		Assert(read);
	}
	for (auto index = 0; index != style::palette::kCount; ++index) {
		auto &slot = slots[index];
		const auto &entry = defaults[index];
		if (slot.status != SlotStatus::Initial) {
			continue;
		} else if (entry.fallback >= 0
			&& slots[entry.fallback].status == SlotStatus::Loaded) {
			slot = slots[entry.fallback];
		} else {
			slot.status = (!index || colorizer.ignoreKeys.contains(entry.name))
				? SlotStatus::Created
				: SlotStatus::Loaded;
			add(index, entry.name, entry.raw);
		}
	}

	auto compact = std::vector<int>(leaves.size(), -1);
	const auto leafOf = [&](int index) {
		const auto leaf = slots[index].leaf;
		if (compact[leaf] < 0 && leaves[leaf].dependent) {
			compact[leaf] = int(_leaves.size());
			_leaves.push_back({ leaves[leaf].rule, leaves[leaf].raw });
		}
		return compact[leaf];
	};
	const auto operand = [&](int index) {
		const auto leaf = leafOf(index);
		return (leaf >= 0)
			? Operand{ .leaf = leaf }
			: Operand{ .fixed = leaves[slots[index].leaf].raw };
	};
	for (const auto &[over, under] : kContrastPairs) {
		const auto overIndex = GetPaletteIndex(over);
		const auto underIndex = GetPaletteIndex(under);
		Assert(overIndex > 1 && underIndex > 1);
		const auto pair = Contrast{
			.name = over,
			.index = overIndex,
			.over = operand(overIndex),
			.under = operand(underIndex),
		};
		if (pair.over.leaf >= 0 || pair.under.leaf >= 0) {
			_contrast.push_back(pair);
		}
	}
	for (auto index = 0; index != style::palette::kCount; ++index) {
		const auto leaf = leafOf(index);
		if (leaf < 0
			|| ranges::contains(_contrast, index, &Contrast::index)) {
			continue;
		}
		Assert(index > 1);
		_keys.push_back({
			.name = defaults[index].name,
			.index = index,
			.leaf = leaf,
		});
	}
}

bool EmbeddedAccentRecipe::apply(
		style::palette &palette,
		const QColor &accent) const {
	const auto colorizer = ColorizerFrom(_scheme, accent);
	auto values = std::vector<QColor>();
	values.reserve(_leaves.size());
	for (const auto &leaf : _leaves) {
		auto r = uchar(leaf.raw.red());
		auto g = uchar(leaf.raw.green());
		auto b = uchar(leaf.raw.blue());
		style::colorize(leaf.rule, r, g, b, colorizer);
		values.push_back(QColor(r, g, b, leaf.raw.alpha()));
	}
	const auto value = [&](const Operand &operand) {
		return (operand.leaf >= 0) ? values[operand.leaf] : operand.fixed;
	};
	auto written = false;
	const auto write = [&](QLatin1String name, int index, QColor color) {
		if (palette.colorAtIndex(index)->c.rgba() != color.rgba()) {
			palette.setColor(name, color);
			written = true;
		}
	};
	for (const auto &key : _keys) {
		write(key.name, key.index, values[key.leaf]);
	}
	for (const auto &pair : _contrast) {
		write(
			pair.name,
			pair.index,
			style::internal::EnsureContrast(
				value(pair.over),
				value(pair.under)));
	}
	return written;
}

const EmbeddedAccentRecipe &AccentRecipe(EmbeddedType type) {
	static auto cache = base::flat_map<
		EmbeddedType,
		std::unique_ptr<EmbeddedAccentRecipe>>();
	const auto i = cache.find(type);
	if (i != end(cache)) {
		return *i->second;
	}
	return *cache.emplace(
		type,
		std::make_unique<EmbeddedAccentRecipe>(type)).first->second;
}

Ui::ChatThemeBackground PrepareDefaultBackground(bool dark) {
	if (!dark) {
		const auto paper = Data::DefaultWallPaper();
		return Ui::PrepareBackgroundImage({
			.path = u":/gui/art/background.tgv"_q,
			.gzipSvg = true,
			.colors = paper.backgroundColors(),
			.isPattern = true,
			.patternOpacity = paper.patternOpacity(),
			.generateGradient = true,
			.gradientRotation = paper.gradientRotation(),
		});
	}
	auto instance = Instance();
	const auto loaded = LoadFromFile(
		kNightBaseFile.utf16(),
		&instance,
		nullptr,
		nullptr,
		style::colorizer());
	Assert(loaded);
	auto prepared = Ui::PreprocessBackgroundImage(
		std::move(instance.background));
	prepared.setDevicePixelRatio(style::DevicePixelRatio());
	return {
		.prepared = prepared,
		.preparedForTiled = Ui::PrepareImageForTiled(prepared),
		.colorForFill = Ui::CalculateImageMonoColor(prepared),
		.tile = instance.tiled,
	};
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
