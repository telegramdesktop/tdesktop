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
#include "window/chat_background.h"

#include "mainwidget.h"
#include "localstorage.h"

namespace Window {
namespace {

NeverFreedPointer<ChatBackground> instance;

} // namespace

bool ChatBackground::empty() const {
	return _image.isNull();
}

void ChatBackground::initIfEmpty() {
	if (empty()) {
		App::initBackground();
	}
}

void ChatBackground::init(int32 id, QPixmap &&image, QPixmap &&dog) {
	_id = id;
	_image = std_::move(image);
	_dog = std_::move(dog);

	notify(ChatBackgroundUpdate(ChatBackgroundUpdate::Type::New, _tile));
}

void ChatBackground::reset() {
	_id = 0;
	_image = QPixmap();
	_dog = QPixmap();
	_tile = false;

	notify(ChatBackgroundUpdate(ChatBackgroundUpdate::Type::New, _tile));
}

int32 ChatBackground::id() const {
	return _id;
}

const QPixmap &ChatBackground::image() const {
	return _image;
}

const QPixmap &ChatBackground::dog() const {
	return _dog;
}

bool ChatBackground::tile() const {
	return _tile;
}

void ChatBackground::setTile(bool tile) {
	if (_tile != tile) {
		_tile = tile;
		Local::writeUserSettings();
		notify(ChatBackgroundUpdate(ChatBackgroundUpdate::Type::Changed, _tile));
	}
}

ChatBackground *chatBackground() {
	instance.makeIfNull();
	return instance.data();
}

} // namespace Window
