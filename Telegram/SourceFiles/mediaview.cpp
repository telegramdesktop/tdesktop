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

#include "mediaview.h"
#include "mainwidget.h"
#include "window.h"
#include "application.h"
#include "gui/filedialog.h"

MediaView::MediaView() : QWidget(App::wnd()),
_photo(0), _leftNavVisible(false), _rightNavVisible(false), _maxWidth(0), _maxHeight(0), _x(0), _y(0), _w(0), _full(false),
_history(0), _peer(0), _user(0), _from(0), _index(-1), _msgid(0), _loadRequest(0), _over(OverNone), _down(OverNone), _lastAction(-st::medviewDeltaFromLastAction, -st::medviewDeltaFromLastAction),
_close(this, lang(lng_mediaview_close), st::medviewButton),
_save(this, lang(lng_mediaview_save), st::medviewButton),
_forward(this, lang(lng_mediaview_forward), st::medviewButton),
_delete(this, lang(lng_mediaview_delete), st::medviewButton),
_menu(0), _receiveMouse(true), _touchPress(false), _touchMove(false), _touchRightButton(false) {
	setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::Tool | Qt::NoDropShadowWindowHint);
	moveToScreen();
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
	setMouseTracking(true);
	hide();

	connect(&_close, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_forward, SIGNAL(clicked()), this, SLOT(onForward()));
	connect(&_delete, SIGNAL(clicked()), this, SLOT(onDelete()));

	connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onCheckActive()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));
}

void MediaView::moveToScreen() {
	QPoint wndCenter(App::wnd()->x() + App::wnd()->width() / 2, App::wnd()->y() + App::wnd()->height() / 2);
	QRect geom = QDesktopWidget().screenGeometry(wndCenter);
	_avail = QDesktopWidget().availableGeometry(wndCenter);
	if (geom != geometry()) {
		setGeometry(geom);
	}
	if (!geom.contains(_avail)) {
		_avail = geom;
	}
	_avail.moveTo(0, 0);
	_maxWidth = _avail.width() - 2 * st::medviewNavBarWidth;
	_maxHeight = _avail.height() - st::medviewTopSkip - st::medviewBottomSkip;
	_leftNav = QRect(0, 0, st::medviewNavBarWidth, height());
	_rightNav = QRect(width() - st::medviewNavBarWidth, 0, st::medviewNavBarWidth, height());
	_close.move(_avail.x() + (_avail.width() + st::medviewMainWidth) / 2 - _close.width(), _avail.y() + (st::medviewTopSkip - _close.height()) / 2);
	_save.move(_avail.x() + (_avail.width() - st::medviewMainWidth) / 2, _avail.y() + (st::medviewTopSkip - _save.height()) / 2);
	_delete.move(_avail.x() + (_avail.width() + st::medviewMainWidth) / 2 - _delete.width(), _avail.y() + _avail.height() - (st::medviewTopSkip + _delete.height()) / 2);
	_forward.move(_avail.x() + (_avail.width() - st::medviewMainWidth) / 2, _avail.y() + _avail.height() - (st::medviewTopSkip + _forward.height()) / 2);
}

void MediaView::mediaOverviewUpdated(PeerData *peer) {
	if (_history && _history->peer == peer) {
		_index = -1;
		for (int i = 0, l = _history->_photosOverview.size(); i < l; ++i) {
			if (_history->_photosOverview.at(i) == _msgid) {
				_index = i;
				break;
			}
		}
		updateControls();
	} else if (_user == peer) {
		_index = -1;
		for (int i = 0, l = _user->photos.size(); i < l; ++i) {
			if (_user->photos.at(i) == _photo) {
				_index = i;
				break;
			}
		}
		updateControls();
	}
}

void MediaView::changingMsgId(HistoryItem *row, MsgId newId) {
	if (row->id == _msgid) {
		_msgid = newId;
	}
	mediaOverviewUpdated(row->history()->peer);
}

