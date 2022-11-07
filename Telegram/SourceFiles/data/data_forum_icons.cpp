/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_forum_icons.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kRefreshDefaultListEach = 60 * 60 * crl::time(1000);
constexpr auto kRecentRequestTimeout = 10 * crl::time(1000);
constexpr auto kMaxTimeout = 6 * 60 * 60 * crl::time(1000);

} // namespace

ForumIcons::ForumIcons(not_null<Session*> owner)
: _owner(owner)
, _resetUserpicsTimer([=] { resetUserpics(); }) {
}

ForumIcons::~ForumIcons() = default;

Main::Session &ForumIcons::session() const {
	return _owner->session();
}

void ForumIcons::requestDefaultIfUnknown() {
	if (_default.empty()) {
		requestDefault();
	}
}

void ForumIcons::refreshDefault() {
	requestDefault();
}

const std::vector<DocumentId> &ForumIcons::list() const {
	return _default;
}

rpl::producer<> ForumIcons::defaultUpdates() const {
	return _defaultUpdated.events();
}

void ForumIcons::requestDefault() {
	if (_defaultRequestId) {
		return;
	}
	auto &api = _owner->session().api();
	_defaultRequestId = api.request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetEmojiDefaultTopicIcons(),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		_defaultRequestId = 0;
		result.match([&](const MTPDmessages_stickerSet &data) {
			updateDefault(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=] {
		_defaultRequestId = 0;
	}).send();
}

void ForumIcons::updateDefault(const MTPDmessages_stickerSet &data) {
	const auto &list = data.vdocuments().v;
	_default.clear();
	_default.reserve(list.size());
	for (const auto &sticker : data.vdocuments().v) {
		_default.push_back(_owner->processDocument(sticker)->id);
	}
	_defaultUpdated.fire({});
}

void ForumIcons::scheduleUserpicsReset(not_null<Forum*> forum) {
	const auto duration = crl::time(st::slideDuration);
	_resetUserpicsWhen[forum] = crl::now() + duration;
	if (!_resetUserpicsTimer.isActive()) {
		_resetUserpicsTimer.callOnce(duration);
	}
}

void ForumIcons::clearUserpicsReset(not_null<Forum*> forum) {
	_resetUserpicsWhen.remove(forum);
}

void ForumIcons::resetUserpics() {
	auto nearest = crl::time();
	auto now = crl::now();
	for (auto i = begin(_resetUserpicsWhen); i != end(_resetUserpicsWhen);) {
		if (i->second > now) {
			if (!nearest || nearest > i->second) {
				nearest = i->second;
			}
			++i;
		} else {
			const auto forum = i->first;
			i = _resetUserpicsWhen.erase(i);
			resetUserpicsFor(forum);
		}
	}
	if (nearest) {
		_resetUserpicsTimer.callOnce(
			std::min(nearest - now, 86400 * crl::time(1000)));
	} else {
		_resetUserpicsTimer.cancel();
	}
}

void ForumIcons::resetUserpicsFor(not_null<Forum*> forum) {
	forum->enumerateTopics([](not_null<ForumTopic*> topic) {
		topic->clearUserpicLoops();
	});
}

} // namespace Data
