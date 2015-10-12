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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "aboutbox.h"
#include "mainwidget.h"
#include "window.h"

AboutBox::AboutBox() : AbstractBox(st::aboutWidth)
, _version(this, lng_about_version(lt_version, QString::fromWCharArray(AppVersionStr) + (cDevVersion() ? " dev" : "")), st::aboutVersionLink)
, _text1(this, lang(lng_about_text_1), st::aboutLabel, st::aboutTextStyle)
, _text2(this, lang(lng_about_text_2), st::aboutLabel, st::aboutTextStyle)
, _text3(this, QString(), st::aboutLabel, st::aboutTextStyle)
, _done(this, lang(lng_close), st::defaultBoxButton) {
	_text3.setRichText(lng_about_text_3(lt_faq_open, qsl("[a href=\"%1\"]").arg(telegramFaqLink()), lt_faq_close, qsl("[/a]")));

	setMaxHeight(st::boxTitleHeight + st::aboutTextTop + _text1.height() + st::aboutSkip + _text2.height() + st::aboutSkip + _text3.height() + st::boxButtonPadding.top() + _done.height() + st::boxButtonPadding.bottom());

	connect(&_version, SIGNAL(clicked()), this, SLOT(onVersion()));
	connect(&_done, SIGNAL(clicked()), this, SLOT(onClose()));

	prepare();
}

void AboutBox::hideAll() {
	_version.hide();
	_text1.hide();
	_text2.hide();
	_text3.hide();
	_done.hide();
}

void AboutBox::showAll() {
	_version.show();
	_text1.show();
	_text2.show();
	_text3.show();
	_done.show();
}

void AboutBox::resizeEvent(QResizeEvent *e) {
	_version.moveToLeft(st::boxPadding.left(), st::boxTitleHeight + st::aboutVersionTop);
	_text1.moveToLeft(st::boxPadding.left(), st::boxTitleHeight + st::aboutTextTop);
	_text2.moveToLeft(st::boxPadding.left(), _text1.y() + _text1.height() + st::aboutSkip);
	_text3.moveToLeft(st::boxPadding.left(), _text2.y() + _text2.height() + st::aboutSkip);
	_done.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _done.height());
}

void AboutBox::onVersion() {
	QDesktopServices::openUrl(qsl("https://desktop.telegram.org/?_hash=changelog"));
}

void AboutBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onClose();
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void AboutBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, qsl("Telegram Desktop"));
}

QString telegramFaqLink() {
	QString result = qsl("https://telegram.org/faq");
	if (cLang() > languageDefault && cLang() < languageCount) {
		const char *code = LanguageCodes[cLang()];
		if (qstr("de") == code || qstr("es") == code || qstr("it") == code || qstr("ko") == code) {
			result += qsl("/") + code;
		} else if (qstr("pt_BR") == code) {
			result += qsl("/br");
		}
	}
	return result;
}