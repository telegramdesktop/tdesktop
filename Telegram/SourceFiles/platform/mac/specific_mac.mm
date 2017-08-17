/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "platform/mac/specific_mac.h"

#include <cstdlib>

#include "lang/lang_keys.h"
#include "application.h"
#include "mainwidget.h"
#include "history/history_widget.h"

#include "storage/localstorage.h"
#include "passcodewidget.h"
#include "mainwindow.h"
#include "history/history_location_manager.h"
#include "platform/mac/mac_utilities.h"

#include <execinfo.h>

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/ev_keymap.h>
#include <SPMediaKeyTap.h>
#include <mach-o/dyld.h>

namespace {

QStringList _initLogs;

class _PsEventFilter : public QAbstractNativeEventFilter {
public:
	_PsEventFilter() {
	}

	bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) {
		auto wnd = App::wnd();
		if (!wnd) return false;

		return wnd->psFilterNativeEvent(message);
	}
};

_PsEventFilter *_psEventFilter = nullptr;

};

namespace {

QRect _monitorRect;
TimeMs _monitorLastGot = 0;

} // namespace

QRect psDesktopRect() {
	auto tnow = getms(true);
	if (tnow > _monitorLastGot + 1000 || tnow < _monitorLastGot) {
		_monitorLastGot = tnow;
		_monitorRect = QApplication::desktop()->availableGeometry(App::wnd());
	}
	return _monitorRect;
}

void psShowOverAll(QWidget *w, bool canFocus) {
	objc_showOverAll(w->winId(), canFocus);
}

void psBringToBack(QWidget *w) {
	objc_bringToBack(w->winId());
}

QAbstractNativeEventFilter *psNativeEventFilter() {
	delete _psEventFilter;
	_psEventFilter = new _PsEventFilter();
	return _psEventFilter;
}

void psWriteDump() {
	double v = objc_appkitVersion();
	SignalHandlers::dump() << "OS-Version: " << v;
}

QString demanglestr(const QString &mangled) {
	QByteArray cmd = ("c++filt -n " + mangled).toUtf8();
	FILE *f = popen(cmd.constData(), "r");
	if (!f) return "BAD_SYMBOL_" + mangled;

	QString result;
	char buffer[4096] = {0};
	while (!feof(f)) {
		if (fgets(buffer, 4096, f) != NULL) {
			result += buffer;
		}
	}
	pclose(f);
	return result.trimmed();
}

QString escapeShell(const QString &str) {
	QString result;
	const QChar *b = str.constData(), *e = str.constEnd();
	for (const QChar *ch = b; ch != e; ++ch) {
		if (*ch == ' ' || *ch == '"' || *ch == '\'' || *ch == '\\') {
			if (result.isEmpty()) {
				result.reserve(str.size() * 2);
			}
			if (ch > b) {
				result.append(b, ch - b);
			}
			result.append('\\');
			b = ch;
		}
	}
	if (result.isEmpty()) return str;

	if (e > b) {
		result.append(b, e - b);
	}
	return result;
}

QStringList atosstr(uint64 *addresses, int count, uint64 base) {
	QStringList result;
	if (!count || cExeName().isEmpty()) return result;

	result.reserve(count);
	QString cmdstr = "atos -o " + escapeShell(cExeDir() + cExeName()) + qsl("/Contents/MacOS/Telegram -l 0x%1").arg(base, 0, 16);
	for (int i = 0; i < count; ++i) {
		if (addresses[i]) {
			cmdstr += qsl(" 0x%1").arg(addresses[i], 0, 16);
		}
	}
	QByteArray cmd = cmdstr.toUtf8();
	FILE *f = popen(cmd.constData(), "r");

	QStringList atosResult;
	if (f) {
		char buffer[4096] = {0};
		while (!feof(f)) {
			if (fgets(buffer, 4096, f) != NULL) {
				atosResult.push_back(QString::fromUtf8(buffer));
			}
		}
		pclose(f);
	}
	for (int i = 0, j = 0; i < count; ++i) {
		if (addresses[i]) {
			if (j < atosResult.size() && !atosResult.at(j).isEmpty() && !atosResult.at(j).startsWith(qstr("0x"))) {
				result.push_back(atosResult.at(j).trimmed());
			} else {
				result.push_back(QString());
			}
			++j;
		} else {
			result.push_back(QString());
		}
	}
	return result;

}

