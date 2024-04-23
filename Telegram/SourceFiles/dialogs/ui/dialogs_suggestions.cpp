/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_suggestions.h"

#include "api/api_chat_participants.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "boxes/peer_list_box.h"
#include "data/components/recent_peers.h"
#include "data/components/top_peers.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_folder.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/delayed_activation.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/painter.h"
#include "ui/unread_badge_paint.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

namespace Dialogs {
namespace {

constexpr auto kCollapsedChannelsCount = 5;
constexpr auto kProbablyMaxChannels = 1000;
constexpr auto kProbablyMaxRecommendations = 100;

class RecentRow final : public PeerListRow {
public:
	explicit RecentRow(not_null<PeerData*> peer);

	bool refreshBadge();

	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;
	void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;
	bool rightActionDisabled() const override;

	const style::PeerListItem &computeSt(
		const style::PeerListItem &st) const override;

private:
	const not_null<History*> _history;
	QString _badgeString;
	QSize _badgeSize;
	uint32 _counter : 30 = 0;
	uint32 _unread : 1 = 0;
	uint32 _muted : 1 = 0;

};

class RecentsController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	RecentsController(
		not_null<Window::SessionController*> window,
		RecentPeersList list);

	[[nodiscard]] rpl::producer<int> count() const {
		return _count.value();
	}
	[[nodiscard]] rpl::producer<not_null<PeerData*>> chosen() const {
		return _chosen.events();
	}

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	QString savedMessagesChatStatus() const override;

private:
	void setupDivider();
	void subscribeToEvents();
	[[nodiscard]] Fn<void()> removeAllCallback();

	const not_null<Window::SessionController*> _window;
	RecentPeersList _recent;
	rpl::variable<int> _count;
	rpl::event_stream<not_null<PeerData*>> _chosen;
	rpl::lifetime _lifetime;

};

class ChannelRow final : public PeerListRow {
public:
	using PeerListRow::PeerListRow;

	void setActive(bool active);

	const style::PeerListItem &computeSt(
		const style::PeerListItem &st) const override;

private:
	bool _active = false;

};

class MyChannelsController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	explicit MyChannelsController(
		not_null<Window::SessionController*> window);

	[[nodiscard]] rpl::producer<int> count() const {
		return _count.value();
	}
	[[nodiscard]] rpl::producer<not_null<PeerData*>> chosen() const {
		return _chosen.events();
	}

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

private:
	void setupDivider();
	void appendRow(not_null<ChannelData*> channel);
	void fill(bool force = false);

	const not_null<Window::SessionController*> _window;
	std::vector<not_null<History*>> _channels;
	rpl::variable<Ui::RpWidget*> _toggleExpanded = nullptr;
	rpl::variable<int> _count = 0;
	rpl::variable<bool> _expanded = false;
	rpl::event_stream<not_null<PeerData*>> _chosen;
	rpl::lifetime _lifetime;

};

class RecommendationsController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	explicit RecommendationsController(
		not_null<Window::SessionController*> window);

	[[nodiscard]] rpl::producer<int> count() const {
		return _count.value();
	}
	[[nodiscard]] rpl::producer<not_null<PeerData*>> chosen() const {
		return _chosen.events();
	}

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	void load();

private:
	void fill();
	void setupDivider();
	void appendRow(not_null<ChannelData*> channel);

	const not_null<Window::SessionController*> _window;
	rpl::variable<int> _count;
	History *_activeHistory = nullptr;
	bool _requested = false;
	rpl::event_stream<not_null<PeerData*>> _chosen;
	rpl::lifetime _lifetime;

};

struct EntryMenuDescriptor {
	not_null<Window::SessionController*> controller;
	not_null<PeerData*> peer;
	QString removeOneText;
	Fn<void()> removeOne;
	QString removeAllText;
	QString removeAllConfirm;
	Fn<void()> removeAll;
};

[[nodiscard]] Fn<void()> RemoveAllConfirm(
		not_null<Window::SessionController*> controller,
		QString removeAllConfirm,
		Fn<void()> removeAll) {
	return [=] {
		controller->show(Ui::MakeConfirmBox({
			.text = removeAllConfirm,
			.confirmed = [=](Fn<void()> close) { removeAll(); close(); }
		}));
	};
}

void FillEntryMenu(
		const Ui::Menu::MenuCallback &add,
		EntryMenuDescriptor &&descriptor) {
	const auto peer = descriptor.peer;
	const auto controller = descriptor.controller;
	const auto group = peer->isMegagroup();
	const auto channel = peer->isChannel();

	add(tr::lng_context_new_window(tr::now), [=] {
		Ui::PreventDelayedActivation();
		controller->showInNewWindow(peer);
	}, &st::menuIconNewWindow);
	Window::AddSeparatorAndShiftUp(add);

	const auto showHistoryText = group
		? tr::lng_context_open_group(tr::now)
		: channel
		? tr::lng_context_open_channel(tr::now)
		: tr::lng_profile_send_message(tr::now);
	add(showHistoryText, [=] {
		controller->showPeerHistory(peer);
	}, channel ? &st::menuIconChannel : &st::menuIconChatBubble);

	const auto viewProfileText = group
		? tr::lng_context_view_group(tr::now)
		: channel
		? tr::lng_context_view_channel(tr::now)
		: tr::lng_context_view_profile(tr::now);
	add(viewProfileText, [=] {
		controller->showPeerInfo(peer);
	}, channel ? &st::menuIconInfo : &st::menuIconProfile);

	add({ .separatorSt = &st::expandedMenuSeparator });

	add({
		.text = descriptor.removeOneText,
		.handler = descriptor.removeOne,
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});

	add({
		.text = descriptor.removeAllText,
		.handler = RemoveAllConfirm(
			descriptor.controller,
			descriptor.removeAllConfirm,
			descriptor.removeAll),
		.icon = &st::menuIconCancelAttention,
		.isAttention = true,
	});
}

