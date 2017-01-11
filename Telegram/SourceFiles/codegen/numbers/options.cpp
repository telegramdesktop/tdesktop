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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "codegen/numbers/options.h"

#include <ostream>
#include <QtCore/QCoreApplication>
#include "codegen/common/logging.h"

namespace codegen {
namespace numbers {
namespace {

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

		// Output path
		if (arg == "-o") {
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

} // namespace numbers
} // namespace codegen
