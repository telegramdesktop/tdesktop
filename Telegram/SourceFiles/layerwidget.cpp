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

#include "layerwidget.h"
#include "application.h"
#include "window.h"
#include "mainwidget.h"
#include "gui/filedialog.h"

BackgroundWidget::BackgroundWidget(QWidget *parent, LayeredWidget *w) : QWidget(parent), w(w), _hidden(0),
	aBackground(0), aBackgroundFunc(anim::easeOutCirc), hiding(false), shadow(st::boxShadow) {
	w->setParent(this);
	setGeometry(0, 0, App::wnd()->width(), App::wnd()->height());
	aBackground.start(1);
	anim::start(this);
	show();
	connect(w, SIGNAL(closed()), this, SLOT(onInnerClose()));
	connect(w, SIGNAL(resized()), this, SLOT(update()));
	w->setFocus();
}

void BackgroundWidget::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	p.setOpacity(st::layerAlpha * aBackground.current());
	p.fillRect(rect(), st::layerBG->b);

	p.setOpacity(aBackground.current());
	shadow.paint(p, w->boxRect());
}

void BackgroundWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		startHide();
	}
}

void BackgroundWidget::mousePressEvent(QMouseEvent *e) {
}

void BackgroundWidget::onClose() {
	startHide();
}

void BackgroundWidget::onInnerClose() {
	if (_hidden) {
		w->deleteLater();
		w = _hidden;
		_hidden = 0;
		w->show();
		resizeEvent(0);
		w->animStep(1);
		update();
	} else {
		onClose();
	}
}

void BackgroundWidget::startHide() {
	if (App::main()) App::main()->setInnerFocus();
	hiding = true;
	aBackground.start(0);
	anim::start(this);
	w->startHide();
}

void BackgroundWidget::resizeEvent(QResizeEvent *e) {
	w->parentResized();
}

void BackgroundWidget::replaceInner(LayeredWidget *n) {
	if (_hidden) _hidden->deleteLater();
	_hidden = w;
	_hidden->hide();
	w = n;
	w->setParent(this);
	connect(w, SIGNAL(closed()), this, SLOT(onInnerClose()));
	connect(w, SIGNAL(resized()), this, SLOT(update()));
	w->show();
	resizeEvent(0);
	w->animStep(1);
	update();
}

bool BackgroundWidget::animStep(float64 ms) {
	float64 dt = ms / (hiding ? st::layerHideDuration : st::layerSlideDuration);
	w->animStep(dt);
	bool res = true;
	if (dt >= 1) {
		aBackground.finish();
		if (hiding)	{
			QTimer::singleShot(0, App::wnd(), SLOT(layerHidden()));
		}
		res = false;
	} else {
		aBackground.update(dt, aBackgroundFunc);
	}
	update();
	return res;
}

BackgroundWidget::~BackgroundWidget() {
	if (App::wnd()) App::wnd()->noBox(this);
	w->deleteLater();
	if (_hidden) _hidden->deleteLater();
}

LayerWidget::LayerWidget(QWidget *parent, PhotoData *photo, HistoryItem *item) : QWidget(parent)
, photo(photo)
, video(0)
, aBackground(0)
, aOver(0)
, iX(App::wnd()->width() / 2)
, iY(App::wnd()->height() / 2)
, iW(0)
, iCoordFunc(anim::sineInOut)
, aBackgroundFunc(anim::easeOutCirc)
, aOverFunc(anim::linear)
, hiding(false)
, _touchPress(false)
, _touchMove(false)
, _touchRightButton(false)
, _menu(0)
{
	int32 x, y, w;
	if (App::wnd()->getPhotoCoords(photo, x, y, w)) {
		iX = anim::ivalue(x);
		iY = anim::ivalue(y);
		iW = anim::ivalue(w);
	}
	photo->full->load();
	setGeometry(0, 0, App::wnd()->width(), App::wnd()->height());
	aBackground.start(1);
	aOver.start(1);
	anim::start(this);
	show();
	setFocus();
	App::contextItem(item);
	
	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));
}