RecentRow::RecentRow(not_null<PeerData*> peer)
: PeerListRow(peer)
, _history(peer->owner().history(peer)) {
	if (peer->isSelf() || peer->isRepliesChat()) {
		setCustomStatus(u" "_q);
	} else if (const auto chat = peer->asChat()) {
		if (chat->count > 0) {
			setCustomStatus(
				tr::lng_chat_status_members(
					tr::now,
					lt_count_decimal,
					chat->count));
		}
	} else if (const auto channel = peer->asChannel()) {
		if (channel->membersCountKnown()) {
			setCustomStatus((channel->isBroadcast()
				? tr::lng_chat_status_subscribers
				: tr::lng_chat_status_members)(
					tr::now,
					lt_count_decimal,
					channel->membersCount()));
		}
	}
	refreshBadge();
}

bool RecentRow::refreshBadge() {
	if (_history->peer->isSelf()) {
		return false;
	}
	auto result = false;
	const auto muted = _history->muted() ? 1 : 0;
	if (_muted != muted) {
		_muted = muted;
		if (_counter || _unread) {
			result = true;
		}
	}
	const auto badges = _history->chatListBadgesState();
	const auto unread = badges.unread ? 1 : 0;
	if (_counter != badges.unreadCounter || _unread != unread) {
		_counter = badges.unreadCounter;
		_unread = unread;
		result = true;

		_badgeString = !_counter
			? (_unread ? u" "_q : QString())
			: (_counter < 1000)
			? QString::number(_counter)
			: (QString::number(_counter / 1000) + 'K');
		if (_badgeString.isEmpty()) {
			_badgeSize = QSize();
		} else {
			auto st = Ui::UnreadBadgeStyle();
			const auto unreadRectHeight = st.size;
			const auto unreadWidth = st.font->width(_badgeString);
			_badgeSize = QSize(
				std::max(unreadWidth + 2 * st.padding, unreadRectHeight),
				unreadRectHeight);
		}
	}
	return result;
}

QSize RecentRow::rightActionSize() const {
	return _badgeSize;
}

QMargins RecentRow::rightActionMargins() const {
	if (_badgeSize.isEmpty()) {
		return {};
	}
	const auto x = st::recentPeersItem.photoPosition.x();
	const auto y = (st::recentPeersItem.height - _badgeSize.height()) / 2;
	return QMargins(x, y, x, y);
}

void RecentRow::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	if (!_counter && !_unread) {
		return;
	} else if (_badgeString.isEmpty()) {
		_badgeString = !_counter
			? u" "_q
			: (_counter < 1000)
			? QString::number(_counter)
			: (QString::number(_counter / 1000) + 'K');
	}
	auto st = Ui::UnreadBadgeStyle();
	st.selected = selected;
	st.muted = _muted;
	const auto &counter = _badgeString;
	PaintUnreadBadge(p, counter, x + _badgeSize.width(), y, st);
}

bool RecentRow::rightActionDisabled() const {
	return true;
}

const style::PeerListItem &RecentRow::computeSt(
		const style::PeerListItem &st) const {
	return (peer()->isSelf() || peer()->isRepliesChat())
		? st::recentPeersSpecialName
		: st;
}

void ChannelRow::setActive(bool active) {
	_active = active;
}

const style::PeerListItem &ChannelRow::computeSt(
		const style::PeerListItem &st) const {
	return _active ? st::recentPeersItemActive : st::recentPeersItem;
}

RecentsController::RecentsController(
	not_null<Window::SessionController*> window,
	RecentPeersList list)
: _window(window)
, _recent(std::move(list)) {
}

void RecentsController::prepare() {
	setupDivider();

	for (const auto &peer : _recent.list) {
		delegate()->peerListAppendRow(std::make_unique<RecentRow>(peer));
	}
	delegate()->peerListRefreshRows();
	_count = _recent.list.size();

	subscribeToEvents();
}

void RecentsController::rowClicked(not_null<PeerListRow*> row) {
	_chosen.fire(row->peer());
}

Fn<void()> RecentsController::removeAllCallback() {
	const auto weak = base::make_weak(this);
	const auto session = &_window->session();
	return crl::guard(session, [=] {
		if (weak) {
			_count = 0;
			while (delegate()->peerListFullRowsCount() > 0) {
				delegate()->peerListRemoveRow(delegate()->peerListRowAt(0));
			}
			delegate()->peerListRefreshRows();
		}
		session->recentPeers().clear();
	});
}

base::unique_qptr<Ui::PopupMenu> RecentsController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto peer = row->peer();
	const auto weak = base::make_weak(this);
	const auto session = &_window->session();
	const auto removeOne = crl::guard(session, [=] {
		if (weak) {
			const auto rowId = peer->id.value;
			if (const auto row = delegate()->peerListFindRow(rowId)) {
				_count = std::max(0, _count.current() - 1);
				delegate()->peerListRemoveRow(row);
				delegate()->peerListRefreshRows();
			}
		}
		session->recentPeers().remove(peer);
	});
	FillEntryMenu(Ui::Menu::CreateAddActionCallback(result), {
		.controller = _window,
		.peer = peer,
		.removeOneText = tr::lng_recent_remove(tr::now),
		.removeOne = removeOne,
		.removeAllText = tr::lng_recent_clear_all(tr::now),
		.removeAllConfirm = tr::lng_recent_clear_sure(tr::now),
		.removeAll = removeAllCallback(),
	});
	return result;
}

