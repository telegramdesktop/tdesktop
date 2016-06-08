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
, a_over(0)
, _a_appearance(animation(this, &TopBarWidget::step_appearance))
, _selPeer(0)
, _selCount(0)
, _canDelete(false)
, _selStrLeft((-st::topBarClearButton.width + st::topBarClearButton.padding.left() + st::topBarClearButton.padding.right()) / 2)
, _selStrWidth(0)
, _animating(false)
, _clearSelection(this, lang(lng_selected_clear), st::topBarClearButton)
, _forward(this, lang(lng_selected_forward), st::topBarActionButton)
, _delete(this, lang(lng_selected_delete), st::topBarActionButton)
, _selectionButtonsWidth(_clearSelection->width() + _forward->width() + _delete->width())
, _forwardDeleteWidth(qMax(_forward->textWidth(), _delete->textWidth()))
, _info(this, nullptr, st::infoButton)
, _mediaType(this, lang(lng_media_type), st::topBarButton)
, _search(this, st::topBarSearch) {

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
	} else {
		p.setFont(st::linkFont);
		p.setPen(st::btnDefLink.color);
		p.drawText(_selStrLeft, st::topBarClearButton.padding.top() + st::topBarClearButton.textTop + st::linkFont->ascent, _selStr);
	}
}

void TopBarWidget::mousePressEvent(QMouseEvent *e) {
	PeerData *p = nullptr;// App::main() ? App::main()->profilePeer() : 0;
	if (e->button() == Qt::LeftButton && e->pos().y() < st::topBarHeight && (p || !_selCount)) {
		emit clicked();
	}
}

void TopBarWidget::resizeEvent(QResizeEvent *e) {
	int32 r = width();
	if (!_forward->isHidden() || !_delete->isHidden()) {
		int fullW = r - (_selectionButtonsWidth + (_selStrWidth - st::topBarClearButton.width + st::topBarClearButton.padding.left() + st::topBarClearButton.padding.right()) + st::topBarActionSkip);
		int selectedClearWidth = st::topBarClearButton.width - st::topBarClearButton.padding.left() - st::topBarClearButton.padding.right();
		int forwardDeleteWidth = st::topBarActionButton.width - _forwardDeleteWidth;
		int skip = st::topBarActionSkip;
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
		_clearSelection->setFullWidth(selectedClearWidth);
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

	_animating = true;
}

void TopBarWidget::stopAnim() {
	_animating = false;
	showAll();
}

void TopBarWidget::showAll() {
	if (_animating) {
		resizeEvent(0);
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
	resizeEvent(nullptr);
}

void TopBarWidget::showSelected(uint32 selCount, bool canDelete) {
	PeerData *p = nullptr;// App::main() ? App::main()->profilePeer() : 0;
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

Ui::RoundButton *TopBarWidget::mediaTypeButton() {
	return _mediaType;
}

MainWidget *TopBarWidget::main() {
	return static_cast<MainWidget*>(parentWidget());
}

} // namespace Window
