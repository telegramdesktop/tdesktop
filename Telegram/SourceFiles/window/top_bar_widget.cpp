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

#include "boxes/addcontactbox.h"
#include "boxes/confirmbox.h"
#include "mainwidget.h"
#include "shortcuts.h"
#include "lang.h"
#include "ui/buttons/peer_avatar_button.h"
#include "ui/flatbutton.h"

namespace Window {

TopBarWidget::TopBarWidget(MainWidget *w) : TWidget(w)
, a_over(0)
, _a_appearance(animation(this, &TopBarWidget::step_appearance))
, _selPeer(0)
, _selCount(0)
, _canDelete(false)
, _selStrLeft(-st::topBarButton.width / 2)
, _selStrWidth(0)
, _animating(false)
, _clearSelection(this, lang(lng_selected_clear), st::topBarButton)
, _forward(this, lang(lng_selected_forward), st::topBarActionButton)
, _delete(this, lang(lng_selected_delete), st::topBarActionButton)
, _selectionButtonsWidth(_clearSelection->width() + _forward->width() + _delete->width())
, _forwardDeleteWidth(qMax(_forward->textWidth(), _delete->textWidth()))
, _info(this, nullptr, st::infoButton)
, _edit(this, lang(lng_profile_edit_contact), st::topBarButton)
, _leaveGroup(this, lang(lng_profile_delete_and_exit), st::topBarButton)
, _addContact(this, lang(lng_profile_add_contact), st::topBarButton)
, _deleteContact(this, lang(lng_profile_delete_contact), st::topBarButton)
, _mediaType(this, lang(lng_media_type), st::topBarButton)
, _search(this, st::topBarSearch)
, _sideShadow(this, st::shadowColor) {

	connect(_forward, SIGNAL(clicked()), this, SLOT(onForwardSelection()));
	connect(_delete, SIGNAL(clicked()), this, SLOT(onDeleteSelection()));
	connect(_clearSelection, SIGNAL(clicked()), this, SLOT(onClearSelection()));
	connect(_info, SIGNAL(clicked()), this, SLOT(onInfoClicked()));
	connect(_addContact, SIGNAL(clicked()), this, SLOT(onAddContact()));
	connect(_deleteContact, SIGNAL(clicked()), this, SLOT(onDeleteContact()));
	connect(_edit, SIGNAL(clicked()), this, SLOT(onEdit()));
	connect(_leaveGroup, SIGNAL(clicked()), this, SLOT(onDeleteAndExit()));
	connect(_search, SIGNAL(clicked()), this, SLOT(onSearch()));

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
	if (p) App::main()->showPeerProfile(p);
}

void TopBarWidget::onAddContact() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	UserData *u = p ? p->asUser() : 0;
	if (u) Ui::showLayer(new AddContactBox(u->firstName, u->lastName, u->phone.isEmpty() ? App::phoneFromSharedContact(peerToUser(u->id)) : u->phone));
}

void TopBarWidget::onEdit() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	if (p) {
		if (p->isChannel()) {
			Ui::showLayer(new EditChannelBox(p->asChannel()));
		} else if (p->isChat()) {
			Ui::showLayer(new EditNameTitleBox(p));
		} else if (p->isUser()) {
			Ui::showLayer(new AddContactBox(p->asUser()));
		}
	}
}

void TopBarWidget::onDeleteContact() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	UserData *u = p ? p->asUser() : 0;
	if (u) {
		ConfirmBox *box = new ConfirmBox(lng_sure_delete_contact(lt_contact, p->name), lang(lng_box_delete));
		connect(box, SIGNAL(confirmed()), this, SLOT(onDeleteContactSure()));
		Ui::showLayer(box);
	}
}

void TopBarWidget::onDeleteContactSure() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	UserData *u = p ? p->asUser() : 0;
	if (u) {
		Ui::showChatsList();
		Ui::hideLayer();
		MTP::send(MTPcontacts_DeleteContact(u->inputUser), App::main()->rpcDone(&MainWidget::deletedContact, u));
	}
}

void TopBarWidget::onDeleteAndExit() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	ChatData *c = p ? p->asChat() : 0;
	if (c) {
		ConfirmBox *box = new ConfirmBox(lng_sure_delete_and_exit(lt_group, p->name), lang(lng_box_leave), st::attentionBoxButton);
		connect(box, SIGNAL(confirmed()), this, SLOT(onDeleteAndExitSure()));
		Ui::showLayer(box);
	}
}

