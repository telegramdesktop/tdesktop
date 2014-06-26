/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "pspecific.h"

namespace {
	QFile debugLog, tcpLog, mtpLog, mainLog;
	QTextStream *debugLogStream = 0, *tcpLogStream = 0, *mtpLogStream = 0, *mainLogStream = 0;
	QChar zero('0');

	QMutex debugLogMutex, mainLogMutex;

	class _StreamCreator {
	public:
		~_StreamCreator() {
			logsClose();
		}
	};

	QString debugLogEntryStart() {
		static uint32 logEntry = 0;

		QDateTime tm(QDateTime::currentDateTime());

		QThread *thread = QThread::currentThread();
		MTPThread *mtpThread = dynamic_cast<MTPThread*>(thread);
		uint32 threadId = mtpThread ? mtpThread->getThreadId() : 0;
		
		return QString("[%1 %2-%3]").arg(tm.toString("hh:mm:ss.zzz")).arg(QString("%1").arg(threadId, 2, 10, zero)).arg(++logEntry, 7, 10, zero);
	}
}

void debugLogWrite(const char *file, int32 line, const QString &v) {
	if (!cDebug() || !debugLogStream) return;

	const char *last = strstr(file, "/"), *found = 0;
	while (last) {
		found = last;
		last = strstr(last + 1, "/");
	}
	last = strstr(file, "\\");
	while (last) {
		found = last;
		last = strstr(last + 1, "\\");
	}
	if (found) {
		file = found + 1;
	}

	{
		QMutexLocker lock(&debugLogMutex);
		QString msg(QString("%1 %2 (%3 : %4)\n").arg(debugLogEntryStart()).arg(v).arg(file).arg(line));
		(*debugLogStream) << msg;
		debugLogStream->flush();
#ifdef Q_OS_WIN
		OutputDebugString(reinterpret_cast<const wchar_t *>(msg.utf16()));
#elif defined Q_OS_MAC
        objc_outputDebugString(msg);
#endif
	}
}

void tcpLogWrite(const QString &v) {
	if (!cDebug() || !tcpLogStream) return;

	{
		QMutexLocker lock(&debugLogMutex);
		(*tcpLogStream) << QString("%1 %2\n").arg(debugLogEntryStart()).arg(v);
		tcpLogStream->flush();
	}
}

void mtpLogWrite(int32 dc, const QString &v) {
	if (!cDebug() || !mtpLogStream) return;

	{
		QMutexLocker lock(&debugLogMutex);
		(*mtpLogStream) << QString("%1 (dc:%2) %3\n").arg(debugLogEntryStart()).arg(dc).arg(v);
		mtpLogStream->flush();
	}
}

void logWrite(const QString &v) {
	if (!mainLog.isOpen()) return;

	time_t t = time(NULL);
	struct tm tm;
    mylocaltime(&tm, &t);

	{
		QMutexLocker lock(&mainLogMutex);
		QString msg(QString("[%1.%2.%3 %4:%5:%6] %7\n").arg(tm.tm_year + 1900).arg(tm.tm_mon + 1, 2, 10, zero).arg(tm.tm_mday, 2, 10, zero).arg(tm.tm_hour, 2, 10, zero).arg(tm.tm_min, 2, 10, zero).arg(tm.tm_sec, 2, 10, zero).arg(v));
		(*mainLogStream) << msg;
		mainLogStream->flush();
	}

	if (cDebug()) debugLogWrite("logs", 0, v);
}

void logsInit() {
	static _StreamCreator streamCreator;
	if (mainLogStream) return;
    
#ifdef Q_OS_MAC
	cForceWorkingDir(psAppDataPath());
#endif

	QString oldDir = cWorkingDir();
	mainLog.setFileName(cWorkingDir() + "log.txt");
	mainLog.open(QIODevice::WriteOnly | QIODevice::Text);
	if (!mainLog.isOpen()) {
		cForceWorkingDir(cExeDir());
		mainLog.setFileName(cWorkingDir() + "log.txt");
		mainLog.open(QIODevice::WriteOnly | QIODevice::Text);
		if (!mainLog.isOpen()) {
			cForceWorkingDir(psAppDataPath());
			mainLog.setFileName(cWorkingDir() + "log.txt");
			mainLog.open(QIODevice::WriteOnly | QIODevice::Text);
		}
	}
	if (mainLog.isOpen()) {
		mainLogStream = new QTextStream();
		mainLogStream->setDevice(&mainLog);
		mainLogStream->setCodec("UTF-8");
	} else {
		cForceWorkingDir(oldDir);
	}
	cForceWorkingDir(QDir(cWorkingDir()).absolutePath() + '/');
	if (cDebug()) logsInitDebug();
}

void logsInitDebug() {
	if (debugLogStream) return;

	time_t t = time(NULL);
	struct tm tm;
    mylocaltime(&tm, &t);

	QString logPrefix = QString("%1%2%3_%4%5%6_").arg(tm.tm_year + 1900).arg(tm.tm_mon + 1, 2, 10, zero).arg(tm.tm_mday, 2, 10, zero).arg(tm.tm_hour, 2, 10, zero).arg(tm.tm_min, 2, 10, zero).arg(tm.tm_sec, 2, 10, zero);

	debugLog.setFileName(cWorkingDir() + "DebugLogs/" + logPrefix + "log.txt");
	if (!debugLog.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QDir dir(QDir::current());
		dir.mkdir(cWorkingDir() + "DebugLogs");
		debugLog.open(QIODevice::WriteOnly | QIODevice::Text);
	}
	if (debugLog.isOpen()) {
		debugLogStream = new QTextStream();
		debugLogStream->setDevice(&debugLog);
		debugLogStream->setCodec("UTF-8");
	}
	tcpLog.setFileName(cWorkingDir() + "DebugLogs/" + logPrefix + "tcp.txt");
	if (tcpLog.open(QIODevice::WriteOnly | QIODevice::Text)) {
		tcpLogStream = new QTextStream();
		tcpLogStream->setDevice(&tcpLog);
		tcpLogStream->setCodec("UTF-8");
	}
	mtpLog.setFileName(cWorkingDir() + "DebugLogs/" + logPrefix + "mtp.txt");
	if (mtpLog.open(QIODevice::WriteOnly | QIODevice::Text)) {
		mtpLogStream = new QTextStream();
		mtpLogStream->setDevice(&mtpLog);
		mtpLogStream->setCodec("UTF-8");
	}
}

void logsClose() {
	if (cDebug()) {
		if (debugLogStream) {
			delete debugLogStream;
			debugLogStream = 0;
			debugLog.close();
		}
		if (tcpLogStream) {
			delete tcpLogStream;
			tcpLogStream = 0;
			tcpLog.close();
		}
		if (mtpLogStream) {
			delete mtpLogStream;
			mtpLogStream = 0;
			mtpLog.close();
		}
	}
	if (mainLogStream) {
		delete mainLogStream;
		mainLogStream = 0;
		mainLog.close();
	}
}

QString logVectorLong(const QVector<MTPlong> &ids) {
	if (!ids.size()) return "[void list]";
	QString idsStr = QString("[%1").arg(ids.cbegin()->v);
	for (QVector<MTPlong>::const_iterator i = ids.cbegin() + 1, e = ids.cend(); i != e; ++i) {
		idsStr += QString(", %2").arg(i->v);
	}
	return idsStr + "]";
}
