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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "backgroundbox.h"
#include "mainwidget.h"
#include "window.h"
#include "settingswidget.h"

BackgroundInner::BackgroundInner() :
_bgCount(0), _rows(0), _over(-1), _overDown(-1) {
	connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(update()));
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

	wallpapers.push_back(App::WallPaper(0, ImagePtr(st::msgBG0), ImagePtr(st::msgBG0)));
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
				wallpapers.push_back(App::WallPaper(d.vid.v ? d.vid.v : INT_MAX, App::image(*thumb), App::image(*full)));
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

BackgroundBox::BackgroundBox() : ItemListBox(st::backgroundScroll)
, _inner() {

	init(&_inner);

	connect(&_inner, SIGNAL(backgroundChosen(int)), this, SLOT(onBackgroundChosen(int)));

	prepare();
}

void BackgroundBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_backgrounds_header));
}

void BackgroundBox::onBackgroundChosen(int index) {
	if (index >= 0 && index < App::cServerBackgrounds().size()) {
		const App::WallPaper &paper(App::cServerBackgrounds().at(index));
		if (App::main()) App::main()->setChatBackground(paper);
		if (App::settings()) App::settings()->needBackgroundUpdate(!paper.id);
	}
	emit closed();
}
