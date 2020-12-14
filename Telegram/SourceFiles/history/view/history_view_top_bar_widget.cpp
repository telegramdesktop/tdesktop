/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_top_bar_widget.h"

#include <rpl/combine.h>
#include <rpl/combine_previous.h>
#include "history/history.h"
#include "history/view/history_view_send_action.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "storage/storage_shared_media.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "lang/lang_keys.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/effects/radial_animation.h"
#include "ui/toasts/common_toasts.h"
#include "ui/special_buttons.h"
#include "ui/unread_badge.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "calls/calls_instance.h"
#include "data/data_peer_values.h"
#include "data/data_group_call.h" // GroupCall::input.
#include "data/data_folder.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "base/unixtime.h"
#include "support/support_helper.h"
#include "apiwrap.h"
#include "facades.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"

namespace HistoryView {

TopBarWidget::TopBarWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _clear(this, tr::lng_selected_clear(), st::topBarClearButton)
, _forward(this, tr::lng_selected_forward(), st::defaultActiveButton)
, _sendNow(this, tr::lng_selected_send_now(), st::defaultActiveButton)
, _delete(this, tr::lng_selected_delete(), st::defaultActiveButton)
, _back(this, st::historyTopBarBack)
, _call(this, st::topBarCall)
, _groupCall(this, st::topBarGroupCall)
, _search(this, st::topBarSearch)
, _infoToggle(this, st::topBarInfo)
, _menuToggle(this, st::topBarMenuToggle)
, _titlePeerText(st::windowMinWidth / 3)
, _onlineUpdater([=] { updateOnlineDisplay(); }) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	_forward->setClickedCallback([=] { _forwardSelection.fire({}); });
	_forward->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_sendNow->setClickedCallback([=] { _sendNowSelection.fire({}); });
	_sendNow->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_delete->setClickedCallback([=] { _deleteSelection.fire({}); });
	_delete->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_clear->setClickedCallback([=] { _clearSelection.fire({}); });
	_call->setClickedCallback([=] { call(); });
	_groupCall->setClickedCallback([=] { groupCall(); });
	_search->setClickedCallback([=] { search(); });
	_menuToggle->setClickedCallback([=] { showMenu(); });
	_infoToggle->setClickedCallback([=] { toggleInfoSection(); });
	_back->addClickHandler([=] { backClicked(); });

	rpl::combine(
		_controller->activeChatValue(),
		_controller->searchInChat.value()
	) | rpl::combine_previous(
		std::make_tuple(Dialogs::Key(), Dialogs::Key())
	) | rpl::map([](
			const std::tuple<Dialogs::Key, Dialogs::Key> &previous,
			const std::tuple<Dialogs::Key, Dialogs::Key> &current) {
		const auto &active = std::get<0>(current);
		const auto &search = std::get<1>(current);
		const auto activeChanged = (active != std::get<0>(previous));
		const auto searchInChat = search && (active == search);
		return std::make_tuple(searchInChat, activeChanged);
	}) | rpl::start_with_next([=](
			bool searchInActiveChat,
			bool activeChanged) {
		auto animated = activeChanged
			? anim::type::instant
			: anim::type::normal;
		_search->setForceRippled(searchInActiveChat, animated);
	}, lifetime());

	subscribe(Adaptive::Changed(), [=] { updateAdaptiveLayout(); });
	refreshUnreadBadge();
	{
		using AnimationUpdate = Data::Session::SendActionAnimationUpdate;
		session().data().sendActionAnimationUpdated(
		) | rpl::filter([=](const AnimationUpdate &update) {
			return (update.history == _activeChat.key.history());
		}) | rpl::start_with_next([=] {
			update();
		}, lifetime());
	}

