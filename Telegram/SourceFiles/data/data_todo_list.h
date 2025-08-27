/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
class Session;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

struct TodoListItem {
	TextWithEntities text;
	PeerData *completedBy = nullptr;
	TimeId completionDate = 0;
	int id = 0;

	friend inline bool operator==(
		const TodoListItem &,
		const TodoListItem &) = default;
};

struct TodoListData {
	TodoListData(not_null<Data::Session*> owner, TodoListId id);

	[[nodiscard]] Data::Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	enum class Flag {
		OthersCanAppend   = 0x01,
		OthersCanComplete = 0x02,
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	bool applyChanges(const MTPDtodoList &todolist);
	bool applyCompletions(const MTPVector<MTPTodoCompletion> *completions);

	void apply(
		not_null<HistoryItem*> item,
		const MTPDmessageActionTodoCompletions &data);
	void apply(const MTPDmessageActionTodoAppendTasks &data);

	[[nodiscard]] TodoListItem *itemById(int id);
	[[nodiscard]] const TodoListItem *itemById(int id) const;

	void setFlags(Flags flags);
	[[nodiscard]] Flags flags() const;
	[[nodiscard]] bool othersCanAppend() const;
	[[nodiscard]] bool othersCanComplete() const;

	TodoListId id;
	TextWithEntities title;
	std::vector<TodoListItem> items;
	int version = 0;

	static constexpr auto kMaxOptions = 32;

private:
	const not_null<Data::Session*> _owner;
	Flags _flags = Flags();

};

[[nodiscard]] MTPVector<MTPTodoItem> TodoListItemsToMTP(
	not_null<Main::Session*> session,
	const std::vector<TodoListItem> &tasks);
[[nodiscard]] MTPTodoList TodoListDataToMTP(
	not_null<const TodoListData*> todolist);
[[nodiscard]] MTPInputMedia TodoListDataToInputMedia(
	not_null<const TodoListData*> todolist);
[[nodiscard]] TodoListItem TodoListItemFromMTP(
	not_null<Main::Session*> session,
	const MTPTodoItem &item);
