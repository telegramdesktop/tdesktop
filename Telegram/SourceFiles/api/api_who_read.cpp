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
#include "ui/controls/who_read_context_action.h"
#include "apiwrap.h"

namespace Api {
namespace {

struct Cached {
	explicit Cached(UserId unknownFlag)
	: list(std::vector<UserId>{ unknownFlag }) {
	}
	rpl::variable<std::vector<UserId>> list;
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
			Cached(item->history()->session().userId())
		).first->second;
	}
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

[[nodiscard]] bool ListUnknown(
		const std::vector<UserId> &list,
		not_null<HistoryItem*> item) {
	return (list.size() == 1)
		&& (list.front() == item->history()->session().userId());
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

rpl::producer<std::vector<UserId>> WhoReadIds(
		not_null<HistoryItem*> item,
		not_null<QWidget*> context) {
	auto weak = QPointer<QWidget>(context.get());
	const auto fullId = item->fullId();
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
			const auto makeEmpty = [=] {
				// Special value that marks a validated empty list.
				return std::vector<UserId>{
					item->history()->session().userId()
				};
			};
			entry.requestId = session->api().request(
				MTPmessages_GetMessageReadParticipants(
					item->history()->peer->input,
					MTP_int(item->id)
				)
			).done([=](const MTPVector<MTPlong> &result) {
				auto &entry = context->cache(item);
				entry.requestId = 0;
				auto users = std::vector<UserId>();
				users.reserve(std::max(result.v.size(), 1));
				for (const auto &id : result.v) {
					users.push_back(UserId(id));
				}
				entry.list = std::move(users);
			}).fail([=](const MTP::Error &error) {
				auto &entry = context->cache(item);
				entry.requestId = 0;
				if (ListUnknown(entry.list.current(), item)) {
					entry.list = std::vector<UserId>();
				}
			}).send();
		}
		return entry.list.value().start_existing(consumer);
	};
}

rpl::producer<Ui::WhoReadContent> WhoRead(
		not_null<HistoryItem*> item,
		not_null<QWidget*> context) {
	return WhoReadIds(
		item,
		context
	) | rpl::map([=](const std::vector<UserId> &users) {
		const auto owner = &item->history()->owner();
		if (ListUnknown(users, item)) {
			return Ui::WhoReadContent{ .unknown = true };
		}
		auto participants = ranges::views::all(
			users
		) | ranges::views::transform([&](UserId id) {
			return owner->userLoaded(id);
		}) | ranges::views::filter([](UserData *user) {
			return user != nullptr;
		}) | ranges::views::transform([](UserData *user) {
			return Ui::WhoReadParticipant{
				.name = user->name,
				.id = user->id.value,
			};
		}) | ranges::to_vector;
		return Ui::WhoReadContent{
			.participants = std::move(participants),
			.type = DetectType(item),
		};
	});
}

} // namespace Api
