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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

namespace Core {
class Launcher;
} // namespace Core

namespace Logs {

void start(not_null<Core::Launcher*> launcher);
bool started();
void finish();

bool instanceChecked();
void multipleInstances();

void closeMain();

void writeMain(const QString &v);

void writeDebug(const char *file, int32 line, const QString &v);
void writeTcp(const QString &v);
void writeMtp(int32 dc, const QString &v);

QString full();

inline const char *b(bool v) {
	return v ? "[TRUE]" : "[FALSE]";
}

struct MemoryBuffer {
	MemoryBuffer(const void *ptr, uint32 size) : p(ptr), s(size) {
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

inline MemoryBuffer mb(const void *ptr, uint32 size) {
	return MemoryBuffer(ptr, size);
}

} // namespace Logs

#define LOG(msg) (Logs::writeMain(QString msg))
//usage LOG(("log: %1 %2").arg(1).arg(2))

#define DEBUG_LOG(msg) { if (cDebug() || !Logs::started()) Logs::writeDebug(__FILE__, __LINE__, QString msg); }
//usage DEBUG_LOG(("log: %1 %2").arg(1).arg(2))

#define TCP_LOG(msg) { if (cDebug() || !Logs::started()) Logs::writeTcp(QString msg); }
//usage TCP_LOG(("log: %1 %2").arg(1).arg(2))

#define MTP_LOG(dc, msg) { if (cDebug() || !Logs::started()) Logs::writeMtp(dc, QString msg); }
//usage MTP_LOG(dc, ("log: %1 %2").arg(1).arg(2))
