/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_suggestions.h"

#include "api/api_chat_participants.h"
#include "api/api_peer_search.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "base/qt/qt_key_modifiers.h"
#include "boxes/choose_filter_box.h"
#include "boxes/peer_list_box.h"
#include "core/application.h"
#include "core/ui_integration.h"
#include "data/components/recent_peers.h"
#include "data/components/top_peers.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_chat_filters.h"
#include "data/data_download_manager.h"
#include "data/data_folder.h"
#include "data/data_messages.h"
#include "data/data_peer_values.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/ui/chat_search_empty.h"
#include "dialogs/ui/chat_search_in.h"
#include "dialogs/ui/posts_search_intro.h"
#include "dialogs/dialogs_inner_widget.h"
#include "dialogs/dialogs_search_posts.h"
#include "history/history.h"
#include "info/downloads/info_downloads_widget.h"
#include "info/media/info_media_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/info_wrap_widget.h"
#include "inline_bots/bot_attach_web_view.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/settings_credits_graphics.h"
#include "settings/sections/settings_premium.h"
#include "storage/storage_shared_media.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/swipe_handler.h"
#include "ui/effects/loading_element.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/slide_animation.h"
#include "ui/toast/toast.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/custom_emoji_text_badge.h"
#include "ui/text/text_utilities.h"
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
#include "ui/search_field_controller.h"
#include "ui/unread_badge_paint.h"
#include "ui/ui_utility.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

namespace Dialogs {
namespace {

constexpr auto kCollapsedChannelsCount = 5;
constexpr auto kProbablyMaxChannels = 1000;
constexpr auto kCollapsedAppsCount = 5;
constexpr auto kProbablyMaxApps = 100;
constexpr auto kSearchQueryDelay = crl::time(900);
constexpr auto kSearchPerPage = 50;
constexpr auto kQueryPreviewLimit = 32;

[[nodiscard]] bool MatchesSearchWords(
		not_null<PeerData*> peer,
		const QStringList &words) {
	const auto &names = peer->nameWords();
	return ranges::all_of(words, [&](const QString &word) {
		return ranges::any_of(names, [&](const QString &name) {
			return name.startsWith(word);
		});
	});
}

[[nodiscard]] std::vector<not_null<PeerData*>> FilterPeers(
		const std::vector<not_null<PeerData*>> &list,
		Fn<bool(not_null<PeerData*>)> accept) {
	auto result = std::vector<not_null<PeerData*>>();
	for (const auto &peer : list) {
		if (accept(peer)) {
			result.push_back(peer);
		}
	}
	return result;
}

void UpdateVisibleRange(
		not_null<Ui::ElasticScroll*> scroll,
		not_null<InnerWidget*> content,
		int skipTop = 0) {
	const auto top = scroll->scrollTop() - skipTop;
	content->setVisibleTopBottom(top, top + scroll->height());
}

void DispatchResultsKey(
		not_null<InnerWidget*> content,
		Qt::Key direction,
		int pageSize) {
	const auto key = !pageSize
		? direction
		: (direction == Qt::Key_Down)
		? Qt::Key_PageDown
		: Qt::Key_PageUp;
	auto event = QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
	content->processKeyDispatch(&event);
}

[[nodiscard]] TextWithEntities NoResultsText(const QString &query) {
	const auto preview = (query.size() > kQueryPreviewLimit + 3)
		? (query.mid(0, kQueryPreviewLimit) + Ui::kQEllipsis)
		: query;
	auto result = tr::lng_search_tab_no_results(tr::now, tr::bold);
	result.append('\n').append(tr::lng_search_tab_no_results_text(
		tr::now,
		lt_query,
		preview));
	return result;
}

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
	void rightActionAddRipple(
		QPoint point,
		Fn<void()> updateCallback) override;
	void rightActionStopLastRipple() override;

	const style::PeerListItem &computeSt(
		const style::PeerListItem &st) const override;

private:
	const not_null<History*> _history;
	std::unique_ptr<Ui::Text::String> _mainAppText;
	std::unique_ptr<Ui::RippleAnimation> _actionRipple;
	QString _badgeString;
	QSize _badgeSize;
	uint32 _counter : 30 = 0;
	uint32 _unread : 1 = 0;
	uint32 _muted : 1 = 0;

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

struct EntryMenuDescriptor {
	not_null<Window::SessionController*> controller;
	not_null<PeerData*> peer;
	QString removeOneText;
	Fn<void()> removeOne;
	QString removeAllText;
	QString removeAllConfirm;
	Fn<void()> removeAll;
	Fn<void()> closeCallback;
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
	const auto channel = peer->asChannel();
	const auto community = channel && channel->isCommunity();
	const auto group = peer->isChat() || peer->isMegagroup();

	add(tr::lng_context_new_window(tr::now), [=] {
		Ui::PreventDelayedActivation();
		controller->showInNewWindow(peer);
		if (descriptor.closeCallback) {
			descriptor.closeCallback();
		}
	}, &st::menuIconNewWindow);
	Window::AddSeparatorAndShiftUp(add);

	const auto showHistoryText = community
		? tr::lng_context_open_community(tr::now)
		: group
		? tr::lng_context_open_group(tr::now)
		: channel
		? tr::lng_context_open_channel(tr::now)
		: tr::lng_profile_send_message(tr::now);
	const auto showHistoryIcon = community
		? &st::menuIconCommunity
		: group
		? &st::menuIconChatBubble
		: channel
		? &st::menuIconChannel
		: &st::menuIconChatBubble;
	add(showHistoryText, [=] {
		controller->showPeerHistory(peer);
	}, showHistoryIcon);

	const auto history = peer->owner().historyLoaded(peer);
	if (history
		&& history->owner().chatsFilters().has()
		&& history->inChatList()) {
		add(Ui::Menu::MenuCallback::Args{
			.text = tr::lng_filters_menu_add(tr::now),
			.handler = nullptr,
			.icon = &st::menuIconAddToFolder,
			.fillSubmenu = [&](not_null<Ui::PopupMenu*> menu) {
				FillChooseFilterMenu(controller, menu, history);
			},
			.submenuSt = &st::foldersMenu,
		});
	}
	const auto viewProfileText = community
		? tr::lng_context_view_community(tr::now)
		: group
		? tr::lng_context_view_group(tr::now)
		: channel
		? tr::lng_context_view_channel(tr::now)
		: tr::lng_context_view_profile(tr::now);
	add(viewProfileText, [=] {
		controller->showPeerInfo(peer);
	}, peer->isUser() ? &st::menuIconProfile : &st::menuIconInfo);

	add({ .separatorSt = &st::expandedMenuSeparator });

