/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace codegen {
namespace numbers {

struct Options {
	QString outputPath = ".";
	QString inputPath;
};

// Parsing failed if inputPath is empty in the result.
Options parseOptions();

} // namespace numbers
} // namespace codegen
