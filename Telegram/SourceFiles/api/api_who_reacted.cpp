/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_who_reacted.h"

#include "history/history_item.h"
#include "history/history.h"
#include "data/data_peer.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "ui/controls/who_reacted_context_action.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace Api {
namespace {

constexpr auto kContextReactionsLimit = 50;

struct PeerWithReaction {
	PeerId peer = 0;
	QString reaction;
};
bool operator==(const PeerWithReaction &a, const PeerWithReaction &b) {
	return (a.peer == b.peer) && (a.reaction == b.reaction);
}

struct CachedRead {
	explicit CachedRead(PeerId unknownFlag)
	: list(std::vector<PeerId>{ unknownFlag }) {
	}
	rpl::variable<std::vector<PeerId>> list;
	mtpRequestId requestId = 0;
};

struct CachedReacted {
	explicit CachedReacted(PeerId unknownFlag)
	: list(
		std::vector<PeerWithReaction>{ PeerWithReaction{ unknownFlag } }) {
	}
	rpl::variable<std::vector<PeerWithReaction>> list;
	mtpRequestId requestId = 0;
};

struct Context {
	base::flat_map<not_null<HistoryItem*>, CachedRead> cachedRead;
	base::flat_map<not_null<HistoryItem*>, CachedReacted> cachedReacted;
	base::flat_map<not_null<Main::Session*>, rpl::lifetime> subscriptions;

	[[nodiscard]] CachedRead &cacheRead(not_null<HistoryItem*> item) {
		const auto i = cachedRead.find(item);
		if (i != end(cachedRead)) {
			return i->second;
		}
		return cachedRead.emplace(
			item,
			CachedRead(item->history()->session().userPeerId())
		).first->second;
	}

	[[nodiscard]] CachedReacted &cacheReacted(not_null<HistoryItem*> item) {
		const auto i = cachedReacted.find(item);
		if (i != end(cachedReacted)) {
			return i->second;
		}
		return cachedReacted.emplace(
			item,
			CachedReacted(item->history()->session().userPeerId())
		).first->second;
	}
};

struct Userpic {
	not_null<PeerData*> peer;
	QString reaction;
	mutable std::shared_ptr<Data::CloudImageView> view;
	mutable InMemoryKey uniqueKey;
};

struct State {
	std::vector<Userpic> userpics;
	Ui::WhoReadContent current;
	base::has_weak_ptr guard;
	bool someUserpicsNotLoaded = false;
	bool scheduled = false;
};

[[nodiscard]] auto Contexts()
-> base::flat_map<not_null<QWidget*>, std::unique_ptr<Context>> & {
	static auto result = base::flat_map<
		not_null<QWidget*>,
		std::unique_ptr<Context>>();
	return result;
}

[[nodiscard]] not_null<Context*> ContextAt(not_null<QWidget*> key) {
	auto &contexts = Contexts();
	const auto i = contexts.find(key);
	if (i != end(contexts)) {
		return i->second.get();
	}
	const auto result = contexts.emplace(
		key,
		std::make_unique<Context>()).first->second.get();
	QObject::connect(key.get(), &QObject::destroyed, [=] {
		auto &contexts = Contexts();
		const auto i = contexts.find(key);
		for (auto &[item, entry] : i->second->cachedRead) {
			if (const auto requestId = entry.requestId) {
				item->history()->session().api().request(requestId).cancel();
			}
		}
		for (auto &[item, entry] : i->second->cachedReacted) {
			if (const auto requestId = entry.requestId) {
				item->history()->session().api().request(requestId).cancel();
			}
		}
		contexts.erase(i);
	});
	return result;
}

[[nodiscard]] not_null<Context*> PreparedContextAt(not_null<QWidget*> key, not_null<Main::Session*> session) {
	const auto context = ContextAt(key);
	if (context->subscriptions.contains(session)) {
		return context;
	}
	session->changes().messageUpdates(
		Data::MessageUpdate::Flag::Destroyed
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		const auto i = context->cachedRead.find(update.item);
		if (i != end(context->cachedRead)) {
			session->api().request(i->second.requestId).cancel();
			context->cachedRead.erase(i);
		}
		const auto j = context->cachedReacted.find(update.item);
		if (j != end(context->cachedReacted)) {
			session->api().request(j->second.requestId).cancel();
			context->cachedReacted.erase(j);
		}
	}, context->subscriptions[session]);
	return context;
}