void MediaView::updateControls() {
	if (!_photo) return;

	_close.show();
	if (_photo->full->loaded()) {
		_save.show();
	} else {
		_save.hide();
	}
	if (_history) {
		HistoryItem *item = App::histItemById(_msgid);
		if (dynamic_cast<HistoryMessage*>(item)) {
			_forward.show();
		} else {
			_forward.hide();
		}
		_delete.show();
	} else {
		_forward.hide();
		if ((App::self() && _photo && App::self()->photoId == _photo->id) || (_photo->chat && _photo->chat->photoId == _photo->id)) {
			_delete.show();
		} else {
			_delete.hide();
		}
	}
	QDateTime d(date(_photo->date)), dNow(date(unixtime()));
	if (d.date() == dNow.date()) {
		_dateText = lang(lng_status_lastseen_today).replace(qsl("{time}"), d.time().toString(qsl("hh:mm")));
	} else if (d.date().addDays(1) == dNow.date()) {
		_dateText = lang(lng_status_lastseen_yesterday).replace(qsl("{time}"), d.time().toString(qsl("hh:mm")));
	} else {
		_dateText = lang(lng_status_lastseen_date_time).replace(qsl("{date}"), d.date().toString(qsl("dd.MM.yy"))).replace(qsl("{time}"), d.time().toString(qsl("hh:mm")));
	}
	int32 nameWidth = _from->nameText.maxWidth(), maxWidth = _delete.x() - _forward.x() - _forward.width(), dateWidth = st::medviewDateFont->m.width(_dateText);
	if (nameWidth > maxWidth) {
		nameWidth = maxWidth;
	}
	_nameNav = QRect(_forward.x() + _forward.width() + (maxWidth - nameWidth) / 2, _forward.y() + st::medviewNameTop, nameWidth, st::msgNameFont->height);
	_dateNav = QRect(_forward.x() + _forward.width() + (maxWidth - dateWidth) / 2, _forward.y() + st::medviewDateTop, dateWidth, st::medviewDateFont->height);
	updateHeader();
	_leftNavVisible = (_index > 0 || (_index == 0 && _history && _history->_photosOverview.size() < _history->_photosOverviewCount));
	_rightNavVisible = (_index >= 0 && (
		(_history && _index + 1 < _history->_photosOverview.size()) ||
		(_user && (_index + 1 < _user->photos.size() || _index + 1 < _user->photosCount))));
	updateOver(mapFromGlobal(QCursor::pos()));
	update();
}

bool MediaView::animStep(float64 msp) {
	uint64 ms = getms();
	for (Showing::iterator i = _animations.begin(); i != _animations.end();) {
		int64 start = i.value();
		switch (i.key()) {
		case OverLeftNav: update(_leftNav); break;
		case OverRightNav: update(_rightNav); break;
		case OverName: update(_nameNav); break;
		case OverDate: update(_dateNav); break;
		default: break;
		}
		float64 dt = float64(ms - start) / st::medviewButton.duration;
		if (dt >= 1) {
			_animOpacities.remove(i.key());
			i = _animations.erase(i);
		} else {
			_animOpacities[i.key()].update(dt, anim::linear);
			++i;
		}
	}
	return !_animations.isEmpty();
}

MediaView::~MediaView() {
	delete _menu;
}

void MediaView::onClose() {
	if (App::wnd()) App::wnd()->layerHidden();
}

void MediaView::onSave() {
	if (!_photo || !_photo->full->loaded()) return;

	QString file;
	if (filedialogGetSaveFile(file, lang(lng_save_photo), qsl("JPEG Image (*.jpg);;All files (*.*)"), filedialogDefaultName(qsl("photo"), qsl(".jpg")))) {
		if (!file.isEmpty()) {
			_photo->full->pix().toImage().save(file, "JPG");
		}
	}
}

void MediaView::onForward() {
	HistoryItem *item = App::histItemById(_msgid);
	if (!_msgid || !item) return;

	if (App::wnd()) {
		onClose();
		if (App::main()) {
			App::contextItem(item);
			App::main()->forwardLayer();
		}
	}
}

void MediaView::onDelete() {
	onClose();
	if (!_msgid) {
		if (App::self() && _photo && App::self()->photoId == _photo->id) {
			App::app()->peerClearPhoto(App::self()->id);
		} else if (_photo->chat && _photo->chat->photoId == _photo->id) {
			App::app()->peerClearPhoto(_photo->chat->id);
		}
	} else {
		HistoryItem *item = App::histItemById(_msgid);
		if (item) {
			App::contextItem(item);
			App::main()->deleteLayer();
		}
	}
}

