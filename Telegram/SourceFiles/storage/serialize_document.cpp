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
#include "storage/serialize_document.h"

#include "storage/serialize_common.h"
#include "chat_helpers/stickers.h"

namespace {

enum StickerSetType {
	StickerSetTypeEmpty = 0,
	StickerSetTypeID = 1,
	StickerSetTypeShortName = 2,
};

} // namespace

namespace Serialize {

void Document::writeToStream(QDataStream &stream, DocumentData *document) {
	stream << quint64(document->id) << quint64(document->_access) << qint32(document->date);
	stream << qint32(document->_version);
	stream << document->filename() << document->mimeString() << qint32(document->_dc) << qint32(document->size);
	stream << qint32(document->dimensions.width()) << qint32(document->dimensions.height());
	stream << qint32(document->type);
	if (auto sticker = document->sticker()) {
		stream << document->sticker()->alt;
		switch (document->sticker()->set.type()) {
		case mtpc_inputStickerSetID: {
			stream << qint32(StickerSetTypeID);
		} break;
		case mtpc_inputStickerSetShortName: {
			stream << qint32(StickerSetTypeShortName);
		} break;
		case mtpc_inputStickerSetEmpty:
		default: {
			stream << qint32(StickerSetTypeEmpty);
		} break;
		}
		writeStorageImageLocation(stream, document->sticker()->loc);
	} else {
		stream << qint32(document->duration());
		writeStorageImageLocation(stream, document->thumb->location());
	}
}

DocumentData *Document::readFromStreamHelper(int streamAppVersion, QDataStream &stream, const StickerSetInfo *info) {
	quint64 id, access;
	QString name, mime;
	qint32 date, dc, size, width, height, type, version;
	stream >> id >> access >> date;
	if (streamAppVersion >= 9061) {
		stream >> version;
	} else {
		version = 0;
	}
	stream >> name >> mime >> dc >> size;
	stream >> width >> height;
	stream >> type;

	QVector<MTPDocumentAttribute> attributes;
	if (!name.isEmpty()) {
		attributes.push_back(MTP_documentAttributeFilename(MTP_string(name)));
	}

	qint32 duration = -1;
	StorageImageLocation thumb;
	if (type == StickerDocument) {
		QString alt;
		qint32 typeOfSet;
		stream >> alt >> typeOfSet;

		thumb = readStorageImageLocation(stream);

		if (typeOfSet == StickerSetTypeEmpty) {
			attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(alt), MTP_inputStickerSetEmpty(), MTPMaskCoords()));
		} else if (info) {
			if (info->setId == Stickers::DefaultSetId || info->setId == Stickers::CloudRecentSetId || info->setId == Stickers::FavedSetId || info->setId == Stickers::CustomSetId) {
				typeOfSet = StickerSetTypeEmpty;
			}

			switch (typeOfSet) {
			case StickerSetTypeID: {
				attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(alt), MTP_inputStickerSetID(MTP_long(info->setId), MTP_long(info->accessHash)), MTPMaskCoords()));
			} break;
			case StickerSetTypeShortName: {
				attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(alt), MTP_inputStickerSetShortName(MTP_string(info->shortName)), MTPMaskCoords()));
			} break;
			case StickerSetTypeEmpty:
			default: {
				attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(alt), MTP_inputStickerSetEmpty(), MTPMaskCoords()));
			} break;
			}
		}
	} else {
		stream >> duration;
		if (type == AnimatedDocument) {
			attributes.push_back(MTP_documentAttributeAnimated());
		}
		thumb = readStorageImageLocation(stream);
	}
	if (width > 0 && height > 0) {
		if (duration >= 0) {
			auto flags = MTPDdocumentAttributeVideo::Flags(0);
			if (type == RoundVideoDocument) {
				flags |= MTPDdocumentAttributeVideo::Flag::f_round_message;
			}
			attributes.push_back(MTP_documentAttributeVideo(MTP_flags(flags), MTP_int(duration), MTP_int(width), MTP_int(height)));
		} else {
			attributes.push_back(MTP_documentAttributeImageSize(MTP_int(width), MTP_int(height)));
		}
	}

	if (!dc && !access) {
		return nullptr;
	}
	return App::documentSet(id, nullptr, access, version, date, attributes, mime, thumb.isNull() ? ImagePtr() : ImagePtr(thumb), dc, size, thumb);
}

DocumentData *Document::readStickerFromStream(int streamAppVersion, QDataStream &stream, const StickerSetInfo &info) {
	return readFromStreamHelper(streamAppVersion, stream, &info);
}

DocumentData *Document::readFromStream(int streamAppVersion, QDataStream &stream) {
	return readFromStreamHelper(streamAppVersion, stream, nullptr);
}

int Document::sizeInStream(DocumentData *document) {
	int result = 0;

	// id + access + date + version
	result += sizeof(quint64) + sizeof(quint64) + sizeof(qint32) + sizeof(qint32);
	// + namelen + name + mimelen + mime + dc + size
	result += stringSize(document->filename()) + stringSize(document->mimeString()) + sizeof(qint32) + sizeof(qint32);
	// + width + height
	result += sizeof(qint32) + sizeof(qint32);
	// + type
	result += sizeof(qint32);

	if (auto sticker = document->sticker()) { // type == StickerDocument
		// + altlen + alt + type-of-set
		result += stringSize(sticker->alt) + sizeof(qint32);
		// + thumb loc
		result += Serialize::storageImageLocationSize();
	} else {
		// + duration
		result += sizeof(qint32);
		// + thumb loc
		result += Serialize::storageImageLocationSize();
	}

	return result;
}

} // namespace Serialize