	using UpdateFlag = Data::PeerUpdate::Flag;
	session().changes().peerUpdates(
		UpdateFlag::HasCalls
		| UpdateFlag::OnlineStatus
		| UpdateFlag::Members
		| UpdateFlag::SupportInfo
		| UpdateFlag::Rights
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		if (update.flags & UpdateFlag::HasCalls) {
			if (update.peer->isUser()
				&& (update.peer->isSelf()
					|| _activeChat.key.peer() == update.peer)) {
				updateControlsVisibility();
			}
		} else if ((update.flags & UpdateFlag::Rights)
			&& (_activeChat.key.peer() == update.peer)) {
			updateControlsVisibility();
		}
		if (update.flags
			& (UpdateFlag::OnlineStatus
				| UpdateFlag::Members
				| UpdateFlag::SupportInfo)) {
			updateOnlineDisplay();
		}
	}, lifetime());

	session().serverConfig().phoneCallsEnabled.changes(
	) | rpl::start_with_next([=] {
		updateControlsVisibility();
	}, lifetime());

	rpl::combine(
		Core::App().settings().thirdSectionInfoEnabledValue(),
		Core::App().settings().tabbedReplacedWithInfoValue()
	) | rpl::start_with_next([=] {
		updateInfoToggleActive();
	}, lifetime());

	rpl::single(rpl::empty_value()) | rpl::then(
		base::ObservableViewer(Global::RefConnectionTypeChanged())
	) | rpl::start_with_next([=] {
		updateConnectingState();
	}, lifetime());

	setCursor(style::cur_pointer);
}

TopBarWidget::~TopBarWidget() = default;

Main::Session &TopBarWidget::session() const {
	return _controller->session();
}

void TopBarWidget::updateConnectingState() {
	const auto state = _controller->session().mtp().dcstate();
	if (state == MTP::ConnectedState) {
		if (_connecting) {
			_connecting = nullptr;
			update();
		}
	} else if (!_connecting) {
		_connecting = std::make_unique<Ui::InfiniteRadialAnimation>(
			[=] { connectingAnimationCallback(); },
			st::topBarConnectingAnimation);
		_connecting->start();
		update();
	}
}

void TopBarWidget::connectingAnimationCallback() {
	if (!anim::Disabled()) {
		update();
	}
}

