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
	using Core::Launcher::Launcher;

private:
	base::optional<QStringList> readArgumentsHook(
		int argc,
		char *argv[]) const override;

	bool launchUpdater(UpdaterLaunch action) override;

	bool launch(
		const QString &operation,
		const QString &binaryPath,
		const QStringList &argumentsList);

};

} // namespace Platform
