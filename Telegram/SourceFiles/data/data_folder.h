/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_main_list.h"
#include "data/data_messages.h"
#include "base/weak_ptr.h"

class ChannelData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;
class Folder;

class Folder final : public Dialogs::Entry, public base::has_weak_ptr {
public:
	static constexpr auto kId = 1;

	Folder(not_null<Data::Session*> owner, FolderId id);
	Folder(const Folder &) = delete;
	Folder &operator=(const Folder &) = delete;

	FolderId id() const;
	void registerOne(not_null<History*> history);
	void unregisterOne(not_null<History*> history);
	void oneListMessageChanged(HistoryItem *from, HistoryItem *to);

	not_null<Dialogs::MainList*> chatsList();

	void applyDialog(const MTPDdialogFolder &data);
	void applyPinnedUpdate(const MTPDupdateDialogPinned &data);

	TimeId adjustedChatListTimeId() const override;

	int fixedOnTopIndex() const override;
	bool shouldBeInChatList() const override;
	int chatListUnreadCount() const override;
	bool chatListUnreadMark() const override;
	bool chatListMutedBadge() const override;
	Dialogs::UnreadState chatListUnreadState() const override;
	HistoryItem *chatListMessage() const override;
	bool chatListMessageKnown() const override;
	void requestChatListMessage() override;
	const QString &chatListName() const override;
	const QString &chatListNameSortKey() const override;
	const base::flat_set<QString> &chatListNameWords() const override;
	const base::flat_set<QChar> &chatListFirstLetters() const override;

	void loadUserpic() override;
	void paintUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const override;

	void paintUserpic(
		Painter &p,
		int x,
		int y,
		int size,
		const style::color &overrideBg,
		const style::color &overrideFg) const;

	const std::vector<not_null<History*>> &lastHistories() const;
	uint32 chatListViewVersion() const;

private:
	void indexNameParts();
	bool applyChatListMessage(HistoryItem *item);
	void computeChatListMessage();

	void reorderLastHistories();
	void updateChatListEntryPostponed();

	void paintUserpic(
		Painter &p,
		int x,
		int y,
		int size,
		const style::color *overrideBg,
		const style::color *overrideFg) const;

	FolderId _id = 0;
	Dialogs::MainList _chatsList;

	QString _name;
	base::flat_set<QString> _nameWords;
	base::flat_set<QChar> _nameFirstLetters;
	QString _chatListNameSortKey;

	std::vector<not_null<History*>> _lastHistories;
	HistoryItem *_chatListMessage = nullptr;
	uint32 _chatListViewVersion = 0;
	bool _updateChatListEntryPostponed = false;
	//rpl::variable<MessagePosition> _unreadPosition;

	rpl::lifetime _lifetime;

};

} // namespace Data
