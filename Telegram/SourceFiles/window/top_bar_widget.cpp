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
#include "ui/buttons/round_button.h"
#include "ui/flatbutton.h"

namespace Window {

TopBarWidget::TopBarWidget(MainWidget *w) : TWidget(w)
, _a_appearance(animation(this, &TopBarWidget::step_appearance))
, _clearSelection(this, lang(lng_selected_clear), st::topBarClearButton)
, _forward(this, lang(lng_selected_forward), st::defaultActiveButton)
, _delete(this, lang(lng_selected_delete), st::defaultActiveButton)
, _info(this, nullptr, st::infoButton)
, _mediaType(this, lang(lng_media_type), st::topBarButton)
, _search(this, st::topBarSearch) {
	_clearSelection->setTextTransform(Ui::RoundButton::TextTransform::ToUpper);
	_forward->setTextTransform(Ui::RoundButton::TextTransform::ToUpper);
	_delete->setTextTransform(Ui::RoundButton::TextTransform::ToUpper);

	connect(_forward, SIGNAL(clicked()), this, SLOT(onForwardSelection()));
	connect(_delete, SIGNAL(clicked()), this, SLOT(onDeleteSelection()));
	connect(_clearSelection, SIGNAL(clicked()), this, SLOT(onClearSelection()));
	connect(_info, SIGNAL(clicked()), this, SLOT(onInfoClicked()));
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
	if (p) Ui::showPeerProfile(p);
}

void TopBarWidget::onSearch() {
	Shortcuts::launch(qsl("search"));
}

void TopBarWidget::enterEvent(QEvent *e) {
	a_over.start(1);
	_a_appearance.start();
}

void TopBarWidget::enterFromChildEvent(QEvent *e, QWidget *child) {
	if (child != _membersShowArea) {
		a_over.start(1);
		_a_appearance.start();
	}
}

void TopBarWidget::leaveEvent(QEvent *e) {
	a_over.start(0);
	_a_appearance.start();
}

void TopBarWidget::leaveToChildEvent(QEvent *e, QWidget *child) {
	if (child != _membersShowArea) {
		a_over.start(0);
		_a_appearance.start();
	}
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

	p.fillRect(QRect(0, 0, width(), st::topBarHeight), st::topBarBG->b);
	if (_clearSelection->isHidden()) {
		p.save();
		int decreaseWidth = 0;
		if (!_info->isHidden()) {
			decreaseWidth += _info->width();
			decreaseWidth -= st::topBarForwardPadding.right();
		}
		if (!_search->isHidden()) {
			decreaseWidth += _search->width();
		}
		main()->paintTopBar(p, a_over.current(), decreaseWidth);
		p.restore();
	}
}

void TopBarWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && e->pos().y() < st::topBarHeight && !_selCount) {
		emit clicked();
	}
}

void TopBarWidget::resizeEvent(QResizeEvent *e) {
	int r = width();

	int buttonsLeft = st::topBarActionSkip + (Adaptive::OneColumn() ? 0 : st::lineWidth);
	int buttonsWidth = _forward->contentWidth() + _delete->contentWidth() + _clearSelection->width();
	buttonsWidth += buttonsLeft + st::topBarActionSkip * 3;

	int widthLeft = qMin(r - buttonsWidth, -2 * st::defaultActiveButton.width);
	_forward->setFullWidth(-(widthLeft / 2));
	_delete->setFullWidth(-(widthLeft / 2));

	int buttonsTop = (height() - _forward->height()) / 2;

	_forward->moveToLeft(buttonsLeft, buttonsTop);
	buttonsLeft += _forward->width() + st::topBarActionSkip;

	_delete->moveToLeft(buttonsLeft, buttonsTop);
	_clearSelection->moveToRight(st::topBarActionSkip, buttonsTop);

	if (!_info->isHidden()) _info->move(r -= _info->width(), 0);
	if (!_mediaType->isHidden()) _mediaType->move(r -= _mediaType->width(), 0);
	_search->move(width() - (_info->isHidden() ? st::topBarForwardPadding.right() : _info->width()) - _search->width(), 0);
}

void TopBarWidget::startAnim() {
	_info->hide();
	_clearSelection->hide();
	_delete->hide();
	_forward->hide();
	_mediaType->hide();
	_search->hide();
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
	PeerData *h = App::main() ? App::main()->historyPeer() : 0, *o = App::main() ? App::main()->overviewPeer() : 0;
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
	if (h && !o && _clearSelection->isHidden()) {
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
		if (_selPeer->isChat()) {
			return true;
		}
		if (_selPeer->isMegagroup()) {
			return (_selPeer->asMegagroup()->membersCount() < Global::ChatSizeMax());
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
}

Ui::RoundButton *TopBarWidget::mediaTypeButton() {
	return _mediaType;
}

MainWidget *TopBarWidget::main() {
	return static_cast<MainWidget*>(parentWidget());
}

} // namespace Window
