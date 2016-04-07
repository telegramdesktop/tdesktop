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
#pragma once

#include "gui/boxshadow.h"

class LayeredWidget : public TWidget {
	Q_OBJECT

public:

	virtual void showStep(float64 ms) {
	}
	virtual void parentResized() = 0;
	virtual void startHide() {
	}

	virtual void setInnerFocus() {
		setFocus();
	}

	virtual void resizeEvent(QResizeEvent *e) {
		emit resized();
	}

	void mousePressEvent(QMouseEvent *e) {
		e->accept();
	}

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || !testAttribute(Qt::WA_OpaquePaintEvent)) return false;
		return rect().contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

signals:

	void closed();
	void resized();

};

class BackgroundWidget : public TWidget {
	Q_OBJECT

public:

	BackgroundWidget(QWidget *parent, LayeredWidget *w);

	void showFast();

	void paintEvent(QPaintEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);

	void updateAdaptiveLayout();

	void replaceInner(LayeredWidget *n);
	void showLayerLast(LayeredWidget *n);

	void step_background(float64 ms, bool timer);

	bool canSetFocus() const;
	void setInnerFocus();

	bool contentOverlapped(const QRect &globalRect);

	~BackgroundWidget();

public slots:

	void onClose();
	bool onInnerClose();
	void boxDestroyed(QObject *obj);

private:

	void startHide();

	LayeredWidget *w;
	typedef QList<LayeredWidget*> HiddenLayers;
	HiddenLayers _hidden;
	anim::fvalue a_bg;
	Animation _a_background;

	bool hiding;

	BoxShadow shadow;
};

class StickerPreviewWidget : public TWidget {
	Q_OBJECT

public:

	StickerPreviewWidget(QWidget *parent);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void step_shown(float64 ms, bool timer);

	void showPreview(DocumentData *sticker);
	void hidePreview();

	~StickerPreviewWidget();

private:

	QSize currentDimensions() const;
	QPixmap currentImage() const;

	anim::fvalue a_shown;
	Animation _a_shown;
	DocumentData *_doc;
	ClipReader *_gif;
	bool gif() const {
		return (!_gif || _gif == BadClipReader) ? false : true;
	}

	void clipCallback(ClipReaderNotification notification);

	enum CacheStatus {
		CacheNotLoaded,
		CacheThumbLoaded,
		CacheLoaded,
	};
	mutable CacheStatus _cacheStatus;
	mutable QPixmap _cache;

};
