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

class Exception : public exception {
public:
    
	Exception(const QString &msg) : _msg(msg.toUtf8()) {
	}
    
    virtual const char *what() const throw() {
        return _msg.constData();
    }
    virtual ~Exception() throw() {
    }
    
private:
	QByteArray _msg;
};

bool genLang(const QString &lang_in, const QString &lang_out);

class GenLang : public QObject {
	Q_OBJECT

public:
	GenLang(const QString &lang_in, const QString &lang_out) : QObject(0),
		_lang_in(lang_in), _lang_out(lang_out) {
	}

	public slots :
		void run()  {
			if (genLang(_lang_in, _lang_out)) {
				emit finished();
			}
		}

signals:
	void finished();

private:

	QString _lang_in, _lang_out;
};
