/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/factchecks.h"

#include "apiwrap.h"
#include "base/random.h"
#include "data/data_session.h"
#include "history/view/media/history_view_web_page.h"
#include "history/view/history_view_message.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"

namespace Data {
namespace {

constexpr auto kRequestDelay = crl::time(1000);

} // namespace

Factchecks::Factchecks(not_null<Main::Session*> session)
: _session(session)
, _requestTimer([=] { request(); }) {
}

void Factchecks::requestFor(not_null<HistoryItem*> item) {
	subscribeIfNotYet();

	if (const auto factcheck = item->Get<HistoryMessageFactcheck>()) {
		factcheck->requested = true;
	}
	if (!_requestTimer.isActive()) {
		_requestTimer.callOnce(kRequestDelay);
	}
	const auto changed = !_pending.empty()
		&& (_pending.front()->history() != item->history());
	const auto added = _pending.emplace(item).second;
	if (changed) {
		request();
	} else if (added && _pending.size() == 1) {
		_requestTimer.callOnce(kRequestDelay);
	}
}

void Factchecks::subscribeIfNotYet() {
	if (_subscribed) {
		return;
	}
	_subscribed = true;

	_session->data().itemRemoved(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		_pending.remove(item);
		const auto i = ranges::find(_requested, item.get());
		if (i != end(_requested)) {
			*i = nullptr;
		}
	}, _lifetime);
}

void Factchecks::request() {
	_requestTimer.cancel();

	if (!_requested.empty() || _pending.empty()) {
		return;
	}
	_session->api().request(base::take(_requestId)).cancel();

	auto ids = QVector<MTPint>();
	ids.reserve(_pending.size());
	const auto history = _pending.front()->history();
	for (auto i = begin(_pending); i != end(_pending);) {
		const auto &item = *i;
		if (item->history() == history) {
			_requested.push_back(item);
			ids.push_back(MTP_int(item->id.bare));
			i = _pending.erase(i);
		} else {
			++i;
		}
	}
	_requestId = _session->api().request(MTPmessages_GetFactCheck(
		history->peer->input,
		MTP_vector<MTPint>(std::move(ids))
	)).done([=](const MTPVector<MTPFactCheck> &result) {
		_requestId = 0;
		const auto &list = result.v;
		auto index = 0;
		for (const auto &item : base::take(_requested)) {
			if (!item) {
			} else if (index >= list.size()) {
				item->setFactcheck({});
			} else {
				item->setFactcheck(FromMTP(item, &list[index]));
			}
			++index;
		}
		if (!_pending.empty()) {
			request();
		}
	}).fail([=] {
		_requestId = 0;
		for (const auto &item : base::take(_requested)) {
			if (item) {
				item->setFactcheck({});
			}
		}
		if (!_pending.empty()) {
			request();
		}
	}).send();
}

std::unique_ptr<HistoryView::WebPage> Factchecks::makeMedia(
		not_null<HistoryView::Message*> view,
		not_null<HistoryMessageFactcheck*> factcheck) {
	if (!factcheck->page) {
		factcheck->page = view->history()->owner().webpage(
			base::RandomValue<WebPageId>(),
			tr::lng_factcheck_title(tr::now),
			factcheck->data.text);
	}
	return std::make_unique<HistoryView::WebPage>(
		view,
		factcheck->page,
		MediaWebPageFlags());
}

} // namespace Data