QString psPrepareCrashDump(const QByteArray &crashdump, QString dumpfile) {
	QString initial = QString::fromUtf8(crashdump), result;
	QStringList lines = initial.split('\n');
	result.reserve(initial.size());
	int32 i = 0, l = lines.size();

	while (i < l) {
		uint64 addresses[1024] = { 0 };
		for (; i < l; ++i) {
			result.append(lines.at(i)).append('\n');
			QString line = lines.at(i).trimmed();
			if (line == qstr("Base image addresses:")) {
				++i;
				break;
			}
		}

		uint64 base = 0;
		for (int32 start = i; i < l; ++i) {
			QString line = lines.at(i).trimmed();
			if (line.isEmpty()) break;

			if (!base) {
				QRegularExpressionMatch m = QRegularExpression(qsl("^\\d+ (\\d+) \\((.+)\\)")).match(line);
				if (m.hasMatch()) {
					if (uint64 address = m.captured(1).toULongLong()) {
						if (m.captured(2).endsWith(qstr("Contents/MacOS/Telegram"))) {
							base = address;
						}
					}
				}
			}
		}
		if (base) {
			result.append(qsl("(base address read: 0x%1)\n").arg(base, 0, 16));
		} else {
			result.append(qsl("ERROR: base address not read!\n"));
		}

		for (; i < l; ++i) {
			result.append(lines.at(i)).append('\n');
			QString line = lines.at(i).trimmed();
			if (line == qstr("Backtrace:")) {
				++i;
				break;
			}
		}

		int32 start = i;
		for (; i < l; ++i) {
			QString line = lines.at(i).trimmed();
			if (line.isEmpty()) break;

			if (QRegularExpression(qsl("^\\d+")).match(line).hasMatch()) {
				QStringList lst = line.split(' ', QString::SkipEmptyParts);
				if (lst.size() > 2) {
					uint64 addr = lst.at(2).startsWith(qstr("0x")) ? lst.at(2).mid(2).toULongLong(0, 16) : lst.at(2).toULongLong();
					addresses[i - start] = addr;
				}
			}
		}

		QStringList atos = atosstr(addresses, i - start, base);
		for (i = start; i < l; ++i) {
			QString line = lines.at(i).trimmed();
			if (line.isEmpty()) break;

			if (!QRegularExpression(qsl("^\\d+")).match(line).hasMatch()) {
				if (!lines.at(i).startsWith(qstr("ERROR: "))) {
					result.append(qstr("BAD LINE: "));
				}
				result.append(line).append('\n');
				continue;
			}
			QStringList lst = line.split(' ', QString::SkipEmptyParts);
			result.append('\n').append(lst.at(0)).append(qsl(". "));
			if (lst.size() < 3) {
				result.append(qstr("BAD LINE: ")).append(line).append('\n');
				continue;
			}
			if (lst.size() > 5 && lst.at(3) == qsl("0x0") && lst.at(4) == qsl("+") && lst.at(5) == qsl("1")) {
				result.append(qsl("(0x1 separator)\n"));
				continue;
			}
			if (i - start < atos.size()) {
				if (!atos.at(i - start).isEmpty()) {
					result.append(atos.at(i - start)).append('\n');
					continue;
				}
			}

			for (int j = 1, s = lst.size();;) {
				if (lst.at(j).startsWith('_')) {
					result.append(demanglestr(lst.at(j)));
					if (++j < s) {
						result.append(' ');
						for (;;) {
							result.append(lst.at(j));
							if (++j < s) {
								result.append(' ');
							} else {
								break;
							}
						}
					}
					break;
				} else if (j > 2) {
					result.append(lst.at(j));
				}
				if (++j < s) {
					result.append(' ');
				} else {
					break;
				}
			}
			result.append(qsl(" [demangled]")).append('\n');
		}
	}
	return result;
}

void psDeleteDir(const QString &dir) {
	objc_deleteDir(dir);
}

namespace {

auto _lastUserAction = 0LL;

} // namespace

void psUserActionDone() {
	_lastUserAction = getms(true);
}

bool psIdleSupported() {
	return objc_idleSupported();
}

TimeMs psIdleTime() {
	auto idleTime = 0LL;
	return objc_idleTime(idleTime) ? idleTime : (getms(true) - _lastUserAction);
}

QStringList psInitLogs() {
	return _initLogs;
}

void psClearInitLogs() {
	_initLogs = QStringList();
}

void psActivateProcess(uint64 pid) {
	if (!pid) {
		objc_activateProgram(App::wnd() ? App::wnd()->winId() : 0);
	}
}

QString psAppDataPath() {
	return objc_appDataPath();
}

QString psDownloadPath() {
	return objc_downloadPath();
}

void psDoCleanup() {
	try {
		psAutoStart(false, true);
		psSendToMenu(false, true);
	} catch (...) {
	}
}