[[nodiscard]] QImage GenerateUserpic(Userpic &userpic, int size) {
	size *= style::DevicePixelRatio();
	auto result = userpic.peer->generateUserpicImage(userpic.view, size);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	return result;
}

[[nodiscard]] bool ListUnknown(
		const std::vector<PeerId> &list,
		not_null<HistoryItem*> item) {
	return (list.size() == 1)
		&& (list.front() == item->history()->session().userPeerId());
}

[[nodiscard]] bool ListUnknown(
		const std::vector<PeerWithReaction> &list,
		not_null<HistoryItem*> item) {
	return (list.size() == 1)
		&& list.front().reaction.isEmpty()
		&& (list.front().peer == item->history()->session().userPeerId());
}

[[nodiscard]] Ui::WhoReadType DetectSeenType(not_null<HistoryItem*> item) {
	if (const auto media = item->media()) {
		if (!media->webpage()) {
			if (const auto document = media->document()) {
				if (document->isVoiceMessage()) {
					return Ui::WhoReadType::Listened;
				} else if (document->isVideoMessage()) {
					return Ui::WhoReadType::Watched;
				}
			}
		}
	}
	return Ui::WhoReadType::Seen;
}

[[nodiscard]] rpl::producer<std::vector<PeerId>> WhoReadIds(
		not_null<HistoryItem*> item,
		not_null<QWidget*> context) {
	auto weak = QPointer<QWidget>(context.get());
	const auto session = &item->history()->session();
	return [=](auto consumer) {
		if (!weak) {
			return rpl::lifetime();
		}
		const auto context = PreparedContextAt(weak.data(), session);
		auto &entry = context->cacheRead(item);
		if (!entry.requestId) {
			entry.requestId = session->api().request(
				MTPmessages_GetMessageReadParticipants(
					item->history()->peer->input,
					MTP_int(item->id)
				)
			).done([=](const MTPVector<MTPlong> &result) {
				auto &entry = context->cacheRead(item);
				entry.requestId = 0;
				auto peers = std::vector<PeerId>();
				peers.reserve(std::max(int(result.v.size()), 1));
				for (const auto &id : result.v) {
					peers.push_back(UserId(id));
				}
				entry.list = std::move(peers);
			}).fail([=] {
				auto &entry = context->cacheRead(item);
				entry.requestId = 0;
				if (ListUnknown(entry.list.current(), item)) {
					entry.list = std::vector<PeerId>();
				}
			}).send();
		}
		return entry.list.value().start_existing(consumer);
	};
}

[[nodiscard]] std::vector < PeerWithReaction> WithEmptyReactions(
		const std::vector<PeerId> &peers) {
	return peers | ranges::views::transform([](PeerId peer) {
		return PeerWithReaction{ .peer = peer };
	}) | ranges::to_vector;
}

[[nodiscard]] rpl::producer<std::vector<PeerWithReaction>> WhoReactedIds(
		not_null<HistoryItem*> item,
		not_null<QWidget*> context) {
	auto weak = QPointer<QWidget>(context.get());
	const auto session = &item->history()->session();
	return [=](auto consumer) {
		if (!weak) {
			return rpl::lifetime();
		}
		const auto context = PreparedContextAt(weak.data(), session);
		auto &entry = context->cacheReacted(item);
		if (!entry.requestId) {
			entry.requestId = session->api().request(
				MTPmessages_GetMessageReactionsList(
					MTP_flags(0),
					item->history()->peer->input,
					MTP_int(item->id),
					MTPstring(), // reaction
					MTPstring(), // offset
					MTP_int(kContextReactionsLimit)
				)
			).done([=](const MTPmessages_MessageReactionsList &result) {
				auto &entry = context->cacheReacted(item);
				entry.requestId = 0;

				result.match([&](
						const MTPDmessages_messageReactionsList &data) {
					session->data().processUsers(data.vusers());

					auto peers = std::vector<PeerWithReaction>();
					peers.reserve(data.vreactions().v.size());
					for (const auto &vote : data.vreactions().v) {
						vote.match([&](const auto &data) {
							peers.push_back(PeerWithReaction{
								.peer = peerFromUser(data.vuser_id()),
								.reaction = qs(data.vreaction()),
							});
						});
					}
					entry.list = std::move(peers);
				});
			}).fail([=] {
				auto &entry = context->cacheReacted(item);
				entry.requestId = 0;
				if (ListUnknown(entry.list.current(), item)) {
					entry.list = std::vector<PeerWithReaction>();
				}
			}).send();
		}
		return entry.list.value().start_existing(consumer);
	};
}

