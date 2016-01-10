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
#include "stdafx.h"
#include <iostream>
#include "pspecific.h"

namespace {
	QFile debugLog, tcpLog, mtpLog, mainLog;
	QTextStream *debugLogStream = 0, *tcpLogStream = 0, *mtpLogStream = 0, *mainLogStream = 0;
	int32 part = -1;
	QChar zero('0');

	QMutex debugLogMutex, mainLogMutex;

	QString debugLogEntryStart() {
		static uint32 logEntry = 0;

		QDateTime tm(QDateTime::currentDateTime());

		QThread *thread = QThread::currentThread();
		MTPThread *mtpThread = qobject_cast<MTPThread*>(thread);
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
		if (!cDebug() || !debugLogStream) return;

		logsInitDebug(); // maybe need to reopen new file

		QString msg(QString("%1 %2 (%3 : %4)\n").arg(debugLogEntryStart()).arg(v).arg(file).arg(line));
		(*debugLogStream) << msg;
		debugLogStream->flush();
#ifdef Q_OS_WIN
//		OutputDebugString(reinterpret_cast<const wchar_t *>(msg.utf16()));
#elif defined Q_OS_MAC
        objc_outputDebugString(msg);
#elif defined Q_OS_LINUX && defined _DEBUG
//        std::cout << msg.toUtf8().constData();
#endif
	}
}

void tcpLogWrite(const QString &v) {
	if (!cDebug() || !tcpLogStream) return;

	{
		QMutexLocker lock(&debugLogMutex);
		if (!cDebug() || !tcpLogStream) return;

		logsInitDebug(); // maybe need to reopen new file

		(*tcpLogStream) << QString("%1 %2\n").arg(debugLogEntryStart()).arg(v);
		tcpLogStream->flush();
	}
}

void mtpLogWrite(int32 dc, const QString &v) {
	if (!cDebug() || !mtpLogStream) return;

	{
		QMutexLocker lock(&debugLogMutex);
		if (!cDebug() || !mtpLogStream) return;

		logsInitDebug(); // maybe need to reopen new file

		(*mtpLogStream) << QString("%1 (dc:%2) %3\n").arg(debugLogEntryStart()).arg(dc).arg(v);
		mtpLogStream->flush();
	}
}

void logWrite(const QString &v) {
	if (!mainLogStream) return;

	time_t t = time(NULL);
	struct tm tm;
    mylocaltime(&tm, &t);

	{
		QMutexLocker lock(&mainLogMutex);
		if (!mainLogStream) return;

		QString msg(QString("[%1.%2.%3 %4:%5:%6] %7\n").arg(tm.tm_year + 1900).arg(tm.tm_mon + 1, 2, 10, zero).arg(tm.tm_mday, 2, 10, zero).arg(tm.tm_hour, 2, 10, zero).arg(tm.tm_min, 2, 10, zero).arg(tm.tm_sec, 2, 10, zero).arg(v));
		(*mainLogStream) << msg;
		mainLogStream->flush();
	}

	if (cDebug()) debugLogWrite("logs", 0, v);
}

void moveOldDataFiles(const QString &wasDir) {
	QFile data(wasDir + "data"), dataConfig(wasDir + "data_config"), tdataConfig(wasDir + "tdata/config");
	if (data.exists() && dataConfig.exists() && !QFileInfo(cWorkingDir() + "data").exists() && !QFileInfo(cWorkingDir() + "data_config").exists()) { // move to home dir
		LOG(("Copying data to home dir '%1' from '%2'").arg(cWorkingDir()).arg(wasDir));
		if (data.copy(cWorkingDir() + "data")) {
			LOG(("Copied 'data' to home dir"));
			if (dataConfig.copy(cWorkingDir() + "data_config")) {
				LOG(("Copied 'data_config' to home dir"));
				bool tdataGood = true;
				if (tdataConfig.exists()) {
					tdataGood = false;
					QDir().mkpath(cWorkingDir() + "tdata");
					if (tdataConfig.copy(cWorkingDir() + "tdata/config")) {
						LOG(("Copied 'tdata/config' to home dir"));
						tdataGood = true;
					} else {
						LOG(("Copied 'data' and 'data_config', but could not copy 'tdata/config'!"));
					}
				}
				if (tdataGood) {
					if (data.remove()) {
						LOG(("Removed 'data'"));
					} else {
						LOG(("Could not remove 'data'"));
					}
					if (dataConfig.remove()) {
						LOG(("Removed 'data_config'"));
					} else {
						LOG(("Could not remove 'data_config'"));
					}
					if (!tdataConfig.exists() || tdataConfig.remove()) {
						LOG(("Removed 'tdata/config'"));
						LOG(("Could not remove 'tdata/config'"));
					} else {
					}
					QDir().rmdir(wasDir + "tdata");
				}
			} else {
				LOG(("Copied 'data', but could not copy 'data_config'!!"));
			}
		} else {
			LOG(("Could not copy 'data'!"));
		}
	}
}

