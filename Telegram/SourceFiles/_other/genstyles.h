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
#include <QtGui/QBitmap>
#include <QtCore/QBuffer>
#include <QtCore/QFileInfo>
#include <QtCore/QFile>
#include <iostream>
#include <exception>
#include <QtCore/QTextStream>
#include <QtCore/QString>
#include <QtCore/QCoreApplication>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>

using std::string;
using std::cout;
using std::cerr;
using std::exception;

bool genStyles(const QString &classes_in, const QString &classes_out, const QString &styles_in, const QString &styles_out);

class GenStyles : public QObject {
    Q_OBJECT

public:
    GenStyles(const QString &classes_in, const QString &classes_out, const QString &styles_in, const QString styles_out) : QObject(0),
	_classes_in(classes_in), _classes_out(classes_out), _styles_in(styles_in), _styles_out(styles_out) {
	}

public slots:
	void run()  {
		if (genStyles(_classes_in, _classes_out, _styles_in, _styles_out)) {
			emit finished();
		}
	}

signals:
    void finished();

private:

	QString _classes_in, _classes_out, _styles_in, _styles_out;
};