LayerWidget::LayerWidget(QWidget *parent, VideoData *video, HistoryItem *item) : QWidget(parent)
, photo(0)
, video(video)
, aBackground(0)
, aOver(0)
, iX(App::wnd()->width() / 2)
, iY(App::wnd()->height() / 2)
, iW(0)
, iCoordFunc(anim::sineInOut)
, aBackgroundFunc(anim::easeOutCirc)
, aOverFunc(anim::linear)
, hiding(false)
, _touchPress(false)
, _touchMove(false)
, _touchRightButton(false)
, _menu(0)
{
	int32 x, y, w;
	if (App::wnd()->getVideoCoords(video, x, y, w)) {
		iX = anim::ivalue(x);
		iY = anim::ivalue(y);
		iW = anim::ivalue(w);
	}
	setGeometry(0, 0, App::wnd()->width(), App::wnd()->height());
	aBackground.start(1);
	aOver.start(1);
	anim::start(this);
	show();
	setFocus();
	App::contextItem(item);

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));
}

PhotoData *LayerWidget::photoShown() {
	return hiding ? 0 : photo;
}

void LayerWidget::onTouchTimer() {
	_touchRightButton = true;
}

bool LayerWidget::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
			return true;
		}
	}
	return QWidget::event(e);
}

void LayerWidget::touchEvent(QTouchEvent *e) {
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

void LayerWidget::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
	}
}

void LayerWidget::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	p.setOpacity(st::layerAlpha * aBackground.current());
	p.fillRect(rect(), st::layerBG->b);
	if (iW.current()) {
		if (!hiding) p.setOpacity(aOver.current());
		if (animating()) {
			const QPixmap &pm((photo ? (photo->full->loaded() ? photo->full : photo->thumb) : video->thumb)->pix());
			int32 h = pm.width() ? (pm.height() * iW.current() / pm.width()) : 1;
			p.drawPixmap(iX.current(), iY.current(), iW.current(), h, pm);
			if (!hiding) {
				p.setOpacity(1);
				p.setClipRect(App::wnd()->photoRect(), Qt::IntersectClip);
				p.drawPixmap(iX.current(), iY.current(), iW.current(), h, pm);
			}
		} else {
			const QPixmap &pm((photo ? (photo->full->loaded() ? photo->full : photo->thumb) : video->thumb)->pixNoCache(iW.current(), 0, !animating()));
			p.drawPixmap(iX.current(), iY.current(), pm);
		}
	}
}

void LayerWidget::keyPressEvent(QKeyEvent *e) {
	if (!_menu && e->key() == Qt::Key_Escape) {
		startHide();
	} else if (photo && photo->full->loaded() && (e == QKeySequence::Save || e == QKeySequence::SaveAs)) {
		QString file;
		if (filedialogGetSaveFile(file, lang(lng_save_photo), qsl("JPEG Image (*.jpg);;All files (*.*)"), filedialogDefaultName(qsl("photo"), qsl(".jpg")))) {
			if (!file.isEmpty()) {
				photo->full->pix().toImage().save(file, "JPG");
			}
		}
	} else if (photo && photo->full->loaded() && (e->key() == Qt::Key_Copy || (e->key() == Qt::Key_C && e->modifiers().testFlag(Qt::ControlModifier)))) {
		QApplication::clipboard()->setPixmap(photo->full->pix());
	}
}

void LayerWidget::mousePressEvent(QMouseEvent *e) {
	if (_menu) return;
	if (e->button() == Qt::LeftButton) startHide();
}

void LayerWidget::contextMenuEvent(QContextMenuEvent *e) {
	if (photo && photo->full->loaded() && !hiding) {
		if (_menu) {
			_menu->deleteLater();
			_menu = 0;
		}
		_menu = new QMenu(this);
		_menu->addAction(lang(lng_context_save_image), this, SLOT(saveContextImage()))->setEnabled(true);
		_menu->addAction(lang(lng_context_copy_image), this, SLOT(copyContextImage()))->setEnabled(true);
		_menu->addAction(lang(lng_context_close_image), this, SLOT(startHide()))->setEnabled(true);
		if (App::contextItem()) {
			if (dynamic_cast<HistoryMessage*>(App::contextItem())) {
				_menu->addAction(lang(lng_context_forward_image), this, SLOT(forwardMessage()))->setEnabled(true);
			}
			_menu->addAction(lang(lng_context_delete_image), this, SLOT(deleteMessage()))->setEnabled(true);
		} else if ((App::self() && App::self()->photoId == photo->id) || (photo->chat && photo->chat->photoId == photo->id)) {
			_menu->addAction(lang(lng_context_delete_image), this, SLOT(deleteMessage()))->setEnabled(true);
		}
		_menu->setAttribute(Qt::WA_DeleteOnClose);

		_menu->setAttribute(Qt::WA_DeleteOnClose);
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
	}
}

