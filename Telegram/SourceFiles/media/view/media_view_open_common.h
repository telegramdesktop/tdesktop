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
		HistoryItem *item,
		MsgId topicRootId)
	: _controller(controller)
	, _photo(photo)
	, _item(item)
	, _topicRootId(topicRootId) {
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
		MsgId topicRootId,
		bool continueStreaming = false,
		crl::time startTime = 0)
	: _controller(controller)
	, _document(document)
	, _item(item)
	, _topicRootId(topicRootId)
	, _continueStreaming(continueStreaming)
	, _startTime(startTime) {
	}
	OpenRequest(
		Window::SessionController *controller,
		not_null<DocumentData*> document,
		const Data::CloudTheme &cloudTheme)
	: _controller(controller)
	, _document(document)
	, _cloudTheme(cloudTheme) {
	}

	[[nodiscard]] PeerData *peer() const {
		return _peer;
	}

	[[nodiscard]] PhotoData *photo() const {
		return _photo;
	}

	[[nodiscard]] HistoryItem *item() const {
		return _item;
	}

	[[nodiscard]] MsgId topicRootId() const {
		return _topicRootId;
	}

	[[nodiscard]] DocumentData *document() const {
		return _document;
	}

	[[nodiscard]] std::optional<Data::CloudTheme> cloudTheme() const {
		return _cloudTheme;
	}

	[[nodiscard]] Window::SessionController *controller() const {
		return _controller;
	}

	[[nodiscard]] bool continueStreaming() const {
		return _continueStreaming;
	}

	[[nodiscard]] crl::time startTime() const {
		return _startTime;
	}

private:
	Window::SessionController *_controller = nullptr;
	DocumentData *_document = nullptr;
	PhotoData *_photo = nullptr;
	PeerData *_peer = nullptr;
	HistoryItem *_item = nullptr;
	MsgId _topicRootId = 0;
	std::optional<Data::CloudTheme> _cloudTheme = std::nullopt;
	bool _continueStreaming = false;
	crl::time _startTime = 0;

};

} // namespace Media::View
