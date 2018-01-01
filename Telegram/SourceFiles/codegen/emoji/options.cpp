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
#include "codegen/emoji/options.h"

#include <ostream>
#include <QtCore/QCoreApplication>
#include "codegen/common/logging.h"

namespace codegen {
namespace emoji {
namespace {

constexpr int kErrorOutputPathExpected      = 902;
constexpr int kErrorReplacesPathExpected    = 903;
constexpr int kErrorOneReplacesPathExpected = 904;

} // namespace

using common::logError;

Options parseOptions() {
	Options result;
	auto args = QCoreApplication::instance()->arguments();
	for (int i = 1, count = args.size(); i < count; ++i) { // skip first
		auto &arg = args.at(i);

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
#ifdef SUPPORT_IMAGE_GENERATION
		} else if (arg == "--images") {
			result.writeImages = true;
#endif // SUPPORT_IMAGE_GENERATION
		} else if (result.replacesPath.isEmpty()) {
			result.replacesPath = arg;
		} else {
			logError(kErrorOneReplacesPathExpected, "Command Line") << "only one replaces path expected";
			return Options();
		}
	}
	if (result.outputPath.isEmpty()) {
		logError(kErrorOutputPathExpected, "Command Line") << "output path expected";
		return Options();
	} else if (result.replacesPath.isEmpty()) {
		logError(kErrorReplacesPathExpected, "Command Line") << "replaces path expected";
		return Options();
	}
	return result;
}

} // namespace emoji
} // namespace codegen
