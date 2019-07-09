/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "codegen/style/options.h"

#include <ostream>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include "codegen/common/logging.h"

namespace codegen {
namespace style {
namespace {

constexpr int kErrorIncludePathExpected     = 901;
constexpr int kErrorOutputPathExpected      = 902;
constexpr int kErrorInputPathExpected       = 903;
constexpr int kErrorSingleInputPathExpected = 904;
constexpr int kErrorWorkingPathExpected     = 905;

} // namespace

using common::logError;

Options parseOptions() {
	Options result;
	auto args = QCoreApplication::instance()->arguments();
	for (int i = 1, count = args.size(); i < count; ++i) { // skip first
		auto &arg = args.at(i);

		// Include paths
		if (arg == "-I") {
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

		// Working path
		} else if (arg == "-w") {
			if (++i == count) {
				logError(kErrorWorkingPathExpected, "Command Line") << "working path expected after -w";
				return Options();
			} else {
				common::logSetWorkingPath(args.at(i));
			}
		} else if (arg.startsWith("-w")) {
			common::logSetWorkingPath(arg.mid(2));

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
	result.isPalette = (QFileInfo(result.inputPath).suffix() == "palette");
	return result;
}

} // namespace style
} // namespace codegen
