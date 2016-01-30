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

QMutex *_logsMutex(LogDataType type, bool clear = false) {
	static QMutex *LogsMutexes = 0;
	if (clear) {
		delete[] LogsMutexes;
		LogsMutexes = 0;
	} else if (!LogsMutexes) {
		LogsMutexes = new QMutex[LogDataCount];
	}
	return &LogsMutexes[type];
}

QString _logsFilePath(LogDataType type, const QString &postfix = QString()) {
	QString path(cWorkingDir());
	switch (type) {
	case LogDataMain: path += qstr("log") + postfix + qstr(".txt"); break;
	case LogDataDebug: path += qstr("DebugLogs/log") + postfix + qstr(".txt"); break;
	case LogDataTcp: path += qstr("DebugLogs/tcp") + postfix + qstr(".txt"); break;
	case LogDataMtp: path += qstr("DebugLogs/mtp") + postfix + qstr(".txt"); break;
	}
	return path;
}

int32 LogsStartIndexChosen = -1;
QString _logsEntryStart() {
	static int32 index = 0;
	QDateTime tm(QDateTime::currentDateTime());

	QThread *thread = QThread::currentThread();
	MTPThread *mtpThread = qobject_cast<MTPThread*>(thread);
	uint threadId = mtpThread ? mtpThread->getThreadId() : 0;

	return QString("[%1 %2-%3]").arg(tm.toString("hh:mm:ss.zzz")).arg(QString("%1").arg(threadId, 2, 10, QChar('0'))).arg(++index, 7, 10, QChar('0'));
}

class LogsDataFields {
public:

	LogsDataFields() {
		for (int32 i = 0; i < LogDataCount; ++i) {
			files[i].reset(new QFile());
		}
	}

	bool openMain() {
		return reopen(LogDataMain, 0, qsl("start"));
	}

	bool instanceChecked() {
		return reopen(LogDataMain, 0, QString());
	}

	QString full() {
		if (!streams[LogDataMain].device()) {
			return QString();
		}

		QFile out(files[LogDataMain]->fileName());
		if (out.open(QIODevice::ReadOnly)) {
			return QString::fromUtf8(out.readAll());
		}
		return QString();
	}

	void write(LogDataType type, const QString &msg) {
		QMutexLocker lock(_logsMutex(type));
		if (type != LogDataMain) reopenDebug();
		if (!streams[type].device()) return;

		streams[type] << msg;
		streams[type].flush();
	}

private:

	QSharedPointer<QFile> files[LogDataCount];
	QTextStream streams[LogDataCount];

	int32 part = -1, index = 0;

