/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace Test {

struct LangIdsMigrationReport {
	bool passed = false;
	QStringList failures;
};

[[nodiscard]] LangIdsMigrationReport RunLangIdsMigrationSelfTest();

} // namespace Test
