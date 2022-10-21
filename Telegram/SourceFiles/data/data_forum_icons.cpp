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
#include "apiwrap.h"

namespace Data {
namespace {

constexpr auto kRefreshDefaultListEach = 60 * 60 * crl::time(1000);
constexpr auto kRecentRequestTimeout = 10 * crl::time(1000);
constexpr auto kMaxTimeout = 6 * 60 * 60 * crl::time(1000);

} // namespace

ForumIcons::ForumIcons(not_null<Session*> owner)
: _owner(owner) {
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

} // namespace Data
