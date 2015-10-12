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

BackgroundWidget::BackgroundWidget(QWidget *parent, LayeredWidget *w) : QWidget(parent), w(w),
aBackground(0), aBackgroundFunc(anim::easeOutCirc), hiding(false), shadow(st::boxShadow) {
	w->setParent(this);
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
		startHide();
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
