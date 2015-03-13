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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "backgroundbox.h"
#include "mainwidget.h"
#include "window.h"
#include "settingswidget.h"

BackgroundInner::BackgroundInner() :
_bgCount(0), _rows(0), _over(-1), _overDown(-1) {
	if (App::cServerBackgrounds().isEmpty()) {
		resize(BackgroundsInRow * (st::backgroundSize.width() + st::backgroundPadding) + st::backgroundPadding, 2 * (st::backgroundSize.height() + st::backgroundPadding) + st::backgroundPadding);
		MTP::send(MTPaccount_GetWallPapers(), rpcDone(&BackgroundInner::gotWallpapers));
	} else {
		updateWallpapers();
	}
	setMouseTracking(true);
}

void BackgroundInner::gotWallpapers(const MTPVector<MTPWallPaper> &result) {
	App::WallPapers wallpapers;

	wallpapers.push_back(App::WallPaper(0, ImagePtr(st::msgBG), ImagePtr(st::msgBG)));
	const QVector<MTPWallPaper> &v(result.c_vector().v);
	for (int i = 0, l = v.size(); i < l; ++i) {
		const MTPWallPaper w(v.at(i));
		switch (w.type()) {
		case mtpc_wallPaper: {
			const MTPDwallPaper &d(w.c_wallPaper());
			const QVector<MTPPhotoSize> &sizes(d.vsizes.c_vector().v);
			const MTPPhotoSize *thumb = 0, *full = 0;
			int32 thumbLevel = -1, fullLevel = -1;
			for (QVector<MTPPhotoSize>::const_iterator j = sizes.cbegin(), e = sizes.cend(); j != e; ++j) {
				char size = 0;
				int32 w = 0, h = 0;
				switch (j->type()) {
				case mtpc_photoSize: {
					const string &s(j->c_photoSize().vtype.c_string().v);
					if (s.size()) size = s[0];
					w = j->c_photoSize().vw.v;
					h = j->c_photoSize().vh.v;
				} break;

				case mtpc_photoCachedSize: {
					const string &s(j->c_photoCachedSize().vtype.c_string().v);
					if (s.size()) size = s[0];
					w = j->c_photoCachedSize().vw.v;
					h = j->c_photoCachedSize().vh.v;
				} break;
				}
				if (!size || !w || !h) continue;

				int32 newThumbLevel = qAbs((st::backgroundSize.width() * cIntRetinaFactor()) - w), newFullLevel = qAbs(2560 - w);
				if (thumbLevel < 0 || newThumbLevel < thumbLevel) {
					thumbLevel = newThumbLevel;
					thumb = &(*j);
				}
				if (fullLevel < 0 || newFullLevel < fullLevel) {
					fullLevel = newFullLevel;
					full = &(*j);
				}
			}
			if (thumb && full && full->type() != mtpc_photoSizeEmpty) {
				wallpapers.push_back(App::WallPaper(d.vid.v, App::image(*thumb), App::image(*full)));
			}
		} break;

		case mtpc_wallPaperSolid: {
			const MTPDwallPaperSolid &d(w.c_wallPaperSolid());
		} break;
		}
	}

	App::cSetServerBackgrounds(wallpapers);
	updateWallpapers();
}

void BackgroundInner::updateWallpapers() {
	_bgCount = App::cServerBackgrounds().size();
	_rows = _bgCount / BackgroundsInRow;
	if (_bgCount % BackgroundsInRow) ++_rows;

	resize(BackgroundsInRow * (st::backgroundSize.width() + st::backgroundPadding) + st::backgroundPadding, _rows * (st::backgroundSize.height() + st::backgroundPadding) + st::backgroundPadding);
	for (int i = 0; i < BackgroundsInRow * 3; ++i) {
		if (i >= _bgCount) break;

		App::cServerBackgrounds().at(i).thumb->load();
	}
}

void BackgroundInner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	QPainter p(this);

	if (_rows) {
		for (int i = 0; i < _rows; ++i) {
			if ((st::backgroundSize.height() + st::backgroundPadding) * (i + 1) <= r.top()) continue;
			for (int j = 0; j < BackgroundsInRow; ++j) {
				int index = i * BackgroundsInRow + j;
				if (index >= _bgCount) break;

				const App::WallPaper &paper(App::cServerBackgrounds().at(index));
				paper.thumb->load();

				int x = st::backgroundPadding + j * (st::backgroundSize.width() + st::backgroundPadding);
				int y = st::backgroundPadding + i * (st::backgroundSize.height() + st::backgroundPadding);

				const QPixmap &pix(paper.thumb->pix(st::backgroundSize.width(), st::backgroundSize.height()));
				p.drawPixmap(x, y, pix);

				if (paper.id == cChatBackgroundId()) {
					p.drawPixmap(QPoint(x + st::backgroundSize.width() - st::overviewPhotoChecked.pxWidth(), y + st::backgroundSize.height() - st::overviewPhotoChecked.pxHeight()), App::sprite(), st::overviewPhotoChecked);
				}
			}
		}
	} else {
		p.setFont(st::noContactsFont->f);
		p.setPen(st::noContactsColor->p);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
	}
}

