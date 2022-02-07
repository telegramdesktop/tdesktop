/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;

namespace HistoryUnreadThings {

enum class AddType {
	New,
	Existing,
};

enum class Type {
	Mentions,
	Reactions,
};

class List final {
public:
	[[nodiscard]] int loadedCount() const {
		return _messages.size();
	}
	[[nodiscard]] MsgId minLoaded() const {
		return _messages.empty() ? 0 : _messages.front();
	}
	[[nodiscard]] MsgId maxLoaded() const {
		return _messages.empty() ? 0 : _messages.back();
	}
	[[nodiscard]] int count(int notKnownValue = -1) const {
		return _count.value_or(notKnownValue);
	}
	[[nodiscard]] bool has() const {
		return (count() > 0);
	}
	[[nodiscard]] bool contains(MsgId msgId) const {
		return _messages.contains(msgId);
	}
	void setCount(int count) {
		_count = count;
	}
	void insert(MsgId msgId) {
		_messages.insert(msgId);
	}
	void erase(MsgId msgId) {
		_messages.remove(msgId);
	}
	void clear() {
		_messages.clear();
	}

private:
	std::optional<int> _count;
	base::flat_set<MsgId> _messages;

};

struct All {
	List mentions;
	List reactions;
};

class ConstProxy {
public:
	ConstProxy(const List *list, bool known) : _list(list), _known(known) {
	}
	ConstProxy(const ConstProxy &) = delete;
	ConstProxy &operator=(const ConstProxy &) = delete;

	[[nodiscard]] int loadedCount() const {
		return _list ? _list->loadedCount() : 0;
	}
	[[nodiscard]] MsgId minLoaded() const {
		return _list ? _list->minLoaded() : 0;
	}
	[[nodiscard]] MsgId maxLoaded() const {
		return _list ? _list->maxLoaded() : 0;
	}
	[[nodiscard]] int count(int notKnownValue = -1) const {
		return _list
			? _list->count(notKnownValue)
			: _known
			? 0
			: notKnownValue;
	}
	[[nodiscard]] bool has() const {
		return _list && _list->has();
	}

private:
	const List *_list = nullptr;
	const bool _known = false;

};

class Proxy final : public ConstProxy {
public:
	Proxy(
		not_null<History*> history,
		std::unique_ptr<All> &data,
		Type type,
		bool known)
	: ConstProxy(
		(!data
			? nullptr
			: (type == Type::Mentions)
			? &data->mentions
			: &data->reactions),
		known)
	, _history(history)
	, _data(data)
	, _type(type)
	, _known(known) {
	}

	void setCount(int count);
	bool add(MsgId msgId, AddType type);
	void erase(MsgId msgId);
	void clear();

	void addSlice(const MTPmessages_Messages &slice, int alreadyLoaded);

	void checkAdd(MsgId msgId, bool resolved = false);

private:
	void createData();
	[[nodiscard]] List &resolveList();

	const not_null<History*> _history;
	std::unique_ptr<All> &_data;
	Type _type = Type::Mentions;
	bool _known = false;

};

} // namespace HistoryUnreadThings