Main::Session &RecentsController::session() const {
	return _window->session();
}

QString RecentsController::savedMessagesChatStatus() const {
	return tr::lng_saved_forward_here(tr::now);
}

void RecentsController::setupDivider() {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		(QWidget*)nullptr,
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
	clear->setClickedCallback(RemoveAllConfirm(
		_window,
		tr::lng_recent_clear_sure(tr::now),
		removeAllCallback()));
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

	delegate()->peerListSetAboveWidget(std::move(result));
}

void RecentsController::subscribeToEvents() {
	using Flag = Data::PeerUpdate::Flag;
	session().changes().peerUpdates(
		Flag::Notifications
		| Flag::OnlineStatus
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		const auto peer = update.peer;
		if (peer->isSelf()) {
			return;
		}
		auto refreshed = false;
		const auto row = delegate()->peerListFindRow(update.peer->id.value);
		if (!row) {
			return;
		} else if (update.flags & Flag::Notifications) {
			refreshed = static_cast<RecentRow*>(row)->refreshBadge();
		}
		if (!peer->isRepliesChat() && (update.flags & Flag::OnlineStatus)) {
			row->clearCustomStatus();
			refreshed = true;
		}
		if (refreshed) {
			delegate()->peerListUpdateRow(row);
		}
	}, _lifetime);

	session().data().unreadBadgeChanges(
	) | rpl::start_with_next([=] {
		for (auto i = 0; i != _count.current(); ++i) {
			const auto row = delegate()->peerListRowAt(i);
			if (static_cast<RecentRow*>(row.get())->refreshBadge()) {
				delegate()->peerListUpdateRow(row);
			}
		}
	}, _lifetime);
}

MyChannelsController::MyChannelsController(
	not_null<Window::SessionController*> window)
: _window(window) {
}

void MyChannelsController::prepare() {
	setupDivider();

	session().changes().peerUpdates(
		Data::PeerUpdate::Flag::ChannelAmIn
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		const auto channel = update.peer->asBroadcast();
		if (!channel || channel->amIn()) {
			return;
		}
		const auto history = channel->owner().history(channel);
		const auto i = ranges::remove(_channels, history);
		if (i == end(_channels)) {
			return;
		}
		_channels.erase(i, end(_channels));
		const auto row = delegate()->peerListFindRow(channel->id.value);
		if (row) {
			delegate()->peerListRemoveRow(row);
		}
		_count = int(_channels.size());
		fill(true);
	}, _lifetime);

	_channels.reserve(kProbablyMaxChannels);
	const auto owner = &session().data();
	const auto add = [&](not_null<Dialogs::MainList*> list) {
		for (const auto &row : list->indexed()->all()) {
			if (const auto history = row->history()) {
				if (const auto channel = history->peer->asBroadcast()) {
					_channels.push_back(history);
				}
			}
		}
	};
	add(owner->chatsList());
	if (const auto folder = owner->folderLoaded(Data::Folder::kId)) {
		add(owner->chatsList(folder));
	}

	ranges::sort(_channels, ranges::greater(), &History::chatListTimeId);
	_count = int(_channels.size());

	_expanded.value() | rpl::start_with_next([=] {
		fill();
	}, _lifetime);

	auto loading = owner->chatsListChanges(
	) | rpl::take_while([=](Data::Folder *folder) {
		return !owner->chatsListLoaded(folder);
	});
	rpl::merge(
		std::move(loading),
		owner->chatsListLoadedEvents()
	) | rpl::start_with_next([=](Data::Folder *folder) {
		const auto list = owner->chatsList(folder);
		for (const auto &row : list->indexed()->all()) {
			if (const auto history = row->history()) {
				if (const auto channel = history->peer->asBroadcast()) {
					if (ranges::contains(_channels, not_null(history))) {
						_channels.push_back(history);
					}
				}
			}
		}
		const auto was = _count.current();
		const auto now = int(_channels.size());
		if (was != now) {
			_count = now;
			fill();
		}
	}, _lifetime);
}

void MyChannelsController::fill(bool force) {
	const auto count = _count.current();
	const auto limit = _expanded.current()
		? count
		: std::min(count, kCollapsedChannelsCount);
	const auto already = delegate()->peerListFullRowsCount();
	const auto delta = limit - already;
	if (!delta && !force) {
		return;
	} else if (delta > 0) {
		for (auto i = already; i != limit; ++i) {
			appendRow(_channels[i]->peer->asBroadcast());
		}
	} else if (delta < 0) {
		for (auto i = already; i != limit;) {
			delegate()->peerListRemoveRow(delegate()->peerListRowAt(--i));
		}
	}
	delegate()->peerListRefreshRows();
}

void MyChannelsController::appendRow(not_null<ChannelData*> channel) {
	auto row = std::make_unique<PeerListRow>(channel);
	if (channel->membersCountKnown()) {
		row->setCustomStatus((channel->isBroadcast()
			? tr::lng_chat_status_subscribers
			: tr::lng_chat_status_members)(
				tr::now,
				lt_count_decimal,
				channel->membersCount()));
	}
	delegate()->peerListAppendRow(std::move(row));
}

void MyChannelsController::rowClicked(not_null<PeerListRow*> row) {
	_chosen.fire(row->peer());
}

base::unique_qptr<Ui::PopupMenu> MyChannelsController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto peer = row->peer();
	const auto addAction = Ui::Menu::CreateAddActionCallback(result);
	Window::FillDialogsEntryMenu(
		_window,
		Dialogs::EntryState{
			.key = peer->owner().history(peer),
			.section = Dialogs::EntryState::Section::ContextMenu,
		},
		addAction);
	return result;
}

