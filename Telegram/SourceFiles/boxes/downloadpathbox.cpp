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

#include "localstorage.h"

#include "downloadpathbox.h"
#include "gui/filedialog.h"

DownloadPathBox::DownloadPathBox() :
	_path(cDownloadPath()),
	_defaultRadio(this, qsl("dir_type"), 0, lang(lng_download_path_default_radio), _path.isEmpty()),
	_tempRadio(this, qsl("dir_type"), 1, lang(lng_download_path_temp_radio), _path == qsl("tmp")),
	_dirRadio(this, qsl("dir_type"), 2, lang(lng_download_path_dir_radio), !_path.isEmpty() && _path != qsl("tmp")),
	_dirInput(this, st::inpDownloadDir, QString(), (_path.isEmpty() || _path == qsl("tmp")) ? QString() : QDir::toNativeSeparators(_path)),
	_saveButton(this, lang(lng_connection_save), st::btnSelectDone),
	_cancelButton(this, lang(lng_cancel), st::btnSelectCancel) {

	connect(&_saveButton, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_defaultRadio, SIGNAL(changed()), this, SLOT(onChange()));
	connect(&_tempRadio, SIGNAL(changed()), this, SLOT(onChange()));
	connect(&_dirRadio, SIGNAL(changed()), this, SLOT(onChange()));

	connect(&_dirInput, SIGNAL(focused()), this, SLOT(onEditPath()));
	_dirInput.setCursorPosition(0);

	prepare();
}

void DownloadPathBox::hideAll() {
	_defaultRadio.hide();
	_tempRadio.hide();
	_dirRadio.hide();

	_dirInput.hide();

	_saveButton.hide();
	_cancelButton.hide();
}

void DownloadPathBox::showAll() {
	_defaultRadio.show();
	_tempRadio.show();
	_dirRadio.show();

	if (_dirRadio.checked()) {
		_dirInput.show();
	} else {
		_dirInput.hide();
	}

	_saveButton.show();
	_cancelButton.show();

	int32 h = st::boxTitleHeight + st::downloadSkip + _defaultRadio.height() + st::downloadSkip + _tempRadio.height() + st::downloadSkip + _dirRadio.height();
	if (_dirRadio.checked()) h += st::boxPadding.top() + _dirInput.height();
	h += st::downloadSkip + _saveButton.height();
	
	setMaxHeight(h);
}

void DownloadPathBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_download_path_header), true);

	// paint shadows
	p.fillRect(0, height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);

	// paint button sep
	p.fillRect(st::btnSelectCancel.width, height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);
}

void DownloadPathBox::resizeEvent(QResizeEvent *e) {
	_defaultRadio.move(st::boxPadding.left(), st::boxTitleHeight + st::downloadSkip);
	_tempRadio.move(st::boxPadding.left(), _defaultRadio.y() + _defaultRadio.height() + st::downloadSkip);
	_dirRadio.move(st::boxPadding.left(), _tempRadio.y() + _tempRadio.height() + st::downloadSkip);
	int32 inputy = _dirRadio.y() + _dirRadio.height() + st::boxPadding.top();

	_dirInput.move(st::boxPadding.left() + st::rbDefFlat.textLeft, inputy);

	int32 buttony = (_dirRadio.checked() ? (_dirInput.y() + _dirInput.height()) : (_dirRadio.y() + _dirRadio.height())) + st::downloadSkip;

	_saveButton.move(width() - _saveButton.width(), buttony);
	_cancelButton.move(0, buttony);
}

void DownloadPathBox::onChange() {
	if (_dirRadio.checked()) {
		if (_path.isEmpty() || _path == qsl("tmp")) {
			(_path.isEmpty() ? _defaultRadio : _tempRadio).setChecked(true);
			onEditPath();
			if (!_path.isEmpty() && _path != qsl("tmp")) {
				_dirRadio.setChecked(true);
			}
		} else {
			_dirInput.setText(QDir::toNativeSeparators(_path));
			_dirInput.setCursorPosition(0);
		}
	} else if (_tempRadio.checked()) {
		_path = qsl("tmp");
	} else {
		_path = QString();
	}
	showAll();
	update();
}

void DownloadPathBox::onEditPath() {
	_dirInput.clearFocus();

	filedialogInit();
	QString path, lastPath = cDialogLastPath();
	if (!cDownloadPath().isEmpty()) {
		cSetDialogLastPath(cDownloadPath());
	}
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
	cSetDownloadPath(_defaultRadio.checked() ? QString() : (_tempRadio.checked() ? qsl("tmp") : _path));
	Local::writeUserSettings();
	emit closed();
}
