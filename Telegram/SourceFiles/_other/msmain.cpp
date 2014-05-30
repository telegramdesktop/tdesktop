/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "msmain.h"

int main(int argc, char *argv[]) {
	QString classes_in("style_classes.txt"), classes_out("style_classes.h"), styles_in("style.txt"), styles_out("style_auto.h");
	for (int i = 0; i < argc; ++i) {
		if (string("-classes_in") == argv[i]) {
			if (++i < argc) classes_in = argv[i];
		} else if (string("-classes_out") == argv[i]) {
			if (++i < argc) classes_out = argv[i];
		} else if (string("-styles_in") == argv[i]) {
			if (++i < argc) styles_in = argv[i];
		} else if (string("-styles_out") == argv[i]) {
			if (++i < argc) styles_out = argv[i];
		}
	}
	QObject *taskImpl = new GenStyles(classes_in, classes_out, styles_in, styles_out);

	QGuiApplication a(argc, argv);

	QObject::connect(taskImpl, SIGNAL(finished()), &a, SLOT(quit()));
	QTimer::singleShot(0, taskImpl, SLOT(run()));

	return a.exec();
}
