/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/serialize_document.h"

#include "storage/serialize_common.h"
#include "storage/serialize_peer.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "ui/image/image.h"
#include "main/main_session.h"

namespace Serialize {
namespace {

constexpr auto kVersionTag = int32(0x7FFFFFFF);
constexpr auto kVersion = 2;

enum StickerSetType {
	StickerSetTypeEmpty = 0,
	StickerSetTypeID = 1,
	StickerSetTypeShortName = 2,
};

} // namespace

void Document::writeToStream(QDataStream &stream, DocumentData *document) {
	stream << quint64(document->id) << quint64(document->_access) << qint32(document->date);
	stream << document->_fileReference << qint32(kVersionTag) << qint32(kVersion);
	stream << document->filename() << document->mimeString() << qint32(document->_dc) << qint32(document->size);
	stream << qint32(document->dimensions.width()) << qint32(document->dimensions.height());
	stream << qint32(document->type);
	if (const auto sticker = document->sticker()) {
		stream << document->sticker()->alt;
		if (document->sticker()->set.id) {
			stream << qint32(StickerSetTypeID);
		} else if (!document->sticker()->set.shortName.isEmpty()) {
			stream << qint32(StickerSetTypeShortName);
		} else {
			stream << qint32(StickerSetTypeEmpty);
		}
	} else {
		stream << qint32(document->getDuration());
	}
	writeImageLocation(stream, document->thumbnailLocation());
	stream << qint32(document->thumbnailByteSize());
	writeImageLocation(stream, document->videoThumbnailLocation());
	stream
		<< qint32(document->videoThumbnailByteSize())
		<< qint32(document->inlineThumbnailIsPath() ? 1 : 0)
		<< document->inlineThumbnailBytes();
}

DocumentData *Document::readFromStreamHelper(
		not_null<Main::Session*> session,
		int streamAppVersion,
		QDataStream &stream,
		const StickerSetInfo *info) {
	quint64 id, access;
	QString name, mime;
	qint32 date, dc, size, width, height, type, versionTag, version = 0;
	QByteArray fileReference;
	stream >> id >> access >> date;
	if (streamAppVersion >= 9061) {
		if (streamAppVersion >= 1003013) {
			stream >> fileReference;
		}
		stream >> versionTag;
		if (versionTag == kVersionTag) {
			stream >> version;
		}
	} else {
		versionTag = 0;
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
	if (type == StickerDocument) {
		QString alt;
		qint32 typeOfSet;
		stream >> alt >> typeOfSet;
		if (typeOfSet == StickerSetTypeEmpty) {
			attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(alt), MTP_inputStickerSetEmpty(), MTPMaskCoords()));
		} else if (info) {
			if (info->setId == Data::Stickers::DefaultSetId
				|| info->setId == Data::Stickers::CloudRecentSetId
				|| info->setId == Data::Stickers::CloudRecentAttachedSetId
				|| info->setId == Data::Stickers::FavedSetId
				|| info->setId == Data::Stickers::CustomSetId) {
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
	}
	std::optional<ImageLocation> videoThumb;
	qint32 thumbnailByteSize = 0, videoThumbnailByteSize = 0;
	qint32 inlineThumbnailIsPath = 0;
	QByteArray inlineThumbnailBytes;
	const auto thumb = readImageLocation(streamAppVersion, stream);
	if (version >= 1) {
		stream >> thumbnailByteSize;
		videoThumb = readImageLocation(streamAppVersion, stream);
		stream >> videoThumbnailByteSize;
		if (version >= 2) {
			stream >> inlineThumbnailIsPath >> inlineThumbnailBytes;
		}
	} else {
		videoThumb = ImageLocation();
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

	const auto storage = std::get_if<StorageFileLocation>(
		&thumb->file().data);
	if ((stream.status() != QDataStream::Ok)
		|| (!dc && !access)
		|| !thumb
		|| !videoThumb
		|| (thumb->valid()
			&& (!storage || !storage->isDocumentThumbnail()))) {
		stream.setStatus(QDataStream::ReadCorruptData);
		// We can't convert legacy thumbnail location to modern, because
		// size letter ('s' or 'm') is lost, it was not saved in legacy.
		return nullptr;
	}
	return session->data().document(
		id,
		access,
		fileReference,
		date,
		attributes,
		mime,
		InlineImageLocation{
			inlineThumbnailBytes,
			(inlineThumbnailIsPath == 1),
		},
		ImageWithLocation{
			.location = *thumb,
			.bytesCount = thumbnailByteSize
		},
		ImageWithLocation{
			.location = *videoThumb,
			.bytesCount = videoThumbnailByteSize
		},
		dc,
		size);
}

DocumentData *Document::readStickerFromStream(
		not_null<Main::Session*> session,
		int streamAppVersion,
		QDataStream &stream,
		const StickerSetInfo &info) {
	return readFromStreamHelper(session, streamAppVersion, stream, &info);
}

DocumentData *Document::readFromStream(
		not_null<Main::Session*> session,
		int streamAppVersion,
		QDataStream &stream) {
	return readFromStreamHelper(session, streamAppVersion, stream, nullptr);
}

int Document::sizeInStream(DocumentData *document) {
	int result = 0;

	// id + access + date
	result += sizeof(quint64) + sizeof(quint64) + sizeof(qint32);
	// file_reference + version tag + version
	result += bytearraySize(document->_fileReference) + sizeof(qint32) * 2;
	// + namelen + name + mimelen + mime + dc + size
	result += stringSize(document->filename()) + stringSize(document->mimeString()) + sizeof(qint32) + sizeof(qint32);
	// + width + height
	result += sizeof(qint32) + sizeof(qint32);
	// + type
	result += sizeof(qint32);

	if (auto sticker = document->sticker()) { // type == StickerDocument
		// + altlen + alt + type-of-set
		result += stringSize(sticker->alt) + sizeof(qint32);
	} else {
		// + duration
		result += sizeof(qint32);
	}
	// + thumb loc
	result += Serialize::imageLocationSize(document->thumbnailLocation());
	result += sizeof(qint32); // thumbnail_byte_size
	result += Serialize::imageLocationSize(document->videoThumbnailLocation());
	result += sizeof(qint32); // video_thumbnail_byte_size

	result += sizeof(qint32) + Serialize::bytearraySize(document->inlineThumbnailBytes());

	return result;
}

} // namespace Serialize
