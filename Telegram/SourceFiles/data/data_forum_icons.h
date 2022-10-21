/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Data {

class DocumentMedia;
class Session;

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

private:
	void requestDefault();

	void updateDefault(const MTPDmessages_stickerSet &data);

	const not_null<Session*> _owner;

	std::vector<DocumentId> _default;
	rpl::event_stream<> _defaultUpdated;

	mtpRequestId _defaultRequestId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Data
