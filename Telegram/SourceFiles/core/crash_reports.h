/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core {
class Launcher;
} // namespace Core

namespace CrashReports {

QString PlatformString();

#ifndef DESKTOP_APP_DISABLE_CRASH_REPORTS

struct dump {
	~dump();
};
const dump &operator<<(const dump &stream, const char *str);
const dump &operator<<(const dump &stream, const wchar_t *str);
const dump &operator<<(const dump &stream, int num);
const dump &operator<<(const dump &stream, unsigned int num);
const dump &operator<<(const dump &stream, unsigned long num);
const dump &operator<<(const dump &stream, unsigned long long num);
const dump &operator<<(const dump &stream, double num);

#endif // DESKTOP_APP_DISABLE_CRASH_REPORTS

enum Status {
	CantOpen,
	Started
};
// Open status or crash report dump.
using StartResult = std::variant<Status, QByteArray>;
StartResult Start();
Status Restart(); // can be only CantOpen or Started
void Finish();

void SetAnnotation(const std::string &key, const QString &value);
void SetAnnotationHex(const std::string &key, const QString &value);
inline void ClearAnnotation(const std::string &key) {
	SetAnnotation(key, QString());
}

// Remembers value pointer and tries to add the value to the crash report.
// Attention! You should call clearCrashAnnotationRef(key) before destroying value.
void SetAnnotationRef(const std::string &key, const QString *valuePtr);
inline void ClearAnnotationRef(const std::string &key) {
	SetAnnotationRef(key, nullptr);
}

void StartCatching(not_null<Core::Launcher*> launcher);
void FinishCatching();

} // namespace CrashReports
