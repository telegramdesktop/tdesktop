/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtGui/QImage>

class QMimeData;
class PhotoData;

namespace HistoryView {

struct PhotoDragData {
	QImage image;
	QByteArray bytes;
};

[[nodiscard]] PhotoDragData PreparePhotoDragData(not_null<PhotoData*> photo);

void FillDragMimeWithPhoto(not_null<QMimeData*> mime, PhotoDragData &&data);

} // namespace HistoryView
