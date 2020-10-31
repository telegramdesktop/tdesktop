/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class Image;
class DocumentData;
class PhotoData;

namespace Data {

class PhotoMedia;
class DocumentMedia;
struct FileOrigin;

class ReplyPreview {
public:
	explicit ReplyPreview(not_null<DocumentData*> document);
	explicit ReplyPreview(not_null<PhotoData*> photo);
	~ReplyPreview();

	[[nodiscard]] Image *image(Data::FileOrigin origin);
	[[nodiscard]] bool loaded() const;

private:
	void prepare(not_null<Image*> image, Images::Options options);

	std::unique_ptr<Image> _image;
	PhotoData *_photo = nullptr;
	DocumentData *_document = nullptr;
	std::shared_ptr<PhotoMedia> _photoMedia;
	std::shared_ptr<DocumentMedia> _documentMedia;
	bool _good = false;
	bool _checked = false;

};

} // namespace Data
