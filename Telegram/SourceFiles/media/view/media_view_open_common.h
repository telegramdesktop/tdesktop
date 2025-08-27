/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_cloud_themes.h"
#include "data/data_stories.h"

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
		MsgId topicRootId,
		PeerId monoforumPeerId)
	: _controller(controller)
	, _photo(photo)
	, _item(item)
	, _topicRootId(topicRootId)
	, _monoforumPeerId(monoforumPeerId) {
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
		PeerId monoforumPeerId,
		bool continueStreaming = false,
		crl::time startTime = 0)
	: _controller(controller)
	, _document(document)
	, _item(item)
	, _topicRootId(topicRootId)
	, _monoforumPeerId(monoforumPeerId)
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

	OpenRequest(
		Window::SessionController *controller,
		not_null<Data::Story*> story,
		Data::StoriesContext context)
	: _controller(controller)
	, _story(story)
	, _storiesContext(context) {
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
	[[nodiscard]] PeerId monoforumPeerId() const {
		return _monoforumPeerId;
	}

	[[nodiscard]] DocumentData *document() const {
		return _document;
	}

	[[nodiscard]] Data::Story *story() const {
		return _story;
	}
	[[nodiscard]] Data::StoriesContext storiesContext() const {
		return _storiesContext;
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
	Data::Story *_story = nullptr;
	Data::StoriesContext _storiesContext;
	PeerData *_peer = nullptr;
	HistoryItem *_item = nullptr;
	MsgId _topicRootId = 0;
	PeerId _monoforumPeerId = 0;
	std::optional<Data::CloudTheme> _cloudTheme = std::nullopt;
	bool _continueStreaming = false;
	crl::time _startTime = 0;

};

[[nodiscard]] TimeId ExtractVideoTimestamp(not_null<HistoryItem*> item);

} // namespace Media::View