void TopBarWidget::refreshLang() {
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

void TopBarWidget::search() {
	if (_activeChat.key) {
		_controller->content()->searchInChat(_activeChat.key);
	}
}

void TopBarWidget::call() {
	if (const auto peer = _activeChat.key.peer()) {
		if (const auto user = peer->asUser()) {
			Core::App().calls().startOutgoingCall(user, false);
		}
	}
}

void TopBarWidget::groupCall() {
	if (const auto peer = _activeChat.key.peer()) {
		_controller->startOrJoinGroupCall(peer);
	}
}

void TopBarWidget::showMenu() {
	if (!_activeChat.key || _menu) {
		return;
	}
	_menu.create(parentWidget());
	_menu->setHiddenCallback([weak = Ui::MakeWeak(this), menu = _menu.data()]{
		menu->deleteLater();
		if (weak && weak->_menu == menu) {
			weak->_menu = nullptr;
			weak->_menuToggle->setForceRippled(false);
		}
	});
	_menu->setShowStartCallback(crl::guard(this, [this, menu = _menu.data()]{
		if (_menu == menu) {
			_menuToggle->setForceRippled(true);
		}
	}));
	_menu->setHideStartCallback(crl::guard(this, [this, menu = _menu.data()]{
		if (_menu == menu) {
			_menuToggle->setForceRippled(false);
		}
	}));
	_menuToggle->installEventFilter(_menu);
	const auto addAction = [&](
		const QString &text,
		Fn<void()> callback) {
		return _menu->addAction(text, std::move(callback));
	};
	Window::FillDialogsEntryMenu(
		_controller,
		_activeChat,
		addAction);
	if (_menu->actions().empty()) {
		_menu.destroy();
	} else {
		_menu->moveToRight((parentWidget()->width() - width()) + st::topBarMenuPosition.x(), st::topBarMenuPosition.y());
		_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
	}
}

void TopBarWidget::toggleInfoSection() {
	if (Adaptive::ThreeColumn()
		&& (Core::App().settings().thirdSectionInfoEnabled()
			|| Core::App().settings().tabbedReplacedWithInfo())) {
		_controller->closeThirdSection();
	} else if (_activeChat.key.peer()) {
		if (_controller->canShowThirdSection()) {
			Core::App().settings().setThirdSectionInfoEnabled(true);
			Core::App().saveSettingsDelayed();
			if (Adaptive::ThreeColumn()) {
				_controller->showSection(
					Info::Memento::Default(_activeChat.key.peer()),
					Window::SectionShow().withThirdColumn());
			} else {
				_controller->resizeForThirdSection();
				_controller->updateColumnLayout();
			}
		} else {
			infoClicked();
		}
	} else {
		updateControlsVisibility();
	}
}

bool TopBarWidget::eventFilter(QObject *obj, QEvent *e) {
	if (obj == _membersShowArea) {
		switch (e->type()) {
		case QEvent::MouseButtonPress:
			mousePressEvent(static_cast<QMouseEvent*>(e));
			return true;

		case QEvent::Enter:
			_membersShowAreaActive.fire(true);
			break;

		case QEvent::Leave:
			_membersShowAreaActive.fire(false);
			break;
		}
	}
	return RpWidget::eventFilter(obj, e);
}

int TopBarWidget::resizeGetHeight(int newWidth) {
	return st::topBarHeight;
}

void TopBarWidget::paintEvent(QPaintEvent *e) {
	if (_animatingMode) {
		return;
	}
	Painter p(this);

	auto hasSelected = (_selectedCount > 0);
	auto selectedButtonsTop = countSelectedButtonsTop(_selectedShown.value(hasSelected ? 1. : 0.));

	p.fillRect(QRect(0, 0, width(), st::topBarHeight), st::topBarBg);
	if (selectedButtonsTop < 0) {
		p.translate(0, selectedButtonsTop + st::topBarHeight);
		paintTopBar(p);
	}
}

void TopBarWidget::paintTopBar(Painter &p) {
	if (!_activeChat.key) {
		return;
	}
	auto nameleft = _leftTaken;
	auto nametop = st::topBarArrowPadding.top();
	auto statustop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	auto availableWidth = width() - _rightTaken - nameleft;

	const auto history = _activeChat.key.history();
	const auto folder = _activeChat.key.folder();
	if (folder
		|| history->peer->sharedMediaInfo()
		|| (_activeChat.section == Section::Scheduled)
		|| (_activeChat.section == Section::Pinned)) {
		// #TODO feed name emoji.
		auto text = (_activeChat.section == Section::Scheduled)
			? ((history && history->peer->isSelf())
				? tr::lng_reminder_messages(tr::now)
				: tr::lng_scheduled_messages(tr::now))
			: (_activeChat.section == Section::Pinned)
			? _customTitleText
			: folder
			? folder->chatListName()
			: history->peer->isSelf()
			? tr::lng_saved_messages(tr::now)
			: tr::lng_replies_messages(tr::now);
		const auto textWidth = st::historySavedFont->width(text);
		if (availableWidth < textWidth) {
			text = st::historySavedFont->elided(text, availableWidth);
		}
		p.setPen(st::dialogsNameFg);
		p.setFont(st::historySavedFont);
		p.drawTextLeft(
			nameleft,
			(height() - st::historySavedFont->height) / 2,
			width(),
			text);
	} else if (_activeChat.section == Section::Replies) {
		p.setPen(st::dialogsNameFg);
		p.setFont(st::semiboldFont);
		p.drawTextLeft(
			nameleft,
			nametop,
			width(),
			tr::lng_manage_discussion_group(tr::now));

		p.setFont(st::dialogsTextFont);
		if (!paintConnectingState(p, nameleft, statustop, width())
			&& !_sendAction->paint(
				p,
				nameleft,
				statustop,
				availableWidth,
				width(),
				st::historyStatusFgTyping,
				crl::now())) {
			p.setPen(st::historyStatusFg);
			p.drawTextLeft(nameleft, statustop, width(), _customTitleText);
		}
	} else if (const auto history = _activeChat.key.history()) {
		const auto peer = history->peer;
		const auto &text = peer->topBarNameText();
		const auto badgeStyle = Ui::PeerBadgeStyle{
			nullptr,
			&st::attentionButtonFg };
		const auto badgeWidth = Ui::DrawPeerBadgeGetWidth(
			peer,
			p,
			QRect(
				nameleft,
				nametop,
				availableWidth,
				st::msgNameStyle.font->height),
			text.maxWidth(),
			width(),
			badgeStyle);
		const auto namewidth = availableWidth - badgeWidth;

		p.setPen(st::dialogsNameFg);
		peer->topBarNameText().drawElided(
			p,
			nameleft,
			nametop,
			namewidth);

		p.setFont(st::dialogsTextFont);
		if (!paintConnectingState(p, nameleft, statustop, width())
			&& !_sendAction->paint(
				p,
				nameleft,
				statustop,
				availableWidth,
				width(),
				st::historyStatusFgTyping,
				crl::now())) {
			paintStatus(p, nameleft, statustop, availableWidth, width());
		}
	}
}

bool TopBarWidget::paintConnectingState(
		Painter &p,
		int left,
		int top,
		int outerWidth) {
	if (!_connecting) {
		return false;
	}
	_connecting->draw(
		p,
		{
			st::topBarConnectingPosition.x() + left,
			st::topBarConnectingPosition.y() + top
		},
		outerWidth);
	left += st::topBarConnectingPosition.x()
		+ st::topBarConnectingAnimation.size.width()
		+ st::topBarConnectingSkip;
	p.setPen(st::historyStatusFg);
	p.drawTextLeft(left, top, outerWidth, tr::lng_status_connecting(tr::now));
	return true;
}

void TopBarWidget::paintStatus(
		Painter &p,
		int left,
		int top,
		int availableWidth,
		int outerWidth) {
	p.setPen(_titlePeerTextOnline
		? st::historyStatusFgActive
		: st::historyStatusFg);
	_titlePeerText.drawLeftElided(p, left, top, availableWidth, outerWidth);
}

QRect TopBarWidget::getMembersShowAreaGeometry() const {
	int membersTextLeft = _leftTaken;
	int membersTextTop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	int membersTextWidth = _titlePeerText.maxWidth();
	int membersTextHeight = st::topBarHeight - membersTextTop;

	return myrtlrect(membersTextLeft, membersTextTop, membersTextWidth, membersTextHeight);
}

void TopBarWidget::mousePressEvent(QMouseEvent *e) {
	auto handleClick = (e->button() == Qt::LeftButton)
		&& (e->pos().y() < st::topBarHeight)
		&& (!_selectedCount);
	if (handleClick) {
		if (_animatingMode && _back->rect().contains(e->pos())) {
			backClicked();
		} else  {
			infoClicked();
		}
	}
}

void TopBarWidget::infoClicked() {
	const auto key = _activeChat.key;
	if (!key) {
		return;
	} else if (key.folder()) {
		_controller->closeFolder();
	//} else if (const auto feed = _activeChat.feed()) { // #feed
	//	_controller->showSection(std::make_shared<Info::Memento>(
	//		feed,
	//		Info::Section(Info::Section::Type::Profile)));
	} else if (key.peer()->isSelf()) {
		_controller->showSection(std::make_shared<Info::Memento>(
			key.peer(),
			Info::Section(Storage::SharedMediaType::Photo)));
	} else if (key.peer()->isRepliesChat()) {
		_controller->showSection(std::make_shared<Info::Memento>(
			key.peer(),
			Info::Section(Storage::SharedMediaType::Photo)));
	} else {
		_controller->showPeerInfo(key.peer());
	}
}

void TopBarWidget::backClicked() {
	if (_activeChat.key.folder()) {
		_controller->closeFolder();
	} else {
		_controller->showBackFromStack();
	}
}

void TopBarWidget::setActiveChat(
		ActiveChat activeChat,
		SendActionPainter *sendAction) {
	if (_activeChat.key == activeChat.key
		&& _activeChat.section == activeChat.section) {
		_activeChat = activeChat;
		return;
	}
	_activeChat = activeChat;
	_sendAction = sendAction;
	_titlePeerText.clear();
	_back->clearState();
	update();

	updateUnreadBadge();
	refreshInfoButton();
	if (_menu) {
		_menuToggle->removeEventFilter(_menu);
		_menu->hideFast();
	}
	updateOnlineDisplay();
	updateControlsVisibility();
	refreshUnreadBadge();
}

void TopBarWidget::setCustomTitle(const QString &title) {
	if (_customTitleText != title) {
		_customTitleText = title;
		update();
	}
}

void TopBarWidget::refreshInfoButton() {
	if (const auto peer = _activeChat.key.peer()) {
		auto info = object_ptr<Ui::UserpicButton>(
			this,
			_controller,
			peer,
			Ui::UserpicButton::Role::Custom,
			st::topBarInfoButton);
		info->showSavedMessagesOnSelf(true);
		_info.destroy();
		_info = std::move(info);
	//} else if (const auto feed = _activeChat.feed()) { // #feed
	//	_info.destroy();
	//	_info = object_ptr<Ui::FeedUserpicButton>(
	//		this,
	//		_controller,
	//		feed,
	//		st::topBarFeedInfoButton);
	}
	if (_info) {
		_info->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
}

void TopBarWidget::resizeEvent(QResizeEvent *e) {
	updateSearchVisibility();
	updateControlsGeometry();
}

int TopBarWidget::countSelectedButtonsTop(float64 selectedShown) {
	return (1. - selectedShown) * (-st::topBarHeight);
}

void TopBarWidget::updateSearchVisibility() {
	const auto historyMode = (_activeChat.section == Section::History);
	const auto smallDialogsColumn = _activeChat.key.folder()
		&& (width() < _back->width() + _search->width());
	_search->setVisible(historyMode && !smallDialogsColumn);
}

void TopBarWidget::updateControlsGeometry() {
	if (!_activeChat.key) {
		return;
	}
	auto hasSelected = (_selectedCount > 0);
	auto selectedButtonsTop = countSelectedButtonsTop(_selectedShown.value(hasSelected ? 1. : 0.));
	auto otherButtonsTop = selectedButtonsTop + st::topBarHeight;
	auto buttonsLeft = st::topBarActionSkip + (Adaptive::OneColumn() ? 0 : st::lineWidth);
	auto buttonsWidth = (_forward->isHidden() ? 0 : _forward->contentWidth())
		+ (_sendNow->isHidden() ? 0 : _sendNow->contentWidth())
		+ (_delete->isHidden() ? 0 : _delete->contentWidth())
		+ _clear->width();
	buttonsWidth += buttonsLeft + st::topBarActionSkip * 3;

	auto widthLeft = qMin(width() - buttonsWidth, -2 * st::defaultActiveButton.width);
	auto buttonFullWidth = qMin(-(widthLeft / 2), 0);
	_forward->setFullWidth(buttonFullWidth);
	_sendNow->setFullWidth(buttonFullWidth);
	_delete->setFullWidth(buttonFullWidth);

	selectedButtonsTop += (height() - _forward->height()) / 2;

	_forward->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_forward->isHidden()) {
		buttonsLeft += _forward->width() + st::topBarActionSkip;
	}

	_sendNow->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_sendNow->isHidden()) {
		buttonsLeft += _sendNow->width() + st::topBarActionSkip;
	}

	_delete->moveToLeft(buttonsLeft, selectedButtonsTop);
	_clear->moveToRight(st::topBarActionSkip, selectedButtonsTop);

	if (_back->isHidden()) {
		_leftTaken = st::topBarArrowPadding.right();
	} else {
		const auto smallDialogsColumn = _activeChat.key.folder()
			&& (width() < _back->width() + _search->width());
		_leftTaken = smallDialogsColumn ? (width() - _back->width()) / 2 : 0;
		_back->moveToLeft(_leftTaken, otherButtonsTop);
		_leftTaken += _back->width();
		if (_info && !_info->isHidden()) {
			_info->moveToLeft(_leftTaken, otherButtonsTop);
			_leftTaken += _info->width();
		}
	}

	_rightTaken = 0;
	_menuToggle->moveToRight(_rightTaken, otherButtonsTop);
	if (_menuToggle->isHidden()) {
		_rightTaken += (_menuToggle->width() - _search->width());
	} else {
		_rightTaken += _menuToggle->width() + st::topBarSkip;
	}
	_infoToggle->moveToRight(_rightTaken, otherButtonsTop);
	if (!_infoToggle->isHidden()) {
		_infoToggle->moveToRight(_rightTaken, otherButtonsTop);
		_rightTaken += _infoToggle->width();
	}
	if (!_call->isHidden() || !_groupCall->isHidden()) {
		_call->moveToRight(_rightTaken, otherButtonsTop);
		_groupCall->moveToRight(_rightTaken, otherButtonsTop);
		_rightTaken += _call->width();
	}
	_search->moveToRight(_rightTaken, otherButtonsTop);
	_rightTaken += _search->width() + st::topBarCallSkip;

	updateMembersShowArea();
}

