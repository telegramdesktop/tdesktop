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

#include "stickersetbox.h"
#include "mainwidget.h"
#include "window.h"
#include "settingswidget.h"
#include "boxes/confirmbox.h"

#include "localstorage.h"

void StickerSetPanel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), st::emojiPanHeaderBg->b);
}

StickerSetInner::StickerSetInner(const MTPInputStickerSet &set) :
_loaded(false), _setId(0), _setAccess(0), _setCount(0), _setHash(0), _setFlags(0), _bottom(0),
_input(set), _installRequest(0), _panel(0) {
	switch (set.type()) {
	case mtpc_inputStickerSetID: _setId = set.c_inputStickerSetID().vid.v; _setAccess = set.c_inputStickerSetID().vaccess_hash.v; break;
	case mtpc_inputStickerSetShortName: _setShortName = qs(set.c_inputStickerSetShortName().vshort_name); break;
	}
	MTP::send(MTPmessages_GetStickerSet(_input), rpcDone(&StickerSetInner::gotSet), rpcFail(&StickerSetInner::failedSet));
	cSetLastStickersUpdate(0);
	App::main()->updateStickers();
}

void StickerSetInner::gotSet(const MTPmessages_StickerSet &set) {
	_pack.clear();
	if (set.type() == mtpc_messages_stickerSet) {
		const MTPDmessages_stickerSet &d(set.c_messages_stickerSet());
		const QVector<MTPDocument> &v(d.vdocuments.c_vector().v);
		_pack.reserve(v.size());
		for (int32 i = 0, l = v.size(); i < l; ++i) {
			DocumentData *doc = App::feedDocument(v.at(i));
			if (!doc || !doc->sticker()) continue;
			
			_pack.push_back(doc);
		}
		if (d.vset.type() == mtpc_stickerSet) {
			const MTPDstickerSet &s(d.vset.c_stickerSet());
			_setTitle = qs(s.vtitle);
			_title = st::boxTitleFont->m.elidedText(_setTitle, Qt::ElideRight, width() - st::btnStickersClose.width - st::boxTitlePos.x());
			_setShortName = qs(s.vshort_name);
			_setId = s.vid.v;
			_setAccess = s.vaccess_hash.v;
			_setCount = s.vcount.v;
			_setHash = s.vhash.v;
			_setFlags = s.vflags.v;
		}
	}

	if (_pack.isEmpty()) {
		App::wnd()->showLayer(new ConfirmBox(lang(lng_stickers_not_found), true), true);
	} else {
		int32 rows = _pack.size() / StickerPanPerRow + ((_pack.size() % StickerPanPerRow) ? 1 : 0);
		resize(st::stickersPadding + StickerPanPerRow * st::stickersSize.width(), rows * st::stickersSize.height() + st::stickersAddOrShare);
		_panel = new StickerSetPanel(parentWidget());
		_panel->setGeometry(0, parentWidget()->height() - st::stickersAddOrShare, width(), st::stickersAddOrShare);
		_panel->show();
	}
	_loaded = true;

	emit updateButtons();
}

bool StickerSetInner::failedSet(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	_loaded = true;

	App::wnd()->showLayer(new ConfirmBox(lang(lng_stickers_not_found), true), true);

	return true;
}

