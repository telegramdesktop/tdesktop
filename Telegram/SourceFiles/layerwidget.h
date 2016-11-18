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

namespace Window {
class MainMenu;
} // namespace Window

#include "ui/effects/rect_shadow.h"

class LayerWidget : public TWidget {
	Q_OBJECT

public:
	using TWidget::TWidget;

	virtual void parentResized() = 0;
	virtual void showFinished() {
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

	void finishAnimation();

	void showLayer(LayerWidget *layer);
	void showSpecialLayer(LayerWidget *layer);
	void showMainMenu();
	void appendLayer(LayerWidget *layer);
	void prependLayer(LayerWidget *layer);

	bool canSetFocus() const;
	void setInnerFocus();

	bool contentOverlapped(const QRect &globalRect);

	void hideLayers();
	void hideAll();

	~LayerStackWidget();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onLayerDestroyed(QObject *obj);
	void onLayerClosed(LayerWidget *layer);
	void onLayerResized();

private:
	void showFinished();
	void hideCurrent();

	enum class Action {
		ShowMainMenu,
		ShowSpecialLayer,
		ShowLayer,
		HideLayer,
		HideAll,
	};
	template <typename SetupNew, typename ClearOld>
	void startAnimation(SetupNew setupNewWidgets, ClearOld clearOldWidgets, Action action);

	void prepareForAnimation();
	void animationDone();

	void setCacheImages();
	void clearLayers();
	void initChildLayer(LayerWidget *layer);
	void updateLayerBoxes();
	void fixOrder();
	void sendFakeMouseEvent();

	LayerWidget *currentLayer() {
		return _layers.empty() ? nullptr : _layers.back();
	}
	const LayerWidget *currentLayer() const {
		return const_cast<LayerStackWidget*>(this)->currentLayer();
	}

	using Layers = QList<LayerWidget*>;
	Layers _layers;

	ChildWidget<LayerWidget> _specialLayer = { nullptr };
	ChildWidget<Window::MainMenu> _mainMenu = { nullptr };

	class BackgroundWidget;
	ChildWidget<BackgroundWidget> _background;

};

class MediaPreviewWidget : public TWidget, private base::Subscriber {
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
	void fillEmojiString();
	void resetGifAndCache();

	anim::fvalue a_shown;
	Animation _a_shown;
	DocumentData *_document = nullptr;
	PhotoData *_photo = nullptr;
	Media::Clip::ReaderPointer _gif;

	int _emojiSize;
	QList<EmojiPtr> _emojiList;

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