[[nodiscard]] auto WhoReadOrReactedIds(
	not_null<HistoryItem*> item,
	not_null<QWidget*> context)
-> rpl::producer<std::vector<PeerWithReaction>> {
	return rpl::combine(
		WhoReactedIds(item, context),
		WhoReadIds(item, context)
	) | rpl::map([=](
			std::vector<PeerWithReaction> reacted,
			std::vector<PeerId> read) {
		if (ListUnknown(reacted, item) || ListUnknown(read, item)) {
			return reacted;
		}
		for (const auto &peer : read) {
			if (!ranges::contains(reacted, peer, &PeerWithReaction::peer)) {
				reacted.push_back({ .peer = peer });
			}
		}
		return reacted;
	});
}

bool UpdateUserpics(
		not_null<State*> state,
		not_null<HistoryItem*> item,
		const std::vector<PeerWithReaction> &ids) {
	auto &owner = item->history()->owner();

	struct ResolvedPeer {
		PeerData *peer = nullptr;
		QString reaction;
	};
	const auto peers = ranges::views::all(
		ids
	) | ranges::views::transform([&](PeerWithReaction id) {
		return ResolvedPeer{
			.peer = owner.peerLoaded(id.peer),
			.reaction = id.reaction,
		};
	}) | ranges::views::filter([](ResolvedPeer resolved) {
		return resolved.peer != nullptr;
	}) | ranges::to_vector;

	const auto same = ranges::equal(
		state->userpics,
		peers,
		ranges::equal_to(),
		&Userpic::peer,
		[](const ResolvedPeer &r) { return not_null{ r.peer }; });
	if (same) {
		return false;
	}
	auto &was = state->userpics;
	auto now = std::vector<Userpic>();
	for (const auto &resolved : peers) {
		const auto peer = not_null{ resolved.peer };
		if (ranges::contains(now, peer, &Userpic::peer)) {
			continue;
		}
		const auto i = ranges::find(was, peer, &Userpic::peer);
		if (i != end(was)) {
			now.push_back(std::move(*i));
			continue;
		}
		now.push_back(Userpic{
			.peer = peer,
			.reaction = resolved.reaction,
		});
		auto &userpic = now.back();
		userpic.uniqueKey = peer->userpicUniqueKey(userpic.view);
		peer->loadUserpic();
	}
	was = std::move(now);
	return true;
}

void RegenerateUserpics(not_null<State*> state, int small, int large) {
	Expects(state->userpics.size() == state->current.participants.size());

	state->someUserpicsNotLoaded = false;
	const auto count = int(state->userpics.size());
	for (auto i = 0; i != count; ++i) {
		auto &userpic = state->userpics[i];
		auto &participant = state->current.participants[i];
		const auto peer = userpic.peer;
		const auto key = peer->userpicUniqueKey(userpic.view);
		if (peer->hasUserpic() && peer->useEmptyUserpic(userpic.view)) {
			state->someUserpicsNotLoaded = true;
		}
		if (userpic.uniqueKey == key) {
			continue;
		}
		participant.userpicKey = userpic.uniqueKey = key;
		participant.userpicLarge = GenerateUserpic(userpic, large);
		if (i < Ui::WhoReadParticipant::kMaxSmallUserpics) {
			participant.userpicSmall = GenerateUserpic(userpic, small);
		}
	}
}

