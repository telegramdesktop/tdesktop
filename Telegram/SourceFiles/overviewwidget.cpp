/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"

#include "lang.h"
#include "window.h"
#include "mainwidget.h"
#include "overviewwidget.h"
#include "boxes/addcontactbox.h"
#include "boxes/confirmbox.h"
#include "boxes/photocropbox.h"
#include "application.h"
#include "boxes/addparticipantbox.h"
#include "gui/filedialog.h"

OverviewInner::OverviewInner(OverviewWidget *overview, ScrollArea *scroll, const PeerData *peer, MediaOverviewType type) : TWidget(0),
	_overview(overview),
	_scroll(scroll),
	_resizeIndex(-1),
	_resizeSkip(0),
	_peer(App::peer(peer->id)),
	_type(type),
	_hist(App::history(peer->id)),
	_menu(0),
	_width(0),
	_height(0),
	_minHeight(0) {

	App::contextItem(0);

	mediaOverviewUpdated();
}

void OverviewInner::clear() {
	_cached.clear();
}

bool OverviewInner::event(QEvent *e) {
	if (e->type() == QEvent::MouseMove) {
		QMouseEvent *ev = dynamic_cast<QMouseEvent*>(e);
		if (ev) {
			_lastPos = ev->globalPos();
			updateSelected();
		}
	}
	return QWidget::event(e);
}

