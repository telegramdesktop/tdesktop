/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/launcher.h"

namespace Platform {

class Launcher : public Core::Launcher {
public:
	Launcher(int argc, char *argv[]);

	int exec() override;

private:
	std::optional<QStringList> readArgumentsHook(
		int argc,
		char *argv[]) const override;

	void initHook() override;

	bool launchUpdater(UpdaterLaunch action) override;

	bool launch(
		const QString &binaryPath,
		const QStringList &argumentsList);
	bool installProtectedUpdate();
	bool relaunchAfterUpdate(bool installed);

	bool _updating = false;
	bool _updateFailed = false;

};

} // namespace Platform
