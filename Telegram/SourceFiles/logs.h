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

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

#ifdef Q_OS_WIN
#define OUTPUT_LOG(msg) (OutputDebugString((QString msg + "\n").toStdWString().c_str()))
#endif

#if (defined _DEBUG || defined _WITH_DEBUG)

struct DebugLogMemoryBuffer {
	DebugLogMemoryBuffer(const void *ptr, uint32 size) : p(ptr), s(size) {
	}
	QString str() const {
		QString result;
		const uchar *buf((const uchar*)p);
		const char *hex = "0123456789ABCDEF";
		result.reserve(s * 3);
		for (uint32 i = 0; i < s; ++i) {
			result += hex[(buf[i] >> 4)];
			result += hex[buf[i] & 0x0F];
			result += ' ';
		}
		result.chop(1);
		return result;
	}

	const void *p;
	uint32 s;
};

inline DebugLogMemoryBuffer mb(const void *ptr, uint32 size) {
	return DebugLogMemoryBuffer(ptr, size);
}

void debugLogWrite(const char *file, int32 line, const QString &v);
#define DEBUG_LOG(msg) { if (cDebug()) debugLogWrite(__FILE__, __LINE__, QString msg); }
//usage DEBUG_LOG(("log: %1 %2").arg(1).arg(2))

void tcpLogWrite(const QString &v);
#define TCP_LOG(msg) { if (cDebug()) tcpLogWrite(QString msg); }
//usage TCP_LOG(("log: %1 %2").arg(1).arg(2))

void mtpLogWrite(int32 dc, const QString &v);
#define MTP_LOG(dc, msg) { if (cDebug()) mtpLogWrite(dc, QString msg); }
//usage MTP_LOG(dc, ("log: %1 %2").arg(1).arg(2))

#else
#define DEBUG_LOG(msg) (void(0))
#define TCP_LOG(msg) (void(0))
#define MTP_LOG(dc, msg) (void(0))
#endif

inline const char *logBool(bool v) {
	return v ? "[TRUE]" : "[FALSE]";
}

class MTPlong;
QString logVectorLong(const QVector<MTPlong> &ids);
QString logVectorLong(const QVector<uint64> &ids);

void logWrite(const QString &v);

#define LOG(msg) (logWrite(QString msg))
//usage LOG(("log: %1 %2").arg(1).arg(2))

static volatile int *t_assert_nullptr = 0;
inline void t_noop() {}
inline void t_assert_fail(const char *message, const char *file, int32 line) {
	LOG(("Assertion Failed! %1 %2:%3").arg(message).arg(file).arg(line));
	*t_assert_nullptr = 0;
}
#define t_assert_full(condition, message, file, line) ((!(condition)) ? t_assert_fail(message, file, line) : t_noop())
#define t_assert_c(condition, comment) t_assert_full(condition, "\"" #condition "\" (" comment ")", __FILE__, __LINE__)
#define t_assert(condition) t_assert_full(condition, "\"" #condition "\"", __FILE__, __LINE__)

bool logsInit();
void logsInitDebug();
void logsClose();
