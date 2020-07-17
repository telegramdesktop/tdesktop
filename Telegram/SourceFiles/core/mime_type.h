/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QMimeType>

namespace Core {

class MimeType {
public:
	enum class Known {
		Unknown,
		TDesktopTheme,
		TDesktopPalette,
		WebP,
		Tgs,
	};

	explicit MimeType(const QMimeType &type);
	explicit MimeType(Known type);
	QStringList globPatterns() const;
	QString filterString() const;
	QString name() const;

private:
	QMimeType _typeStruct;
	Known _type = Known::Unknown;

};

MimeType MimeTypeForName(const QString &mime);
MimeType MimeTypeForFile(const QFileInfo &file);
MimeType MimeTypeForData(const QByteArray &data);

bool IsMimeStickerAnimated(const QString &mime);
bool IsMimeSticker(const QString &mime);

} // namespace Core
