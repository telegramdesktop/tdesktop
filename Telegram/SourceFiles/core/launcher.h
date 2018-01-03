/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core {

class Launcher {
public:
	Launcher(int argc, char *argv[]);

	static std::unique_ptr<Launcher> Create(int argc, char *argv[]);

	int exec();

	QString argumentsString() const;
	bool customWorkingDir() const {
		return _customWorkingDir;
	}

protected:
	enum class UpdaterLaunch {
		PerformUpdate,
		JustRelaunch,
	};

private:
	void prepareSettings();
	void processArguments();

	QStringList readArguments(int argc, char *argv[]) const;
	virtual base::optional<QStringList> readArgumentsHook(
			int argc,
			char *argv[]) const {
		return base::none;
	}

	void init();
	virtual void initHook() {
	}

	virtual bool launchUpdater(UpdaterLaunch action) = 0;

	int executeApplication();

	int _argc;
	char **_argv;
	QStringList _arguments;

	bool _customWorkingDir = false;

};

} // namespace Core
