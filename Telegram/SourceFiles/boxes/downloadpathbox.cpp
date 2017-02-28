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
#include "stdafx.h"
#include "boxes/downloadpathbox.h"

#include "lang.h"
#include "localstorage.h"
#include "core/file_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "pspecific.h"
#include "styles/style_boxes.h"

DownloadPathBox::DownloadPathBox(QWidget *parent)
: _path(Global::DownloadPath())
, _pathBookmark(Global::DownloadPathBookmark())
, _default(this, qsl("dir_type"), 0, lang(lng_download_path_default_radio), _path.isEmpty(), st::defaultBoxCheckbox)
, _temp(this, qsl("dir_type"), 1, lang(lng_download_path_temp_radio), (_path == qsl("tmp")), st::defaultBoxCheckbox)
, _dir(this, qsl("dir_type"), 2, lang(lng_download_path_dir_radio), (!_path.isEmpty() && _path != qsl("tmp")), st::defaultBoxCheckbox)
, _pathLink(this, QString(), st::boxLinkButton) {
}

void DownloadPathBox::prepare() {
	addButton(lang(lng_connection_save), [this] { save(); });
	addButton(lang(lng_cancel), [this] { closeBox(); });

	setTitle(lang(lng_download_path_header));

	connect(_default, SIGNAL(changed()), this, SLOT(onChange()));
	connect(_temp, SIGNAL(changed()), this, SLOT(onChange()));
	connect(_dir, SIGNAL(changed()), this, SLOT(onChange()));

	connect(_pathLink, SIGNAL(clicked()), this, SLOT(onEditPath()));
	if (!_path.isEmpty() && _path != qsl("tmp")) {
		setPathText(QDir::toNativeSeparators(_path));
	}
	updateControlsVisibility();
}

void DownloadPathBox::updateControlsVisibility() {
	_pathLink->setVisible(_dir->checked());

	auto newHeight = st::boxOptionListPadding.top() + _default->heightNoMargins() + st::boxOptionListSkip + _temp->heightNoMargins() + st::boxOptionListSkip + _dir->heightNoMargins();
	if (_dir->checked()) {
		newHeight += st::downloadPathSkip + _pathLink->height();
	}
	newHeight += st::boxOptionListPadding.bottom();

	setDimensions(st::boxWideWidth, newHeight);
}

void DownloadPathBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_default->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), st::boxOptionListPadding.top());
	_temp->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _default->bottomNoMargins() + st::boxOptionListSkip);
	_dir->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _temp->bottomNoMargins() + st::boxOptionListSkip);
	auto inputx = st::boxPadding.left() + st::boxOptionListPadding.left() + st::defaultBoxCheckbox.textPosition.x();
	auto inputy = _dir->bottomNoMargins() + st::downloadPathSkip;

	_pathLink->moveToLeft(inputx, inputy);
}

void DownloadPathBox::onChange() {
	if (_dir->checked()) {
		if (_path.isEmpty() || _path == qsl("tmp")) {
			(_path.isEmpty() ? _default : _temp)->setChecked(true);
			onEditPath();
			if (!_path.isEmpty() && _path != qsl("tmp")) {
				_dir->setChecked(true);
			}
		} else {
			setPathText(QDir::toNativeSeparators(_path));
		}
	} else if (_temp->checked()) {
		_path = qsl("tmp");
	} else {
		_path = QString();
	}
	updateControlsVisibility();
	update();
}

void DownloadPathBox::onEditPath() {
	auto initialPath = [] {
		if (!Global::DownloadPath().isEmpty() && Global::DownloadPath() != qstr("tmp")) {
			return Global::DownloadPath().left(Global::DownloadPath().size() - (Global::DownloadPath().endsWith('/') ? 1 : 0));
		}
		return QString();
	};
	FileDialog::GetFolder(lang(lng_download_path_choose), initialPath(), base::lambda_guarded(this, [this](const QString &result) {
		if (!result.isEmpty()) {
			_path = result + '/';
			_pathBookmark = psDownloadPathBookmark(_path);
			setPathText(QDir::toNativeSeparators(_path));
		}
	}));
}

void DownloadPathBox::save() {
#ifndef OS_WIN_STORE
	Global::SetDownloadPath(_default->checked() ? QString() : (_temp->checked() ? qsl("tmp") : _path));
	Global::SetDownloadPathBookmark((_default->checked() || _temp->checked()) ? QByteArray() : _pathBookmark);
	Local::writeUserSettings();
	Global::RefDownloadPathChanged().notify();
	closeBox();
#endif // OS_WIN_STORE
}

void DownloadPathBox::setPathText(const QString &text) {
	auto availw = st::boxWideWidth - st::boxPadding.left() - st::defaultBoxCheckbox.textPosition.x() - st::boxPadding.right();
	_pathLink->setText(st::boxTextFont->elided(text, availw));
}
