/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/launcher.h"

#include "platform/platform_launcher.h"
#include "platform/platform_specific.h"
#include "base/options.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/base_platform_file_utilities.h"
#include "ui/main_queue_processor.h"
#include "core/crash_reports.h"
#include "core/update_checker.h"
#include "core/sandbox.h"
#include "base/concurrent_timer.h"
#include "base/options.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/QStandardPaths>

namespace Core {
namespace {

uint64 InstallationTag = 0;

base::options::toggle OptionFreeType({
	.id = kOptionFreeType,
	.name = "FreeType font engine",
	.description = "Use the font engine from Linux instead of the system one.",
	.scope = base::options::windows | base::options::macos,
	.restartRequired = true,
});

class FilteredCommandLineArguments {
public:
	FilteredCommandLineArguments(int argc, char **argv);

	int &count();
	char **values();

private:
	static constexpr auto kForwardArgumentCount = 1;

	int _count = 0;
	std::vector<QByteArray> _owned;
	std::vector<char*> _arguments;

	void pushArgument(const char *text);

};

FilteredCommandLineArguments::FilteredCommandLineArguments(
	int argc,
	char **argv) {
	// For now just pass only the first argument, the executable path.
	for (auto i = 0; i != kForwardArgumentCount; ++i) {
		pushArgument(argv[i]);
	}

#if defined Q_OS_WIN || defined Q_OS_MAC
	if (OptionFreeType.value()) {
		pushArgument("-platform");
#ifdef Q_OS_WIN
		pushArgument("windows:fontengine=freetype");
#else // Q_OS_WIN
		pushArgument("cocoa:fontengine=freetype");
#endif // !Q_OS_WIN
	}
#endif // Q_OS_WIN || Q_OS_MAC

	pushArgument(nullptr);
}

int &FilteredCommandLineArguments::count() {
	_count = _arguments.size() - 1;
	return _count;
}

char **FilteredCommandLineArguments::values() {
	return _arguments.data();
}

void FilteredCommandLineArguments::pushArgument(const char *text) {
	_owned.emplace_back(text);
	_arguments.push_back(_owned.back().data());
}

QString DebugModeSettingPath() {
	return cWorkingDir() + u"tdata/withdebug"_q;
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
	if (cDebugMode()) {
		Logs::SetDebugEnabled(true);
	}
	if (Logs::DebugEnabled()) {
		QLoggingCategory::setFilterRules("qt.qpa.gl.debug=true");
	}
}

void ComputeExternalUpdater() {
	auto locations = QStandardPaths::standardLocations(
		QStandardPaths::AppDataLocation);
	if (locations.isEmpty()) {
		locations << QString();
	}
	locations[0] = QDir::cleanPath(cWorkingDir());
	locations << QDir::cleanPath(cExeDir());
	for (const auto &location : locations) {
		const auto dir = location + u"/externalupdater.d"_q;
		for (const auto &info : QDir(dir).entryInfoList(QDir::Files)) {
			QFile file(info.absoluteFilePath());
			if (file.open(QIODevice::ReadOnly)) {
				QTextStream fileStream(&file);
				while (!fileStream.atEnd()) {
					const auto path = fileStream.readLine();
					if (path == (cExeDir() + cExeName())) {
						SetUpdaterDisabledAtStartup();
						return;
					}
				}
			}
		}
	}
}

QString InstallBetaVersionsSettingPath() {
	return cWorkingDir() + u"tdata/devversion"_q;
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
	} else if (QFile::exists(installBetaSettingPath)) {
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
	auto file = QFile(cWorkingDir() + u"tdata/usertag"_q);
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
		auto generator = std::mt19937(std::random_device()());
		auto distribution = std::uniform_int_distribution<uint64>();
		do {
			InstallationTag = distribution(generator);
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
	const auto now = cExeDir() + u"TelegramForcePortable"_q;
	if (QDir(was).exists() && !QDir(now).exists()) {
		const auto oldFile = was + "/tdata/" + file;
		const auto newFile = was + "/tdata/alpha";
		if (QFile::exists(oldFile) && !QFile::exists(newFile)) {
			if (!QFile(oldFile).copy(newFile)) {
				LOG(("FATAL: Could not copy '%1' to '%2'").arg(
					oldFile,
					newFile));
				return false;
			}
		}
		if (!QDir().rename(was, now)) {
			LOG(("FATAL: Could not rename '%1' to '%2'").arg(was, now));
			return false;
		}
	}
	return true;
}

bool MoveLegacyAlphaFolder() {
	if (!MoveLegacyAlphaFolder(u"TelegramAlpha_data"_q, u"alpha"_q)
		|| !MoveLegacyAlphaFolder(u"TelegramBeta_data"_q, u"beta"_q)) {
		return false;
	}
	return true;
}

bool CheckPortableVersionFolder() {
	if (!MoveLegacyAlphaFolder()) {
		return false;
	}

	const auto portable = cExeDir() + u"TelegramForcePortable"_q;
	QFile key(portable + u"/tdata/alpha"_q);
	if (cAlphaVersion()) {
		Assert(*AlphaPrivateKey != 0);

		cForceWorkingDir(portable);
		QDir().mkpath(cWorkingDir() + u"tdata"_q);
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
	cForceWorkingDir(portable);
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

base::options::toggle OptionFractionalScalingEnabled({
	.id = kOptionFractionalScalingEnabled,
	.name = "Enable precise High DPI scaling",
	.description = "Follow system interface scale settings exactly.",
	.scope = base::options::windows | base::options::linux,
	.restartRequired = true,
});

} // namespace

const char kOptionFractionalScalingEnabled[] = "fractional-scaling-enabled";
const char kOptionFreeType[] = "freetype";

Launcher *Launcher::InstanceSetter::Instance = nullptr;

std::unique_ptr<Launcher> Launcher::Create(int argc, char *argv[]) {
	return std::make_unique<Platform::Launcher>(argc, argv);
}

Launcher::Launcher(int argc, char *argv[])
: _argc(argc)
, _argv(argv)
, _arguments(readArguments(_argc, _argv))
, _baseIntegration(_argc, _argv)
, _initialWorkingDir(QDir::currentPath() + '/') {
	crl::toggle_fp_exceptions(true);

	base::Integration::Set(&_baseIntegration);
}

Launcher::~Launcher() {
	InstanceSetter::Instance = nullptr;
}

void Launcher::init() {
	prepareSettings();
	initQtMessageLogging();

	QApplication::setApplicationName(u"TelegramDesktop"_q);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	// fallback session management is useless for tdesktop since it doesn't have
	// any "are you sure you want to close this window?" dialogs
	// but it produces bugs like https://github.com/telegramdesktop/tdesktop/issues/5022
	// and https://github.com/telegramdesktop/tdesktop/issues/7549
	// and https://github.com/telegramdesktop/tdesktop/issues/948
	// more info: https://doc.qt.io/qt-5/qguiapplication.html#isFallbackSessionManagementEnabled
	QApplication::setFallbackSessionManagementEnabled(false);
#endif // Qt < 6.0.0

	initHook();
}

void Launcher::initHighDpi() {
#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
	qputenv("QT_DPI_ADJUSTMENT_POLICY", "AdjustDpi");
#endif // Qt < 6.2.0

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
#endif // Qt < 6.0.0

	if (OptionFractionalScalingEnabled.value()) {
		QApplication::setHighDpiScaleFactorRoundingPolicy(
			Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
	} else {
		QApplication::setHighDpiScaleFactorRoundingPolicy(
			Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor);
	}
}

int Launcher::exec() {
	init();

	if (cLaunchMode() == LaunchModeFixPrevious) {
		return psFixPrevious();
	} else if (cLaunchMode() == LaunchModeCleanup) {
		return psCleanup();
	}

	// Must be started before Platform is started.
	Logs::start();
	base::options::init(cWorkingDir() + "tdata/experimental_options.json");

	// Must be called after options are inited.
	initHighDpi();

	if (Logs::DebugEnabled()) {
		const auto openalLogPath = QDir::toNativeSeparators(
			cWorkingDir() + u"DebugLogs/last_openal_log.txt"_q);

		qputenv("ALSOFT_LOGLEVEL", "3");

#ifdef Q_OS_WIN
		_wputenv_s(
			L"ALSOFT_LOGFILE",
			openalLogPath.toStdWString().c_str());
#else // Q_OS_WIN
		qputenv(
			"ALSOFT_LOGFILE",
			QFile::encodeName(openalLogPath));
#endif // !Q_OS_WIN
	}

	// Must be started before Sandbox is created.
	Platform::start();
	ThirdParty::start();
	auto result = executeApplication();

	DEBUG_LOG(("Telegram finished, result: %1").arg(result));

	if (!UpdaterDisabled() && cRestartingUpdate()) {
		DEBUG_LOG(("Sandbox Info: executing updater to install update."));
		if (!launchUpdater(UpdaterLaunch::PerformUpdate)) {
			base::Platform::DeleteDirectory(cWorkingDir() + u"tupdates/temp"_q);
		}
	} else if (cRestarting()) {
		DEBUG_LOG(("Sandbox Info: executing Telegram because of restart."));
		launchUpdater(UpdaterLaunch::JustRelaunch);
	}

	CrashReports::Finish();
	ThirdParty::finish();
	Platform::finish();
	Logs::finish();

	return result;
}

bool Launcher::validateCustomWorkingDir() {
	if (customWorkingDir()) {
		if (_customWorkingDir == cWorkingDir()) {
			_customWorkingDir = QString();
			return false;
		}
		cForceWorkingDir(_customWorkingDir);
		return true;
	}
	return false;
}

void Launcher::workingFolderReady() {
	srand((unsigned int)time(nullptr));

	ComputeDebugMode();
	ComputeExternalUpdater();
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
		result.push_back(base::FromUtf8Safe(argv[i]));
	}
	return result;
}

const QStringList &Launcher::arguments() const {
	return _arguments;
}

QString Launcher::initialWorkingDir() const {
	return _initialWorkingDir;
}

bool Launcher::customWorkingDir() const {
	return !_customWorkingDir.isEmpty();
}

void Launcher::prepareSettings() {
	auto path = base::Platform::CurrentExecutablePath(_argc, _argv);
	LOG(("Executable path before check: %1").arg(path));
	if (cExeName().isEmpty()) {
		LOG(("WARNING: Could not compute executable path, some features will be disabled."));
	}

	processArguments();
}

void Launcher::initQtMessageLogging() {
	static QtMessageHandler OriginalMessageHandler = nullptr;
	OriginalMessageHandler = qInstallMessageHandler([](
			QtMsgType type,
			const QMessageLogContext &context,
			const QString &msg) {
		if (OriginalMessageHandler) {
			OriginalMessageHandler(type, context, msg);
		}
		if (Logs::DebugEnabled() || !Logs::started()) {
			if (!Logs::WritingEntry()) {
				// Sometimes Qt logs something inside our own logging.
				LOG((msg));
			}
		}
	});
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
		{ "-debug"          , KeyFormat::NoValues },
		{ "-key"            , KeyFormat::OneValue },
		{ "-autostart"      , KeyFormat::NoValues },
		{ "-fixprevious"    , KeyFormat::NoValues },
		{ "-cleanup"        , KeyFormat::NoValues },
		{ "-noupdate"       , KeyFormat::NoValues },
		{ "-tosettings"     , KeyFormat::NoValues },
		{ "-startintray"    , KeyFormat::NoValues },
		{ "-quit"           , KeyFormat::NoValues },
		{ "-sendpath"       , KeyFormat::AllLeftValues },
		{ "-workdir"        , KeyFormat::OneValue },
		{ "--"              , KeyFormat::OneValue },
		{ "-scale"          , KeyFormat::OneValue },
	};
	auto parseResult = QMap<QByteArray, QStringList>();
	auto parsingKey = QByteArray();
	auto parsingFormat = KeyFormat::NoValues;
	for (const auto &argument : std::as_const(_arguments)) {
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

	static const auto RegExp = QRegularExpression("[^a-z0-9\\-_]");
	gDebugMode = parseResult.contains("-debug");
	gKeyFile = parseResult
		.value("-key", {})
		.join(QString())
		.toLower()
		.replace(RegExp, {});
	gLaunchMode = parseResult.contains("-autostart") ? LaunchModeAutoStart
		: parseResult.contains("-fixprevious") ? LaunchModeFixPrevious
		: parseResult.contains("-cleanup") ? LaunchModeCleanup
		: LaunchModeNormal;
	gNoStartUpdate = parseResult.contains("-noupdate");
	gStartToSettings = parseResult.contains("-tosettings");
	gStartInTray = parseResult.contains("-startintray");
	gQuit = parseResult.contains("-quit");
	gSendPaths = parseResult.value("-sendpath", {});
	_customWorkingDir = parseResult.value("-workdir", {}).join(QString());
	if (!_customWorkingDir.isEmpty()) {
		_customWorkingDir = QDir(_customWorkingDir).absolutePath() + '/';
	}
	gStartUrl = parseResult.value("--", {}).join(QString());

	const auto scaleKey = parseResult.value("-scale", {});
	if (scaleKey.size() > 0) {
		using namespace style;
		const auto value = scaleKey[0].toInt();
		gConfigScale = ((value < kScaleMin) || (value > kScaleMax))
			? kScaleAuto
			: value;
	}
}

int Launcher::executeApplication() {
	FilteredCommandLineArguments arguments(_argc, _argv);
	Sandbox sandbox(arguments.count(), arguments.values());
	Ui::MainQueueProcessor processor;
	base::ConcurrentTimerEnvironment environment;
	return sandbox.start();
}

} // namespace Core
