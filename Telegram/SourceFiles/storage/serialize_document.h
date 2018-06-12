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

	static void writeToStream(QDataStream &stream, DocumentData *document);
	static DocumentData *readStickerFromStream(int streamAppVersion, QDataStream &stream, const StickerSetInfo &info);
	static DocumentData *readFromStream(int streamAppVersion, QDataStream &stream);
	static int sizeInStream(DocumentData *document);

private:
	static DocumentData *readFromStreamHelper(int streamAppVersion, QDataStream &stream, const StickerSetInfo *info);

};

} // namespace Serialize
