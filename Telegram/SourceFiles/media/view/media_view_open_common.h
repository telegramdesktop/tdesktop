/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_cloud_themes.h"

class DocumentData;
class PeerData;
class PhotoData;
class HistoryItem;

namespace Media::View {

struct OpenRequest {
public:
	OpenRequest() {
	}

	OpenRequest(not_null<PhotoData*> photo, HistoryItem *item)
	: _photo(photo)
	, _item(item) {
	}
	OpenRequest(not_null<PhotoData*> photo, not_null<PeerData*> peer)
	: _photo(photo)
	, _peer(peer) {
	}

	OpenRequest(not_null<DocumentData*> document, HistoryItem *item)
	: _document(document)
	, _item(item) {
	}
	OpenRequest(
		not_null<DocumentData*> document,
		const Data::CloudTheme &cloudTheme)
	: _document(document)
	, _cloudTheme(cloudTheme) {
	}

	PeerData *peer() const {
		return _peer;
	}

	PhotoData *photo() const {
		return _photo;
	}

	HistoryItem *item() const {
		return _item;
	}

	DocumentData *document() const {
		return _document;
	}

	std::optional<Data::CloudTheme> cloudTheme() const {
		return _cloudTheme;
	}

private:
	DocumentData *_document = nullptr;
	PhotoData *_photo = nullptr;
	PeerData *_peer = nullptr;
	HistoryItem *_item = nullptr;
	std::optional<Data::CloudTheme> _cloudTheme = std::nullopt;

};

} // namespace Media::View