QPixmap OverviewInner::genPix(PhotoData *photo, int32 size) {
	size *= cIntRetinaFactor();
	QImage img = (photo->full->loaded() ? photo->full : (photo->medium->loaded() ? photo->medium : photo->thumb))->pix().toImage();
	if (img.width() > img.height()) {
		img = img.scaled(img.width() * size / img.height(), size, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
	} else {
		img = img.scaled(size, img.height() * size / img.width(), Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
	}
	img.setDevicePixelRatio(cRetinaFactor());
	photo->forget();
	return QPixmap::fromImage(img);
}

void OverviewInner::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	QRect r(e->rect());
	p.setClipRect(r);

	if (_hist->_overview[_type].isEmpty()) {
		QPoint dogPos((width() - st::msgDogImg.pxWidth()) / 2, ((height() - st::msgDogImg.pxHeight()) * 4) / 9);
		p.drawPixmap(dogPos, App::sprite(), st::msgDogImg);
		return;
	}
	if (_type == OverviewPhotos) {
		int32 rowFrom = int32(r.top() - st::overviewPhotoSkip) / int32(_vsize + st::overviewPhotoSkip);
		int32 rowTo = int32(r.bottom() - st::overviewPhotoSkip) / int32(_vsize + st::overviewPhotoSkip) + 1;
		History::MediaOverview &overview(_hist->_overview[_type]);
		int32 count = overview.size();
		float64 w = float64(width() - st::overviewPhotoSkip) / _photosInRow;
		for (int32 row = rowFrom; row < rowTo; ++row) {
			if (row * _photosInRow >= count) break;
			for (int32 i = 0; i < _photosInRow; ++i) {
				int32 index = row * _photosInRow + i;
				if (index >= count) break;

				HistoryItem *item = App::histItemById(overview[count - index - 1]);
				HistoryMedia *m = item ? item->getMedia(true) : 0;
				if (!m) continue;

				switch (m->type()) {
				case MediaTypePhoto: {
					PhotoData *photo = static_cast<HistoryPhoto*>(m)->photo();
					bool quality = photo->full->loaded();
					if (!quality) {
						if (photo->thumb->loaded()) {
							photo->medium->load(false, false);
							quality = photo->medium->loaded();
						} else {
							photo->thumb->load();
						}
					}
					CachedSizes::iterator it = _cached.find(photo);
					if (it == _cached.cend()) {
						CachedSize size;
						size.medium = quality;
						size.vsize = _vsize;
						size.pix = genPix(photo, _vsize);
						it = _cached.insert(photo, size);
					} else if (it->medium != quality || it->vsize != _vsize) {
						it->medium = quality;
						it->vsize = _vsize;
						it->pix = genPix(photo, _vsize);
					}
					QPixmap &pix(it->pix);
					QPoint pos(int32(i * w + st::overviewPhotoSkip), row * (_vsize + st::overviewPhotoSkip) + st::overviewPhotoSkip);
					int32 w = pix.width(), h = pix.height();
					if (w == h) {
						p.drawPixmap(pos, pix);
					} else if (w > h) {
						p.drawPixmap(pos, pix, QRect((w - h) / 2, 0, h, h));
					} else {
						p.drawPixmap(pos, pix, QRect(0, (h - w) / 2, w, w));
					}
				} break;
				}
			}
		}
	} else {
		int32 addToY = (_height < _minHeight ? (_minHeight - _height) : 0);
		p.translate(0, st::msgMargin.top() + addToY);
		int32 y = 0, w = _width - st::msgMargin.left() - st::msgMargin.right();
		for (int32 i = _items.size(); i > 0;) {
			--i;
			if (!i || (addToY + _height - _items[i - 1].y > r.top())) {
				int32 curY = _height - _items[i].y;
				if (addToY + curY >= r.bottom()) break;

				p.translate(0, curY - y);
				if (_items[i].msgid) { // draw item
					HistoryItem *item = App::histItemById(_items[i].msgid);
					HistoryMedia *media = item ? item->getMedia(true) : 0;
					if (media) {
						bool out = item->out();
						int32 mw = media->maxWidth(), left = (out ? st::msgMargin.right() : st::msgMargin.left()) + (out && mw < w ? (w - mw) : 0);
						if (!out && _hist->peer->chat) {
							p.drawPixmap(left, media->height() - st::msgPhotoSize, item->from()->photo->pix(st::msgPhotoSize));
							left += st::msgPhotoSkip;
						}
						p.save();
						p.translate(left, 0);
						media->draw(p, item, false, w);
						p.restore();
					}
				} else {
					QString str = langDayOfMonth(_items[i].date);

					int32 left = st::msgServiceMargin.left(), width = _width - st::msgServiceMargin.left() - st::msgServiceMargin.left(), height = st::msgServiceFont->height + st::msgServicePadding.top() + st::msgServicePadding.bottom();
					if (width < 1) return;

					int32 strwidth = st::msgServiceFont->m.width(str) + st::msgServicePadding.left() + st::msgServicePadding.right();

					QRect trect(QRect(left, st::msgServiceMargin.top(), width, height).marginsAdded(-st::msgServicePadding));
					left += (width - strwidth) / 2;
					width = strwidth;

					QRect r(left, st::msgServiceMargin.top(), width, height);
					p.setBrush(st::msgServiceBG->b);
					p.setPen(Qt::NoPen);
					p.drawRoundedRect(r, st::msgServiceRadius, st::msgServiceRadius);

					p.setBrush(Qt::NoBrush);
					p.setPen(st::msgServiceColor->p);
					p.setFont(st::msgServiceFont->f);
					p.drawText(r.x() + st::msgServicePadding.left(), r.y() + st::msgServicePadding.top() + st::msgServiceFont->ascent, str);
				}
				y = curY;
			}
		}
	}
}

void OverviewInner::mouseMoveEvent(QMouseEvent *e) {
	_lastPos = e->globalPos();
	updateSelected();
}

void OverviewInner::updateSelected() {
	if (!isVisible()) return;

	QPoint p(mapFromGlobal(_lastPos));

	HistoryItem *hovered = App::hoveredLinkItem(), *nhovered = 0;
	TextLinkPtr lnk = textlnkOver(), nlnk;
	if (_type == OverviewPhotos) {
		float64 w = (float64(width() - st::overviewPhotoSkip) / _photosInRow);
		int32 inRow = int32(p.x() / w), vsize = (_vsize + st::overviewPhotoSkip);
		int32 row = int32(p.y() / vsize);
		if (inRow < 0) inRow = 0;
		if (row < 0) row = 0;

		if (p.x() >= inRow * w + st::overviewPhotoSkip && p.x() < inRow * w + st::overviewPhotoSkip + _vsize) {
			if (p.y() >= row * vsize + st::overviewPhotoSkip && p.y() < (row + 1) * vsize + st::overviewPhotoSkip) {
				int32 index = row * _photosInRow + inRow, count = _hist->_overview[_type].size();
				if (index >= 0 && index < count) {
					MsgId msgid = _hist->_overview[_type][count - index - 1];
					HistoryItem *item = App::histItemById(msgid);
					HistoryMedia *media = item ? item->getMedia(true) : 0;
					if (media && media->type() == MediaTypePhoto) {
						nlnk = static_cast<HistoryPhoto*>(media)->lnk();
						nhovered = item;
					}
				}
			}
		}
	} else {
		int32 addToY = (_height < _minHeight ? (_minHeight - _height) : 0);
		int32 w = _width - st::msgMargin.left() - st::msgMargin.right();
		for (int32 i = _items.size(); i > 0;) {
			--i;
			if (!i || (addToY + _height - _items[i - 1].y > p.y())) {
				int32 y = addToY + _height - _items[i].y;
				if (y >= p.y()) break;

				if (!_items[i].msgid) break; // day item

				HistoryItem *item = App::histItemById(_items[i].msgid);
				HistoryMedia *media = item ? item->getMedia(true) : 0;
				if (media) {
					bool out = item->out();
					int32 mw = media->maxWidth(), left = (out ? st::msgMargin.right() : st::msgMargin.left()) + (out && mw < w ? (w - mw) : 0);
					if (!out && _hist->peer->chat) {
						if (QRect(left, y + st::msgMargin.top() + media->height() - st::msgPhotoSize, st::msgPhotoSize, st::msgPhotoSize).contains(p)) {
							nlnk = item->from()->lnk;
							nhovered = item;
							break;
						}
						left += st::msgPhotoSkip;
					}
					TextLinkPtr lnk = media->getLink(p.x() - left, p.y() - y - st::msgMargin.top(), item, w);
					if (lnk) {
						nlnk = lnk;
						nhovered = item;
						break;
					}
				}
			}
		}
	}
	textlnkOver(nlnk);
	if (hovered != nhovered) {
		App::hoveredLinkItem(nhovered);
		if (App::main()) {
			if (hovered) App::main()->msgUpdated(hovered->history()->peer->id, hovered);
			if (nhovered) App::main()->msgUpdated(nhovered->history()->peer->id, nhovered);
		}
	}
	if (lnk && !nlnk) {
		setCursor(style::cur_default);
	} else if (!lnk && nlnk) {
		setCursor(style::cur_pointer);
	}
}

void OverviewInner::mousePressEvent(QMouseEvent *e) {
	_lastPos = e->globalPos();
	updateSelected();

	textlnkDown(textlnkOver());
}

void OverviewInner::mouseReleaseEvent(QMouseEvent *e) {
	_lastPos = e->globalPos();
	updateSelected();

	TextLinkPtr over = textlnkOver();
	if (over && over == textlnkDown()) {
		over->onClick(e->button());
	}
	textlnkDown(TextLinkPtr());
}

void OverviewInner::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		App::main()->showPeer(0, 0, true);
	}
}