Main::Session &MyChannelsController::session() const {
	return _window->session();
}

void MyChannelsController::setupDivider() {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		(QWidget*)nullptr,
		st::searchedBarHeight);
	const auto raw = result.data();
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		tr::lng_channels_your_title(),
		st::searchedBarLabel);
	_count.value(
	) | rpl::map(
		rpl::mappers::_1 > kCollapsedChannelsCount
	) | rpl::distinct_until_changed() | rpl::start_with_next([=](bool more) {
		_expanded = false;
		if (!more) {
			const auto toggle = _toggleExpanded.current();
			_toggleExpanded = nullptr;
			delete toggle;
			return;
		} else if (_toggleExpanded.current()) {
			return;
		}
		const auto toggle = Ui::CreateChild<Ui::LinkButton>(
			raw,
			tr::lng_channels_your_more(tr::now),
			st::searchedBarLink);
		toggle->show();
		toggle->setClickedCallback([=] {
			const auto expand = !_expanded.current();
			toggle->setText(expand
				? tr::lng_channels_your_less(tr::now)
				: tr::lng_channels_your_more(tr::now));
			_expanded = expand;
		});
		rpl::combine(
			raw->sizeValue(),
			toggle->widthValue()
		) | rpl::start_with_next([=](QSize size, int width) {
			const auto x = st::searchedBarPosition.x();
			const auto y = st::searchedBarPosition.y();
			toggle->moveToRight(0, 0, size.width());
			label->resizeToWidth(size.width() - x - width);
			label->moveToLeft(x, y, size.width());
		}, toggle->lifetime());
		_toggleExpanded = toggle;
	}, raw->lifetime());

	rpl::combine(
		raw->sizeValue(),
		_toggleExpanded.value()
	) | rpl::filter(
		rpl::mappers::_2 == nullptr
	) | rpl::start_with_next([=](QSize size, const auto) {
		const auto x = st::searchedBarPosition.x();
		const auto y = st::searchedBarPosition.y();
		label->resizeToWidth(size.width() - x * 2);
		label->moveToLeft(x, y, size.width());
	}, raw->lifetime());

	raw->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(raw).fillRect(clip, st::searchedBarBg);
	}, raw->lifetime());

	delegate()->peerListSetAboveWidget(std::move(result));
}

RecommendationsController::RecommendationsController(
	not_null<Window::SessionController*> window)
: _window(window) {
}

void RecommendationsController::prepare() {
	setupDivider();
	fill();
}

void RecommendationsController::load() {
	if (_requested || _count.current()) {
		return;
	}
	_requested = true;
	const auto participants = &session().api().chatParticipants();
	participants->loadRecommendations();
	participants->recommendationsLoaded(
	) | rpl::take(1) | rpl::start_with_next([=] {
		fill();
	}, _lifetime);
}

void RecommendationsController::fill() {
	const auto participants = &session().api().chatParticipants();
	const auto &list = participants->recommendations().list;
	if (list.empty()) {
		return;
	}
	for (const auto &peer : list) {
		if (const auto channel = peer->asBroadcast()) {
			appendRow(channel);
		}
	}
	delegate()->peerListRefreshRows();
	_count = delegate()->peerListFullRowsCount();

	_window->activeChatValue() | rpl::start_with_next([=](const Key &key) {
		const auto history = key.history();
		if (_activeHistory == history) {
			return;
		} else if (_activeHistory) {
			const auto id = _activeHistory->peer->id.value;
			if (const auto row = delegate()->peerListFindRow(id)) {
				static_cast<ChannelRow*>(row)->setActive(false);
				delegate()->peerListUpdateRow(row);
			}
		}
		_activeHistory = history;
		if (_activeHistory) {
			const auto id = _activeHistory->peer->id.value;
			if (const auto row = delegate()->peerListFindRow(id)) {
				static_cast<ChannelRow*>(row)->setActive(true);
				delegate()->peerListUpdateRow(row);
			}
		}
	}, _lifetime);
}

void RecommendationsController::appendRow(not_null<ChannelData*> channel) {
	auto row = std::make_unique<ChannelRow>(channel);
	if (channel->membersCountKnown()) {
		row->setCustomStatus((channel->isBroadcast()
			? tr::lng_chat_status_subscribers
			: tr::lng_chat_status_members)(
				tr::now,
				lt_count_decimal,
				channel->membersCount()));
	}
	delegate()->peerListAppendRow(std::move(row));
}

void RecommendationsController::rowClicked(not_null<PeerListRow*> row) {
	_chosen.fire(row->peer());
}

base::unique_qptr<Ui::PopupMenu> RecommendationsController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	return nullptr;
}

Main::Session &RecommendationsController::session() const {
	return _window->session();
}

void RecommendationsController::setupDivider() {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		(QWidget*)nullptr,
		st::searchedBarHeight);
	const auto raw = result.data();
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		tr::lng_channels_recommended(),
		st::searchedBarLabel);
	raw->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto x = st::searchedBarPosition.x();
		const auto y = st::searchedBarPosition.y();
		label->resizeToWidth(size.width() - x * 2);
		label->moveToLeft(x, y, size.width());
	}, raw->lifetime());
	raw->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(raw).fillRect(clip, st::searchedBarBg);
	}, raw->lifetime());

	delegate()->peerListSetAboveWidget(std::move(result));
}

} // namespace

Suggestions::Suggestions(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	rpl::producer<TopPeersList> topPeers,
	RecentPeersList recentPeers)
