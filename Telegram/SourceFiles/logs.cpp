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

enum LogDataType {
	LogDataMain,
	LogDataDebug,
	LogDataTcp,
	LogDataMtp,

	LogDataCount
};

QMutex *LogsMutexes = 0;
QMutex *_logsMutex(LogDataType type) {
	if (!LogsMutexes) {
		LogsMutexes = new QMutex[LogDataCount];
	}
	return &LogsMutexes[type];
}
QString _logsFilePath(LogDataType type, const QString &postfix = QString()) {
	QString path(cWorkingDir());
	switch (type) {
	case LogDataMain: path += qstr("log.txt"); break;
	case LogDataDebug: path += qstr("DebugLogs/log") + postfix + qstr(".txt"); break;
	case LogDataTcp: path += qstr("DebugLogs/tcp") + postfix + qstr(".txt"); break;
	case LogDataMtp: path += qstr("DebugLogs/mtp") + postfix + qstr(".txt"); break;
	}
	return path;
}

class LogsDataFields {
public:
	QString entryStart() {
		QDateTime tm(QDateTime::currentDateTime());

		QThread *thread = QThread::currentThread();
		MTPThread *mtpThread = qobject_cast<MTPThread*>(thread);
		uint threadId = mtpThread ? mtpThread->getThreadId() : 0;

		return QString("[%1 %2-%3]").arg(tm.toString("hh:mm:ss.zzz")).arg(QString("%1").arg(threadId, 2, 10, QChar('0'))).arg(++index, 7, 10, QChar('0'));
	}

	bool openMain() {
		QMutexLocker lock(_logsMutex(LogDataMain));
		return reopen(LogDataMain, 0, QString());
	}

	void write(LogDataType type, const QString &msg) {
		QMutexLocker lock(_logsMutex(type));
		if (type != LogDataMain) reopenDebug();
		if (!streams[type].device()) return;

		streams[type] << msg;
		streams[type].flush();
	}

private:

	QFile files[LogDataCount];
	QTextStream streams[LogDataCount];

	int32 part = -1, index = 0;

	bool reopen(LogDataType type, int32 dayIndex, const QString &postfix) {
		if (streams[type].device()) {
			if (type == LogDataMain) {
				return true;
			}
			streams[type].setDevice(0);
			files[type].close();
		}

		files[type].setFileName(_logsFilePath(type, postfix));
		QFlags<QIODevice::OpenModeFlag> mode = QIODevice::WriteOnly | QIODevice::Text;
		if (type != LogDataMain) {
			if (files[type].exists()) {
				if (files[type].open(QIODevice::ReadOnly | QIODevice::Text)) {
					if (QString::fromUtf8(files[type].readLine()).toInt() == dayIndex) {
						mode |= QIODevice::Append;
					}
					files[type].close();
				}
			} else {
				QDir().mkdir(cWorkingDir() + qstr("DebugLogs"));
			}
		}
		if (files[type].open(mode)) {
			streams[type].setDevice(&files[type]);
			streams[type].setCodec("UTF-8");

			if (type != LogDataMain) {
				streams[type] << ((mode & QIODevice::Append) ? qsl("----------------------------------------------------------------\nNEW LOGGING INSTANCE STARTED!!!\n----------------------------------------------------------------\n") : qsl("%1\n").arg(dayIndex));
				streams[type].flush();
			}

			return true;
		}
		return false;
	}

	void reopenDebug() {
		time_t t = time(NULL);
		struct tm tm;
		mylocaltime(&tm, &t);

		static const int switchEach = 15; // minutes
		int32 newPart = (tm.tm_min + tm.tm_hour * 60) / switchEach;
		if (newPart == part) return;

		part = newPart;

		int32 dayIndex = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
		QString postfix = QString("_%4_%5").arg((part * switchEach) / 60, 2, 10, QChar('0')).arg((part * switchEach) % 60, 2, 10, QChar('0'));

		reopen(LogDataDebug, dayIndex, postfix);
		reopen(LogDataTcp, dayIndex, postfix);
		reopen(LogDataMtp, dayIndex, postfix);
	}

};

LogsDataFields *LogsData = 0;

typedef QList<QPair<LogDataType, QString> > LogsInMemoryList;
LogsInMemoryList *LogsInMemory = 0;
LogsInMemoryList *DeletedLogsInMemory = SharedMemoryLocation<LogsInMemoryList, 0>();
void _logsWrite(LogDataType type, const QString &msg) {
	if (LogsData) {
		if (type == LogDataMain || cDebug()) {
			LogsData->write(type, msg);
		}
	} else if (LogsInMemory != DeletedLogsInMemory) {
		if (!LogsInMemory) {
			LogsInMemory = new LogsInMemoryList;
		}
		LogsInMemory->push_back(qMakePair(LogDataMain, msg));
	}
}

void _moveOldDataFiles(const QString &from);

namespace Logs {