void OverviewInner::enterEvent(QEvent *e) {
	setMouseTracking(true);
	_lastPos = QCursor::pos();
	updateSelected();
	return TWidget::enterEvent(e);
}

void OverviewInner::leaveEvent(QEvent *e) {
	setMouseTracking(false);
	_lastPos = QCursor::pos();
	updateSelected();
	return TWidget::leaveEvent(e);
}

void OverviewInner::leaveToChildEvent(QEvent *e) {
	_lastPos = QCursor::pos();
	updateSelected();
	return TWidget::leaveToChildEvent(e);
}

void OverviewInner::resizeEvent(QResizeEvent *e) {
	_width = width();
	showAll();
	update();
}

void OverviewInner::contextMenuEvent(QContextMenuEvent *e) {
	if (_menu) {
		_menu->deleteLater();
		_menu = 0;
	}
	if (e->reason() == QContextMenuEvent::Mouse) {
		_lastPos = e->globalPos();
		updateSelected();
	}

	_contextMenuLnk = textlnkOver();
	PhotoLink *lnkPhoto = dynamic_cast<PhotoLink*>(_contextMenuLnk.data());
	VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
	AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
	DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	if (lnkPhoto || lnkVideo || lnkAudio || lnkDocument) {
		_menu = new QMenu(_overview);
		if (App::hoveredLinkItem()) {
			_menu->addAction(lang(lng_context_to_msg), this, SLOT(goToMessage()))->setEnabled(true);
		}
		if (lnkPhoto) {
			_menu->addAction(lang(lng_context_open_image), this, SLOT(openContextUrl()))->setEnabled(true);
		} else {
			if ((lnkVideo && lnkVideo->video()->loader) || (lnkAudio && lnkAudio->audio()->loader) || (lnkDocument && lnkDocument->document()->loader)) {
				_menu->addAction(lang(lng_context_cancel_download), this, SLOT(cancelContextDownload()))->setEnabled(true);
			} else {
				if ((lnkVideo && !lnkVideo->video()->already(true).isEmpty()) || (lnkAudio && !lnkAudio->audio()->already(true).isEmpty()) || (lnkDocument && !lnkDocument->document()->already(true).isEmpty())) {
					_menu->addAction(lang(cPlatform() == dbipMac ? lng_context_show_in_finder : lng_context_show_in_folder), this, SLOT(showContextInFolder()))->setEnabled(true);
				}
				_menu->addAction(lang(lnkVideo ? lng_context_open_video : (lnkAudio ? lng_context_open_audio : lng_context_open_document)), this, SLOT(openContextFile()))->setEnabled(true);
				_menu->addAction(lang(lnkVideo ? lng_context_save_video : (lnkAudio ? lng_context_save_audio : lng_context_save_document)), this, SLOT(saveContextFile()))->setEnabled(true);
			}
		}
		if (App::hoveredLinkItem()) {
			if (dynamic_cast<HistoryMessage*>(App::hoveredLinkItem())) {
				_menu->addAction(lang(lng_context_forward_msg), this, SLOT(forwardMessage()))->setEnabled(true);
			}
			_menu->addAction(lang(lng_context_delete_msg), this, SLOT(deleteMessage()))->setEnabled(true);
			App::contextItem(App::hoveredLinkItem());
		}
	}
	if (_menu) {
		_menu->setAttribute(Qt::WA_DeleteOnClose);
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
	}
}

