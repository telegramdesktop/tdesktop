/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/info_win.h"

#include "platform/platform_info.h"
#include "platform/win/wrapper_windows_h.h"

#include <VersionHelpers.h>

namespace Platform {
namespace {

QString GetLangCodeById(unsigned int lngId) {
	const auto primary = (lngId & 0xFFU);
	switch (primary) {
	case 0x36: return qsl("af");
	case 0x1C: return qsl("sq");
	case 0x5E: return qsl("am");
	case 0x01: return qsl("ar");
	case 0x2B: return qsl("hy");
	case 0x4D: return qsl("as");
	case 0x2C: return qsl("az");
	case 0x45: return qsl("bn");
	case 0x6D: return qsl("ba");
	case 0x2D: return qsl("eu");
	case 0x23: return qsl("be");
	case 0x1A:
	if (lngId == LANG_CROATIAN) {
		return qsl("hr");
	} else if (lngId == LANG_BOSNIAN_NEUTRAL || lngId == LANG_BOSNIAN) {
		return qsl("bs");
	}
	return qsl("sr");
	break;
	case 0x7E: return qsl("br");
	case 0x02: return qsl("bg");
	case 0x92: return qsl("ku");
	case 0x03: return qsl("ca");
	case 0x04: return qsl("zh");
	case 0x83: return qsl("co");
	case 0x05: return qsl("cs");
	case 0x06: return qsl("da");
	case 0x65: return qsl("dv");
	case 0x13: return qsl("nl");
	case 0x09: return qsl("en");
	case 0x25: return qsl("et");
	case 0x38: return qsl("fo");
	case 0x0B: return qsl("fi");
	case 0x0c: return qsl("fr");
	case 0x62: return qsl("fy");
	case 0x56: return qsl("gl");
	case 0x37: return qsl("ka");
	case 0x07: return qsl("de");
	case 0x08: return qsl("el");
	case 0x6F: return qsl("kl");
	case 0x47: return qsl("gu");
	case 0x68: return qsl("ha");
	case 0x0D: return qsl("he");
	case 0x39: return qsl("hi");
	case 0x0E: return qsl("hu");
	case 0x0F: return qsl("is");
	case 0x70: return qsl("ig");
	case 0x21: return qsl("id");
	case 0x5D: return qsl("iu");
	case 0x3C: return qsl("ga");
	case 0x34: return qsl("xh");
	case 0x35: return qsl("zu");
	case 0x10: return qsl("it");
	case 0x11: return qsl("ja");
	case 0x4B: return qsl("kn");
	case 0x3F: return qsl("kk");
	case 0x53: return qsl("kh");
	case 0x87: return qsl("rw");
	case 0x12: return qsl("ko");
	case 0x40: return qsl("ky");
	case 0x54: return qsl("lo");
	case 0x26: return qsl("lv");
	case 0x27: return qsl("lt");
	case 0x6E: return qsl("lb");
	case 0x2F: return qsl("mk");
	case 0x3E: return qsl("ms");
	case 0x4C: return qsl("ml");
	case 0x3A: return qsl("mt");
	case 0x81: return qsl("mi");
	case 0x4E: return qsl("mr");
	case 0x50: return qsl("mn");
	case 0x61: return qsl("ne");
	case 0x14: return qsl("no");
	case 0x82: return qsl("oc");
	case 0x48: return qsl("or");
	case 0x63: return qsl("ps");
	case 0x29: return qsl("fa");
	case 0x15: return qsl("pl");
	case 0x16: return qsl("pt");
	case 0x67: return qsl("ff");
	case 0x46: return qsl("pa");
	case 0x18: return qsl("ro");
	case 0x17: return qsl("rm");
	case 0x19: return qsl("ru");
	case 0x3B: return qsl("se");
	case 0x4F: return qsl("sa");
	case 0x32: return qsl("tn");
	case 0x59: return qsl("sd");
	case 0x5B: return qsl("si");
	case 0x1B: return qsl("sk");
	case 0x24: return qsl("sl");
	case 0x0A: return qsl("es");
	case 0x41: return qsl("sw");
	case 0x1D: return qsl("sv");
	case 0x28: return qsl("tg");
	case 0x49: return qsl("ta");
	case 0x44: return qsl("tt");
	case 0x4A: return qsl("te");
	case 0x1E: return qsl("th");
	case 0x51: return qsl("bo");
	case 0x73: return qsl("ti");
	case 0x1F: return qsl("tr");
	case 0x42: return qsl("tk");
	case 0x22: return qsl("uk");
	case 0x20: return qsl("ur");
	case 0x80: return qsl("ug");
	case 0x43: return qsl("uz");
	case 0x2A: return qsl("vi");
	case 0x52: return qsl("cy");
	case 0x88: return qsl("wo");
	case 0x78: return qsl("ii");
	case 0x6A: return qsl("yo");
	}
	return QString();
}

} // namespace

QString DeviceModelPretty() {
	return "PC";
}

QString SystemVersionPretty() {
	if (IsWindows10OrGreater()) {
		return "Windows 10";
	} else if (IsWindows8Point1OrGreater()) {
		return "Windows 8.1";
	} else if (IsWindows8OrGreater()) {
		return "Windows 8";
	} else if (IsWindows7OrGreater()) {
		return "Windows 7";
	} else if (IsWindowsVistaOrGreater()) {
		return "Windows Vista";
	} else if (IsWindowsXPOrGreater()) {
		return "Windows XP";
	} else {
		return QSysInfo::prettyProductName();
	}
}

QString SystemCountry() {
	int chCount = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, 0, 0);
	if (chCount && chCount < 128) {
		WCHAR wstrCountry[128];
		int len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, wstrCountry, chCount);
		if (len) {
			return QString::fromStdWString(std::wstring(wstrCountry));
		}
	}
	return QString();
}

