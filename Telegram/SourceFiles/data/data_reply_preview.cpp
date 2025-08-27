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
#include "styles/style_chat.h"

namespace Data {

ReplyPreview::ReplyPreview(not_null<DocumentData*> document)
: _document(document) {
}

ReplyPreview::ReplyPreview(not_null<PhotoData*> photo)
: _photo(photo) {
}

ReplyPreview::~ReplyPreview() = default;

void ReplyPreview::prepare(
		not_null<Image*> image,
		Images::Options options,
		bool spoiler) {
	using namespace Images;
	if (image->isNull()) {
		return;
	}
	int w = image->width(), h = image->height();
	if (w <= 0) w = 1;
	if (h <= 0) h = 1;
	auto thumbSize = (w > h)
		? QSize(
			w * st::historyReplyPreview / h,
			st::historyReplyPreview)
		: QSize(
			st::historyReplyPreview,
			h * st::historyReplyPreview / w);
	thumbSize *= style::DevicePixelRatio();
	options |= Option::TransparentBackground;
	auto outerSize = st::historyReplyPreview;
	auto original = spoiler
		? image->original().scaled(
			{ 40, 40 },
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation)
		: image->original();
	auto prepared = Prepare(std::move(original), thumbSize, {
		.options = options | (spoiler ? Option::Blur : Option()),
		.outer = { outerSize, outerSize },
	});
	(spoiler ? _spoilered : _regular) = std::make_unique<Image>(
		std::move(prepared));
	_good = spoiler || ((options & Option::Blur) == 0);
}

Image *ReplyPreview::image(
		Data::FileOrigin origin,
		not_null<PeerData*> context,
		bool spoiler) {
	auto &image = spoiler ? _spoilered : _regular;
	auto &checked = spoiler ? _checkedSpoilered : _checkedRegular;
	if (checked) {
		return image.get();
	} else if (_document) {
		if (!image || (!_good && _document->hasThumbnail())) {
			if (!_documentMedia) {
				_documentMedia = _document->createMediaView();
				_documentMedia->thumbnailWanted(origin);
			}
			const auto thumbnail = _documentMedia->thumbnail();
			const auto option = _document->isVideoMessage()
				? Images::Option::RoundCircle
				: Images::Option::None;
			if (spoiler) {
				if (const auto image = _documentMedia->thumbnailInline()) {
					prepare(image, option, true);
				} else if (thumbnail) {
					prepare(thumbnail, option, true);
				}
			} else if (thumbnail) {
				prepare(thumbnail, option);
			} else if (!image) {
				if (const auto image = _documentMedia->thumbnailInline()) {
					prepare(image, option | Images::Option::Blur);
				}
			}
			if (_good || !_document->hasThumbnail()) {
				checked = true;
				_documentMedia = nullptr;
			}
		}
	} else {
		Assert(_photo != nullptr);
		if (!image || !_good) {
			const auto inlineThumbnailBytes = _photo->inlineThumbnailBytes();
			if (!_photoMedia) {
				_photoMedia = _photo->createMediaView();
			}
			using Size = PhotoSize;
			const auto loadThumbnail = inlineThumbnailBytes.isEmpty()
				|| (!spoiler
					&& _photoMedia->autoLoadThumbnailAllowed(context));
			if (loadThumbnail) {
				_photoMedia->wanted(Size::Small, origin);
			}
			if (spoiler) {
				if (const auto blurred = _photoMedia->thumbnailInline()) {
					prepare(blurred, {}, true);
				} else if (const auto small = _photoMedia->image(Size::Small)) {
					prepare(small, {}, true);
				} else if (const auto large = _photoMedia->image(Size::Large)) {
					prepare(large, {}, true);
				}
			} else if (const auto small = _photoMedia->image(Size::Small)) {
				prepare(small, {});
			} else if (const auto large = _photoMedia->image(Size::Large)) {
				prepare(large, {});
			} else if (!image) {
				if (const auto blurred = _photoMedia->thumbnailInline()) {
					prepare(blurred, Images::Option::Blur);
				}
			}
			if (_good) {
				checked = true;
				_photoMedia = nullptr;
			}
		}
	}
	return image.get();
}

bool ReplyPreview::loaded(bool spoiler) const {
	return spoiler ? _checkedSpoilered : _checkedRegular;
}

} // namespace Data