: RpWidget(parent)
, _controller(controller)
, _tabs(std::make_unique<Ui::SettingsSlider>(this, st::dialogsSearchTabs))
, _chatsScroll(std::make_unique<Ui::ElasticScroll>(this))
, _chatsContent(
	_chatsScroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this)))
, _topPeersWrap(
	_chatsContent->add(object_ptr<Ui::SlideWrap<TopPeersStrip>>(
		this,
		object_ptr<TopPeersStrip>(this, std::move(topPeers)))))
, _topPeers(_topPeersWrap->entity())
, _recentPeers(_chatsContent->add(setupRecentPeers(std::move(recentPeers))))
, _emptyRecent(_chatsContent->add(setupEmptyRecent()))
, _channelsScroll(std::make_unique<Ui::ElasticScroll>(this))
, _channelsContent(
	_channelsScroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this)))
, _myChannels(_channelsContent->add(setupMyChannels()))
, _recommendations(_channelsContent->add(setupRecommendations()))
, _emptyChannels(_channelsContent->add(setupEmptyChannels())) {

	setupTabs();
	setupChats();
	setupChannels();
}

Suggestions::~Suggestions() = default;

void Suggestions::setupTabs() {
	const auto shadow = Ui::CreateChild<Ui::PlainShadow>(this);
	shadow->lower();

	_tabs->sizeValue() | rpl::start_with_next([=](QSize size) {
		const auto line = st::lineWidth;
		shadow->setGeometry(0, size.height() - line, size.width(), line);
	}, shadow->lifetime());

	shadow->showOn(_tabs->shownValue());

	_tabs->setSections({
		tr::lng_recent_chats(tr::now),
		tr::lng_recent_channels(tr::now),
	});
	_tabs->sectionActivated(
	) | rpl::start_with_next([=](int section) {
		switchTab(section ? Tab::Channels : Tab::Chats);
	}, _tabs->lifetime());
}

void Suggestions::setupChats() {
	_recentCount.value() | rpl::start_with_next([=](int count) {
		_recentPeers->toggle(count > 0, anim::type::instant);
		_emptyRecent->toggle(count == 0, anim::type::instant);
	}, _recentPeers->lifetime());

	_topPeers->emptyValue() | rpl::start_with_next([=](bool empty) {
		_topPeersWrap->toggle(!empty, anim::type::instant);
	}, _topPeers->lifetime());

	_topPeers->clicks() | rpl::start_with_next([=](uint64 peerIdRaw) {
		const auto peerId = PeerId(peerIdRaw);
		_topPeerChosen.fire(_controller->session().data().peer(peerId));
	}, _topPeers->lifetime());

	_topPeers->showMenuRequests(
	) | rpl::start_with_next([=](const ShowTopPeerMenuRequest &request) {
		const auto weak = Ui::MakeWeak(this);
		const auto owner = &_controller->session().data();
		const auto peer = owner->peer(PeerId(request.id));
		const auto removeOne = [=] {
			peer->session().topPeers().remove(peer);
			if (weak) {
				_topPeers->removeLocally(peer->id.value);
			}
		};
		const auto session = &_controller->session();
		const auto removeAll = crl::guard(session, [=] {
			session->topPeers().toggleDisabled(true);
			if (weak) {
				_topPeers->removeLocally();
			}
		});
		FillEntryMenu(request.callback, {
			.controller = _controller,
			.peer = peer,
			.removeOneText = tr::lng_recent_remove(tr::now),
			.removeOne = removeOne,
			.removeAllText = tr::lng_recent_hide_top(
				tr::now).replace('&', u"&&"_q),
			.removeAllConfirm = tr::lng_recent_hide_sure(tr::now),
			.removeAll = removeAll,
		});
	}, _topPeers->lifetime());

	_topPeers->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		_chatsScroll->scrollToY(request.ymin, request.ymax);
	}, _topPeers->lifetime());

	_chatsScroll->setVisible(_tab.current() == Tab::Chats);
}

void Suggestions::setupChannels() {
	_myChannelsCount.value() | rpl::start_with_next([=](int count) {
		_myChannels->toggle(count > 0, anim::type::instant);
	}, _myChannels->lifetime());

	_recommendationsCount.value() | rpl::start_with_next([=](int count) {
		_recommendations->toggle(count > 0, anim::type::instant);
	}, _recommendations->lifetime());

	_emptyChannels->toggleOn(
		rpl::combine(
			_myChannelsCount.value(),
			_recommendationsCount.value(),
			rpl::mappers::_1 + rpl::mappers::_2 == 0),
		anim::type::instant);

	_channelsScroll->setVisible(_tab.current() == Tab::Channels);
}

void Suggestions::selectJump(Qt::Key direction, int pageSize) {
	if (_tab.current() == Tab::Chats) {
		selectJumpChats(direction, pageSize);
	} else {
		selectJumpChannels(direction, pageSize);
	}
}

