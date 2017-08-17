/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "window/top_bar_widget.h"

#include "styles/style_window.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "shortcuts.h"
#include "lang/lang_keys.h"
#include "ui/special_buttons.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "dialogs/dialogs_layout.h"
#include "window/window_controller.h"
#include "calls/calls_instance.h"
#include "observer_peer.h"

namespace Window {

TopBarWidget::TopBarWidget(QWidget *parent, not_null<Window::Controller*> controller) : TWidget(parent)
, _controller(controller)
, _clearSelection(this, langFactory(lng_selected_clear), st::topBarClearButton)
, _forward(this, langFactory(lng_selected_forward), st::defaultActiveButton)
, _delete(this, langFactory(lng_selected_delete), st::defaultActiveButton)
, _info(this, nullptr, st::topBarInfoButton)
, _mediaType(this, langFactory(lng_media_type), st::topBarButton)
, _call(this, st::topBarCall)
, _search(this, st::topBarSearch)
, _menuToggle(this, st::topBarMenuToggle) {
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });

	_forward->setClickedCallback([this] { onForwardSelection(); });
	_forward->setWidthChangedCallback([this] { updateControlsGeometry(); });
	_delete->setClickedCallback([this] { onDeleteSelection(); });
	_delete->setWidthChangedCallback([this] { updateControlsGeometry(); });
	_clearSelection->setClickedCallback([this] { onClearSelection(); });
	_info->setClickedCallback([this] { onInfoClicked(); });
	_call->setClickedCallback([this] { onCall(); });
	_search->setClickedCallback([this] { onSearch(); });
	_menuToggle->setClickedCallback([this] { showMenu(); });

	subscribe(_controller->searchInPeerChanged(), [this](PeerData *peer) {
		_searchInPeer = peer;
		auto historyPeer = App::main() ? App::main()->historyPeer() : nullptr;
		_search->setForceRippled(historyPeer && historyPeer == _searchInPeer);
	});
	subscribe(_controller->historyPeerChanged(), [this](PeerData *peer) {
		_search->setForceRippled(peer && peer == _searchInPeer, Ui::IconButton::SetForceRippledWay::SkipAnimation);
		update();
	});

	subscribe(Adaptive::Changed(), [this]() { updateAdaptiveLayout(); });
	if (Adaptive::OneColumn()) {
		_unreadCounterSubscription = subscribe(Global::RefUnreadCounterUpdate(), [this] {
			rtlupdate(0, 0, st::titleUnreadCounterRight, st::titleUnreadCounterTop);
		});
	}
	subscribe(App::histories().sendActionAnimationUpdated(), [this](const Histories::SendActionAnimationUpdate &update) {
		if (App::main() && update.history->peer == App::main()->historyPeer()) {
			rtlupdate(0, 0, width(), height());
		}
	});
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserHasCalls, [this](const Notify::PeerUpdate &update) {
		if (update.peer->isUser()) {
			updateControlsVisibility();
		}
	}));
	subscribe(Global::RefPhoneCallsEnabledChanged(), [this] { updateControlsVisibility(); });

	setCursor(style::cur_pointer);
	updateControlsVisibility();
}

