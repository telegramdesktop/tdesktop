/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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
	[[nodiscard]] QByteArray imageBytes(PhotoSize size) const;
	[[nodiscard]] QSize size(PhotoSize size) const;
	void wanted(PhotoSize size, Data::FileOrigin origin);
	void set(
		PhotoSize size,
		PhotoSize goodFor,
		QImage image,
		QByteArray bytes);

	[[nodiscard]] QByteArray videoContent(PhotoSize size) const;
	[[nodiscard]] QSize videoSize(PhotoSize size) const;
	void videoWanted(PhotoSize size, Data::FileOrigin origin);
	void setVideo(PhotoSize size, QByteArray content);

	[[nodiscard]] bool loaded() const;
	[[nodiscard]] float64 progress() const;

	[[nodiscard]] bool autoLoadThumbnailAllowed(
		not_null<PeerData*> peer) const;
	void automaticLoad(FileOrigin origin, const HistoryItem *item);
	void automaticLoad(FileOrigin origin, not_null<PeerData*> peer);

	void collectLocalData(not_null<PhotoMedia*> local);

	bool saveToFile(const QString &path);
	bool setToClipboard();

private:
	struct PhotoImage {
		std::unique_ptr<Image> data;
		QByteArray bytes;
		PhotoSize goodFor = PhotoSize();
	};

	const PhotoImage *resolveLoadedImage(PhotoSize size) const;

	// NB! Right now DocumentMedia can outlive Main::Session!
	// In DocumentData::collectLocalData a shared_ptr is sent on_main.
	// In case this is a problem the ~Gif code should be rewritten.
	const not_null<PhotoData*> _owner;
	mutable std::unique_ptr<Image> _inlineThumbnail;
	std::array<PhotoImage, kPhotoSizeCount> _images;
	QByteArray _videoBytesSmall;
	QByteArray _videoBytesLarge;

};

} // namespace Data
