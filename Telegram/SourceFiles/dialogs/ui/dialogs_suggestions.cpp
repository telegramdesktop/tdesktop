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
#include "base/qt/qt_key_modifiers.h"
#include "boxes/peer_list_box.h"
#include "core/application.h"
#include "data/components/recent_peers.h"
#include "data/components/top_peers.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_download_manager.h"
#include "data/data_folder.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/ui/chat_search_empty.h"
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
#include "storage/storage_shared_media.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/ripple_animation.h"
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
				st::dialogRowOpenBotTextStyle,
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
	if (_mainAppText && _badgeSize.isEmpty()) {
		return QSize(
			_mainAppText->maxWidth() + _mainAppText->minHeight(),
			st::dialogRowOpenBotHeight);
	}
	return _badgeSize;
}

QMargins RecentRow::rightActionMargins() const {
	if (_mainAppText && _badgeSize.isEmpty()) {
		return QMargins(
			0,
			st::dialogRowOpenBotRecentTop,
			st::dialogRowOpenBotRight,
			0);
	}
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
		const auto top = 0
			+ (st::dialogRowOpenBotHeight - _mainAppText->minHeight()) / 2;
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

	void setupPlainDivider(rpl::producer<QString> title);
	void setupExpandDivider(rpl::producer<QString> title);

private:
	const not_null<Window::SessionController*> _window;

	std::optional<QPoint> _chatPreviewTouchGlobal;
	rpl::event_stream<> _touchCancelRequests;
	rpl::event_stream<not_null<PeerData*>> _chosen;
	rpl::variable<int> _count;
	rpl::variable<Ui::RpWidget*> _toggleExpanded = nullptr;
	rpl::variable<bool> _expanded = false;

};

class RecentsController final : public Suggestions::ObjectListController {
public:
	using RightActionCallback = Fn<void(not_null<PeerData*>)>;

	RecentsController(
		not_null<Window::SessionController*> window,
		RecentPeersList list,
		RightActionCallback rightActionCallback);

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
	explicit MyChannelsController(
		not_null<Window::SessionController*> window);

	void prepare() override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

private:
	void appendRow(not_null<ChannelData*> channel);
	void fill(bool force = false);

	std::vector<not_null<History*>> _channels;
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