void MediaView::onCopy() {
	if (!_photo || !_photo->full->loaded()) return;

	QApplication::clipboard()->setPixmap(_photo->full->pix());
}

void MediaView::showPhoto(PhotoData *photo, HistoryItem *context) {
	_history = context->history();
	_peer = 0;
	_user = 0;

	_loadRequest = 0;
	_over = OverNone;
	if (!_animations.isEmpty()) {
		_animations.clear();
		anim::stop(this);
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();
	setCursor(style::cur_default);

	_index = -1;
	_msgid = context->id;
	for (int i = 0, l = _history->_photosOverview.size(); i < l; ++i) {
		if (_history->_photosOverview.at(i) == _msgid) {
			_index = i;
			break;
		}
	}

	if (_history->_photosOverviewCount < 0) {
		loadPhotosBack();
	}

	showPhoto(photo);
	preloadPhotos(0);
}

void MediaView::showPhoto(PhotoData *photo, PeerData *context) {
	_history = 0;
	_peer = context;
	_user = context->chat ? 0 : context->asUser();

	_loadRequest = 0;
	_over = OverNone;
	if (!_animations.isEmpty()) {
		_animations.clear();
		anim::stop(this);
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();
	setCursor(style::cur_default);

	_msgid = 0;
	_index = -1;
	if (_user) {
		if (_user->photos.isEmpty() && _user->photosCount < 0 && _user->photoId) {
			_index = 0;
		}
		for (int i = 0, l = _user->photos.size(); i < l; ++i) {
			if (_user->photos.at(i) == photo) {
				_index = i;
				break;
			}
		}

		if (_user->photosCount < 0) {
			loadPhotosBack();
		}
	}
	showPhoto(photo);
	preloadPhotos(0);
}

void MediaView::showPhoto(PhotoData *photo) {
	_photo = photo;
	MTP::clearLoaderPriorities();
	_photo->full->load();
	_full = false;
	_current = QPixmap();
	_w = photo->full->width();
	_down = OverNone;
	int h = photo->full->height();
	switch (cScale()) {
	case dbisOneAndQuarter: _w = qRound(float64(_w) * 1.25 - 0.01); h = qRound(float64(h) * 1.25 - 0.01); break;
	case dbisOneAndHalf: _w = qRound(float64(_w) * 1.5 - 0.01); h = qRound(float64(h) * 1.5 - 0.01); break;
	case dbisTwo: _w *= 2; h *= 2; break;
	}
	if (_w > _maxWidth) {
		h = qRound(h * _maxWidth / float64(_w));
		_w = _maxWidth;
	}
	if (h > _maxHeight) {
		_w = qRound(_w * _maxHeight / float64(h));
		h = _maxHeight;
	}
	_x = _avail.x() + (_avail.width() - _w) / 2;
	_y = _avail.y() + (_avail.height() - h) / 2;
	_from = App::user(_photo->user);
	updateControls();
	if (isHidden()) {
		moveToScreen();
#ifdef Q_OS_WIN
		bool wm = testAttribute(Qt::WA_Mapped), wv = testAttribute(Qt::WA_WState_Visible);
		if (!wm) setAttribute(Qt::WA_Mapped, true);
		if (!wv) setAttribute(Qt::WA_WState_Visible, true);
		update();
		QEvent e(QEvent::UpdateRequest);
		event(&e);
		if (!wm) setAttribute(Qt::WA_Mapped, false);
		if (!wv) setAttribute(Qt::WA_WState_Visible, false);
#endif
		show();
	}
}

void MediaView::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	QRect r(e->rect());
	
	QPainter::CompositionMode m = p.compositionMode();
	p.setCompositionMode(QPainter::CompositionMode_Source);

	// main bg
	p.setOpacity(st::medviewLightOpacity);
	QRect r_bg(st::medviewNavBarWidth, 0, width() - 2 * st::medviewNavBarWidth, height());
	if (r_bg.intersects(r)) p.fillRect(r_bg.intersected(r), st::black->b);

	// left nav bar bg
	if (_leftNav.intersects(r)) {
		if (_leftNavVisible) {
			float64 o = overLevel(OverLeftNav);
			p.setOpacity(o * st::medviewDarkOpacity + (1 - o) * st::medviewLightOpacity);
			p.fillRect(_leftNav.intersected(r), st::black->b);
		} else {
			p.setOpacity(st::medviewLightOpacity);
			p.fillRect(_leftNav.intersected(r), st::black->b);
		}
	}

	// right nav bar
	if (_rightNav.intersects(r)) {
		if (_rightNavVisible) {
			float64 o = overLevel(OverRightNav);
			p.setOpacity(o * st::medviewDarkOpacity + (1 - o) * st::medviewLightOpacity);
			p.fillRect(_rightNav.intersected(r), st::black->b);
		} else {
			p.setOpacity(st::medviewLightOpacity);
			p.fillRect(_rightNav.intersected(r), st::black->b);
		}
	}

	p.setCompositionMode(m);

	// header
	p.setOpacity(1);
	p.setPen(st::medviewHeaderColor->p);
	p.setFont(st::medviewHeaderFont->f);
	QRect r_header(_save.x() + _save.width(), _save.y(), _close.x() - _save.x() - _save.width(), _save.height());
	if (r_header.intersects(r)) p.drawText(r_header, _header, style::al_center);

	// name
	p.setPen(nameDateColor(overLevel(OverName)));
	if (_over == OverName) _from->nameText.replaceFont(st::msgNameFont->underline());
	if (_nameNav.intersects(r)) _from->nameText.drawElided(p, _nameNav.left(), _nameNav.top(), _nameNav.width());
	if (_over == OverName) _from->nameText.replaceFont(st::msgNameFont);

	// date
	p.setPen(nameDateColor(overLevel(OverDate)));
	p.setFont((_over == OverDate ? st::medviewDateFont->underline() : st::medviewDateFont)->f);
	if (_dateNav.intersects(r)) p.drawText(_dateNav.left(), _dateNav.top() + st::medviewDateFont->ascent, _dateText);

	// left nav bar
	if (_leftNavVisible) {
		QPoint p_left((st::medviewNavBarWidth - st::medviewLeft.pxWidth()) / 2, (height() - st::medviewLeft.pxHeight()) / 2);
		if (QRect(p_left.x(), p_left.y(), st::medviewLeft.pxWidth(), st::medviewLeft.pxHeight()).intersects(r)) {
			float64 o = overLevel(OverLeftNav);
			p.setOpacity(o * st::medviewDarkNav + (1 - o) * st::medviewLightNav);
			p.drawPixmap(p_left, App::sprite(), st::medviewLeft);
		}
	}

	// right nav bar
	if (_rightNavVisible) {
		QPoint p_right(width() - (st::medviewNavBarWidth + st::medviewRight.pxWidth()) / 2, (height() - st::medviewRight.pxHeight()) / 2);
		if (QRect(p_right.x(), p_right.y(), st::medviewRight.pxWidth(), st::medviewRight.pxHeight()).intersects(r)) {
			float64 o = overLevel(OverRightNav);
			p.setOpacity(o * st::medviewDarkNav + (1 - o) * st::medviewLightNav);
			p.drawPixmap(p_right, App::sprite(), st::medviewRight);
		}
	}

	// photo
	p.setOpacity(1);
	if (!_full && _photo->full->loaded()) {
		_current = _photo->full->pixNoCache(_w, 0, true);
		_full = true;
	} else if (_current.isNull() && _photo->thumb->loaded()) {
		_current = _photo->thumb->pixBlurredNoCache(_w);
	}
	if (QRect(_x, _y, _current.width() / cIntRetinaFactor(), _current.height() / cIntRetinaFactor()).intersects(r)) {
		p.drawPixmap(_x, _y, _current);
	}
}

void MediaView::keyPressEvent(QKeyEvent *e) {
	if (!_menu && e->key() == Qt::Key_Escape) {
		onClose();
	} else if (e == QKeySequence::Save || e == QKeySequence::SaveAs) {
		onSave();
	} else if (e->key() == Qt::Key_Copy || (e->key() == Qt::Key_C && e->modifiers().testFlag(Qt::ControlModifier))) {
		onCopy();
	} else if (e->key() == Qt::Key_Left) {
		moveToPhoto(-1);
	} else if (e->key() == Qt::Key_Right) {
		moveToPhoto(1);
	}
}

void MediaView::moveToPhoto(int32 delta) {
	if (_index < 0) return;

	int32 newIndex = _index + delta;
	if (_history) {
		if (newIndex >= 0 && newIndex < _history->_photosOverview.size()) {
			_index = newIndex;
			if (HistoryItem *item = App::histItemById(_history->_photosOverview[_index])) {
				_msgid = item->id;
				HistoryPhoto *photo = dynamic_cast<HistoryPhoto*>(item->getMedia());
				if (photo) {
					showPhoto(photo->photo());
					preloadPhotos(delta);
				}
			}
		}
		if (delta < 0 && _index < MediaOverviewStartPerPage) {
			loadPhotosBack();
		}
	} else if (_user) {
		if (newIndex >= 0 && newIndex < _user->photos.size()) {
			_index = newIndex;
			showPhoto(_user->photos[_index]);
			preloadPhotos(delta);
		}
		if (delta > 0 && _index > _user->photos.size() - MediaOverviewStartPerPage) {
			loadPhotosBack();
		}
	}
}

void MediaView::preloadPhotos(int32 delta) {
	if (_index < 0) return;

	int32 from = _index + (delta ? delta : -1), to = _index + (delta ? delta * MediaOverviewPreloadCount : 1);
	if (from > to) qSwap(from, to);
	if (_history) {
		for (int32 i = from; i <= to; ++i) {
			if (i >= 0 && i < _history->_photosOverview.size() && i != _index) {
				if (HistoryItem *item = App::histItemById(_history->_photosOverview[i])) {
					HistoryPhoto *photo = dynamic_cast<HistoryPhoto*>(item->getMedia());
					if (photo) {
						photo->photo()->full->load();
					}
				}
			}
		}
	} else if (_user) {
		for (int32 i = from; i <= to; ++i) {
			if (i >= 0 && i < _user->photos.size() && i != _index) {
				_user->photos[i]->thumb->load();
			}
		}
		for (int32 i = from; i <= to; ++i) {
			if (i >= 0 && i < _user->photos.size() && i != _index) {
				_user->photos[i]->full->load();
			}
		}
	}
}

void MediaView::mousePressEvent(QMouseEvent *e) {
	updateOver(e->pos());
	if (_menu || !_receiveMouse) return;

	if (e->button() == Qt::LeftButton) {
		_down = OverNone;
		if (_over == OverLeftNav && _index >= 0) {
			moveToPhoto(-1);
			_lastAction = e->pos();
		} else if (_over == OverRightNav && _index >= 0) {
			moveToPhoto(1);
			_lastAction = e->pos();
		} else if (_over == OverName) {
			_down = OverName;
		} else if (_over == OverDate) {
			_down = OverDate;
		} else {
			int32 w = st::medviewMainWidth + (st::medviewTopSkip - _save.height()), l = _avail.x() + (_avail.width() - w) / 2;
			if (!QRect(l, _avail.y(), w, st::medviewTopSkip).contains(e->pos()) && !QRect(l, _avail.y() + _avail.height() - st::medviewBottomSkip, w, st::medviewBottomSkip).contains(e->pos())) {
				if ((e->pos() - _lastAction).manhattanLength() >= st::medviewDeltaFromLastAction) {
					onClose();
				}
			}
		}
	}
}

void MediaView::mouseMoveEvent(QMouseEvent *e) {
	updateOver(e->pos());
	if (_lastAction.x() >= 0 && (e->pos() - _lastAction).manhattanLength() >= st::medviewDeltaFromLastAction) {
		_lastAction = QPoint(-st::medviewDeltaFromLastAction, -st::medviewDeltaFromLastAction);
	}
}

bool MediaView::updateOverState(OverState newState) {
	bool result = true;
	if (_over != newState) {
		if (_over != OverNone) {
			_animations[_over] = getms();
			ShowingOpacities::iterator i = _animOpacities.find(_over);
			if (i != _animOpacities.end()) {
				i->start(0);
			} else {
				_animOpacities.insert(_over, anim::fvalue(1, 0));
			}
			anim::start(this);
			if (newState != OverNone) update();
		} else {
			result = false;
		}
		_over = newState;
		if (newState != OverNone) {
			_animations[_over] = getms();
			ShowingOpacities::iterator i = _animOpacities.find(_over);
			if (i != _animOpacities.end()) {
				i->start(1);
			} else {
				_animOpacities.insert(_over, anim::fvalue(0, 1));
			}
			anim::start(this);
			setCursor(style::cur_pointer);
		} else {
			setCursor(style::cur_default);
		}
	}
	return result;
}

void MediaView::updateOver(const QPoint &pos) {
	if (_leftNavVisible && _leftNav.contains(pos)) {
		if (!updateOverState(OverLeftNav)) {
			update(_leftNav);
		}
	} else if (_rightNavVisible && _rightNav.contains(pos)) {
		if (!updateOverState(OverRightNav)) {
			update(_rightNav);
		}
	} else if (_nameNav.contains(pos)) {
		if (!updateOverState(OverName)) {
			update(_nameNav);
		}
	} else if (_msgid && _dateNav.contains(pos)) {
		if (!updateOverState(OverDate)) {
			update(_dateNav);
		}
	} else if (_over != OverNone) {
		if (_over == OverLeftNav) {
			update(_leftNav);
		} else if (_over == OverRightNav) {
			update(_rightNav);
		} else if (_over == OverName) {
			update(_nameNav);
		} else if (_over == OverDate) {
			update(_dateNav);
		}
		updateOverState(OverNone);
	}
}

void MediaView::mouseReleaseEvent(QMouseEvent *e) {
	updateOver(e->pos());
	if (_over == OverName && _down == OverName) {
		if (App::wnd()) {
			onClose();
			if (App::main()) App::main()->showPeerProfile(_from);
		}
	} else if (_over == OverDate && _down == OverDate && _msgid) {
		HistoryItem *item = App::histItemById(_msgid);
		if (item) {
			if (App::wnd()) {
				onClose();
				if (App::main()) App::main()->showPeer(item->history()->peer->id, _msgid, false, true);
			}
		}
	}
	_down = OverNone;
}

void MediaView::contextMenuEvent(QContextMenuEvent *e) {
	if (_photo && _photo->full->loaded() && (e->reason() != QContextMenuEvent::Mouse || QRect(_x, _y, _current.width() / cIntRetinaFactor(), _current.height() / cIntRetinaFactor()).contains(e->pos()))) {
		
		if (_menu) {
			_menu->deleteLater();
			_menu = 0;
		}
		_menu = new QMenu(this);
		_menu->addAction(lang(lng_context_save_image), this, SLOT(onSave()))->setEnabled(true);
		_menu->addAction(lang(lng_context_copy_image), this, SLOT(onCopy()))->setEnabled(true);
		_menu->addAction(lang(lng_context_close_image), this, SLOT(onClose()))->setEnabled(true);
		if (_msgid) {
			_menu->addAction(lang(lng_context_forward_image), this, SLOT(onForward()))->setEnabled(true);
			_menu->addAction(lang(lng_context_delete_image), this, SLOT(onDelete()))->setEnabled(true);
		} else if ((App::self() && App::self()->photoId == _photo->id) || (_photo->chat && _photo->chat->photoId == _photo->id)) {
			_menu->addAction(lang(lng_context_delete_image), this, SLOT(onDelete()))->setEnabled(true);
		}
		_menu->setAttribute(Qt::WA_DeleteOnClose);
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
	}
}

void MediaView::touchEvent(QTouchEvent *e) {
	switch (e->type()) {
	case QEvent::TouchBegin:
		if (_touchPress || e->touchPoints().isEmpty()) return;
		_touchTimer.start(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
		break;

	case QEvent::TouchUpdate:
		if (!_touchPress || e->touchPoints().isEmpty()) return;
		if (!_touchMove && (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchMove = true;
		}
		break;

	case QEvent::TouchEnd:
		if (!_touchPress) return;
		if (!_touchMove && App::wnd()) {
			Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);
			QPoint mapped(mapFromGlobal(_touchStart)), winMapped(App::wnd()->mapFromGlobal(_touchStart));

			QMouseEvent pressEvent(QEvent::MouseButtonPress, mapped, winMapped, _touchStart, btn, Qt::MouseButtons(btn), Qt::KeyboardModifiers());
			pressEvent.accept();
			mousePressEvent(&pressEvent);

			QMouseEvent releaseEvent(QEvent::MouseButtonRelease, mapped, winMapped, _touchStart, btn, Qt::MouseButtons(btn), Qt::KeyboardModifiers());
			mouseReleaseEvent(&releaseEvent);

			if (_touchRightButton) {
				QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, mapped, _touchStart);
				contextMenuEvent(&contextEvent);
			}
		} else if (_touchMove) {
			if ((!_leftNavVisible || !_leftNav.contains(mapFromGlobal(_touchStart))) && (!_rightNavVisible || !_rightNav.contains(mapFromGlobal(_touchStart)))) {
				QPoint d = (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart);
				if (d.x() * d.x() > d.y() * d.y() && (d.x() > st::medviewSwipeDistance || d.x() < -st::medviewSwipeDistance)) {
					moveToPhoto(d.x() > 0 ? -1 : 1);
				}
			}
		}
		_touchTimer.stop();
		_touchPress = _touchMove = _touchRightButton = false;
		break;

	case QEvent::TouchCancel:
		_touchPress = false;
		_touchTimer.stop();
		break;
	}
}

bool MediaView::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			if (!ev->touchPoints().isEmpty()) {
				QPoint p(mapFromGlobal(ev->touchPoints().cbegin()->screenPos().toPoint()));
				if ((!_close.isHidden() && _close.geometry().contains(p)) ||
				    (!_save.isHidden() && _save.geometry().contains(p)) ||
				    (!_forward.isHidden() && _forward.geometry().contains(p)) ||
					(!_delete.isHidden() && _delete.geometry().contains(p))) {
					return QWidget::event(e);
				}
			}
			touchEvent(ev);
			return true;
		}
	}
	return QWidget::event(e);
}

