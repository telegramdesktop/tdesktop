/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_todo_list.h"

#include "api/api_text_entities.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "base/call_delayed.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "api/api_text_entities.h"
#include "ui/text/text_options.h"

namespace {

constexpr auto kShortPollTimeout = 30 * crl::time(1000);

const TodoListItem *ItemById(const std::vector<TodoListItem> &list, int id) {
	const auto i = ranges::find(list, id, &TodoListItem::id);
	return (i != end(list)) ? &*i : nullptr;
}

TodoListItem *ItemById(std::vector<TodoListItem> &list, int id) {
	return const_cast<TodoListItem*>(ItemById(std::as_const(list), id));
}

} // namespace

TodoListData::TodoListData(not_null<Data::Session*> owner, TodoListId id)
: id(id)
, _owner(owner) {
}

Data::Session &TodoListData::owner() const {
	return *_owner;
}

Main::Session &TodoListData::session() const {
	return _owner->session();
}

bool TodoListData::applyChanges(const MTPDtodoList &todolist) {
	const auto newTitle = Api::ParseTextWithEntities(
		&session(),
		todolist.vtitle());
	const auto newFlags = (todolist.is_others_can_append()
		? Flag::OthersCanAppend
		: Flag())
		| (todolist.is_others_can_complete() ? Flag::OthersCanComplete
			: Flag());
	auto newItems = ranges::views::all(
		todolist.vlist().v
	) | ranges::views::transform([&](const MTPTodoItem &item) {
		return TodoListItemFromMTP(&session(), item);
	}) | ranges::views::take(
		kMaxOptions
	) | ranges::to_vector;

	const auto changed1 = (title != newTitle) || (_flags != newFlags);
	const auto changed2 = (items != newItems);
	if (!changed1 && !changed2) {
		return false;
	}
	if (changed1) {
		title = newTitle;
		_flags = newFlags;
	}
	if (changed2) {
		std::swap(items, newItems);
		for (const auto &old : newItems) {
			if (const auto current = itemById(old.id)) {
				current->completedBy = old.completedBy;
				current->completionDate = old.completionDate;
			}
		}
	}
	++version;
	return true;
}

bool TodoListData::applyCompletions(
		const MTPVector<MTPTodoCompletion> *completions) {
	auto changed = false;
	const auto lookup = [&](int id) {
		if (!completions) {
			return (const MTPDtodoCompletion*)nullptr;
		}
		const auto proj = [](const MTPTodoCompletion &completion) {
			return completion.data().vid().v;
		};
		const auto i = ranges::find(completions->v, id, proj);
		return (i != completions->v.end()) ? &i->data() : nullptr;
	};
	for (auto &item : items) {
		const auto completion = lookup(item.id);
		const auto by = (completion && completion->vcompleted_by().v)
			? owner().user(UserId(completion->vcompleted_by().v)).get()
			: nullptr;
		const auto date = completion ? completion->vdate().v : TimeId();
		if (item.completedBy != by || item.completionDate != date) {
			item.completedBy = by;
			item.completionDate = date;
			changed = true;
		}
	}
	if (changed) {
		++version;
	}
	return changed;
}

void TodoListData::apply(
		not_null<HistoryItem*> item,
		const MTPDmessageActionTodoCompletions &data) {
	for (const auto &id : data.vcompleted().v) {
		if (const auto task = itemById(id.v)) {
			task->completedBy = item->from();
			task->completionDate = item->date();
		}
	}
	for (const auto &id : data.vincompleted().v) {
		if (const auto task = itemById(id.v)) {
			task->completedBy = nullptr;
			task->completionDate = TimeId();
		}
	}
	owner().notifyTodoListUpdateDelayed(this);
}

void TodoListData::apply(const MTPDmessageActionTodoAppendTasks &data) {
	const auto limit = TodoListData::kMaxOptions;
	for (const auto &task : data.vlist().v) {
		if (items.size() < limit) {
			const auto parsed = TodoListItemFromMTP(
				&session(),
				task);
			if (!itemById(parsed.id)) {
				items.push_back(std::move(parsed));
			}
		}
	}
	owner().notifyTodoListUpdateDelayed(this);
}

TodoListItem *TodoListData::itemById(int id) {
	return ItemById(items, id);
}

const TodoListItem *TodoListData::itemById(int id) const {
	return ItemById(items, id);
}

void TodoListData::setFlags(Flags flags) {
	if (_flags != flags) {
		_flags = flags;
		++version;
	}
}

TodoListData::Flags TodoListData::flags() const {
	return _flags;
}

bool TodoListData::othersCanAppend() const {
	return (_flags & Flag::OthersCanAppend);
}

bool TodoListData::othersCanComplete() const {
	return (_flags & Flag::OthersCanComplete);
}

MTPVector<MTPTodoItem> TodoListItemsToMTP(
		not_null<Main::Session*> session,
		const std::vector<TodoListItem> &tasks) {
	const auto convert = [&](const TodoListItem &item) {
		return MTP_todoItem(
			MTP_int(item.id),
			MTP_textWithEntities(
				MTP_string(item.text.text),
				Api::EntitiesToMTP(session, item.text.entities)));
	};
	auto items = QVector<MTPTodoItem>();
	items.reserve(tasks.size());
	ranges::transform(tasks, ranges::back_inserter(items), convert);
	return MTP_vector<MTPTodoItem>(items);
}

MTPTodoList TodoListDataToMTP(not_null<const TodoListData*> todolist) {
	using Flag = MTPDtodoList::Flag;
	const auto flags = Flag()
		| (todolist->othersCanAppend()
			? Flag::f_others_can_append
			: Flag())
		| (todolist->othersCanComplete()
			? Flag::f_others_can_complete
			: Flag());
	return MTP_todoList(
		MTP_flags(flags),
		MTP_textWithEntities(
			MTP_string(todolist->title.text),
			Api::EntitiesToMTP(
				&todolist->session(),
				todolist->title.entities)),
		TodoListItemsToMTP(&todolist->session(), todolist->items));
}

MTPInputMedia TodoListDataToInputMedia(
		not_null<const TodoListData*> todolist) {
	return MTP_inputMediaTodo(TodoListDataToMTP(todolist));
}

TodoListItem TodoListItemFromMTP(
		not_null<Main::Session*> session,
		const MTPTodoItem &item) {
	const auto &data = item.data();
	return {
		.text = Api::ParseTextWithEntities(session, data.vtitle()),
		.id = data.vid().v,
	};
}