void TopBarWidget::finishAnimating() {
	_selectedShown.stop();
	updateControlsVisibility();
	update();
}

void TopBarWidget::setAnimatingMode(bool enabled) {
	if (_animatingMode != enabled) {
		_animatingMode = enabled;
		setAttribute(Qt::WA_OpaquePaintEvent, !_animatingMode);
		finishAnimating();
	}
}

void TopBarWidget::updateControlsVisibility() {
	if (!_activeChat.key) {
		return;
	} else if (_animatingMode) {
		hideChildren();
		return;
	}
	_clear->show();
	_delete->setVisible(_canDelete);
	_forward->setVisible(_canForward);
	_sendNow->setVisible(_canSendNow);

	auto backVisible = Adaptive::OneColumn()
		|| !_controller->content()->stackIsEmpty()
		|| _activeChat.key.folder();
	_back->setVisible(backVisible);
	if (_info) {
		_info->setVisible(Adaptive::OneColumn());
	}
	if (_unreadBadge) {
		_unreadBadge->show();
	}
	const auto section = _activeChat.section;
	const auto historyMode = (section == Section::History);
	const auto hasPollsMenu = _activeChat.key.peer()
		&& _activeChat.key.peer()->canSendPolls();
	const auto hasMenu = !_activeChat.key.folder()
		&& ((section == Section::Scheduled || section == Section::Replies)
			? hasPollsMenu
			: historyMode);
	updateSearchVisibility();
	_menuToggle->setVisible(hasMenu);
	_infoToggle->setVisible(historyMode
		&& !_activeChat.key.folder()
		&& !Adaptive::OneColumn()
		&& _controller->canShowThirdSection());
	const auto callsEnabled = [&] {
		if (const auto peer = _activeChat.key.peer()) {
			if (const auto user = peer->asUser()) {
				return !user->isSelf() && !user->isBot();
			}
		}
		return false;
	}();
	_call->setVisible(historyMode && callsEnabled);
	const auto groupCallsEnabled = [&] {
		if (const auto peer = _activeChat.key.peer()) {
			return peer->canManageGroupCall();
		}
		return false;
	}();
	_groupCall->setVisible(historyMode && groupCallsEnabled);

	if (_membersShowArea) {
		_membersShowArea->show();
	}
	updateControlsGeometry();
}

