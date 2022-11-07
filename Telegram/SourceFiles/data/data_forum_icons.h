/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {

class DocumentMedia;
class Session;
class Forum;

class ForumIcons final {
public:
	explicit ForumIcons(not_null<Session*> owner);
	~ForumIcons();

	[[nodiscard]] Session &owner() const {
		return *_owner;
	}
	[[nodiscard]] Main::Session &session() const;

	void refreshDefault();
	void requestDefaultIfUnknown();

	[[nodiscard]] const std::vector<DocumentId> &list() const;

	[[nodiscard]] rpl::producer<> defaultUpdates() const;

	void scheduleUserpicsReset(not_null<Forum*> forum);
	void clearUserpicsReset(not_null<Forum*> forum);

private:
	void requestDefault();
	void resetUserpics();
	void resetUserpicsFor(not_null<Forum*> forum);

	void updateDefault(const MTPDmessages_stickerSet &data);

	const not_null<Session*> _owner;

	std::vector<DocumentId> _default;
	rpl::event_stream<> _defaultUpdated;

	mtpRequestId _defaultRequestId = 0;

	base::flat_map<not_null<Forum*>, crl::time> _resetUserpicsWhen;
	base::Timer _resetUserpicsTimer;

	rpl::lifetime _lifetime;

};

} // namespace Data