void StickerSetInner::installDone(const MTPBool &result) {
	StickerSets &sets(cRefStickerSets());

	sets.insert(_setId, StickerSet(_setId, _setAccess, _setTitle, _setShortName, _setCount, _setHash, _setFlags)).value().stickers = _pack;

	int32 insertAtIndex = 0;
	StickerSetsOrder &order(cRefStickerSetsOrder());
	for (int32 s = order.size(); insertAtIndex < s; ++insertAtIndex) {
		StickerSets::const_iterator i = sets.constFind(order.at(insertAtIndex));
		if (i == sets.cend() || !(i->flags & MTPDstickerSet_flag_official)) {
			break;
		}
	}
	int32 currentIndex = cStickerSetsOrder().indexOf(_setId);
	if (currentIndex != insertAtIndex) {
		if (currentIndex > 0) {
			order.removeAt(currentIndex);
			if (currentIndex < insertAtIndex) {
				--insertAtIndex;
			}
		}
		order.insert(insertAtIndex, _setId);
	}

	StickerSets::iterator custom = sets.find(CustomStickerSetId);
	if (custom != sets.cend()) {
		for (int32 i = 0, l = _pack.size(); i < l; ++i) {
			int index = custom->stickers.indexOf(_pack.at(i));
			if (index >= 0) {
				custom->stickers.remove(index);
			}
		}
		if (custom->stickers.isEmpty()) {
			sets.erase(custom);
		}
	}
	cSetStickersHash(QByteArray());
	Local::writeStickers();
	emit installed(_setId);
	App::wnd()->hideLayer();
}

bool StickerSetInner::installFailed(const RPCError &error) {
	if (error.type().startsWith(qsl("FLOOD_WAIT_"))) return false;

	App::wnd()->showLayer(new ConfirmBox(lang(lng_stickers_not_found), true), true);

	return true;
}

void StickerSetInner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	if (_pack.isEmpty()) return;

	int32 rows = _pack.size() / StickerPanPerRow + ((_pack.size() % StickerPanPerRow) ? 1 : 0);
	int32 from = qFloor(e->rect().top() / st::stickersSize.height()), to = qFloor(e->rect().bottom() / st::stickersSize.height()) + 1;

	for (int32 i = from; i < to; ++i) {
		for (int32 j = 0; j < StickerPanPerRow; ++j) {
			int32 index = i * StickerPanPerRow + j;
			if (index >= _pack.size()) break;

			DocumentData *doc = _pack.at(index);
			QPoint pos(st::stickerPanPadding + j * st::stickersSize.width(), i * st::stickersSize.height());

			bool goodThumb = !doc->thumb->isNull() && ((doc->thumb->width() >= 128) || (doc->thumb->height() >= 128));
			if (goodThumb) {
				doc->thumb->load();
			} else {
				bool already = !doc->already().isEmpty(), hasdata = !doc->data.isEmpty();
				if (!doc->loader && doc->status != FileFailed && !already && !hasdata) {
					doc->save(QString());
				}
				if (doc->sticker()->img->isNull() && (already || hasdata)) {
					if (already) {
						doc->sticker()->img = ImagePtr(doc->already());
					} else {
						doc->sticker()->img = ImagePtr(doc->data);
					}
				}
			}

			float64 coef = qMin((st::stickersSize.width() - st::msgRadius * 2) / float64(doc->dimensions.width()), (st::stickersSize.height() - st::msgRadius * 2) / float64(doc->dimensions.height()));
			if (coef > 1) coef = 1;
			int32 w = qRound(coef * doc->dimensions.width()), h = qRound(coef * doc->dimensions.height());
			if (w < 1) w = 1;
			if (h < 1) h = 1;
			QPoint ppos = pos + QPoint((st::stickersSize.width() - w) / 2, (st::stickersSize.height() - h) / 2);
			if (goodThumb) {
				p.drawPixmapLeft(ppos, width(), doc->thumb->pix(w, h));
			} else if (!doc->sticker()->img->isNull()) {
				p.drawPixmapLeft(ppos, width(), doc->sticker()->img->pix(w, h));
			}
		}
	}
}

void StickerSetInner::setScrollBottom(int32 bottom) {
	if (bottom == _bottom) return;

	_bottom = bottom;
}

bool StickerSetInner::loaded() const {
	return _loaded && !_pack.isEmpty();
}

int32 StickerSetInner::notInstalled() const {
	return (_loaded && (cStickerSets().constFind(_setId) == cStickerSets().cend())) ? _pack.size() : 0;
}

bool StickerSetInner::official() const {
	return _loaded && _setShortName.isEmpty();
}

QString StickerSetInner::title() const {
	return _loaded ? (_pack.isEmpty() ? lang(lng_attach_failed) : _title) : lang(lng_contacts_loading);
}

