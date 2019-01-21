/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/launcher.h"

#include "platform/platform_launcher.h"
#include "platform/platform_specific.h"
#include "core/crash_reports.h"
#include "core/main_queue_processor.h"
#include "core/update_checker.h"
#include "core/sandbox.h"
#include "base/concurrent_timer.h"

namespace Core {
namespace {

uint64 InstallationTag = 0;

QString DebugModeSettingPath() {
	return cWorkingDir() + qsl("tdata/withdebug");
}

void WriteDebugModeSetting() {
	auto file = QFile(DebugModeSettingPath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(Logs::DebugEnabled() ? "1" : "0");
	}
}

void ComputeDebugMode() {
	Logs::SetDebugEnabled(cAlphaVersion() != 0);
	const auto debugModeSettingPath = DebugModeSettingPath();
	auto file = QFile(debugModeSettingPath);
	if (file.exists() && file.open(QIODevice::ReadOnly)) {
		Logs::SetDebugEnabled(file.read(1) != "0");
	}
}

void ComputeTestMode() {
	if (QFile(cWorkingDir() + qsl("tdata/withtestmode")).exists()) {
		cSetTestMode(true);
	}
}

QString InstallBetaVersionsSettingPath() {
	return cWorkingDir() + qsl("tdata/devversion");
}

void WriteInstallBetaVersionsSetting() {
	QFile f(InstallBetaVersionsSettingPath());
	if (f.open(QIODevice::WriteOnly)) {
		f.write(cInstallBetaVersion() ? "1" : "0");
	}
}

void ComputeInstallBetaVersions() {
	const auto installBetaSettingPath = InstallBetaVersionsSettingPath();
	if (cAlphaVersion()) {
		cSetInstallBetaVersion(false);
	} else if (QFile(installBetaSettingPath).exists()) {
		QFile f(installBetaSettingPath);
		if (f.open(QIODevice::ReadOnly)) {
			cSetInstallBetaVersion(f.read(1) != "0");
		}
	} else if (AppBetaVersion) {
		WriteInstallBetaVersionsSetting();
	}
}

void ComputeInstallationTag() {
	InstallationTag = 0;
	auto file = QFile(cWorkingDir() + qsl("tdata/usertag"));
	if (file.open(QIODevice::ReadOnly)) {
		const auto result = file.read(
			reinterpret_cast<char*>(&InstallationTag),
			sizeof(uint64));
		if (result != sizeof(uint64)) {
			InstallationTag = 0;
		}
		file.close();
	}
	if (!InstallationTag) {
		do {
			memsetrnd_bad(InstallationTag);
		} while (!InstallationTag);

		if (file.open(QIODevice::WriteOnly)) {
			file.write(
				reinterpret_cast<char*>(&InstallationTag),
				sizeof(uint64));
			file.close();
		}
	}
}

bool MoveLegacyAlphaFolder(const QString &folder, const QString &file) {
	const auto was = cExeDir() + folder;
	const auto now = cExeDir() + qsl("TelegramForcePortable");
	if (QDir(was).exists() && !QDir(now).exists()) {
		const auto oldFile = was + "/tdata/" + file;
		const auto newFile = was + "/tdata/alpha";
		if (QFile(oldFile).exists() && !QFile(newFile).exists()) {
			if (!QFile(oldFile).copy(newFile)) {
				LOG(("FATAL: Could not copy '%1' to '%2'"
					).arg(oldFile
					).arg(newFile));
				return false;
			}
		}
		if (!QDir().rename(was, now)) {
			LOG(("FATAL: Could not rename '%1' to '%2'"
				).arg(was
				).arg(now));
			return false;
		}
	}
	return true;
}

bool MoveLegacyAlphaFolder() {
	if (!MoveLegacyAlphaFolder(qsl("TelegramAlpha_data"), qsl("alpha"))
		|| !MoveLegacyAlphaFolder(qsl("TelegramBeta_data"), qsl("beta"))) {
		return false;
	}
	return true;
}

bool CheckPortableVersionFolder() {
	if (!MoveLegacyAlphaFolder()) {
		return false;
	}

	const auto portable = cExeDir() + qsl("TelegramForcePortable");
	QFile key(portable + qsl("/tdata/alpha"));
	if (cAlphaVersion()) {
		Assert(*AlphaPrivateKey != 0);

		cForceWorkingDir(portable + '/');
		QDir().mkpath(cWorkingDir() + qstr("tdata"));
		cSetAlphaPrivateKey(QByteArray(AlphaPrivateKey));
		if (!key.open(QIODevice::WriteOnly)) {
			LOG(("FATAL: Could not open '%1' for writing private key!"
				).arg(key.fileName()));
			return false;
		}
		QDataStream dataStream(&key);
		dataStream.setVersion(QDataStream::Qt_5_3);
		dataStream << quint64(cRealAlphaVersion()) << cAlphaPrivateKey();
		return true;
	}
	if (!QDir(portable).exists()) {
		return true;
	}
	cForceWorkingDir(portable + '/');
	if (!key.exists()) {
		return true;
	}

	if (!key.open(QIODevice::ReadOnly)) {
		LOG(("FATAL: could not open '%1' for reading private key. "
			"Delete it or reinstall private alpha version."
			).arg(key.fileName()));
		return false;
	}
	QDataStream dataStream(&key);
	dataStream.setVersion(QDataStream::Qt_5_3);

	quint64 v;
	QByteArray k;
	dataStream >> v >> k;
	if (dataStream.status() != QDataStream::Ok || k.isEmpty()) {
		LOG(("FATAL: '%1' is corrupted. "
			"Delete it or reinstall private alpha version."
			).arg(key.fileName()));
		return false;
	}
	cSetAlphaVersion(AppVersion * 1000ULL);
	cSetAlphaPrivateKey(k);
	cSetRealAlphaVersion(v);
	return true;
}

} // namespace

std::unique_ptr<Launcher> Launcher::Create(int argc, char *argv[]) {
	return std::make_unique<Platform::Launcher>(argc, argv);
}

Launcher::Launcher(
	int argc,
	char *argv[],
	const QString &deviceModel,
	const QString &systemVersion)
: _argc(argc)
, _argv(argv)
, _deviceModel(deviceModel)
, _systemVersion(systemVersion) {
}

void Launcher::init() {
	_arguments = readArguments(_argc, _argv);

	prepareSettings();

	QApplication::setApplicationName(qsl("TelegramDesktop"));

#ifndef OS_MAC_OLD
	QApplication::setAttribute(Qt::AA_DisableHighDpiScaling, true);
#endif // OS_MAC_OLD

	initHook();
}

int Launcher::exec() {
	init();

	if (cLaunchMode() == LaunchModeFixPrevious) {
		return psFixPrevious();
	} else if (cLaunchMode() == LaunchModeCleanup) {
		return psCleanup();
	}

	// both are finished in Sandbox::closeApplication
	Logs::start(this); // must be started before Platform is started
	Platform::start(); // must be started before Sandbox is created

	auto result = executeApplication();

	DEBUG_LOG(("Telegram finished, result: %1").arg(result));

	if (!UpdaterDisabled() && cRestartingUpdate()) {
		DEBUG_LOG(("Sandbox Info: executing updater to install update."));
		if (!launchUpdater(UpdaterLaunch::PerformUpdate)) {
			psDeleteDir(cWorkingDir() + qsl("tupdates/temp"));
		}
	} else if (cRestarting()) {
		DEBUG_LOG(("Sandbox Info: executing Telegram because of restart."));
		launchUpdater(UpdaterLaunch::JustRelaunch);
	}

	CrashReports::Finish();
	Platform::finish();
	Logs::finish();

	return result;
}

void Launcher::workingFolderReady() {
	srand((unsigned int)time(nullptr));

	ComputeTestMode();
	ComputeDebugMode();
	ComputeInstallBetaVersions();
	ComputeInstallationTag();
}

void Launcher::writeDebugModeSetting() {
	WriteDebugModeSetting();
}

void Launcher::writeInstallBetaVersionsSetting() {
	WriteInstallBetaVersionsSetting();
}

bool Launcher::checkPortableVersionFolder() {
	return CheckPortableVersionFolder();
}

QStringList Launcher::readArguments(int argc, char *argv[]) const {
	Expects(argc >= 0);

	if (const auto native = readArgumentsHook(argc, argv)) {
		return *native;
	}

	auto result = QStringList();
	result.reserve(argc);
	for (auto i = 0; i != argc; ++i) {
		result.push_back(fromUtf8Safe(argv[i]));
	}
	return result;
}

QString Launcher::argumentsString() const {
	return _arguments.join(' ');
}

bool Launcher::customWorkingDir() const {
	return _customWorkingDir;
}

void Launcher::prepareSettings() {
#ifdef Q_OS_MAC
#ifndef OS_MAC_OLD
	if (QSysInfo::macVersion() >= QSysInfo::MV_10_11) {
		gIsElCapitan = true;
	}
#else // OS_MAC_OLD
	if (QSysInfo::macVersion() < QSysInfo::MV_10_7) {
		gIsSnowLeopard = true;
	}
#endif // OS_MAC_OLD
#endif // Q_OS_MAC

	switch (cPlatform()) {
	case dbipWindows:
#ifndef OS_WIN_STORE
		gPlatformString = qsl("Windows");
#else // OS_WIN_STORE
		gPlatformString = qsl("WinStore");
#endif // OS_WIN_STORE
	break;
	case dbipMac:
#ifndef OS_MAC_STORE
		gPlatformString = qsl("MacOS");
#else // OS_MAC_STORE
		gPlatformString = qsl("MacAppStore");
#endif // OS_MAC_STORE
	break;
	case dbipMacOld:
		gPlatformString = qsl("MacOSold");
	break;
	case dbipLinux64:
		gPlatformString = qsl("Linux64bit");
	break;
	case dbipLinux32:
		gPlatformString = qsl("Linux32bit");
	break;
	}

	auto path = Platform::CurrentExecutablePath(_argc, _argv);
	LOG(("Executable path before check: %1").arg(path));
	if (!path.isEmpty()) {
		auto info = QFileInfo(path);
		if (info.isSymLink()) {
			info = info.symLinkTarget();
		}
		if (info.exists()) {
			const auto dir = info.absoluteDir().absolutePath();
			gExeDir = (dir.endsWith('/') ? dir : (dir + '/'));
			gExeName = info.fileName();
		}
	}
	if (cExeName().isEmpty()) {
		LOG(("WARNING: Could not compute executable path, some features will be disabled."));
	}

	processArguments();
}

QString Launcher::deviceModel() const {
	return _deviceModel;
}

QString Launcher::systemVersion() const {
	return _systemVersion;
}

uint64 Launcher::installationTag() const {
	return InstallationTag;
}

void Launcher::processArguments() {
		enum class KeyFormat {
		NoValues,
		OneValue,
		AllLeftValues,
	};
	auto parseMap = std::map<QByteArray, KeyFormat> {
		{ "-testmode"       , KeyFormat::NoValues },
		{ "-debug"          , KeyFormat::NoValues },
		{ "-many"           , KeyFormat::NoValues },
		{ "-key"            , KeyFormat::OneValue },
		{ "-autostart"      , KeyFormat::NoValues },
		{ "-fixprevious"    , KeyFormat::NoValues },
		{ "-cleanup"        , KeyFormat::NoValues },
		{ "-noupdate"       , KeyFormat::NoValues },
		{ "-externalupdater", KeyFormat::NoValues },
		{ "-tosettings"     , KeyFormat::NoValues },
		{ "-startintray"    , KeyFormat::NoValues },
		{ "-sendpath"       , KeyFormat::AllLeftValues },
		{ "-workdir"        , KeyFormat::OneValue },
		{ "--"              , KeyFormat::OneValue },
	};
	auto parseResult = QMap<QByteArray, QStringList>();
	auto parsingKey = QByteArray();
	auto parsingFormat = KeyFormat::NoValues;
	for (const auto &argument : _arguments) {
		switch (parsingFormat) {
		case KeyFormat::OneValue: {
			parseResult[parsingKey] = QStringList(argument.mid(0, 8192));
			parsingFormat = KeyFormat::NoValues;
		} break;
		case KeyFormat::AllLeftValues: {
			parseResult[parsingKey].push_back(argument.mid(0, 8192));
		} break;
		case KeyFormat::NoValues: {
			parsingKey = argument.toLatin1();
			auto it = parseMap.find(parsingKey);
			if (it != parseMap.end()) {
				parsingFormat = it->second;
				parseResult[parsingKey] = QStringList();
			}
		} break;
		}
	}

	if (parseResult.contains("-externalupdater")) {
		SetUpdaterDisabledAtStartup();
	}
	gTestMode = parseResult.contains("-testmode");
	Logs::SetDebugEnabled(parseResult.contains("-debug"));
	gManyInstance = parseResult.contains("-many");
	gKeyFile = parseResult.value("-key", {}).join(QString()).toLower();
	gKeyFile = gKeyFile.replace(QRegularExpression("[^a-z0-9\\-_]"), {});
	gLaunchMode = parseResult.contains("-autostart") ? LaunchModeAutoStart
		: parseResult.contains("-fixprevious") ? LaunchModeFixPrevious
		: parseResult.contains("-cleanup") ? LaunchModeCleanup
		: LaunchModeNormal;
	gNoStartUpdate = parseResult.contains("-noupdate");
	gStartToSettings = parseResult.contains("-tosettings");
	gStartInTray = parseResult.contains("-startintray");
	gSendPaths = parseResult.value("-sendpath", {});
	gWorkingDir = parseResult.value("-workdir", {}).join(QString());
	if (!gWorkingDir.isEmpty()) {
		if (QDir().exists(gWorkingDir)) {
			_customWorkingDir = true;
		} else {
			gWorkingDir = QString();
		}
	}
	gStartUrl = parseResult.value("--", {}).join(QString());
}

int Launcher::executeApplication() {
	Sandbox sandbox(this, _argc, _argv);
	MainQueueProcessor processor;
	base::ConcurrentTimerEnvironment environment;
	return sandbox.start();
}

} // namespace Core
