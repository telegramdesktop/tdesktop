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
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "storage/storage_shared_media.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "lang/lang_keys.h"
#include "core/shortcuts.h"
#include "ui/special_buttons.h"
#include "ui/unread_badge.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/effects/radial_animation.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "calls/calls_instance.h"
#include "data/data_peer_values.h"
#include "data/data_feed.h"
#include "support/support_helper.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"
#include "styles/style_history.h"
#include "styles/style_info.h"

namespace HistoryView {

TopBarWidget::TopBarWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _clear(this, langFactory(lng_selected_clear), st::topBarClearButton)
, _forward(this, langFactory(lng_selected_forward), st::defaultActiveButton)
, _delete(this, langFactory(lng_selected_delete), st::defaultActiveButton)
, _back(this, st::historyTopBarBack)
, _call(this, st::topBarCall)
, _search(this, st::topBarSearch)
, _infoToggle(this, st::topBarInfo)
, _menuToggle(this, st::topBarMenuToggle)
, _titlePeerText(st::windowMinWidth / 3)
, _onlineUpdater([this] { updateOnlineDisplay(); }) {
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });
	setAttribute(Qt::WA_OpaquePaintEvent);

	_forward->setClickedCallback([this] { _forwardSelection.fire({}); });
	_forward->setWidthChangedCallback([this] { updateControlsGeometry(); });
	_delete->setClickedCallback([this] { _deleteSelection.fire({}); });
	_delete->setWidthChangedCallback([this] { updateControlsGeometry(); });
	_clear->setClickedCallback([this] { _clearSelection.fire({}); });
	_call->setClickedCallback([this] { onCall(); });
	_search->setClickedCallback([this] { onSearch(); });
	_menuToggle->setClickedCallback([this] { showMenu(); });
	_infoToggle->setClickedCallback([this] { toggleInfoSection(); });
	_back->addClickHandler([this] { backClicked(); });

	rpl::combine(
		_controller->activeChatValue(),
		_controller->searchInChat.value()
	) | rpl::combine_previous(
		std::make_tuple(Dialogs::Key(), Dialogs::Key())
	) | rpl::map([](
			const std::tuple<Dialogs::Key, Dialogs::Key> &previous,
			const std::tuple<Dialogs::Key, Dialogs::Key> &current) {
		auto active = std::get<0>(current);
		auto search = std::get<1>(current);
		auto activeChanged = (active != std::get<0>(previous));
		auto searchInChat
			= search && (active == search);
		return std::make_tuple(searchInChat, activeChanged);
	}) | rpl::start_with_next([this](
			bool searchInActiveChat,
			bool activeChanged) {
		auto animated = activeChanged
			? anim::type::instant
			: anim::type::normal;
		_search->setForceRippled(searchInActiveChat, animated);
	}, lifetime());

	subscribe(Adaptive::Changed(), [this] { updateAdaptiveLayout(); });
	if (Adaptive::OneColumn()) {
		createUnreadBadge();
	}
	subscribe(
		App::histories().sendActionAnimationUpdated(),
		[this](const Histories::SendActionAnimationUpdate &update) {
			if (update.history == _activeChat.history()) {
				this->update();
			}
		});
	using UpdateFlag = Notify::PeerUpdate::Flag;
	auto flags = UpdateFlag::UserHasCalls
		| UpdateFlag::UserOnlineChanged
		| UpdateFlag::MembersChanged
		| UpdateFlag::UserSupportInfoChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(flags, [this](const Notify::PeerUpdate &update) {
		if (update.flags & UpdateFlag::UserHasCalls) {
			if (update.peer->isUser()) {
				updateControlsVisibility();
			}
		} else {
			updateOnlineDisplay();
		}
	}));
	subscribe(Global::RefPhoneCallsEnabledChanged(), [this] {
		updateControlsVisibility();
	});

	rpl::combine(
		Auth().settings().thirdSectionInfoEnabledValue(),
		Auth().settings().tabbedReplacedWithInfoValue()
	) | rpl::start_with_next(
		[this] { updateInfoToggleActive(); },
		lifetime());

	rpl::single(rpl::empty_value()) | rpl::then(
		base::ObservableViewer(Global::RefConnectionTypeChanged())
	) | rpl::start_with_next(
		[=] { updateConnectingState(); },
		lifetime());

	setCursor(style::cur_pointer);
	updateControlsVisibility();
}