void TopBarWidget::onDeleteAndExitSure() {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	ChatData *c = p ? p->asChat() : 0;
	if (c) {
		Ui::showChatsList();
		Ui::hideLayer();
		MTP::send(MTPmessages_DeleteChatUser(c->inputChat, App::self()->inputUser), App::main()->rpcDone(&MainWidget::deleteHistoryAfterLeave, p), App::main()->rpcFail(&MainWidget::leaveChatFailed, p));
	}
}

void TopBarWidget::onSearch() {
	Shortcuts::launch(qsl("search"));
}

void TopBarWidget::enterEvent(QEvent *e) {
	a_over.start(1);
	_a_appearance.start();
}

void TopBarWidget::enterFromChildEvent(QEvent *e) {
	a_over.start(1);
	_a_appearance.start();
}

void TopBarWidget::leaveEvent(QEvent *e) {
	a_over.start(0);
	_a_appearance.start();
}

void TopBarWidget::leaveToChildEvent(QEvent *e) {
	a_over.start(0);
	_a_appearance.start();
}

void TopBarWidget::step_appearance(float64 ms, bool timer) {
	float64 dt = ms / st::topBarDuration;
	if (dt >= 1) {
		_a_appearance.stop();
		a_over.finish();
	} else {
		a_over.update(dt, anim::linear);
	}
	if (timer) update();
}

void TopBarWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (e->rect().top() < st::topBarHeight) { // optimize shadow-only drawing
		p.fillRect(QRect(0, 0, width(), st::topBarHeight), st::topBarBG->b);
		if (_clearSelection->isHidden()) {
			p.save();
			main()->paintTopBar(p, a_over.current(), _info->isHidden() ? 0 : _info->width());
			p.restore();
		} else {
			p.setFont(st::linkFont->f);
			p.setPen(st::btnDefLink.color->p);
			p.drawText(_selStrLeft, st::topBarButton.textTop + st::linkFont->ascent, _selStr);
		}
	}
}

void TopBarWidget::mousePressEvent(QMouseEvent *e) {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	if (e->button() == Qt::LeftButton && e->pos().y() < st::topBarHeight && (p || !_selCount)) {
		emit clicked();
	}
}

void TopBarWidget::resizeEvent(QResizeEvent *e) {
	int32 r = width();
	if (!_forward->isHidden() || !_delete->isHidden()) {
		int32 fullW = r - (_selectionButtonsWidth + (_selStrWidth - st::topBarButton.width) + st::topBarActionSkip);
		int32 selectedClearWidth = st::topBarButton.width, forwardDeleteWidth = st::topBarActionButton.width - _forwardDeleteWidth, skip = st::topBarActionSkip;
		while (fullW < 0) {
			int fit = 0;
			if (selectedClearWidth < -2 * (st::topBarMinPadding + 1)) {
				fullW += 4;
				selectedClearWidth += 2;
			} else if (selectedClearWidth < -2 * st::topBarMinPadding) {
				fullW += (-2 * st::topBarMinPadding - selectedClearWidth) * 2;
				selectedClearWidth = -2 * st::topBarMinPadding;
			} else {
				++fit;
			}
			if (fullW >= 0) break;

			if (forwardDeleteWidth > 2 * (st::topBarMinPadding + 1)) {
				fullW += 4;
				forwardDeleteWidth -= 2;
			} else if (forwardDeleteWidth > 2 * st::topBarMinPadding) {
				fullW += (forwardDeleteWidth - 2 * st::topBarMinPadding) * 2;
				forwardDeleteWidth = 2 * st::topBarMinPadding;
			} else {
				++fit;
			}
			if (fullW >= 0) break;

			if (skip > st::topBarMinPadding) {
				--skip;
				++fullW;
			} else {
				++fit;
			}
			if (fullW >= 0 || fit >= 3) break;
		}
		_clearSelection->setWidth(selectedClearWidth);
		_forward->setWidth(_forwardDeleteWidth + forwardDeleteWidth);
		_delete->setWidth(_forwardDeleteWidth + forwardDeleteWidth);
		_selStrLeft = -selectedClearWidth / 2;

		int32 availX = _selStrLeft + _selStrWidth, availW = r - (_clearSelection->width() + selectedClearWidth / 2) - availX;
		if (_forward->isHidden()) {
			_delete->move(availX + (availW - _delete->width()) / 2, (st::topBarHeight - _forward->height()) / 2);
		} else if (_delete->isHidden()) {
			_forward->move(availX + (availW - _forward->width()) / 2, (st::topBarHeight - _forward->height()) / 2);
		} else {
			_forward->move(availX + (availW - _forward->width() - _delete->width() - skip) / 2, (st::topBarHeight - _forward->height()) / 2);
			_delete->move(availX + (availW + _forward->width() - _delete->width() + skip) / 2, (st::topBarHeight - _forward->height()) / 2);
		}
		_clearSelection->move(r -= _clearSelection->width(), 0);
	}
	if (!_info->isHidden()) _info->move(r -= _info->width(), 0);
	if (!_deleteContact->isHidden()) _deleteContact->move(r -= _deleteContact->width(), 0);
	if (!_leaveGroup->isHidden()) _leaveGroup->move(r -= _leaveGroup->width(), 0);
	if (!_edit->isHidden()) _edit->move(r -= _edit->width(), 0);
	if (!_addContact->isHidden()) _addContact->move(r -= _addContact->width(), 0);
	if (!_mediaType->isHidden()) _mediaType->move(r -= _mediaType->width(), 0);
	_search->move(width() - (_info->isHidden() ? st::topBarForwardPadding.right() : _info->width()) - _search->width(), 0);

	_sideShadow->resize(st::lineWidth, height());
	_sideShadow->moveToLeft(0, 0);
}

