/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "codegen/style/options.h"

#include <ostream>
#include <QtCore/QCoreApplication>
#include "codegen/common/logging.h"

namespace codegen {
namespace style {
namespace {

constexpr int kErrorIncludePathExpected     = 901;
constexpr int kErrorOutputPathExpected      = 902;
constexpr int kErrorInputPathExpected       = 903;
constexpr int kErrorSingleInputPathExpected = 904;

} // namespace

using common::logError;

Options parseOptions() {
	Options result;
	auto args(QCoreApplication::instance()->arguments());
	for (int i = 1, count = args.size(); i < count; ++i) { // skip first
		const auto &arg(args.at(i));

		// Rebuild all dependencies
		if (arg == "--rebuild") {
			result.rebuildDependencies = true;

		// Include paths
		} else if (arg == "-I") {
			if (++i == count) {
				logError(kErrorIncludePathExpected, "Command Line") << "include path expected after -I";
				return Options();
			} else {
				result.includePaths.push_back(args.at(i));
			}
		} else if (arg.startsWith("-I")) {
			result.includePaths.push_back(arg.mid(2));

		// Output path
		} else if (arg == "-o") {
			if (++i == count) {
				logError(kErrorOutputPathExpected, "Command Line") << "output path expected after -o";
				return Options();
			} else {
				result.outputPath = args.at(i);
			}
		} else if (arg.startsWith("-o")) {
			result.outputPath = arg.mid(2);

		// Input path
		} else {
			if (result.inputPath.isEmpty()) {
				result.inputPath = arg;
			} else {
				logError(kErrorSingleInputPathExpected, "Command Line") << "only one input path expected";
				return Options();
			}
		}
	}
	if (result.inputPath.isEmpty()) {
		logError(kErrorInputPathExpected, "Command Line") << "input path expected";
		return Options();
	}
	return result;
}

} // namespace style
} // namespace codegen
