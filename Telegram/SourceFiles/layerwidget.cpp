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

#include "layerwidget.h"
#include "application.h"
#include "window.h"
#include "mainwidget.h"
#include "gui/filedialog.h"

BackgroundWidget::BackgroundWidget(QWidget *parent, LayeredWidget *w) : TWidget(parent)
, w(w)
, aBackground(0)
, aBackgroundFunc(anim::easeOutCirc)
, hiding(false)
, shadow(st::boxShadow) {
	w->setParent(this);
	if (App::app()) App::app()->mtpPause();
	setGeometry(0, 0, App::wnd()->width(), App::wnd()->height());
	aBackground.start(1);
	anim::start(this);
	show();
	connect(w, SIGNAL(closed()), this, SLOT(onInnerClose()));
	connect(w, SIGNAL(resized()), this, SLOT(update()));
	connect(w, SIGNAL(destroyed(QObject*)), this, SLOT(boxDestroyed(QObject*)));
	w->setFocus();
}

void BackgroundWidget::showFast() {
	animStep(st::layerSlideDuration + 1);
	update();
}

void BackgroundWidget::paintEvent(QPaintEvent *e) {
	if (!w) return;
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	p.setOpacity(st::layerAlpha * aBackground.current());
	p.fillRect(rect(), st::layerBg->b);

	p.setOpacity(aBackground.current());
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
	w->animStep(1);
	update();
	return false;
}

void BackgroundWidget::startHide() {
	if (hiding) return;
	if (App::app()) App::app()->mtpPause();

	hiding = true;
	if (App::wnd()) App::wnd()->setInnerFocus();
	aBackground.start(0);
	anim::start(this);
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

void BackgroundWidget::updateWideMode() {

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
	w->animStep(1);
	update();
}

void BackgroundWidget::showLayerLast(LayeredWidget *n) {
	_hidden.push_front(n);
	n->setParent(this);
	connect(n, SIGNAL(closed()), this, SLOT(onInnerClose()));
	connect(n, SIGNAL(resized()), this, SLOT(update()));
	connect(n, SIGNAL(destroyed(QObject*)), this, SLOT(boxDestroyed(QObject*)));
	n->parentResized();
	n->animStep(1);
	n->hide();
	update();
}

bool BackgroundWidget::animStep(float64 ms) {
	float64 dt = ms / (hiding ? st::layerHideDuration : st::layerSlideDuration);
	w->animStep(dt);
	bool res = true;
	if (dt >= 1) {
		aBackground.finish();
		if (hiding)	{
			App::wnd()->layerFinishedHide(this);
		}
		anim::stop(this);
		res = false;
		if (App::app()) App::app()->mtpUnpause();
	} else {
		aBackground.update(dt, aBackgroundFunc);
	}
	update();
	return res;
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
, _a_shown(animFunc(this, &StickerPreviewWidget::animStep_shown))
, _doc(0)
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

bool StickerPreviewWidget::animStep_shown(float64 ms) {
	float64 dt = ms / st::stickerPreviewDuration;
	if (dt >= 1) {
		a_shown.finish();
		_a_shown.stop();
		if (a_shown.current() < 0.5) hide();
	} else {
		a_shown.update(dt, anim::linear);
	}
	update();
	return true;
}

void StickerPreviewWidget::showPreview(DocumentData *sticker) {
	if (sticker && !sticker->sticker()) sticker = 0;
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
		a_shown.start(0);
		_a_shown.start();
	}
	_doc = sticker;
	_cacheStatus = CacheNotLoaded;
}

void StickerPreviewWidget::hidePreview() {
	showPreview(0);
}

QSize StickerPreviewWidget::currentDimensions() const {
	if (!_doc) return QSize(_cache.width() / cIntRetinaFactor(), _cache.height() / cIntRetinaFactor());

	QSize result(qMax(_doc->dimensions.width(), 1), qMax(_doc->dimensions.height(), 1));
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
	if (_doc && _cacheStatus != CacheLoaded) {
		bool already = !_doc->already().isEmpty(), hasdata = !_doc->data.isEmpty();
		if (!_doc->loader && _doc->status != FileFailed && !already && !hasdata) {
			_doc->save(QString());
		}
		if (_doc->sticker()->img->isNull() && (already || hasdata)) {
			if (already) {
				_doc->sticker()->img = ImagePtr(_doc->already());
			} else {
				_doc->sticker()->img = ImagePtr(_doc->data);
			}
		}
		if (_doc->sticker()->img->isNull()) {
			if (_cacheStatus != CacheThumbLoaded) {
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
	return _cache;
}

StickerPreviewWidget::~StickerPreviewWidget() {
}