	Initializer::Initializer() {
		t_assert(LogsData == 0);

		if (!Global::CheckBetaVersionDir()) {
			return;
		}
		bool workingDirChosen = cBetaVersion();

		QString moveOldDataFrom;
		if (cBetaVersion()) {
			cSetDebug(true);
#if (defined Q_OS_MAC || defined Q_OS_LINUX)
		} else {
			QString wasDir = QDir(cWorkingDir()).absolutePath() + '/';

#ifdef _DEBUG
			cForceWorkingDir(cExeDir());
#else
			if (cWorkingDir().isEmpty()) {
				cForceWorkingDir(psAppDataPath());
			}
#endif
			workingDirChosen = true;

#if (defined Q_OS_LINUX && !defined _DEBUG) // fix first version
			moveOldDataFrom = wasDir;
#endif

#endif
		}

		LogsData = new LogsDataFields();
		if (!workingDirChosen) {
			cForceWorkingDir(cWorkingDir());
			if (!LogsData->openMain()) {
				cForceWorkingDir(cExeDir());
				if (!LogsData->openMain()) {
					cForceWorkingDir(psAppDataPath());
				}
			}
		}

		cForceWorkingDir(QDir(cWorkingDir()).absolutePath() + '/');
		QDir().setCurrent(cWorkingDir());

		Global::WorkingDirReady();

		LOG(("Launched version: %1, dev: %2, beta: %3, debug mode: %4, test dc: %5").arg(AppVersion).arg(Logs::b(cDevVersion())).arg(cBetaVersion()).arg(Logs::b(cDebug())).arg(Logs::b(cTestMode())));
		LOG(("Executable dir: %1, name: %2").arg(cExeDir()).arg(cExeName()));
		LOG(("Working dir: %1").arg(cWorkingDir()));
		LOG(("Arguments: %1").arg(cArguments()));

		if (!LogsData->openMain()) {
			delete LogsData;
			LogsData = 0;
			LOG(("Error: could not open '%1' for writing log!").arg(_logsFilePath(LogDataMain)));
			return;
		}

#ifdef Q_OS_WIN
		if (cWorkingDir() == psAppDataPath()) { // fix old "Telegram Win (Unofficial)" version
			moveOldDataFrom = psAppDataPathOld();
		}
#endif
		if (!moveOldDataFrom.isEmpty()) {
			_moveOldDataFiles(moveOldDataFrom);
		}

		if (LogsInMemory) {
			t_assert(LogsInMemory != DeletedLogsInMemory);
			LogsInMemoryList list = *LogsInMemory;
			for (LogsInMemoryList::const_iterator i = list.cbegin(), e = list.cend(); i != e; ++i) {
				_logsWrite(i->first, i->second);
			}
		}
		if (LogsInMemory) {
			t_assert(LogsInMemory != DeletedLogsInMemory);
			delete LogsInMemory;
			LogsInMemory = DeletedLogsInMemory;
		}

		LOG(("Logs started."));
		DEBUG_LOG(("Debug logs started."));
	}

	Initializer::~Initializer() {
		delete LogsData;
		LogsData = 0;

		delete[] LogsMutexes;
		LogsMutexes = 0;
	}

	bool started() {
		return LogsData != 0;
	}

	void writeMain(const QString &v) {
		time_t t = time(NULL);
		struct tm tm;
		mylocaltime(&tm, &t);

		QString msg(QString("[%1.%2.%3 %4:%5:%6] %7\n").arg(tm.tm_year + 1900).arg(tm.tm_mon + 1, 2, 10, QChar('0')).arg(tm.tm_mday, 2, 10, QChar('0')).arg(tm.tm_hour, 2, 10, QChar('0')).arg(tm.tm_min, 2, 10, QChar('0')).arg(tm.tm_sec, 2, 10, QChar('0')).arg(v));
		_logsWrite(LogDataMain, msg);

		QString debugmsg(QString("%1 %2\n").arg(LogsData->entryStart()).arg(v));
		_logsWrite(LogDataDebug, debugmsg);
	}

	void writeDebug(const char *file, int32 line, const QString &v) {
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

		QString msg(QString("%1 %2 (%3 : %4)\n").arg(LogsData->entryStart()).arg(v).arg(file).arg(line));
		_logsWrite(LogDataDebug, msg);

#ifdef Q_OS_WIN
		//OutputDebugString(reinterpret_cast<const wchar_t *>(msg.utf16()));
#elif defined Q_OS_MAC
		//objc_outputDebugString(msg);
#elif defined Q_OS_LINUX && defined _DEBUG
		//std::cout << msg.toUtf8().constData();
#endif
	}

	void writeTcp(const QString &v) {
		QString msg(QString("%1 %2\n").arg(LogsData->entryStart()).arg(v));
		_logsWrite(LogDataTcp, msg);
	}

	void writeMtp(int32 dc, const QString &v) {
		QString msg(QString("%1 (dc:%2) %3\n").arg(LogsData->entryStart()).arg(dc).arg(v));
		_logsWrite(LogDataMtp, msg);
	}

	QString vector(const QVector<MTPlong> &ids) {
		if (!ids.size()) return "[]";
		QString idsStr = QString("[%1").arg(ids.cbegin()->v);
		for (QVector<MTPlong>::const_iterator i = ids.cbegin() + 1, e = ids.cend(); i != e; ++i) {
			idsStr += QString(", %2").arg(i->v);
		}
		return idsStr + "]";
	}

	QString vector(const QVector<uint64> &ids) {
		if (!ids.size()) return "[]";
		QString idsStr = QString("[%1").arg(*ids.cbegin());
		for (QVector<uint64>::const_iterator i = ids.cbegin() + 1, e = ids.cend(); i != e; ++i) {
			idsStr += QString(", %2").arg(*i);
		}
		return idsStr + "]";
	}

}

void _moveOldDataFiles(const QString &wasDir) {
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
