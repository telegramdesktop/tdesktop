/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_suggestions.h"

#include "base/unixtime.h"
#include "data/components/top_peers.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/dynamic_thumbnails.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"

namespace Dialogs {

Suggestions::Suggestions(
	not_null<QWidget*> parent,
	rpl::producer<TopPeersList> topPeers)
: RpWidget(parent)
, _scroll(std::make_unique<Ui::ElasticScroll>(this))
, _content(_scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this)))
, _topPeersWrap(_content->add(object_ptr<Ui::SlideWrap<TopPeersStrip>>(
	this,
	object_ptr<TopPeersStrip>(this, std::move(topPeers)))))
, _topPeers(_topPeersWrap->entity())
, _divider(_content->add(setupDivider())) {
	_topPeers->emptyValue() | rpl::start_with_next([=](bool empty) {
		_topPeersWrap->toggle(!empty, anim::type::instant);
	}, _topPeers->lifetime());

	_topPeers->clicks() | rpl::start_with_next([=](uint64 peerIdRaw) {
		_topPeerChosen.fire(PeerId(peerIdRaw));
	}, _topPeers->lifetime());
}

Suggestions::~Suggestions() = default;

void Suggestions::paintEvent(QPaintEvent *e) {
	QPainter(this).fillRect(e->rect(), st::windowBg);
}

void Suggestions::resizeEvent(QResizeEvent *e) {
	_scroll->setGeometry(rect());
	_content->resizeToWidth(width());
}

object_ptr<Ui::RpWidget> Suggestions::setupDivider() {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		this,
		st::searchedBarHeight);
	const auto raw = result.data();
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		tr::lng_recent_title(),
		st::searchedBarLabel);
	const auto clear = Ui::CreateChild<Ui::LinkButton>(
		raw,
		tr::lng_recent_clear(tr::now),
		st::searchedBarLink);
	rpl::combine(
		raw->sizeValue(),
		clear->widthValue()
	) | rpl::start_with_next([=](QSize size, int width) {
		const auto x = st::searchedBarPosition.x();
		const auto y = st::searchedBarPosition.y();
		clear->moveToRight(0, 0, size.width());
		label->resizeToWidth(size.width() - x - width);
		label->moveToLeft(x, y, size.width());
	}, raw->lifetime());
	raw->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(raw).fillRect(clip, st::searchedBarBg);
	}, raw->lifetime());
	return result;
}

rpl::producer<TopPeersList> TopPeersContent(
		not_null<Main::Session*> session) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		struct Entry {
			not_null<History*> history;
			int index = 0;
		};
		struct State {
			TopPeersList data;
			base::flat_map<not_null<PeerData*>, Entry> indices;
			base::has_weak_ptr guard;
			bool scheduled = true;
		};
		auto state = lifetime.make_state<State>();
		const auto top = session->topPeers().list();
		auto &entries = state->data.entries;
		auto &indices = state->indices;
		entries.reserve(top.size());
		indices.reserve(top.size());
		const auto now = base::unixtime::now();
		for (const auto &peer : top) {
			const auto user = peer->asUser();
			const auto self = user && user->isSelf();
			const auto history = peer->owner().history(peer);
			const auto badges = history->chatListBadgesState();
			entries.push_back({
				.id = peer->id.value,
				.name = (self
					? tr::lng_saved_messages(tr::now)
					: peer->shortName()),
				.userpic = (self
					? Ui::MakeSavedMessagesThumbnail()
					: Ui::MakeUserpicThumbnail(peer)),
				.badge = uint32(badges.unreadCounter),
				.unread = badges.unread,
				.muted = !self && history->muted(),
				.online = user && !self && Data::IsUserOnline(user, now),
			});
			if (entries.back().online) {
				user->owner().watchForOffline(user, now);
			}
			indices.emplace(peer, Entry{
				.history = peer->owner().history(peer),
				.index = int(entries.size()) - 1,
			});
		}

		const auto push = [=] {
			if (!state->scheduled) {
				return;
			}
			state->scheduled = false;
			consumer.put_next_copy(state->data);
		};
		const auto schedule = [=] {
			if (state->scheduled) {
				return;
			}
			state->scheduled = true;
			crl::on_main(&state->guard, push);
		};

		using Flag = Data::PeerUpdate::Flag;
		session->changes().peerUpdates(
			Flag::Name
			| Flag::Photo
			| Flag::Notifications
			| Flag::OnlineStatus
		) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
			const auto peer = update.peer;
			if (peer->isSelf()) {
				return;
			}
			const auto i = state->indices.find(peer);
			if (i == end(state->indices)) {
				return;
			}
			auto changed = false;
			auto &entry = state->data.entries[i->second.index];
			const auto flags = update.flags;
			if (flags & Flag::Name) {
				const auto now = peer->shortName();
				if (entry.name != now) {
					entry.name = now;
					changed = true;
				}
			}
			if (flags & Flag::Photo) {
				entry.userpic = Ui::MakeUserpicThumbnail(peer);
				changed = true;
			}
			if (flags & Flag::Notifications) {
				const auto now = i->second.history->muted();
				if (entry.muted != now) {
					entry.muted = now;
					changed = true;
				}
			}
			if (flags & Flag::OnlineStatus) {
				if (const auto user = peer->asUser()) {
					const auto now = base::unixtime::now();
					const auto value = Data::IsUserOnline(user, now);
					if (entry.online != value) {
						entry.online = value;
						changed = true;
						if (value) {
							user->owner().watchForOffline(user, now);
						}
					}
				}
			}
			if (changed) {
				schedule();
			}
		}, lifetime);

		session->data().unreadBadgeChanges(
		) | rpl::start_with_next([=] {
			auto changed = false;
			auto &entries = state->data.entries;
			for (const auto &[peer, data] : state->indices) {
				const auto badges = data.history->chatListBadgesState();
				auto &entry = entries[data.index];
				if (entry.badge != badges.unreadCounter
					|| entry.unread != badges.unread) {
					entry.badge = badges.unreadCounter;
					entry.unread = badges.unread;
					changed = true;
				}
			}
			if (changed) {
				schedule();
			}
		}, lifetime);

		push();
		return lifetime;
	};
}

} // namespace Dialogs