void BackgroundInner::mouseMoveEvent(QMouseEvent *e) {
	int x = e->pos().x(), y = e->pos().y();
	int row = int((y - st::backgroundPadding) / (st::backgroundSize.height() + st::backgroundPadding));
	if (y - row * (st::backgroundSize.height() + st::backgroundPadding) > st::backgroundPadding + st::backgroundSize.height()) row = _rows + 1;

	int col = int((x - st::backgroundPadding) / (st::backgroundSize.width() + st::backgroundPadding));
	if (x - col * (st::backgroundSize.width() + st::backgroundPadding) > st::backgroundPadding + st::backgroundSize.width()) row = _rows + 1;

	int newOver = row * BackgroundsInRow + col;
	if (newOver >= _bgCount) newOver = -1;
	if (newOver != _over) {
		_over = newOver;
		setCursor((_over >= 0 || _overDown >= 0) ? style::cur_pointer : style::cur_default);
	}
}

void BackgroundInner::mousePressEvent(QMouseEvent *e) {
	_overDown = _over;
}

void BackgroundInner::mouseReleaseEvent(QMouseEvent *e) {
	if (_overDown == _over && _over >= 0) {
		emit backgroundChosen(_over);
	} else if (_over < 0) {
		setCursor(style::cur_default);
	}
}

BackgroundInner::~BackgroundInner() {
}

void BackgroundInner::resizeEvent(QResizeEvent *e) {
}

BackgroundBox::BackgroundBox() : _scroll(this, st::backgroundScroll), _inner(),
_close(this, lang(lng_contacts_done), st::contactsClose),
_hiding(false), a_opacity(0, 1) {

	_width = st::participantWidth;
	_height = App::wnd()->height() - st::boxPadding.top() - st::boxPadding.bottom();

	resize(_width, _height);
	resizeEvent(0);

	_scroll.setWidget(&_inner);
	_scroll.setFocusPolicy(Qt::NoFocus);

	connect(&_close, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_inner, SIGNAL(backgroundChosen(int)), this, SLOT(onBackgroundChosen(int)));

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
}

void BackgroundBox::hideAll() {
	_scroll.hide();
	_close.hide();
}

void BackgroundBox::showAll() {
	_scroll.show();
	_close.show();
	_close.raise();
}

void BackgroundBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		onClose();
	} else {
		e->ignore();
	}
}

void BackgroundBox::parentResized() {
	QSize s = parentWidget()->size();
	_height = App::wnd()->height() - st::boxPadding.top() - st::boxPadding.bottom();
	if (_height > st::participantMaxHeight) _height = st::participantMaxHeight;
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void BackgroundBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(QRect(QPoint(0, 0), size()), st::boxBG->b);

			// draw box title / text
			p.setFont(st::boxFont->f);
			p.setPen(st::boxGrayTitle->p);
			p.drawText(QRect(st::addContactTitlePos.x(), st::addContactTitlePos.y(), _width - 2 * st::addContactTitlePos.x(), st::boxFont->height), lang(lng_backgrounds_header), style::al_top);
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void BackgroundBox::resizeEvent(QResizeEvent *e) {
	LayeredWidget::resizeEvent(e);
	_inner.resize(_width, _inner.height());
	_scroll.resize(_width, _height - st::boxFont->height - st::newGroupNamePadding.top() - st::newGroupNamePadding.bottom() - _close.height());
	_scroll.move(0, st::boxFont->height + st::newGroupNamePadding.top() + st::newGroupNamePadding.bottom());
	_close.move(0, _height - _close.height());
}

void BackgroundBox::animStep(float64 dt) {
	if (dt >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
		}
	} else {
		a_opacity.update(dt, anim::linear);
	}
	update();
}

void BackgroundBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

void BackgroundBox::onBackgroundChosen(int index) {
	if (index >= 0 && index < App::cServerBackgrounds().size()) {
		const App::WallPaper &paper(App::cServerBackgrounds().at(index));
		if (App::main()) App::main()->setChatBackground(paper);
		if (App::settings()) App::settings()->needBackgroundUpdate(!paper.id);
	}
	emit closed();
}

void BackgroundBox::onClose() {
	emit closed();
}

BackgroundBox::~BackgroundBox() {

}
