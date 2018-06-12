/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <QtCore/QCoreApplication>

#include "codegen/lang/options.h"
#include "codegen/lang/processor.h"

int main(int argc, char *argv[]) {
	QCoreApplication app(argc, argv);

	auto options = codegen::lang::parseOptions();
	if (options.inputPath.isEmpty()) {
		return -1;
	}

	codegen::lang::Processor processor(options);
	return processor.launch();
}
