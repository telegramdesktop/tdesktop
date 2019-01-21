/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/mime_type.h"

namespace Core {

MimeType::MimeType(const QMimeType &type) : _typeStruct(type) {
}

MimeType::MimeType(Known type) : _type(type) {
}

QStringList MimeType::globPatterns() const {
	switch (_type) {
	case Known::WebP: return QStringList(qsl("*.webp"));
	case Known::TDesktopTheme: return QStringList(qsl("*.tdesktop-theme"));
	case Known::TDesktopPalette: return QStringList(qsl("*.tdesktop-palette"));
	default: break;
	}
	return _typeStruct.globPatterns();
}

QString MimeType::filterString() const {
	switch (_type) {
	case Known::WebP: return qsl("WebP image (*.webp)");
	case Known::TDesktopTheme: return qsl("Theme files (*.tdesktop-theme)");
	case Known::TDesktopPalette: return qsl("Palette files (*.tdesktop-palette)");
	default: break;
	}
	return _typeStruct.filterString();
}

QString MimeType::name() const {
	switch (_type) {
	case Known::WebP: return qsl("image/webp");
	case Known::TDesktopTheme: return qsl("application/x-tdesktop-theme");
	case Known::TDesktopPalette: return qsl("application/x-tdesktop-palette");
	default: break;
	}
	return _typeStruct.name();
}

MimeType MimeTypeForName(const QString &mime) {
	if (mime == qstr("image/webp")) {
		return MimeType(MimeType::Known::WebP);
	} else if (mime == qstr("application/x-tdesktop-theme")) {
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

} // namespace Core
