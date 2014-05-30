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
#include "mlmain.h"

int main(int argc, char *argv[]) {
	QString lang_in("lang.txt"), lang_out("lang");
	for (int i = 0; i < argc; ++i) {
		if (string("-lang_in") == argv[i]) {
			if (++i < argc) lang_in = argv[i];
		} else if (string("-lang_out") == argv[i]) {
			if (++i < argc) lang_out = argv[i];
		}
	}
	QObject *taskImpl = new GenLang(lang_in, lang_out);

	QCoreApplication a(argc, argv);

	QObject::connect(taskImpl, SIGNAL(finished()), &a, SLOT(quit()));
	QTimer::singleShot(0, taskImpl, SLOT(run()));

	return a.exec();
}
