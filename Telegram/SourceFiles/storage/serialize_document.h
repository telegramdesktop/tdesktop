/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_document.h"

namespace Serialize {

class Document {
public:
	struct StickerSetInfo {
		StickerSetInfo(uint64 setId, uint64 accessHash, QString shortName)
			: setId(setId)
			, accessHash(accessHash)
			, shortName(shortName) {
		}
		uint64 setId;
		uint64 accessHash;
		QString shortName;
	};

	struct Fields {
		DocumentId id = 0;
		uint64 access = 0;
		QByteArray fileReference;
		TimeId date = 0;
		QVector<MTPDocumentAttribute> attributes;
		QString mime;
		InlineImageLocation inlineThumbnail;
		ImageWithLocation thumbnail;
		ImageWithLocation videoThumbnail;
		bool premium = false;
		int dc = 0;
		int64 size = 0;

		[[nodiscard]] DocumentData *apply(
			not_null<Main::Session*> session) const;
	};

	static void writeToStream(QDataStream &stream, DocumentData *document);
	[[nodiscard]] static std::optional<Fields> readFields(
		int streamAppVersion,
		QDataStream &stream,
		const StickerSetInfo *info = nullptr);
	static DocumentData *readFromStream(
		not_null<Main::Session*> session,
		int streamAppVersion,
		QDataStream &stream);
	static int sizeInStream(DocumentData *document);

};

} // namespace Serialize
