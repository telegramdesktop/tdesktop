/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_views.h"

#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace Api {
namespace {

// Send channel views each second.
constexpr auto kSendViewsTimeout = crl::time(1000);

} // namespace

ViewsManager::ViewsManager(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance())
, _incrementTimer([=] { viewsIncrement(); }) {
}

void ViewsManager::scheduleIncrement(not_null<HistoryItem*> item) {
	auto peer = item->history()->peer;
	auto i = _incremented.find(peer);
	if (i != _incremented.cend()) {
		if (i->second.contains(item->id)) {
			return;
		}
	} else {
		i = _incremented.emplace(peer).first;
	}
	i->second.emplace(item->id);
	auto j = _toIncrement.find(peer);
	if (j == _toIncrement.cend()) {
		j = _toIncrement.emplace(peer).first;
		_incrementTimer.callOnce(kSendViewsTimeout);
	}
	j->second.emplace(item->id);
}

void ViewsManager::removeIncremented(not_null<PeerData*> peer) {
	_incremented.remove(peer);
}

void ViewsManager::viewsIncrement() {
	for (auto i = _toIncrement.begin(); i != _toIncrement.cend();) {
		if (_incrementRequests.contains(i->first)) {
			++i;
			continue;
		}

		QVector<MTPint> ids;
		ids.reserve(i->second.size());
		for (const auto &msgId : i->second) {
			ids.push_back(MTP_int(msgId));
		}
		const auto requestId = _api.request(MTPmessages_GetMessagesViews(
			i->first->input,
			MTP_vector<MTPint>(ids),
			MTP_bool(true)
		)).done([=](
				const MTPmessages_MessageViews &result,
				mtpRequestId requestId) {
			done(ids, result, requestId);
		}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
			fail(error, requestId);
		}).afterDelay(5).send();

		_incrementRequests.emplace(i->first, requestId);
		i = _toIncrement.erase(i);
	}
}

void ViewsManager::done(
		QVector<MTPint> ids,
		const MTPmessages_MessageViews &result,
		mtpRequestId requestId) {
	const auto &data = result.c_messages_messageViews();
	auto &owner = _session->data();
	owner.processUsers(data.vusers());
	owner.processChats(data.vchats());
	auto &v = data.vviews().v;
	if (ids.size() == v.size()) {
		for (const auto &[peer, id] : _incrementRequests) {
			if (id != requestId) {
				continue;
			}
			for (auto j = 0, l = int(ids.size()); j < l; ++j) {
				if (const auto item = owner.message(peer->id, ids[j].v)) {
					v[j].match([&](const MTPDmessageViews &data) {
						if (const auto views = data.vviews()) {
							if (item->changeViewsCount(views->v)) {
								_session->data().notifyItemDataChange(item);
							}
						}
						if (const auto forwards = data.vforwards()) {
							item->setForwardsCount(forwards->v);
						}
						if (const auto replies = data.vreplies()) {
							item->setReplies(
								HistoryMessageRepliesData(replies));
						}
					});
				}
			}
			_incrementRequests.erase(peer);
			break;
		}
	}
	if (!_toIncrement.empty() && !_incrementTimer.isActive()) {
		_incrementTimer.callOnce(kSendViewsTimeout);
	}
}

void ViewsManager::fail(const MTP::Error &error, mtpRequestId requestId) {
	for (const auto &[peer, id] : _incrementRequests) {
		if (id == requestId) {
			_incrementRequests.erase(peer);
			break;
		}
	}
	if (!_toIncrement.empty() && !_incrementTimer.isActive()) {
		_incrementTimer.callOnce(kSendViewsTimeout);
	}
}

} // namespace Api
