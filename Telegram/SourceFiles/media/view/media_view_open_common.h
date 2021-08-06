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

namespace Window {
class SessionController;
} // namespace Window

namespace Media::View {

struct OpenRequest {
public:
	OpenRequest() {
	}

	OpenRequest(
		Window::SessionController *controller,
		not_null<PhotoData*> photo,
		HistoryItem *item)
	: _controller(controller)
	, _photo(photo)
	, _item(item) {
	}
	OpenRequest(
		Window::SessionController *controller,
		not_null<PhotoData*> photo,
		not_null<PeerData*> peer)
	: _controller(controller)
	, _photo(photo)
	, _peer(peer) {
	}

	OpenRequest(
		Window::SessionController *controller,
		not_null<DocumentData*> document,
		HistoryItem *item,
		bool continueStreaming = false)
	: _controller(controller)
	, _document(document)
	, _item(item)
	, _continueStreaming(continueStreaming) {
	}
	OpenRequest(
		Window::SessionController *controller,
		not_null<DocumentData*> document,
		const Data::CloudTheme &cloudTheme)
	: _controller(controller)
	, _document(document)
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

	Window::SessionController *controller() const {
		return _controller;
	}

	bool continueStreaming() const {
		return _continueStreaming;
	}

private:
	Window::SessionController *_controller = nullptr;
	DocumentData *_document = nullptr;
	PhotoData *_photo = nullptr;
	PeerData *_peer = nullptr;
	HistoryItem *_item = nullptr;
	std::optional<Data::CloudTheme> _cloudTheme = std::nullopt;
	bool _continueStreaming = false;

};

} // namespace Media::View
