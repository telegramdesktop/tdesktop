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
#include "stdafx.h"
#include "lang.h"

#include "aboutbox.h"
#include "mainwidget.h"
#include "window.h"

AboutBox::AboutBox() :
_done(this, lang(lng_about_done), st::aboutCloseButton),
_text(this, lang(lng_about_text), st::aboutLabel, st::aboutTextStyle),
_hiding(false), a_opacity(0, 1) {

	_width = st::aboutWidth;
	_height = st::aboutHeight;

	_text.move(0, st::aboutTextTop);

	_headerWidth = st::aboutHeaderFont->m.width(qsl("Telegram "));
	_subheaderWidth = st::aboutSubheaderFont->m.width(qsl("Desktop"));

	_versionText = lang(lng_about_version).replace(qsl("{version}"), QString::fromWCharArray(AppVersionStr));
	_versionWidth = st::aboutVersionFont->m.width(_versionText);

	_done.move(0, _height - _done.height());

	connect(&_done, SIGNAL(clicked()), this, SLOT(onClose()));

	resize(_width, _height);

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
}

void AboutBox::hideAll() {
	_done.hide();
	_text.hide();
}

void AboutBox::showAll() {
	_done.show();
	_text.show();
}

void AboutBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onClose();
	} else if (e->key() == Qt::Key_Escape) {
		onClose();
	}
}

void AboutBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void AboutBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(0, 0, _width, _height, st::boxBG->b);

			p.drawPixmap(QPoint((_width - st::aboutIcon.pxWidth()) / 2, st::aboutIconTop), App::sprite(), st::aboutIcon);

			p.setPen(st::black->p);
			p.setFont(st::aboutHeaderFont->f);
			p.drawText((_width - (_headerWidth + _subheaderWidth)) / 2, st::aboutHeaderTop + st::aboutHeaderFont->ascent, qsl("Telegram"));

			p.setFont(st::aboutSubheaderFont->f);
			p.drawText((_width - (_headerWidth + _subheaderWidth)) / 2 + _headerWidth, st::aboutHeaderTop + st::aboutSubheaderFont->ascent, qsl("Desktop"));

			p.setFont(st::aboutVersionFont->f);
			p.setPen(st::aboutVersionColor->p);
			p.drawText((_width - _versionWidth) / 2, st::aboutVersionTop + st::aboutVersionFont->ascent, _versionText);
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void AboutBox::animStep(float64 ms) {
	if (ms >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
			setFocus();
		}
	} else {
		a_opacity.update(ms, anim::linear);
	}
	update();
}

void AboutBox::onClose() {
	emit closed();
}

void AboutBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

AboutBox::~AboutBox() {
}
