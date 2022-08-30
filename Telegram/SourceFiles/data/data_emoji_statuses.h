/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Main {
class Session;
} // namespace Main

namespace Data {

class DocumentMedia;
class Session;

class EmojiStatuses final {
public:
	explicit EmojiStatuses(not_null<Session*> owner);
	~EmojiStatuses();

	[[nodiscard]] Session &owner() const {
		return *_owner;
	}
	[[nodiscard]] Main::Session &session() const;

	void refreshRecent();
	void refreshRecentDelayed();
	void refreshDefault();

	enum class Type {
		Recent,
		Default,
	};
	[[nodiscard]] const std::vector<DocumentId> &list(Type type) const;

	[[nodiscard]] rpl::producer<> recentUpdates() const;
	[[nodiscard]] rpl::producer<> defaultUpdates() const;

	void set(DocumentId id);
	[[nodiscard]] bool setting() const;

private:
	void requestRecent();
	void requestDefault();

	void updateRecent(const MTPDaccount_emojiStatuses &data);
	void updateDefault(const MTPDaccount_emojiStatuses &data);

	const not_null<Session*> _owner;

	std::vector<DocumentId> _recent;
	std::vector<DocumentId> _default;
	rpl::event_stream<> _recentUpdated;
	rpl::event_stream<> _defaultUpdated;

	mtpRequestId _recentRequestId = 0;
	bool _recentRequestScheduled = false;
	uint64 _recentHash = 0;

	base::Timer _defaultRefreshTimer;
	mtpRequestId _defaultRequestId = 0;
	uint64 _defaultHash = 0;

	mtpRequestId _sentRequestId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Data
