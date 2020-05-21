/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;
class PhotoData;

namespace Data {

class DocumentMedia;
struct FileOrigin;

class ReplyPreview {
public:
	explicit ReplyPreview(not_null<DocumentData*> document);
	explicit ReplyPreview(not_null<PhotoData*> photo);

	[[nodiscard]] Image *image(Data::FileOrigin origin);

private:
	void prepare(not_null<Image*> image, Images::Options options);

	std::unique_ptr<Image> _image;
	bool _good = false;
	bool _checked = false;
	DocumentData *_document = nullptr;
	PhotoData *_photo = nullptr;
	std::shared_ptr<DocumentMedia> _documentMedia;

};

} // namespace Data
