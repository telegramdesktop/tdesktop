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
#include "lang.h"

#include "layerwidget.h"
#include "application.h"
#include "window.h"
#include "mainwidget.h"
#include "gui/filedialog.h"

BackgroundWidget::BackgroundWidget(QWidget *parent, LayeredWidget *w) : TWidget(parent)
, w(w)
, a_bg(0)
, _a_background(animation(this, &BackgroundWidget::step_background))
, hiding(false)
, shadow(st::boxShadow) {
	w->setParent(this);
	if (App::app()) App::app()->mtpPause();
	setGeometry(0, 0, App::wnd()->width(), App::wnd()->height());
	a_bg.start(1);
	_a_background.start();
	show();
	connect(w, SIGNAL(closed()), this, SLOT(onInnerClose()));
	connect(w, SIGNAL(resized()), this, SLOT(update()));
	connect(w, SIGNAL(destroyed(QObject*)), this, SLOT(boxDestroyed(QObject*)));
	w->setFocus();
}

void BackgroundWidget::showFast() {
	_a_background.step(getms() + st::layerSlideDuration + 1);
	update();
}

void BackgroundWidget::paintEvent(QPaintEvent *e) {
	if (!w) return;
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	p.setOpacity(st::layerAlpha * a_bg.current());
	p.fillRect(rect(), st::layerBg->b);

	p.setOpacity(a_bg.current());
	shadow.paint(p, w->geometry(), st::boxShadowShift);
}

void BackgroundWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		onClose();
	}
}

void BackgroundWidget::mousePressEvent(QMouseEvent *e) {
	onClose();
}

void BackgroundWidget::onClose() {
	startHide();
}

bool BackgroundWidget::onInnerClose() {
	if (_hidden.isEmpty()) {
		onClose();
		return true;
	}
	w->hide();
	w->deleteLater();
	w = _hidden.back();
	_hidden.pop_back();
	w->show();
	resizeEvent(0);
	w->showStep(1);
	update();
	return false;
}

void BackgroundWidget::startHide() {
	if (hiding) return;
	if (App::app()) App::app()->mtpPause();

	hiding = true;
	if (App::wnd()) App::wnd()->setInnerFocus();
	a_bg.start(0);
	_a_background.start();
	w->startHide();
}

bool BackgroundWidget::canSetFocus() const {
	return w && !hiding;
}

void BackgroundWidget::setInnerFocus() {
	if (w) {
		w->setInnerFocus();
	}
}

bool BackgroundWidget::contentOverlapped(const QRect &globalRect) {
	if (isHidden()) return false;
	return w && w->overlaps(globalRect);
}

void BackgroundWidget::resizeEvent(QResizeEvent *e) {
	w->parentResized();
}

void BackgroundWidget::updateAdaptiveLayout() {
}

void BackgroundWidget::replaceInner(LayeredWidget *n) {
	_hidden.push_back(w);
	w->hide();
	w = n;
	w->setParent(this);
	connect(w, SIGNAL(closed()), this, SLOT(onInnerClose()));
	connect(w, SIGNAL(resized()), this, SLOT(update()));
	connect(w, SIGNAL(destroyed(QObject*)), this, SLOT(boxDestroyed(QObject*)));
	w->show();
	resizeEvent(0);
	w->showStep(1);
	update();
}

void BackgroundWidget::showLayerLast(LayeredWidget *n) {
	_hidden.push_front(n);
	n->setParent(this);
	connect(n, SIGNAL(closed()), this, SLOT(onInnerClose()));
	connect(n, SIGNAL(resized()), this, SLOT(update()));
	connect(n, SIGNAL(destroyed(QObject*)), this, SLOT(boxDestroyed(QObject*)));
	n->parentResized();
	n->showStep(1);
	n->hide();
	update();
}

void BackgroundWidget::step_background(float64 ms, bool timer) {
	float64 dt = ms / (hiding ? st::layerHideDuration : st::layerSlideDuration);
	w->showStep(dt);
	if (dt >= 1) {
		a_bg.finish();
		if (hiding)	{
			App::wnd()->layerFinishedHide(this);
		}
		_a_background.stop();
		if (App::app()) App::app()->mtpUnpause();
	} else {
		a_bg.update(dt, anim::easeOutCirc);
	}
	if (timer) update();
}

void BackgroundWidget::boxDestroyed(QObject *obj) {
	if (obj == w) {
		if (App::wnd()) App::wnd()->layerFinishedHide(this);
		w = 0;
	} else {
		int32 index = _hidden.indexOf(static_cast<LayeredWidget*>(obj));
		if (index >= 0) {
			_hidden.removeAt(index);
		}
	}
}

BackgroundWidget::~BackgroundWidget() {
	if (App::wnd()) App::wnd()->noBox(this);
	w->deleteLater();
	for (HiddenLayers::const_iterator i = _hidden.cbegin(), e = _hidden.cend(); i != e; ++i) {
		(*i)->deleteLater();
	}
}

StickerPreviewWidget::StickerPreviewWidget(QWidget *parent) : TWidget(parent)
, a_shown(0, 0)
, _a_shown(animation(this, &StickerPreviewWidget::step_shown))
, _doc(0)
, _gif(0)
, _cacheStatus(CacheNotLoaded) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
}

void StickerPreviewWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r(e->rect());

	const QPixmap &draw(currentImage());
	uint32 w = draw.width() / cIntRetinaFactor(), h = draw.height() / cIntRetinaFactor();
	if (_a_shown.animating()) {
		float64 shown = a_shown.current();
		p.setOpacity(shown);
//		w = qMax(qRound(w * (st::stickerPreviewMin + ((1. - st::stickerPreviewMin) * shown)) / 2.) * 2 + int(w % 2), 1);
//		h = qMax(qRound(h * (st::stickerPreviewMin + ((1. - st::stickerPreviewMin) * shown)) / 2.) * 2 + int(h % 2), 1);
	}
	p.fillRect(r, st::stickerPreviewBg);
	p.drawPixmap((width() - w) / 2, (height() - h) / 2, draw);
}

void StickerPreviewWidget::resizeEvent(QResizeEvent *e) {
	update();
}

void StickerPreviewWidget::step_shown(float64 ms, bool timer) {
	float64 dt = ms / st::stickerPreviewDuration;
	if (dt >= 1) {
		_a_shown.stop();
		a_shown.finish();
		if (a_shown.current() < 0.5) hide();
	} else {
		a_shown.update(dt, anim::linear);
	}
	if (timer) update();
}

void StickerPreviewWidget::showPreview(DocumentData *sticker) {
	if (sticker && !sticker->isAnimation() && !sticker->sticker()) sticker = 0;
	if (sticker) {
		_cache = QPixmap();
		if (isHidden() || _a_shown.animating()) {
			if (isHidden()) show();
			a_shown.start(1);
			_a_shown.start();
		} else {
			update();
		}
	} else if (isHidden()) {
		return;
	} else {
		if (_gif) _cache = currentImage();
		a_shown.start(0);
		_a_shown.start();
	}
	_doc = sticker;
	if (_gif) {
		if (gif()) {
			delete _gif;
		}
		_gif = 0;
	}
	_cacheStatus = CacheNotLoaded;
}

void StickerPreviewWidget::hidePreview() {
	showPreview(0);
}

QSize StickerPreviewWidget::currentDimensions() const {
	if (!_doc) return QSize(_cache.width() / cIntRetinaFactor(), _cache.height() / cIntRetinaFactor());

	QSize result(qMax(convertScale(_doc->dimensions.width()), 1), qMax(convertScale(_doc->dimensions.height()), 1));
	if (gif() && _gif->ready()) {
		result = QSize(qMax(convertScale(_gif->width()), 1), qMax(convertScale(_gif->height()), 1));
	}
	if (result.width() > st::maxStickerSize) {
		result.setHeight(qMax(qRound((st::maxStickerSize * result.height()) / result.width()), 1));
		result.setWidth(st::maxStickerSize);
	}
	if (result.height() > st::maxStickerSize) {
		result.setWidth(qMax(qRound((st::maxStickerSize * result.width()) / result.height()), 1));
		result.setHeight(st::maxStickerSize);
	}
	return result;
}

QPixmap StickerPreviewWidget::currentImage() const {
	if (_doc) {
		if (_doc->sticker()) {
			if (_cacheStatus != CacheLoaded) {
				_doc->checkSticker();
				if (_doc->sticker()->img->isNull()) {
					if (_cacheStatus != CacheThumbLoaded && _doc->thumb->loaded()) {
						QSize s = currentDimensions();
						_cache = _doc->thumb->pixBlurred(s.width(), s.height());
						_cacheStatus = CacheThumbLoaded;
					}
				} else {
					QSize s = currentDimensions();
					_cache = _doc->sticker()->img->pix(s.width(), s.height());
					_cacheStatus = CacheLoaded;
				}
			}
		} else {
			_doc->automaticLoad(0);
			if (_doc->loaded()) {
				if (!_gif && _gif != BadClipReader) {
					StickerPreviewWidget *that = const_cast<StickerPreviewWidget*>(this);
					that->_gif = new ClipReader(_doc->location(), _doc->data(), func(that, &StickerPreviewWidget::clipCallback));
					if (gif()) _gif->setAutoplay();
				}
			}
			if (gif() && _gif->started()) {
				QSize s = currentDimensions();
				return _gif->current(s.width(), s.height(), s.width(), s.height(), getms());
			}
			if (_cacheStatus != CacheThumbLoaded && _doc->thumb->loaded()) {
				QSize s = currentDimensions();
				_cache = _doc->thumb->pixBlurred(s.width(), s.height());
				_cacheStatus = CacheThumbLoaded;
			}
		}
	}
	return _cache;
}

void StickerPreviewWidget::clipCallback(ClipReaderNotification notification) {
	switch (notification) {
	case ClipReaderReinit: {
		if (gif() && _gif->state() == ClipError) {
			delete _gif;
			_gif = BadClipReader;
		}

		if (gif() && _gif->ready() && !_gif->started()) {
			QSize s = currentDimensions();
			_gif->start(s.width(), s.height(), s.width(), s.height(), false);
		}

		update();
	} break;

	case ClipReaderRepaint: {
		if (gif() && !_gif->currentDisplayed()) {
			update();
		}
	} break;
	}
}

StickerPreviewWidget::~StickerPreviewWidget() {
}
