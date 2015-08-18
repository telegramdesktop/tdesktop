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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "aboutbox.h"
#include "mainwidget.h"
#include "window.h"

AboutBox::AboutBox() :
_done(this, lang(lng_about_done), st::aboutCloseButton),
_version(this, qsl("[a href=\"https://desktop.telegram.org/#changelog\"]") + textClean(lng_about_version(lt_version, QString::fromWCharArray(AppVersionStr) + (cDevVersion() ? " dev" : ""))) + qsl("[/a]"), st::aboutVersion, st::defaultTextStyle),
_text(this, lang(lng_about_text), st::aboutLabel, st::aboutTextStyle) {
	
	resizeMaxHeight(st::aboutWidth, st::aboutHeight);

	_version.move(0, st::aboutVersionTop);
	_text.move(0, st::aboutTextTop);

	_headerWidth = st::aboutHeaderFont->m.width(qsl("Telegram "));
	_subheaderWidth = st::aboutSubheaderFont->m.width(qsl("Desktop"));

	_done.move(0, height() - _done.height());

	connect(&_done, SIGNAL(clicked()), this, SLOT(onClose()));

	prepare();
}

void AboutBox::hideAll() {
	_done.hide();
	_version.hide();
	_text.hide();
}

void AboutBox::showAll() {
	_done.show();
	_version.show();
	_text.show();
}

void AboutBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onClose();
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void AboutBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (paint(p)) return;

	p.drawPixmap(QPoint((width() - st::aboutIcon.pxWidth()) / 2, st::aboutIconTop), App::sprite(), st::aboutIcon);

	p.setPen(st::black->p);
	p.setFont(st::aboutHeaderFont->f);
	p.drawText((width() - (_headerWidth + _subheaderWidth)) / 2, st::aboutHeaderTop + st::aboutHeaderFont->ascent, qsl("Telegram"));

	p.setFont(st::aboutSubheaderFont->f);
	p.drawText((width() - (_headerWidth + _subheaderWidth)) / 2 + _headerWidth, st::aboutHeaderTop + st::aboutSubheaderFont->ascent, qsl("Desktop"));
}
