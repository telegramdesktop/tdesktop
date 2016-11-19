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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
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
	_delete->setClickedCallback([this] { onDeleteSelection(); });
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

	setCursor(style::cur_pointer);
	showAll();
}

void TopBarWidget::onForwardSelection() {
	if (App::main()) App::main()->forwardSelectedItems();
}

void TopBarWidget::onDeleteSelection() {
	if (App::main()) App::main()->deleteSelectedItems();
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
				struct Data {
					Ui::DropdownMenu *menu = nullptr;
					QPointer<TWidget> that;
				};
				auto data = MakeShared<Data>();
				data->that = weakThis();
				data->menu = _menu.ptr();
				_menu->setHiddenCallback([this, data] {
					data->menu->deleteLater();
					if (data->that && _menu == data->menu) {
						_menu = nullptr;
						_menuToggle->setForceRippled(false);
					}
				});
				_menu->setShowStartCallback([this, data] {
					if (data->that && _menu == data->menu) {
						_menuToggle->setForceRippled(true);
					}
				});
				_menu->setHideStartCallback([this, data] {
					if (data->that && _menu == data->menu) {
						_menuToggle->setForceRippled(false);
					}
				});
				_menuToggle->installEventFilter(_menu);
				App::main()->fillPeerMenu(peer, [this](const QString &text, base::lambda_unique<void()> callback) {
					return _menu->addAction(text, std_::move(callback));
				});
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

	p.fillRect(QRect(0, 0, width(), st::topBarHeight), st::topBarBg);
	if (_clearSelection->isHidden()) {
		p.save();
		int decreaseWidth = 0;
		if (!_info->isHidden()) {
			decreaseWidth += _info->width();
			decreaseWidth -= st::topBarArrowPadding.left();
		}
		if (!_search->isHidden()) {
			decreaseWidth += _search->width();
		}
		auto paintCounter = main()->paintTopBar(p, decreaseWidth);
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
	if (auto counter = App::histories().unreadBadge()) {
		auto counterText = (counter > 99) ? qsl("..%1").arg(counter % 100) : QString::number(counter);
		Dialogs::Layout::UnreadBadgeStyle unreadSt;
		unreadSt.muted = App::histories().unreadOnlyMuted();
		auto unreadRight = st::titleUnreadCounterRight;
		if (rtl()) unreadRight = outerWidth - st::titleUnreadCounterRight;
		auto unreadTop = st::titleUnreadCounterTop;
		Dialogs::Layout::paintUnreadCount(p, counterText, unreadRight, unreadTop, unreadSt);
	}
}

void TopBarWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && e->pos().y() < st::topBarHeight && !_selCount) {
		emit clicked();
	}
}

void TopBarWidget::resizeEvent(QResizeEvent *e) {
	int buttonsLeft = st::topBarActionSkip + (Adaptive::OneColumn() ? 0 : st::lineWidth);
	int buttonsWidth = _forward->contentWidth() + _delete->contentWidth() + _clearSelection->width();
	buttonsWidth += buttonsLeft + st::topBarActionSkip * 3;

	int widthLeft = qMin(width() - buttonsWidth, -2 * st::defaultActiveButton.width);
	_forward->setFullWidth(-(widthLeft / 2));
	_delete->setFullWidth(-(widthLeft / 2));

	int buttonsTop = (height() - _forward->height()) / 2;

	_forward->moveToLeft(buttonsLeft, buttonsTop);
	buttonsLeft += _forward->width() + st::topBarActionSkip;

	_delete->moveToLeft(buttonsLeft, buttonsTop);
	_clearSelection->moveToRight(st::topBarActionSkip, buttonsTop);

	_info->moveToRight(0, 0);
	_menuToggle->moveToRight(0, 0);
	_mediaType->moveToRight(0, 0);
	_search->moveToRight(_info->isHidden() ? _menuToggle->width() : _info->width(), 0);
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
		resizeEvent(nullptr);
		return;
	}
	auto historyPeer = App::main() ? App::main()->historyPeer() : nullptr;
	auto overviewPeer = App::main() ? App::main()->overviewPeer() : nullptr;
	if (_selCount) {
		_clearSelection->show();
		if (_canDelete) {
			_delete->show();
		} else {
			_delete->hide();
		}
		_forward->show();
		_mediaType->hide();
	} else {
		_clearSelection->hide();
		_delete->hide();
		_forward->hide();
		if (App::main() && App::main()->mediaTypeSwitch()) {
			_mediaType->show();
		} else {
			_mediaType->hide();
		}
	}
	if (historyPeer && !overviewPeer && _clearSelection->isHidden()) {
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
	resizeEvent(nullptr);
}

void TopBarWidget::updateMembersShowArea() {
	auto membersShowAreaNeeded = [this]() {
		if (_selCount || App::main()->overviewPeer() || !_selPeer) {
			return false;
		}
		if (auto chat = _selPeer->asChat()) {
			return chat->amIn();
		}
		if (auto megagroup = _selPeer->asMegagroup()) {
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
		_membersShowArea = new TWidget(this);
		_membersShowArea->show();
		_membersShowArea->installEventFilter(this);
	}
	_membersShowArea->setGeometry(App::main()->getMembersShowAreaGeometry());
}

void TopBarWidget::showSelected(uint32 selCount, bool canDelete) {
	_selPeer = App::main()->overviewPeer() ? App::main()->overviewPeer() : App::main()->peer();
	_selCount = selCount;
	if (_selCount > 0) {
		_canDelete = canDelete;
		_forward->setSecondaryText(QString::number(_selCount));
		_delete->setSecondaryText(QString::number(_selCount));
	}
	setCursor(_selCount ? style::cur_default : style::cur_pointer);

	updateMembersShowArea();
	showAll();
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
