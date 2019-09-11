/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_message_reactions.h"

#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace Data {

std::vector<QString> MessageReactions::SuggestList() {
	constexpr auto utf = [](const char *utf) {
		return QString::fromUtf8(utf);
	};
	return {
		utf("\xE2\x9D\xA4\xEF\xB8\x8F"), // :heart:
		utf("\xF0\x9F\x91\x8D"), // :like:
		utf("\xF0\x9F\x98\x82"), // :joy:
		utf("\xF0\x9F\x98\xB3"), // :flushed:
		utf("\xF0\x9F\x98\x94"), // :pensive:
	};
}

MessageReactions::MessageReactions(not_null<HistoryItem*> item)
: _item(item) {
}

void MessageReactions::add(const QString &reaction) {
	if (_chosen == reaction) {
		return;
	}
	if (!_chosen.isEmpty()) {
		const auto i = _list.find(_chosen);
		Assert(i != end(_list));
		--i->second;
		if (!i->second) {
			_list.erase(i);
		}
	}
	_chosen = reaction;
	if (!reaction.isEmpty()) {
		++_list[reaction];
	}
	sendRequest();
}

void MessageReactions::set(
		const QVector<MTPReactionCount> &list,
		bool ignoreChosen) {
	if (_requestId) {
		// We'll apply non-stale data from the request response.
		return;
	}
	auto existing = base::flat_set<QString>();
	for (const auto &count : list) {
		count.match([&](const MTPDreactionCount &data) {
			const auto reaction = qs(data.vreaction());
			if (data.is_chosen() && !ignoreChosen) {
				_chosen = reaction;
			}
			_list[reaction] = data.vcount().v;
			existing.emplace(reaction);
		});
	}
	if (_list.size() != existing.size()) {
		for (auto i = begin(_list); i != end(_list);) {
			if (!existing.contains(i->first)) {
				i = _list.erase(i);
			} else {
				++i;
			}
		}
		if (!_chosen.isEmpty() && !_list.contains(_chosen)) {
			_chosen = QString();
		}
	}
}

const base::flat_map<QString, int> &MessageReactions::list() const {
	return _list;
}

QString MessageReactions::chosen() const {
	return _chosen;
}

void MessageReactions::sendRequest() {
	const auto api = &_item->history()->session().api();
	if (_requestId) {
		api->request(_requestId).cancel();
	}
	const auto flags = _chosen.isEmpty()
		? MTPmessages_SendReaction::Flag(0)
		: MTPmessages_SendReaction::Flag::f_reaction;
	_requestId = api->request(MTPmessages_SendReaction(
		MTP_flags(flags),
		_item->history()->peer->input,
		MTP_int(_item->id),
		MTP_string(_chosen)
	)).done([=](const MTPUpdates &result) {
		_requestId = 0;
		api->applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
	}).send();
}

} // namespace Data