QString StickerSetInner::shortName() const {
	return _setShortName;
}

void StickerSetInner::install() {
	if (_installRequest) return;
	_installRequest = MTP::send(MTPmessages_InstallStickerSet(_input, MTP_bool(false)), rpcDone(&StickerSetInner::installDone), rpcFail(&StickerSetInner::installFailed));
}

StickerSetInner::~StickerSetInner() {
}

StickerSetBox::StickerSetBox(const MTPInputStickerSet &set) : ScrollableBox(st::stickersScroll), _inner(set),
_close(this, st::btnStickersClose),
_addStickers(this, lng_stickers_add_pack(lt_count, 0), st::btnStickersAdd),
_shareStickers(this, lang(lng_stickers_share_pack), st::btnStickersAdd),
_closeStickers(this, lang(lng_close), st::btnStickersAdd) {
	resize(st::stickersWidth, height());
	setMaxHeight(st::stickersMaxHeight);
	connect(App::main(), SIGNAL(stickersUpdated()), this, SLOT(onStickersUpdated()));

	init(&_inner, 0, st::boxFont->height + st::newGroupNamePadding.top() + st::newGroupNamePadding.bottom());

	connect(&_close, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_addStickers, SIGNAL(clicked()), this, SLOT(onAddStickers()));
	connect(&_shareStickers, SIGNAL(clicked()), this, SLOT(onShareStickers()));
	connect(&_closeStickers, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_inner, SIGNAL(updateButtons()), this, SLOT(onUpdateButtons()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	connect(&_inner, SIGNAL(installed(uint64)), this, SIGNAL(installed(uint64)));

	onStickersUpdated();

	onScroll();

	prepare();
}

void StickerSetBox::onStickersUpdated() {
	showAll();
}

void StickerSetBox::onAddStickers() {
	_inner.install();
}

void StickerSetBox::onShareStickers() {
	QString url = qsl("https://telegram.me/addstickers/") + _inner.shortName();
	QApplication::clipboard()->setText(url);
	App::wnd()->showLayer(new ConfirmBox(lang(lng_stickers_copied), true), true);
}

void StickerSetBox::onUpdateButtons() {
	if (!_close.isHidden()) showAll();
}

void StickerSetBox::onScroll() {
	_inner.setScrollBottom(_scroll.scrollTop() + _scroll.height());
}

void StickerSetBox::hideAll() {
	ScrollableBox::hideAll();
	_close.hide();
	_addStickers.hide();
	_shareStickers.hide();
}

void StickerSetBox::showAll() {
	ScrollableBox::showAll();
	_close.show();
	int32 cnt = _inner.notInstalled();
	if (_inner.loaded()) {
		if (_inner.official()) {
			_addStickers.hide();
			_shareStickers.hide();
			_closeStickers.show();
		} else if (_inner.notInstalled()) {
			_addStickers.setText(lng_stickers_add_pack(lt_count, cnt));
			_addStickers.show();
			_addStickers.raise();
			_shareStickers.hide();
			_closeStickers.hide();
		} else {
			_shareStickers.show();
			_shareStickers.raise();
			_addStickers.hide();
			_closeStickers.hide();
		}
	} else {
		_addStickers.hide();
		_shareStickers.hide();
		_closeStickers.hide();
	}
	update();
}

void StickerSetBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, _inner.title(), false);
}

void StickerSetBox::resizeEvent(QResizeEvent *e) {
	ScrollableBox::resizeEvent(e);
	_inner.resize(width(), _inner.height());
	_close.moveToRight(0, 0, width());
	_addStickers.move((width() - _addStickers.width()) / 2, height() - (st::stickersAddOrShare + _addStickers.height()) / 2);
	_shareStickers.move((width() - _shareStickers.width()) / 2, height() - (st::stickersAddOrShare + _shareStickers.height()) / 2);
	_closeStickers.move((width() - _closeStickers.width()) / 2, height() - (st::stickersAddOrShare + _closeStickers.height()) / 2);
}
