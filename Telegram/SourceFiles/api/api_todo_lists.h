/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "mtproto/sender.h"

class ApiWrap;
class HistoryItem;
struct TodoListItem;
struct TodoListData;

namespace Main {
class Session;
} // namespace Main

namespace Api {

struct SendAction;
struct SendOptions;

class TodoLists final {
public:
	explicit TodoLists(not_null<ApiWrap*> api);

	void create(
		const TodoListData &data,
		SendAction action,
		Fn<void()> done,
		Fn<void(QString)> fail);
	void edit(
		not_null<HistoryItem*> item,
		const TodoListData &data,
		SendOptions options,
		Fn<void()> done,
		Fn<void(QString)> fail);
	void add(
		not_null<HistoryItem*> item,
		const std::vector<TodoListItem> &items,
		Fn<void()> done,
		Fn<void(QString)> fail);
	void toggleCompletion(FullMsgId itemId, int id, bool completed);

private:
	struct Accumulated {
		base::flat_set<int> completed;
		base::flat_set<int> incompleted;
		crl::time scheduled = 0;
		mtpRequestId requestId = 0;
	};

	void sendAccumulatedToggles(bool force);
	void send(FullMsgId itemId, Accumulated &entry);
	void finishRequest(FullMsgId itemId);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	base::flat_map<FullMsgId, Accumulated> _toggles;
	base::Timer _sendTimer;

};

} // namespace Api
