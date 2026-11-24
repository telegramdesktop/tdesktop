/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_open_common.h"

#include "data/data_media_types.h"
#include "data/data_web_page.h"
#include "history/history_item.h"

namespace Media::View {

OpenRequest::OpenRequest(
	Window::SessionController *controller,
	BotPreviewGallery gallery,
	int index,
	Data::FileOrigin origin)
: _controller(controller)
, _fileOrigin(std::move(origin))
, _gallery(std::make_shared<BotPreviewGallery>(std::move(gallery)))
, _galleryIndex(index) {
	Expects(index >= 0 && index < _gallery->media.size());
	const auto &item = _gallery->media[index];
	if (const auto photo = std::get_if<not_null<PhotoData*>>(&item)) {
		_photo = *photo;
	} else {
		_document = std::get<not_null<DocumentData*>>(item);
	}
}

TimeId ExtractVideoTimestamp(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return 0;
	} else if (const auto timestamp = media->videoTimestamp()) {
		return timestamp;
	} else if (const auto webpage = media->webpage()) {
		return webpage->extractVideoTimestamp();
	}
	return 0;
}

TextWithEntities StripQuoteEntities(TextWithEntities text) {
	for (auto i = text.entities.begin(); i != text.entities.end();) {
		if (i->type() == EntityType::Blockquote) {
			i = text.entities.erase(i);
			continue;
		} else if (i->type() == EntityType::Pre) {
			*i = EntityInText(EntityType::Code, i->offset(), i->length());
		}
		++i;
	}
	return text;
}

} // namespace Media::View
