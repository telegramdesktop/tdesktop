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

#include "localstorage.h"

#include "downloadpathbox.h"
#include "gui/filedialog.h"

DownloadPathBox::DownloadPathBox() :
	_path(cDownloadPath()),
	_default(this, qsl("dir_type"), 0, lang(lng_download_path_default_radio), _path.isEmpty()),
	_temp(this, qsl("dir_type"), 1, lang(lng_download_path_temp_radio), _path == qsl("tmp")),
	_dir(this, qsl("dir_type"), 2, lang(lng_download_path_dir_radio), !_path.isEmpty() && _path != qsl("tmp")),
	_pathLink(this, QString(), st::defaultBoxLinkButton),
	_save(this, lang(lng_connection_save), st::defaultBoxButton),
	_cancel(this, lang(lng_cancel), st::cancelBoxButton) {

	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_default, SIGNAL(changed()), this, SLOT(onChange()));
	connect(&_temp, SIGNAL(changed()), this, SLOT(onChange()));
	connect(&_dir, SIGNAL(changed()), this, SLOT(onChange()));

	connect(&_pathLink, SIGNAL(clicked()), this, SLOT(onEditPath()));
	if (!_path.isEmpty() && _path != qsl("tmp")) {
		setPathText(QDir::toNativeSeparators(_path));
	}
	prepare();
}

void DownloadPathBox::hideAll() {
	_default.hide();
	_temp.hide();
	_dir.hide();

	_pathLink.hide();

	_save.hide();
	_cancel.hide();
}

void DownloadPathBox::showAll() {
	_default.show();
	_temp.show();
	_dir.show();

	if (_dir.checked()) {
		_pathLink.show();
	} else {
		_pathLink.hide();
	}

	_save.show();
	_cancel.show();

	int32 h = st::boxTitleHeight + st::boxOptionListPadding.top() + _default.height() + st::boxOptionListPadding.top() + _temp.height() + st::boxOptionListPadding.top() + _dir.height();
	if (_dir.checked()) h += st::downloadPathSkip + _pathLink.height();
	h += st::boxOptionListPadding.bottom() + st::boxButtonPadding.top() + _save.height() + st::boxButtonPadding.bottom();
	
	setMaxHeight(h);
}

void DownloadPathBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_download_path_header));
}

void DownloadPathBox::resizeEvent(QResizeEvent *e) {
	_default.moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), st::boxTitleHeight + st::boxOptionListPadding.top());
	_temp.moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _default.y() + _default.height() + st::boxOptionListPadding.top());
	_dir.moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _temp.y() + _temp.height() + st::boxOptionListPadding.top());
	int32 inputx = st::boxPadding.left() + st::boxOptionListPadding.left() + st::defaultRadiobutton.textPosition.x();
	int32 inputy = _dir.y() + _dir.height() + st::downloadPathSkip;

	_pathLink.moveToLeft(inputx, inputy);

	_save.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _save.width() + st::boxButtonPadding.left(), _save.y());
}

void DownloadPathBox::onChange() {
	if (_dir.checked()) {
		if (_path.isEmpty() || _path == qsl("tmp")) {
			(_path.isEmpty() ? _default : _temp).setChecked(true);
			onEditPath();
			if (!_path.isEmpty() && _path != qsl("tmp")) {
				_dir.setChecked(true);
			}
		} else {
			setPathText(QDir::toNativeSeparators(_path));
		}
	} else if (_temp.checked()) {
		_path = qsl("tmp");
	} else {
		_path = QString();
	}
	showAll();
	update();
}

void DownloadPathBox::onEditPath() {
	filedialogInit();
	QString path, lastPath = cDialogLastPath();
	if (!cDownloadPath().isEmpty()) {
		cSetDialogLastPath(cDownloadPath());
	}
	if (filedialogGetDir(path, lang(lng_download_path_choose))) {
		if (!path.isEmpty()) {
			_path = path + '/';
			setPathText(QDir::toNativeSeparators(_path));
		}
	}
	cSetDialogLastPath(lastPath);
}

void DownloadPathBox::onSave() {
	cSetDownloadPath(_default.checked() ? QString() : (_temp.checked() ? qsl("tmp") : _path));
	Local::writeUserSettings();
	emit closed();
}

void DownloadPathBox::setPathText(const QString &text) {
	int32 availw = st::boxWideWidth - st::boxPadding.left() - st::defaultRadiobutton.textPosition.x() - st::boxPadding.right();
	_pathLink.setText(st::boxTextFont->elided(text, availw));
}
