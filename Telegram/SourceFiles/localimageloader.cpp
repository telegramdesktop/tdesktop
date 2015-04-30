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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "localimageloader.h"
#include "gui/filedialog.h"
#include <libexif/exif-data.h>

LocalImageLoaderPrivate::LocalImageLoaderPrivate(int32 currentUser, LocalImageLoader *loader, QThread *thread) : QObject(0)
    , loader(loader)
    , user(currentUser)
{
	moveToThread(thread);
	connect(loader, SIGNAL(needToPrepare()), this, SLOT(prepareImages()));
	connect(this, SIGNAL(imageReady()), loader, SLOT(onImageReady()));
	connect(this, SIGNAL(imageFailed(quint64)), loader, SLOT(onImageFailed(quint64)));
};

void LocalImageLoaderPrivate::prepareImages() {
	QString file, filename, mime, stickerMime = qsl("image/webp");
    int32 filesize = 0;
	QImage img;
	QByteArray data;
	PeerId peer;
    uint64 id, thumbId = 0;
	QString thumbExt = "jpg";
	ToPrepareMediaType type;
	bool animated = false;
	bool ctrlShiftEnter = false;
	MsgId replyTo = 0;
	{
		QMutexLocker lock(loader->toPrepareMutex());
		ToPrepareMedias &list(loader->toPrepareMedias());
		if (list.isEmpty()) return;

		file = list.front().file;
		img = list.front().img;
		data = list.front().data;
		peer = list.front().peer;
		id = list.front().id;
		type = list.front().type;
		ctrlShiftEnter = list.front().ctrlShiftEnter;
		replyTo = list.front().replyTo;
	}

	if (img.isNull()) {
		if (!file.isEmpty()) {
			QFileInfo info(file);
			if (type == ToPrepareAuto) {
				QString lower(file.toLower());
				const QStringList &photoExtensions(cPhotoExtensions());
				for (QStringList::const_iterator i = photoExtensions.cbegin(), e = photoExtensions.cend(); i != e; ++i) {
					if (lower.lastIndexOf(*i) == lower.size() - i->size()) {
						if (info.size() < MaxUploadPhotoSize) {
							type = ToPreparePhoto;
							break;
						}
					}
				}
				if (type == ToPrepareAuto && info.size() < MaxUploadDocumentSize) {
					type = ToPrepareDocument;
				}
			}
			if (type == ToPrepareDocument) {
				mime = mimeTypeForFile(info).name();
			}
			if (type != ToPrepareAuto && info.size() < MaxUploadPhotoSize) {
				bool opaque = (mime != stickerMime);
				img = App::readImage(file, 0, opaque, &animated);
			}
			filename = info.fileName();
			filesize = info.size();
		} else if (!data.isEmpty()) {
			img = App::readImage(data, 0, true, &animated);
			if (type == ToPrepareAuto) {
				if (!img.isNull() && data.size() < MaxUploadPhotoSize) {
					type = ToPreparePhoto;
				} else if (data.size() < MaxUploadDocumentSize) {
					type = ToPrepareDocument;
				} else {
					img = QImage();
				}
			}
			MimeType mimeType = mimeTypeForData(data);
			if (type == ToPrepareDocument) {
				mime = mimeType.name();
			}
			if (mime == "image/jpeg") {
				filename = filedialogDefaultName(qsl("image"), qsl(".jpg"), QString(), true);
			} else {
				QString ext;
				QStringList patterns = mimeType.globPatterns();
				if (!patterns.isEmpty()) {
					ext = patterns.front().replace('*', QString());
				}
				filename = filedialogDefaultName(qsl("doc"), ext, QString(), true);
			}
			filesize = data.size();
		}
	} else {
		if (type == ToPrepareDocument) {
			filename = filedialogDefaultName(qsl("image"), qsl(".png"), QString(), true);
			mime = mimeTypeForName("image/png").name();
			data = QByteArray();
			{
				QBuffer b(&data);
				img.save(&b, "PNG");
			}
			filesize = data.size();
		} else {
			if (img.hasAlphaChannel()) {
				QImage solid(img.width(), img.height(), QImage::Format_ARGB32_Premultiplied);
				solid.fill(st::white->c);
				{
					QPainter(&solid).drawImage(0, 0, img);
				}
				img = solid;
			}
			type = ToPreparePhoto;
			filename = qsl("Untitled.jpg");
			filesize = 0;
		}
	}

	if ((img.isNull() && (type != ToPrepareDocument || !filesize)) || type == ToPrepareAuto || (img.isNull() && file.isEmpty() && data.isEmpty())) { // if could not decide what type
		{
			QMutexLocker lock(loader->toPrepareMutex());
			ToPrepareMedias &list(loader->toPrepareMedias());
			list.pop_front();
		}

		QTimer::singleShot(1, this, SLOT(prepareImages()));

		emit imageFailed(id);
	} else {
		PreparedPhotoThumbs photoThumbs;
		QVector<MTPPhotoSize> photoSizes;

		QVector<MTPDocumentAttribute> attributes(1, MTP_documentAttributeFilename(MTP_string(filename)));

		MTPPhotoSize thumb(MTP_photoSizeEmpty(MTP_string("")));
		MTPPhoto photo(MTP_photoEmpty(MTP_long(0)));
		MTPDocument document(MTP_documentEmpty(MTP_long(0)));

		QByteArray jpeg;
		if (type == ToPreparePhoto) {
			int32 w = img.width(), h = img.height();

			QPixmap thumb = (w > 100 || h > 100) ? QPixmap::fromImage(img.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(img);
			photoThumbs.insert('s', thumb);
			photoSizes.push_back(MTP_photoSize(MTP_string("s"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(thumb.width()), MTP_int(thumb.height()), MTP_int(0)));

			QPixmap medium = (w > 320 || h > 320) ? QPixmap::fromImage(img.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(img);
			photoThumbs.insert('m', medium);
			photoSizes.push_back(MTP_photoSize(MTP_string("m"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(medium.width()), MTP_int(medium.height()), MTP_int(0)));

			QPixmap full = (w > 1280 || h > 1280) ? QPixmap::fromImage(img.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(img);
			photoThumbs.insert('y', full);
			photoSizes.push_back(MTP_photoSize(MTP_string("y"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0)));

			{
				QBuffer jpegBuffer(&jpeg);
				full.save(&jpegBuffer, "JPG", 77);
			}
			if (!filesize) filesize = jpeg.size();
		
			photo = MTP_photo(MTP_long(id), MTP_long(0), MTP_int(user), MTP_int(unixtime()), MTP_geoPointEmpty(), MTP_vector<MTPPhotoSize>(photoSizes));

			thumbId = id;
		} else if ((type == ToPrepareVideo || type == ToPrepareDocument) && !img.isNull()) {
			int32 w = img.width(), h = img.height();
			QByteArray thumbFormat = "JPG";
			int32 thumbQuality = 87;
			if (animated) {
				attributes.push_back(MTP_documentAttributeAnimated());
			} else if (mime == stickerMime && w > 0 && h > 0 && w <= StickerMaxSize && h <= StickerMaxSize && filesize < StickerInMemory) {
				attributes.push_back(MTP_documentAttributeSticker(MTP_string("")));
				thumbFormat = "webp";
				thumbExt = qsl("webp");
			}
			attributes.push_back(MTP_documentAttributeImageSize(MTP_int(w), MTP_int(h)));
			if (w < 20 * h && h < 20 * w) {
				QPixmap full = (w > 90 || h > 90) ? QPixmap::fromImage(img.scaled(90, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(img, Qt::ColorOnly);

				{
					QBuffer jpegBuffer(&jpeg);
					full.save(&jpegBuffer, thumbFormat, thumbQuality);
				}

				photoThumbs.insert('0', full);
				thumb = MTP_photoSize(MTP_string(""), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(full.width()), MTP_int(full.height()), MTP_int(0));

				thumbId = MTP::nonce<uint64>();
			}
		}

		if (type == ToPrepareDocument) {
			document = MTP_document(MTP_long(id), MTP_long(0), MTP_int(unixtime()), MTP_string(mime), MTP_int(filesize), thumb, MTP_int(MTP::maindc()), MTP_vector<MTPDocumentAttribute>(attributes));
		}

		{
			QMutexLocker lock(loader->readyMutex());
			loader->readyList().push_back(ReadyLocalMedia(type, file, filename, filesize, data, id, thumbId, thumbExt, peer, photo, photoThumbs, document, jpeg, ctrlShiftEnter, replyTo));
		}

		{
			QMutexLocker lock(loader->toPrepareMutex());
			ToPrepareMedias &list(loader->toPrepareMedias());
			list.pop_front();
		}

		QTimer::singleShot(1, this, SLOT(prepareImages()));

		emit imageReady();
	}
}

LocalImageLoaderPrivate::~LocalImageLoaderPrivate() {
	loader = 0;
}

LocalImageLoader::LocalImageLoader(QObject *parent) : QObject(parent), thread(0), priv(0) {
}

void LocalImageLoader::append(const QStringList &files, const PeerId &peer, MsgId replyTo, ToPrepareMediaType t) {
	{
		QMutexLocker lock(toPrepareMutex());
		for (QStringList::const_iterator i = files.cbegin(), e = files.cend(); i != e; ++i) {
			toPrepare.push_back(ToPrepareMedia(*i, peer, t, false, replyTo));
		}
	}
	if (!thread) {
		thread = new QThread();
		priv = new LocalImageLoaderPrivate(MTP::authedId(), this, thread);
		thread->start();
	}
	emit needToPrepare();
}

PhotoId LocalImageLoader::append(const QByteArray &img, const PeerId &peer, MsgId replyTo, ToPrepareMediaType t) {
	PhotoId result = 0;
	{
		QMutexLocker lock(toPrepareMutex());
		toPrepare.push_back(ToPrepareMedia(img, peer, t, false, replyTo));
		result = toPrepare.back().id;
	}
	if (!thread) {
		thread = new QThread();
		priv = new LocalImageLoaderPrivate(MTP::authedId(), this, thread);
		thread->start();
	}
	emit needToPrepare();
	return result;
}

PhotoId LocalImageLoader::append(const QImage &img, const PeerId &peer, MsgId replyTo, ToPrepareMediaType t, bool ctrlShiftEnter) {
	PhotoId result = 0;
	{
		QMutexLocker lock(toPrepareMutex());
		toPrepare.push_back(ToPrepareMedia(img, peer, t, ctrlShiftEnter, replyTo));
		result = toPrepare.back().id;
	}
	if (!thread) {
		thread = new QThread();
		priv = new LocalImageLoaderPrivate(MTP::authedId(), this, thread);
		thread->start();
	}
	emit needToPrepare();
	return result;
}

PhotoId LocalImageLoader::append(const QString &file, const PeerId &peer, MsgId replyTo, ToPrepareMediaType t) {
	PhotoId result = 0;
	{
		QMutexLocker lock(toPrepareMutex());
		toPrepare.push_back(ToPrepareMedia(file, peer, t, false, replyTo));
		result = toPrepare.back().id;
	}
	if (!thread) {
		thread = new QThread();
		priv = new LocalImageLoaderPrivate(MTP::authedId(), this, thread);
		thread->start();
	}
	emit needToPrepare();
	return result;
}

void LocalImageLoader::onImageReady() {
	{
		QMutexLocker lock(toPrepareMutex());
		if (toPrepare.isEmpty()) {
			if (priv) priv->deleteLater();
			priv = 0;
			if (thread) thread->deleteLater();
			thread = 0;
		}
	}

	emit imageReady();
}

void LocalImageLoader::onImageFailed(quint64 id) {
	{
		QMutexLocker lock(toPrepareMutex());
		if (toPrepare.isEmpty()) {
			if (priv) priv->deleteLater();
			priv = 0;
			if (thread) thread->deleteLater();
			thread = 0;
		}
	}

	emit imageFailed(id);
}

QMutex *LocalImageLoader::readyMutex() {
	return &readyLock;
}

ReadyLocalMedias &LocalImageLoader::readyList() {
	return ready;
}

QMutex *LocalImageLoader::toPrepareMutex() {
	return &toPrepareLock;
}

ToPrepareMedias &LocalImageLoader::toPrepareMedias() {
	return toPrepare;
}

LocalImageLoader::~LocalImageLoader() {
	delete priv;
	delete thread;
}
