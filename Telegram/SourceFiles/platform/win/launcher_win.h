/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "core/launcher.h"

namespace Platform {

class Launcher : public Core::Launcher {
public:
	Launcher(int argc, char *argv[]);

private:
	std::optional<QStringList> readArgumentsHook(
		int argc,
		char *argv[]) const override;

	bool launchUpdater(UpdaterLaunch action) override;

	bool launch(
		const QString &operation,
		const QString &binaryPath,
		const QStringList &argumentsList);

};

} // namespace Platform