	add({
		.text = descriptor.removeOneText,
		.handler = descriptor.removeOne,
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	if (!descriptor.removeAllText.isEmpty()) {
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
}

RecentRow::RecentRow(not_null<PeerData*> peer)
: PeerListRow(peer)
, _history(peer->owner().history(peer))
, _mainAppText([&]() -> std::unique_ptr<Ui::Text::String> {
	if (const auto user = peer->asUser()) {
		if (user->botInfo && user->botInfo->hasMainApp) {
			return std::make_unique<Ui::Text::String>(
				st::dialogRowOpenBotRecent.button.style,
				tr::lng_profile_open_app_short(tr::now));
		}
	}
	return nullptr;
}()) {
	if (peer->isSelf() || peer->isRepliesChat() || peer->isVerifyCodes()) {
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
		if (!channel->isCommunity() && channel->membersCountKnown()) {
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
	if (_mainAppText && _badgeSize.isEmpty()) {
		return QSize(
			_mainAppText->maxWidth() + _mainAppText->minHeight(),
			st::dialogRowOpenBotRecent.button.height);
	}
	return _badgeSize;
}

QMargins RecentRow::rightActionMargins() const {
	if (_mainAppText && _badgeSize.isEmpty()) {
		const auto &st = st::dialogRowOpenBotRecent;
		auto margins = st.margin;
		margins.setTop((st::recentPeersItem.height - st.button.height) / 2);
		return margins;
	} else if (_badgeSize.isEmpty()) {
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
	if (_mainAppText && _badgeSize.isEmpty()) {
		const auto size = RecentRow::rightActionSize();
		p.setPen(Qt::NoPen);
		p.setBrush(actionSelected
			? st::activeButtonBgOver
			: st::activeButtonBg);
		const auto radius = size.height() / 2;
		auto hq = PainterHighQualityEnabler(p);
		p.drawRoundedRect(QRect(QPoint(x, y), size), radius, radius);
		if (_actionRipple) {
			_actionRipple->paint(p, x, y, outerWidth);
			if (_actionRipple->empty()) {
				_actionRipple.reset();
			}
		}
		p.setPen(actionSelected
			? st::activeButtonFgOver
			: st::activeButtonFg);
		const auto top = st::dialogRowOpenBotRecent.button.textTop;
		_mainAppText->draw(p, {
			.position = QPoint(x + size.height() / 2, y + top),
			.outerWidth = outerWidth,
			.availableWidth = outerWidth,
			.elisionLines = 1,
		});
	}
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
	return !_mainAppText || !_badgeSize.isEmpty();
}

void RecentRow::rightActionAddRipple(
		QPoint point,
		Fn<void()> updateCallback) {
	if (!_mainAppText || !_badgeSize.isEmpty()) {
		return;
	}
	if (!_actionRipple) {
		const auto size = rightActionSize();
		const auto radius = size.height() / 2;
		auto mask = Ui::RippleAnimation::RoundRectMask(size, radius);
		_actionRipple = std::make_unique<Ui::RippleAnimation>(
			st::defaultActiveButton.ripple,
			std::move(mask),
			std::move(updateCallback));
	}
	_actionRipple->add(point);
}

void RecentRow::rightActionStopLastRipple() {
	if (_actionRipple) {
		_actionRipple->lastStop();
	}
}

const style::PeerListItem &RecentRow::computeSt(
		const style::PeerListItem &st) const {
	return (peer()->isSelf()
		|| peer()->isRepliesChat()
		|| peer()->isVerifyCodes())
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

} // namespace


class Suggestions::ObjectListController
	: public PeerListController
	, public base::has_weak_ptr {
public:
	explicit ObjectListController(
		not_null<Window::SessionController*> window);

	[[nodiscard]] not_null<Window::SessionController*> window() const {
		return _window;
	}
	[[nodiscard]] rpl::producer<int> count() const {
		return _count.value();
	}
	[[nodiscard]] rpl::producer<not_null<PeerData*>> chosen() const {
		return _chosen.events();
	}

	void setCloseCallback(Fn<void()> callback) {
		_closeCallback = std::move(callback);
	}

	Main::Session &session() const override {
		return _window->session();
	}

	void rowClicked(not_null<PeerListRow*> row) override;
	void rowMiddleClicked(not_null<PeerListRow*> row) override;
	bool rowTrackPress(not_null<PeerListRow*> row) override;
	void rowTrackPressCancel() override;
	bool rowTrackPressSkipMouseSelection() override;

	bool processTouchEvent(not_null<QTouchEvent*> e);
	void setupTouchChatPreview(not_null<Ui::ElasticScroll*> scroll);

protected:
	[[nodiscard]] int countCurrent() const;
	void setCount(int count);

	[[nodiscard]] bool expandedCurrent() const;
	[[nodiscard]] rpl::producer<bool> expanded() const;
	void collapse();

	void setupActiveRows();
	[[nodiscard]] bool isActive(not_null<PeerData*> peer) const;

	void setupPlainDivider(rpl::producer<QString> title);
	void setupExpandDivider(rpl::producer<QString> title);

	void setupQueryFilter(
		rpl::producer<QString> query,
		rpl::producer<std::vector<not_null<PeerData*>>> found,
		Fn<void()> refilter);
	[[nodiscard]] const QStringList &words() const;
	[[nodiscard]] const std::vector<not_null<PeerData*>> &found() const;
	void removeFound(not_null<PeerData*> peer);

	Fn<void()> _closeCallback;

private:
	const not_null<Window::SessionController*> _window;
	QStringList _words;
	std::vector<not_null<PeerData*>> _found;
	rpl::lifetime _filterLifetime;

	std::optional<QPoint> _chatPreviewTouchGlobal;
	rpl::event_stream<> _touchCancelRequests;
	rpl::event_stream<not_null<PeerData*>> _chosen;
	rpl::variable<int> _count;
	rpl::variable<Ui::RpWidget*> _toggleExpanded = nullptr;
	rpl::variable<bool> _expanded = false;
	History *_activeHistory = nullptr;

};

class RecentsController final : public Suggestions::ObjectListController {
public:
	using RightActionCallback = Fn<void(not_null<PeerData*>)>;

	RecentsController(
		not_null<Window::SessionController*> window,
		RecentPeersList list,
		RightActionCallback rightActionCallback,
		Fn<void()> closeCallback);

	void prepare() override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;

	QString savedMessagesChatStatus() const override;

private:
	void setupDivider();
	void subscribeToEvents();
	[[nodiscard]] Fn<void()> removeAllCallback();

	RecentPeersList _recent;
	RightActionCallback _rightActionCallback;
	rpl::lifetime _lifetime;

};

class MyChannelsController final
	: public Suggestions::ObjectListController {
public:
	MyChannelsController(
		not_null<Window::SessionController*> window,
		rpl::producer<QString> query,
		rpl::producer<std::vector<not_null<PeerData*>>> found);

	void prepare() override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

private:
	void appendRow(not_null<ChannelData*> channel);
	void fill(bool force = false);
	void refilter();
	[[nodiscard]] const std::vector<not_null<History*>> &shown() const;

	rpl::producer<QString> _query;
	rpl::producer<std::vector<not_null<PeerData*>>> _foundValue;
	std::vector<not_null<History*>> _channels;
	std::vector<not_null<History*>> _filtered;
	rpl::lifetime _lifetime;

};

class GlobalPeersController final
	: public Suggestions::ObjectListController {
public:
	GlobalPeersController(
		not_null<Window::SessionController*> window,
		rpl::producer<std::vector<not_null<PeerData*>>> peers,
		bool expandable);

	void prepare() override;

private:
	void fill();
	void appendRow(not_null<PeerData*> peer);

	rpl::producer<std::vector<not_null<PeerData*>>> _results;
	std::vector<not_null<PeerData*>> _peers;
	const bool _expandable = false;
	rpl::lifetime _lifetime;

};

class RecommendationsController final
	: public Suggestions::ObjectListController {
public:
	explicit RecommendationsController(
		not_null<Window::SessionController*> window);

	void prepare() override;

	void load();

private:
	void fill();
	void appendRow(not_null<ChannelData*> channel);

	bool _requested = false;
	rpl::lifetime _lifetime;

};

class RecentAppsController final
	: public Suggestions::ObjectListController {
public:
	explicit RecentAppsController(
		not_null<Window::SessionController*> window);

	void prepare() override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

	void load();

	[[nodiscard]] rpl::producer<> refreshed() const;
	[[nodiscard]] bool shown(not_null<PeerData*> peer) const;

private:
	void appendRow(not_null<UserData*> bot);
	void fill();
	[[nodiscard]] const std::vector<not_null<UserData*>> &bots() const;

	std::vector<not_null<UserData*>> _bots;
	rpl::event_stream<> _refreshed;
	rpl::lifetime _lifetime;

};

class PopularAppsController final
	: public Suggestions::ObjectListController {
public:
	PopularAppsController(
		not_null<Window::SessionController*> window,
		Fn<bool(not_null<PeerData*>)> filterOut,
		rpl::producer<> filterOutRefreshes);

	void prepare() override;

	void load();

private:
	void fill();
	void appendRow(not_null<UserData*> bot);

	Fn<bool(not_null<PeerData*>)> _filterOut;
	rpl::producer<> _filterOutRefreshes;
	bool _requested = false;
	rpl::lifetime _lifetime;

};

Suggestions::ObjectListController::ObjectListController(
	not_null<Window::SessionController*> window)
: _window(window) {
}

bool Suggestions::ObjectListController::rowTrackPress(
		not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	const auto history = peer->owner().history(peer);
	const auto callback = crl::guard(this, [=](bool shown) {
		delegate()->peerListPressLeftToContextMenu(shown);
	});
	if (base::IsAltPressed()) {
		_window->showChatPreview(
			{ history, FullMsgId() },
			callback,
			nullptr,
			_chatPreviewTouchGlobal);
		return false;
	}
	const auto point = delegate()->peerListLastRowMousePosition();
	const auto &st = computeListSt().item;
	if (point && point->x() < st.photoPosition.x() + st.photoSize) {
		_window->scheduleChatPreview(
			{ history, FullMsgId() },
			callback,
			nullptr,
			_chatPreviewTouchGlobal);
		return true;
	}
	return false;
}

void Suggestions::ObjectListController::rowTrackPressCancel() {
	_chatPreviewTouchGlobal = {};
	_window->cancelScheduledPreview();
}

bool Suggestions::ObjectListController::rowTrackPressSkipMouseSelection() {
	return _chatPreviewTouchGlobal.has_value();
}

bool Suggestions::ObjectListController::processTouchEvent(
		not_null<QTouchEvent*> e) {
	const auto point = e->touchPoints().empty()
		? std::optional<QPoint>()
		: e->touchPoints().front().screenPos().toPoint();
	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (!point) {
			return false;
		}
		_chatPreviewTouchGlobal = point;
		if (!delegate()->peerListTrackRowPressFromGlobal(*point)) {
			_chatPreviewTouchGlobal = {};
		}
	} break;

	case QEvent::TouchUpdate: {
		if (!point) {
			return false;
		}
		if (_chatPreviewTouchGlobal) {
			const auto delta = (*_chatPreviewTouchGlobal - *point);
			if (delta.manhattanLength() > computeListSt().item.photoSize) {
				rowTrackPressCancel();
			}
		}
	} break;

	case QEvent::TouchEnd:
	case QEvent::TouchCancel: {
		if (_chatPreviewTouchGlobal) {
			rowTrackPressCancel();
		}
	} break;
	}
	return false;
}

void Suggestions::ObjectListController::setupTouchChatPreview(
		not_null<Ui::ElasticScroll*> scroll) {
	_touchCancelRequests.events() | rpl::on_next([=] {
		QTouchEvent ev(QEvent::TouchCancel);
		ev.setTimestamp(crl::now());
		QGuiApplication::sendEvent(scroll, &ev);
	}, lifetime());
}

int Suggestions::ObjectListController::countCurrent() const {
	return _count.current();
}

void Suggestions::ObjectListController::setCount(int count) {
	_count = count;
}

bool Suggestions::ObjectListController::expandedCurrent() const {
	return _expanded.current();
}

rpl::producer<bool> Suggestions::ObjectListController::expanded() const {
	return _expanded.value();
}

void Suggestions::ObjectListController::collapse() {
	_expanded = false;
}

void Suggestions::ObjectListController::setupQueryFilter(
		rpl::producer<QString> query,
		rpl::producer<std::vector<not_null<PeerData*>>> found,
		Fn<void()> refilter) {
	std::move(query) | rpl::on_next([=](const QString &query) {
		auto words = TextUtilities::PrepareSearchWords(query);
		if (_words != words) {
			_words = std::move(words);
			refilter();
			collapse();
		}
	}, _filterLifetime);

	std::move(
		found
	) | rpl::on_next([=](std::vector<not_null<PeerData*>> &&peers) {
		_found = std::move(peers);
		if (!_words.isEmpty()) {
			refilter();
		}
	}, _filterLifetime);
}

const QStringList &Suggestions::ObjectListController::words() const {
	return _words;
}

auto Suggestions::ObjectListController::found() const
-> const std::vector<not_null<PeerData*>> & {
	return _found;
}

void Suggestions::ObjectListController::removeFound(
		not_null<PeerData*> peer) {
	_found.erase(ranges::remove(_found, peer), end(_found));
}

void Suggestions::ObjectListController::setupActiveRows() {
	_window->activeChatValue(
	) | rpl::on_next([=](const Dialogs::Key &key) {
		const auto history = key.history();
		if (_activeHistory == history) {
			return;
		}
		const auto toggle = [&](History *history, bool active) {
			if (!history) {
				return;
			}
			const auto id = history->peer->id.value;
			if (const auto row = delegate()->peerListFindRow(id)) {
				static_cast<ChannelRow*>(row)->setActive(active);
				delegate()->peerListUpdateRow(row);
			}
		};
		toggle(std::exchange(_activeHistory, history), false);
		toggle(_activeHistory, true);
	}, lifetime());
}

bool Suggestions::ObjectListController::isActive(
		not_null<PeerData*> peer) const {
	return _activeHistory && (_activeHistory->peer == peer);
}

void Suggestions::ObjectListController::rowClicked(
		not_null<PeerListRow*> row) {
	_chosen.fire(row->peer());
}

void Suggestions::ObjectListController::rowMiddleClicked(
		not_null<PeerListRow*> row) {
	window()->showInNewWindow(row->peer());
	if (_closeCallback) {
		_closeCallback();
	}
}

void Suggestions::ObjectListController::setupPlainDivider(
		rpl::producer<QString> title) {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		(QWidget*)nullptr,
		st::searchedBarHeight);
	const auto raw = result.data();
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		std::move(title),
		st::searchedBarLabel);
	raw->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto x = st::searchedBarPosition.x();
		const auto y = st::searchedBarPosition.y();
		label->resizeToWidth(size.width() - x * 2);
		label->moveToLeft(x, y, size.width());
	}, raw->lifetime());
	raw->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(raw).fillRect(clip, st::searchedBarBg);
	}, raw->lifetime());

	delegate()->peerListSetAboveWidget(std::move(result));
}

void Suggestions::ObjectListController::setupExpandDivider(
		rpl::producer<QString> title) {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		(QWidget*)nullptr,
		st::searchedBarHeight);
	const auto raw = result.data();
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		std::move(title),
		st::searchedBarLabel);
	count(
	) | rpl::map(
		rpl::mappers::_1 > kCollapsedChannelsCount
	) | rpl::distinct_until_changed() | rpl::on_next([=](bool more) {
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
			_expanded = !_expanded.current();
		});
		_expanded.value() | rpl::on_next([=](bool expanded) {
			toggle->setText(expanded
				? tr::lng_channels_your_less(tr::now)
				: tr::lng_channels_your_more(tr::now));
		}, toggle->lifetime());
		rpl::combine(
			raw->sizeValue(),
			toggle->widthValue()
		) | rpl::on_next([=](QSize size, int width) {
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
	) | rpl::on_next([=](QSize size, const auto) {
		const auto x = st::searchedBarPosition.x();
		const auto y = st::searchedBarPosition.y();
		label->resizeToWidth(size.width() - x * 2);
		label->moveToLeft(x, y, size.width());
	}, raw->lifetime());

	raw->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(raw).fillRect(clip, st::searchedBarBg);
	}, raw->lifetime());

	delegate()->peerListSetAboveWidget(std::move(result));
}

RecentsController::RecentsController(
	not_null<Window::SessionController*> window,
	RecentPeersList list,
	RightActionCallback rightActionCallback,
	Fn<void()> closeCallback)
: ObjectListController(window)
, _recent(std::move(list))
, _rightActionCallback(std::move(rightActionCallback)) {
	_closeCallback = std::move(closeCallback);
}

void RecentsController::prepare() {
	setupDivider();

	for (const auto &peer : _recent.list) {
		delegate()->peerListAppendRow(std::make_unique<RecentRow>(peer));
	}
	delegate()->peerListRefreshRows();
	setCount(_recent.list.size());

	subscribeToEvents();
}

Fn<void()> RecentsController::removeAllCallback() {
	const auto weak = base::make_weak(this);
	const auto session = &this->session();
	return crl::guard(session, [=] {
		if (weak) {
			setCount(0);
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
	const auto session = &this->session();
	const auto removeOne = crl::guard(session, [=] {
		if (weak) {
			const auto rowId = peer->id.value;
			if (const auto row = delegate()->peerListFindRow(rowId)) {
				setCount(std::max(0, countCurrent() - 1));
				delegate()->peerListRemoveRow(row);
				delegate()->peerListRefreshRows();
			}
		}
		session->recentPeers().remove(peer);
	});
	FillEntryMenu(Ui::Menu::CreateAddActionCallback(result), {
		.controller = window(),
		.peer = peer,
		.removeOneText = tr::lng_recent_remove(tr::now),
		.removeOne = removeOne,
		.removeAllText = tr::lng_recent_clear_all(tr::now),
		.removeAllConfirm = tr::lng_recent_clear_sure(tr::now),
		.removeAll = removeAllCallback(),
		.closeCallback = crl::guard(this, [=] {
			if (_closeCallback) {
				_closeCallback();
			}
		}),
	});
	return result;
}

void RecentsController::rowRightActionClicked(not_null<PeerListRow*> row) {
	if (_rightActionCallback) {
		if (const auto peer = row->peer()) {
			_rightActionCallback(peer);
		}
	}
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
		window(),
		tr::lng_recent_clear_sure(tr::now),
		removeAllCallback()));
	rpl::combine(
		raw->sizeValue(),
		clear->widthValue()
	) | rpl::on_next([=](QSize size, int width) {
		const auto x = st::searchedBarPosition.x();
		const auto y = st::searchedBarPosition.y();
		clear->moveToRight(0, 0, size.width());
		label->resizeToWidth(size.width() - x - width);
		label->moveToLeft(x, y, size.width());
	}, raw->lifetime());
	raw->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(raw).fillRect(clip, st::searchedBarBg);
	}, raw->lifetime());

	delegate()->peerListSetAboveWidget(std::move(result));
}

