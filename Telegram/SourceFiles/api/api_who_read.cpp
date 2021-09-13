/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_who_read.h"

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
#include "ui/controls/who_read_context_action.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace Api {
namespace {

struct Cached {
	explicit Cached(PeerId unknownFlag)
	: list(std::vector<PeerId>{ unknownFlag }) {
	}
	rpl::variable<std::vector<PeerId>> list;
	mtpRequestId requestId = 0;
};

struct Context {
	base::flat_map<not_null<HistoryItem*>, Cached> cached;
	base::flat_map<not_null<Main::Session*>, rpl::lifetime> subscriptions;

	[[nodiscard]] Cached &cache(not_null<HistoryItem*> item) {
		const auto i = cached.find(item);
		if (i != end(cached)) {
			return i->second;
		}
		return cached.emplace(
			item,
			Cached(item->history()->session().userPeerId())
		).first->second;
	}
};

struct Userpic {
	not_null<PeerData*> peer;
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
		for (auto &[item, entry] : i->second->cached) {
			if (const auto requestId = entry.requestId) {
				item->history()->session().api().request(requestId).cancel();
			}
		}
		contexts.erase(i);
	});
	return result;
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

[[nodiscard]] Ui::WhoReadType DetectType(not_null<HistoryItem*> item) {
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
		const auto context = ContextAt(weak.data());
		if (!context->subscriptions.contains(session)) {
			session->changes().messageUpdates(
				Data::MessageUpdate::Flag::Destroyed
			) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
				const auto i = context->cached.find(update.item);
				if (i == end(context->cached)) {
					return;
				}
				session->api().request(i->second.requestId).cancel();
				context->cached.erase(i);
			}, context->subscriptions[session]);
		}
		auto &entry = context->cache(item);
		if (!entry.requestId) {
			entry.requestId = session->api().request(
				MTPmessages_GetMessageReadParticipants(
					item->history()->peer->input,
					MTP_int(item->id)
				)
			).done([=](const MTPVector<MTPlong> &result) {
				auto &entry = context->cache(item);
				entry.requestId = 0;
				auto peers = std::vector<PeerId>();
				peers.reserve(std::max(result.v.size(), 1));
				for (const auto &id : result.v) {
					peers.push_back(UserId(id));
				}
				entry.list = std::move(peers);
			}).fail([=](const MTP::Error &error) {
				auto &entry = context->cache(item);
				entry.requestId = 0;
				if (ListUnknown(entry.list.current(), item)) {
					entry.list = std::vector<PeerId>();
				}
			}).send();
		}
		return entry.list.value().start_existing(consumer);
	};
}

bool UpdateUserpics(
		not_null<State*> state,
		not_null<HistoryItem*> item,
		const std::vector<PeerId> &ids) {
	auto &owner = item->history()->owner();

	const auto peers = ranges::views::all(
		ids
	) | ranges::views::transform([&](PeerId id) {
		return owner.peerLoaded(id);
	}) | ranges::views::filter([](PeerData *peer) {
		return peer != nullptr;
	}) | ranges::views::transform([](PeerData *peer) {
		return not_null(peer);
	}) | ranges::to_vector;

	const auto same = ranges::equal(
		state->userpics,
		peers,
		ranges::less(),
		&Userpic::peer);
	if (same) {
		return false;
	}
	auto &was = state->userpics;
	auto now = std::vector<Userpic>();
	for (const auto &peer : peers) {
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
	const auto type = DetectType(item);
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
	} else if (peer->migrateTo()) {
		// They're all always marked as read.
		// We don't know if there really are any readers.
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

rpl::producer<Ui::WhoReadContent> WhoRead(
		not_null<HistoryItem*> item,
		not_null<QWidget*> context,
		const style::WhoRead &st) {
	const auto small = st.userpics.size;
	const auto large = st.photoSize;
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto state = lifetime.make_state<State>();
		state->current.type = [&] {
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
		}();
		const auto pushNext = [=] {
			consumer.put_next_copy(state->current);
		};

		WhoReadIds(
			item,
			context
		) | rpl::start_with_next([=](const std::vector<PeerId> &peers) {
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