int psCleanup() {
	psDoCleanup();
	return 0;
}

void psDoFixPrevious() {
}

int psFixPrevious() {
	psDoFixPrevious();
	return 0;
}

namespace Platform {

void start() {
	objc_start();
}

void finish() {
	delete _psEventFilter;
	_psEventFilter = nullptr;

	objc_finish();
}

void StartTranslucentPaint(QPainter &p, QPaintEvent *e) {
#ifdef OS_MAC_OLD
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(e->rect(), Qt::transparent);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
#endif // OS_MAC_OLD
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

QString CurrentExecutablePath(int argc, char *argv[]) {
	return NS2QString([[NSBundle mainBundle] bundlePath]);
}

} // namespace Platform

void psNewVersion() {
	objc_registerCustomScheme();
}

void psExecUpdater() {
	if (!objc_execUpdater()) {
		psDeleteDir(cWorkingDir() + qsl("tupdates/temp"));
	}
}

void psExecTelegram(const QString &crashreport) {
	objc_execTelegram(crashreport);
}

void psAutoStart(bool start, bool silent) {
}

void psSendToMenu(bool send, bool silent) {
}

void psUpdateOverlayed(QWidget *widget) {
}

void psDownloadPathEnableAccess() {
	objc_downloadPathEnableAccess(Global::DownloadPathBookmark());
}

QByteArray psDownloadPathBookmark(const QString &path) {
	return objc_downloadPathBookmark(path);
}

QByteArray psPathBookmark(const QString &path) {
	return objc_pathBookmark(path);
}

bool psLaunchMaps(const LocationCoords &coords) {
	return QDesktopServices::openUrl(qsl("https://maps.apple.com/?q=Point&z=16&ll=%1,%2").arg(coords.latAsString()).arg(coords.lonAsString()));
}

QString strNotificationAboutThemeChange() {
	const uint32 letters[] = { 0xE9005541, 0x5600DC70, 0x88001570, 0xF500D86C, 0x8100E165, 0xEE005949, 0x2900526E, 0xAE00FB74, 0x96000865, 0x7000CD72, 0x3B001566, 0x5F007361, 0xAE00B663, 0x74009A65, 0x29003054, 0xC6002668, 0x98003865, 0xFA00336D, 0xA3007A65, 0x93001443, 0xBB007868, 0xE100E561, 0x3500366E, 0xC0007A67, 0x0200CA65, 0xBE00DF64, 0xE300BB4E, 0x2900D26F, 0xD500D374, 0xE900E269, 0x86008F66, 0xC4006669, 0x1C00A863, 0xE600A761, 0x8E00EE74, 0xB300B169, 0xCF00B36F, 0xE600D36E };
	return strMakeFromLetters(letters);
}

QString strNotificationAboutScreenLocked() {
	const uint32 letters[] = { 0x22008263, 0x0800DB6F, 0x45004F6D, 0xCC00972E, 0x0E00A861, 0x9700D970, 0xA100D570, 0x8900686C, 0xB300B365, 0xFE00DE2E, 0x76009B73, 0xFA00BF63, 0xE000A772, 0x9C009F65, 0x4E006065, 0xD900426E, 0xB7007849, 0x64006473, 0x6700824C, 0xE300706F, 0x7C00A063, 0x8F00D76B, 0x04001C65, 0x1C00A664 };
	return strMakeFromLetters(letters);
}

QString strNotificationAboutScreenUnlocked() {
	const uint32 letters[] = { 0x9200D763, 0xC8003C6F, 0xD2003F6D, 0x6000012E, 0x36004061, 0x4400E570, 0xA500BF70, 0x2E00796C, 0x4A009E65, 0x2E00612E, 0xC8001D73, 0x57002263, 0xF0005872, 0x49000765, 0xE5008D65, 0xE600D76E, 0xE8007049, 0x19005C73, 0x34009455, 0xB800B36E, 0xF300CA6C, 0x4C00806F, 0x5300A763, 0xD1003B6B, 0x63003565, 0xF800F264 };
	return strMakeFromLetters(letters);
}

QString strStyleOfInterface() {
	const uint32 letters[] = { 0xEF004041, 0x4C007F70, 0x1F007A70, 0x9E00A76C, 0x8500D165, 0x2E003749, 0x7B00526E, 0x3400E774, 0x3C00FA65, 0x6200B172, 0xF7001D66, 0x0B002961, 0x71008C63, 0x86005465, 0xA3006F53, 0x11006174, 0xCD001779, 0x8200556C, 0x6C009B65 };
	return strMakeFromLetters(letters);
}