void Suggestions::selectJumpChats(Qt::Key direction, int pageSize) {
	const auto recentHasSelection = [=] {
		return _recentSelectJump({}, 0) == JumpResult::Applied;
	};
	if (pageSize) {
		if (direction == Qt::Key_Down || direction == Qt::Key_Up) {
			_topPeers->deselectByKeyboard();
			if (!recentHasSelection()) {
				if (direction == Qt::Key_Down) {
					_recentSelectJump(direction, 0);
				} else {
					return;
				}
			}
			if (_recentSelectJump(direction, pageSize) == JumpResult::AppliedAndOut) {
				if (direction == Qt::Key_Up) {
					_chatsScroll->scrollTo(0);
				}
			}
		}
	} else if (direction == Qt::Key_Up) {
		if (_recentSelectJump(direction, pageSize)
			== JumpResult::AppliedAndOut) {
			_topPeers->selectByKeyboard(direction);
		} else if (_topPeers->selectedByKeyboard()) {
			_topPeers->selectByKeyboard(direction);
		}
	} else if (direction == Qt::Key_Down) {
		if (!_topPeersWrap->toggled() || recentHasSelection()) {
			_recentSelectJump(direction, pageSize);
		} else if (_topPeers->selectedByKeyboard()) {
			if (!_topPeers->selectByKeyboard(direction)
				&& _recentCount.current() > 0) {
				_topPeers->deselectByKeyboard();
				_recentSelectJump(direction, pageSize);
			}
		} else {
			_topPeers->selectByKeyboard({});
			_chatsScroll->scrollTo(0);
		}
	} else if (direction == Qt::Key_Left || direction == Qt::Key_Right) {
		if (!recentHasSelection()) {
			_topPeers->selectByKeyboard(direction);
		}
	}
}

void Suggestions::selectJumpChannels(Qt::Key direction, int pageSize) {
	const auto myChannelsHasSelection = [=] {
		return _myChannelsSelectJump({}, 0) == JumpResult::Applied;
	};
	const auto recommendationsHasSelection = [=] {
		return _recommendationsSelectJump({}, 0) == JumpResult::Applied;
	};
	if (pageSize) {
		if (direction == Qt::Key_Down) {
			if (recommendationsHasSelection()) {
				_recommendationsSelectJump(direction, pageSize);
			} else if (myChannelsHasSelection()) {
				if (_myChannelsSelectJump(direction, pageSize)
					== JumpResult::AppliedAndOut) {
					_recommendationsSelectJump(direction, 0);
				}
			} else if (_myChannelsCount.current()) {
				_myChannelsSelectJump(direction, 0);
				_myChannelsSelectJump(direction, pageSize);
			} else if (_recommendationsCount.current()) {
				_recommendationsSelectJump(direction, 0);
				_recommendationsSelectJump(direction, pageSize);
			}
		} else if (direction == Qt::Key_Up) {
			if (myChannelsHasSelection()) {
				if (_myChannelsSelectJump(direction, pageSize)
					== JumpResult::AppliedAndOut) {
					_channelsScroll->scrollTo(0);
				}
			} else if (recommendationsHasSelection()) {
				if (_recommendationsSelectJump(direction, pageSize)
					== JumpResult::AppliedAndOut) {
					_myChannelsSelectJump(direction, -1);
				}
			}
		}
	} else if (direction == Qt::Key_Up) {
		if (myChannelsHasSelection()) {
			_myChannelsSelectJump(direction, 0);
		} else if (_recommendationsSelectJump(direction, 0)
			== JumpResult::AppliedAndOut) {
			_myChannelsSelectJump(direction, -1);
		} else if (!recommendationsHasSelection()) {
			if (_myChannelsSelectJump(direction, 0)
				== JumpResult::AppliedAndOut) {
				_channelsScroll->scrollTo(0);
			}
		}
	} else if (direction == Qt::Key_Down) {
		if (recommendationsHasSelection()) {
			_recommendationsSelectJump(direction, 0);
		} else if (_myChannelsSelectJump(direction, 0)
			== JumpResult::AppliedAndOut) {
			_recommendationsSelectJump(direction, 0);
		} else if (!myChannelsHasSelection()) {
			if (_recommendationsSelectJump(direction, 0)
				== JumpResult::AppliedAndOut) {
				_myChannelsSelectJump(direction, 0);
			}
		}
	}
}

void Suggestions::chooseRow() {
	if (!_topPeers->chooseRow()) {
		_recentPeersChoose();
	}
}

void Suggestions::show(anim::type animated, Fn<void()> finish) {
	RpWidget::show();

	_hidden = false;
	if (animated == anim::type::instant) {
		finishShow();
	} else {
		startShownAnimation(true, std::move(finish));
	}
}

void Suggestions::hide(anim::type animated, Fn<void()> finish) {
	_hidden = true;
	if (isHidden()) {
		return;
	} else if (animated == anim::type::instant) {
		RpWidget::hide();
	} else {
		startShownAnimation(false, std::move(finish));
	}
}

void Suggestions::switchTab(Tab tab) {
	if (_tab.current() == tab) {
		return;
	}
	_tab = tab;
	_persist = false;
	if (_tabs->isHidden()) {
		return;
	}
	startSlideAnimation();
}

void Suggestions::startSlideAnimation() {
	if (!_slideAnimation.animating()) {
		_slideLeft = Ui::GrabWidget(_chatsScroll.get());
		_slideRight = Ui::GrabWidget(_channelsScroll.get());
		_chatsScroll->hide();
		_channelsScroll->hide();
	}
	const auto channels = (_tab.current() == Tab::Channels);
	const auto from = channels ? 0. : 1.;
	const auto to = channels ? 1. : 0.;
	_slideAnimation.start([=] {
		update();
		if (!_slideAnimation.animating() && !_shownAnimation.animating()) {
			finishShow();
		}
	}, from, to, st::slideDuration, anim::sineInOut);
}