bool logsInit() {
	t_assert(mainLogStream == 0);

	QFile beta(cExeDir() + qsl("TelegramBeta_data/tdata/beta"));
	if (cBetaVersion()) {
		cForceWorkingDir(cExeDir() + qsl("TelegramBeta_data/"));
		if (*BetaPrivateKey) {
			cSetBetaPrivateKey(QByteArray(BetaPrivateKey));
		}
		if (beta.open(QIODevice::WriteOnly)) {
			QDataStream dataStream(&beta);
			dataStream.setVersion(QDataStream::Qt_5_3);
			dataStream << quint64(cRealBetaVersion()) << cBetaPrivateKey();
		} else {
			LOG(("Error: could not open \"beta\" file for writing private key!"));
		}
	} else if (beta.exists()) {
		if (beta.open(QIODevice::ReadOnly)) {
			QDataStream dataStream(&beta);
			dataStream.setVersion(QDataStream::Qt_5_3);

			quint64 v;
			QByteArray k;
			dataStream >> v >> k;
			if (dataStream.status() == QDataStream::Ok) {
				cSetBetaVersion(qMax(v, AppVersion * 1000ULL));
				cSetBetaPrivateKey(k);
				cSetRealBetaVersion(v);

				cForceWorkingDir(cExeDir() + qsl("TelegramBeta_data/"));
			}
		}
	}

	if (cBetaVersion()) {
		cSetDebug(true);
	} else {
		QString wasDir = cWorkingDir();
#if (defined Q_OS_MAC || defined Q_OS_LINUX)

#ifdef _DEBUG
		cForceWorkingDir(cExeDir());
#else
		if(cWorkingDir().isEmpty()){
			cForceWorkingDir(psAppDataPath());
		}
#endif

#if (defined Q_OS_LINUX && !defined _DEBUG) // fix first version
		moveOldDataFiles(wasDir);
#endif

#endif
	}

    QString rightDir = cWorkingDir();
    cForceWorkingDir(rightDir);
	mainLog.setFileName(cWorkingDir() + "log.txt");
	mainLog.open(QIODevice::WriteOnly | QIODevice::Text);
	if (!cBetaVersion() && !mainLog.isOpen()) {
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
        cForceWorkingDir(rightDir);
	}
	cForceWorkingDir(QDir(cWorkingDir()).absolutePath() + '/');

	if (QFile(cWorkingDir() + qsl("tdata/withtestmode")).exists()) {
		cSetTestMode(true);
		LOG(("Switched to test mode!"));
	}

#ifdef Q_OS_WIN
	if (cWorkingDir() == psAppDataPath()) { // fix old "Telegram Win (Unofficial)" version
		moveOldDataFiles(psAppDataPathOld());
	}
#endif

	if (cDebug()) {
		logsInitDebug();
	} else if (QFile(cWorkingDir() + qsl("tdata/withdebug")).exists()) {
		logsInitDebug();
		cSetDebug(true);
	}

	if (cBetaVersion()) {
		cSetDevVersion(false);
	} else if (!cDevVersion() && QFile(cWorkingDir() + qsl("tdata/devversion")).exists()) {
		cSetDevVersion(true);
	}

	QDir().setCurrent(cWorkingDir());
	return true;
}

