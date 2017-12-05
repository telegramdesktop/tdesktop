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
#include "inline_bots/inline_bot_layout_item.h"

#include "data/data_photo.h"
#include "data/data_document.h"
#include "core/click_handler_types.h"
#include "inline_bots/inline_bot_result.h"
#include "inline_bots/inline_bot_layout_internal.h"
#include "storage/localstorage.h"
#include "mainwidget.h"
#include "ui/empty_userpic.h"

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

DocumentData *ItemBase::getPreviewDocument() const {
	auto previewDocument = [this]() -> DocumentData* {
		if (_doc) {
			return _doc;
		}
		if (_result) {
			return _result->_document;
		}
		return nullptr;
	};
	if (DocumentData *result = previewDocument()) {
		if (result->sticker() || result->loaded()) {
			return result;
		}
	}
	return nullptr;
}

PhotoData *ItemBase::getPreviewPhoto() const {
	if (_photo) {
		return _photo;
	}
	if (_result) {
		return _result->_photo;
	}
	return nullptr;
}

void ItemBase::preload() const {
	if (_result) {
		if (_result->_photo) {
			_result->_photo->thumb->load();
		} else if (_result->_document) {
			_result->_document->thumb->load();
		} else if (!_result->_thumb->isNull()) {
			_result->_thumb->load();
		}
	} else if (_doc) {
		_doc->thumb->load();
	} else if (_photo) {
		_photo->medium->load();
	}
}

void ItemBase::update() {
	if (_position >= 0) {
		context()->inlineItemRepaint(this);
	}
}

void ItemBase::layoutChanged() {
	if (_position >= 0) {
		context()->inlineItemLayoutChanged(this);
	}
}

std::unique_ptr<ItemBase> ItemBase::createLayout(not_null<Context*> context, Result *result, bool forceThumb) {
	using Type = Result::Type;

	switch (result->_type) {
	case Type::Photo: return std::make_unique<internal::Photo>(context, result); break;
	case Type::Audio:
	case Type::File: return std::make_unique<internal::File>(context, result); break;
	case Type::Video: return std::make_unique<internal::Video>(context, result); break;
	case Type::Sticker: return std::make_unique<internal::Sticker>(context, result); break;
	case Type::Gif: return std::make_unique<internal::Gif>(context, result); break;
	case Type::Article:
	case Type::Geo:
	case Type::Venue: return std::make_unique<internal::Article>(context, result, forceThumb); break;
	case Type::Game: return std::make_unique<internal::Game>(context, result); break;
	case Type::Contact: return std::make_unique<internal::Contact>(context, result); break;
	}
	return nullptr;
}

std::unique_ptr<ItemBase> ItemBase::createLayoutGif(not_null<Context*> context, DocumentData *document) {
	return std::make_unique<internal::Gif>(context, document, true);
}

DocumentData *ItemBase::getResultDocument() const {
	return _result ? _result->_document : nullptr;
}

PhotoData *ItemBase::getResultPhoto() const {
	return _result ? _result->_photo : nullptr;
}

ImagePtr ItemBase::getResultThumb() const {
	if (_result) {
		if (_result->_photo && !_result->_photo->thumb->isNull()) {
			return _result->_photo->thumb;
		}
		if (!_result->_thumb->isNull()) {
			return _result->_thumb;
		}
		return _result->_locationThumb;
	}
	return ImagePtr();
}

QPixmap ItemBase::getResultContactAvatar(int width, int height) const {
	if (_result->_type == Result::Type::Contact) {
		auto result = Ui::EmptyUserpic(
			Data::PeerUserpicColor(qHash(_result->_id)),
			_result->getLayoutTitle()
		).generate(width);
		if (result.height() != height * cIntRetinaFactor()) {
			result = result.scaled(QSize(width, height) * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		}
		return result;
	}
	return QPixmap();
}

int ItemBase::getResultDuration() const {
	return _result->_duration;
}

QString ItemBase::getResultUrl() const {
	return _result->_url;
}

ClickHandlerPtr ItemBase::getResultUrlHandler() const {
	if (!_result->_url.isEmpty()) {
		return MakeShared<UrlClickHandler>(_result->_url);
	}
	return ClickHandlerPtr();
}

ClickHandlerPtr ItemBase::getResultContentUrlHandler() const {
	if (!_result->_content_url.isEmpty()) {
		return MakeShared<UrlClickHandler>(_result->_content_url);
	}
	return ClickHandlerPtr();
}

QString ItemBase::getResultThumbLetter() const {
#ifndef OS_MAC_OLD
	auto parts = _result->_url.splitRef('/');
#else // OS_MAC_OLD
	auto parts = _result->_url.split('/');
#endif // OS_MAC_OLD
	if (!parts.isEmpty()) {
		auto domain = parts.at(0);
		if (parts.size() > 2 && domain.endsWith(':') && parts.at(1).isEmpty()) { // http:// and others
			domain = parts.at(2);
		}

		parts = domain.split('@').back().split('.');
		if (parts.size() > 1) {
			return parts.at(parts.size() - 2).at(0).toUpper();
		}
	}
	if (!_result->_title.isEmpty()) {
		return _result->_title.at(0).toUpper();
	}
	return QString();
}

namespace {

NeverFreedPointer<DocumentItems> documentItemsMap;

} // namespace

const DocumentItems *documentItems() {
	return documentItemsMap.data();
}

namespace internal {

void regDocumentItem(DocumentData *document, ItemBase *item) {
	documentItemsMap.createIfNull();
	(*documentItemsMap)[document].insert(item);
}

void unregDocumentItem(DocumentData *document, ItemBase *item) {
	if (documentItemsMap) {
		auto i = documentItemsMap->find(document);
		if (i != documentItemsMap->cend()) {
			if (i->remove(item) && i->isEmpty()) {
				documentItemsMap->erase(i);
			}
		}
	}
}

} // namespace internal

} // namespace Layout
} // namespace InlineBots
