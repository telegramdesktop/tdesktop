/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_bot_layout_item.h"

#include "base/never_freed_pointer.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_file_origin.h"
#include "data/data_document_media.h"
#include "core/click_handler_types.h"
#include "inline_bots/inline_bot_result.h"
#include "inline_bots/inline_bot_layout_internal.h"
#include "storage/localstorage.h"
#include "mainwidget.h"
#include "ui/image/image.h"
#include "ui/empty_userpic.h"

namespace InlineBots {
namespace Layout {
namespace {

base::NeverFreedPointer<DocumentItems> documentItemsMap;

} // namespace

std::shared_ptr<Result> ItemBase::getResult() const {
	return _result;
}

DocumentData *ItemBase::getDocument() const {
	return _document;
}

PhotoData *ItemBase::getPhoto() const {
	return _photo;
}

DocumentData *ItemBase::getPreviewDocument() const {
	if (_document) {
		return _document;
	} else if (_result) {
		return _result->_document;
	}
	return nullptr;
}

PhotoData *ItemBase::getPreviewPhoto() const {
	if (_photo) {
		return _photo;
	} else if (_result) {
		return _result->_photo;
	}
	return nullptr;
}

void ItemBase::preload() const {
	const auto origin = fileOrigin();
	if (_result) {
		if (const auto photo = _result->_photo) {
			if (photo->hasExact(Data::PhotoSize::Thumbnail)) {
				photo->load(Data::PhotoSize::Thumbnail, origin);
			}
		} else if (const auto document = _result->_document) {
			document->loadThumbnail(origin);
		} else if (auto &thumb = _result->_thumbnail; !thumb.empty()) {
			thumb.load(_result->_session, origin);
		}
	} else if (_document) {
		_document->loadThumbnail(origin);
	} else if (_photo && _photo->hasExact(Data::PhotoSize::Thumbnail)) {
		_photo->load(Data::PhotoSize::Thumbnail, origin);
	}
}

void ItemBase::update() const {
	if (_position >= 0) {
		context()->inlineItemRepaint(this);
	}
}

void ItemBase::layoutChanged() {
	if (_position >= 0) {
		context()->inlineItemLayoutChanged(this);
	}
}

std::unique_ptr<ItemBase> ItemBase::createLayout(
		not_null<Context*> context,
		std::shared_ptr<Result> result,
		bool forceThumb) {
	using Type = Result::Type;

	switch (result->_type) {
	case Type::Photo:
		return std::make_unique<internal::Photo>(context, std::move(result));
	case Type::Audio:
	case Type::File:
		return std::make_unique<internal::File>(context, std::move(result));
	case Type::Video:
		return std::make_unique<internal::Video>(context, std::move(result));
	case Type::Sticker:
		return std::make_unique<internal::Sticker>(
			context,
			std::move(result));
	case Type::Gif:
		return std::make_unique<internal::Gif>(context, std::move(result));
	case Type::Article:
	case Type::Geo:
	case Type::Venue:
		return std::make_unique<internal::Article>(
			context,
			std::move(result),
			forceThumb);
	case Type::Game:
		return std::make_unique<internal::Game>(context, std::move(result));
	case Type::Contact:
		return std::make_unique<internal::Contact>(
			context,
			std::move(result));
	}
	return nullptr;
}

std::unique_ptr<ItemBase> ItemBase::createLayoutGif(
		not_null<Context*> context,
		not_null<DocumentData*> document) {
	return std::make_unique<internal::Gif>(context, document, true);
}

DocumentData *ItemBase::getResultDocument() const {
	return _result ? _result->_document : nullptr;
}

PhotoData *ItemBase::getResultPhoto() const {
	return _result ? _result->_photo : nullptr;
}

bool ItemBase::hasResultThumb() const {
	return _result
		&& (!_result->_thumbnail.empty()
			|| !_result->_locationThumbnail.empty());
}

QImage *ItemBase::getResultThumb(Data::FileOrigin origin) const {
	if (_result && !_thumbnail) {
		if (!_result->_thumbnail.empty()) {
			_thumbnail = _result->_thumbnail.createView();
			_result->_thumbnail.load(_result->_session, origin);
		} else if (!_result->_locationThumbnail.empty()) {
			_thumbnail = _result->_locationThumbnail.createView();
			_result->_locationThumbnail.load(_result->_session, origin);
		}
	}
	return (_thumbnail && !_thumbnail->isNull())
		? _thumbnail.get()
		: nullptr;
}

QPixmap ItemBase::getResultContactAvatar(int width, int height) const {
	if (_result->_type == Result::Type::Contact) {
		auto result = Ui::EmptyUserpic(
			Ui::EmptyUserpic::UserpicColor(Ui::EmptyUserpic::ColorIndex(
				BareId(qHash(_result->_id)))),
			_result->getLayoutTitle()
		).generate(width);
		if (result.height() != height * style::DevicePixelRatio()) {
			result = result.scaled(
				QSize(width, height) * style::DevicePixelRatio(),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
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

ClickHandlerPtr ItemBase::getResultPreviewHandler() const {
	if (!_result->_content_url.isEmpty()) {
		return std::make_shared<UrlClickHandler>(
			_result->_content_url,
			false);
	} else if (const auto document = _result->_document
		; document && document->createMediaView()->canBePlayed(nullptr)) {
		return std::make_shared<OpenFileClickHandler>();
	} else if (_result->_photo) {
		return std::make_shared<OpenFileClickHandler>();
	}
	return ClickHandlerPtr();
}

QString ItemBase::getResultThumbLetter() const {
	auto parts = QStringView(_result->_url).split('/');
	if (!parts.isEmpty()) {
		auto domain = parts.at(0);
		if (parts.size() > 2 && domain.endsWith(':') && parts.at(1).isEmpty()) { // http:// and others
			domain = parts.at(2);
		}

		parts = domain.split('@').constLast().split('.');
		if (parts.size() > 1) {
			return parts.at(parts.size() - 2).at(0).toUpper();
		}
	}
	if (!_result->_title.isEmpty()) {
		return _result->_title.at(0).toUpper();
	}
	return QString();
}

Data::FileOrigin ItemBase::fileOrigin() const {
	return _context->inlineItemFileOrigin();
}

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
