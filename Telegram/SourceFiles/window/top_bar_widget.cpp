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
#include "stdafx.h"
#include "window/top_bar_widget.h"

#include "styles/style_window.h"
#include "boxes/addcontactbox.h"
#include "boxes/confirmbox.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "shortcuts.h"
#include "lang.h"
#include "ui/buttons/peer_avatar_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "dialogs/dialogs_layout.h"

namespace Window {

TopBarWidget::TopBarWidget(MainWidget *w) : TWidget(w)
, _clearSelection(this, lang(lng_selected_clear), st::topBarClearButton)
, _forward(this, lang(lng_selected_forward), st::defaultActiveButton)
, _delete(this, lang(lng_selected_delete), st::defaultActiveButton)
, _info(this, nullptr, st::topBarInfoButton)
, _mediaType(this, lang(lng_media_type), st::topBarButton)
, _search(this, st::topBarSearch)
, _menuToggle(this, st::topBarMenuToggle) {
	_forward->setClickedCallback([this] { onForwardSelection(); });
	_forward->setWidthChangedCallback([this] { updateControlsGeometry(); });
	_delete->setClickedCallback([this] { onDeleteSelection(); });
	_delete->setWidthChangedCallback([this] { updateControlsGeometry(); });
	_clearSelection->setClickedCallback([this] { onClearSelection(); });
	_info->setClickedCallback([this] { onInfoClicked(); });
	_search->setClickedCallback([this] { onSearch(); });
	_menuToggle->setClickedCallback([this] { showMenu(); });

	subscribe(w->searchInPeerChanged(), [this](PeerData *peer) {
		_searchInPeer = peer;
		auto historyPeer = App::main() ? App::main()->historyPeer() : nullptr;
		_search->setForceRippled(historyPeer && historyPeer == _searchInPeer);
	});
	subscribe(w->historyPeerChanged(), [this](PeerData *peer) {
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

	setCursor(style::cur_pointer);
	showAll();
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
	PeerData *p = App::main() ? App::main()->historyPeer() : 0;
	if (p) Ui::showPeerProfile(p);
}

void TopBarWidget::onSearch() {
	if (auto main = App::main()) {
		if (auto peer = main->peer()) {
			main->searchInPeer(peer);
		}
	}
}

void TopBarWidget::showMenu() {
	if (auto main = App::main()) {
		if (auto peer = main->peer()) {
			if (!_menu) {
				_menu.create(App::main());
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
				App::main()->fillPeerMenu(peer, [this](const QString &text, base::lambda<void()> &&callback) {
					return _menu->addAction(text, std_::move(callback));
				}, false);
				_menu->moveToRight(st::topBarMenuPosition.x(), st::topBarMenuPosition.y());
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
			main()->setMembersShowAreaActive(true);
			break;

		case QEvent::Leave:
			main()->setMembersShowAreaActive(false);
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
		auto paintCounter = main()->paintTopBar(p, decreaseWidth, ms);
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
	buttonsLeft += _forward->width() + st::topBarActionSkip;

	_delete->moveToLeft(buttonsLeft, selectedButtonsTop);
	_clearSelection->moveToRight(st::topBarActionSkip, selectedButtonsTop);

	_info->moveToRight(0, otherButtonsTop);
	_menuToggle->moveToRight(0, otherButtonsTop);
	_mediaType->moveToRight(0, otherButtonsTop);
	_search->moveToRight(_info->isHidden() ? _menuToggle->width() : _info->width(), otherButtonsTop);
}

void TopBarWidget::startAnim() {
	_info->hide();
	_clearSelection->hide();
	_delete->hide();
	_forward->hide();
	_mediaType->hide();
	_search->hide();
	_menuToggle->hide();
	_menu.destroy();
	if (_membersShowArea) {
		_membersShowArea->hide();
	}

	_animating = true;
}

void TopBarWidget::stopAnim() {
	_animating = false;
	updateMembersShowArea();
	showAll();
}

void TopBarWidget::showAll() {
	if (_animating) {
		updateControlsGeometry();
		return;
	}
	auto historyPeer = App::main() ? App::main()->historyPeer() : nullptr;
	auto overviewPeer = App::main() ? App::main()->overviewPeer() : nullptr;

	_clearSelection->show();
	_delete->setVisible(_canDelete);
	_forward->show();

	_mediaType->setVisible(App::main() ? App::main()->mediaTypeSwitch() : false);
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
	} else {
		_search->hide();
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
			main()->setMembersShowAreaActive(false);
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

void TopBarWidget::showSelected(int selectedCount, bool canDelete) {
	if (_selectedCount == selectedCount && _canDelete == canDelete) {
		return;
	}
	if (selectedCount == 0) {
		// Don't change the visible buttons if the selection is cancelled.
		canDelete = _canDelete;
	}

	auto wasSelected = (_selectedCount > 0);
	_selectedCount = selectedCount;
	if (_selectedCount > 0) {
		_forward->setNumbersText(_selectedCount);
		_delete->setNumbersText(_selectedCount);
		if (!wasSelected) {
			_forward->finishNumbersAnimation();
			_delete->finishNumbersAnimation();
		}
	}
	auto hasSelected = (_selectedCount > 0);
	if (_canDelete != canDelete) {
		_canDelete = canDelete;
		showAll();
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
	showAll();
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

MainWidget *TopBarWidget::main() {
	return static_cast<MainWidget*>(parentWidget());
}

} // namespace Window
