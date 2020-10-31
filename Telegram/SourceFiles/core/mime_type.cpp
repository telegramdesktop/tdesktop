/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/mime_type.h"

#include <QtCore/QMimeDatabase>

namespace Core {

MimeType::MimeType(const QMimeType &type) : _typeStruct(type) {
}

MimeType::MimeType(Known type) : _type(type) {
}

QStringList MimeType::globPatterns() const {
	switch (_type) {
	case Known::WebP: return QStringList(u"*.webp"_q);
	case Known::Tgs: return QStringList(u"*.tgs"_q);
	case Known::TDesktopTheme: return QStringList(u"*.tdesktop-theme"_q);
	case Known::TDesktopPalette: return QStringList(u"*.tdesktop-palette"_q);
	default: break;
	}
	return _typeStruct.globPatterns();
}

QString MimeType::filterString() const {
	switch (_type) {
	case Known::WebP: return u"WebP image (*.webp)"_q;
	case Known::Tgs: return u"Telegram sticker (*.tgs)"_q;
	case Known::TDesktopTheme: return u"Theme files (*.tdesktop-theme)"_q;
	case Known::TDesktopPalette: return u"Palette files (*.tdesktop-palette)"_q;
	default: break;
	}
	return _typeStruct.filterString();
}

QString MimeType::name() const {
	switch (_type) {
	case Known::WebP: return u"image/webp"_q;
	case Known::Tgs: return u"application/x-tgsticker"_q;
	case Known::TDesktopTheme: return u"application/x-tdesktop-theme"_q;
	case Known::TDesktopPalette: return u"application/x-tdesktop-palette"_q;
	default: break;
	}
	return _typeStruct.name();
}

MimeType MimeTypeForName(const QString &mime) {
	if (mime == qstr("image/webp")) {
		return MimeType(MimeType::Known::WebP);
	} else if (mime == qstr("application/x-tgsticker")) {
		return MimeType(MimeType::Known::Tgs);
	} else if (mime == qstr("application/x-tdesktop-theme")
		|| mime == qstr("application/x-tgtheme-tdesktop")) {
		return MimeType(MimeType::Known::TDesktopTheme);
	} else if (mime == qstr("application/x-tdesktop-palette")) {
		return MimeType(MimeType::Known::TDesktopPalette);
	} else if (mime == qstr("audio/mpeg3")) {
		return MimeType(QMimeDatabase().mimeTypeForName("audio/mp3"));
	}
	return MimeType(QMimeDatabase().mimeTypeForName(mime));
}

MimeType MimeTypeForFile(const QFileInfo &file) {
	QString path = file.absoluteFilePath();
	if (path.endsWith(qstr(".webp"), Qt::CaseInsensitive)) {
		return MimeType(MimeType::Known::WebP);
	} else if (path.endsWith(qstr(".tgs"), Qt::CaseInsensitive)) {
		return MimeType(MimeType::Known::Tgs);
	} else if (path.endsWith(qstr(".tdesktop-theme"), Qt::CaseInsensitive)) {
		return MimeType(MimeType::Known::TDesktopTheme);
	} else if (path.endsWith(qstr(".tdesktop-palette"), Qt::CaseInsensitive)) {
		return MimeType(MimeType::Known::TDesktopPalette);
	}

	{
		QFile f(path);
		if (f.open(QIODevice::ReadOnly)) {
			QByteArray magic = f.read(12);
			if (magic.size() >= 12) {
				if (!memcmp(magic.constData(), "RIFF", 4) && !memcmp(magic.constData() + 8, "WEBP", 4)) {
					return MimeType(MimeType::Known::WebP);
				}
			}
			f.close();
		}
	}
	return MimeType(QMimeDatabase().mimeTypeForFile(file));
}

MimeType MimeTypeForData(const QByteArray &data) {
	if (data.size() >= 12) {
		if (!memcmp(data.constData(), "RIFF", 4) && !memcmp(data.constData() + 8, "WEBP", 4)) {
			return MimeType(MimeType::Known::WebP);
		}
	}
	return MimeType(QMimeDatabase().mimeTypeForData(data));
}

bool IsMimeStickerAnimated(const QString &mime) {
	return (mime == u"application/x-tgsticker"_q);
}

bool IsMimeSticker(const QString &mime) {
	return (mime == u"image/webp"_q)
		|| IsMimeStickerAnimated(mime);
}

bool IsMimeAcceptedForPhotoVideoAlbum(const QString &mime) {
	return (mime == u"image/jpeg"_q)
		|| (mime == u"image/png"_q)
		|| (mime == u"video/mp4"_q)
		|| (mime == u"video/quicktime"_q);
}

bool FileIsImage(const QString &name, const QString &mime) {
	QString lowermime = mime.toLower(), namelower = name.toLower();
	if (lowermime.startsWith(qstr("image/"))) {
		return true;
	} else if (namelower.endsWith(qstr(".bmp"))
		|| namelower.endsWith(qstr(".jpg"))
		|| namelower.endsWith(qstr(".jpeg"))
		|| namelower.endsWith(qstr(".gif"))
		|| namelower.endsWith(qstr(".webp"))
		|| namelower.endsWith(qstr(".tga"))
		|| namelower.endsWith(qstr(".tiff"))
		|| namelower.endsWith(qstr(".tif"))
		|| namelower.endsWith(qstr(".psd"))
		|| namelower.endsWith(qstr(".png"))) {
		return true;
	}
	return false;
}

} // namespace Core