void LayerWidget::deleteMessage() {
	if (!App::contextItem()) {
		if (App::self() && photo && App::self()->photoId == photo->id) {
			App::app()->peerClearPhoto(App::self()->id);
		} else if (photo->chat && photo->chat->photoId == photo->id) {
			App::app()->peerClearPhoto(photo->chat->id);
		}
		startHide();
	} else {
		App::wnd()->layerHidden();
		App::main()->deleteLayer();
	}
}

void LayerWidget::forwardMessage() {
	startHide();
	App::main()->forwardLayer();
}

void LayerWidget::saveContextImage() {
	if (!photo || !photo->full->loaded() || hiding) return;

	QString file;
	if (filedialogGetSaveFile(file, lang(lng_save_photo), qsl("JPEG Image (*.jpg);;All files (*.*)"), filedialogDefaultName(qsl("photo"), qsl(".jpg")))) {
		if (!file.isEmpty()) {
			photo->full->pix().toImage().save(file, "JPG");
		}
	}
}

void LayerWidget::copyContextImage() {
	if (!photo || !photo->full->loaded() || hiding) return;

	QApplication::clipboard()->setPixmap(photo->full->pix());
}

void LayerWidget::startHide() {
	hiding = true;
	aBackground.start(0);
	anim::start(this);
}

void LayerWidget::resizeEvent(QResizeEvent *e) {
	int32 w = width() - st::layerPadding.left() - st::layerPadding.right(), h = height() - st::layerPadding.top() - st::layerPadding.bottom();
	int32 iw = (photo ? photo->full : video->thumb)->width(), ih = (photo ? photo->full : video->thumb)->height();
	if (!iw || !ih) {
		iw = ih = 1;
	} else {
		switch (cScale()) {
		case dbisOneAndQuarter: iw = qRound(float64(iw) * 1.25 - 0.01); ih = qRound(float64(ih) * 1.25 - 0.01); break;
		case dbisOneAndHalf: iw = qRound(float64(iw) * 1.5 - 0.01); ih = qRound(float64(ih) * 1.5 - 0.01); break;
		case dbisTwo: iw *= 2; ih *= 2; break;
		}
	}
	if (w >= iw && h >= ih) {
		iW.start(iw);
		iX.start(st::layerPadding.left() + (w - iw) / 2);
		iY.start(st::layerPadding.top() + (h - ih) / 2);
	} else if (w * ih > iw * h) {
		int32 nw = qRound(iw * float64(h) / ih);
		iW.start(nw);
		iX.start(st::layerPadding.left() + (w - nw) / 2);
		iY.start(st::layerPadding.top());
	} else {
		int32 nh = qRound(ih * float64(w) / iw);
		iW.start(w);
		iX.start(st::layerPadding.left());
		iY.start(st::layerPadding.top() + (h - nh) / 2);
	}
	if (!animating() || hiding) {
		iX.finish();
		iY.finish();
		iW.finish();
	}
}

bool LayerWidget::animStep(float64 ms) {
	float64 dt = ms / (hiding ? st::layerHideDuration : st::layerSlideDuration);
	bool res = true;
	if (dt >= 1) {
		aBackground.finish();
		aOver.finish();
		iX.finish();
		iY.finish();
		iW.finish();
		if (hiding)	{
			QTimer::singleShot(0, App::wnd(), SLOT(layerHidden()));
		}
		res = false;
	} else {
		aBackground.update(dt, aBackgroundFunc);
		if (!hiding) {
			aOver.update(dt, aOverFunc);
			iX.update(dt, iCoordFunc);
			iY.update(dt, iCoordFunc);
			iW.update(dt, iCoordFunc);
		}
	}
	update();
	return res;
}

LayerWidget::~LayerWidget() {
	if (App::wnd()) App::wnd()->noLayer(this);
	delete _menu;
}