int32 OverviewInner::resizeToWidth(int32 nwidth, int32 scrollTop, int32 minHeight) {
	if (width() == nwidth && minHeight == _minHeight) return scrollTop;
	_minHeight = minHeight;
	if (_resizeIndex < 0) {
		_resizeIndex = _photosInRow * (scrollTop / int32(_vsize + st::overviewPhotoSkip));
		_resizeSkip = scrollTop - (scrollTop / int32(_vsize + st::overviewPhotoSkip)) * int32(_vsize + st::overviewPhotoSkip);
	}
	resize(nwidth, height() > _minHeight ? height() : _minHeight);
	showAll();
	int32 newRow = _resizeIndex / _photosInRow;
	return newRow * int32(_vsize + st::overviewPhotoSkip) + _resizeSkip;
}

void OverviewInner::dropResizeIndex() {
	_resizeIndex = -1;
}

PeerData *OverviewInner::peer() const {
	return _peer;
}

MediaOverviewType OverviewInner::type() const {
	return _type;
}

void OverviewInner::switchType(MediaOverviewType type) {
	_type = type;
	mediaOverviewUpdated();
	if (App::wnd()) App::wnd()->update();
}

void OverviewInner::openContextUrl() {
	HistoryItem *was = App::hoveredLinkItem();
	App::hoveredLinkItem(App::contextItem());
	_contextMenuLnk->onClick(Qt::LeftButton);
	App::hoveredLinkItem(was);
}

void OverviewInner::goToMessage() {
	HistoryItem *item = App::contextItem();
	if (!item) return;

	App::main()->showPeer(item->history()->peer->id, item->id, true, true);
}

void OverviewInner::forwardMessage() {
	HistoryItem *item = App::contextItem();
	if (!item || item->itemType() != HistoryItem::MsgType) return;

	App::main()->forwardLayer();
}

void OverviewInner::deleteMessage() {
	HistoryItem *item = App::contextItem();
	if (!item || item->itemType() != HistoryItem::MsgType) return;

	HistoryMessage *msg = dynamic_cast<HistoryMessage*>(item);
	App::main()->deleteLayer((msg && msg->uploading()) ? -2 : -1);
}

void OverviewInner::cancelContextDownload() {
	VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
	AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
	DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	mtpFileLoader *loader = lnkVideo ? lnkVideo->video()->loader : (lnkAudio ? lnkAudio->audio()->loader : (lnkDocument ? lnkDocument->document()->loader : 0));
	if (loader) loader->cancel();
}

