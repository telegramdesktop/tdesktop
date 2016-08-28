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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "boxes/localstoragebox.h"

#include "localstorage.h"
#include "ui/flatbutton.h"
#include "lang.h"
#include "styles/style_boxes.h"
#include "mainwindow.h"

LocalStorageBox::LocalStorageBox() : AbstractBox()
, _clear(this, lang(lng_local_storage_clear), st::defaultBoxLinkButton)
, _close(this, lang(lng_box_ok), st::defaultBoxButton) {
	connect(_clear, SIGNAL(clicked()), this, SLOT(onClear()));
	connect(_close, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(update()));
	connect(App::wnd(), SIGNAL(tempDirCleared(int)), this, SLOT(onTempDirCleared(int)));
	connect(App::wnd(), SIGNAL(tempDirClearFailed(int)), this, SLOT(onTempDirClearFailed(int)));

	checkLocalStoredCounts();
	prepare();
}

void LocalStorageBox::updateControls() {
	int rowsHeight = 0;
	if (_imagesCount > 0 && _audiosCount > 0) {
		rowsHeight = 2 * (st::linkFont->height + st::localStorageBoxSkip);
	} else {
		rowsHeight = st::linkFont->height + st::localStorageBoxSkip;
	}
	_clear->setVisible(_imagesCount > 0 || _audiosCount > 0);
	setMaxHeight(st::boxTitleHeight + st::localStorageBoxSkip + rowsHeight + _clear->height() + st::boxButtonPadding.top() + _close->height() + st::boxButtonPadding.bottom());
	_clear->moveToLeft(st::boxPadding.left(), st::boxTitleHeight + st::localStorageBoxSkip + rowsHeight);
	_close->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _close->height());
	update();
}

void LocalStorageBox::showAll() {
	showChildren();
	_clear->setVisible(_imagesCount > 0 || _audiosCount > 0);
}

void LocalStorageBox::checkLocalStoredCounts() {
	int imagesCount = Local::hasImages() + Local::hasStickers() + Local::hasWebFiles();
	int audiosCount = Local::hasAudios();
	if (imagesCount != _imagesCount || audiosCount != _audiosCount) {
		_imagesCount = imagesCount;
		_audiosCount = audiosCount;
		if (_imagesCount > 0 || _audiosCount > 0) {
			_state = State::Normal;
		}
		updateControls();
	}
}

void LocalStorageBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_local_storage_title));

	p.setFont(st::boxTextFont);
	p.setPen(st::windowTextFg);
	checkLocalStoredCounts();
	int top = st::boxTitleHeight + st::localStorageBoxSkip;
	if (_imagesCount > 0) {
		auto text = lng_settings_images_cached(lt_count, _imagesCount, lt_size, formatSizeText(Local::storageImagesSize() + Local::storageStickersSize() + Local::storageWebFilesSize()));
		p.drawTextLeft(st::boxPadding.left(), top, width(), text);
		top += st::boxTextFont->height + st::localStorageBoxSkip;
	}
	if (_audiosCount > 0) {
		auto text = lng_settings_audios_cached(lt_count, _audiosCount, lt_size, formatSizeText(Local::storageAudiosSize()));
		p.drawTextLeft(st::boxPadding.left(), top, width(), text);
		top += st::boxTextFont->height + st::localStorageBoxSkip;
	} else if (_imagesCount <= 0) {
		p.drawTextLeft(st::boxPadding.left(), top, width(), lang(lng_settings_no_data_cached));
		top += st::boxTextFont->height + st::localStorageBoxSkip;
	}
	auto text = ([this]() -> QString {
		switch (_state) {
		case State::Clearing: return lang(lng_local_storage_clearing);
		case State::Cleared: return lang(lng_local_storage_cleared);
		case State::ClearFailed: return lang(lng_local_storage_clear_failed);
		}
		return QString();
	})();
	if (!text.isEmpty()) {
		p.drawTextLeft(st::boxPadding.left(), top, width(), text);
		top += st::boxTextFont->height + st::localStorageBoxSkip;
	}
}

void LocalStorageBox::onClear() {
	App::wnd()->tempDirDelete(Local::ClearManagerStorage);
	_state = State::Clearing;
	updateControls();
}

void LocalStorageBox::onTempDirCleared(int task) {
	if (task & Local::ClearManagerStorage) {
		_state = State::Cleared;
	}
	updateControls();
}

void LocalStorageBox::onTempDirClearFailed(int task) {
	if (task & Local::ClearManagerStorage) {
		_state = State::ClearFailed;
	}
	updateControls();
}
