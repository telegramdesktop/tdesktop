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