void RecentsController::subscribeToEvents() {
	using Flag = Data::PeerUpdate::Flag;
	session().changes().peerUpdates(
		Flag::Notifications
		| Flag::OnlineStatus
	) | rpl::on_next([=](const Data::PeerUpdate &update) {
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
		if (!peer->isRepliesChat()
			&& !peer->isVerifyCodes()
			&& (update.flags & Flag::OnlineStatus)) {
			row->clearCustomStatus();
			refreshed = true;
		}
		if (refreshed) {
			delegate()->peerListUpdateRow(row);
		}
	}, _lifetime);

	session().data().unreadBadgeChanges(
	) | rpl::on_next([=] {
		for (auto i = 0; i != countCurrent(); ++i) {
			const auto row = delegate()->peerListRowAt(i);
			if (static_cast<RecentRow*>(row.get())->refreshBadge()) {
				delegate()->peerListUpdateRow(row);
			}
		}
	}, _lifetime);
}

MyChannelsController::MyChannelsController(
	not_null<Window::SessionController*> window,
	rpl::producer<QString> query,
	rpl::producer<std::vector<not_null<PeerData*>>> found)
: ObjectListController(window)
, _query(std::move(query))
, _foundValue(std::move(found)) {
}

void MyChannelsController::prepare() {
	setupExpandDivider(tr::lng_channels_your_title());

	session().changes().peerUpdates(
		Data::PeerUpdate::Flag::ChannelAmIn
	) | rpl::on_next([=](const Data::PeerUpdate &update) {
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
		_filtered.erase(ranges::remove(_filtered, history), end(_filtered));
		removeFound(channel);
		const auto row = delegate()->peerListFindRow(channel->id.value);
		if (row) {
			delegate()->peerListRemoveRow(row);
		}
		setCount(shown().size());
		fill(true);
	}, _lifetime);

	_channels.reserve(kProbablyMaxChannels);
	const auto owner = &session().data();
	const auto add = [&](not_null<Dialogs::MainList*> list) {
		for (const auto &row : list->indexed()->all()) {
			if (const auto history = row->history()) {
				if (history->peer->isBroadcast()) {
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
	setCount(_channels.size());

	expanded() | rpl::on_next([=] {
		fill();
	}, _lifetime);

	auto loading = owner->chatsListChanges(
	) | rpl::take_while([=](Data::Folder *folder) {
		return !owner->chatsListLoaded(folder);
	});
	rpl::merge(
		std::move(loading),
		owner->chatsListLoadedEvents()
	) | rpl::on_next([=](Data::Folder *folder) {
		const auto list = owner->chatsList(folder);
		for (const auto &row : list->indexed()->all()) {
			if (const auto history = row->history()) {
				if (history->peer->isBroadcast()) {
					if (!ranges::contains(_channels, not_null(history))) {
						_channels.push_back(history);
					}
				}
			}
		}
		if (!words().isEmpty()) {
			refilter();
			return;
		}
		const auto was = countCurrent();
		const auto now = int(_channels.size());
		if (was != now) {
			setCount(now);
			fill();
		}
	}, _lifetime);

	setupQueryFilter(
		std::move(_query),
		std::move(_foundValue),
		[=] { refilter(); });
}

void MyChannelsController::refilter() {
	_filtered.clear();
	if (!words().isEmpty()) {
		for (const auto &history : _channels) {
			if (MatchesSearchWords(history->peer, words())) {
				_filtered.push_back(history);
			}
		}
		for (const auto &peer : found()) {
			const auto history = peer->owner().history(peer);
			if (!ranges::contains(_filtered, history)) {
				_filtered.push_back(history);
			}
		}
	}
	for (auto i = delegate()->peerListFullRowsCount(); i != 0;) {
		delegate()->peerListRemoveRow(delegate()->peerListRowAt(--i));
	}
	setCount(shown().size());
	fill(true);
}

const std::vector<not_null<History*>> &MyChannelsController::shown() const {
	return words().isEmpty() ? _channels : _filtered;
}

void MyChannelsController::fill(bool force) {
	const auto count = countCurrent();
	const auto limit = expandedCurrent()
		? count
		: std::min(count, kCollapsedChannelsCount);
	const auto already = delegate()->peerListFullRowsCount();
	const auto delta = limit - already;
	if (!delta && !force) {
		return;
	} else if (delta > 0) {
		for (auto i = already; i != limit; ++i) {
			appendRow(shown()[i]->peer->asBroadcast());
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

base::unique_qptr<Ui::PopupMenu> MyChannelsController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto peer = row->peer();
	const auto addAction = Ui::Menu::CreateAddActionCallback(result);
	Window::FillDialogsEntryMenu(
		window(),
		Dialogs::EntryState{
			.key = peer->owner().history(peer),
			.section = Dialogs::EntryState::Section::ContextMenu,
		},
		addAction);
	return result;
}

GlobalPeersController::GlobalPeersController(
	not_null<Window::SessionController*> window,
	rpl::producer<std::vector<not_null<PeerData*>>> peers,
	bool expandable)
: ObjectListController(window)
, _results(std::move(peers))
, _expandable(expandable) {
}

void GlobalPeersController::prepare() {
	if (_expandable) {
		setupExpandDivider(tr::lng_search_global_results());
	} else {
		setupPlainDivider(tr::lng_search_global_results());
	}
	setupActiveRows();

	expanded() | rpl::skip(1) | rpl::on_next([=] {
		fill();
	}, _lifetime);

	std::move(
		_results
	) | rpl::on_next([=](std::vector<not_null<PeerData*>> &&peers) {
		for (auto i = delegate()->peerListFullRowsCount(); i != 0;) {
			delegate()->peerListRemoveRow(delegate()->peerListRowAt(--i));
		}
		_peers = std::move(peers);
		setCount(_peers.size());
		collapse();
		fill();
	}, _lifetime);
}

void GlobalPeersController::fill() {
	const auto count = int(_peers.size());
	const auto limit = (_expandable && !expandedCurrent())
		? std::min(count, kCollapsedChannelsCount)
		: count;
	const auto already = delegate()->peerListFullRowsCount();
	for (auto i = already; i < limit; ++i) {
		appendRow(_peers[i]);
	}
	for (auto i = already; i > limit;) {
		delegate()->peerListRemoveRow(delegate()->peerListRowAt(--i));
	}
	delegate()->peerListRefreshRows();
}

void GlobalPeersController::appendRow(not_null<PeerData*> peer) {
	auto row = std::make_unique<ChannelRow>(peer);
	row->setActive(isActive(peer));
	auto status = QStringList();
	if (const auto username = peer->username(); !username.isEmpty()) {
		status.push_back('@' + username);
	}
	const auto channel = peer->asChannel();
	const auto user = peer->asUser();
	const auto info = user ? user->botInfo.get() : nullptr;
	if (channel && channel->membersCountKnown()) {
		status.push_back(tr::lng_chat_status_subscribers(
			tr::now,
			lt_count_decimal,
			channel->membersCount()));
	} else if (info && info->activeUsers) {
		status.push_back(tr::lng_bot_status_users(
			tr::now,
			lt_count_decimal,
			info->activeUsers));
	}
	if (!status.isEmpty()) {
		row->setCustomStatus(status.join(u", "_q));
	}
	delegate()->peerListAppendRow(std::move(row));
}

RecommendationsController::RecommendationsController(
	not_null<Window::SessionController*> window)
: ObjectListController(window) {
}

void RecommendationsController::prepare() {
	setupPlainDivider(tr::lng_channels_recommended());
	setupActiveRows();
	fill();
}

void RecommendationsController::load() {
	if (_requested || countCurrent()) {
		return;
	}
	_requested = true;
	const auto participants = &session().api().chatParticipants();
	participants->loadRecommendations();
	participants->recommendationsLoaded(
	) | rpl::take(1) | rpl::on_next([=] {
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
	setCount(delegate()->peerListFullRowsCount());
}

void RecommendationsController::appendRow(not_null<ChannelData*> channel) {
	auto row = std::make_unique<ChannelRow>(channel);
	row->setActive(isActive(channel));
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

RecentAppsController::RecentAppsController(
	not_null<Window::SessionController*> window)
: ObjectListController(window) {
}

void RecentAppsController::prepare() {
	setupExpandDivider(tr::lng_bot_apps_your());

	_bots.reserve(kProbablyMaxApps);
	rpl::single() | rpl::then(
		session().topBotApps().updates()
	) | rpl::on_next([=] {
		_bots.clear();
		for (const auto &peer : session().topBotApps().list()) {
			if (const auto bot = peer->asUser()) {
				if (bot->isBot() && !bot->isInaccessible()) {
					_bots.push_back(bot);
				}
			}
		}
		setCount(_bots.size());
		while (delegate()->peerListFullRowsCount()) {
			delegate()->peerListRemoveRow(delegate()->peerListRowAt(0));
		}
		fill();
	}, _lifetime);

	expanded() | rpl::skip(1) | rpl::on_next([=] {
		fill();
	}, _lifetime);
}

const std::vector<not_null<UserData*>> &RecentAppsController::bots() const {
	return _bots;
}

base::unique_qptr<Ui::PopupMenu> RecentAppsController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto peer = row->peer();
	const auto weak = base::make_weak(this);
	const auto session = &this->session();
	const auto removeOne = crl::guard(session, [=] {
		if (weak) {
			const auto rowId = peer->id.value;
			if (const auto row = delegate()->peerListFindRow(rowId)) {
				setCount(std::max(0, countCurrent() - 1));
				delegate()->peerListRemoveRow(row);
				delegate()->peerListRefreshRows();
			}
		}
		session->topBotApps().remove(peer);
	});
	FillEntryMenu(Ui::Menu::CreateAddActionCallback(result), {
		.controller = window(),
		.peer = peer,
		.removeOneText = tr::lng_recent_remove(tr::now),
		.removeOne = removeOne,
		.closeCallback = crl::guard(this, [=] {
			if (_closeCallback) {
				_closeCallback();
			}
		}),
	});
	return result;
}

void RecentAppsController::load() {
	session().topBotApps().reload();
}

rpl::producer<> RecentAppsController::refreshed() const {
	return _refreshed.events();
}

bool RecentAppsController::shown(not_null<PeerData*> peer) const {
	return delegate()->peerListFindRow(peer->id.value) != nullptr;
}

void RecentAppsController::fill() {
	const auto count = countCurrent();
	const auto limit = expandedCurrent()
		? count
		: std::min(count, kCollapsedAppsCount);
	const auto already = delegate()->peerListFullRowsCount();
	const auto delta = limit - already;
	if (!delta) {
		return;
	} else if (delta > 0) {
		for (auto i = already; i != limit; ++i) {
			appendRow(bots()[i]);
		}
	} else if (delta < 0) {
		for (auto i = already; i != limit;) {
			delegate()->peerListRemoveRow(delegate()->peerListRowAt(--i));
		}
	}
	delegate()->peerListRefreshRows();

	_refreshed.fire({});
}

void RecentAppsController::appendRow(not_null<UserData*> bot) {
	auto row = std::make_unique<PeerListRow>(bot);
	if (const auto count = bot->botInfo->activeUsers) {
		row->setCustomStatus(
			tr::lng_bot_status_users(tr::now, lt_count_decimal, count));
	}
	delegate()->peerListAppendRow(std::move(row));
}

PopularAppsController::PopularAppsController(
	not_null<Window::SessionController*> window,
	Fn<bool(not_null<PeerData*>)> filterOut,
	rpl::producer<> filterOutRefreshes)
: ObjectListController(window)
, _filterOut(std::move(filterOut))
, _filterOutRefreshes(std::move(filterOutRefreshes)) {
}

void PopularAppsController::prepare() {
	if (_filterOut) {
		setupPlainDivider(tr::lng_bot_apps_popular());
	}
	rpl::single() | rpl::then(
		std::move(_filterOutRefreshes)
	) | rpl::on_next([=] {
		fill();
	}, _lifetime);
}

void PopularAppsController::load() {
	if (_requested || countCurrent()) {
		return;
	}
	_requested = true;
	const auto attachWebView = &session().attachWebView();
	attachWebView->loadPopularAppBots();
	attachWebView->popularAppBotsLoaded(
	) | rpl::take(1) | rpl::on_next([=] {
		fill();
	}, _lifetime);
}

void PopularAppsController::fill() {
	while (delegate()->peerListFullRowsCount()) {
		delegate()->peerListRemoveRow(delegate()->peerListRowAt(0));
	}
	for (const auto &bot : session().attachWebView().popularAppBots()) {
		if (!_filterOut || !_filterOut(bot)) {
			appendRow(bot);
		}
	}
	const auto count = delegate()->peerListFullRowsCount();
	setCount(count);
	if (count > 0) {
		delegate()->peerListSetBelowWidget(object_ptr<Ui::DividerLabel>(
			(QWidget*)nullptr,
			object_ptr<Ui::FlatLabel>(
				(QWidget*)nullptr,
				tr::lng_bot_apps_which(
					lt_link,
					tr::lng_bot_apps_which_link(
						tr::url(u"internal:about_popular_apps"_q)),
					tr::marked),
				st::dialogsPopularAppsAbout),
			st::dialogsPopularAppsPadding));
	}
	delegate()->peerListRefreshRows();
}

void PopularAppsController::appendRow(not_null<UserData*> bot) {
	auto row = std::make_unique<PeerListRow>(bot);
	if (bot->isBot()) {
		if (!bot->botInfo->activeUsers && !bot->username().isEmpty()) {
			row->setCustomStatus('@' + bot->username());
		}
	}
	delegate()->peerListAppendRow(std::move(row));
}

struct Suggestions::SearchList {
	Key key;
	std::unique_ptr<Ui::ElasticScroll> scroll;
	not_null<Ui::RpWidget*> wrap;
	not_null<InnerWidget*> content;
	QString query;
	Data::MessagePosition offset;
	int32 offsetRate = 0;
	mtpRequestId requestId = 0;
	int received = 0;
	bool loaded = false;
};

Suggestions::Suggestions(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	rpl::producer<TopPeersList> topPeers,
	RecentPeersList recentPeers)
: RpWidget(parent)
, _controller(controller)
, _tabsScroll(
	std::make_unique<Ui::ScrollArea>(this, st::dialogsTabsScroll, true))
, _tabs(
	_tabsScroll->setOwnedWidget(
		object_ptr<Ui::SettingsSlider>(this, st::dialogsSearchTabs)))
, _tabKeys(TabKeysFor(controller))
, _chatsScroll(std::make_unique<Ui::ElasticScroll>(this))
, _chatsContent(
	_chatsScroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this)))
, _topPeersWrap(
	_chatsContent->add(object_ptr<Ui::SlideWrap<TopPeersStrip>>(
		this,
		object_ptr<TopPeersStrip>(this, std::move(topPeers)))))
, _topPeers(_topPeersWrap->entity())
, _recent(setupRecentPeers(std::move(recentPeers)))
, _emptyRecent(_chatsContent->add(setupEmptyRecent()))
, _channelsScroll(std::make_unique<Ui::ElasticScroll>(this))
, _channelsContent(
	_channelsScroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this)))
, _myChannels(setupMyChannels())
, _recommendations(setupRecommendations())
, _globalChannels(setupGlobalPeers(
	_channelsScroll.get(),
	_channelsContent,
	_globalChannelsResults.value(),
	_myChannels.get(),
	true))
, _channelsPosts(setupChannelsPosts())
, _emptyChannels(_channelsContent->add(setupEmptyChannels()))
, _appsScroll(std::make_unique<Ui::ElasticScroll>(this))
, _appsContent(
	_appsScroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this)))
, _postsScroll(std::make_unique<Ui::ElasticScroll>(this))
, _postsWrap(_postsScroll->setOwnedWidget(object_ptr<Ui::RpWidget>(this)))
, _recentApps(setupRecentApps())
, _popularApps(setupPopularApps())
, _searchQueryTimer([=] { applySearchQuery(); }) {
	setupTabs();
	setupChats();
	setupChannels();
	setupApps();
}

Suggestions::~Suggestions() {
	const auto cancel = [&](const std::unique_ptr<SearchList> &search) {
		if (search && search->requestId) {
			_controller->session().api().request(search->requestId).cancel();
		}
	};
	cancel(_channelsPosts);
	for (const auto &[key, search] : _searchLists) {
		cancel(search);
	}
}

void Suggestions::setupTabs() {
	_tabsScroll->setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		const auto pixelDelta = e->pixelDelta();
		const auto angleDelta = e->angleDelta();
		if (std::abs(pixelDelta.x()) + std::abs(angleDelta.x())) {
			return false;
		}
		const auto y = pixelDelta.y() ? pixelDelta.y() : angleDelta.y();
		_tabsScroll->scrollToX(_tabsScroll->scrollLeft() - y);
		return true;
	});

	const auto scrollToIndex = [=](int index, anim::type type) {
		const auto to = index
			? (_tabs->centerOfSection(index) - _tabsScroll->width() / 2)
			: 0;
		_tabsScrollAnimation.stop();
		if (type == anim::type::instant) {
			_tabsScroll->scrollToX(to);
		} else {
			_tabsScrollAnimation.start(
				[=](float64 v) { _tabsScroll->scrollToX(v); },
				_tabsScroll->scrollLeft(),
				std::min(to, _tabsScroll->scrollLeftMax()),
				st::defaultTabsSlider.duration);
		}
	};
	rpl::single(-1) | rpl::then(
		_tabs->sectionActivated()
	) | rpl::combine_previous(
	) | rpl::on_next([=](int was, int index) {
		if (was != index) {
			scrollToIndex(index, anim::type::normal);
		}
	}, _tabs->lifetime());

	const auto shadow = Ui::CreateChild<Ui::PlainShadow>(this);
	shadow->lower();

	_tabsScroll->move(0, 0);
	_tabs->move(0, 0);
	rpl::combine(
		widthValue(),
		_tabs->heightValue()
	) | rpl::on_next([=](int width, int height) {
		const auto line = st::lineWidth;
		shadow->setGeometry(0, height - line, width, line);
	}, shadow->lifetime());

	shadow->showOn(_tabsScroll->shownValue());

	const auto labels = base::flat_map<Key, QString>{
		{ Key{ Tab::Chats }, tr::lng_recent_chats(tr::now) },
		{ Key{ Tab::Channels }, tr::lng_recent_channels(tr::now) },
		{ Key{ Tab::Apps }, tr::lng_recent_apps(tr::now) },
		{ Key{ Tab::Posts }, tr::lng_recent_posts(tr::now) },
		{ Key{ Tab::Media, MediaType::Photo }, tr::lng_all_photos(tr::now) },
		{ Key{ Tab::Media, MediaType::Video }, tr::lng_all_videos(tr::now) },
		{ Key{ Tab::Downloads }, tr::lng_all_downloads(tr::now) },
		{ Key{ Tab::Media, MediaType::Link }, tr::lng_all_links(tr::now) },
		{ Key{ Tab::Media, MediaType::File }, tr::lng_all_files(tr::now) },
		{
			Key{ Tab::Media, MediaType::MusicFile },
			tr::lng_all_music(tr::now),
		},
		{
			Key{ Tab::Media, MediaType::RoundVoiceFile },
			tr::lng_all_voice(tr::now),
		},
	};

	auto sections = std::vector<TextWithEntities>();
	for (const auto key : _tabKeys) {
		const auto i = labels.find(key);
		Assert(i != end(labels));
		sections.push_back({ i->second });
	}
	_tabs->setSections(sections);
	_tabs->sectionActivated(
	) | rpl::on_next([=](int section) {
		Assert(section >= 0 && section < _tabKeys.size());
		switchTab(_tabKeys[section]);
	}, _tabs->lifetime());
}

