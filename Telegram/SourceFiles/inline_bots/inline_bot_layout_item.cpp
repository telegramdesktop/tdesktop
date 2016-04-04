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
#include "inline_bots/inline_bot_layout_item.h"

#include "inline_bots/inline_bot_result.h"
#include "inline_bots/inline_bot_layout_internal.h"
#include "localstorage.h"
#include "mainwidget.h"

namespace InlineBots {
namespace Layout {

void ItemBase::setPosition(int32 position) {
	_position = position;
}

int32 ItemBase::position() const {
	return _position;
}

Result *ItemBase::getResult() const {
	return _result;
}

DocumentData *ItemBase::getDocument() const {
	return _doc;
}

PhotoData *ItemBase::getPhoto() const {
	return _photo;
}

void ItemBase::preload() const {
	if (_result) {
		if (_result->photo) {
			_result->photo->thumb->load();
		} else if (_result->document) {
			_result->document->thumb->load();
		} else if (!_result->thumb->isNull()) {
			_result->thumb->load();
		}
	} else if (_doc) {
		_doc->thumb->load();
	} else if (_photo) {
		_photo->medium->load();
	}
}

void ItemBase::update() {
	if (_position >= 0) {
		Ui::repaintInlineItem(this);
	}
}

UniquePointer<ItemBase> ItemBase::createLayout(Result *result, bool forceThumb) {
	using Type = Result::Type;

	switch (result->type) {
	case Type::Photo: return MakeUnique<internal::Photo>(result); break;
	case Type::Audio:
	case Type::File: return MakeUnique<internal::File>(result); break;
	case Type::Video: return MakeUnique<internal::Video>(result); break;
	case Type::Sticker: return MakeUnique<internal::Sticker>(result); break;
	case Type::Gif: return MakeUnique<internal::Gif>(result); break;
	case Type::Article:
	case Type::Contact:
	case Type::Venue: return MakeUnique<internal::Article>(result, forceThumb); break;
	}
	return UniquePointer<ItemBase>();
}

UniquePointer<ItemBase> ItemBase::createLayoutGif(DocumentData *document) {
	return MakeUnique<internal::Gif>(document, true);
}

} // namespace Layout
} // namespace InlineBots
