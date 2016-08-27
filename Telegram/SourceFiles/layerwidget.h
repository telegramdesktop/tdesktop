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

#include "ui/boxshadow.h"

class LayerWidget : public TWidget {
	Q_OBJECT

public:
	virtual void parentResized() = 0;
	virtual void showDone() {
	}
	void setInnerFocus();

	bool overlaps(const QRect &globalRect) {
		if (isHidden() || !testAttribute(Qt::WA_OpaquePaintEvent)) return false;
		return rect().contains(QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size()));
	}

protected:
	void mousePressEvent(QMouseEvent *e) override {
		e->accept();
	}
	void resizeEvent(QResizeEvent *e) override {
		emit resized();
	}
	virtual void doSetInnerFocus() {
		setFocus();
	}

signals:
	void closed(LayerWidget *layer);
	void resized();

};

class LayerStackWidget : public TWidget {
	Q_OBJECT

public:
	LayerStackWidget(QWidget *parent);

	void showFast();

	void showLayer(LayerWidget *l);
	void showSpecialLayer(LayerWidget *l);
	void appendLayer(LayerWidget *l);
	void prependLayer(LayerWidget *l);

	bool canSetFocus() const;
	void setInnerFocus();

	bool contentOverlapped(const QRect &globalRect);

	void onCloseCurrent();
	void onCloseLayers();
	void onClose();

	~LayerStackWidget();

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onLayerDestroyed(QObject *obj);
	void onLayerClosed(LayerWidget *l);
	void onLayerResized();

private:
	void clearLayers();
	void initChildLayer(LayerWidget *l);
	void activateLayer(LayerWidget *l);
	void updateLayerBox();
	void fixOrder();
	void sendFakeMouseEvent();

	void startShow();
	void startHide();
	void startAnimation(float64 toOpacity);

	void step_background(float64 ms, bool timer);

	LayerWidget *layer() {
		return _layers.empty() ? nullptr : _layers.back();
	}
	const LayerWidget *layer() const {
		return const_cast<LayerStackWidget*>(this)->layer();
	}

	using Layers = QList<LayerWidget*>;
	Layers _layers;

	ChildWidget<LayerWidget> _specialLayer = { nullptr };

	class BackgroundWidget;
	ChildWidget<BackgroundWidget> _background;

	anim::fvalue a_bg, a_layer;
	Animation _a_background;

	QPixmap _layerCache;
	QRect _layerCacheBox;
	QPixmap _hiddenSpecialLayerCache;
	QRect _hiddenSpecialLayerCacheBox;

	bool _hiding = false;

};

class MediaPreviewWidget : public TWidget {
	Q_OBJECT

public:

	MediaPreviewWidget(QWidget *parent);

	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void step_shown(float64 ms, bool timer);

	void showPreview(DocumentData *document);
	void showPreview(PhotoData *photo);
	void hidePreview();

	~MediaPreviewWidget();

private:

	QSize currentDimensions() const;
	QPixmap currentImage() const;
	void startShow();
	void resetGifAndCache();

	anim::fvalue a_shown;
	Animation _a_shown;
	DocumentData *_document = nullptr;
	PhotoData *_photo = nullptr;
	Media::Clip::Reader *_gif = nullptr;
	bool gif() const {
		return (!_gif || _gif == Media::Clip::BadReader) ? false : true;
	}

	void clipCallback(Media::Clip::Notification notification);

	enum CacheStatus {
		CacheNotLoaded,
		CacheThumbLoaded,
		CacheLoaded,
	};
	mutable CacheStatus _cacheStatus = CacheNotLoaded;
	mutable QPixmap _cache;
	mutable QSize _cachedSize;

};