void Suggestions::setupChats() {
	_recent->count.value() | rpl::on_next([=](int count) {
		_recent->wrap->toggle(count > 0, anim::type::instant);
		_emptyRecent->toggle(count == 0, anim::type::instant);
	}, _recent->wrap->lifetime());

	_topPeers->emptyValue() | rpl::on_next([=](bool empty) {
		_topPeersWrap->toggle(!empty, anim::type::instant);
	}, _topPeers->lifetime());

	_topPeers->clicks() | rpl::on_next([=](uint64 peerIdRaw) {
		const auto peerId = PeerId(peerIdRaw);
		_topPeerChosen.fire(_controller->session().data().peer(peerId));
	}, _topPeers->lifetime());

	_topPeers->pressed() | rpl::on_next([=](uint64 peerIdRaw) {
		handlePressForChatPreview(PeerId(peerIdRaw), [=](bool shown) {
			_topPeers->pressLeftToContextMenu(shown);
		});
	}, _topPeers->lifetime());

	_topPeers->pressCancelled() | rpl::on_next([=] {
		_controller->cancelScheduledPreview();
	}, _topPeers->lifetime());

	_topPeers->showMenuRequests(
	) | rpl::on_next([=](const ShowTopPeerMenuRequest &request) {
		const auto weak = base::make_weak(this);
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
				tr::now,
				Ui::Text::FixAmpersandInAction),
			.removeAllConfirm = tr::lng_recent_hide_sure(tr::now),
			.removeAll = removeAll,
			.closeCallback = crl::guard(
				this,
				[=] { _closeRequests.fire({}); }),
			});
	}, _topPeers->lifetime());

	_topPeers->scrollToRequests(
	) | rpl::on_next([this](Ui::ScrollToRequest request) {
		_chatsScroll->scrollToY(request.ymin, request.ymax);
	}, _topPeers->lifetime());

	_topPeers->verticalScrollEvents(
	) | rpl::on_next([=](not_null<QWheelEvent*> e) {
		_chatsScroll->viewportEvent(e);
	}, _topPeers->lifetime());

	_chatsScroll->setVisible(_key.current().tab == Tab::Chats);
	_chatsScroll->setCustomTouchProcess(_recent->processTouch);
}

void Suggestions::handlePressForChatPreview(
	PeerId id,
	Fn<void(bool)> callback) {
	callback = crl::guard(this, callback);
	const auto row = RowDescriptor(
		_controller->session().data().history(id),
		FullMsgId());
	if (base::IsAltPressed()) {
		_controller->showChatPreview(row, callback);
	} else {
		_controller->scheduleChatPreview(row, callback);
	}
}

void Suggestions::setupChannels() {
	_myChannels->count.value() | rpl::on_next([=](int count) {
		_myChannels->wrap->toggle(count > 0, anim::type::instant);
	}, _myChannels->wrap->lifetime());

	rpl::combine(
		_recommendations->count.value(),
		_channelsQuery.value()
	) | rpl::on_next([=](int count, const QString &query) {
		_recommendations->wrap->toggle(
			count > 0 && query.isEmpty(),
			anim::type::instant);
	}, _recommendations->wrap->lifetime());

	_globalChannels->count.value() | rpl::on_next([=](int count) {
		_globalChannels->wrap->toggle(count > 0, anim::type::instant);
	}, _globalChannels->wrap->lifetime());

	_emptyChannels->toggleOn(
		rpl::combine(
			_myChannels->count.value(),
			_recommendations->count.value(),
			_globalChannels->count.value(),
			_channelsQuery.value(),
			_channelsLoading.value(),
			_channelsHasPosts.value()
		) | rpl::map([](
				int my,
				int recommended,
				int global,
				const QString &query,
				bool loading,
				bool posts) {
			return query.isEmpty()
				? !(my + recommended)
				: (!(my + global) && !loading && !posts);
		}),
		anim::type::instant);

	_channelsScroll->setVisible(_key.current().tab == Tab::Channels);
	_channelsScroll->setCustomTouchProcess([=](not_null<QTouchEvent*> e) {
		const auto myChannels = _myChannels->processTouch(e);
		const auto recommendations = _recommendations->processTouch(e);
		const auto globalChannels = _globalChannels->processTouch(e);
		return myChannels || recommendations || globalChannels;
	});
}