QString SystemLanguage() {
	constexpr auto kMaxLanguageLength = 128;

	auto uiLanguageId = GetUserDefaultUILanguage();
	auto uiLanguageLength = GetLocaleInfo(uiLanguageId, LOCALE_SNAME, nullptr, 0);
	if (uiLanguageLength > 0 && uiLanguageLength < kMaxLanguageLength) {
		WCHAR uiLanguageWideString[kMaxLanguageLength] = { 0 };
		uiLanguageLength = GetLocaleInfo(uiLanguageId, LOCALE_SNAME, uiLanguageWideString, uiLanguageLength);
		if (uiLanguageLength <= 0) {
			return QString();
		}
		return QString::fromWCharArray(uiLanguageWideString);
	}
	auto uiLanguageCodeLength = GetLocaleInfo(uiLanguageId, LOCALE_ILANGUAGE, nullptr, 0);
	if (uiLanguageCodeLength > 0 && uiLanguageCodeLength < kMaxLanguageLength) {
		WCHAR uiLanguageCodeWideString[kMaxLanguageLength] = { 0 };
		uiLanguageCodeLength = GetLocaleInfo(uiLanguageId, LOCALE_ILANGUAGE, uiLanguageCodeWideString, uiLanguageCodeLength);
		if (uiLanguageCodeLength <= 0) {
			return QString();
		}

		auto languageCode = 0U;
		for (auto i = 0; i != uiLanguageCodeLength; ++i) {
			auto ch = uiLanguageCodeWideString[i];
			if (!ch) {
				break;
			}
			languageCode *= 0x10U;
			if (ch >= WCHAR('0') && ch <= WCHAR('9')) {
				languageCode += static_cast<unsigned>(int(ch) - int(WCHAR('0')));
			} else if (ch >= WCHAR('A') && ch <= WCHAR('F')) {
				languageCode += static_cast<unsigned>(0x0A + int(ch) - int(WCHAR('A')));
			} else {
				return QString();
			}
		}
		return GetLangCodeById(languageCode);
	}
	return QString();
}

QDate WhenSystemBecomesOutdated() {
	if (!IsWindows7OrGreater()) {
		return QDate(2019, 9, 1);
	}
	return QDate();
}

bool IsWindowsXPOrGreater() {
	static const auto result = ::IsWindowsXPOrGreater();
	return result;
}

bool IsWindowsVistaOrGreater() {
	static const auto result = ::IsWindowsVistaOrGreater();
	return result;
}

bool IsWindows7OrGreater() {
	static const auto result = ::IsWindows7OrGreater();
	return result;
}

bool IsWindows8OrGreater() {
	static const auto result = ::IsWindows8OrGreater();
	return result;
}

bool IsWindows8Point1OrGreater() {
	static const auto result = ::IsWindows8Point1OrGreater();
	return result;
}

bool IsWindows10OrGreater() {
	static const auto result = ::IsWindows10OrGreater();
	return result;
}

} // namespace Platform
