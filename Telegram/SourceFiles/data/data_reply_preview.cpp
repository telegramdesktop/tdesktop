/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_reply_preview.h"

#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo.h"
#include "ui/image/image.h"
#include "ui/image/image_source.h"

namespace Data {

ReplyPreview::ReplyPreview(not_null<DocumentData*> document)
: _document(document)
, _documentMedia(_document->createMediaView()) {
}

ReplyPreview::ReplyPreview(not_null<PhotoData*> photo)
: _photo(photo) {
}

void ReplyPreview::prepare(not_null<Image*> image, Images::Options options) {
	if (image->isNull() || !image->loaded()) {
		return;
	}
	int w = image->width(), h = image->height();
	if (w <= 0) w = 1;
	if (h <= 0) h = 1;
	auto thumbSize = (w > h)
		? QSize(
			w * st::msgReplyBarSize.height() / h,
			st::msgReplyBarSize.height())
		: QSize(
			st::msgReplyBarSize.height(),
			h * st::msgReplyBarSize.height() / w);
	thumbSize *= cIntRetinaFactor();
	const auto prepareOptions = Images::Option::Smooth
		| Images::Option::TransparentBackground
		| options;
	auto outerSize = st::msgReplyBarSize.height();
	auto bitmap = image->pixNoCache(
		FileOrigin(),
		thumbSize.width(),
		thumbSize.height(),
		prepareOptions,
		outerSize,
		outerSize);
	_image = std::make_unique<Image>(
		std::make_unique<Images::ImageSource>(
			bitmap.toImage(),
			"PNG"));
	_good = ((options & Images::Option::Blurred) == 0);
}

Image *ReplyPreview::image(Data::FileOrigin origin) {
	if (_document) {
		const auto thumbnail = _document->thumbnail();
		Assert(thumbnail != nullptr);
		if (!_image || (!_good && thumbnail->loaded())) {
			const auto option = _document->isVideoMessage()
				? Images::Option::Circled
				: Images::Option::None;
			if (thumbnail->loaded()) {
				prepare(thumbnail, option);
			} else {
				thumbnail->load(origin);
				if (const auto image = _documentMedia->thumbnailInline()) {
					prepare(image, option | Images::Option::Blurred);
				}
			}
		}
	} else {
		Assert(_photo != nullptr);
		const auto small = _photo->thumbnailSmall();
		const auto large = _photo->large();
		if (!_image || (!_good && (small->loaded() || large->loaded()))) {
			if (small->isDelayedStorageImage()
				&& !large->isNull()
				&& !large->isDelayedStorageImage()
				&& large->loaded()) {
				prepare(large, Images::Option(0));
			} else if (small->loaded()) {
				prepare(small, Images::Option(0));
			} else {
				small->load(origin);
				if (const auto blurred = _photo->thumbnailInline()) {
					prepare(blurred, Images::Option::Blurred);
				}
			}
		}
	}
	return _image.get();
}

} // namespace Data
