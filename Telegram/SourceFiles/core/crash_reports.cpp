/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/crash_reports.h"

#include "platform/platform_specific.h"
#include "base/platform/base_platform_info.h"
#include "core/launcher.h"

#include <signal.h>
#include <new>
#include <mutex>

#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
#ifdef Q_OS_WIN

#include <new.h>

#pragma warning(push)
#pragma warning(disable:4091)
#include <client/windows/handler/exception_handler.h>
#pragma warning(pop)

#else // Q_OS_WIN

#include <execinfo.h>
#include <sys/syscall.h>

#ifdef Q_OS_MAC

#include <dlfcn.h>
#include <unistd.h>

#ifdef MAC_USE_BREAKPAD
#include <client/mac/handler/exception_handler.h>
#else // MAC_USE_BREAKPAD
#include <client/crashpad_client.h>
#endif // else for MAC_USE_BREAKPAD

#else // Q_OS_MAC

#include <client/linux/handler/exception_handler.h>

#endif // Q_OS_MAC

#endif // Q_OS_WIN
#endif // !TDESKTOP_DISABLE_CRASH_REPORTS

namespace CrashReports {
namespace {

using Annotations = std::map<std::string, std::string>;
using AnnotationRefs = std::map<std::string, const QString*>;

Annotations ProcessAnnotations;
AnnotationRefs ProcessAnnotationRefs;

#ifndef TDESKTOP_DISABLE_CRASH_REPORTS

QString ReportPath;
FILE *ReportFile = nullptr;
int ReportFileNo = 0;

void SafeWriteChar(char ch) {
	fwrite(&ch, 1, 1, ReportFile);
}

template <bool Unsigned, typename Type>
struct writeNumberSignAndRemoveIt {
	static void call(Type &number) {
		if (number < 0) {
			SafeWriteChar('-');
			number = -number;
		}
	}
};
template <typename Type>
struct writeNumberSignAndRemoveIt<true, Type> {
	static void call(Type &number) {
	}
};

template <typename Type>
const dump &SafeWriteNumber(const dump &stream, Type number) {
	if (!ReportFile) return stream;

	writeNumberSignAndRemoveIt<(Type(-1) > Type(0)), Type>::call(number);
	Type upper = 1, prev = number / 10;
	while (prev >= upper) {
		upper *= 10;
	}
	while (upper > 0) {
		int digit = (number / upper);
		SafeWriteChar('0' + digit);
		number -= digit * upper;
		upper /= 10;
	}
	return stream;
}

using ReservedMemoryChunk = std::array<gsl::byte, 1024 * 1024>;
std::unique_ptr<ReservedMemoryChunk> ReservedMemory;

void InstallOperatorNewHandler() {
	ReservedMemory = std::make_unique<ReservedMemoryChunk>();
#ifdef Q_OS_WIN
	_set_new_handler([](size_t requested) -> int {
		_set_new_handler(nullptr);
		ReservedMemory.reset();
		CrashReports::SetAnnotation("Requested", QString::number(requested));
		Unexpected("Could not allocate!");
	});
#else // Q_OS_WIN
	std::set_new_handler([] {
		std::set_new_handler(nullptr);
		ReservedMemory.reset();
		Unexpected("Could not allocate!");
	});
#endif // Q_OS_WIN
}

void InstallQtMessageHandler() {
	static QtMessageHandler original = nullptr;
	original = qInstallMessageHandler([](
			QtMsgType type,
			const QMessageLogContext &context,
			const QString &message) {
		if (original) {
			original(type, context, message);
		}
		if (type == QtFatalMsg) {
			CrashReports::SetAnnotation("QtFatal", message);
			Unexpected("Qt FATAL message was generated!");
		}
	});
}

std::atomic<Qt::HANDLE> ReportingThreadId/* = nullptr*/;
bool ReportingHeaderWritten/* = false*/;
const char *BreakpadDumpPath/* = nullptr*/;
const wchar_t *BreakpadDumpPathW/* = nullptr*/;

void WriteReportHeader() {
	if (ReportingHeaderWritten) {
		return;
	}
	ReportingHeaderWritten = true;
	const auto dec2hex = [](int value) -> char {
		if (value >= 0 && value < 10) {
			return '0' + value;
		} else if (value >= 10 && value < 16) {
			return 'a' + (value - 10);
		}
		return '#';
	};
	for (const auto &i : ProcessAnnotationRefs) {
		QByteArray utf8 = i.second->toUtf8();
		std::string wrapped;
		wrapped.reserve(4 * utf8.size());
		for (auto ch : utf8) {
			auto uch = static_cast<uchar>(ch);
			wrapped.append("\\x", 2).append(1, dec2hex(uch >> 4)).append(1, dec2hex(uch & 0x0F));
		}
		ProcessAnnotations[i.first] = wrapped;
	}
	for (const auto &i : ProcessAnnotations) {
		dump() << i.first.c_str() << ": " << i.second.c_str() << "\n";
	}
	Platform::WriteCrashDumpDetails();
	dump() << "\n";
}

void WriteReportInfo(int signum, const char *name) {
	WriteReportHeader();

	const auto thread = ReportingThreadId.load();
	if (name) {
		dump() << "Caught signal " << signum << " (" << name << ") in thread " << uint64(thread) << "\n";
	} else if (signum == -1) {
		dump() << "Google Breakpad caught a crash, minidump written in thread " << uint64(thread) << "\n";
		if (BreakpadDumpPath) {
			dump() << "Minidump: " << BreakpadDumpPath << "\n";
		} else if (BreakpadDumpPathW) {
			dump() << "Minidump: " << BreakpadDumpPathW << "\n";
		}
	} else {
		dump() << "Caught signal " << signum << " in thread " << uint64(thread) << "\n";
	}
}

const int HandledSignals[] = {
	SIGSEGV,
	SIGABRT,
	SIGFPE,
	SIGILL,
#ifndef Q_OS_WIN
	SIGBUS,
	SIGTRAP,
#endif // !Q_OS_WIN
};

#ifdef Q_OS_WIN
void SignalHandler(int signum) {
#else // Q_OS_WIN
struct sigaction OldSigActions[32]/* = { 0 }*/;

void RestoreSignalHandlers() {
	for (const auto signum : HandledSignals) {
		sigaction(signum, &OldSigActions[signum], nullptr);
	}
}

void InvokeOldSignalHandler(int signum, siginfo_t *info, void *ucontext) {
	if (signum < 0 || signum > 31) {
		return;
	} else if (OldSigActions[signum].sa_flags & SA_SIGINFO) {
		if (OldSigActions[signum].sa_sigaction) {
			OldSigActions[signum].sa_sigaction(signum, info, ucontext);
		}
	} else {
		if (OldSigActions[signum].sa_handler) {
			OldSigActions[signum].sa_handler(signum);
		}
	}
}

void SignalHandler(int signum, siginfo_t *info, void *ucontext) {
	RestoreSignalHandlers();

#endif // else for Q_OS_WIN

	const char* name = 0;
	switch (signum) {
	case SIGABRT: name = "SIGABRT"; break;
	case SIGSEGV: name = "SIGSEGV"; break;
	case SIGILL: name = "SIGILL"; break;
	case SIGFPE: name = "SIGFPE"; break;
#ifndef Q_OS_WIN
	case SIGBUS: name = "SIGBUS"; break;
	case SIGSYS: name = "SIGSYS"; break;
#endif // !Q_OS_WIN
	}

	auto expected = Qt::HANDLE(nullptr);
	const auto thread = QThread::currentThreadId();

	if (ReportingThreadId.compare_exchange_strong(expected, thread)) {
		WriteReportInfo(signum, name);
		ReportingThreadId = nullptr;
	}

#ifndef Q_OS_WIN
	InvokeOldSignalHandler(signum, info, ucontext);
#endif // !Q_OS_WIN
}

bool SetSignalHandlers = true;
bool CrashLogged = false;
#if !defined Q_OS_MAC || defined MAC_USE_BREAKPAD
google_breakpad::ExceptionHandler* BreakpadExceptionHandler = 0;

#ifdef Q_OS_WIN
bool DumpCallback(const wchar_t* _dump_dir, const wchar_t* _minidump_id, void* context, EXCEPTION_POINTERS* exinfo, MDRawAssertionInfo* assertion, bool success)
#elif defined Q_OS_MAC // Q_OS_WIN
bool DumpCallback(const char* _dump_dir, const char* _minidump_id, void *context, bool success)
#else // Q_OS_MAC
bool DumpCallback(const google_breakpad::MinidumpDescriptor &md, void *context, bool success)
#endif // else for Q_OS_WIN || Q_OS_MAC
{
	if (CrashLogged) return success;
	CrashLogged = true;

#ifdef Q_OS_WIN
	BreakpadDumpPathW = _minidump_id;
	SignalHandler(-1);
#else // Q_OS_WIN

#ifdef Q_OS_MAC
	BreakpadDumpPath = _minidump_id;
#else // Q_OS_MAC
	BreakpadDumpPath = md.path();
#endif // else for Q_OS_MAC
	SignalHandler(-1, 0, 0);
#endif // else for Q_OS_WIN
	return success;
}
#endif // !Q_OS_MAC || MAC_USE_BREAKPAD

#endif // !TDESKTOP_DISABLE_CRASH_REPORTS

} // namespace

QString PlatformString() {
	if (Platform::IsWindowsStoreBuild()) {
		return Platform::IsWindowsARM64()
			? u"WinStoreARM64"_q
			: Platform::IsWindows64Bit()
			? u"WinStore64Bit"_q
			: u"WinStore32Bit"_q;
	} else if (Platform::IsWindows32Bit()) {
		return u"Windows32Bit"_q;
	} else if (Platform::IsWindows64Bit()) {
		return u"Windows64Bit"_q;
	} else if (Platform::IsWindowsARM64()) {
		return u"WindowsARM64"_q;
	} else if (Platform::IsMacStoreBuild()) {
		return u"MacAppStore"_q;
	} else if (Platform::IsMac()) {
		return u"MacOS"_q;
	} else if (Platform::IsLinux()) {
		return u"Linux"_q;
	}
	Unexpected("Platform in CrashReports::PlatformString.");
}

void StartCatching() {
#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
	ProcessAnnotations["Binary"] = cExeName().toUtf8().constData();
	ProcessAnnotations["ApiId"] = QString::number(ApiId).toUtf8().constData();
	ProcessAnnotations["Version"] = (cAlphaVersion()
		? u"%1 alpha"_q.arg(cAlphaVersion())
		: (AppBetaVersion
			? u"%1 beta"_q
			: u"%1"_q).arg(AppVersion)).toUtf8().constData();
	ProcessAnnotations["Launched"] = QDateTime::currentDateTime().toString("dd.MM.yyyy hh:mm:ss").toUtf8().constData();
	ProcessAnnotations["Platform"] = PlatformString().toUtf8().constData();
	ProcessAnnotations["UserTag"] = QString::number(Core::Launcher::Instance().installationTag(), 16).toUtf8().constData();

	QString dumpspath = cWorkingDir() + u"tdata/dumps"_q;
	QDir().mkpath(dumpspath);

#ifdef Q_OS_WIN
	BreakpadExceptionHandler = new google_breakpad::ExceptionHandler(
		dumpspath.toStdWString(),
		google_breakpad::ExceptionHandler::FilterCallback(nullptr),
		DumpCallback,
		(void*)nullptr, // callback_context
		google_breakpad::ExceptionHandler::HANDLER_ALL,
		MINIDUMP_TYPE(MiniDumpNormal),
		// MINIDUMP_TYPE(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithProcessThreadData | MiniDumpWithFullMemoryInfo | MiniDumpWithUnloadedModules | MiniDumpWithFullAuxiliaryState | MiniDumpIgnoreInaccessibleMemory | MiniDumpWithTokenInformation),
		(const wchar_t*)nullptr, // pipe_name
		(const google_breakpad::CustomClientInfo*)nullptr
	);
#elif defined Q_OS_MAC // Q_OS_WIN

#ifdef MAC_USE_BREAKPAD
#ifndef _DEBUG
	BreakpadExceptionHandler = new google_breakpad::ExceptionHandler(
		QFile::encodeName(dumpspath).toStdString(),
		/*FilterCallback*/ 0,
		DumpCallback,
		/*context*/ 0,
		true,
		0
	);
#endif // !_DEBUG
	SetSignalHandlers = false;
#else // MAC_USE_BREAKPAD
	crashpad::CrashpadClient crashpad_client;
	std::string handler = (cExeDir() + cExeName() + u"/Contents/Helpers/crashpad_handler"_q).toUtf8().constData();
	std::string database = QFile::encodeName(dumpspath).constData();
	if (crashpad_client.StartHandler(
			base::FilePath(handler),
			base::FilePath(database),
			{}, // metrics_dir
			std::string(), // url
			ProcessAnnotations,
			std::vector<std::string>(), // arguments
			false, // restartable
			false)) { // asynchronous_start
	}
#endif // else for MAC_USE_BREAKPAD
#else
	BreakpadExceptionHandler = new google_breakpad::ExceptionHandler(
		google_breakpad::MinidumpDescriptor(QFile::encodeName(dumpspath).toStdString()),
		/*FilterCallback*/ 0,
		DumpCallback,
		/*context*/ 0,
		true,
		-1
	);
#endif // else for Q_OS_WIN || Q_OS_MAC
#endif // !TDESKTOP_DISABLE_CRASH_REPORTS
}

void FinishCatching() {
#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
#if !defined Q_OS_MAC || defined MAC_USE_BREAKPAD

	delete base::take(BreakpadExceptionHandler);

#endif // !Q_OS_MAC || MAC_USE_BREAKPAD
#endif // !TDESKTOP_DISABLE_CRASH_REPORTS
}

StartResult Start() {
#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
	ReportPath = cWorkingDir() + u"tdata/working"_q;

#ifdef Q_OS_WIN
	FILE *f = nullptr;
	if (_wfopen_s(&f, ReportPath.toStdWString().c_str(), L"rb") != 0) {
		f = nullptr;
	} else {
#else // !Q_OS_WIN
	if (FILE *f = fopen(QFile::encodeName(ReportPath).constData(), "rb")) {
#endif // else for !Q_OS_WIN
		QByteArray lastdump;
		char buffer[256 * 1024] = { 0 };
		int32 read = fread(buffer, 1, 256 * 1024, f);
		if (read > 0) {
			lastdump.append(buffer, read);
		}
		fclose(f);

		LOG(("Opened '%1' for reading, the previous "
			"Telegram Desktop launch was not finished properly :( "
			"Crash log size: %2").arg(ReportPath).arg(lastdump.size()));

		return lastdump;
	}

#endif // !TDESKTOP_DISABLE_CRASH_REPORTS
	return Restart();
}

Status Restart() {
#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
	if (ReportFile) {
		return Started;
	}

#ifdef Q_OS_WIN
	if (_wfopen_s(&ReportFile, ReportPath.toStdWString().c_str(), L"wb") != 0) {
		ReportFile = nullptr;
	}
#else // Q_OS_WIN
	ReportFile = fopen(QFile::encodeName(ReportPath).constData(), "wb");
#endif // else for Q_OS_WIN
	if (ReportFile) {
#ifdef Q_OS_WIN
		ReportFileNo = _fileno(ReportFile);
#else // Q_OS_WIN
		ReportFileNo = fileno(ReportFile);
#endif // else for Q_OS_WIN
		if (SetSignalHandlers) {
#ifndef Q_OS_WIN
			struct sigaction sigact;

			sigact.sa_sigaction = SignalHandler;
			sigemptyset(&sigact.sa_mask);
			sigact.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;

			for (const auto signum : HandledSignals) {
				sigaction(signum, &sigact, &OldSigActions[signum]);
			}
#else // !Q_OS_WIN
			for (const auto signum : HandledSignals) {
				signal(signum, SignalHandler);
			}
#endif // else for !Q_OS_WIN
		}

		InstallOperatorNewHandler();
		InstallQtMessageHandler();

		return Started;
	}

	LOG(("FATAL: Could not open '%1' for writing!").arg(ReportPath));

	return CantOpen;
#else // !TDESKTOP_DISABLE_CRASH_REPORTS
	return Started;
#endif // else for !TDESKTOP_DISABLE_CRASH_REPORTS
}

void Finish() {
#ifndef TDESKTOP_DISABLE_CRASH_REPORTS
	FinishCatching();

	if (ReportFile) {
		fclose(ReportFile);
		ReportFile = nullptr;

#ifdef Q_OS_WIN
		_wunlink(ReportPath.toStdWString().c_str());
#else // Q_OS_WIN
		unlink(ReportPath.toUtf8().constData());
#endif // else for Q_OS_WIN
	}
#endif // !TDESKTOP_DISABLE_CRASH_REPORTS
}

void SetAnnotation(const std::string &key, const QString &value) {
	static QMutex mutex;
	QMutexLocker lock(&mutex);

	if (!value.trimmed().isEmpty()) {
		ProcessAnnotations[key] = value.toUtf8().constData();
	} else {
		ProcessAnnotations.erase(key);
	}
}

void SetAnnotationHex(const std::string &key, const QString &value) {
	if (value.isEmpty()) {
		return SetAnnotation(key, value);
	}
	const auto utf = value.toUtf8();
	auto buffer = std::string();
	buffer.reserve(4 * utf.size());
	const auto hexDigit = [](std::uint8_t value) {
		if (value >= 10) {
			return 'A' + (value - 10);
		}
		return '0' + value;
	};
	const auto appendHex = [&](std::uint8_t value) {
		buffer.push_back('\\');
		buffer.push_back('x');
		buffer.push_back(hexDigit(value / 16));
		buffer.push_back(hexDigit(value % 16));
	};
	for (const auto ch : utf) {
		appendHex(ch);
	}
	ProcessAnnotations[key] = std::move(buffer);
}

void SetAnnotationRef(const std::string &key, const QString *valuePtr) {
	static QMutex mutex;
	QMutexLocker lock(&mutex);

	if (valuePtr) {
		ProcessAnnotationRefs[key] = valuePtr;
	} else {
		ProcessAnnotationRefs.erase(key);
	}
}

#ifndef TDESKTOP_DISABLE_CRASH_REPORTS

dump::~dump() {
	if (ReportFile) {
		fflush(ReportFile);
	}
}

const dump &operator<<(const dump &stream, const char *str) {
	if (!ReportFile) return stream;

	fwrite(str, 1, strlen(str), ReportFile);
	return stream;
}

const dump &operator<<(const dump &stream, const wchar_t *str) {
	if (!ReportFile) return stream;

	for (int i = 0, l = wcslen(str); i < l; ++i) {
		if (
#if !defined(__WCHAR_UNSIGNED__)
			str[i] >= 0 &&
#endif
			str[i] < 128) {
			SafeWriteChar(char(str[i]));
		} else {
			SafeWriteChar('?');
		}
	}
	return stream;
}

const dump &operator<<(const dump &stream, int num) {
	return SafeWriteNumber(stream, num);
}

const dump &operator<<(const dump &stream, unsigned int num) {
	return SafeWriteNumber(stream, num);
}

const dump &operator<<(const dump &stream, unsigned long num) {
	return SafeWriteNumber(stream, num);
}

const dump &operator<<(const dump &stream, unsigned long long num) {
	return SafeWriteNumber(stream, num);
}

const dump &operator<<(const dump &stream, double num) {
	if (num < 0) {
		SafeWriteChar('-');
		num = -num;
	}
	SafeWriteNumber(stream, uint64(floor(num)));
	SafeWriteChar('.');
	num -= floor(num);
	for (int i = 0; i < 4; ++i) {
		num *= 10;
		int digit = int(floor(num));
		SafeWriteChar('0' + digit);
		num -= digit;
	}
	return stream;
}

#endif // TDESKTOP_DISABLE_CRASH_REPORTS

} // namespace CrashReports
