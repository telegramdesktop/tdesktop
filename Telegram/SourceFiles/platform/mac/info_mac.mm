/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/info_mac.h"

#include "platform/platform_info.h"
#include "platform/mac/mac_utilities.h"

#include <sys/sysctl.h>
#include <Cocoa/Cocoa.h>

namespace Platform {
namespace {

QString FromIdentifier(const QString &model) {
	if (model.isEmpty() || model.toLower().indexOf("mac") < 0) {
		return QString();
	}
	QStringList words;
	QString word;
	for (const QChar ch : model) {
		if (!ch.isLetter()) {
			continue;
		}
		if (ch.isUpper()) {
			if (!word.isEmpty()) {
				words.push_back(word);
				word = QString();
			}
		}
		word.append(ch);
	}
	if (!word.isEmpty()) {
		words.push_back(word);
	}
	QString result;
	for (const QString word : words) {
		if (!result.isEmpty()
			&& word != "Mac"
			&& word != "Book") {
			result.append(' ');
		}
		result.append(word);
	}
	return result;
}

int MinorVersion() {
	static const int version = QSysInfo::macVersion();
	constexpr int kShift = 2;
	if (version == QSysInfo::MV_Unknown
#ifndef OS_MAC_OLD
		|| version == QSysInfo::MV_None
#endif // OS_MAC_OLD
		|| version < kShift + 6) {
		return 0;
	}
	return version - kShift;
}

template <int Minor>
bool IsMacThatOrGreater() {
	static const auto result = (MinorVersion() >= Minor);
	return result;
}

} // namespace

QString DeviceModelPretty() {
	size_t length = 0;
    sysctlbyname("hw.model", nullptr, &length, nullptr, 0);
    if (length > 0) {
        QByteArray bytes(length, Qt::Uninitialized);
        sysctlbyname("hw.model", bytes.data(), &length, nullptr, 0);
		const QString parsed = FromIdentifier(QString::fromUtf8(bytes));
		if (!parsed.isEmpty()) {
			return parsed;
		}
    }
	return "Mac";
}

QString SystemVersionPretty() {
	const auto version = MinorVersion();
	if (!version) {
		return "OS X";
	} else if (version < 12) {
		return QString("OS X 10.%1").arg(version);
	}
	return QString("macOS 10.%1").arg(version);
}

QString SystemCountry() {
	NSLocale *currentLocale = [NSLocale currentLocale];  // get the current locale.
	NSString *countryCode = [currentLocale objectForKey:NSLocaleCountryCode];
	return countryCode ? NS2QString(countryCode) : QString();
}

QString SystemLanguage() {
	if (auto currentLocale = [NSLocale currentLocale]) { // get the current locale.
		if (NSString *collator = [currentLocale objectForKey:NSLocaleCollatorIdentifier]) {
			return NS2QString(collator);
		}
		if (NSString *identifier = [currentLocale objectForKey:NSLocaleIdentifier]) {
			return NS2QString(identifier);
		}
		if (NSString *language = [currentLocale objectForKey:NSLocaleLanguageCode]) {
			return NS2QString(language);
		}
	}
	return QString();
}

QDate WhenSystemBecomesOutdated() {
	if (!IsMac10_10OrGreater()) {
		return QDate(2019, 9, 1);
	}
	return QDate();
}

int AutoUpdateVersion() {
	if (!IsMac10_10OrGreater()) {
		return 1;
	}
	return 2;
}

QString AutoUpdateKey() {
	if (IsMacOldBuild()) {
		return "mac32";
	} else if (!IsMac10_12OrGreater()) {
		return "osx";
	} else {
		return "mac";
	}
}

bool IsMac10_6OrGreater() {
	return IsMacThatOrGreater<6>();
}

bool IsMac10_7OrGreater() {
	return IsMacThatOrGreater<7>();
}

bool IsMac10_8OrGreater() {
	return IsMacThatOrGreater<8>();
}

bool IsMac10_9OrGreater() {
	return IsMacThatOrGreater<9>();
}

bool IsMac10_10OrGreater() {
	return IsMacThatOrGreater<10>();
}

bool IsMac10_11OrGreater() {
	return IsMacThatOrGreater<11>();
}

bool IsMac10_12OrGreater() {
	return IsMacThatOrGreater<12>();
}

bool IsMac10_13OrGreater() {
	return IsMacThatOrGreater<13>();
}

bool IsMac10_14OrGreater() {
	return IsMacThatOrGreater<14>();
}

} // namespace Platform