void TopBarWidget::updateMembersShowArea() {
	const auto membersShowAreaNeeded = [&] {
		const auto peer = _activeChat.key.peer();
		if ((_selectedCount > 0) || !peer) {
			return false;
		} else if (const auto chat = peer->asChat()) {
			return chat->amIn();
		} else if (const auto megagroup = peer->asMegagroup()) {
			return megagroup->canViewMembers()
				&& (megagroup->membersCount()
					< megagroup->session().serverConfig().chatSizeMax);
		}
		return false;
	}();
	if (!membersShowAreaNeeded) {
		if (_membersShowArea) {
			_membersShowAreaActive.fire(false);
			_membersShowArea.destroy();
		}
		return;
	} else if (!_membersShowArea) {
		_membersShowArea.create(this);
		_membersShowArea->show();
		_membersShowArea->installEventFilter(this);
	}
	_membersShowArea->setGeometry(getMembersShowAreaGeometry());
}

void TopBarWidget::showSelected(SelectedState state) {
	auto canDelete = (state.count > 0 && state.count == state.canDeleteCount);
	auto canForward = (state.count > 0 && state.count == state.canForwardCount);
	auto canSendNow = (state.count > 0 && state.count == state.canSendNowCount);
	if (_selectedCount == state.count && _canDelete == canDelete && _canForward == canForward && _canSendNow == canSendNow) {
		return;
	}
	if (state.count == 0) {
		// Don't change the visible buttons if the selection is cancelled.
		canDelete = _canDelete;
		canForward = _canForward;
		canSendNow = _canSendNow;
	}

	auto wasSelected = (_selectedCount > 0);
	_selectedCount = state.count;
	if (_selectedCount > 0) {
		_forward->setNumbersText(_selectedCount);
		_sendNow->setNumbersText(_selectedCount);
		_delete->setNumbersText(_selectedCount);
		if (!wasSelected) {
			_forward->finishNumbersAnimation();
			_sendNow->finishNumbersAnimation();
			_delete->finishNumbersAnimation();
		}
	}
	auto hasSelected = (_selectedCount > 0);
	if (_canDelete != canDelete || _canForward != canForward || _canSendNow != canSendNow) {
		_canDelete = canDelete;
		_canForward = canForward;
		_canSendNow = canSendNow;
		updateControlsVisibility();
	}
	if (wasSelected != hasSelected) {
		setCursor(hasSelected ? style::cur_default : style::cur_pointer);

		updateMembersShowArea();
		_selectedShown.start(
			[this] { selectedShowCallback(); },
			hasSelected ? 0. : 1.,
			hasSelected ? 1. : 0.,
			st::slideWrapDuration,
			anim::easeOutCirc);
	} else {
		updateControlsGeometry();
	}
}