void TopBarWidget::updateConnectingState() {
	const auto mtp = MTP::dcstate();
	if (mtp == MTP::ConnectedState) {
		if (_connecting) {
			_connecting = nullptr;
			update();
		}
	} else if (!_connecting) {
		_connecting = std::make_unique<Ui::InfiniteRadialAnimation>(
			animation(this, &TopBarWidget::step_connecting),
			st::topBarConnectingAnimation);
		_connecting->start();
		update();
	}
}

void TopBarWidget::step_connecting(TimeMs ms, bool timer) {
	if (timer && !anim::Disabled()) {
		update();
	}
}

void TopBarWidget::refreshLang() {
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

void TopBarWidget::onSearch() {
	if (_activeChat) {
		App::main()->searchInChat(_activeChat);
	}
}

void TopBarWidget::onCall() {
	if (const auto peer = _activeChat.peer()) {
		if (const auto user = peer->asUser()) {
			Calls::Current().startOutgoingCall(user);
		}
	}
}

void TopBarWidget::showMenu() {
	if (!_activeChat || _menu) {
		return;
	}
	_menu.create(parentWidget());
	_menu->setHiddenCallback([weak = make_weak(this), menu = _menu.data()] {
		menu->deleteLater();
		if (weak && weak->_menu == menu) {
			weak->_menu = nullptr;
			weak->_menuToggle->setForceRippled(false);
		}
	});
	_menu->setShowStartCallback(crl::guard(this, [this, menu = _menu.data()] {
		if (_menu == menu) {
			_menuToggle->setForceRippled(true);
		}
	}));
	_menu->setHideStartCallback(crl::guard(this, [this, menu = _menu.data()] {
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
	if (const auto peer = _activeChat.peer()) {
		Window::FillPeerMenu(
			_controller,
			peer,
			addAction,
			Window::PeerMenuSource::History);
	} else if (const auto feed = _activeChat.feed()) {
		Window::FillFeedMenu(
			_controller,
			feed,
			addAction,
			Window::PeerMenuSource::History);
	} else {
		Unexpected("Empty active chat in TopBarWidget::showMenu.");
	}
	_menu->moveToRight((parentWidget()->width() - width()) + st::topBarMenuPosition.x(), st::topBarMenuPosition.y());
	_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
}

void TopBarWidget::toggleInfoSection() {
	if (Adaptive::ThreeColumn()
		&& (Auth().settings().thirdSectionInfoEnabled()
			|| Auth().settings().tabbedReplacedWithInfo())) {
		_controller->closeThirdSection();
	} else if (_activeChat) {
		if (_controller->canShowThirdSection()) {
			Auth().settings().setThirdSectionInfoEnabled(true);
			Auth().saveSettingsDelayed();
			if (Adaptive::ThreeColumn()) {
				_controller->showSection(
					Info::Memento::Default(_activeChat),
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
	return TWidget::eventFilter(obj, e);
}

int TopBarWidget::resizeGetHeight(int newWidth) {
	return st::topBarHeight;
}

void TopBarWidget::paintEvent(QPaintEvent *e) {
	if (_animatingMode) {
		return;
	}
	Painter p(this);

	auto ms = getms();
	_forward->stepNumbersAnimation(ms);
	_delete->stepNumbersAnimation(ms);
	auto hasSelected = (_selectedCount > 0);
	auto selectedButtonsTop = countSelectedButtonsTop(_selectedShown.current(getms(), hasSelected ? 1. : 0.));

	p.fillRect(QRect(0, 0, width(), st::topBarHeight), st::topBarBg);
	if (selectedButtonsTop < 0) {
		p.translate(0, selectedButtonsTop + st::topBarHeight);
		paintTopBar(p, ms);
	}
}

void TopBarWidget::paintTopBar(Painter &p, TimeMs ms) {
	if (!_activeChat) {
		return;
	}
	auto nameleft = _leftTaken;
	auto nametop = st::topBarArrowPadding.top();
	auto statustop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	auto namewidth = width() - _rightTaken - nameleft;

	auto history = _activeChat.history();

	p.setPen(st::dialogsNameFg);
	if (const auto feed = _activeChat.feed()) {
		auto text = feed->chatsListName(); // TODO feed name emoji
		auto textWidth = st::historySavedFont->width(text);
		if (namewidth < textWidth) {
			text = st::historySavedFont->elided(text, namewidth);
		}
		p.setFont(st::historySavedFont);
		p.drawTextLeft(
			nameleft,
			(height() - st::historySavedFont->height) / 2,
			width(),
			text);
	} else if (_activeChat.peer()->isSelf()) {
		auto text = lang(lng_saved_messages);
		auto textWidth = st::historySavedFont->width(text);
		if (namewidth < textWidth) {
			text = st::historySavedFont->elided(text, namewidth);
		}
		p.setFont(st::historySavedFont);
		p.drawTextLeft(
			nameleft,
			(height() - st::historySavedFont->height) / 2,
			width(),
			text);
	} else if (const auto history = _activeChat.history()) {
		history->peer->dialogName().drawElided(p, nameleft, nametop, namewidth);

		p.setFont(st::dialogsTextFont);
		if (paintConnectingState(p, nameleft, statustop, width(), ms)) {
			return;
		} else if (history->paintSendAction(
				p,
				nameleft,
				statustop,
				namewidth,
				width(),
				st::historyStatusFgTyping,
				ms)) {
			return;
		} else {
			paintStatus(p, nameleft, statustop, namewidth, width());
		}
	}
}

bool TopBarWidget::paintConnectingState(
		Painter &p,
		int left,
		int top,
		int outerWidth,
		TimeMs ms) {
	if (_connecting) {
		_connecting->step(ms);
	}
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
	p.drawTextLeft(left, top, outerWidth, lang(lng_status_connecting));
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
	if (!_activeChat) {
		return;
	} else if (const auto feed = _activeChat.feed()) {
		_controller->showSection(Info::Memento(
			feed,
			Info::Section(Info::Section::Type::Profile)));
	} else if (_activeChat.peer()->isSelf()) {
		_controller->showSection(Info::Memento(
			_activeChat.peer()->id,
			Info::Section(Storage::SharedMediaType::Photo)));
	} else {
		_controller->showPeerInfo(_activeChat.peer());
	}
}

void TopBarWidget::backClicked() {
	_controller->showBackFromStack();
}

void TopBarWidget::setActiveChat(Dialogs::Key chat) {
	if (_activeChat != chat) {
		_activeChat = chat;
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
	}
}

void TopBarWidget::refreshInfoButton() {
	if (const auto peer = _activeChat.peer()) {
		auto info = object_ptr<Ui::UserpicButton>(
			this,
			_controller,
			peer,
			Ui::UserpicButton::Role::Custom,
			st::topBarInfoButton);
		info->showSavedMessagesOnSelf(true);
		_info.destroy();
		_info = std::move(info);
	} else if (const auto feed = _activeChat.feed()) {
		_info.destroy();
		_info = object_ptr<Ui::FeedUserpicButton>(
			this,
			_controller,
			feed,
			st::topBarFeedInfoButton);
	}
	if (_info) {
		_info->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
}

void TopBarWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

int TopBarWidget::countSelectedButtonsTop(float64 selectedShown) {
	return (1. - selectedShown) * (-st::topBarHeight);
}

void TopBarWidget::updateControlsGeometry() {
	auto hasSelected = (_selectedCount > 0);
	auto selectedButtonsTop = countSelectedButtonsTop(_selectedShown.current(hasSelected ? 1. : 0.));
	auto otherButtonsTop = selectedButtonsTop + st::topBarHeight;
	auto buttonsLeft = st::topBarActionSkip + (Adaptive::OneColumn() ? 0 : st::lineWidth);
	auto buttonsWidth = _forward->contentWidth() + _delete->contentWidth() + _clear->width();
	buttonsWidth += buttonsLeft + st::topBarActionSkip * 3;

	auto widthLeft = qMin(width() - buttonsWidth, -2 * st::defaultActiveButton.width);
	auto buttonFullWidth = qMin(-(widthLeft / 2), 0);
	_forward->setFullWidth(buttonFullWidth);
	_delete->setFullWidth(buttonFullWidth);

	selectedButtonsTop += (height() - _forward->height()) / 2;

	_forward->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_forward->isHidden()) {
		buttonsLeft += _forward->width() + st::topBarActionSkip;
	}

	_delete->moveToLeft(buttonsLeft, selectedButtonsTop);
	_clear->moveToRight(st::topBarActionSkip, selectedButtonsTop);

	if (_unreadBadge) {
		_unreadBadge->setGeometryToLeft(
			0,
			otherButtonsTop + st::titleUnreadCounterTop,
			_back->width(),
			st::dialogsUnreadHeight);
	}
	if (_back->isHidden()) {
		_leftTaken = st::topBarArrowPadding.right();
	} else {
		_leftTaken = 0;
		_back->moveToLeft(_leftTaken, otherButtonsTop);
		_leftTaken += _back->width();
		if (_info && !_info->isHidden()) {
			_info->moveToLeft(_leftTaken, otherButtonsTop);
			_leftTaken += _info->width();
		}
	}

	_rightTaken = 0;
	_menuToggle->moveToRight(_rightTaken, otherButtonsTop);
	_rightTaken += _menuToggle->width() + st::topBarSkip;
	_infoToggle->moveToRight(_rightTaken, otherButtonsTop);
	if (!_infoToggle->isHidden()) {
		_rightTaken += _infoToggle->width() + st::topBarSkip;
	}
	_search->moveToRight(_rightTaken, otherButtonsTop);
	_rightTaken += _search->width() + st::topBarCallSkip;
	_call->moveToRight(_rightTaken, otherButtonsTop);
	_rightTaken += _call->width();

	updateMembersShowArea();
}

void TopBarWidget::finishAnimating() {
	_selectedShown.finish();
	updateControlsVisibility();
}

void TopBarWidget::setAnimatingMode(bool enabled) {
	if (_animatingMode != enabled) {
		_animatingMode = enabled;
		setAttribute(Qt::WA_OpaquePaintEvent, !_animatingMode);
		finishAnimating();
		updateControlsVisibility();
	}
}

void TopBarWidget::updateControlsVisibility() {
	if (_animatingMode) {
		hideChildren();
		return;
	}
	_clear->show();
	_delete->setVisible(_canDelete);
	_forward->setVisible(_canForward);

	auto backVisible = Adaptive::OneColumn()
		|| (App::main() && !App::main()->stackIsEmpty());
	_back->setVisible(backVisible);
	if (_info) {
		_info->setVisible(Adaptive::OneColumn());
	}
	if (_unreadBadge) {
		_unreadBadge->show();
	}
	_search->show();
	_menuToggle->show();
	_infoToggle->setVisible(!Adaptive::OneColumn()
		&& _controller->canShowThirdSection());
	const auto callsEnabled = [&] {
		if (const auto peer = _activeChat.peer()) {
			if (const auto user = peer->asUser()) {
				return Global::PhoneCallsEnabled() && user->hasCalls();
			}
		}
		return false;
	}();
	_call->setVisible(callsEnabled);

	if (_membersShowArea) {
		_membersShowArea->show();
	}
	updateControlsGeometry();
}

void TopBarWidget::updateMembersShowArea() {
	if (!App::main()) {
		return;
	}
	auto membersShowAreaNeeded = [this]() {
		auto peer = App::main()->peer();
		if ((_selectedCount > 0) || !peer) {
			return false;
		}
		if (auto chat = peer->asChat()) {
			return chat->amIn();
		}
		if (auto megagroup = peer->asMegagroup()) {
			return megagroup->canViewMembers() && (megagroup->membersCount() < Global::ChatSizeMax());
		}
		return false;
	};
	if (!membersShowAreaNeeded()) {
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
	if (_selectedCount == state.count && _canDelete == canDelete && _canForward == canForward) {
		return;
	}
	if (state.count == 0) {
		// Don't change the visible buttons if the selection is cancelled.
		canDelete = _canDelete;
		canForward = _canForward;
	}

	auto wasSelected = (_selectedCount > 0);
	_selectedCount = state.count;
	if (_selectedCount > 0) {
		_forward->setNumbersText(_selectedCount);
		_delete->setNumbersText(_selectedCount);
		if (!wasSelected) {
			_forward->finishNumbersAnimation();
			_delete->finishNumbersAnimation();
		}
	}
	auto hasSelected = (_selectedCount > 0);
	if (_canDelete != canDelete || _canForward != canForward) {
		_canDelete = canDelete;
		_canForward = canForward;
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
	if (Adaptive::OneColumn()) {
		createUnreadBadge();
	} else if (_unreadBadge) {
		unsubscribe(base::take(_unreadCounterSubscription));
		_unreadBadge.destroy();
	}
	updateInfoToggleActive();
}

void TopBarWidget::createUnreadBadge() {
	if (_unreadBadge) {
		return;
	}
	_unreadBadge.create(this);
	_unreadBadge->setGeometryToLeft(0, st::titleUnreadCounterTop, _back->width(), st::dialogsUnreadHeight);
	_unreadBadge->show();
	_unreadBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
	_unreadCounterSubscription = subscribe(
		Global::RefUnreadCounterUpdate(),
		[this] { updateUnreadBadge(); });
	updateUnreadBadge();
}

void TopBarWidget::updateUnreadBadge() {
	if (!_unreadBadge) return;

	const auto history = _activeChat.history();
	const auto active = !App::histories().unreadBadgeMutedIgnoreOne(history);
	const auto counter = App::histories().unreadBadgeIgnoreOne(history);
	const auto text = [&] {
		if (!counter) {
			return QString();
		}
		return (counter > 999)
			? qsl("..%1").arg(counter % 100, 2, 10, QChar('0'))
			: QString::number(counter);
	}();
	_unreadBadge->setText(text, active);
}

void TopBarWidget::updateInfoToggleActive() {
	auto infoThirdActive = Adaptive::ThreeColumn()
		&& (Auth().settings().thirdSectionInfoEnabled()
			|| Auth().settings().tabbedReplacedWithInfo());
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
	if (!_activeChat.peer()) return;

	QString text;
	const auto now = unixtime();
	bool titlePeerTextOnline = false;
	if (const auto user = _activeChat.peer()->asUser()) {
		if (Auth().supportMode()
			&& !Auth().supportHelper().infoCurrent(user).text.empty()) {
			text = QString::fromUtf8("\xe2\x9a\xa0\xef\xb8\x8f check info");
			titlePeerTextOnline = false;
		} else {
			text = Data::OnlineText(user, now);
			titlePeerTextOnline = Data::OnlineTextActive(user, now);
		}
	} else if (const auto chat = _activeChat.peer()->asChat()) {
		if (!chat->amIn()) {
			text = lang(lng_chat_status_unaccessible);
		} else if (chat->participants.empty()) {
			if (!_titlePeerText.isEmpty()) {
				text = _titlePeerText.originalText();
			} else if (chat->count <= 0) {
				text = lang(lng_group_status);
			} else {
				text = lng_chat_status_members(lt_count, chat->count);
			}
		} else {
			const auto self = Auth().user();
			auto online = 0;
			auto onlyMe = true;
			for (const auto [user, v] : chat->participants) {
				if (user->onlineTill > now) {
					++online;
					if (onlyMe && user != self) onlyMe = false;
				}
			}
			if (online > 0 && !onlyMe) {
				auto membersCount = lng_chat_status_members(lt_count, chat->participants.size());
				auto onlineCount = lng_chat_status_online(lt_count, online);
				text = lng_chat_status_members_online(lt_members_count, membersCount, lt_online_count, onlineCount);
			} else if (chat->participants.size() > 0) {
				text = lng_chat_status_members(lt_count, chat->participants.size());
			} else {
				text = lang(lng_group_status);
			}
		}
	} else if (const auto channel = _activeChat.peer()->asChannel()) {
		if (channel->isMegagroup() && channel->membersCount() > 0 && channel->membersCount() <= Global::ChatSizeMax()) {
			if (channel->mgInfo->lastParticipants.empty() || channel->lastParticipantsCountOutdated()) {
				Auth().api().requestLastParticipants(channel);
			}
			const auto self = Auth().user();
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
				auto membersCount = lng_chat_status_members(lt_count, channel->membersCount());
				auto onlineCount = lng_chat_status_online(lt_count, online);
				text = lng_chat_status_members_online(lt_members_count, membersCount, lt_online_count, onlineCount);
			} else if (channel->membersCount() > 0) {
				text = lng_chat_status_members(lt_count, channel->membersCount());
			} else {
				text = lang(lng_group_status);
			}
		} else if (channel->membersCount() > 0) {
			text = lng_chat_status_members(lt_count, channel->membersCount());
		} else {
			text = lang(channel->isMegagroup() ? lng_group_status : lng_channel_status);
		}
	}
	if (_titlePeerText.originalText() != text) {
		_titlePeerText.setText(st::dialogsTextStyle, text);
		_titlePeerTextOnline = titlePeerTextOnline;
		updateMembersShowArea();
		update();
	}
	updateOnlineDisplayTimer();
}

void TopBarWidget::updateOnlineDisplayTimer() {
	if (!_activeChat.peer()) return;

	const auto now = unixtime();
	auto minTimeout = TimeMs(86400);
	const auto handleUser = [&](not_null<UserData*> user) {
		auto hisTimeout = Data::OnlineChangeTimeout(user, now);
		accumulate_min(minTimeout, hisTimeout);
	};
	if (const auto user = _activeChat.peer()->asUser()) {
		handleUser(user);
	} else if (auto chat = _activeChat.peer()->asChat()) {
		for (const auto [user, v] : chat->participants) {
			handleUser(user);
		}
	} else if (_activeChat.peer()->isChannel()) {
	}
	updateOnlineDisplayIn(minTimeout);
}

void TopBarWidget::updateOnlineDisplayIn(TimeMs timeout) {
	_onlineUpdater.callOnce(timeout);
}

TopBarWidget::~TopBarWidget() = default;

} // namespace HistoryView