	bool reopen(LogDataType type, int32 dayIndex, const QString &postfix) {
		if (streams[type].device()) {
			if (type == LogDataMain) {
				if (!postfix.isEmpty()) {
					return true;
				}
			} else {
				streams[type].setDevice(0);
				files[type]->close();
			}
		}

		QFlags<QIODevice::OpenModeFlag> mode = QIODevice::WriteOnly | QIODevice::Text;
		if (type == LogDataMain) { // we can call LOG() in LogDataMain reopen - mutex not locked
			if (postfix.isEmpty()) { // instance checked, need to move to log.txt
				t_assert(!files[type]->fileName().isEmpty()); // one of log_startXX.txt should've been opened already

				QSharedPointer<QFile> to(new QFile(_logsFilePath(type, postfix)));
				if (to->exists() && !to->remove()) {
					LOG(("Could not delete '%1' file to start new logging!").arg(to->fileName()));
					return false;
				}
				if (!QFile(files[type]->fileName()).copy(to->fileName())) { // don't close files[type] yet
					LOG(("Could not copy '%1' to '%2' to start new logging!").arg(files[type]->fileName()).arg(to->fileName()));
					return false;
				}
				if (to->open(mode | QIODevice::Append)) {
					qSwap(files[type], to);
					streams[type].setDevice(files[type].data());
					streams[type].setCodec("UTF-8");
					LOG(("Moved logging from '%1' to '%2'!").arg(to->fileName()).arg(files[type]->fileName()));
					to->remove();

					LogsStartIndexChosen = -1;

					QDir working(cWorkingDir()); // delete all other log_startXX.txt that we can
					QStringList oldlogs = working.entryList(QStringList("log_start*.txt"), QDir::Files);
					for (QStringList::const_iterator i = oldlogs.cbegin(), e = oldlogs.cend(); i != e; ++i) {
						QString oldlog = cWorkingDir() + *i, oldlogend = i->mid(qstr("log_start").size());
						if (oldlogend.size() == 1 + qstr(".txt").size() && oldlogend.at(0).isDigit() && oldlogend.midRef(1) == qstr(".txt")) {
							bool removed = QFile(*i).remove();
							LOG(("Old start log '%1' found, deleted: %2").arg(*i).arg(Logs::b(removed)));
						}
					}

					return true;
				}
				LOG(("Could not open '%1' file to start new logging!").arg(to->fileName()));
				return false;
			} else {
				bool found = false;
				int32 oldest = -1; // find not existing log_startX.txt or pick the oldest one (by lastModified)
				QDateTime oldestLastModified;
				for (int32 i = 0; i < 10; ++i) {
					QString trying = _logsFilePath(type, qsl("_start%1").arg(i));
					files[type]->setFileName(trying);
					if (!files[type]->exists()) {
						LogsStartIndexChosen = i;
						found = true;
						break;
					}
					QDateTime lastModified = QFileInfo(trying).lastModified();
					if (oldest < 0 || lastModified < oldestLastModified) {
						oldestLastModified = lastModified;
						oldest = i;
					}
				}
				if (!found) {
					files[type]->setFileName(_logsFilePath(type, qsl("_start%1").arg(oldest)));
					LogsStartIndexChosen = oldest;
				}
			}
		} else {
			files[type]->setFileName(_logsFilePath(type, postfix));
			if (files[type]->exists()) {
				if (files[type]->open(QIODevice::ReadOnly | QIODevice::Text)) {
					if (QString::fromUtf8(files[type]->readLine()).toInt() == dayIndex) {
						mode |= QIODevice::Append;
					}
					files[type]->close();
				}
			} else {
				QDir().mkdir(cWorkingDir() + qstr("DebugLogs"));
			}
		}
		if (files[type]->open(mode)) {
			streams[type].setDevice(files[type].data());
			streams[type].setCodec("UTF-8");

			if (type != LogDataMain) {
				streams[type] << ((mode & QIODevice::Append) ? qsl("----------------------------------------------------------------\nNEW LOGGING INSTANCE STARTED!!!\n----------------------------------------------------------------\n") : qsl("%1\n").arg(dayIndex));
				streams[type].flush();
			}

			return true;
		} else if (type != LogDataMain) {
			LOG(("Could not open debug log '%1'!").arg(files[type]->fileName()));
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

QString LogsBeforeSingleInstanceChecked; // LogsInMemory already dumped in LogsData, but LogsData is about to be deleted

void _logsWrite(LogDataType type, const QString &msg) {
	if (LogsData && (type == LogDataMain || LogsStartIndexChosen < 0)) {
		if (type == LogDataMain || cDebug()) {
			LogsData->write(type, msg);
		}
	} else if (LogsInMemory != DeletedLogsInMemory) {
		if (!LogsInMemory) {
			LogsInMemory = new LogsInMemoryList;
		}
		LogsInMemory->push_back(qMakePair(type, msg));
	} else if (!LogsBeforeSingleInstanceChecked.isEmpty() && type == LogDataMain) {
		LogsBeforeSingleInstanceChecked += msg;
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

		QString initialWorkingDir = QDir(cWorkingDir()).absolutePath() + '/', moveOldDataFrom;
		if (cBetaVersion()) {
			cSetDebug(true);
#if (defined Q_OS_MAC || defined Q_OS_LINUX)
		} else {
#ifdef _DEBUG
			cForceWorkingDir(cExeDir());
#else
			if (cWorkingDir().isEmpty()) {
				cForceWorkingDir(psAppDataPath());
			}
#endif
			workingDirChosen = true;

#if (defined Q_OS_LINUX && !defined _DEBUG) // fix first version
			moveOldDataFrom = initialWorkingDir;
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
		QDir().mkpath(cWorkingDir() + qstr("tdata"));

		Global::WorkingDirReady();

		if (!LogsData->openMain()) {
			delete LogsData;
			LogsData = 0;
		}

		LOG(("Launched version: %1, dev: %2, beta: %3, debug mode: %4, test dc: %5").arg(AppVersion).arg(Logs::b(cDevVersion())).arg(cBetaVersion()).arg(Logs::b(cDebug())).arg(Logs::b(cTestMode())));
		LOG(("Executable dir: %1, name: %2").arg(cExeDir()).arg(cExeName()));
		LOG(("Initial working dir: %1").arg(initialWorkingDir));
		LOG(("Working dir: %1").arg(cWorkingDir()));
		LOG(("Arguments: %1").arg(cArguments()));

		if (!LogsData) {
			LOG(("Could not open '%1' for writing log!").arg(_logsFilePath(LogDataMain, qsl("_startXX"))));
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
				if (i->first == LogDataMain) {
					_logsWrite(i->first, i->second);
					LOG(("First: %1, %2").arg(i->first).arg(i->second));
				}
			}
		}

		LOG(("Logs started"));
	}

	Initializer::~Initializer() {
		delete LogsData;
		LogsData = 0;

		if (LogsInMemory && LogsInMemory != DeletedLogsInMemory) {
			delete LogsInMemory;
		}
		LogsInMemory = DeletedLogsInMemory;

		_logsMutex(LogDataMain, true);
	}

	bool started() {
		return LogsData != 0;
	}

	bool instanceChecked() {
		if (!LogsData) return false;

		if (!LogsData->instanceChecked()) {
			LogsBeforeSingleInstanceChecked = Logs::full();

			delete LogsData;
			LogsData = 0;
			LOG(("Could not move logging to '%1'!").arg(_logsFilePath(LogDataMain)));
			return false;
		}



		if (LogsInMemory) {
			t_assert(LogsInMemory != DeletedLogsInMemory);
			LogsInMemoryList list = *LogsInMemory;
			for (LogsInMemoryList::const_iterator i = list.cbegin(), e = list.cend(); i != e; ++i) {
				if (i->first != LogDataMain) {
					_logsWrite(i->first, i->second);
				}
			}
		}
		if (LogsInMemory) {
			t_assert(LogsInMemory != DeletedLogsInMemory);
			delete LogsInMemory;
		}
		LogsInMemory = DeletedLogsInMemory;

		DEBUG_LOG(("Debug logs started."));
		LogsBeforeSingleInstanceChecked.clear();
		return true;
	}

	void multipleInstances() {
		if (LogsInMemory) {
			t_assert(LogsInMemory != DeletedLogsInMemory);
			delete LogsInMemory;
		}
		LogsInMemory = DeletedLogsInMemory;

		if (cDebug()) {
			LOG(("WARNING: debug logs are not written in multiple instances mode!"));
		}
		LogsBeforeSingleInstanceChecked.clear();
	}

	void writeMain(const QString &v) {
		time_t t = time(NULL);
		struct tm tm;
		mylocaltime(&tm, &t);

		QString msg(QString("[%1.%2.%3 %4:%5:%6] %7\n").arg(tm.tm_year + 1900).arg(tm.tm_mon + 1, 2, 10, QChar('0')).arg(tm.tm_mday, 2, 10, QChar('0')).arg(tm.tm_hour, 2, 10, QChar('0')).arg(tm.tm_min, 2, 10, QChar('0')).arg(tm.tm_sec, 2, 10, QChar('0')).arg(v));
		_logsWrite(LogDataMain, msg);

		QString debugmsg(QString("%1 %2\n").arg(_logsEntryStart()).arg(v));
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

		QString msg(QString("%1 %2 (%3 : %4)\n").arg(_logsEntryStart()).arg(v).arg(file).arg(line));
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
		QString msg(QString("%1 %2\n").arg(_logsEntryStart()).arg(v));
		_logsWrite(LogDataTcp, msg);
	}

	void writeMtp(int32 dc, const QString &v) {
		QString msg(QString("%1 (dc:%2) %3\n").arg(_logsEntryStart()).arg(dc).arg(v));
		_logsWrite(LogDataMtp, msg);
	}

	QString full() {
		if (LogsData) {
			return LogsData->full();
		}
		if (!LogsInMemory || LogsInMemory == DeletedLogsInMemory) {
			return LogsBeforeSingleInstanceChecked;
		}

		int32 size = 0;
		for (LogsInMemoryList::const_iterator i = LogsInMemory->cbegin(), e = LogsInMemory->cend(); i != e; ++i) {
			if (i->first == LogDataMain) {
				size += i->second.size();
			}
		}
		QString result;
		result.reserve(size);
		for (LogsInMemoryList::const_iterator i = LogsInMemory->cbegin(), e = LogsInMemory->cend(); i != e; ++i) {
			if (i->first == LogDataMain) {
				result += i->second;
			}
		}
		return result;
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

namespace SignalHandlers {

	QByteArray CrashDumpPath;
	FILE *CrashDumpFile = 0;
	int CrashDumpFileNo = 0;
	char LaunchedDateTimeStr[32] = { 0 };
	char LaunchedBinaryName[256] = { 0 };

	void _writeChar(char ch) {
		fwrite(&ch, 1, 1, CrashDumpFile);
	}

	dump::~dump() {
		if (CrashDumpFile) {
			fflush(CrashDumpFile);
		}
	}

	const dump &operator<<(const dump &stream, const char *str) {
		if (!CrashDumpFile) return stream;

		fwrite(str, 1, strlen(str), CrashDumpFile);
		return stream;
	}

	template <typename Type>
	const dump &_writeNumber(const dump &stream, Type number) {
		if (!CrashDumpFile) return stream;

		if (number < 0) {
			_writeChar('-');
			number = -number;
		}
		Type upper = 1, prev = number / 10;
		while (prev >= upper) {
			upper *= 10;
		}
		while (upper > 0) {
			int digit = (number / upper);
			_writeChar('0' + digit);
			number -= digit * upper;
			upper /= 10;
		}
		return stream;
	}

	const dump &operator<<(const dump &stream, int num) {
		return _writeNumber(stream, num);
	}

	const dump &operator<<(const dump &stream, DWORD num) {
		return _writeNumber(stream, num);
	}

	const dump &operator<<(const dump &stream, DWORD64 num) {
		return _writeNumber(stream, num);
	}

	Qt::HANDLE LoggingCrashThreadId = 0;
	bool LoggingCrashHeaderWritten = false;
	QMutex LoggingCrashMutex;

	void Handler(int signum) {
		const char* name = 0;
		switch (signum) {
		case SIGABRT: name = "SIGABRT"; break;
		case SIGSEGV: name = "SIGSEGV"; break;
		case SIGILL: name = "SIGILL"; break;
		case SIGFPE: name = "SIGFPE"; break;
#ifndef Q_OS_WIN
		case SIGBUS: name = "SIGBUS"; break;
		case SIGSYS: name = "SIGSYS"; break;
#endif
		}

		Qt::HANDLE thread = QThread::currentThreadId();
		if (thread == LoggingCrashThreadId) return;

		QMutexLocker lock(&LoggingCrashMutex);
		LoggingCrashThreadId = thread;

		if (!LoggingCrashHeaderWritten) {
			LoggingCrashHeaderWritten = true;
			dump() << "Binary: " << LaunchedBinaryName << "\n";
			dump() << "ApiId: " << ApiId << "\n";
			if (cBetaVersion()) {
				dump() << "Version: " << cBetaVersion() << " beta\n";
			} else {
				dump() << "Version: " << AppVersion;
				if (cDevVersion()) {
					dump() << " dev\n";
				} else {
					dump() << "\n";
				}
			}
			dump() << "Launched: " << LaunchedDateTimeStr << "\n";
			dump() << "Platform: ";
			switch (cPlatform()) {
			case dbipWindows: dump() << "win"; break;
			case dbipMac: dump() << "mac"; break;
			case dbipMacOld: dump() << "macold"; break;
			case dbipLinux64: dump() << "linux64"; break;
			case dbipLinux32: dump() << "linux32"; break;
			}
			dump() << "\n";
			psWriteDump();
			dump() << "\n";
		}
		if (name) {
			dump() << "Caught signal " << signum << " (" << name << ") in thread " << uint64(thread) << "\n";
		} else {
			dump() << "Caught signal " << signum << " in thread " << uint64(thread) << "\n";
		}

		dump() << "\nBacktrace:\n";
		psWriteStackTrace(CrashDumpFileNo);
		dump() << "\n";

		LoggingCrashThreadId = 0;
	}

	Status start() {
		CrashDumpPath = (cWorkingDir() + qsl("tdata/working")).toUtf8();
		if (FILE *f = fopen(CrashDumpPath.constData(), "rb")) {
			QByteArray lastdump;
			char buffer[64 * 1024] = { 0 };
			int32 read = 0;
			while ((read = fread(buffer, 1, 64 * 1024, f)) > 0) {
				lastdump.append(buffer, read);
			}
			fclose(f);

			Global::SetLastCrashDump(lastdump);

			LOG(("Opened '%1' for reading, the previous Telegram Desktop launch was not finished properly :( Crash log size: %2").arg(QString::fromUtf8(CrashDumpPath)).arg(lastdump.size()));

			return LastCrashed;
		}
		return restart();
	}

	Status restart() {
		if (CrashDumpFile) {
			return Started;
		}

		CrashDumpFile = fopen(CrashDumpPath.constData(), "wb");
		if (CrashDumpFile) {
			CrashDumpFileNo = fileno(CrashDumpFile);

			QByteArray launchedDateTime = QDateTime::currentDateTime().toString("dd.MM.yyyy hh:mm:ss").toUtf8();
			t_assert(launchedDateTime.size() < sizeof(LaunchedDateTimeStr));
			memcpy(LaunchedDateTimeStr, launchedDateTime.constData(), launchedDateTime.size());

			QByteArray launchedBinaryName = cExeName().toUtf8();
			t_assert(launchedBinaryName.size() < sizeof(LaunchedBinaryName));
			memcpy(LaunchedBinaryName, launchedBinaryName.constData(), launchedBinaryName.size());

			signal(SIGABRT, SignalHandlers::Handler);
			signal(SIGSEGV, SignalHandlers::Handler);
			signal(SIGILL, SignalHandlers::Handler);
			signal(SIGFPE, SignalHandlers::Handler);
#ifndef Q_OS_WIN
			signal(SIGBUS, SignalHandlers::Handler);
			signal(SIGSYS, SignalHandlers::Handler);
#endif
			return Started;
		}

		LOG(("Could not open '%1' for writing!").arg(QString::fromUtf8(CrashDumpPath)));

		return CantOpen;
	}

	void finish() {
		if (CrashDumpFile) {
			fclose(CrashDumpFile);
			unlink(CrashDumpPath.constData());
		}
	}

}