void OverviewInner::showContextInFolder() {
	VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
	AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
	DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	QString already = lnkVideo ? lnkVideo->video()->already(true) : (lnkAudio ? lnkAudio->audio()->already(true) : (lnkDocument ? lnkDocument->document()->already(true) : QString()));
	if (!already.isEmpty()) psShowInFolder(already);
}

void OverviewInner::saveContextFile() {
	VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
	AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
	DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	if (lnkVideo) VideoSaveLink(lnkVideo->video()).doSave(true);
	if (lnkAudio) AudioSaveLink(lnkAudio->audio()).doSave(true);
	if (lnkDocument) DocumentSaveLink(lnkDocument->document()).doSave(true);
}

void OverviewInner::openContextFile() {
	VideoLink *lnkVideo = dynamic_cast<VideoLink*>(_contextMenuLnk.data());
	AudioLink *lnkAudio = dynamic_cast<AudioLink*>(_contextMenuLnk.data());
	DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	if (lnkVideo) VideoOpenLink(lnkVideo->video()).onClick(Qt::LeftButton);
	if (lnkAudio) AudioOpenLink(lnkAudio->audio()).onClick(Qt::LeftButton);
	if (lnkDocument) DocumentOpenLink(lnkDocument->document()).onClick(Qt::LeftButton);
}

void OverviewInner::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
	}
}

void OverviewInner::mediaOverviewUpdated() {
	int32 oldHeight = _height;
	if (_type != OverviewPhotos) {
		History::MediaOverview &o(_hist->_overview[_type]);
		int32 l = o.size();
		_items.reserve(2 * l); // day items

		int32 y = 0, in = 0;
		bool allGood = true;
		QDate prevDate;
		for (int32 i = 0; i < l; ++i) {
			MsgId msgid = o.at(l - i - 1);
			if (allGood) {
				if (_items.size() > in && _items.at(in).msgid == msgid) {
					prevDate = _items.at(in).date;
					y = _items.at(in).y;
					++in;
					continue;
				}
				if (_items.size() > in + 1 && !_items.at(in).msgid && _items.at(in + 1).msgid == msgid) { // day item
					++in;
					prevDate = _items.at(in).date;
					y = _items.at(in).y;
					++in;
					continue;
				}
				allGood = false;
			}
			HistoryItem *item = App::histItemById(msgid);
			HistoryMedia *media = item ? item->getMedia(true) : 0;
			if (!media) continue;

			QDate date = item->date.date();
			if (in > 0) {
				if (date != prevDate) { // add day item
					y += st::msgServiceFont->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom(); // day item height
					if (_items.size() > in) {
						_items[in].msgid = 0;
						_items[in].date = prevDate;
						_items[in].y = y;
					} else {
						_items.push_back(CachedItem(0, prevDate, y));
					}
					++in;
					prevDate = date;
				}
			} else {
				prevDate = date;
			}
			y += media->height() + st::msgMargin.top() + st::msgMargin.bottom(); // item height
			if (_items.size() > in) {
				_items[in].msgid = msgid;
				_items[in].date = date;
				_items[in].y = y;
			} else {
				_items.push_back(CachedItem(msgid, date, y));
			}
			++in;
		}
		if (!_items.isEmpty()) {
			y += st::msgServiceFont->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom(); // day item height
			if (_items.size() > in) {
				_items[in].msgid = 0;
				_items[in].date = prevDate;
				_items[in].y = y;
			} else {
				_items.push_back(CachedItem(0, prevDate, y));
			}
			_items.resize(++in);
		}
		if (_height != y) {
			_height = y;
			resize(width(), _minHeight > _height ? _minHeight : _height);
		}
	}
	resizeEvent(0);
	showAll();
	if (_height != oldHeight) {
		_overview->scrollBy(_height - oldHeight);
	}
}

void OverviewInner::msgUpdated(HistoryItem *msg) {
	if (!msg || _hist != msg->history()) return;
	MsgId msgid = msg->id;
	if (_hist->_overviewIds[_type].constFind(msgid) != _hist->_overviewIds[_type].cend()) {
		if (_type == OverviewPhotos) {
			int32 index = _hist->_overview[_type].indexOf(msgid);
			if (index >= 0) {
				index = _hist->_overview[_type].size() - index - 1;

				float64 w = (float64(width() - st::overviewPhotoSkip) / _photosInRow);
				int32 vsize = (_vsize + st::overviewPhotoSkip);
				int32 row = index / _photosInRow, col = index % _photosInRow;
				update(int32(col * w), int32(row * vsize), qCeil(w), vsize);
			}
		} else {
			int32 addToY = (_height < _minHeight ? (_minHeight - _height) : 0);
			for (int32 i = 0, l = _items.size(); i != l; ++i) {
				if (_items[i].msgid == msgid) {
					HistoryMedia *media = msg->getMedia(true);
					if (media) update(0, addToY + _height - _items[i].y, _width, media->height() + st::msgMargin.top() + st::msgMargin.bottom());
					break;
				}
			}
		}
	}
}

