/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
	case Type::Photo:
		return std::make_unique<internal::Photo>(context, result);
	case Type::Audio:
	case Type::File:
		return std::make_unique<internal::File>(context, result);
	case Type::Video:
		return std::make_unique<internal::Video>(context, result);
	case Type::Sticker:
		return std::make_unique<internal::Sticker>(context, result);
	case Type::Gif:
		return std::make_unique<internal::Gif>(context, result);
	case Type::Article:
	case Type::Geo:
	case Type::Venue:
		return std::make_unique<internal::Article>(
			context,
			result,
			forceThumb);
	case Type::Game:
		return std::make_unique<internal::Game>(context, result);
	case Type::Contact:
		return std::make_unique<internal::Contact>(context, result);
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
	return 0;
}

QString ItemBase::getResultUrl() const {
	return _result->_url;
}

ClickHandlerPtr ItemBase::getResultUrlHandler() const {
	if (!_result->_url.isEmpty()) {
		return std::make_shared<UrlClickHandler>(_result->_url);
	}
	return ClickHandlerPtr();
}

ClickHandlerPtr ItemBase::getResultContentUrlHandler() const {
	if (!_result->_content_url.isEmpty()) {
		return std::make_shared<UrlClickHandler>(_result->_content_url);
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

void regDocumentItem(
		not_null<const DocumentData*> document,
		not_null<ItemBase*> item) {
	documentItemsMap.createIfNull();
	(*documentItemsMap)[document].insert(item);
}

void unregDocumentItem(
		not_null<const DocumentData*> document,
		not_null<ItemBase*> item) {
	if (documentItemsMap) {
		auto i = documentItemsMap->find(document);
		if (i != documentItemsMap->cend()) {
			if (i->second.remove(item) && i->second.empty()) {
				documentItemsMap->erase(i);
			}
		}
		if (documentItemsMap->empty()) {
			documentItemsMap.clear();
		}
	}
}

} // namespace internal

} // namespace Layout
} // namespace InlineBots
