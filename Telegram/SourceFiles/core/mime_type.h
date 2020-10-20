/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QFileInfo>
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

[[nodiscard]] MimeType MimeTypeForName(const QString &mime);
[[nodiscard]] MimeType MimeTypeForFile(const QFileInfo &file);
[[nodiscard]] MimeType MimeTypeForData(const QByteArray &data);

[[nodiscard]] bool IsMimeStickerAnimated(const QString &mime);
[[nodiscard]] bool IsMimeSticker(const QString &mime);
[[nodiscard]] bool IsMimeAcceptedForPhotoVideoAlbum(const QString &mime);

[[nodiscard]] bool FileIsImage(const QString &name, const QString &mime);

} // namespace Core