void MediaView::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
	}
	_receiveMouse = false;
	QTimer::singleShot(0, this, SLOT(receiveMouse()));
}

void MediaView::receiveMouse() {
	_receiveMouse = true;
}

void MediaView::onCheckActive() {
	if (App::wnd() && isVisible()) {
		if (App::wnd()->isActiveWindow()) {
			activateWindow();
			setFocus();
		}
	}
}

void MediaView::onTouchTimer() {
	_touchRightButton = true;
}

void MediaView::loadPhotosBack() {
	if (_loadRequest || _index < 0) return;

	if (_history && _history->_photosOverviewCount != 0) {
		MsgId minId = 0;
		for (History::MediaOverviewIds::const_iterator i = _history->_photosOverviewIds.cbegin(), e = _history->_photosOverviewIds.cend(); i != e; ++i) {
			if (*i > 0) {
				minId = *i;
				break;
			}
		}
		int32 limit = (_index < MediaOverviewStartPerPage && _history->_photosOverview.size() > MediaOverviewStartPerPage) ? SearchPerPage : MediaOverviewStartPerPage;
		_loadRequest = MTP::send(MTPmessages_Search(_history->peer->input, MTPstring(), MTP_inputMessagesFilterPhotos(), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(minId), MTP_int(limit)), rpcDone(&MediaView::photosLoaded, _history));
	} else if (_user && _user->photosCount != 0) {
		int32 limit = (_index < MediaOverviewStartPerPage && _user->photos.size() > MediaOverviewStartPerPage) ? SearchPerPage : MediaOverviewStartPerPage;
		_loadRequest = MTP::send(MTPphotos_GetUserPhotos(_user->inputUser, MTP_int(_user->photos.size()), MTP_int(0), MTP_int(limit)), rpcDone(&MediaView::userPhotosLoaded, _user));
	}
}