void Suggestions::startShownAnimation(bool shown, Fn<void()> finish) {
	const auto from = shown ? 0. : 1.;
	const auto to = shown ? 1. : 0.;
	_shownAnimation.start([=] {
		update();
		if (!_shownAnimation.animating() && finish) {
			finish();
			if (shown) {
				finishShow();
			}
		}
	}, from, to, st::slideDuration, anim::easeOutQuint);
	if (_cache.isNull()) {
		const auto now = width();
		if (now < st::columnMinimalWidthLeft) {
			resize(st::columnMinimalWidthLeft, height());
		}
		_cache = Ui::GrabWidget(this);
		if (now < st::columnMinimalWidthLeft) {
			resize(now, height());
		}
	}
	_tabs->hide();
	_chatsScroll->hide();
	_channelsScroll->hide();
	_slideAnimation.stop();
}

void Suggestions::finishShow() {
	_slideAnimation.stop();
	_slideLeft = _slideRight = QPixmap();

	_shownAnimation.stop();
	_cache = QPixmap();

	_tabs->show();
	const auto channels = (_tab.current() == Tab::Channels);
	_chatsScroll->setVisible(!channels);
	_channelsScroll->setVisible(channels);
}

float64 Suggestions::shownOpacity() const {
	return _shownAnimation.value(_hidden ? 0. : 1.);
}

void Suggestions::paintEvent(QPaintEvent *e) {
	const auto opacity = shownOpacity();
	auto color = st::windowBg->c;
	color.setAlphaF(color.alphaF() * opacity);

	auto p = QPainter(this);
	p.fillRect(e->rect(), color);
	if (!_cache.isNull()) {
		const auto slide = st::topPeers.height + st::searchedBarHeight;
		p.setOpacity(opacity);
		p.drawPixmap(0, (opacity - 1.) * slide, _cache);
	} else if (!_slideLeft.isNull()) {
		const auto slide = st::topPeers.height + st::searchedBarHeight;
		const auto right = (_tab.current() == Tab::Channels);
		const auto progress = _slideAnimation.value(right ? 1. : 0.);
		p.setOpacity(1. - progress);
		p.drawPixmap(
			anim::interpolate(0, -slide, progress),
			_chatsScroll->y(),
			_slideLeft);
		p.setOpacity(progress);
		p.drawPixmap(
			anim::interpolate(slide, 0, progress),
			_channelsScroll->y(),
			_slideRight);
	}
}

void Suggestions::resizeEvent(QResizeEvent *e) {
	const auto w = std::max(width(), st::columnMinimalWidthLeft);
	_tabs->resizeToWidth(w);
	const auto tabs = _tabs->height();

	_chatsScroll->setGeometry(0, tabs, w, height() - tabs);
	_chatsContent->resizeToWidth(w);

	_channelsScroll->setGeometry(0, tabs, w, height() - tabs);
	_channelsContent->resizeToWidth(w);
}

object_ptr<Ui::SlideWrap<>> Suggestions::setupRecentPeers(
		RecentPeersList recentPeers) {
	auto &lifetime = _chatsContent->lifetime();
	const auto delegate = lifetime.make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = lifetime.make_state<RecentsController>(
		_controller,
		std::move(recentPeers));
	controller->setStyleOverrides(&st::recentPeersList);

	_recentCount = controller->count();

	controller->chosen(
	) | rpl::start_with_next([=](not_null<PeerData*> peer) {
		_controller->session().recentPeers().bump(peer);
		_recentPeerChosen.fire_copy(peer);
	}, lifetime);

	auto content = object_ptr<PeerListContent>(_chatsContent, controller);

	const auto raw = content.data();
	_recentPeersChoose = [=] {
		return raw->submitted();
	};
	_recentSelectJump = [raw](Qt::Key direction, int pageSize) {
		const auto had = raw->hasSelection();
		if (direction == Qt::Key()) {
			return had ? JumpResult::Applied : JumpResult::NotApplied;
		} else if (direction == Qt::Key_Up && !had) {
			return JumpResult::NotApplied;
		} else if (direction == Qt::Key_Down || direction == Qt::Key_Up) {
			const auto delta = (direction == Qt::Key_Down) ? 1 : -1;
			if (pageSize > 0) {
				raw->selectSkipPage(pageSize, delta);
			} else {
				raw->selectSkip(delta);
			}
			return raw->hasSelection()
				? JumpResult::Applied
				: had
				? JumpResult::AppliedAndOut
				: JumpResult::NotApplied;
		}
		return JumpResult::NotApplied;
	};
	raw->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		const auto add = _topPeersWrap->toggled() ? _topPeers->height() : 0;
		_chatsScroll->scrollToY(request.ymin + add, request.ymax + add);
	}, lifetime);

	delegate->setContent(raw);
	controller->setDelegate(delegate);

	return object_ptr<Ui::SlideWrap<>>(this, std::move(content));
}

object_ptr<Ui::SlideWrap<>> Suggestions::setupEmptyRecent() {
	return setupEmpty(_chatsContent, "search", tr::lng_recent_none());
}

object_ptr<Ui::SlideWrap<>> Suggestions::setupEmptyChannels() {
	return setupEmpty(
		_channelsContent,
		"noresults",
		tr::lng_channels_none_about());
}

