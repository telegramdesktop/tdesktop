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
	bool launchUpdater(UpdaterLaunch action) override;
	bool installProtectedUpdate();
	bool relaunchAfterUpdate(bool installed);

	bool _updating = false;
	bool _updateFailed = false;

};

} // namespace Platform
