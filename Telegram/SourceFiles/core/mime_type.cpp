/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/mime_type.h"

#include "core/utils.h"

#include <QtCore/QMimeDatabase>
#include <QtCore/QMimeData>

namespace Core {

MimeType::MimeType(const QMimeType &type) : _typeStruct(type) {
}

MimeType::MimeType(Known type) : _type(type) {
}

QStringList MimeType::globPatterns() const {
	switch (_type) {
	case Known::WebP: return QStringList(u"*.webp"_q);
	case Known::Tgs: return QStringList(u"*.tgs"_q);
	case Known::Tgv: return QStringList(u"*.tgv"_q);
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
	case Known::Tgv: return u"Wallpaper pattern (*.tgv)"_q;
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
	case Known::Tgv: return u"application/x-tgwallpattern"_q;
	case Known::TDesktopTheme: return u"application/x-tdesktop-theme"_q;
	case Known::TDesktopPalette: return u"application/x-tdesktop-palette"_q;
	default: break;
	}
	return _typeStruct.name();
}

MimeType MimeTypeForName(const QString &mime) {
	if (mime == u"image/webp"_q) {
		return MimeType(MimeType::Known::WebP);
	} else if (mime == u"application/x-tgsticker"_q) {
		return MimeType(MimeType::Known::Tgs);
	} else if (mime == u"application/x-tgwallpattern"_q) {
		return MimeType(MimeType::Known::Tgv);
	} else if (mime == u"application/x-tdesktop-theme"_q
		|| mime == u"application/x-tgtheme-tdesktop"_q) {
		return MimeType(MimeType::Known::TDesktopTheme);
	} else if (mime == u"application/x-tdesktop-palette"_q) {
		return MimeType(MimeType::Known::TDesktopPalette);
	} else if (mime == u"audio/mpeg3"_q) {
		return MimeType(QMimeDatabase().mimeTypeForName("audio/mp3"));
	}
	return MimeType(QMimeDatabase().mimeTypeForName(mime));
}

MimeType MimeTypeForFile(const QFileInfo &file) {
	QString path = file.absoluteFilePath();
	if (path.endsWith(u".webp"_q, Qt::CaseInsensitive)) {
		return MimeType(MimeType::Known::WebP);
	} else if (path.endsWith(u".tgs"_q, Qt::CaseInsensitive)) {
		return MimeType(MimeType::Known::Tgs);
	} else if (path.endsWith(u".tgv"_q)) {
		return MimeType(MimeType::Known::Tgv);
	} else if (path.endsWith(u".tdesktop-theme"_q, Qt::CaseInsensitive)) {
		return MimeType(MimeType::Known::TDesktopTheme);
	} else if (path.endsWith(u".tdesktop-palette"_q, Qt::CaseInsensitive)) {
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

bool IsMimeStickerLottie(const QString &mime) {
	return (mime == u"application/x-tgsticker"_q);
}

bool IsMimeStickerWebm(const QString &mime) {
	return (mime == u"video/webm"_q);
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
	if (lowermime.startsWith(u"image/"_q)) {
		return true;
	} else if (namelower.endsWith(u".bmp"_q)
		|| namelower.endsWith(u".jpg"_q)
		|| namelower.endsWith(u".jpeg"_q)
		|| namelower.endsWith(u".gif"_q)
		|| namelower.endsWith(u".webp"_q)
		|| namelower.endsWith(u".tga"_q)
		|| namelower.endsWith(u".tiff"_q)
		|| namelower.endsWith(u".tif"_q)
		|| namelower.endsWith(u".psd"_q)
		|| namelower.endsWith(u".png"_q)) {
		return true;
	}
	return false;
}

std::shared_ptr<QMimeData> ShareMimeMediaData(
		not_null<const QMimeData*> original) {
	auto result = std::make_shared<QMimeData>();
	if (original->hasFormat(u"application/x-td-forward"_q)) {
		result->setData(u"application/x-td-forward"_q, "1");
	}
	if (original->hasImage()) {
		result->setImageData(original->imageData());
	}
	if (auto list = base::GetMimeUrls(original); !list.isEmpty()) {
		result->setUrls(std::move(list));
	}
	return result;
}

} // namespace Core