void logsInitDebug() {
	time_t t = time(NULL);
	struct tm tm;
	mylocaltime(&tm, &t);

	static const int switchEach = 15; // minutes
	int32 newPart = (tm.tm_min + tm.tm_hour * 60) / switchEach;
	if (newPart == part) return;

	part = newPart;

	int32 dayIndex = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
	QString logPostfix = QString("_%4_%5").arg((part * switchEach) / 60, 2, 10, zero).arg((part * switchEach) % 60, 2, 10, zero);

	if (debugLogStream) {
		delete debugLogStream;
		debugLogStream = 0;
		debugLog.close();
	}
	debugLog.setFileName(cWorkingDir() + qsl("DebugLogs/log") + logPostfix + qsl(".txt"));
	QIODevice::OpenMode debugLogMode = QIODevice::WriteOnly | QIODevice::Text;
	if (debugLog.exists()) {
		if (debugLog.open(QIODevice::ReadOnly | QIODevice::Text)) {
			if (QString::fromUtf8(debugLog.readLine()).toInt() == dayIndex) {
				debugLogMode |= QIODevice::Append;
			}
			debugLog.close();
		}
	}
	if (!debugLog.open(debugLogMode)) {
		QDir dir(QDir::current());
		dir.mkdir(cWorkingDir() + qsl("DebugLogs"));
		debugLog.open(debugLogMode);
	}
	if (debugLog.isOpen()) {
		debugLogStream = new QTextStream();
		debugLogStream->setDevice(&debugLog);
		debugLogStream->setCodec("UTF-8");
		(*debugLogStream) << ((debugLogMode & QIODevice::Append) ? qsl("----------------------------------------------------------------\nNEW LOGGING INSTANCE STARTED!!!\n----------------------------------------------------------------\n") : qsl("%1\n").arg(dayIndex));
		debugLogStream->flush();
	}
	if (tcpLogStream) {
		delete tcpLogStream;
		tcpLogStream = 0;
		tcpLog.close();
	}
	tcpLog.setFileName(cWorkingDir() + qsl("DebugLogs/tcp") + logPostfix + qsl(".txt"));
	QIODevice::OpenMode tcpLogMode = QIODevice::WriteOnly | QIODevice::Text;
	if (tcpLog.exists()) {
		if (tcpLog.open(QIODevice::ReadOnly | QIODevice::Text)) {
			if (QString::fromUtf8(tcpLog.readLine()).toInt() == dayIndex) {
				tcpLogMode |= QIODevice::Append;
			}
			tcpLog.close();
		}
	}
	if (tcpLog.open(tcpLogMode)) {
		tcpLogStream = new QTextStream();
		tcpLogStream->setDevice(&tcpLog);
		tcpLogStream->setCodec("UTF-8");
		(*tcpLogStream) << ((tcpLogMode & QIODevice::Append) ? qsl("----------------------------------------------------------------\nNEW LOGGING INSTANCE STARTED!!!\n----------------------------------------------------------------\n") : qsl("%1\n").arg(dayIndex));
		tcpLogStream->flush();
	}
	if (mtpLogStream) {
		delete mtpLogStream;
		mtpLogStream = 0;
		mtpLog.close();
	}
	mtpLog.setFileName(cWorkingDir() + qsl("DebugLogs/mtp") + logPostfix + qsl(".txt"));
	QIODevice::OpenMode mtpLogMode = QIODevice::WriteOnly | QIODevice::Text;
	if (mtpLog.exists()) {
		if (mtpLog.open(QIODevice::ReadOnly | QIODevice::Text)) {
			if (QString::fromUtf8(mtpLog.readLine()).toInt() == dayIndex) {
				mtpLogMode |= QIODevice::Append;
			}
			mtpLog.close();
		}
	}
	if (mtpLog.open(mtpLogMode)) {
		mtpLogStream = new QTextStream();
		mtpLogStream->setDevice(&mtpLog);
		mtpLogStream->setCodec("UTF-8");
		(*mtpLogStream) << ((mtpLogMode & QIODevice::Append) ? qsl("----------------------------------------------------------------\nNEW LOGGING INSTANCE STARTED!!!\n----------------------------------------------------------------\n") : qsl("%1\n").arg(dayIndex));
		mtpLogStream->flush();
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
	if (!ids.size()) return "[]";
	QString idsStr = QString("[%1").arg(ids.cbegin()->v);
	for (QVector<MTPlong>::const_iterator i = ids.cbegin() + 1, e = ids.cend(); i != e; ++i) {
		idsStr += QString(", %2").arg(i->v);
	}
	return idsStr + "]";
}

QString logVectorLong(const QVector<uint64> &ids) {
	if (!ids.size()) return "[]";
	QString idsStr = QString("[%1").arg(*ids.cbegin());
	for (QVector<uint64>::const_iterator i = ids.cbegin() + 1, e = ids.cend(); i != e; ++i) {
		idsStr += QString(", %2").arg(*i);
	}
	return idsStr + "]";
}