void Suggestions::setupApps() {
	_recentApps->count.value() | rpl::on_next([=](int count) {
		_recentApps->wrap->toggle(count > 0, anim::type::instant);
	}, _recentApps->wrap->lifetime());

	_popularApps->count.value() | rpl::on_next([=](int count) {
		_popularApps->wrap->toggle(count > 0, anim::type::instant);
	}, _popularApps->wrap->lifetime());

	_appsScroll->setVisible(_key.current().tab == Tab::Apps);
	_appsScroll->setCustomTouchProcess([=](not_null<QTouchEvent*> e) {
		const auto recentApps = _recentApps->processTouch(e);
		const auto popularApps = _popularApps->processTouch(e);
		return recentApps || popularApps;
	});
}

Ui::Controls::SwipeHandlerArgs Suggestions::generateIncompleteSwipeArgs() {
	_swipeLifetime.destroy();

	auto update = [=](Ui::Controls::SwipeContextData data) {
		if (data.translation != 0) {
			if (!_swipeBackData.callback) {
				_swipeBackData = Ui::Controls::SetupSwipeBack(
					this,
					[=]() -> std::pair<QColor, QColor> {
						return {
							st::historyForwardChooseBg->c,
							st::historyForwardChooseFg->c,
						};
					},
					data.translation < 0);
			}
			_swipeBackData.callback(data);
			return;
		} else if (_swipeBackData.lifetime) {
			_swipeBackData = {};
		}
	};
	auto init = [=](Ui::Controls::SwipeHandlerInitData data) {
		if (!_tabs) {
			return Ui::Controls::SwipeHandlerFinishData();
		}
		const auto activeSection = _tabs->activeSection();
		const auto isToLeft = data.direction == Qt::RightToLeft;
		if ((isToLeft && activeSection > 0)
			|| (!isToLeft && activeSection < _tabKeys.size() - 1)) {
			return Ui::Controls::DefaultSwipeBackHandlerFinishData([=] {
				if (_tabs
					&& _tabs->activeSection() == activeSection) {
					_swipeBackData = {};
					_tabs->setActiveSection(isToLeft
						? activeSection - 1
						: activeSection + 1);
				}
			});
		}
		return Ui::Controls::SwipeHandlerFinishData();
	};
	return { .widget = this, .update = update, .init = init };
}

void Suggestions::reinstallSwipe(not_null<Ui::ElasticScroll*> scroll) {
	_swipeLifetime.destroy();

	auto args = generateIncompleteSwipeArgs();
	args.scroll = scroll;
	args.onLifetime = &_swipeLifetime;

	Ui::Controls::SetupSwipeHandler(std::move(args));
}

void Suggestions::selectJump(Qt::Key direction, int pageSize) {
	if (const auto search = shownSearchList(_key.current())) {
		DispatchResultsKey(search->content, direction, pageSize);
		return;
	}
	switch (_key.current().tab) {
	case Tab::Chats: selectJumpChats(direction, pageSize); return;
	case Tab::Channels: selectJumpChannels(direction, pageSize); return;
	case Tab::Apps: selectJumpApps(direction, pageSize); return;
	case Tab::Posts:
		if (_postsContent) {
			DispatchResultsKey(_postsContent, direction, pageSize);
		}
		return;
	}
}

void Suggestions::selectJumpSections(
		const std::vector<Fn<JumpResult(Qt::Key, int)>> &sections,
		not_null<Ui::ElasticScroll*> scroll,
		Qt::Key direction,
		int pageSize) {
	const auto count = int(sections.size());
	const auto selected = int(ranges::find_if(sections, [](const auto &jump) {
		return jump(Qt::Key(), 0) == JumpResult::Applied;
	}) - begin(sections));
	if (direction == Qt::Key_Down) {
		auto from = selected;
		if (from == count) {
			for (auto i = 0; i != count; ++i) {
				if (sections[i](direction, 0) == JumpResult::Applied) {
					from = i;
					break;
				}
			}
			if (from == count || !pageSize) {
				return;
			}
		}
		if (sections[from](direction, pageSize)
			== JumpResult::AppliedAndOut) {
			for (auto i = from + 1; i != count; ++i) {
				if (sections[i](direction, 0) == JumpResult::Applied) {
					return;
				}
			}
			sections[from](Qt::Key_Up, -1);
		}
	} else if (direction == Qt::Key_Up && selected < count) {
		if (sections[selected](direction, pageSize)
			== JumpResult::AppliedAndOut) {
			for (auto i = selected; i != 0;) {
				if (sections[--i](direction, -1) == JumpResult::Applied) {
					return;
				}
			}
			scroll->scrollTo(0);
		}
	}
}

