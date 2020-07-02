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

#ifndef DESKTOP_APP_DISABLE_CRASH_REPORTS

// see https://blog.inventic.eu/2012/08/qt-and-google-breakpad/
#ifdef Q_OS_WIN

#pragma warning(push)
#pragma warning(disable:4091)
#include "client/windows/handler/exception_handler.h"
#pragma warning(pop)

#elif defined Q_OS_MAC // Q_OS_WIN

#include <execinfo.h>
#include <signal.h>
#include <sys/syscall.h>
#include <dlfcn.h>
#include <unistd.h>

#ifdef MAC_USE_BREAKPAD
#include "client/mac/handler/exception_handler.h"
#else // MAC_USE_BREAKPAD
#include "client/crashpad_client.h"
#endif // else for MAC_USE_BREAKPAD

#elif defined Q_OS_UNIX // Q_OS_MAC

#include <execinfo.h>
#include <signal.h>
#include <sys/syscall.h>

#include "client/linux/handler/exception_handler.h"

#endif // Q_OS_UNIX

#endif // !DESKTOP_APP_DISABLE_CRASH_REPORTS

namespace CrashReports {
namespace {

using Annotations = std::map<std::string, std::string>;
using AnnotationRefs = std::map<std::string, const QString*>;

Annotations ProcessAnnotations;
AnnotationRefs ProcessAnnotationRefs;

#ifndef DESKTOP_APP_DISABLE_CRASH_REPORTS

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
	std::set_new_handler([] {
		std::set_new_handler(nullptr);
		ReservedMemory.reset();
		Unexpected("Could not allocate!");
	});
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

Qt::HANDLE ReportingThreadId = nullptr;
bool ReportingHeaderWritten = false;
QMutex ReportingMutex;

const char *BreakpadDumpPath = nullptr;
const wchar_t *BreakpadDumpPathW = nullptr;

#ifdef Q_OS_UNIX
struct sigaction SIG_def[32];

void SignalHandler(int signum, siginfo_t *info, void *ucontext) {
	if (signum > 0) {
		sigaction(signum, &SIG_def[signum], 0);
	}

#else // Q_OS_UNIX
void SignalHandler(int signum) {
#endif // else for Q_OS_UNIX

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

	Qt::HANDLE thread = QThread::currentThreadId();
	if (thread == ReportingThreadId) return;

	QMutexLocker lock(&ReportingMutex);
	ReportingThreadId = thread;

	if (!ReportingHeaderWritten) {
		ReportingHeaderWritten = true;
		auto dec2hex = [](int value) -> char {
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
		psWriteDump();
		dump() << "\n";
	}
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

	// see https://github.com/benbjohnson/bandicoot
#ifdef Q_OS_UNIX
	ucontext_t *uc = (ucontext_t*)ucontext;

	void *caller = 0;
	if (uc) {
#if defined(__APPLE__) && !defined(MAC_OS_X_VERSION_10_6)
		/* OSX < 10.6 */
#if defined(__x86_64__)
		caller = (void*)uc->uc_mcontext->__ss.__rip;
#elif defined(__i386__)
		caller = (void*)uc->uc_mcontext->__ss.__eip;
#else
		caller = (void*)uc->uc_mcontext->__ss.__srr0;
#endif
#elif defined(__APPLE__) && defined(MAC_OS_X_VERSION_10_6)
		/* OSX >= 10.6 */
#if defined(_STRUCT_X86_THREAD_STATE64) && !defined(__i386__)
		caller = (void*)uc->uc_mcontext->__ss.__rip;
#else
		caller = (void*)uc->uc_mcontext->__ss.__eip;
#endif
#elif defined(__linux__)
		/* Linux */
#if defined(__i386__)
		caller = (void*)uc->uc_mcontext.gregs[14]; /* Linux 32 */
#elif defined(__X86_64__) || defined(__x86_64__)
		caller = (void*)uc->uc_mcontext.gregs[16]; /* Linux 64 */
#elif defined(__ia64__) /* Linux IA64 */
		caller = (void*)uc->uc_mcontext.sc_ip;
#endif

#endif
	}

	void *addresses[132] = { 0 };
	size_t size = backtrace(addresses, 128);

	/* overwrite sigaction with caller's address */
	if (caller) {
		for (int i = size; i > 1; --i) {
			addresses[i + 3] = addresses[i];
		}
		addresses[2] = (void*)0x1;
		addresses[3] = caller;
		addresses[4] = (void*)0x1;
	}

#ifdef Q_OS_MAC
	dump() << "\nBase image addresses:\n";
	for (size_t i = 0; i < size; ++i) {
		Dl_info info;
		dump() << i << " ";
		if (dladdr(addresses[i], &info)) {
			dump() << uint64(info.dli_fbase) << " (" << info.dli_fname << ")\n";
		} else {
			dump() << "_unknown_module_\n";
		}
	}
#endif // Q_OS_MAC

	dump() << "\nBacktrace:\n";

	backtrace_symbols_fd(addresses, size, ReportFileNo);

#else // Q_OS_UNIX
	dump() << "\nBacktrace omitted.\n";
#endif // else for Q_OS_UNIX

	dump() << "\n";

	ReportingThreadId = nullptr;
}

bool SetSignalHandlers = true;
bool CrashLogged = false;
#if !defined Q_OS_MAC || defined MAC_USE_BREAKPAD
google_breakpad::ExceptionHandler* BreakpadExceptionHandler = 0;

#ifdef Q_OS_WIN
bool DumpCallback(const wchar_t* _dump_dir, const wchar_t* _minidump_id, void* context, EXCEPTION_POINTERS* exinfo, MDRawAssertionInfo* assertion, bool success)
#elif defined Q_OS_MAC // Q_OS_WIN
bool DumpCallback(const char* _dump_dir, const char* _minidump_id, void *context, bool success)
#elif defined Q_OS_UNIX // Q_OS_MAC
bool DumpCallback(const google_breakpad::MinidumpDescriptor &md, void *context, bool success)
#endif // Q_OS_UNIX
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

#endif // !DESKTOP_APP_DISABLE_CRASH_REPORTS

} // namespace

QString PlatformString() {
	if (Platform::IsWindowsStoreBuild()) {
		return qsl("WinStore");
	} else if (Platform::IsWindows()) {
		return qsl("Windows");
	} else if (Platform::IsMacStoreBuild()) {
		return qsl("MacAppStore");
	} else if (Platform::IsOSXBuild()) {
		return qsl("OSX");
	} else if (Platform::IsMac()) {
		return qsl("MacOS");
	} else if (Platform::IsLinux32Bit()) {
		return qsl("Linux32Bit");
	} else if (Platform::IsLinux64Bit()) {
		return qsl("Linux64bit");
	}
	Unexpected("Platform in CrashReports::PlatformString.");
}

void StartCatching(not_null<Core::Launcher*> launcher) {
#ifndef DESKTOP_APP_DISABLE_CRASH_REPORTS
	ProcessAnnotations["Binary"] = cExeName().toUtf8().constData();
	ProcessAnnotations["ApiId"] = QString::number(ApiId).toUtf8().constData();
	ProcessAnnotations["Version"] = (cAlphaVersion() ? qsl("%1 alpha").arg(cAlphaVersion()) : (AppBetaVersion ? qsl("%1 beta") : qsl("%1")).arg(AppVersion)).toUtf8().constData();
	ProcessAnnotations["Launched"] = QDateTime::currentDateTime().toString("dd.MM.yyyy hh:mm:ss").toUtf8().constData();
	ProcessAnnotations["Platform"] = PlatformString().toUtf8().constData();
	ProcessAnnotations["UserTag"] = QString::number(launcher->installationTag(), 16).toUtf8().constData();

	QString dumpspath = cWorkingDir() + qsl("tdata/dumps");
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
	std::string handler = (cExeDir() + cExeName() + qsl("/Contents/Helpers/crashpad_handler")).toUtf8().constData();
	std::string database = QFile::encodeName(dumpspath).constData();
	if (crashpad_client.StartHandler(
			base::FilePath(handler),
			base::FilePath(database),
			std::string(),
			ProcessAnnotations,
			std::vector<std::string>(),
			false)) {
		crashpad_client.UseHandler();
	}
#endif // else for MAC_USE_BREAKPAD
#elif defined Q_OS_UNIX
	BreakpadExceptionHandler = new google_breakpad::ExceptionHandler(
		google_breakpad::MinidumpDescriptor(QFile::encodeName(dumpspath).toStdString()),
		/*FilterCallback*/ 0,
		DumpCallback,
		/*context*/ 0,
		true,
		-1
	);
#endif // Q_OS_UNIX
#endif // !DESKTOP_APP_DISABLE_CRASH_REPORTS
}

void FinishCatching() {
#ifndef DESKTOP_APP_DISABLE_CRASH_REPORTS
#if !defined Q_OS_MAC || defined MAC_USE_BREAKPAD

	delete base::take(BreakpadExceptionHandler);

#endif // !Q_OS_MAC || MAC_USE_BREAKPAD
#endif // !DESKTOP_APP_DISABLE_CRASH_REPORTS
}

StartResult Start() {
#ifndef DESKTOP_APP_DISABLE_CRASH_REPORTS
	ReportPath = cWorkingDir() + qsl("tdata/working");

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

#endif // !DESKTOP_APP_DISABLE_CRASH_REPORTS
	return Restart();
}

Status Restart() {
#ifndef DESKTOP_APP_DISABLE_CRASH_REPORTS
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

			sigaction(SIGABRT, &sigact, &SIG_def[SIGABRT]);
			sigaction(SIGSEGV, &sigact, &SIG_def[SIGSEGV]);
			sigaction(SIGILL, &sigact, &SIG_def[SIGILL]);
			sigaction(SIGFPE, &sigact, &SIG_def[SIGFPE]);
			sigaction(SIGBUS, &sigact, &SIG_def[SIGBUS]);
			sigaction(SIGSYS, &sigact, &SIG_def[SIGSYS]);
#else // !Q_OS_WIN
			signal(SIGABRT, SignalHandler);
			signal(SIGSEGV, SignalHandler);
			signal(SIGILL, SignalHandler);
			signal(SIGFPE, SignalHandler);
#endif // else for !Q_OS_WIN
		}

		InstallOperatorNewHandler();
		InstallQtMessageHandler();

		return Started;
	}

	LOG(("FATAL: Could not open '%1' for writing!").arg(ReportPath));

	return CantOpen;
#else // !DESKTOP_APP_DISABLE_CRASH_REPORTS
	return Started;
#endif // else for !DESKTOP_APP_DISABLE_CRASH_REPORTS
}

void Finish() {
#ifndef DESKTOP_APP_DISABLE_CRASH_REPORTS
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
#endif // !DESKTOP_APP_DISABLE_CRASH_REPORTS
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

#ifndef DESKTOP_APP_DISABLE_CRASH_REPORTS

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

#endif // DESKTOP_APP_DISABLE_CRASH_REPORTS

} // namespace CrashReports
