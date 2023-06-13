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
	void initHook() override;
	bool launchUpdater(UpdaterLaunch action) override;

	std::vector<std::string> _arguments;

};

} // namespace Platform
