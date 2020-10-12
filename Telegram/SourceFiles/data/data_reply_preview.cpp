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
#include "data/data_photo_media.h"
#include "ui/image/image.h"

namespace Data {

ReplyPreview::ReplyPreview(not_null<DocumentData*> document)
: _document(document) {
}

ReplyPreview::ReplyPreview(not_null<PhotoData*> photo)
: _photo(photo) {
}

ReplyPreview::~ReplyPreview() = default;

void ReplyPreview::prepare(not_null<Image*> image, Images::Options options) {
	if (image->isNull()) {
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
		thumbSize.width(),
		thumbSize.height(),
		prepareOptions,
		outerSize,
		outerSize);
	_image = std::make_unique<Image>(bitmap.toImage());
	_good = ((options & Images::Option::Blurred) == 0);
}

Image *ReplyPreview::image(Data::FileOrigin origin) {
	if (_checked) {
		return _image.get();
	}
	if (_document) {
		if (!_image || (!_good && _document->hasThumbnail())) {
			if (!_documentMedia) {
				_documentMedia = _document->createMediaView();
				_documentMedia->thumbnailWanted(origin);
			}
			const auto thumbnail = _documentMedia->thumbnail();
			const auto option = _document->isVideoMessage()
				? Images::Option::Circled
				: Images::Option::None;
			if (thumbnail) {
				prepare(thumbnail, option);
			} else if (!_image) {
				if (const auto image = _documentMedia->thumbnailInline()) {
					prepare(image, option | Images::Option::Blurred);
				}
			}
			if (_good || !_document->hasThumbnail()) {
				_checked = true;
				_documentMedia = nullptr;
			}
		}
	} else {
		Assert(_photo != nullptr);
		if (!_image || !_good) {
			if (!_photoMedia) {
				_photoMedia = _photo->createMediaView();
				_photoMedia->wanted(PhotoSize::Small, origin);
			}
			if (const auto small = _photoMedia->image(PhotoSize::Small)) {
				prepare(small, Images::Option(0));
			} else if (const auto large = _photoMedia->image(
					PhotoSize::Large)) {
				prepare(large, Images::Option(0));
			} else if (!_image) {
				if (const auto blurred = _photoMedia->thumbnailInline()) {
					prepare(blurred, Images::Option::Blurred);
				}
			}
			if (_good) {
				_checked = true;
				_photoMedia = nullptr;
			}
		}
	}
	return _image.get();
}

bool ReplyPreview::loaded() const {
	return _checked;
}

} // namespace Data
