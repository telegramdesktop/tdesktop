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

#include "downloadpathbox.h"
#include "gui/filedialog.h"

DownloadPathBox::DownloadPathBox() :
	_path(cDownloadPath()),
	_tempRadio(this, qsl("dir_type"), 0, lang(lng_download_path_temp_radio), _path.isEmpty()),
	_dirRadio(this, qsl("dir_type"), 1, lang(lng_download_path_dir_radio), !_path.isEmpty()),
	_dirInput(this, st::inpDownloadDir, QString(), QDir::toNativeSeparators(_path)),
	_saveButton(this, lang(lng_connection_save), st::btnSelectDone),
	_cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
	a_opacity(0, 1), _hiding(false) {

	_width = st::addContactWidth;

	connect(&_saveButton, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onCancel()));

	connect(&_tempRadio, SIGNAL(changed()), this, SLOT(onChange()));
	connect(&_dirRadio, SIGNAL(changed()), this, SLOT(onChange()));

	connect(&_dirInput, SIGNAL(focused()), this, SLOT(onEditPath()));
	_dirInput.setCursorPosition(0);

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
}

void DownloadPathBox::hideAll() {
	_tempRadio.hide();
	_dirRadio.hide();

	_dirInput.hide();

	_saveButton.hide();
	_cancelButton.hide();
}

void DownloadPathBox::showAll() {
	_tempRadio.show();
	_dirRadio.show();

	if (_dirRadio.checked()) {
		_dirInput.show();
	} else {
		_dirInput.hide();
	}

	_saveButton.show();
	_cancelButton.show();

	_tempRadio.move(st::boxPadding.left(), st::addContactTitleHeight + st::downloadSkip);
	_dirRadio.move(st::boxPadding.left(), _tempRadio.y() + _tempRadio.height() + st::downloadSkip);
	int32 inputy = _dirRadio.y() + _dirRadio.height() + st::boxPadding.top();

	_dirInput.move(st::boxPadding.left() + st::rbDefFlat.textLeft, inputy);
		
	int32 buttony = (_dirRadio.checked() ? (_dirInput.y() + _dirInput.height()) : (_dirRadio.y() + _dirRadio.height())) + st::downloadSkip;

	_saveButton.move(_width - _saveButton.width(), buttony);
	_cancelButton.move(0, buttony);

	_height = _saveButton.y() + _saveButton.height();
	resize(_width, _height);
}

void DownloadPathBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
	} else if (e->key() == Qt::Key_Escape) {
		onCancel();
	}
}

void DownloadPathBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void DownloadPathBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(0, 0, _width, _height, st::boxBG->b);

			// paint shadows
			p.fillRect(0, st::addContactTitleHeight, _width, st::scrollDef.topsh, st::scrollDef.shColor->b);
			p.fillRect(0, _height - st::btnSelectCancel.height - st::scrollDef.bottomsh, _width, st::scrollDef.bottomsh, st::scrollDef.shColor->b);

			// paint button sep
			p.fillRect(st::btnSelectCancel.width, _height - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);

			// draw box title / text
			p.setFont(st::addContactTitleFont->f);
			p.setPen(st::black->p);
			p.drawText(st::addContactTitlePos.x(), st::addContactTitlePos.y() + st::addContactTitleFont->ascent, lang(lng_download_path_header));
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void DownloadPathBox::animStep(float64 dt) {
	if (dt >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
		}
	} else {
		a_opacity.update(dt, anim::linear);
	}
	update();
}

void DownloadPathBox::onChange() {
	if (_dirRadio.checked()) {
		if (_path.isEmpty()) {
			_tempRadio.setChecked(true);
			onEditPath();
			if (!_path.isEmpty()) {
				_dirRadio.setChecked(true);
			}
		} else {
			_dirInput.setText(QDir::toNativeSeparators(_path));
			_dirInput.setCursorPosition(0);
		}
	}
	showAll();
	update();
}

void DownloadPathBox::onEditPath() {
	_dirInput.clearFocus();

	filedialogInit();
	QString path, lastPath = cDialogLastPath();
	if (!cDownloadPath().isEmpty()) cSetDialogLastPath(cDownloadPath());
	if (filedialogGetDir(path, lang(lng_download_path_choose))) {
		if (!path.isEmpty()) {
			_path = path + '/';
			_dirInput.setText(QDir::toNativeSeparators(_path));
			_dirInput.setCursorPosition(0);
		}
	}
	cSetDialogLastPath(lastPath);
}

void DownloadPathBox::onSave() {
	cSetDownloadPath(_tempRadio.checked() ? QString() : _path);
	App::writeUserConfig();
	emit closed();
}

void DownloadPathBox::onCancel() {
	emit closed();
}

void DownloadPathBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

DownloadPathBox::~DownloadPathBox() {
}