void RegenerateParticipants(not_null<State*> state, int small, int large) {
	auto old = base::take(state->current.participants);
	auto &now = state->current.participants;
	now.reserve(state->userpics.size());
	for (auto &userpic : state->userpics) {
		const auto peer = userpic.peer;
		const auto id = peer->id.value;
		const auto was = ranges::find(old, id, &Ui::WhoReadParticipant::id);
		if (was != end(old)) {
			was->name = peer->name;
			now.push_back(std::move(*was));
			continue;
		}
		now.push_back({
			.name = peer->name,
			.reaction = userpic.reaction,
			.userpicLarge = GenerateUserpic(userpic, large),
			.userpicKey = userpic.uniqueKey,
			.id = id,
		});
		if (now.size() <= Ui::WhoReadParticipant::kMaxSmallUserpics) {
			now.back().userpicSmall = GenerateUserpic(userpic, small);
		}
	}
	RegenerateUserpics(state, small, large);
}

} // namespace

bool WhoReadExists(not_null<HistoryItem*> item) {
	if (!item->out()) {
		return false;
	}
	const auto type = DetectSeenType(item);
	const auto unseen = (type == Ui::WhoReadType::Seen)
		? item->unread()
		: item->isUnreadMedia();
	if (unseen) {
		return false;
	}
	const auto history = item->history();
	const auto peer = history->peer;
	const auto chat = peer->asChat();
	const auto megagroup = peer->asMegagroup();
	if (!chat && !megagroup) {
		return false;
	}
	const auto &appConfig = peer->session().account().appConfig();
	const auto expirePeriod = TimeId(appConfig.get<double>(
		"chat_read_mark_expire_period",
		7 * 86400.));
	if (item->date() + expirePeriod <= base::unixtime::now()) {
		return false;
	}
	const auto maxCount = int(appConfig.get<double>(
		"chat_read_mark_size_threshold",
		50));
	const auto count = megagroup ? megagroup->membersCount() : chat->count;
	if (count <= 0 || count > maxCount) {
		return false;
	}
	return true;
}

bool WhoReactedExists(not_null<HistoryItem*> item) {
	return item->canViewReactions() || WhoReadExists(item);
}

rpl::producer<Ui::WhoReadContent> WhoReacted(
		not_null<HistoryItem*> item,
		not_null<QWidget*> context,
		const style::WhoRead &st) {
	const auto small = st.userpics.size;
	const auto large = st.photoSize;
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto resolveWhoRead = WhoReadExists(item);

		const auto state = lifetime.make_state<State>();
		const auto pushNext = [=] {
			consumer.put_next_copy(state->current);
		};

		const auto resolveWhoReacted = item->canViewReactions();
		auto idsWithReactions = (resolveWhoRead && resolveWhoReacted)
			? WhoReadOrReactedIds(item, context)
			: resolveWhoRead
			? (WhoReadIds(item, context) | rpl::map(WithEmptyReactions))
			: WhoReactedIds(item, context);
		state->current.type = resolveWhoRead
			? DetectSeenType(item)
			: Ui::WhoReadType::Reacted;
		if (resolveWhoReacted) {
			const auto &list = item->reactions();
			state->current.fullReactionsCount = ranges::accumulate(
				list,
				0,
				ranges::plus{},
				[](const auto &pair) { return pair.second; });

			// #TODO reactions
			state->current.singleReaction = (list.size() == 1)
				? list.front().first
				: QString();
		}
		std::move(
			idsWithReactions
		) | rpl::start_with_next([=](
				const std::vector<PeerWithReaction> &peers) {
			if (ListUnknown(peers, item)) {
				state->userpics.clear();
				consumer.put_next(Ui::WhoReadContent{
					.type = state->current.type,
					.unknown = true,
				});
				return;
			} else if (UpdateUserpics(state, item, peers)) {
				RegenerateParticipants(state, small, large);
				pushNext();
			} else if (peers.empty()) {
				pushNext();
			}
		}, lifetime);

		item->history()->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return state->someUserpicsNotLoaded && !state->scheduled;
		}) | rpl::start_with_next([=] {
			for (const auto &userpic : state->userpics) {
				if (userpic.peer->userpicUniqueKey(userpic.view)
					!= userpic.uniqueKey) {
					state->scheduled = true;
					crl::on_main(&state->guard, [=] {
						state->scheduled = false;
						RegenerateUserpics(state, small, large);
						pushNext();
					});
					return;
				}
			}
		}, lifetime);

		return lifetime;
	};
}

} // namespace Api