void OverviewInner::showAll() {
	int32 newHeight = height();
	if (_type == OverviewPhotos) {
		_photosInRow = int32(width() - st::overviewPhotoSkip) / int32(st::overviewPhotoMinSize + st::overviewPhotoSkip);
		_vsize = (int32(width() - st::overviewPhotoSkip) / _photosInRow) - st::overviewPhotoSkip;
		int32 count = _hist->_overview[_type].size();
		int32 rows = (count / _photosInRow) + ((count % _photosInRow) ? 1 : 0);
		newHeight = (_vsize + st::overviewPhotoSkip) * rows + st::overviewPhotoSkip;
	} else {
		newHeight = _height;
	}
	if (newHeight < _minHeight) {
		newHeight = _minHeight;
	}
	if (height() != newHeight) {
		resize(width(), newHeight);
	}
}

OverviewInner::~OverviewInner() {
}

OverviewWidget::OverviewWidget(QWidget *parent, const PeerData *peer, MediaOverviewType type) : QWidget(parent)
    , _scroll(this, st::setScroll)
    , _inner(this, &_scroll, peer, type)
	, _noDropResizeIndex(false)
	, _bg(st::msgBG)
    , _showing(false)
{
	_scroll.setWidget(&_inner);
	_scroll.move(0, 0);
	_inner.move(0, 0);
	_scroll.show();
	connect(&_scroll, SIGNAL(scrolled()), &_inner, SLOT(updateSelected()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));

	switchType(type);
}

void OverviewWidget::clear() {
	_inner.clear();
}

void OverviewWidget::onScroll() {
	MTP::clearLoaderPriorities();
	bool nearBottom = _scroll.scrollTop() + _scroll.height() * 5 > _scroll.scrollTopMax(), nearTop = _scroll.scrollTop() < _scroll.height() * 5;
	if ((nearBottom && type() == OverviewPhotos) || (nearTop && type() != OverviewPhotos)) {
		if (App::main()) {
			App::main()->loadMediaBack(peer(), type(), true);
		}
	}
	if (!_noDropResizeIndex) {
		_inner.dropResizeIndex();
	}
}

void OverviewWidget::resizeEvent(QResizeEvent *e) {
	_scroll.resize(size());
	int32 newScrollTop = _inner.resizeToWidth(width(), _scroll.scrollTop(), height());
	if (newScrollTop != _scroll.scrollTop()) {
		_noDropResizeIndex = true;
		_scroll.scrollToY(newScrollTop);
		_noDropResizeIndex = false;
	}
}

void OverviewWidget::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (animating() && _showing) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animCache);
		return;
	}

	QRect r(e->rect());
	if (cCatsAndDogs()) {
		int32 i_from = r.left() / _bg.width(), i_to = (r.left() + r.width() - 1) / _bg.width() + 1;
		int32 j_from = r.top() / _bg.height(), j_to = (r.top() + r.height() - 1) / _bg.height() + 1;
		for (int32 i = i_from; i < i_to; ++i) {
			for (int32 j = j_from; j < j_to; ++j) {
				p.drawPixmap(i * _bg.width(), j * _bg.height(), _bg);
			}
		}
	} else {
		p.fillRect(r, st::historyBG->b);
	}
}

void OverviewWidget::scrollBy(int32 add) {
	_scroll.scrollToY(_scroll.scrollTop() + add);
}

