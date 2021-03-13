/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_single_message_search.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_search_controller.h"
#include "core/local_url_handlers.h"
#include "history/history_item.h"
#include "base/qthelp_url.h"
#include "apiwrap.h"

namespace Api {
namespace {

using Key = details::SingleMessageSearchKey;

Key ExtractKey(const QString &query) {
	const auto trimmed = query.trimmed();
	const auto local = Core::TryConvertUrlToLocal(trimmed);
	const auto check = local.isEmpty() ? trimmed : local;
	const auto parse = [&] {
		const auto delimeter = check.indexOf('?');
		return (delimeter > 0)
			? qthelp::url_parse_params(
				check.mid(delimeter + 1),
				qthelp::UrlParamNameTransform::ToLower)
			: QMap<QString, QString>();
	};
	if (check.startsWith(qstr("tg://privatepost"), Qt::CaseInsensitive)) {
		const auto params = parse();
		const auto channel = params.value("channel");
		const auto post = params.value("post").toInt();
		return (channel.toInt() && post) ? Key{ channel, post } : Key();
	} else if (check.startsWith(qstr("tg://resolve"), Qt::CaseInsensitive)) {
		const auto params = parse();
		const auto domain = params.value("domain");
		const auto post = params.value("post").toInt();
		return (!domain.isEmpty() && post) ? Key{ domain, post } : Key();
	}
	return Key();
}

} // namespace

SingleMessageSearch::SingleMessageSearch(not_null<Main::Session*> session)
: _session(session) {
}

SingleMessageSearch::~SingleMessageSearch() {
	clear();
}

void SingleMessageSearch::clear() {
	_cache.clear();
	_requestKey = Key();
	_session->api().request(base::take(_requestId)).cancel();
}

std::optional<HistoryItem*> SingleMessageSearch::lookup(
		const QString &query,
		Fn<void()> ready) {
	const auto key = ExtractKey(query);
	if (!key) {
		return nullptr;
	}
	const auto i = _cache.find(key);
	if (i != end(_cache)) {
		return _session->data().message(i->second);
	}
	if (!(_requestKey == key)) {
		_session->api().request(base::take(_requestId)).cancel();
		_requestKey = key;
	}
	return performLookup(ready);
}

std::optional<HistoryItem*> SingleMessageSearch::performLookupByChannel(
		not_null<ChannelData*> channel,
		Fn<void()> ready) {
	Expects(!_requestKey.empty());

	const auto postId = _requestKey.postId;
	if (const auto item = _session->data().message(channel, postId)) {
		_cache.emplace(_requestKey, item->fullId());
		return item;
	} else if (!ready) {
		return nullptr;
	}

	const auto fail = [=] {
		_cache.emplace(_requestKey, FullMsgId());
		ready();
	};
	_requestId = _session->api().request(MTPchannels_GetMessages(
		channel->inputChannel,
		MTP_vector<MTPInputMessage>(1, MTP_inputMessageID(MTP_int(postId)))
	)).done([=](const MTPmessages_Messages &result) {
		const auto received = Api::ParseSearchResult(
			channel,
			Storage::SharedMediaType::kCount,
			postId,
			Data::LoadDirection::Around,
			result);
		if (!received.messageIds.empty()
			&& received.messageIds.front() == postId) {
			_cache.emplace(
				_requestKey,
				FullMsgId(channel->bareId(), postId));
			ready();
		} else {
			fail();
		}
	}).fail([=](const MTP::Error &error) {
		fail();
	}).send();

	return std::nullopt;
}

std::optional<HistoryItem*> SingleMessageSearch::performLookupById(
		ChannelId channelId,
		Fn<void()> ready) {
	Expects(!_requestKey.empty());

	if (const auto channel = _session->data().channelLoaded(channelId)) {
		return performLookupByChannel(channel, ready);
	} else if (!ready) {
		return nullptr;
	}

	const auto fail = [=] {
		_cache.emplace(_requestKey, FullMsgId());
		ready();
	};
	_requestId = _session->api().request(MTPchannels_GetChannels(
		MTP_vector<MTPInputChannel>(
			1,
			MTP_inputChannel(MTP_int(channelId), MTP_long(0)))
	)).done([=](const MTPmessages_Chats &result) {
		result.match([&](const auto &data) {
			const auto peer = _session->data().processChats(data.vchats());
			if (peer && peer->id == peerFromChannel(channelId)) {
				if (performLookupByChannel(peer->asChannel(), ready)) {
					ready();
				}
			} else {
				fail();
			}
		});
	}).fail([=](const MTP::Error &error) {
		fail();
	}).send();

	return std::nullopt;
}

std::optional<HistoryItem*> SingleMessageSearch::performLookupByUsername(
		const QString &username,
		Fn<void()> ready) {
	Expects(!_requestKey.empty());

	if (const auto peer = _session->data().peerByUsername(username)) {
		if (const auto channel = peer->asChannel()) {
			return performLookupByChannel(channel, ready);
		}
		_cache.emplace(_requestKey, FullMsgId());
		return nullptr;
	} else if (!ready) {
		return nullptr;
	}

	const auto fail = [=] {
		_cache.emplace(_requestKey, FullMsgId());
		ready();
	};
	_requestId = _session->api().request(MTPcontacts_ResolveUsername(
		MTP_string(username)
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			const auto peerId = peerFromMTP(data.vpeer());
			const auto peer = peerId
				? _session->data().peerLoaded(peerId)
				: nullptr;
			if (const auto channel = peer ? peer->asChannel() : nullptr) {
				if (performLookupByChannel(channel, ready)) {
					ready();
				}
			} else {
				fail();
			}
		});
	}).fail([=](const MTP::Error &error) {
		fail();
	}).send();

	return std::nullopt;
}

std::optional<HistoryItem*> SingleMessageSearch::performLookup(
		Fn<void()> ready) {
	Expects(!_requestKey.empty());

	if (!_requestKey.domainOrId[0].isDigit()) {
		return performLookupByUsername(_requestKey.domainOrId, ready);
	}
	const auto channelId = _requestKey.domainOrId.toInt();
	return performLookupById(channelId, ready);
}

QString ConvertPeerSearchQuery(const QString &query) {
	const auto trimmed = query.trimmed();
	const auto local = Core::TryConvertUrlToLocal(trimmed);
	const auto check = local.isEmpty() ? trimmed : local;
	if (!check.startsWith(qstr("tg://resolve"), Qt::CaseInsensitive)) {
		return query;
	}
	const auto delimeter = check.indexOf('?');
	const auto params = (delimeter > 0)
		? qthelp::url_parse_params(
			check.mid(delimeter + 1),
			qthelp::UrlParamNameTransform::ToLower)
		: QMap<QString, QString>();
	return params.value("domain", query);
}

} // namespace Api
