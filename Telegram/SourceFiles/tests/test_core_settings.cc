/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_settings.h"

#include "base/platform/base_platform_info.h"
#include "chat_helpers/spellchecker_locale_ids.h"
#include "spellcheck/spellcheck_types.h"
#include "tests/test_named_checks.hpp"
#include "window/themes/window_themes_embedded.h"

#include <initializer_list>

namespace {

using Test::NamedChecks::Check;
using Test::NamedChecks::FirstFailure;

static_assert(
	Platform::IsWindows(),
	"The default literal holds the Windows writer's stored values.");

constexpr auto kWriterVersion = 7004005;
constexpr auto kWriterVersion2001013 = 2001013;
constexpr auto kWriterVersion7003003 = 7003003;

#ifdef Q_PROCESSOR_ARM
constexpr auto kOldWriterConvertsDictionaries = false;
#else // Q_PROCESSOR_ARM
constexpr auto kOldWriterConvertsDictionaries = true;
#endif // Q_PROCESSOR_ARM

constexpr auto kAccentColors = 0;
constexpr auto kSongVolume = 12;
constexpr auto kAskDownloadPath = 20;
constexpr auto kDownloadPath = 24;
constexpr auto kDownloadPathBookmark = 28;
constexpr auto kLegacyVoiceSpeed = 32;
constexpr auto kNotifyView = 48;
constexpr auto kNotificationsCount = 56;
constexpr auto kLegacyCallPlaybackDevice = 68;
constexpr auto kLegacyCallCaptureDevice = 72;
constexpr auto kCallOutputVolume = 76;
constexpr auto kCallInputVolume = 80;
constexpr auto kSoundOverrides = 92;
constexpr auto kDictionaries = 152;
constexpr auto kTabbedSelector = 164;
constexpr auto kThirdSectionInfo = 176;
constexpr auto kWithChatRatio = 180;
constexpr auto kNotifyFromAll = 192;
constexpr auto kNativeWindowFrame = 196;
constexpr auto kWindowPosition = 236;
constexpr auto kProxyTryIPv6 = 292;
constexpr auto kProxyUseForCalls = 296;
constexpr auto kProxyListCount = 328;
constexpr auto kVoiceSpeed = 364;
constexpr auto kHardwareAcceleratedVideo = 400;
constexpr auto kSkipLanguages = 416;
constexpr auto kTranslateTo = 428;
constexpr auto kTrayIcon = 500;
constexpr auto kCallPlaybackDevice = 516;
constexpr auto kCallCaptureDevice = 520;
constexpr auto kIvPosition = 524;
constexpr auto kNoWarningExtensions = 556;
constexpr auto kNoChatRatio = 564;
constexpr auto kVideoQuality = 596;
constexpr auto kSystemDarkMode = 604;
constexpr auto kQuickDialogAction = 608;
constexpr auto kPrefs = 666;
constexpr auto kAudioSpeed = 670;
constexpr auto kGridZoom = 674;
constexpr auto kPullToNextChannel = 678;
constexpr auto kTabsMode = 682;
constexpr auto kLanguageTail = 686;

constexpr auto kDefault7004005 = ""
	"ffffffff0000000100000000000dbba0000dbba000000000ffffffffffffffff"
	"0000000100000001000000010000000100000000000000000000000300000002"
	"00000e10ffffffffffffffff0000006400000064000000010000000000000000"
	"0000000000000000000000010000000100000001000000010000000100000001"
	"00000001000000010000000100000001ffffff56ffffffff0000000000000001"
	"00000001000000000000000100000004000000010005731700000000ffffffff"
	"000000010000000000000000ffffffff0000000100000000ffffffff00000000"
	"0000001400000000000000000000001c00000000000000000000000000000000"
	"0000000000000000000000000000000000000000000000000000000000000000"
	"000000380000000000000000000000000000001400000000ffffffff00000000"
	"ffffffffffffffff0000000000000000000000000000000a0000000000000000"
	"00000000ffffffff00000000ffffff5600000000ffffffff0000000000000000"
	"0000000100000000000000000000000000000000000000010000000100000000"
	"0000000000000000000000010000000000000000000000000000000000000000"
	"0000001c00000000000000000000000000000000000000000000000200000000"
	"00000000000000000000000000000000000000000000000100000000ffffffff"
	"ffffffffffffffffffffffff0000001c00000000000000000000000000000000"
	"000000000000000000000000ffffffffffffffff000573170000000000000000"
	"ffffffff00000001000000000000000000000000000003c00000000000000001"
	"000000050064000000000000001c000000000000000000000000000000000000"
	"000000000000000000000000000100000000000000000000000100000000ffff"
	"ff560000000000000001000000000000000100000000ffffffff";

constexpr auto kSchemePref = ""
	"00000001"
	"00000016" "64696374696f6e61726965735f69645f736368656d65"
	"00000001" "36";

constexpr auto kIncomingPrefs = ""
	"00000001"
	"0000000b" "7a7a2e696e636f6d696e67"
	"00000001" "69";

constexpr auto kDownloadPathTG = "0000000a" "0044003a002f00540047";

constexpr auto kFourAccents = ""
	"00000024" "00000004"
	"00000000ff030201" "00000001ff060504"
	"00000002ff090807" "00000003ff0c0b0a";

constexpr auto kGermanDictionaries = ""
	"00000002" "000000000000002a" "000000000000a462";
constexpr auto kGermanQt6Dictionaries = ""
	"00000002" "000000000000005e" "0000000000016f8b";

constexpr auto kDefaultDevice = "0000000e" "00640065006600610075006c0074";
constexpr auto kUsbMicDevice = "0000000e" "007500730062002d006d00690063";

constexpr auto kGermanEnglishIds = ""
	"00000002" "000000000000005e" "000000000000004b";
constexpr auto kFrenchId = "0000000000000055";
constexpr auto kGermanEnglishTail = ""
	"00000001" "00000002"
	"0000000400640065" "000000040065006e"
	"000000040065006e";

struct Edit {
	int offset = 0;
	QByteArray bytes;
	int size = 4;
};

[[nodiscard]] QByteArray Hex(const char *text) {
	return QByteArray::fromHex(text);
}

[[nodiscard]] QByteArray Int32(qint32 value) {
	const auto bits = quint32(value);
	auto result = QByteArray(4, '\0');
	for (auto i = 0; i != 4; ++i) {
		result[i] = char((bits >> (24 - 8 * i)) & 0xFF);
	}
	return result;
}

[[nodiscard]] QByteArray Default() {
	return Hex(kDefault7004005);
}

[[nodiscard]] QByteArray Edited(QByteArray bytes, std::vector<Edit> edits) {
	ranges::sort(edits, ranges::greater(), &Edit::offset);
	for (const auto &edit : edits) {
		bytes.replace(edit.offset, edit.size, edit.bytes);
	}
	return bytes;
}

[[nodiscard]] QByteArray Resaved(std::vector<Edit> edits = {}) {
	edits.push_back({ kPrefs, Hex(kSchemePref) });
	return Edited(Default(), std::move(edits));
}

[[nodiscard]] QByteArray Prefix(int size, std::vector<Edit> edits = {}) {
	return Edited(Default().left(size), std::move(edits));
}

[[nodiscard]] QString ExpectBytes(
		const char *what,
		const QByteArray &got,
		const QByteArray &expected) {
	if (got == expected) {
		return QString();
	}
	const auto common = std::min(got.size(), expected.size());
	auto offset = common;
	for (auto i = qsizetype(); i != common; ++i) {
		if (got[i] != expected[i]) {
			offset = i;
			break;
		}
	}
	return u"%1: size %2 vs %3, first difference at %4: got %5, expected %6"_q
		.arg(QString::fromLatin1(what))
		.arg(got.size())
		.arg(expected.size())
		.arg(offset)
		.arg(QString::fromLatin1(got.mid(offset, 8).toHex()))
		.arg(QString::fromLatin1(expected.mid(offset, 8).toHex()));
}

[[nodiscard]] QString ExpectSize(
		const char *what,
		const QByteArray &got,
		int expected) {
	return (got.size() == expected)
		? QString()
		: u"%1: got size=%2, expected size=%3"_q
			.arg(QString::fromLatin1(what))
			.arg(got.size())
			.arg(expected);
}

[[nodiscard]] QString ExpectValue(
		const char *what,
		qint64 got,
		qint64 expected) {
	return (got == expected)
		? QString()
		: u"%1: got %2, expected %3"_q
			.arg(QString::fromLatin1(what))
			.arg(got)
			.arg(expected);
}

[[nodiscard]] QString ExpectReal(
		const char *what,
		float64 got,
		float64 expected) {
	return (std::abs(got - expected) < 1e-9)
		? QString()
		: u"%1: got %2, expected %3"_q
			.arg(QString::fromLatin1(what))
			.arg(got, 0, 'g', 17)
			.arg(expected, 0, 'g', 17);
}

[[nodiscard]] QString ExpectText(
		const char *what,
		const QString &got,
		const QString &expected) {
	return (got == expected)
		? QString()
		: u"%1: got '%2', expected '%3'"_q.arg(
			QString::fromLatin1(what),
			got,
			expected);
}

[[nodiscard]] QString ExpectNull(const char *what, const QString &got) {
	return got.isNull()
		? QString()
		: u"%1: got '%2', expected a null string"_q.arg(
			QString::fromLatin1(what),
			got);
}

[[nodiscard]] QString ExpectNumbers(
		const char *what,
		const std::vector<int> &got,
		const std::vector<int> &expected) {
	const auto joined = [](const std::vector<int> &values) {
		auto result = QStringList();
		for (const auto value : values) {
			result.push_back(QString::number(value));
		}
		return result.join(',');
	};
	return ExpectText(what, joined(got), joined(expected));
}

[[nodiscard]] QString Labeled(const char *label, const QString &failure) {
	return failure.isEmpty()
		? QString()
		: (QString::fromLatin1(label) + u", "_q + failure);
}

[[nodiscard]] std::vector<int> LanguageValues(
		const std::vector<LanguageId> &list) {
	auto result = std::vector<int>();
	for (const auto &id : list) {
		result.push_back(int(id.value));
	}
	return result;
}

[[nodiscard]] QString AccentChannels(const Core::Settings &settings) {
	using Type = Window::Theme::EmbeddedType;
	const auto types = {
		Type::DayBlue,
		Type::Default,
		Type::Night,
		Type::NightGreen,
	};
	auto result = QStringList();
	for (const auto type : types) {
		const auto color = settings.themesAccentColors().get(type);
		result.push_back(color
			? u"%1.%2.%3.%4"_q
				.arg(color->red())
				.arg(color->green())
				.arg(color->blue())
				.arg(color->alpha())
			: u"none"_q);
	}
	return result.join(' ');
}

[[nodiscard]] QString ExpectFourAccents(const Core::Settings &settings) {
	return ExpectText(
		"accent channels",
		AccentChannels(settings),
		u"1.2.3.255 4.5.6.255 7.8.9.255 10.11.12.255"_q);
}

[[nodiscard]] QString CheckWriteDefaultGolden() {
	const auto written = Core::Settings().serialize();
	return FirstFailure({
		ExpectSize("fresh writer", written, 698),
		ExpectBytes("fresh writer", written, Default()),
	});
}

[[nodiscard]] QString CheckReadDefaults() {
	auto settings = Core::Settings();
	settings.addFromSerialized(Default(), kWriterVersion);
	const auto columns = settings.windowColumns();
	const auto resaved = settings.serialize();
	return FirstFailure({
		ExpectValue("tabbed", columns.tabbedSelectorSectionEnabled, 0),
		ExpectValue("info", columns.thirdSectionInfoEnabled, 1),
		ExpectValue("tray monochrome", settings.trayIconMonochrome(), 1),
		ExpectValue(
			"dictionaries scheme pref",
			settings.readPref<int>(
				Spellchecker::kDictionariesIdSchemePref,
				-1),
			6),
		ExpectSize("default resave", resaved, 729),
		ExpectBytes("default resave", resaved, Resaved()),
	});
}

[[nodiscard]] QString CheckReadMacStoreValues(
		qint32 storedWithChatRatio,
		float64 withChatRatio) {
	const auto macStore = std::vector<Edit>{
		{ kWithChatRatio, Int32(storedWithChatRatio) },
		{ kProxyTryIPv6, Int32(1) },
		{ kHardwareAcceleratedVideo, Int32(1) },
		{ kNoChatRatio, Int32(275000) },
	};
	auto settings = Core::Settings();
	settings.addFromSerialized(Edited(Default(), macStore), kWriterVersion);
	const auto columns = settings.windowColumns();
	return FirstFailure({
		ExpectValue("try IPv6", settings.proxy().tryIPv6(), 1),
		ExpectValue(
			"hardware video",
			settings.hardwareAcceleratedVideo(),
			1),
		ExpectReal(
			"with-chat ratio",
			columns.dialogsWithChatWidthRatio,
			withChatRatio),
		ExpectReal("no-chat ratio", columns.dialogsNoChatWidthRatio, .275),
		ExpectBytes(
			"Mac Store resave",
			settings.serialize(),
			Resaved(macStore)),
	});
}

[[nodiscard]] QString CheckReadDefaultGolden() {
	return FirstFailure({
		CheckReadDefaults(),
		Labeled("Mac Store defaults", CheckReadMacStoreValues(275000, .275)),
		Labeled("zero with-chat ratio", CheckReadMacStoreValues(0, 0.)),
	});
}

[[nodiscard]] QString CheckWriteEarlyNumbers() {
	auto settings = Core::Settings();
	settings.setSongVolume(.25);
	settings.setDownloadPath(u"D:/TG"_q);
	settings.setCallOutputVolume(50);
	settings.setCallInputVolume(70);
	return ExpectBytes(
		"early numbers",
		settings.serialize(),
		Edited(Default(), {
			{ kSongVolume, Int32(250000) },
			{ kDownloadPath, Hex(kDownloadPathTG) },
			{ kCallOutputVolume, Int32(50) },
			{ kCallInputVolume, Int32(70) },
		}));
}

[[nodiscard]] QString CheckWriteRecentEncodings() {
	auto settings = Core::Settings();
	settings.updateDialogsWidthRatio(.4);
	settings.setNoWarningExtensions({ u"zip"_q, u"exe"_q });
	settings.setMediaGridZoomStep(3);
	return ExpectBytes(
		"recent encodings",
		settings.serialize(),
		Edited(Default(), {
			{ kWithChatRatio, Int32(400000) },
			{
				kNoWarningExtensions,
				Hex("0000000e" "0065007800650020007a00690070"),
			},
			{ kNoChatRatio, Int32(400000) },
			{ kGridZoom, Int32(3) },
		}));
}

[[nodiscard]] std::vector<Edit> EarlyFieldsWithoutSounds() {
	return {
		{ kSongVolume, Int32(250000) },
		{ kAskDownloadPath, Int32(1) },
		{ kDownloadPath, Hex(kDownloadPathTG) },
		{ kDownloadPathBookmark, Hex("00000003" "010203") },
		{ kCallOutputVolume, Int32(50) },
		{ kCallInputVolume, Int32(70) },
	};
}

[[nodiscard]] std::vector<Edit> EarlyFields() {
	auto result = EarlyFieldsWithoutSounds();
	result.push_back({ kSoundOverrides, Hex(""
		"00000001"
		"00000006" "006d00730067"
		"00000010" "0044003a002f0073002e006d00700033") });
	return result;
}

[[nodiscard]] QByteArray EarlyInput() {
	return Edited(Default(), EarlyFields());
}

[[nodiscard]] QString CheckReadEarlyFields() {
	auto settings = Core::Settings();
	settings.addFromSerialized(EarlyInput(), kWriterVersion);
	return FirstFailure({
		ExpectReal("song volume", settings.songVolume(), .25),
		ExpectValue("ask download path", settings.askDownloadPath(), 1),
		ExpectText("download path", settings.downloadPath(), u"D:/TG"_q),
		ExpectBytes(
			"download path bookmark",
			settings.downloadPathBookmark(),
			Hex("010203")),
		ExpectValue("call output volume", settings.callOutputVolume(), 50),
		ExpectValue("call input volume", settings.callInputVolume(), 70),
		ExpectText(
			"msg sound",
			settings.getSoundPath(u"msg"_q),
			u"D:/s.mp3"_q),
		ExpectBytes(
			"early resave",
			settings.serialize(),
			Resaved(EarlyFields())),
	});
}

[[nodiscard]] std::vector<Edit> MiddleFields(qint32 noChatRatio) {
	return {
		{ kWithChatRatio, Int32(400000) },
		{ kWindowPosition, Hex(""
			"0000001c"
			"0000000a" "00000014" "00000320" "00000258"
			"01020304" "00000001" "0000007d"), 32 },
		{ kProxyUseForCalls, Int32(1) },
		{ kNoChatRatio, Int32(noChatRatio) },
	};
}

[[nodiscard]] QString CheckReadMiddleFields() {
	auto settings = Core::Settings();
	settings.addFromSerialized(
		Edited(Default(), MiddleFields(200000)),
		kWriterVersion);
	const auto &position = settings.windowPosition();
	const auto columns = settings.windowColumns();
	return FirstFailure({
		ExpectValue("position moncrc", position.moncrc, 0x01020304),
		ExpectValue("position maximized", position.maximized, 1),
		ExpectValue("position scale", position.scale, 125),
		ExpectValue("position x", position.x, 10),
		ExpectValue("position y", position.y, 20),
		ExpectValue("position w", position.w, 800),
		ExpectValue("position h", position.h, 600),
		ExpectValue(
			"proxy for calls",
			settings.proxy().useProxyForCalls(),
			1),
		ExpectReal(
			"with-chat ratio",
			columns.dialogsWithChatWidthRatio,
			.4),
		ExpectReal("no-chat ratio", columns.dialogsNoChatWidthRatio, .4),
		ExpectBytes(
			"middle resave",
			settings.serialize(),
			Resaved(MiddleFields(400000))),
	});
}

[[nodiscard]] std::vector<Edit> RecentFields() {
	return {
		{ kQuickDialogAction, Int32(1) },
		{ kAudioSpeed, Int32(50) },
		{ kGridZoom, Int32(3) },
		{ kPullToNextChannel, Int32(0) },
		{ kTabsMode, Int32(2) },
	};
}

[[nodiscard]] QString CheckReadRecentFields() {
	auto settings = Core::Settings();
	settings.addFromSerialized(
		Edited(Default(), RecentFields()),
		kWriterVersion);
	return FirstFailure({
		ExpectValue(
			"quick dialog action",
			int(settings.quickDialogAction()),
			1),
		ExpectReal("audio speed", settings.audioPlaybackSpeed(), .5),
		ExpectValue("grid zoom step", settings.mediaGridZoomStep(), 3),
		ExpectValue(
			"pull to next channel",
			settings.pullToNextChannel(),
			0),
		ExpectValue("tabs mode", int(settings.chatFiltersTabsMode()), 2),
		ExpectBytes(
			"recent resave",
			settings.serialize(),
			Resaved(RecentFields())),
	});
}

[[nodiscard]] QString CheckReadOutOfRangeNumbers() {
	auto settings = Core::Settings();
	settings.addFromSerialized(Edited(Default(), {
		{ kSongVolume, Int32(2000000) },
		{ kNotificationsCount, Int32(0) },
		{ kVideoQuality, Int32(10000) },
	}), kWriterVersion);
	return FirstFailure({
		ExpectValue("notifications count", settings.notificationsCount(), 3),
		ExpectReal("song volume", settings.songVolume(), 1.),
		ExpectValue("video height", settings.videoQuality().height, 0),
		ExpectBytes("out-of-range resave", settings.serialize(), Resaved({
			{ kSongVolume, Int32(1000000) },
			{ kVideoQuality, Int32(0) },
		})),
	});
}

[[nodiscard]] QString CheckLoadValidEnums(Core::Settings &settings) {
	settings.addFromSerialized(Edited(Default(), {
		{ kNotifyView, Int32(2) },
		{ kTabsMode, Int32(3) },
	}), kWriterVersion);
	return FirstFailure({
		ExpectValue("valid notify view", int(settings.notifyView()), 2),
		ExpectValue(
			"valid tabs mode",
			int(settings.chatFiltersTabsMode()),
			3),
	});
}

[[nodiscard]] QString CheckLoadInvalidEnums(Core::Settings &settings) {
	settings.addFromSerialized(Edited(Default(), {
		{ kAskDownloadPath, Int32(2) },
		{ kNotifyView, Int32(3) },
		{ kQuickDialogAction, Int32(9) },
		{ kTabsMode, Int32(4) },
	}), kWriterVersion);
	return FirstFailure({
		ExpectValue("kept notify view", int(settings.notifyView()), 2),
		ExpectValue(
			"kept tabs mode",
			int(settings.chatFiltersTabsMode()),
			3),
		ExpectValue(
			"raw quick dialog action",
			int(settings.quickDialogAction()),
			9),
		ExpectValue("ask download path", settings.askDownloadPath(), 0),
		ExpectBytes("enum bounds resave", settings.serialize(), Resaved({
			{ kNotifyView, Int32(2) },
			{ kQuickDialogAction, Int32(9) },
			{ kTabsMode, Int32(3) },
		})),
	});
}

[[nodiscard]] QString CheckReadEnumBounds() {
	auto settings = Core::Settings();
	return FirstFailure({
		CheckLoadValidEnums(settings),
		CheckLoadInvalidEnums(settings),
	});
}

[[nodiscard]] QString CheckLoaded(
		Core::Settings &settings,
		const char *what,
		const QByteArray &input,
		const QByteArray &expected) {
	settings.addFromSerialized(input, kWriterVersion);
	return ExpectBytes(what, settings.serialize(), expected);
}

[[nodiscard]] QString CheckLoadedNullAccentPrefix(Core::Settings &settings) {
	settings.addFromSerialized(Hex("ffffffff"), kWriterVersion);
	const auto columns = settings.windowColumns();
	auto expected = EarlyFieldsWithoutSounds();
	expected.push_back({ kTabbedSelector, Int32(1) });
	expected.push_back({ kThirdSectionInfo, Int32(0) });
	expected.push_back({ kTrayIcon, Int32(0) });
	return FirstFailure({
		ExpectReal("kept song volume", settings.songVolume(), .25),
		ExpectText("kept download path", settings.downloadPath(), u"D:/TG"_q),
		ExpectBytes(
			"kept download path bookmark",
			settings.downloadPathBookmark(),
			Hex("010203")),
		ExpectText(
			"replaced msg sound",
			settings.getSoundPath(u"msg"_q),
			u":/sounds/msg.mp3"_q),
		ExpectValue("absent tabbed", columns.tabbedSelectorSectionEnabled, 1),
		ExpectValue("absent info", columns.thirdSectionInfoEnabled, 0),
		ExpectValue("absent tray", settings.trayIconMonochrome(), 0),
		ExpectBytes(
			"null accent prefix resave",
			settings.serialize(),
			Resaved(expected)),
	});
}

[[nodiscard]] QString CheckReadEmptyAndRepeated() {
	auto fresh = Core::Settings();
	auto settings = Core::Settings();
	return FirstFailure({
		CheckLoaded(fresh, "fresh after empty", QByteArray(), Default()),
		CheckLoaded(
			settings,
			"first early load",
			EarlyInput(),
			Resaved(EarlyFields())),
		CheckLoaded(
			settings,
			"second early load",
			EarlyInput(),
			Resaved(EarlyFields())),
		CheckLoaded(
			settings,
			"early then empty",
			QByteArray(),
			Resaved(EarlyFields())),
		CheckLoadedNullAccentPrefix(settings),
	});
}

[[nodiscard]] QString CheckFourAccentsOnly() {
	const auto input = Hex(kFourAccents);
	auto settings = Core::Settings();
	settings.addFromSerialized(input, kWriterVersion);
	return FirstFailure({
		ExpectSize("four accents input", input, 40),
		ExpectFourAccents(settings),
		ExpectBytes("four accents resave", settings.serialize(), Resaved({
			{ kAccentColors, Hex(kFourAccents) },
			{ kTabbedSelector, Int32(1) },
			{ kThirdSectionInfo, Int32(0) },
			{ kTrayIcon, Int32(0) },
		})),
	});
}

[[nodiscard]] QString CheckFiveAccentsOnly() {
	const auto input = Hex(""
		"0000002c" "00000005"
		"00000000ff030201" "00000001ff060504"
		"00000002ff090807" "00000003ff0c0b0a"
		"00000000ff0f0e0d");
	auto settings = Core::Settings();
	settings.addFromSerialized(input, kWriterVersion);
	return FirstFailure({
		ExpectSize("five accents input", input, 48),
		ExpectText(
			"five accents",
			AccentChannels(settings),
			u"none none none none"_q),
		ExpectBytes("five accents resave", settings.serialize(), Default()),
	});
}

[[nodiscard]] QString CheckPrefixAccentOnly() {
	return FirstFailure({
		CheckFourAccentsOnly(),
		CheckFiveAccentsOnly(),
	});
}

[[nodiscard]] QString CheckLoadedBlockA(
		Core::Settings &settings,
		const std::vector<int> &dictionaries,
		const char *savedDictionaries) {
	const auto input = Prefix(kTabbedSelector, {
		{ kDictionaries, Hex(kGermanDictionaries) },
	});
	settings.addFromSerialized(input, kWriterVersion2001013);
	const auto columns = settings.windowColumns();
	return FirstFailure({
		ExpectSize("block A input", input, 180),
		ExpectValue("absent info", columns.thirdSectionInfoEnabled, 0),
		ExpectValue("absent tabbed", columns.tabbedSelectorSectionEnabled, 1),
		ExpectValue("absent tray", settings.trayIconMonochrome(), 0),
		ExpectNumbers(
			"dictionaries",
			settings.dictionariesEnabled(),
			dictionaries),
		ExpectValue(
			"dictionaries scheme pref",
			settings.readPref<int>(
				Spellchecker::kDictionariesIdSchemePref,
				-1),
			6),
		ExpectBytes("block A resave", settings.serialize(), Resaved({
			{ kDictionaries, Hex(savedDictionaries) },
			{ kTabbedSelector, Int32(1) },
			{ kThirdSectionInfo, Int32(0) },
			{ kTrayIcon, Int32(0) },
		})),
	});
}

[[nodiscard]] QString CheckPrefixColumnsAbsent() {
	const auto converts = kOldWriterConvertsDictionaries;
	auto fresh = Core::Settings();
	auto seeded = Core::Settings();
	seeded.writePref<int>(Spellchecker::kDictionariesIdSchemePref, 6);
	return FirstFailure({
		Labeled("old writer", CheckLoadedBlockA(
			fresh,
			converts ? std::vector{ 94, 94091 } : std::vector{ 42, 42082 },
			converts ? kGermanQt6Dictionaries : kGermanDictionaries)),
		Labeled("scheme 6 pref", CheckLoadedBlockA(
			seeded,
			{ 42, 42082 },
			kGermanDictionaries)),
	});
}

[[nodiscard]] QString CheckLoadedCleanEnd(const QByteArray &input) {
	auto settings = Core::Settings();
	settings.addFromSerialized(input, kWriterVersion);
	return FirstFailure({
		ExpectValue("notify from all", settings.notifyFromAll(), 0),
		ExpectValue("native frame", settings.nativeWindowFrame(), 0),
		ExpectValue(
			"dictionaries scheme pref",
			settings.readPref<int>(
				Spellchecker::kDictionariesIdSchemePref,
				-1),
			6),
		ExpectBytes("resave", settings.serialize(), Resaved({
			{ kNotifyFromAll, Int32(0) },
			{ kTrayIcon, Int32(0) },
		})),
	});
}

[[nodiscard]] QString CheckLoadedRejected(const QByteArray &input) {
	auto settings = Core::Settings();
	settings.addFromSerialized(input, kWriterVersion);
	return FirstFailure({
		ExpectValue("notify from all", settings.notifyFromAll(), 1),
		ExpectBytes("resave", settings.serialize(), Default()),
	});
}

[[nodiscard]] QString CheckPrefixCleanEnd() {
	const auto edited = Edited(Default(), {
		{ kNotifyFromAll, Int32(0) },
	});
	return FirstFailure({
		Labeled("196", CheckLoadedCleanEnd(edited.left(kNativeWindowFrame))),
		Labeled(
			"198",
			CheckLoadedRejected(edited.left(kNativeWindowFrame + 2))),
		Labeled("192", CheckLoadedRejected(edited.left(kNotifyFromAll))),
		Labeled("ff ff", CheckLoadedRejected(Hex("ffff"))),
	});
}

[[nodiscard]] QString CheckLoadedTrayPrefix(
		int size,
		bool tray,
		std::vector<Edit> resave) {
	auto settings = Core::Settings();
	settings.addFromSerialized(Prefix(size), kWriterVersion);
	resave.push_back({ kSystemDarkMode, Int32(0) });
	return FirstFailure({
		ExpectValue("tray monochrome", settings.trayIconMonochrome(), tray),
		ExpectValue("system dark mode", settings.systemDarkModeEnabled(), 0),
		ExpectBytes("resave", settings.serialize(), Resaved(resave)),
	});
}

[[nodiscard]] QString CheckPrefixTray() {
	return FirstFailure({
		Labeled("500", CheckLoadedTrayPrefix(kTrayIcon, false, {
			{ kTrayIcon, Int32(0) },
		})),
		Labeled("504", CheckLoadedTrayPrefix(kTrayIcon + 4, true, {})),
	});
}

[[nodiscard]] std::vector<Edit> LegacyCallDevices() {
	return {
		{ kLegacyCallPlaybackDevice, Hex(kDefaultDevice) },
		{ kLegacyCallCaptureDevice, Hex(kUsbMicDevice) },
	};
}

[[nodiscard]] QString CheckCallDevicesAbsent() {
	const auto input = Prefix(kCallPlaybackDevice, LegacyCallDevices());
	auto settings = Core::Settings();
	settings.addFromSerialized(input, kWriterVersion);
	return FirstFailure({
		ExpectSize("legacy devices input", input, 544),
		ExpectNull("legacy playback", settings.callPlaybackDeviceId()),
		ExpectText(
			"legacy capture",
			settings.callCaptureDeviceId(),
			u"usb-mic"_q),
		ExpectBytes("legacy devices resave", settings.serialize(), Resaved({
			{ kCallCaptureDevice, Hex(kUsbMicDevice) },
			{ kSystemDarkMode, Int32(0) },
		})),
	});
}

[[nodiscard]] QString CheckCallDevicesPresent() {
	const auto input = Prefix(kIvPosition, LegacyCallDevices());
	auto settings = Core::Settings();
	settings.addFromSerialized(input, kWriterVersion);
	return FirstFailure({
		ExpectSize("present devices input", input, 552),
		ExpectNull("present playback", settings.callPlaybackDeviceId()),
		ExpectNull("present capture", settings.callCaptureDeviceId()),
		ExpectBytes("present devices resave", settings.serialize(), Resaved({
			{ kSystemDarkMode, Int32(0) },
		})),
	});
}

[[nodiscard]] QString CheckPrefixCallDevices() {
	return FirstFailure({
		CheckCallDevicesAbsent(),
		CheckCallDevicesPresent(),
	});
}

[[nodiscard]] QString CheckAudioSpeedFromVoice() {
	auto settings = Core::Settings();
	settings.addFromSerialized(Prefix(kAudioSpeed, {
		{ kVoiceSpeed, Int32(150) },
	}), kWriterVersion);
	return FirstFailure({
		ExpectReal("voice speed", settings.voicePlaybackSpeed(), 1.5),
		ExpectReal("audio speed", settings.audioPlaybackSpeed(), 1.5),
		ExpectBytes("speeds resave", settings.serialize(), Resaved({
			{ kVoiceSpeed, Int32(150) },
			{ kAudioSpeed, Int32(150) },
		})),
	});
}

[[nodiscard]] QString CheckDisabledAudioSpeedFromVoice() {
	auto settings = Core::Settings();
	settings.addFromSerialized(Prefix(kAudioSpeed, {
		{ kLegacyVoiceSpeed, Int32(0) },
		{ kVoiceSpeed, Int32(150) },
	}), kWriterVersion);
	return FirstFailure({
		ExpectReal("disabled voice", settings.voicePlaybackSpeed(), 1.),
		ExpectReal(
			"remembered voice",
			settings.voicePlaybackSpeed(true),
			1.5),
		ExpectReal("disabled audio", settings.audioPlaybackSpeed(), 1.),
		ExpectReal(
			"remembered audio",
			settings.audioPlaybackSpeed(true),
			1.5),
		ExpectBytes("disabled speeds resave", settings.serialize(), Resaved({
			{ kVoiceSpeed, Int32(-150) },
			{ kAudioSpeed, Int32(-150) },
		})),
	});
}

[[nodiscard]] QString CheckPrefixAudioSpeed() {
	return FirstFailure({
		CheckAudioSpeedFromVoice(),
		CheckDisabledAudioSpeedFromVoice(),
	});
}

[[nodiscard]] QString CheckLoadedLanguages(
		const QByteArray &input,
		const QByteArray &expected,
		const std::vector<int> &skip,
		int target) {
	auto settings = Core::Settings();
	settings.addFromSerialized(input, kWriterVersion7003003);
	const auto bytes = ExpectBytes("resave", settings.serialize(), expected);
	if (!bytes.isEmpty()) {
		return bytes;
	}
	return FirstFailure({
		ExpectNumbers(
			"skip languages",
			LanguageValues(settings.skipTranslationLanguages()),
			skip),
		ExpectValue(
			"translate to",
			int(settings.translateTo().value),
			target),
	});
}

[[nodiscard]] QString CheckPrefixLanguageTail() {
	const auto noTail = Prefix(kLanguageTail, {
		{ kSkipLanguages, Hex(kGermanEnglishIds) },
		{ kTranslateTo, Hex(kFrenchId), 8 },
	});
	const auto cutTail = noTail + Hex(""
		"00000001" "00000002"
		"0000000400640065" "000000040065");
	const auto fullTail = noTail + Hex(kGermanEnglishTail);
	const auto legacy = Resaved({
		{ kSkipLanguages, Hex(""
			"00000002" "000000000000004b" "000000000000005e") },
		{ kTranslateTo, Hex(kFrenchId), 8 },
		{ kLanguageTail, Hex(""
			"00000001" "00000002"
			"000000040065006e" "0000000400640065"
			"0000000400660072"), 12 },
	});
	const auto codes = Resaved({
		{ kSkipLanguages, Hex(kGermanEnglishIds) },
		{ kTranslateTo, Hex("000000000000004b"), 8 },
		{ kLanguageTail, Hex(kGermanEnglishTail), 12 },
	});
	return FirstFailure({
		ExpectSize("no tail input", noTail, 702),
		ExpectSize("cut tail input", cutTail, 724),
		ExpectSize("full tail input", fullTail, 734),
		Labeled("no tail", CheckLoadedLanguages(
			noTail,
			legacy,
			{ QLocale::English, QLocale::German },
			QLocale::French)),
		Labeled("cut tail", CheckLoadedLanguages(
			cutTail,
			legacy,
			{ QLocale::English, QLocale::German },
			QLocale::French)),
		Labeled("full tail", CheckLoadedLanguages(
			fullTail,
			codes,
			{ QLocale::German, QLocale::English },
			QLocale::English)),
	});
}

[[nodiscard]] QString CheckLoadedLateCut(
		const QByteArray &input,
		const QByteArray &incoming,
		const QByteArray &expected) {
	auto settings = Core::Settings();
	settings.writePref<QByteArray>("zz.prior", QByteArray("p"));
	const auto seeded = Labeled("setup", ExpectBytes(
		"seeded prior pref",
		settings.readPref<QByteArray>("zz.prior", QByteArray("unset")),
		QByteArray("p")));
	settings.addFromSerialized(input, kWriterVersion);
	return FirstFailure({
		seeded,
		ExpectBytes(
			"prior pref",
			settings.readPref<QByteArray>("zz.prior", QByteArray("none")),
			QByteArray("none")),
		ExpectBytes(
			"incoming pref",
			settings.readPref<QByteArray>("zz.incoming", QByteArray("none")),
			incoming),
		ExpectValue("grid zoom step", settings.mediaGridZoomStep(), 5),
		ExpectReal("song volume", settings.songVolume(), .9),
		ExpectBytes("late cut resave", settings.serialize(), expected),
	});
}

[[nodiscard]] QString CheckRejectLateCut() {
	const auto populated = Prefix(kPullToNextChannel + 2, {
		{ kSongVolume, Int32(250000) },
		{ kPrefs, Hex(kIncomingPrefs) },
		{ kGridZoom, Int32(5) },
	});
	return FirstFailure({
		Labeled("empty prefs", CheckLoadedLateCut(
			Prefix(kPullToNextChannel + 2, {
				{ kSongVolume, Int32(250000) },
				{ kGridZoom, Int32(5) },
			}),
			QByteArray("none"),
			Edited(Default(), { { kGridZoom, Int32(5) } }))),
		ExpectSize("populated prefs input", populated, 700),
		Labeled("populated prefs", CheckLoadedLateCut(
			populated,
			QByteArray("i"),
			Edited(Default(), {
				{ kPrefs, Hex(kIncomingPrefs) },
				{ kGridZoom, Int32(5) },
			}))),
	});
}

[[nodiscard]] QString CheckRejectProxyAfterAccents() {
	auto settings = Core::Settings();
	settings.addFromSerialized(Edited(Default(), {
		{ kAccentColors, Hex(kFourAccents) },
		{ kSongVolume, Int32(250000) },
		{ kProxyListCount, Int32(-1) },
	}), kWriterVersion);
	return FirstFailure({
		ExpectFourAccents(settings),
		ExpectReal("song volume", settings.songVolume(), .9),
		ExpectBytes("proxy reject resave", settings.serialize(), Edited(
			Default(),
			{ { kAccentColors, Hex(kFourAccents) } })),
	});
}

[[nodiscard]] std::vector<Check> Checks() {
	return {
		{ "write.default-golden", CheckWriteDefaultGolden },
		{ "read.default-golden", CheckReadDefaultGolden },
		{ "write.early-numbers", CheckWriteEarlyNumbers },
		{ "write.recent-encodings", CheckWriteRecentEncodings },
		{ "read.early-fields", CheckReadEarlyFields },
		{ "read.middle-fields", CheckReadMiddleFields },
		{ "read.recent-fields", CheckReadRecentFields },
		{ "read.out-of-range-numbers", CheckReadOutOfRangeNumbers },
		{ "read.enum-bounds", CheckReadEnumBounds },
		{ "read.empty-and-repeated", CheckReadEmptyAndRepeated },
		{ "prefix.accent-only-four-vs-five-colors", CheckPrefixAccentOnly },
		{ "prefix.164-columns-absent", CheckPrefixColumnsAbsent },
		{ "prefix.196-clean-end-vs-198-truncation", CheckPrefixCleanEnd },
		{ "prefix.500-tray", CheckPrefixTray },
		{ "prefix.516-call-devices-from-legacy", CheckPrefixCallDevices },
		{ "prefix.670-audio-speed-from-voice", CheckPrefixAudioSpeed },
		{ "prefix.686-language-tail", CheckPrefixLanguageTail },
		{ "reject.late-cut-keeps-prefs-and-grid", CheckRejectLateCut },
		{ "reject.proxy-after-accents", CheckRejectProxyAfterAccents },
	};
}

} // namespace

int main() {
	return Test::NamedChecks::Run(Checks());
}
