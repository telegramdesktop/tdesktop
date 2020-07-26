/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "data/data_photo.h"

class FileLoader;

namespace Data {

class PhotoMedia final {
public:
	explicit PhotoMedia(not_null<PhotoData*> owner);
	~PhotoMedia();

	[[nodiscard]] not_null<PhotoData*> owner() const;

	[[nodiscard]] Image *thumbnailInline() const;

	[[nodiscard]] Image *image(PhotoSize size) const;
	[[nodiscard]] QSize size(PhotoSize size) const;
	void wanted(PhotoSize size, Data::FileOrigin origin);
	void set(PhotoSize size, QImage image);

	[[nodiscard]] QByteArray videoContent() const;
	[[nodiscard]] QSize videoSize() const;
	void videoWanted(Data::FileOrigin origin);
	void setVideo(QByteArray content);

	[[nodiscard]] bool loaded() const;
	[[nodiscard]] float64 progress() const;

	void automaticLoad(Data::FileOrigin origin, const HistoryItem *item);

	void collectLocalData(not_null<PhotoMedia*> local);

private:
	// NB! Right now DocumentMedia can outlive Main::Session!
	// In DocumentData::collectLocalData a shared_ptr is sent on_main.
	// In case this is a problem the ~Gif code should be rewritten.
	const not_null<PhotoData*> _owner;
	mutable std::unique_ptr<Image> _inlineThumbnail;
	std::array<std::unique_ptr<Image>, kPhotoSizeCount>  _images;
	QByteArray _videoBytes;

};

} // namespace Data
