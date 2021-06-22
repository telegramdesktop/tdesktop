/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/base_integration.h"

namespace Core {

class Launcher {
public:
	Launcher(
		int argc,
		char *argv[]);

	static std::unique_ptr<Launcher> Create(int argc, char *argv[]);

	virtual int exec();

	QString argumentsString() const;
	bool customWorkingDir() const;

	uint64 installationTag() const;

	bool checkPortableVersionFolder();
	void workingFolderReady();
	void writeDebugModeSetting();
	void writeInstallBetaVersionsSetting();

	virtual ~Launcher() = default;

protected:
	enum class UpdaterLaunch {
		PerformUpdate,
		JustRelaunch,
	};

private:
	void prepareSettings();
	void initQtMessageLogging();
	void processArguments();

	QStringList readArguments(int argc, char *argv[]) const;
	virtual std::optional<QStringList> readArgumentsHook(
			int argc,
			char *argv[]) const {
		return std::nullopt;
	}

	void init();
	virtual void initHook() {
	}

	virtual bool launchUpdater(UpdaterLaunch action) = 0;

	int executeApplication();

	int _argc;
	char **_argv;
	QStringList _arguments;
	BaseIntegration _baseIntegration;

	bool _customWorkingDir = false;

};

} // namespace Core