void TopBarWidget::startAnim() {
	_info->hide();
	_edit->hide();
	_leaveGroup->hide();
	_addContact->hide();
	_deleteContact->hide();
	_clearSelection->hide();
	_delete->hide();
	_forward->hide();
	_mediaType->hide();
	_search->hide();

	_animating = true;
}

void TopBarWidget::stopAnim() {
	_animating = false;
	_sideShadow->setVisible(!Adaptive::OneColumn());
	showAll();
}

void TopBarWidget::showAll() {
	if (_animating) {
		resizeEvent(0);
		return;
	}
	PeerData *p = App::main() ? App::main()->profilePeer() : 0, *h = App::main() ? App::main()->historyPeer() : 0, *o = App::main() ? App::main()->overviewPeer() : 0;
	if (p && (p->isChat() || (p->isUser() && (p->asUser()->contact >= 0 || !App::phoneFromSharedContact(peerToUser(p->id)).isEmpty())))) {
		if (p->isChat()) {
			if (p->asChat()->canEdit()) {
				_edit->show();
			} else {
				_edit->hide();
			}
			_leaveGroup->show();
			_addContact->hide();
			_deleteContact->hide();
		} else if (p->asUser()->contact > 0) {
			_edit->show();
			_leaveGroup->hide();
			_addContact->hide();
			_deleteContact->show();
		} else {
			_edit->hide();
			_leaveGroup->hide();
			_addContact->show();
			_deleteContact->hide();
		}
		_clearSelection->hide();
		_info->hide();
		_delete->hide();
		_forward->hide();
		_mediaType->hide();
		_search->hide();
	} else {
		if (p && p->isChannel() && (p->asChannel()->amCreator() || (p->isMegagroup() && p->asChannel()->amEditor()))) {
			_edit->show();
		} else {
			_edit->hide();
		}
		_leaveGroup->hide();
		_addContact->hide();
		_deleteContact->hide();
		if (!p && _selCount) {
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
		if (h && !o && !p && _clearSelection->isHidden()) {
			if (Adaptive::OneColumn()) {
				_info->setPeer(h);
				_info->show();
			} else {
				_info->hide();
			}
			_search->show();
		} else {
			_search->hide();
			_info->hide();
		}
	}
	_sideShadow->setVisible(!Adaptive::OneColumn());
	resizeEvent(nullptr);
}

void TopBarWidget::showSelected(uint32 selCount, bool canDelete) {
	PeerData *p = App::main() ? App::main()->profilePeer() : 0;
	_selPeer = App::main()->overviewPeer() ? App::main()->overviewPeer() : App::main()->peer();
	_selCount = selCount;
	_canDelete = canDelete;
	_selStr = (_selCount > 0) ? lng_selected_count(lt_count, _selCount) : QString();
	_selStrWidth = st::btnDefLink.font->width(_selStr);
	setCursor((!p && _selCount) ? style::cur_default : style::cur_pointer);
	showAll();
}

void TopBarWidget::updateAdaptiveLayout() {
	showAll();
}

FlatButton *TopBarWidget::mediaTypeButton() {
	return _mediaType;
}

MainWidget *TopBarWidget::main() {
	return static_cast<MainWidget*>(parentWidget());
}

} // namespace Window