void MediaView::photosLoaded(History *h, const MTPmessages_Messages &msgs, mtpRequestId req) {
	if (req == _loadRequest) {
		_loadRequest = 0;
	}

	const QVector<MTPMessage> *v = 0;
	switch (msgs.type()) {
	case mtpc_messages_messages: {
		const MTPDmessages_messages &d(msgs.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
		h->_photosOverviewCount = 0;
	} break;

	case mtpc_messages_messagesSlice: {
		const MTPDmessages_messagesSlice &d(msgs.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		h->_photosOverviewCount = d.vcount.v;
		v = &d.vmessages.c_vector().v;
	} break;

	default: return;
	}

	if (h->_photosOverviewCount > 0) {
		for (History::MediaOverviewIds::const_iterator i = h->_photosOverviewIds.cbegin(), e = h->_photosOverviewIds.cend(); i != e; ++i) {
			if (*i < 0) {
				++h->_photosOverviewCount;
			} else {
				break;
			}
		}
	}
	if (v->isEmpty()) {
		h->_photosOverviewCount = 0;
	}

	for (QVector<MTPMessage>::const_iterator i = v->cbegin(), e = v->cend(); i != e; ++i) {
		HistoryItem *item = App::histories().addToBack(*i, -1);
		if (item && h->_photosOverviewIds.constFind(item->id) == h->_photosOverviewIds.cend()) {
			h->_photosOverviewIds.insert(item->id);
			h->_photosOverview.push_front(item->id);
		}
	}
	if (App::wnd()) App::wnd()->mediaOverviewUpdated(h->peer);
	preloadPhotos(0);
}

void MediaView::userPhotosLoaded(UserData *u, const MTPphotos_Photos &photos, mtpRequestId req) {
	if (req == _loadRequest) {
		_loadRequest = 0;
	}

	const QVector<MTPPhoto> *v = 0;
	switch (photos.type()) {
	case mtpc_photos_photos: {
		const MTPDphotos_photos &d(photos.c_photos_photos());
		App::feedUsers(d.vusers);
		v = &d.vphotos.c_vector().v;
		u->photosCount = 0;
	} break;

	case mtpc_photos_photosSlice: {
		const MTPDphotos_photosSlice &d(photos.c_photos_photosSlice());
		App::feedUsers(d.vusers);
		u->photosCount = d.vcount.v;
		v = &d.vphotos.c_vector().v;
	} break;

	default: return;
	}

	if (v->isEmpty()) {
		u->photosCount = 0;
	}

	for (QVector<MTPPhoto>::const_iterator i = v->cbegin(), e = v->cend(); i != e; ++i) {
		PhotoData *photo = App::feedPhoto(*i);
		photo->thumb->load();
		u->photos.push_back(photo);
	}
	if (App::wnd()) App::wnd()->mediaOverviewUpdated(u);
	preloadPhotos(0);
}

void MediaView::updateHeader() {
	int32 index = _index, count = 0;
	if (_history) {
		count = _history->_photosOverviewCount ? _history->_photosOverviewCount : _history->_photosOverview.size();
		if (index >= 0) index += count - _history->_photosOverview.size();
	} else if (_user) {
		count = _user->photosCount ? _user->photosCount : _user->photos.size();
	}
	if (_index >= 0 && _index < count && count > 1) {
		_header = lang(lng_mediaview_n_of_count).replace(qsl("{n}"), QString::number(index + 1)).replace(qsl("{count}"), QString::number(count));
	} else if (_user) {
		_header = lang(lng_mediaview_profile_photo);
	} else if (_peer) {
		_header = lang(lng_mediaview_group_photo);
	} else {
		_header = lang(lng_mediaview_single_photo);
	}
}

float64 MediaView::overLevel(OverState control) {
	ShowingOpacities::const_iterator i = _animOpacities.constFind(control);
	return (i == _animOpacities.cend()) ? (_over == control ? 1 : 0) : i->current();
}

QColor MediaView::nameDateColor(float64 over) {
	float64 mover = 1 - over;
	QColor result;
	result.setRedF(over * st::medviewNameOverColor->c.redF() + mover * st::medviewNameColor->c.redF());
	result.setGreenF(over * st::medviewNameOverColor->c.greenF() + mover * st::medviewNameColor->c.greenF());
	result.setBlueF(over * st::medviewNameOverColor->c.blueF() + mover * st::medviewNameColor->c.blueF());
	result.setAlphaF(over * st::medviewNameOverColor->c.alphaF() + mover * st::medviewNameColor->c.alphaF());
	return result;
}