void TopBarWidget::refreshLang() {
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

void TopBarWidget::onForwardSelection() {
	if (App::main()) App::main()->forwardSelectedItems();
}

void TopBarWidget::onDeleteSelection() {
	if (App::main()) App::main()->confirmDeleteSelectedItems();
}

void TopBarWidget::onClearSelection() {
	if (App::main()) App::main()->clearSelectedItems();
}

void TopBarWidget::onInfoClicked() {
	auto p = App::main() ? App::main()->historyPeer() : nullptr;
	if (p) Ui::showPeerProfile(p);
}

void TopBarWidget::onSearch() {
	if (auto main = App::main()) {
		if (auto peer = main->peer()) {
			main->searchInPeer(peer);
		}
	}
}

void TopBarWidget::onCall() {
	if (auto main = App::main()) {
		if (auto peer = main->peer()) {
			if (auto user = peer->asUser()) {
				Calls::Current().startOutgoingCall(user);
			}
		}
	}
}

void TopBarWidget::showMenu() {
	if (auto main = App::main()) {
		if (auto peer = main->peer()) {
			if (!_menu) {
				_menu.create(parentWidget());
				_menu->setHiddenCallback([that = weak(this), menu = _menu.data()] {
					menu->deleteLater();
					if (that && that->_menu == menu) {
						that->_menu = nullptr;
						that->_menuToggle->setForceRippled(false);
					}
				});
				_menu->setShowStartCallback(base::lambda_guarded(this, [this, menu = _menu.data()] {
					if (_menu == menu) {
						_menuToggle->setForceRippled(true);
					}
				}));
				_menu->setHideStartCallback(base::lambda_guarded(this, [this, menu = _menu.data()] {
					if (_menu == menu) {
						_menuToggle->setForceRippled(false);
					}
				}));
				_menuToggle->installEventFilter(_menu);
				App::main()->fillPeerMenu(peer, [this](const QString &text, base::lambda<void()> callback) {
					return _menu->addAction(text, std::move(callback));
				}, false);
				_menu->moveToRight((parentWidget()->width() - width()) + st::topBarMenuPosition.x(), st::topBarMenuPosition.y());
				_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
			}
		}
	}
}

bool TopBarWidget::eventFilter(QObject *obj, QEvent *e) {
	if (obj == _membersShowArea) {
		switch (e->type()) {
		case QEvent::MouseButtonPress:
			mousePressEvent(static_cast<QMouseEvent*>(e));
			return true;

		case QEvent::Enter:
			App::main()->setMembersShowAreaActive(true);
			break;

		case QEvent::Leave:
			App::main()->setMembersShowAreaActive(false);
			break;
		}
	}
	return TWidget::eventFilter(obj, e);
}

void TopBarWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	_forward->stepNumbersAnimation(ms);
	_delete->stepNumbersAnimation(ms);
	auto hasSelected = (_selectedCount > 0);
	auto selectedButtonsTop = countSelectedButtonsTop(_selectedShown.current(getms(), hasSelected ? 1. : 0.));

	p.fillRect(QRect(0, 0, width(), st::topBarHeight), st::topBarBg);
	if (selectedButtonsTop < 0) {
		p.translate(0, selectedButtonsTop + st::topBarHeight);

		p.save();
		auto decreaseWidth = 0;
		if (!_info->isHidden()) {
			decreaseWidth += _info->width();
		}
		if (!_menuToggle->isHidden()) {
			decreaseWidth += _menuToggle->width();
		}
		if (!_search->isHidden()) {
			decreaseWidth += _search->width();
		}
		if (!_call->isHidden()) {
			decreaseWidth += st::topBarCallSkip + _call->width();
		}
		auto paintCounter = App::main()->paintTopBar(p, decreaseWidth, ms);
		p.restore();

		if (paintCounter) {
			paintUnreadCounter(p, width());
		}
	}
}

void TopBarWidget::paintUnreadCounter(Painter &p, int outerWidth) {
	if (!Adaptive::OneColumn()) {
		return;
	}
	auto mutedCount = App::histories().unreadMutedCount();
	auto fullCounter = App::histories().unreadBadge() + (Global::IncludeMuted() ? 0 : mutedCount);

	// Do not include currently shown chat in the top bar unread counter.
	if (auto historyShown = App::historyLoaded(App::main()->historyPeer())) {
		auto shownUnreadCount = historyShown->unreadCount();
		if (!historyShown->mute() || Global::IncludeMuted()) {
			fullCounter -= shownUnreadCount;
		}
		if (historyShown->mute()) {
			mutedCount -= shownUnreadCount;
		}
	}

	if (auto counter = (fullCounter - (Global::IncludeMuted() ? 0 : mutedCount))) {
		auto counterText = (counter > 99) ? qsl("..%1").arg(counter % 100) : QString::number(counter);
		Dialogs::Layout::UnreadBadgeStyle unreadSt;
		unreadSt.muted = (mutedCount >= fullCounter);
		auto unreadRight = st::titleUnreadCounterRight;
		if (rtl()) unreadRight = outerWidth - st::titleUnreadCounterRight;
		auto unreadTop = st::titleUnreadCounterTop;
		Dialogs::Layout::paintUnreadCount(p, counterText, unreadRight, unreadTop, unreadSt);
	}
}

void TopBarWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && e->pos().y() < st::topBarHeight && !_selectedCount) {
		emit clicked();
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
	auto buttonsWidth = _forward->contentWidth() + _delete->contentWidth() + _clearSelection->width();
	buttonsWidth += buttonsLeft + st::topBarActionSkip * 3;

	auto widthLeft = qMin(width() - buttonsWidth, -2 * st::defaultActiveButton.width);
	_forward->setFullWidth(-(widthLeft / 2));
	_delete->setFullWidth(-(widthLeft / 2));

	selectedButtonsTop += (height() - _forward->height()) / 2;

	_forward->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_forward->isHidden()) {
		buttonsLeft += _forward->width() + st::topBarActionSkip;
	}

	_delete->moveToLeft(buttonsLeft, selectedButtonsTop);
	_clearSelection->moveToRight(st::topBarActionSkip, selectedButtonsTop);

	auto right = 0;
	_info->moveToRight(right, otherButtonsTop);
	_menuToggle->moveToRight(right, otherButtonsTop);
	_mediaType->moveToRight(right, otherButtonsTop);
	if (_info->isHidden()) {
		right += _menuToggle->width();
	} else {
		right += _info->width();
	}
	_search->moveToRight(right, otherButtonsTop);
	right += _search->width() + st::topBarCallSkip;
	_call->moveToRight(right, otherButtonsTop);
}

void TopBarWidget::animationFinished() {
	updateMembersShowArea();
	updateControlsVisibility();
}

void TopBarWidget::updateControlsVisibility() {
	auto historyPeer = App::main() ? App::main()->historyPeer() : nullptr;
	auto overviewPeer = App::main() ? App::main()->overviewPeer() : nullptr;

	_clearSelection->show();
	_delete->setVisible(_canDelete);
	_forward->setVisible(_canForward);

	_mediaType->setVisible(App::main() ? App::main()->showMediaTypeSwitch() : false);
	if (historyPeer && !overviewPeer) {
		if (Adaptive::OneColumn() || !App::main()->stackIsEmpty()) {
			_info->setPeer(historyPeer);
			_info->show();
			_menuToggle->hide();
			_menu.destroy();
		} else {
			_info->hide();
			_menuToggle->show();
		}
		_search->show();
		auto callsEnabled = false;
		if (auto user = historyPeer->asUser()) {
			callsEnabled = Global::PhoneCallsEnabled() && user->hasCalls();
		}
		_call->setVisible(callsEnabled);
	} else {
		_search->hide();
		_call->hide();
		_info->hide();
		_menuToggle->hide();
		_menu.destroy();
	}
	if (_membersShowArea) {
		_membersShowArea->show();
	}
	updateControlsGeometry();
}

void TopBarWidget::updateMembersShowArea() {
	auto membersShowAreaNeeded = [this]() {
		auto peer = App::main()->peer();
		if ((_selectedCount > 0) || !peer || App::main()->overviewPeer()) {
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
			App::main()->setMembersShowAreaActive(false);
			_membersShowArea.destroy();
		}
		return;
	} else if (!_membersShowArea) {
		_membersShowArea.create(this);
		_membersShowArea->show();
		_membersShowArea->installEventFilter(this);
	}
	_membersShowArea->setGeometry(App::main()->getMembersShowAreaGeometry());
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
		_selectedShown.start([this] { selectedShowCallback(); }, hasSelected ? 0. : 1., hasSelected ? 1. : 0., st::topBarSlideDuration, anim::easeOutCirc);
	} else {
		updateControlsGeometry();
	}
}

void TopBarWidget::selectedShowCallback() {
	updateControlsGeometry();
	update();
}

void TopBarWidget::updateAdaptiveLayout() {
	updateMembersShowArea();
	updateControlsVisibility();
	if (!Adaptive::OneColumn()) {
		unsubscribe(base::take(_unreadCounterSubscription));
	} else if (!_unreadCounterSubscription) {
		_unreadCounterSubscription = subscribe(Global::RefUnreadCounterUpdate(), [this] {
			rtlupdate(0, 0, st::titleUnreadCounterRight, st::titleUnreadCounterTop);
		});
	}
}

Ui::RoundButton *TopBarWidget::mediaTypeButton() {
	return _mediaType;
}

} // namespace Window
