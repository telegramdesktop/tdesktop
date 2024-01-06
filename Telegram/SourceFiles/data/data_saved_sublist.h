/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/ui/dialogs_message_view.h"
#include "dialogs/dialogs_entry.h"

class PeerData;
class History;

namespace Data {

class Session;

class SavedSublist final : public Dialogs::Entry {
public:
	explicit SavedSublist(not_null<PeerData*> peer);
	~SavedSublist();

	[[nodiscard]] not_null<History*> history() const;
	[[nodiscard]] not_null<PeerData*> peer() const;
	[[nodiscard]] bool isHiddenAuthor() const;
	[[nodiscard]] bool isFullLoaded() const;

	[[nodiscard]] auto messages() const
		-> const std::vector<not_null<HistoryItem*>> &;
	void applyMaybeLast(not_null<HistoryItem*> item, bool added = false);
	void removeOne(not_null<HistoryItem*> item);
	void append(std::vector<not_null<HistoryItem*>> &&items, int fullCount);
	void setFullLoaded(bool loaded = true);

	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] std::optional<int> fullCount() const;
	[[nodiscard]] rpl::producer<int> fullCountValue() const;

	[[nodiscard]] Dialogs::Ui::MessageView &lastItemDialogsView() {
		return _lastItemDialogsView;
	}

	int fixedOnTopIndex() const override;
	bool shouldBeInChatList() const override;
	Dialogs::UnreadState chatListUnreadState() const override;
	Dialogs::BadgesState chatListBadgesState() const override;
	HistoryItem *chatListMessage() const override;
	bool chatListMessageKnown() const override;
	const QString &chatListName() const override;
	const QString &chatListNameSortKey() const override;
	int chatListNameVersion() const override;
	const base::flat_set<QString> &chatListNameWords() const override;
	const base::flat_set<QChar> &chatListFirstLetters() const override;

	void chatListPreloadData() override;
	void paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Dialogs::Ui::PaintContext &context) const override;

private:
	enum class Flag : uchar {
		ResolveChatListMessage = (1 << 0),
		FullLoaded = (1 << 1),
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; }
	using Flags = base::flags<Flag>;

	bool hasOrphanMediaGroupPart() const;
	void allowChatListMessageResolve();
	void resolveChatListMessageGroup();

	const not_null<History*> _history;

	std::vector<not_null<HistoryItem*>> _items;
	std::optional<int> _fullCount;
	rpl::event_stream<> _changed;
	Dialogs::Ui::MessageView _lastItemDialogsView;
	Flags _flags;

};

} // namespace Data