object_ptr<Ui::SlideWrap<>> Suggestions::setupEmpty(
		not_null<QWidget*> parent,
		const QString &animation,
		rpl::producer<QString> text) {
	auto content = object_ptr<Ui::RpWidget>(parent);
	const auto raw = content.data();

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		std::move(text),
		st::defaultPeerListAbout);
	const auto size = st::recentPeersEmptySize;
	const auto [widget, animate] = Settings::CreateLottieIcon(
		raw,
		{
			.name = animation,
			.sizeOverride = { size, size },
		},
		st::recentPeersEmptyMargin);
	const auto icon = widget.data();

	rpl::combine(
		_chatsScroll->heightValue(),
		_topPeersWrap->heightValue()
	) | rpl::start_with_next([=](int height, int top) {
		raw->resize(
			raw->width(),
			std::max(height - top, st::recentPeersEmptyHeightMin));
	}, raw->lifetime());

	raw->sizeValue() | rpl::start_with_next([=](QSize size) {
		const auto x = (size.width() - icon->width()) / 2;
		const auto y = (size.height() - icon->height()) / 3;
		icon->move(x, y);
		label->move(
			(size.width() - label->width()) / 2,
			y + icon->height() + st::recentPeersEmptySkip);
	}, raw->lifetime());

	auto result = object_ptr<Ui::SlideWrap<>>(
		parent,
		std::move(content));
	result->toggle(false, anim::type::instant);

	result->toggledValue() | rpl::filter([=](bool shown) {
		return shown && _controller->session().data().chatsListLoaded();
	}) | rpl::start_with_next([=] {
		animate(anim::repeat::once);
	}, raw->lifetime());

	return result;
}

object_ptr<Ui::SlideWrap<>> Suggestions::setupMyChannels() {
	auto &lifetime = _channelsContent->lifetime();
	const auto delegate = lifetime.make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = lifetime.make_state<MyChannelsController>(
		_controller);
	controller->setStyleOverrides(&st::recentPeersList);

	_myChannelsCount = controller->count();

	controller->chosen(
	) | rpl::start_with_next([=](not_null<PeerData*> peer) {
		_persist = false;
		_myChannelChosen.fire_copy(peer);
	}, lifetime);

	auto content = object_ptr<PeerListContent>(_channelsContent, controller);

	const auto raw = content.data();
	_myChannelsChoose = [=] {
		return raw->submitted();
	};
	_myChannelsSelectJump = [=](Qt::Key direction, int pageSize) {
		const auto had = raw->hasSelection();
		if (direction == Qt::Key()) {
			return had ? JumpResult::Applied : JumpResult::NotApplied;
		} else if (direction == Qt::Key_Up && !had) {
			if (pageSize < 0) {
				raw->selectLast();
				return raw->hasSelection()
					? JumpResult::Applied
					: JumpResult::NotApplied;
			}
			return JumpResult::NotApplied;
		} else if (direction == Qt::Key_Down || direction == Qt::Key_Up) {
			const auto was = raw->selectedIndex();
			const auto delta = (direction == Qt::Key_Down) ? 1 : -1;
			if (pageSize > 0) {
				raw->selectSkipPage(pageSize, delta);
			} else {
				raw->selectSkip(delta);
			}
			if (had
				&& delta > 0
				&& _recommendationsCount.current()
				&& raw->selectedIndex() == was) {
				raw->clearSelection();
				return JumpResult::AppliedAndOut;
			}
			return raw->hasSelection()
				? JumpResult::Applied
				: had
				? JumpResult::AppliedAndOut
				: JumpResult::NotApplied;
		}
		return JumpResult::NotApplied;
	};
	raw->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		_channelsScroll->scrollToY(request.ymin, request.ymax);
	}, lifetime);

	delegate->setContent(raw);
	controller->setDelegate(delegate);

	return object_ptr<Ui::SlideWrap<>>(this, std::move(content));
}

object_ptr<Ui::SlideWrap<>> Suggestions::setupRecommendations() {
	auto &lifetime = _channelsContent->lifetime();
	const auto delegate = lifetime.make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = lifetime.make_state<RecommendationsController>(
		_controller);
	controller->setStyleOverrides(&st::recentPeersList);

	_recommendationsCount = controller->count();

	_tab.value() | rpl::filter(
		rpl::mappers::_1 == Tab::Channels
	) | rpl::start_with_next([=] {
		controller->load();
	}, lifetime);

	controller->chosen(
	) | rpl::start_with_next([=](not_null<PeerData*> peer) {
		_persist = true;
		_recommendationChosen.fire_copy(peer);
	}, lifetime);

	auto content = object_ptr<PeerListContent>(_channelsContent, controller);

	const auto raw = content.data();
	_recommendationsChoose = [=] {
		return raw->submitted();
	};
	_recommendationsSelectJump = [raw](Qt::Key direction, int pageSize) {
		const auto had = raw->hasSelection();
		if (direction == Qt::Key()) {
			return had ? JumpResult::Applied : JumpResult::NotApplied;
		} else if (direction == Qt::Key_Up && !had) {
			return JumpResult::NotApplied;
		} else if (direction == Qt::Key_Down || direction == Qt::Key_Up) {
			const auto delta = (direction == Qt::Key_Down) ? 1 : -1;
			if (pageSize > 0) {
				raw->selectSkipPage(pageSize, delta);
			} else {
				raw->selectSkip(delta);
			}
			return raw->hasSelection()
				? JumpResult::Applied
				: had
				? JumpResult::AppliedAndOut
				: JumpResult::NotApplied;
		}
		return JumpResult::NotApplied;
	};
	raw->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		const auto add = _myChannels->toggled() ? _myChannels->height() : 0;
		_channelsScroll->scrollToY(request.ymin + add, request.ymax + add);
	}, lifetime);

	delegate->setContent(raw);
	controller->setDelegate(delegate);

	return object_ptr<Ui::SlideWrap<>>(this, std::move(content));
}

bool Suggestions::persist() const {
	return _persist;
}

void Suggestions::clearPersistance() {
	_persist = false;
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
			if (user->isInaccessible()) {
				continue;
			}
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

RecentPeersList RecentPeersContent(not_null<Main::Session*> session) {
	return RecentPeersList{ session->recentPeers().list() };
}

} // namespace Dialogs