	History *_activeHistory = nullptr;
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
	History *_activeHistory = nullptr;
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
	_touchCancelRequests.events() | rpl::start_with_next([=] {
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

void Suggestions::ObjectListController::rowClicked(
		not_null<PeerListRow*> row) {
	_chosen.fire(row->peer());
}

void Suggestions::ObjectListController::rowMiddleClicked(
		not_null<PeerListRow*> row) {
	window()->showInNewWindow(row->peer());
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

RecentsController::RecentsController(
	not_null<Window::SessionController*> window,
	RecentPeersList list,
	RightActionCallback rightActionCallback)
: ObjectListController(window)
, _recent(std::move(list))
, _rightActionCallback(std::move(rightActionCallback)) {
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
	) | rpl::start_with_next([=] {
		for (auto i = 0; i != countCurrent(); ++i) {
			const auto row = delegate()->peerListRowAt(i);
			if (static_cast<RecentRow*>(row.get())->refreshBadge()) {
				delegate()->peerListUpdateRow(row);
			}
		}
	}, _lifetime);
}

MyChannelsController::MyChannelsController(
	not_null<Window::SessionController*> window)
: ObjectListController(window) {
}

void MyChannelsController::prepare() {
	setupExpandDivider(tr::lng_channels_your_title());

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
		setCount(_channels.size());
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
	setCount(_channels.size());

	expanded() | rpl::start_with_next([=] {
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
		const auto was = countCurrent();
		const auto now = int(_channels.size());
		if (was != now) {
			setCount(now);
			fill();
		}
	}, _lifetime);
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

RecommendationsController::RecommendationsController(
	not_null<Window::SessionController*> window)
: ObjectListController(window) {
}

void RecommendationsController::prepare() {
	setupPlainDivider(tr::lng_channels_recommended());
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
	setCount(delegate()->peerListFullRowsCount());

	window()->activeChatValue() | rpl::start_with_next([=](const Key &key) {
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

RecentAppsController::RecentAppsController(
	not_null<Window::SessionController*> window)
: ObjectListController(window) {
}

void RecentAppsController::prepare() {
	setupExpandDivider(tr::lng_bot_apps_your());

	_bots.reserve(kProbablyMaxApps);
	rpl::single() | rpl::then(
		session().topBotApps().updates()
	) | rpl::start_with_next([=] {
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

	expanded() | rpl::skip(1) | rpl::start_with_next([=] {
		fill();
	}, _lifetime);
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
			appendRow(_bots[i]);
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
	) | rpl::start_with_next([=] {
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
	) | rpl::take(1) | rpl::start_with_next([=] {
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
					) | Ui::Text::ToLink(u"internal:about_popular_apps"_q),
					Ui::Text::WithEntities),
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
, _emptyChannels(_channelsContent->add(setupEmptyChannels()))
, _appsScroll(std::make_unique<Ui::ElasticScroll>(this))
, _appsContent(
	_appsScroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this)))
, _recentApps(setupRecentApps())
, _popularApps(setupPopularApps())
, _searchQueryTimer([=] { applySearchQuery(); }) {
	setupTabs();
	setupChats();
	setupChannels();
	setupApps();
}

Suggestions::~Suggestions() = default;

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
	) | rpl::start_with_next([=](int was, int index) {
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
	) | rpl::start_with_next([=](int width, int height) {
		const auto line = st::lineWidth;
		shadow->setGeometry(0, height - line, width, line);
	}, shadow->lifetime());

	shadow->showOn(_tabsScroll->shownValue());

	const auto labels = base::flat_map<Key, QString>{
		{ Key{ Tab::Chats }, tr::lng_recent_chats(tr::now) },
		{ Key{ Tab::Channels }, tr::lng_recent_channels(tr::now) },
		{ Key{ Tab::Apps }, tr::lng_recent_apps(tr::now) },
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
	auto sections = std::vector<QString>();
	for (const auto key : _tabKeys) {
		const auto i = labels.find(key);
		Assert(i != end(labels));
		sections.push_back(i->second);
	}
	_tabs->setSections(sections);
	_tabs->sectionActivated(
	) | rpl::start_with_next([=](int section) {
		Assert(section >= 0 && section < _tabKeys.size());
		switchTab(_tabKeys[section]);
	}, _tabs->lifetime());
}

void Suggestions::setupChats() {
	_recent->count.value() | rpl::start_with_next([=](int count) {
		_recent->wrap->toggle(count > 0, anim::type::instant);
		_emptyRecent->toggle(count == 0, anim::type::instant);
	}, _recent->wrap->lifetime());

	_topPeers->emptyValue() | rpl::start_with_next([=](bool empty) {
		_topPeersWrap->toggle(!empty, anim::type::instant);
	}, _topPeers->lifetime());

	_topPeers->clicks() | rpl::start_with_next([=](uint64 peerIdRaw) {
		const auto peerId = PeerId(peerIdRaw);
		_topPeerChosen.fire(_controller->session().data().peer(peerId));
	}, _topPeers->lifetime());

	_topPeers->pressed() | rpl::start_with_next([=](uint64 peerIdRaw) {
		handlePressForChatPreview(PeerId(peerIdRaw), [=](bool shown) {
			_topPeers->pressLeftToContextMenu(shown);
		});
	}, _topPeers->lifetime());

	_topPeers->pressCancelled() | rpl::start_with_next([=] {
		_controller->cancelScheduledPreview();
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
				tr::now,
				Ui::Text::FixAmpersandInAction),
			.removeAllConfirm = tr::lng_recent_hide_sure(tr::now),
			.removeAll = removeAll,
		});
	}, _topPeers->lifetime());

	_topPeers->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		_chatsScroll->scrollToY(request.ymin, request.ymax);
	}, _topPeers->lifetime());

	_topPeers->verticalScrollEvents(
	) | rpl::start_with_next([=](not_null<QWheelEvent*> e) {
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
	_myChannels->count.value() | rpl::start_with_next([=](int count) {
		_myChannels->wrap->toggle(count > 0, anim::type::instant);
	}, _myChannels->wrap->lifetime());

	_recommendations->count.value() | rpl::start_with_next([=](int count) {
		_recommendations->wrap->toggle(count > 0, anim::type::instant);
	}, _recommendations->wrap->lifetime());

	_emptyChannels->toggleOn(
		rpl::combine(
			_myChannels->count.value(),
			_recommendations->count.value(),
			rpl::mappers::_1 + rpl::mappers::_2 == 0),
		anim::type::instant);

	_channelsScroll->setVisible(_key.current().tab == Tab::Channels);
	_channelsScroll->setCustomTouchProcess([=](not_null<QTouchEvent*> e) {
		const auto myChannels = _myChannels->processTouch(e);
		const auto recommendations = _recommendations->processTouch(e);
		return myChannels || recommendations;
	});
}

void Suggestions::setupApps() {
	_recentApps->count.value() | rpl::start_with_next([=](int count) {
		_recentApps->wrap->toggle(count > 0, anim::type::instant);
	}, _recentApps->wrap->lifetime());

	_popularApps->count.value() | rpl::start_with_next([=](int count) {
		_popularApps->wrap->toggle(count > 0, anim::type::instant);
	}, _popularApps->wrap->lifetime());

	_appsScroll->setVisible(_key.current().tab == Tab::Apps);
	_appsScroll->setCustomTouchProcess([=](not_null<QTouchEvent*> e) {
		const auto recentApps = _recentApps->processTouch(e);
		const auto popularApps = _popularApps->processTouch(e);
		return recentApps || popularApps;
	});
}

void Suggestions::selectJump(Qt::Key direction, int pageSize) {
	switch (_key.current().tab) {
	case Tab::Chats: selectJumpChats(direction, pageSize); return;
	case Tab::Channels: selectJumpChannels(direction, pageSize); return;
	case Tab::Apps: selectJumpApps(direction, pageSize); return;
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
	const auto myChannelsHasSelection = [=] {
		return _myChannels->selectJump({}, 0) == JumpResult::Applied;
	};
	const auto recommendationsHasSelection = [=] {
		return _recommendations->selectJump({}, 0) == JumpResult::Applied;
	};
	if (pageSize) {
		if (direction == Qt::Key_Down) {
			if (recommendationsHasSelection()) {
				_recommendations->selectJump(direction, pageSize);
			} else if (myChannelsHasSelection()) {
				if (_myChannels->selectJump(direction, pageSize)
					== JumpResult::AppliedAndOut) {
					_recommendations->selectJump(direction, 0);
				}
			} else if (_myChannels->count.current()) {
				_myChannels->selectJump(direction, 0);
				_myChannels->selectJump(direction, pageSize);
			} else if (_recommendations->count.current()) {
				_recommendations->selectJump(direction, 0);
				_recommendations->selectJump(direction, pageSize);
			}
		} else if (direction == Qt::Key_Up) {
			if (myChannelsHasSelection()) {
				if (_myChannels->selectJump(direction, pageSize)
					== JumpResult::AppliedAndOut) {
					_channelsScroll->scrollTo(0);
				}
			} else if (recommendationsHasSelection()) {
				if (_recommendations->selectJump(direction, pageSize)
					== JumpResult::AppliedAndOut) {
					_myChannels->selectJump(direction, -1);
				}
			}
		}
	} else if (direction == Qt::Key_Up) {
		if (myChannelsHasSelection()) {
			_myChannels->selectJump(direction, 0);
		} else if (_recommendations->selectJump(direction, 0)
			== JumpResult::AppliedAndOut) {
			_myChannels->selectJump(direction, -1);
		} else if (!recommendationsHasSelection()) {
			if (_myChannels->selectJump(direction, 0)
				== JumpResult::AppliedAndOut) {
				_channelsScroll->scrollTo(0);
			}
		}
	} else if (direction == Qt::Key_Down) {
		if (recommendationsHasSelection()) {
			_recommendations->selectJump(direction, 0);
		} else if (_myChannels->selectJump(direction, 0)
			== JumpResult::AppliedAndOut) {
			_recommendations->selectJump(direction, 0);
		} else if (!myChannelsHasSelection()) {
			if (_recommendations->selectJump(direction, 0)
				== JumpResult::AppliedAndOut) {
				_myChannels->selectJump(direction, 0);
			}
		}
	}
}

void Suggestions::selectJumpApps(Qt::Key direction, int pageSize) {
	const auto recentAppsHasSelection = [=] {
		return _recentApps->selectJump({}, 0) == JumpResult::Applied;
	};
	const auto popularAppsHasSelection = [=] {
		return _popularApps->selectJump({}, 0) == JumpResult::Applied;
	};
	if (pageSize) {
		if (direction == Qt::Key_Down) {
			if (popularAppsHasSelection()) {
				_popularApps->selectJump(direction, pageSize);
			} else if (recentAppsHasSelection()) {
				if (_recentApps->selectJump(direction, pageSize)
					== JumpResult::AppliedAndOut) {
					_popularApps->selectJump(direction, 0);
				}
			} else if (_recentApps->count.current()) {
				_recentApps->selectJump(direction, 0);
				_recentApps->selectJump(direction, pageSize);
			} else if (_popularApps->count.current()) {
				_popularApps->selectJump(direction, 0);
				_popularApps->selectJump(direction, pageSize);
			}
		} else if (direction == Qt::Key_Up) {
			if (recentAppsHasSelection()) {
				if (_recentApps->selectJump(direction, pageSize)
					== JumpResult::AppliedAndOut) {
					_channelsScroll->scrollTo(0);
				}
			} else if (popularAppsHasSelection()) {
				if (_popularApps->selectJump(direction, pageSize)
					== JumpResult::AppliedAndOut) {
					_recentApps->selectJump(direction, -1);
				}
			}
		}
	} else if (direction == Qt::Key_Up) {
		if (recentAppsHasSelection()) {
			_recentApps->selectJump(direction, 0);
		} else if (_popularApps->selectJump(direction, 0)
			== JumpResult::AppliedAndOut) {
			_recentApps->selectJump(direction, -1);
		} else if (!popularAppsHasSelection()) {
			if (_recentApps->selectJump(direction, 0)
				== JumpResult::AppliedAndOut) {
				_channelsScroll->scrollTo(0);
			}
		}
	} else if (direction == Qt::Key_Down) {
		if (popularAppsHasSelection()) {
			_popularApps->selectJump(direction, 0);
		} else if (_recentApps->selectJump(direction, 0)
			== JumpResult::AppliedAndOut) {
			_popularApps->selectJump(direction, 0);
		} else if (!recentAppsHasSelection()) {
			if (_popularApps->selectJump(direction, 0)
				== JumpResult::AppliedAndOut) {
				_recentApps->selectJump(direction, 0);
			}
		}
	}
}

void Suggestions::chooseRow() {
	switch (_key.current().tab) {
	case Tab::Chats:
		if (!_topPeers->chooseRow()) {
			_recent->choose();
		}
		break;
	case Tab::Channels:
		if (!_myChannels->choose()) {
			_recommendations->choose();
		}
		break;
	case Tab::Apps:
		if (!_recentApps->choose()) {
			_popularApps->choose();
		}
		break;
	}
}

bool Suggestions::consumeSearchQuery(const QString &query) {
	using Type = MediaType;
	const auto key = _key.current();
	const auto tab = key.tab;
	const auto type = (key.tab == Tab::Media) ? key.mediaType : Type::kCount;
	if (tab != Tab::Downloads
		&& type != Type::File
		&& type != Type::Link
		&& type != Type::MusicFile) {
		return false;
	} else if (_searchQuery == query) {
		return false;
	}
	_searchQuery = query;
	_persist = !_searchQuery.isEmpty();
	if (query.isEmpty() || tab == Tab::Downloads) {
		_searchQueryTimer.cancel();
		applySearchQuery();
	} else {
		_searchQueryTimer.callOnce(kSearchQueryDelay);
	}
	return true;
}

void Suggestions::applySearchQuery() {
	const auto key = _key.current();
	const auto controller = _mediaLists[key].wrap->controller();
	const auto search = controller->searchFieldController();
	if (search->query() != _searchQuery) {
		search->setQuery(_searchQuery);
	}
}

rpl::producer<> Suggestions::clearSearchQueryRequests() const {
	return _clearSearchQueryRequests.events();
}

Data::Thread *Suggestions::updateFromParentDrag(QPoint globalPosition) {
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
	return fromListId(_recommendations->updateFromParentDrag(globalPosition));
}

Data::Thread *Suggestions::updateFromAppsDrag(QPoint globalPosition) {
	if (const auto id = _recentApps->updateFromParentDrag(globalPosition)) {
		return fromListId(id);
	}
	return fromListId(_popularApps->updateFromParentDrag(globalPosition));
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
	_recentApps->dragLeft();
	_popularApps->dragLeft();
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
	consumeSearchQuery(QString());
	_key = key;
	_persist = false;
	_clearSearchQueryRequests.fire({});
	if (_tabs->isHidden()) {
		return;
	}
	startSlideAnimation(was, key);
}

void Suggestions::ensureContent(Key key) {
	if (key.tab != Tab::Downloads && key.tab != Tab::Media) {
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
	list.wrap->show();
	updateControlsGeometry();
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
	for (const auto &[key, list] : _mediaLists) {
		list.wrap->hide();
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
	_chatsScroll->setVisible(key == Key{ Tab::Chats });
	_channelsScroll->setVisible(key == Key{ Tab::Channels });
	_appsScroll->setVisible(key == Key{ Tab::Apps });
	for (const auto &[mediaKey, list] : _mediaLists) {
		list.wrap->setVisible(key == mediaKey);
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

	const auto content = QRect(0, tabs, w, height() - tabs);

	_chatsScroll->setGeometry(content);
	_chatsContent->resizeToWidth(w);

	_channelsScroll->setGeometry(content);
	_channelsContent->resizeToWidth(w);

	_appsScroll->setGeometry(content);
	_appsContent->resizeToWidth(w);

	const auto expanding = false;
	for (const auto &[key, list] : _mediaLists) {
		const auto full = !list.wrap->scrollBottomSkip();
		const auto additionalScroll = (full ? st::boxRadius : 0);
		const auto height = content.height() - (full ? 0 : st::boxRadius);
		const auto wrapGeometry = QRect{ 0, tabs, w, height};
		list.wrap->updateGeometry(
			wrapGeometry,
			expanding,
			additionalScroll,
			content.height());
	}
}

auto Suggestions::setupRecentPeers(RecentPeersList recentPeers)
-> std::unique_ptr<ObjectList> {
	const auto controller = lifetime().make_state<RecentsController>(
		_controller,
		std::move(recentPeers),
		[=](not_null<PeerData*> p) { _openBotMainAppRequests.fire_copy(p); });

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
	) | rpl::start_with_next([=](not_null<PeerData*> peer) {
		_controller->session().recentPeers().bump(peer);
	}, list->lifetime());

	return result;
}

object_ptr<Ui::SlideWrap<>> Suggestions::setupEmptyRecent() {
	const auto icon = SearchEmptyIcon::Search;
	return setupEmpty(_chatsContent, icon, tr::lng_recent_none());
}

auto Suggestions::setupMyChannels() -> std::unique_ptr<ObjectList> {
	const auto controller = lifetime().make_state<MyChannelsController>(
		_controller);

	auto result = setupObjectList(
		_channelsScroll.get(),
		_channelsContent,
		controller);
	const auto raw = result.get();
	const auto list = raw->wrap->entity();

	raw->selectJump = [=](Qt::Key direction, int pageSize) {
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

	raw->chosen.events(
	) | rpl::start_with_next([=] {
		_persist = false;
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
	) | rpl::start_with_next([=] {
		_persist = true;
	}, list->lifetime());

	_key.value() | rpl::filter(
		rpl::mappers::_1 == Key{ Tab::Channels }
	) | rpl::start_with_next([=] {
		controller->load();
	}, list->lifetime());

	return result;
}

auto Suggestions::setupRecentApps() -> std::unique_ptr<ObjectList> {
	const auto controller = lifetime().make_state<RecentAppsController>(
		_controller);
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

	raw->selectJump = [=](Qt::Key direction, int pageSize) {
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

	raw->chosen.events(
	) | rpl::start_with_next([=] {
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
	) | rpl::start_with_next([=] {
		_persist = true;
	}, list->lifetime());

	_key.value() | rpl::filter(
		rpl::mappers::_1 == Key{ Tab::Apps }
	) | rpl::start_with_next([=] {
		controller->load();
	}, list->lifetime());

	return result;
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
	) | rpl::start_with_next([=](not_null<PeerData*> peer) {
		raw->chosen.fire_copy(peer);
	}, lifetime);

	raw->choose = [=] {
		return list->submitted();
	};
	raw->updateFromParentDrag = [=](QPoint globalPosition) {
		return list->updateFromParentDrag(globalPosition);
	};
	raw->dragLeft = [=] {
		list->dragLeft();
	};

	list->scrollToRequests(
	) | rpl::start_with_next([=](Ui::ScrollToRequest request) {
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
	return setupEmpty(_channelsContent, icon, tr::lng_channels_none_about());
}

object_ptr<Ui::SlideWrap<>> Suggestions::setupEmpty(
		not_null<QWidget*> parent,
		SearchEmptyIcon icon,
		rpl::producer<QString> text) {
	auto content = object_ptr<SearchEmpty>(
		parent,
		icon,
		std::move(text) | Ui::Text::ToWithEntities());

	const auto raw = content.data();
	rpl::combine(
		_chatsScroll->heightValue(),
		_topPeersWrap->heightValue()
	) | rpl::start_with_next([=](int height, int top) {
		raw->setMinimalHeight(height - top);
	}, raw->lifetime());

	auto result = object_ptr<Ui::SlideWrap<>>(
		parent,
		std::move(content));
	result->toggle(false, anim::type::instant);

	result->toggledValue() | rpl::filter([=](bool shown) {
		return shown && _controller->session().data().chatsListLoaded();
	}) | rpl::start_with_next([=] {
		raw->animate();
	}, raw->lifetime());

	return result;
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
		raw->chosen() | rpl::start_with_next([=](not_null<PeerData*> peer) {
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
			rpl::single(Ui::Text::Link(
				u"@botfather"_q,
				u"https://t.me/botfather"_q)),
			lt_link,
			tr::lng_popular_apps_info_here(
			) | Ui::Text::ToLink(tr::lng_popular_apps_info_url(tr::now)),
			Ui::Text::RichLangValue),
		.confirmText = tr::lng_popular_apps_info_confirm(),
		.title = tr::lng_popular_apps_info_title(),
	});
}

} // namespace Dialogs