void OverviewWidget::paintTopBar(QPainter &p, float64 over, int32 decreaseWidth) {
	if (animating() && _showing) {
		p.setOpacity(a_bgAlpha.current());
		p.drawPixmap(a_bgCoord.current(), 0, _bgAnimTopBarCache);
		p.setOpacity(a_alpha.current());
		p.drawPixmap(a_coord.current(), 0, _animTopBarCache);
	} else {
		p.setOpacity(st::topBarBackAlpha + (1 - st::topBarBackAlpha) * over);
		p.drawPixmap(QPoint(st::topBarBackPadding.left(), (st::topBarHeight - st::topBarBackImg.pxHeight()) / 2), App::sprite(), st::topBarBackImg);
		p.setFont(st::topBarBackFont->f);
		p.setPen(st::topBarBackColor->p);
		p.drawText(st::topBarBackPadding.left() + st::topBarBackImg.pxWidth() + st::topBarBackPadding.right(), (st::topBarHeight - st::titleFont->height) / 2 + st::titleFont->ascent, _header);
	}
}

void OverviewWidget::topBarClick() {
	App::main()->showBackFromStack();
}

PeerData *OverviewWidget::peer() const {
	return _inner.peer();
}

MediaOverviewType OverviewWidget::type() const {
	return _inner.type();
}

void OverviewWidget::switchType(MediaOverviewType type) {
	_inner.switchType(type);
	switch (type) {
	case OverviewPhotos: _header = lang(lng_profile_photos_header); break;
	case OverviewVideos: _header = lang(lng_profile_videos_header); break;
	case OverviewDocuments: _header = lang(lng_profile_documents_header); break;
	case OverviewAudios: _header = lang(lng_profile_audios_header); break;
	}
}

int32 OverviewWidget::lastWidth() const {
	return width();
}

int32 OverviewWidget::lastScrollTop() const {
	return _scroll.scrollTop();
}

void OverviewWidget::animShow(const QPixmap &bgAnimCache, const QPixmap &bgAnimTopBarCache, bool back, int32 lastScrollTop) {
	_bgAnimCache = bgAnimCache;
	_bgAnimTopBarCache = bgAnimTopBarCache;
	_scroll.scrollToY(lastScrollTop < 0 ? (type() == OverviewPhotos ? 0 : _scroll.scrollTopMax()) : lastScrollTop);
	_animCache = myGrab(this, rect());
	App::main()->topBar()->stopAnim();
	_animTopBarCache = myGrab(App::main()->topBar(), QRect(0, 0, width(), st::topBarHeight));
	App::main()->topBar()->startAnim();
	_scroll.hide();
	a_coord = back ? anim::ivalue(-st::introSlideShift, 0) : anim::ivalue(st::introSlideShift, 0);
	a_alpha = anim::fvalue(0, 1);
	a_bgCoord = back ? anim::ivalue(0, st::introSlideShift) : anim::ivalue(0, -st::introSlideShift);
	a_bgAlpha = anim::fvalue(1, 0);
	anim::start(this);
	_showing = true;
	show();
	_inner.setFocus();
	App::main()->topBar()->update();
}

bool OverviewWidget::animStep(float64 ms) {
	float64 fullDuration = st::introSlideDelta + st::introSlideDuration, dt = ms / fullDuration;
	float64 dt1 = (ms > st::introSlideDuration) ? 1 : (ms / st::introSlideDuration), dt2 = (ms > st::introSlideDelta) ? (ms - st::introSlideDelta) / (st::introSlideDuration) : 0;
	bool res = true;
	if (dt2 >= 1) {
		res = _showing = false;
		a_bgCoord.finish();
		a_bgAlpha.finish();
		a_coord.finish();
		a_alpha.finish();
		_bgAnimCache = _animCache = _animTopBarCache = _bgAnimTopBarCache = QPixmap();
		App::main()->topBar()->stopAnim();
		_scroll.show();
		activate();
		onScroll();
	} else {
		a_bgCoord.update(dt1, st::introHideFunc);
		a_bgAlpha.update(dt1, st::introAlphaHideFunc);
		a_coord.update(dt2, st::introShowFunc);
		a_alpha.update(dt2, st::introAlphaShowFunc);
	}
	update();
	App::main()->topBar()->update();
	return res;
}

void OverviewWidget::mediaOverviewUpdated(PeerData *p) {
	if (peer() == p) {
		_inner.mediaOverviewUpdated();
		onScroll();
	}
}

void OverviewWidget::msgUpdated(PeerId p, HistoryItem *msg) {
	if (peer()->id == p) {
		_inner.msgUpdated(msg);
	}
}

OverviewWidget::~OverviewWidget() {
}

void OverviewWidget::activate() {
	_inner.setFocus();
}
