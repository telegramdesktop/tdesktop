/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <QtGui/QGuiApplication>

#include "codegen/emoji/options.h"
#include "codegen/emoji/generator.h"

int main(int argc, char *argv[]) {
#ifdef SUPPORT_IMAGE_GENERATION
#ifndef Q_OS_MAC
#error "Image generation is supported only on macOS"
#endif // Q_OS_MAC
	QGuiApplication app(argc, argv);
#else // SUPPORT_IMAGE_GENERATION
	QCoreApplication app(argc, argv);
#endif // SUPPORT_IMAGE_GENERATION

	auto options = codegen::emoji::parseOptions();

	codegen::emoji::Generator generator(options);
	return generator.generate();
}