void TopBarWidget::selectedShowCallback() {
	updateControlsGeometry();
	update();
}

void TopBarWidget::updateAdaptiveLayout() {
	updateControlsVisibility();
	updateInfoToggleActive();
	refreshUnreadBadge();
}

void TopBarWidget::refreshUnreadBadge() {
	if (!Adaptive::OneColumn() && !_activeChat.key.folder()) {
		_unreadBadge.destroy();
		return;
	} else if (_unreadBadge) {
		return;
	}
	_unreadBadge.create(this);

	rpl::combine(
		_back->geometryValue(),
		_unreadBadge->widthValue()
	) | rpl::start_with_next([=](QRect geometry, int width) {
		_unreadBadge->move(
			geometry.x() + geometry.width() - width,
			geometry.y() + st::titleUnreadCounterTop);
	}, _unreadBadge->lifetime());

	_unreadBadge->show();
	_unreadBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
	_controller->session().data().unreadBadgeChanges(
	) | rpl::start_with_next([=] {
		updateUnreadBadge();
	}, _unreadBadge->lifetime());
	updateUnreadBadge();
}

void TopBarWidget::updateUnreadBadge() {
	if (!_unreadBadge) return;

	const auto key = _activeChat.key;
	const auto muted = session().data().unreadBadgeMutedIgnoreOne(key);
	const auto counter = session().data().unreadBadgeIgnoreOne(key);
	const auto text = [&] {
		if (!counter) {
			return QString();
		}
		return (counter > 999)
			? qsl("..%1").arg(counter % 100, 2, 10, QChar('0'))
			: QString::number(counter);
	}();
	_unreadBadge->setText(text, !muted);
}

