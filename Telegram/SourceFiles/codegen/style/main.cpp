/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <QtCore/QCoreApplication>

#include "codegen/style/options.h"
#include "codegen/style/processor.h"

int main(int argc, char *argv[]) {
	QCoreApplication app(argc, argv);

	auto options = codegen::style::parseOptions();
	if (options.inputPath.isEmpty()) {
		return -1;
	}

	codegen::style::Processor processor(options);
	return processor.launch();
}
