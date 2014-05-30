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
#include <QtCore/QMap>
#include <QtCore/QVector>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QBuffer>
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtGui/QPainter>
#include <iostream>
#include <exception>
#include <QtCore/QTextStream>
#include <QtCore/QString>
#include <QtCore/QCoreApplication>
#include <QtGui/QGuiApplication>

using std::string;
using std::cout;
using std::cerr;
using std::exception;

bool genEmoji(QString emoji_in, const QString &emoji_out);

class GenEmoji : public QObject {
	Q_OBJECT

public:
	GenEmoji(const QString &emoji_in, const QString &emoji_out) : QObject(0),
		_emoji_in(emoji_in), _emoji_out(emoji_out) {
	}

	public slots :
		void run()  {
			if (genEmoji(_emoji_in, _emoji_out)) {
				emit finished();
			}
		}

signals:
	void finished();

private:

	QString _emoji_in, _emoji_out;
};
