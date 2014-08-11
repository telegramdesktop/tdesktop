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
