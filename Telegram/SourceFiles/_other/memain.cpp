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
#include "memain.h"

int main(int argc, char *argv[]) {
	QString emoji_in("./Resources/art/emojisprite_"), emoji_out("./SourceFiles/gui/emoji_config.cpp"), emoji_png("./Resources/art/emoji");
	for (int i = 0; i < argc; ++i) {
		if (string("-emoji_in") == argv[i]) {
			if (++i < argc) emoji_in = argv[i];
		} else if (string("-emoji_out") == argv[i]) {
			if (++i < argc) emoji_out = argv[i];
		} else if (string("-emoji_png") == argv[i]) {
			if (++i < argc) emoji_png = argv[i];
		}
	}
#ifdef Q_OS_MAC
    if (QDir(QString()).absolutePath() == "/") {
        QString first = argc ? QString::fromLocal8Bit(argv[0]) : QString();
        if (!first.isEmpty()) {
            QFileInfo info(first);
            if (info.exists()) {
                QDir result(info.absolutePath() + "/../../..");
                QString basePath = result.absolutePath() + '/';
                emoji_in = basePath + emoji_in;
                emoji_out = basePath + emoji_out;
				emoji_png = basePath + emoji_png;
            }
        }
    }
#endif
	QObject *taskImpl = new GenEmoji(emoji_in, emoji_out, emoji_png);

	QGuiApplication a(argc, argv);

	QObject::connect(taskImpl, SIGNAL(finished()), &a, SLOT(quit()));
	QTimer::singleShot(0, taskImpl, SLOT(run()));

	return a.exec();
}