void TopBarWidget::updateInfoToggleActive() {
	auto infoThirdActive = Adaptive::ThreeColumn()
		&& (Core::App().settings().thirdSectionInfoEnabled()
			|| Core::App().settings().tabbedReplacedWithInfo());
	auto iconOverride = infoThirdActive
		? &st::topBarInfoActive
		: nullptr;
	auto rippleOverride = infoThirdActive
		? &st::lightButtonBgOver
		: nullptr;
	_infoToggle->setIconOverride(iconOverride, iconOverride);
	_infoToggle->setRippleColorOverride(rippleOverride);
}

void TopBarWidget::updateOnlineDisplay() {
	const auto peer = _activeChat.key.peer();
	if (!peer) {
		return;
	}

	QString text;
	const auto now = base::unixtime::now();
	bool titlePeerTextOnline = false;
	if (const auto user = peer->asUser()) {
		if (session().supportMode()
			&& !session().supportHelper().infoCurrent(user).text.empty()) {
			text = QString::fromUtf8("\xe2\x9a\xa0\xef\xb8\x8f check info");
			titlePeerTextOnline = false;
		} else {
			text = Data::OnlineText(user, now);
			titlePeerTextOnline = Data::OnlineTextActive(user, now);
		}
	} else if (const auto chat = peer->asChat()) {
		if (!chat->amIn()) {
			text = tr::lng_chat_status_unaccessible(tr::now);
		} else if (chat->participants.empty()) {
			if (!_titlePeerText.isEmpty()) {
				text = _titlePeerText.toString();
			} else if (chat->count <= 0) {
				text = tr::lng_group_status(tr::now);
			} else {
				text = tr::lng_chat_status_members(tr::now, lt_count_decimal, chat->count);
			}
		} else {
			const auto self = session().user();
			auto online = 0;
			auto onlyMe = true;
			for (const auto user : chat->participants) {
				if (user->onlineTill > now) {
					++online;
					if (onlyMe && user != self) onlyMe = false;
				}
			}
			if (online > 0 && !onlyMe) {
				auto membersCount = tr::lng_chat_status_members(tr::now, lt_count_decimal, chat->participants.size());
				auto onlineCount = tr::lng_chat_status_online(tr::now, lt_count, online);
				text = tr::lng_chat_status_members_online(tr::now, lt_members_count, membersCount, lt_online_count, onlineCount);
			} else if (chat->participants.size() > 0) {
				text = tr::lng_chat_status_members(tr::now, lt_count_decimal, chat->participants.size());
			} else {
				text = tr::lng_group_status(tr::now);
			}
		}
	} else if (const auto channel = peer->asChannel()) {
		if (channel->isMegagroup()
			&& (channel->membersCount() > 0)
			&& (channel->membersCount()
				<= channel->session().serverConfig().chatSizeMax)) {
			if (channel->lastParticipantsRequestNeeded()) {
				session().api().requestLastParticipants(channel);
			}
			const auto self = session().user();
			auto online = 0;
			auto onlyMe = true;
			for (auto &participant : std::as_const(channel->mgInfo->lastParticipants)) {
				if (participant->onlineTill > now) {
					++online;
					if (onlyMe && participant != self) {
						onlyMe = false;
					}
				}
			}
			if (online && !onlyMe) {
				auto membersCount = tr::lng_chat_status_members(tr::now, lt_count_decimal, channel->membersCount());
				auto onlineCount = tr::lng_chat_status_online(tr::now, lt_count, online);
				text = tr::lng_chat_status_members_online(tr::now, lt_members_count, membersCount, lt_online_count, onlineCount);
			} else if (channel->membersCount() > 0) {
				text = tr::lng_chat_status_members(tr::now, lt_count_decimal, channel->membersCount());
			} else {
				text = tr::lng_group_status(tr::now);
			}
		} else if (channel->membersCount() > 0) {
			text = channel->isMegagroup()
				? tr::lng_chat_status_members(tr::now, lt_count_decimal, channel->membersCount())
				: tr::lng_chat_status_subscribers(tr::now, lt_count_decimal, channel->membersCount());

		} else {
			text = channel->isMegagroup() ? tr::lng_group_status(tr::now) : tr::lng_channel_status(tr::now);
		}
	}
	if (_titlePeerText.toString() != text) {
		_titlePeerText.setText(st::dialogsTextStyle, text);
		_titlePeerTextOnline = titlePeerTextOnline;
		updateMembersShowArea();
		update();
	}
	updateOnlineDisplayTimer();
}

void TopBarWidget::updateOnlineDisplayTimer() {
	const auto peer = _activeChat.key.peer();
	if (!peer) {
		return;
	}

	const auto now = base::unixtime::now();
	auto minTimeout = crl::time(86400);
	const auto handleUser = [&](not_null<UserData*> user) {
		auto hisTimeout = Data::OnlineChangeTimeout(user, now);
		accumulate_min(minTimeout, hisTimeout);
	};
	if (const auto user = peer->asUser()) {
		handleUser(user);
	} else if (const auto chat = peer->asChat()) {
		for (const auto user : chat->participants) {
			handleUser(user);
		}
	} else if (peer->isChannel()) {
	}
	updateOnlineDisplayIn(minTimeout);
}

void TopBarWidget::updateOnlineDisplayIn(crl::time timeout) {
	_onlineUpdater.callOnce(timeout);
}

} // namespace HistoryView