void Suggestions::selectJumpChats(Qt::Key direction, int pageSize) {
	const auto recentHasSelection = [=] {
		return _recent->selectJump({}, 0) == JumpResult::Applied;
	};
	if (pageSize) {
		if (direction == Qt::Key_Down || direction == Qt::Key_Up) {
			_topPeers->deselectByKeyboard();
			if (!recentHasSelection()) {
				if (direction == Qt::Key_Down) {
					_recent->selectJump(direction, 0);
				} else {
					return;
				}
			}
			if (_recent->selectJump(direction, pageSize)
				== JumpResult::AppliedAndOut) {
				if (direction == Qt::Key_Up) {
					_chatsScroll->scrollTo(0);
				}
			}
		}
	} else if (direction == Qt::Key_Up) {
		if (_recent->selectJump(direction, pageSize)
			== JumpResult::AppliedAndOut) {
			_topPeers->selectByKeyboard(direction);
		} else if (_topPeers->selectedByKeyboard()) {
			_topPeers->selectByKeyboard(direction);
		}
	} else if (direction == Qt::Key_Down) {
		if (!_topPeersWrap->toggled() || recentHasSelection()) {
			_recent->selectJump(direction, pageSize);
		} else if (_topPeers->selectedByKeyboard()) {
			if (!_topPeers->selectByKeyboard(direction)
				&& _recent->count.current() > 0) {
				_topPeers->deselectByKeyboard();
				_recent->selectJump(direction, pageSize);
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
	if (_channelsQuery.current().isEmpty()) {
		selectJumpSections(
			{ _myChannels->selectJump, _recommendations->selectJump },
			_channelsScroll.get(),
			direction,
			pageSize);
		return;
	}
	const auto posts = _channelsPosts->content;
	const auto postsJump = [=](Qt::Key direction, int pageSize) {
		const auto had = posts->hasSelection();
		if (!_channelsHasPosts.current()) {
			return JumpResult::NotApplied;
		} else if (direction == Qt::Key()
			|| (direction == Qt::Key_Up && !had)) {
			return had ? JumpResult::Applied : JumpResult::NotApplied;
		}
		_channelsPostsKeyJump = true;
		DispatchResultsKey(posts, direction, std::max(pageSize, 0));
		_channelsPostsKeyJump = false;
		return posts->hasSelection()
			? JumpResult::Applied
			: had
			? JumpResult::AppliedAndOut
			: JumpResult::NotApplied;
	};
	selectJumpSections(
		{ _myChannels->selectJump, _globalChannels->selectJump, postsJump },
		_channelsScroll.get(),
		direction,
		pageSize);
}

void Suggestions::selectJumpApps(Qt::Key direction, int pageSize) {
	selectJumpSections(
		{ _recentApps->selectJump, _popularApps->selectJump },
		_appsScroll.get(),
		direction,
		pageSize);
}

void Suggestions::chooseRow() {
	if (const auto search = shownSearchList(_key.current())) {
		search->content->chooseRow();
		return;
	}
	switch (_key.current().tab) {
	case Tab::Chats:
		if (!_topPeers->chooseRow()) {
			_recent->choose();
		}
		break;
	case Tab::Channels:
		if (!_myChannels->choose()) {
			if (_channelsQuery.current().isEmpty()) {
				_recommendations->choose();
			} else if (!_globalChannels->choose()
				&& _channelsHasPosts.current()) {
				_channelsPosts->content->chooseRow();
			}
		}
		break;
	case Tab::Apps:
		if (!_recentApps->choose()) {
			_popularApps->choose();
		}
		break;
	case Tab::Posts:
		if (_postsContent) {
			_postsContent->chooseRow();
		}
		break;
	}
}

bool Suggestions::consumeSearchQuery(const QString &query) {
	_fieldQuery = query;
	return setTabSearchQuery(query);
}

bool Suggestions::TakesSearchQuery(Key key) {
	return (key.tab != Tab::Chats);
}

bool Suggestions::ListsSearchResults(Key key) {
	return (key == Key{ Tab::Media, MediaType::Photo })
		|| (key == Key{ Tab::Media, MediaType::Video });
}

bool Suggestions::ownsSearchQuery(const QString &query) const {
	return !query.isEmpty()
		&& (query == _fieldQuery)
		&& TakesSearchQuery(_key.current());
}

bool Suggestions::setTabSearchQuery(const QString &query) {
	const auto key = _key.current();
	if (!TakesSearchQuery(key)) {
		return false;
	} else if (key.tab == Tab::Posts) {
		const auto changed = (_searchQuery != query);
		setPostsSearchQuery(query);
		return changed || !query.isEmpty();
	} else if (_searchQuery == query) {
		return !query.isEmpty();
	}
	_searchQuery = query;
	_persist = !_searchQuery.isEmpty();
	if (key.tab == Tab::Channels) {
		setChannelsSearchQuery(query.trimmed());
	}
	if (query.isEmpty() || key.tab == Tab::Downloads) {
		_searchQueryTimer.cancel();
		applySearchQuery();
	} else {
		_searchQueryTimer.callOnce(kSearchQueryDelay);
	}
	return true;
}

void Suggestions::setupPostsSearch() {
	_postsSearch = std::make_unique<PostsSearch>(&_controller->session());

	_postsSearch->stateUpdates(
	) | rpl::on_next([=](const PostsSearchState &state) {
		if (state.intro) {
			if (!_postsSearchIntro) {
				setupPostsIntro(*state.intro);
			} else {
				_postsSearchIntro->update(*state.intro);
			}
			return;
		} else if (!_postsContent) {
			setupPostsResults();
		}

		_postsContent->applySearchState(SearchState{
			.tab = ChatSearchTab::PublicPosts,
			.query = _postsSearchQuery,
		});
		if (state.loading) {
			_postsContent->searchRequested(true);
		} else {
			_postsContent->searchReceived(
				state.page,
				nullptr,
				{ .posts = true, .start = true },
				state.totalCount);
			_postsScroll->scrollToY(0);
			updatePostsSearchVisibleRange();
		}
	}, _postsWrap->lifetime());

	_postsSearch->pagesUpdates(
	) | rpl::on_next([=](const PostsSearchState &state) {
		Expects(!state.intro && !state.loading);

		if (!_postsContent) {
			return;
		}
		_postsContent->searchReceived(
			state.page,
			nullptr,
			{ .posts = true },
			state.totalCount);
		updatePostsSearchVisibleRange();
	}, _postsWrap->lifetime());
}

void Suggestions::setPostsSearchQuery(const QString &query) {
	if (!_postsSearch) {
		setupPostsSearch();
	}
	if (!query.isEmpty()) {
		_persist = true;
	}
	_searchQuery = query;
	_postsSearchQuery = query;
	_searchQueryTimer.cancel();
	_postsSearch->setQuery(query);
}

void Suggestions::setupPostsResults() {
	Expects(!_postsContent);

	delete base::take(_postsSearchIntro);
	_postsContent = Ui::CreateChild<InnerWidget>(
		_postsWrap.get(),
		_controller,
		rpl::single(InnerWidget::ChildListShown()));

	_postsContent->applySearchState(SearchState{
		.tab = ChatSearchTab::PublicPosts,
		.query = _postsSearchQuery,
	});
	_postsContent->searchRequested(true);

	_postsContent->chosenRow(
	) | rpl::on_next([=](const ChosenRow &row) {
		showSearchResult(row, _postsSearchQuery);
	}, _postsContent->lifetime());

	_postsContent->heightValue() | rpl::on_next([=](int height) {
		_postsWrap->resize(_postsWrap->width(), height);
	}, _postsContent->lifetime());

	_postsContent->mustScrollTo(
	) | rpl::on_next([=](const Ui::ScrollToRequest &request) {
		_postsScroll->scrollToY(request.ymin, request.ymax);
	}, _postsContent->lifetime());

	rpl::combine(
		rpl::single(rpl::empty) | rpl::then(_postsScroll->scrolls()),
		_postsScroll->heightValue()
	) | rpl::on_next([=] {
		updatePostsSearchVisibleRange();
	}, _postsContent->lifetime());

	_postsContent->setLoadMoreCallback([=] {
		_postsSearch->requestMore();
	});

	_postsContent->setDeselectOnTopUp(true);
	_postsContent->setNarrowRatio(0.);
	_postsContent->show();
	updateControlsGeometry();
}

void Suggestions::updatePostsSearchVisibleRange() {
	Expects(_postsContent != nullptr);

	UpdateVisibleRange(_postsScroll.get(), _postsContent);
}

void Suggestions::showSearchResult(
		const ChosenRow &row,
		const QString &query) {
	const auto history = row.key.history();
	if (!history) {
		return;
	}
	_persist = true;
	const auto showAtMsgId = row.message.fullId.msg;
	auto params = Window::SectionShow(
		Window::SectionShow::Way::ClearStack);
	params.highlight = Window::SearchHighlightId(query);
	if (row.newWindow) {
		_controller->showInNewWindow(history->peer, showAtMsgId);
		_closeRequests.fire({});
	} else {
		_controller->showThread(history, showAtMsgId, params);
	}
}

void Suggestions::setupPostsIntro(const PostsSearchIntroState &intro) {
	Expects(!_postsSearchIntro);

	delete base::take(_postsContent);
	_postsSearchIntro = Ui::CreateChild<PostsSearchIntro>(_postsWrap, intro);

	_postsSearchIntro->searchWithStars(
	) | rpl::on_next([=](int stars) {
		if (!_controller->session().premium()) {
			Settings::ShowPremium(
				_controller,
				u"posts_search"_q);
		} else if (!stars) {
			_postsSearch->setAllowedStars(0);
		} else {
			using namespace Settings;
			const auto done = [=](Settings::SmallBalanceResult result) {
				if (result == Settings::SmallBalanceResult::Success
					|| result == Settings::SmallBalanceResult::Already) {
					const auto spent = _postsSearch->setAllowedStars(stars);
					if (spent > 0) {
						_controller->showToast({
							.text = tr::lng_posts_paid_spent(
								tr::now,
								lt_count,
								spent,
								tr::rich),
							.attach = RectPart::Top,
							.duration = Ui::Toast::kDefaultDuration * 2,
						});
					}
				}
			};
			MaybeRequestBalanceIncrease(
				_controller->uiShow(),
				stars,
				SmallBalanceForSearch{},
				done);
		}
	}, _postsSearchIntro->lifetime());

	_postsScroll->heightValue() | rpl::on_next([=](int height) {
		_postsWrap->resize(_postsWrap->width(), height);
	}, _postsSearchIntro->lifetime());

	_postsSearchIntro->show();
	updateControlsGeometry();
}

auto Suggestions::setupSearchList(Key key) -> std::unique_ptr<SearchList> {
	auto scroll = std::make_unique<Ui::ElasticScroll>(this);
	const auto wrap = scroll->setOwnedWidget(
		object_ptr<Ui::RpWidget>(this));
	const auto content = Ui::CreateChild<InnerWidget>(
		wrap,
		_controller,
		rpl::single(InnerWidget::ChildListShown()));
	auto result = std::make_unique<SearchList>(SearchList{
		.key = key,
		.scroll = std::move(scroll),
		.wrap = wrap,
		.content = content,
	});
	const auto raw = result.get();
	setupSearchListContent(raw);

	content->heightValue() | rpl::on_next([=](int height) {
		wrap->resize(wrap->width(), height);
	}, content->lifetime());

	content->mustScrollTo(
	) | rpl::on_next([=](const Ui::ScrollToRequest &request) {
		raw->scroll->scrollToY(request.ymin, request.ymax);
	}, content->lifetime());

	rpl::combine(
		rpl::single(rpl::empty) | rpl::then(raw->scroll->scrolls()),
		raw->scroll->heightValue()
	) | rpl::on_next([=] {
		updateSearchListVisibleRange(raw);
	}, content->lifetime());

	content->show();
	raw->scroll->hide();
	return result;
}

auto Suggestions::setupChannelsPosts() -> std::unique_ptr<SearchList> {
	const auto wrap = _channelsContent->add(
		object_ptr<Ui::SlideWrap<InnerWidget>>(
			_channelsContent,
			object_ptr<InnerWidget>(
				_channelsContent,
				_controller,
				rpl::single(InnerWidget::ChildListShown()))));
	wrap->toggleOn(
		rpl::combine(
			_channelsLoading.value(),
			_channelsHasPosts.value(),
			rpl::mappers::_1 || rpl::mappers::_2),
		anim::type::instant);
	auto result = std::make_unique<SearchList>(SearchList{
		.key = Key{ Tab::Channels },
		.wrap = wrap,
		.content = wrap->entity(),
	});
	setupSearchListContent(result.get());

	rpl::combine(
		rpl::single(rpl::empty) | rpl::then(_channelsScroll->scrolls()),
		_channelsScroll->heightValue(),
		wrap->geometryValue()
	) | rpl::on_next([=] {
		updateChannelsPostsVisibleRange();
	}, wrap->lifetime());

	result->content->mustScrollTo(
	) | rpl::filter([=] {
		return _channelsPostsKeyJump;
	}) | rpl::on_next([=](const Ui::ScrollToRequest &request) {
		const auto top = wrap->y();
		_channelsScroll->scrollToY(
			top + request.ymin,
			(request.ymax >= 0) ? (top + request.ymax) : -1);
	}, wrap->lifetime());
	return result;
}

void Suggestions::setupSearchListContent(not_null<SearchList*> search) {
	const auto content = search->content;
	content->setSearchResultsOnly([](int count) {
		return tr::lng_search_found_results(tr::now, lt_count, count);
	});
	content->chosenRow(
	) | rpl::on_next([=](const ChosenRow &row) {
		showSearchResult(row, search->query);
	}, content->lifetime());
	content->setLoadMoreCallback([=] {
		if (search->offset) {
			requestSearchList(search);
		}
	});
	content->setNarrowRatio(0.);
}

Suggestions::SearchList *Suggestions::shownSearchList(Key key) const {
	const auto i = _searchLists.find(key);
	return (i != end(_searchLists) && !i->second->query.isEmpty())
		? i->second.get()
		: nullptr;
}

void Suggestions::setSearchListQuery(Key key, const QString &query) {
	auto i = _searchLists.find(key);
	if (i == end(_searchLists)) {
		if (query.isEmpty()) {
			return;
		}
		i = _searchLists.emplace(key, setupSearchList(key)).first;
		updateControlsGeometry();
	}
	const auto search = i->second.get();
	if (search->query == query) {
		return;
	}
	const auto toggled = (search->query.isEmpty() != query.isEmpty());
	resetSearchList(search, query);
	if (!query.isEmpty()) {
		search->scroll->scrollToY(0);
		requestSearchList(search);
	}
	if (toggled
		&& _key.current() == key
		&& !_slideAnimation.animating()
		&& !_shownAnimation.animating()) {
		finishShow();
	}
}

void Suggestions::resetSearchList(
		not_null<SearchList*> search,
		const QString &query) {
	if (search->requestId) {
		_controller->session().api().request(
			base::take(search->requestId)).cancel();
	}
	search->query = query;
	search->offset = Data::MessagePosition();
	search->offsetRate = 0;
	search->received = 0;
	search->loaded = false;
	if (!query.isEmpty()) {
		search->content->applySearchState(SearchState{
			.tab = ChatSearchTab::PublicPosts,
			.query = query,
		});
		search->content->searchRequested(true);
	}
}

void Suggestions::setChannelsSearchQuery(const QString &query) {
	if (_channelsQuery.current() == query) {
		return;
	}
	_channelsScroll->scrollToY(0);
	resetSearchList(_channelsPosts.get(), query);
	_joinedChannelsResults = std::vector<not_null<PeerData*>>();
	_globalChannelsResults = std::vector<not_null<PeerData*>>();
	_channelsLoading = !query.isEmpty();
	_channelsHasPosts = false;
	_channelsQuery = query;
	if (query.isEmpty() && _channelsPeerSearch) {
		_channelsPeerSearch->clear();
	}
}

void Suggestions::requestChannelsSearch() {
	const auto query = _channelsQuery.current();
	if (query.isEmpty()) {
		return;
	} else if (!_channelsPeerSearch) {
		_channelsPeerSearch = std::make_unique<Api::PeerSearch>(
			&_controller->session(),
			Api::PeerSearch::Type::Channels);
	}
	_channelsPeerSearch->request(query, [=](Api::PeerSearchResult result) {
		if (_channelsQuery.current() != query) {
			return;
		}
		const auto channels = [](bool joined) {
			return [=](not_null<PeerData*> peer) {
				const auto channel = peer->asBroadcast();
				return channel && (channel->amIn() == joined);
			};
		};
		_joinedChannelsResults = FilterPeers(result.my, channels(true));
		_globalChannelsResults = FilterPeers(result.peers, channels(false));
		const auto posts = _channelsPosts.get();
		if (!posts->offset && !posts->loaded) {
			requestSearchList(posts);
		}
	});
}

void Suggestions::requestSearchList(not_null<SearchList*> search) {
	if (search->requestId || search->loaded || search->query.isEmpty()) {
		return;
	}
	const auto query = search->query;
	const auto done = crl::guard(this, [=](
			const Api::GlobalMediaResult &result) {
		if (search->query == query) {
			searchListReceived(search, result);
		}
	});
	const auto session = &_controller->session();
	if (search->key.tab != Tab::Channels) {
		search->requestId = session->api().requestGlobalMedia(
			search->key.mediaType,
			query,
			search->offsetRate,
			search->offset,
			false,
			done);
		return;
	}
	using Flag = MTPmessages_SearchGlobal::Flag;
	const auto offset = search->offset;
	search->requestId = session->api().request(MTPmessages_SearchGlobal(
		MTP_flags(Flag::f_broadcasts_only),
		MTP_int(0),
		MTPInputChannel(),
		MTP_string(query),
		MTP_inputMessagesFilterEmpty(),
		MTP_int(0),
		MTP_int(0),
		MTP_int(search->offsetRate),
		(offset
			? session->data().peer(offset.fullId.peer)->input()
			: MTP_inputPeerEmpty()),
		MTP_int(offset.fullId.msg),
		MTP_int(kSearchPerPage)
	)).done([=](const MTPmessages_Messages &result) {
		done(Api::ParseGlobalMediaResult(session, result, false));
	}).fail(crl::guard(this, [=] {
		if (search->query != query) {
			return;
		}
		search->requestId = 0;
		if (search == _channelsPosts.get()) {
			_channelsLoading = false;
		}
	})).send();
}

void Suggestions::searchListReceived(
		not_null<SearchList*> search,
		const Api::GlobalMediaResult &result) {
	search->requestId = 0;

	const auto start = !search->offset;
	const auto owner = &_controller->session().data();
	auto items = std::vector<not_null<HistoryItem*>>();
	items.reserve(result.messageIds.size());
	for (const auto &position : result.messageIds) {
		if (const auto item = owner->message(position.fullId)) {
			items.push_back(item);
		}
	}
	search->received += int(items.size());
	if (!result.offsetPosition || result.offsetPosition == search->offset) {
		search->loaded = true;
	} else {
		search->offset = result.offsetPosition;
		search->offsetRate = result.offsetRate;
		search->loaded = !result.offsetRate;
	}
	const auto fullCount = search->loaded
		? search->received
		: std::max(result.fullCount, search->received);
	search->content->searchReceived(
		std::move(items),
		nullptr,
		{ .start = start },
		fullCount);
	if (search == _channelsPosts.get()) {
		_channelsLoading = false;
		_channelsHasPosts = (search->received > 0);
		updateChannelsPostsVisibleRange();
	} else {
		updateSearchListVisibleRange(search);
	}
}

void Suggestions::updateChannelsPostsVisibleRange() {
	if (!_channelsPosts) {
		return;
	}
	UpdateVisibleRange(
		_channelsScroll.get(),
		_channelsPosts->content,
		_channelsPosts->wrap->y());
}

void Suggestions::updateSearchListVisibleRange(
		not_null<SearchList*> search) {
	UpdateVisibleRange(search->scroll.get(), search->content);
}

void Suggestions::applySearchQuery() {
	if (_key.current().tab == Tab::Channels) {
		requestChannelsSearch();
		return;
	} else if (ListsSearchResults(_key.current())) {
		setSearchListQuery(_key.current(), _searchQuery.trimmed());
		return;
	}
	if (const auto search = mediaListSearch(_key.current())) {
		if (search->query() != _searchQuery) {
			search->setQuery(_searchQuery);
		}
	}
}

void Suggestions::resetTabSearchQuery(Key key) {
	if (key.tab == Tab::Posts) {
		if (_postsSearch) {
			_postsSearchQuery = QString();
			_postsSearch->setQuery(QString());
		}
		return;
	} else if (key.tab == Tab::Channels) {
		setChannelsSearchQuery(QString());
		return;
	} else if (ListsSearchResults(key)) {
		setSearchListQuery(key, QString());
		return;
	}
	if (const auto search = mediaListSearch(key)) {
		if (!search->query().isEmpty()) {
			search->setQuery(QString());
		}
	}
}

Ui::SearchFieldController *Suggestions::mediaListSearch(Key key) const {
	const auto i = _mediaLists.find(key);
	return (i != end(_mediaLists) && i->second.wrap)
		? i->second.wrap->controller()->searchFieldController()
		: nullptr;
}

rpl::producer<> Suggestions::clearSearchQueryRequests() const {
	return _clearSearchQueryRequests.events();
}

rpl::producer<> Suggestions::reapplySearchQueryRequests() const {
	return _reapplySearchQueryRequests.events();
}

Data::Thread *Suggestions::updateFromParentDrag(QPoint globalPosition) {
	if (const auto search = shownSearchList(_key.current())) {
		return search->content->updateFromParentDrag(globalPosition);
	}
	switch (_key.current().tab) {
	case Tab::Chats: return updateFromChatsDrag(globalPosition);
	case Tab::Channels: return updateFromChannelsDrag(globalPosition);
	}
	return nullptr;
}

Data::Thread *Suggestions::updateFromChatsDrag(QPoint globalPosition) {
	if (const auto top = _topPeers->updateFromParentDrag(globalPosition)) {
		return _controller->session().data().history(PeerId(top));
	}
	return fromListId(_recent->updateFromParentDrag(globalPosition));
}

Data::Thread *Suggestions::updateFromChannelsDrag(QPoint globalPosition) {
	if (const auto id = _myChannels->updateFromParentDrag(globalPosition)) {
		return fromListId(id);
	}
	return fromListId(
		channelsSecondList()->updateFromParentDrag(globalPosition));
}

Data::Thread *Suggestions::updateFromAppsDrag(QPoint globalPosition) {
	if (const auto id = _recentApps->updateFromParentDrag(globalPosition)) {
		return fromListId(id);
	}
	return fromListId(_popularApps->updateFromParentDrag(globalPosition));
}

not_null<Suggestions::ObjectList*> Suggestions::channelsSecondList() const {
	return _channelsQuery.current().isEmpty()
		? _recommendations.get()
		: _globalChannels.get();
}

Data::Thread *Suggestions::fromListId(uint64 peerListRowId) {
	return peerListRowId
		? _controller->session().data().history(PeerId(peerListRowId)).get()
		: nullptr;
}

void Suggestions::dragLeft() {
	_topPeers->dragLeft();
	_recent->dragLeft();
	_myChannels->dragLeft();
	_recommendations->dragLeft();
	_globalChannels->dragLeft();
	_recentApps->dragLeft();
	_popularApps->dragLeft();
	for (const auto &[key, search] : _searchLists) {
		search->content->dragLeft();
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

void Suggestions::switchTab(Key key) {
	const auto was = _key.current();
	if (was == key) {
		return;
	}
	const auto query = _fieldQuery;
	_key = key;
	_persist = false;
	_searchQuery = QString();
	_searchQueryTimer.cancel();
	const auto carry = !query.isEmpty();
	if (!carry) {
		_clearSearchQueryRequests.fire({});
		resetTabSearchQuery(key);
	} else if (TakesSearchQuery(key)) {
		setTabSearchQuery(query);
		if (_searchQueryTimer.isActive()) {
			_searchQueryTimer.cancel();
			applySearchQuery();
		}
	}
	if (!_tabs->isHidden()) {
		startSlideAnimation(was, key);
	}
	if (carry) {
		_reapplySearchQueryRequests.fire({});
	}
}

void Suggestions::ensureContent(Key key) {
	if (key.tab == Tab::Posts) {
		setPostsSearchQuery(_searchQuery);
		return;
	} else if (key.tab != Tab::Downloads && key.tab != Tab::Media) {
		return;
	}
	auto &list = _mediaLists[key];
	if (list.wrap) {
		return;
	}
	const auto self = _controller->session().user();
	const auto memento = (key.tab == Tab::Downloads)
		? Info::Downloads::Make(self)
		: std::make_shared<Info::Memento>(
			self,
			Info::Section(key.mediaType, Info::Section::Type::GlobalMedia));
	list.wrap = Ui::CreateChild<Info::WrapWidget>(
		this,
		_controller,
		Info::Wrap::Search,
		memento.get());
	list.wrap->setVisible(!shownSearchList(key));
	updateControlsGeometry();
	if (!_searchQuery.isEmpty()) {
		applySearchQuery();
	}
}

void Suggestions::startSlideAnimation(Key was, Key now) {
	ensureContent(now);
	const auto wasIndex = ranges::find(_tabKeys, was);
	const auto nowIndex = ranges::find(_tabKeys, now);
	if (!_slideAnimation.animating()) {
		const auto find = [&](Key key) -> not_null<QWidget*> {
			switch (key.tab) {
			case Tab::Chats: return _chatsScroll.get();
			case Tab::Channels: return _channelsScroll.get();
			case Tab::Apps: return _appsScroll.get();
			case Tab::Posts: return _postsScroll.get();
			}
			return _mediaLists[key].wrap;
		};
		auto left = find(was);
		auto right = find(now);
		if (wasIndex > nowIndex) {
			std::swap(left, right);
		}
		_slideLeft = Ui::GrabWidget(left);
		_slideLeftTop = left->y();
		_slideRight = Ui::GrabWidget(right);
		_slideRightTop = right->y();
		left->hide();
		right->hide();
	}
	const auto from = (nowIndex > wasIndex) ? 0. : 1.;
	const auto to = (nowIndex > wasIndex) ? 1. : 0.;
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
	_tabsScroll->hide();
	_chatsScroll->hide();
	_channelsScroll->hide();
	_appsScroll->hide();
	_postsScroll->hide();
	for (const auto &[key, list] : _mediaLists) {
		list.wrap->hide();
	}
	for (const auto &[key, search] : _searchLists) {
		search->scroll->hide();
	}
	_slideAnimation.stop();
}

void Suggestions::finishShow() {
	_slideAnimation.stop();
	_slideLeft = _slideRight = QPixmap();
	_slideLeftTop = _slideRightTop = 0;

	_shownAnimation.stop();
	_cache = QPixmap();

	_tabsScroll->show();
	const auto key = _key.current();
	_chatsScroll->setVisible(!_tabsOnly && key == Key{ Tab::Chats });
	_channelsScroll->setVisible(key == Key{ Tab::Channels });
	_appsScroll->setVisible(key == Key{ Tab::Apps });
	_postsScroll->setVisible(key == Key{ Tab::Posts });
	const auto shownSearch = shownSearchList(key);
	for (const auto &[searchKey, search] : _searchLists) {
		search->scroll->setVisible(search.get() == shownSearch);
	}
	for (const auto &[mediaKey, list] : _mediaLists) {
		const auto current = (key == mediaKey) && !shownSearch;
		list.wrap->setVisible(current);
		if (current) {
			_swipeLifetime.destroy();
			auto incomplete = generateIncompleteSwipeArgs();
			list.wrap->replaceSwipeHandler(&incomplete);
		}
	}
	if (shownSearch) {
		reinstallSwipe(shownSearch->scroll.get());
	} else if (key == Key{ Tab::Chats }) {
		reinstallSwipe(_chatsScroll.get());
	} else if (key == Key{ Tab::Channels }) {
		reinstallSwipe(_channelsScroll.get());
	} else if (key == Key{ Tab::Apps }) {
		reinstallSwipe(_appsScroll.get());
	} else if (key == Key{ Tab::Posts }) {
		reinstallSwipe(_postsScroll.get());
	}
}

float64 Suggestions::shownOpacity() const {
	return _shownAnimation.value(_hidden ? 0. : 1.);
}

std::vector<Suggestions::Key> Suggestions::TabKeysFor(
		not_null<Window::SessionController*> controller) {
	auto result = std::vector<Key>{
		{ Tab::Chats },
		{ Tab::Channels },
		{ Tab::Apps },
		{ Tab::Posts },
		{ Tab::Media, MediaType::Photo },
		{ Tab::Media, MediaType::Video },
		{ Tab::Downloads },
		{ Tab::Media, MediaType::Link },
		{ Tab::Media, MediaType::File },
		{ Tab::Media, MediaType::MusicFile },
		{ Tab::Media, MediaType::RoundVoiceFile },
	};
	if (Core::App().downloadManager().empty()) {
		result.erase(ranges::find(result, Key{ Tab::Downloads }));
	}
	return result;
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
		const auto right = (_key.current().tab == Tab::Channels);
		const auto progress = _slideAnimation.value(right ? 1. : 0.);
		p.setOpacity(1. - progress);
		p.drawPixmap(
			anim::interpolate(0, -slide, progress),
			_slideLeftTop,
			_slideLeft);
		p.setOpacity(progress);
		p.drawPixmap(
			anim::interpolate(slide, 0, progress),
			_slideRightTop,
			_slideRight);
	}
}

void Suggestions::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void Suggestions::updateControlsGeometry() {
	const auto w = std::max(width(), st::columnMinimalWidthLeft);
	_tabs->fitWidthToSections();

	const auto tabs = _tabs->height();
	_tabsScroll->setGeometry(0, 0, w, tabs);
	if (_tabsOnly) {
		return;
	}

	const auto content = QRect(0, tabs, w, height() - tabs);

	_chatsScroll->setGeometry(content);
	_chatsContent->resizeToWidth(w);

	_channelsScroll->setGeometry(content);
	_channelsContent->resizeToWidth(w);

	_appsScroll->setGeometry(content);
	_appsContent->resizeToWidth(w);

	_postsScroll->setGeometry(content);
	_postsWrap->resizeToWidth(w);
	if (_postsSearchIntro) {
		_postsSearchIntro->setGeometry(0, 0, w, height() - tabs);
	} else if (_postsContent) {
		_postsContent->resizeToWidth(w);
		_postsContent->setMinimumHeight(height() - tabs);
		_postsContent->refresh();
	}

	for (const auto &[key, search] : _searchLists) {
		search->scroll->setGeometry(content);
		search->wrap->resizeToWidth(w);
		search->content->resizeToWidth(w);
		search->content->setMinimumHeight(content.height());
		search->content->refresh();
	}

	const auto expanding = false;
	const auto contentTillBottom = true;
	for (const auto &[key, list] : _mediaLists) {
		const auto full = !list.wrap->scrollBottomSkip();
		const auto additionalScroll = (full ? st::boxRadius : 0);
		const auto height = content.height() - (full ? 0 : st::boxRadius);
		const auto wrapGeometry = QRect{ 0, tabs, w, height};
		list.wrap->updateGeometry(
			wrapGeometry,
			expanding,
			contentTillBottom,
			additionalScroll,
			content.height());
	}
}

auto Suggestions::setupRecentPeers(RecentPeersList recentPeers)
-> std::unique_ptr<ObjectList> {
	const auto controller = lifetime().make_state<RecentsController>(
		_controller,
		std::move(recentPeers),
		[=](not_null<PeerData*> p) { _openBotMainAppRequests.fire_copy(p); },
		[=] { _closeRequests.fire({}); });

	const auto addToScroll = [=] {
		return _topPeersWrap->toggled() ? _topPeers->height() : 0;
	};
	auto result = setupObjectList(
		_chatsScroll.get(),
		_chatsContent,
		controller,
		addToScroll);
	const auto raw = result.get();
	const auto list = raw->wrap->entity();

	raw->selectJump = [list](Qt::Key direction, int pageSize) {
		const auto had = list->hasSelection();
		if (direction == Qt::Key()) {
			return had ? JumpResult::Applied : JumpResult::NotApplied;
		} else if (direction == Qt::Key_Up && !had) {
			return JumpResult::NotApplied;
		} else if (direction == Qt::Key_Down || direction == Qt::Key_Up) {
			const auto delta = (direction == Qt::Key_Down) ? 1 : -1;
			if (pageSize > 0) {
				list->selectSkipPage(pageSize, delta);
			} else {
				list->selectSkip(delta);
			}
			return list->hasSelection()
				? JumpResult::Applied
				: had
				? JumpResult::AppliedAndOut
				: JumpResult::NotApplied;
		}
		return JumpResult::NotApplied;
	};

	raw->chosen.events(
	) | rpl::on_next([=](not_null<PeerData*> peer) {
		_controller->session().recentPeers().bump(peer);
	}, list->lifetime());

	return result;
}

object_ptr<Ui::SlideWrap<>> Suggestions::setupEmptyRecent() {
	const auto icon = SearchEmptyIcon::Search;
	return setupEmpty(
		_chatsContent,
		_chatsScroll.get(),
		icon,
		tr::lng_recent_none(tr::marked));
}

auto Suggestions::setupMyChannels() -> std::unique_ptr<ObjectList> {
	const auto controller = lifetime().make_state<MyChannelsController>(
		_controller,
		_channelsQuery.value(),
		_joinedChannelsResults.value());

	auto result = setupObjectList(
		_channelsScroll.get(),
		_channelsContent,
		controller);
	const auto raw = result.get();
	const auto list = raw->wrap->entity();

	raw->selectJump = ListSelectJump(raw);

	raw->chosen.events(
	) | rpl::on_next([=] {
		_persist = false;
		if (!_channelsQuery.current().isEmpty()) {
			_clearSearchQueryRequests.fire({});
		}
	}, list->lifetime());

	return result;
}

auto Suggestions::setupRecommendations() -> std::unique_ptr<ObjectList> {
	const auto controller = lifetime().make_state<RecommendationsController>(
		_controller);

	const auto addToScroll = [=] {
		const auto wrap = _myChannels->wrap;
		return wrap->toggled() ? wrap->height() : 0;
	};
	auto result = setupObjectList(
		_channelsScroll.get(),
		_channelsContent,
		controller,
		addToScroll);
	const auto raw = result.get();
	const auto list = raw->wrap->entity();

	raw->selectJump = ListSelectJump(raw);

	raw->chosen.events(
	) | rpl::on_next([=] {
		_persist = true;
	}, list->lifetime());

	_key.value() | rpl::filter(
		rpl::mappers::_1 == Key{ Tab::Channels }
	) | rpl::on_next([=] {
		controller->load();
	}, list->lifetime());

	return result;
}

auto Suggestions::setupGlobalPeers(
	not_null<Ui::ElasticScroll*> scroll,
	not_null<Ui::VerticalLayout*> parent,
	rpl::producer<std::vector<not_null<PeerData*>>> peers,
	not_null<ObjectList*> above,
	bool expandable)
-> std::unique_ptr<ObjectList> {
	const auto controller = lifetime().make_state<GlobalPeersController>(
		_controller,
		std::move(peers),
		expandable);

	const auto addToScroll = [=] {
		const auto wrap = above->wrap;
		return wrap->toggled() ? wrap->height() : 0;
	};
	auto result = setupObjectList(scroll, parent, controller, addToScroll);
	const auto raw = result.get();
	const auto list = raw->wrap->entity();

	raw->selectJump = ListSelectJump(raw);

	raw->chosen.events(
	) | rpl::on_next([=] {
		_persist = true;
	}, list->lifetime());

	return result;
}

auto Suggestions::setupRecentApps() -> std::unique_ptr<ObjectList> {
	const auto controller = lifetime().make_state<RecentAppsController>(
		_controller);
	controller->setCloseCallback([=] {
		_closeRequests.fire({});
	});
	_recentAppsShows = [=](not_null<PeerData*> peer) {
		return controller->shown(peer);
	};
	_recentAppsRefreshed = controller->refreshed();

	auto result = setupObjectList(
		_appsScroll.get(),
		_appsContent,
		controller);
	const auto raw = result.get();
	const auto list = raw->wrap->entity();

	raw->selectJump = ListSelectJump(raw);

	raw->chosen.events(
	) | rpl::on_next([=] {
		_persist = false;
	}, list->lifetime());

	controller->load();

	return result;
}

auto Suggestions::setupPopularApps() -> std::unique_ptr<ObjectList> {
	const auto controller = lifetime().make_state<PopularAppsController>(
		_controller,
		_recentAppsShows,
		rpl::duplicate(_recentAppsRefreshed));

	const auto addToScroll = [=] {
		const auto wrap = _recentApps->wrap;
		return wrap->toggled() ? wrap->height() : 0;
	};
	auto result = setupObjectList(
		_appsScroll.get(),
		_appsContent,
		controller,
		addToScroll);
	const auto raw = result.get();
	const auto list = raw->wrap->entity();

	raw->selectJump = ListSelectJump(raw);

	raw->chosen.events(
	) | rpl::on_next([=] {
		_persist = true;
	}, list->lifetime());

	_key.value() | rpl::filter(
		rpl::mappers::_1 == Key{ Tab::Apps }
	) | rpl::on_next([=] {
		controller->load();
	}, list->lifetime());

	return result;
}

auto Suggestions::ListSelectJump(not_null<ObjectList*> raw)
-> Fn<JumpResult(Qt::Key, int)> {
	const auto list = raw->wrap->entity();
	return [=](Qt::Key direction, int pageSize) {
		const auto had = list->hasSelection();
		if (direction == Qt::Key()) {
			return had ? JumpResult::Applied : JumpResult::NotApplied;
		} else if (direction == Qt::Key_Up && !had) {
			if (pageSize < 0) {
				list->selectLast();
				return list->hasSelection()
					? JumpResult::Applied
					: JumpResult::NotApplied;
			}
			return JumpResult::NotApplied;
		} else if (direction == Qt::Key_Down || direction == Qt::Key_Up) {
			const auto was = list->selectedIndex();
			const auto delta = (direction == Qt::Key_Down) ? 1 : -1;
			if (pageSize > 0) {
				list->selectSkipPage(pageSize, delta);
			} else {
				list->selectSkip(delta);
			}
			if (had
				&& delta > 0
				&& raw->count.current()
				&& list->selectedIndex() == was) {
				list->clearSelection();
				return JumpResult::AppliedAndOut;
			}
			return list->hasSelection()
				? JumpResult::Applied
				: had
				? JumpResult::AppliedAndOut
				: JumpResult::NotApplied;
		}
		return JumpResult::NotApplied;
	};
}

auto Suggestions::setupObjectList(
	not_null<Ui::ElasticScroll*> scroll,
	not_null<Ui::VerticalLayout*> parent,
	not_null<ObjectListController*> controller,
	Fn<int()> addToScroll)
-> std::unique_ptr<ObjectList> {
	auto &lifetime = parent->lifetime();
	const auto delegate = lifetime.make_state<
		PeerListContentDelegateSimple
	>();
	controller->setStyleOverrides(&st::recentPeersList);

	auto content = object_ptr<PeerListContent>(parent, controller);
	const auto list = content.data();

	auto result = std::make_unique<ObjectList>(ObjectList{
		.wrap = parent->add(object_ptr<Ui::SlideWrap<PeerListContent>>(
			parent,
			std::move(content))),
	});
	const auto raw = result.get();

	raw->count = controller->count();
	raw->processTouch = [=](not_null<QTouchEvent*> e) {
		return controller->processTouchEvent(e);
	};

	controller->chosen(
	) | rpl::on_next([=](not_null<PeerData*> peer) {
		raw->chosen.fire_copy(peer);
	}, lifetime);

	raw->choose = [=] {
		return list->hasSelection() && list->submitted();
	};
	raw->updateFromParentDrag = [=](QPoint globalPosition) {
		return list->updateFromParentDrag(globalPosition);
	};
	raw->dragLeft = [=] {
		list->dragLeft();
	};

	list->scrollToRequests(
	) | rpl::on_next([=](Ui::ScrollToRequest request) {
		const auto add = addToScroll ? addToScroll() : 0;
		scroll->scrollToY(request.ymin + add, request.ymax + add);
	}, list->lifetime());

	delegate->setContent(list);
	controller->setDelegate(delegate);
	controller->setupTouchChatPreview(scroll);

	return result;
}

object_ptr<Ui::SlideWrap<>> Suggestions::setupEmptyChannels() {
	const auto icon = SearchEmptyIcon::NoResults;
	return setupEmpty(
		_channelsContent,
		_channelsScroll.get(),
		icon,
		rpl::conditional(
			_channelsQuery.value() | rpl::map([](const QString &query) {
				return query.isEmpty();
			}),
			tr::lng_channels_none_about(tr::marked),
			_channelsQuery.value() | rpl::map(NoResultsText)));
}

object_ptr<Ui::SlideWrap<>> Suggestions::setupEmpty(
		not_null<QWidget*> parent,
		not_null<Ui::ElasticScroll*> scroll,
		SearchEmptyIcon icon,
		rpl::producer<TextWithEntities> text) {
	auto content = object_ptr<SearchEmpty>(
		parent,
		icon,
		std::move(text));

	const auto raw = content.data();
	auto top = (scroll == _chatsScroll.get())
		? _topPeersWrap->heightValue()
		: rpl::single(0);
	rpl::combine(
		scroll->heightValue(),
		std::move(top)
	) | rpl::on_next([=](int height, int top) {
		raw->setMinimalHeight(height - top);
	}, raw->lifetime());

	auto result = object_ptr<Ui::SlideWrap<>>(
		parent,
		std::move(content));
	result->toggle(false, anim::type::instant);

	result->toggledValue() | rpl::filter([=](bool shown) {
		return shown && _controller->session().data().chatsListLoaded();
	}) | rpl::on_next([=] {
		raw->animate();
	}, raw->lifetime());

	return result;
}

bool Suggestions::persist() const {
	return _persist || _tabsOnly;
}

void Suggestions::clearPersistance() {
	_persist = false;
}

bool Suggestions::chatsTabActive() const {
	return (_key.current().tab == Tab::Chats);
}

void Suggestions::setTabsOnly(bool tabsOnly) {
	if (_tabsOnly == tabsOnly) {
		return;
	}
	_tabsOnly = tabsOnly;
	if (!_hidden) {
		finishShow();
	}
}

bool Suggestions::tabsOnly() const {
	return _tabsOnly;
}

int Suggestions::tabsHeight() const {
	return _tabs->height();
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
		) | rpl::on_next([=](const Data::PeerUpdate &update) {
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
		) | rpl::on_next([=] {
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

object_ptr<Ui::BoxContent> StarsExamplesBox(
		not_null<Window::SessionController*> window) {
	auto controller = std::make_unique<PopularAppsController>(
		window,
		nullptr,
		nullptr);
	const auto raw = controller.get();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->setTitle(tr::lng_credits_box_history_entry_gift_examples());
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});

		raw->load();
		raw->chosen() | rpl::on_next([=](not_null<PeerData*> peer) {
			if (const auto user = peer->asUser()) {
				if (const auto info = user->botInfo.get()) {
					if (info->hasMainApp) {
						window->session().attachWebView().open({
							.bot = user,
							.context = {
								.controller = window,
								.maySkipConfirmation = true,
							},
							.source = InlineBots::WebViewSourceBotProfile(),
						});
						return;
					}
				}
			}
			window->showPeerInfo(peer);
		}, box->lifetime());
	};
	return Box<PeerListBox>(std::move(controller), std::move(initBox));
}

object_ptr<Ui::BoxContent> PopularAppsAboutBox(
		not_null<Window::SessionController*> window) {
	return Ui::MakeInformBox({
		.text = tr::lng_popular_apps_info_text(
			lt_bot,
			rpl::single(tr::link(
				u"@botfather"_q,
				u"https://t.me/botfather"_q)),
			lt_link,
			tr::lng_popular_apps_info_here(
				tr::url(tr::lng_popular_apps_info_url(tr::now))),
			tr::rich),
		.confirmText = tr::lng_popular_apps_info_confirm(),
		.title = tr::lng_popular_apps_info_title(),
	});
}

} // namespace Dialogs
