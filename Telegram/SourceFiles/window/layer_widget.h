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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/rp_widget.h"

namespace Window {

class MainMenu;
class Controller;
class SectionMemento;
struct SectionShow;

class LayerWidget : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	virtual void parentResized() = 0;
	virtual void showFinished() {
	}
	void setInnerFocus();
	bool setClosing() {
		if (!_closing) {
			_closing = true;
			closeHook();
			return true;
		}
		return false;
	}

	bool overlaps(const QRect &globalRect);

	void setClosedCallback(base::lambda<void()> callback) {
		_closedCallback = std::move(callback);
	}
	void setResizedCallback(base::lambda<void()> callback) {
		_resizedCallback = std::move(callback);
	}
	virtual bool takeToThirdSection() {
		return false;
	}
	virtual bool showSectionInternal(
			not_null<SectionMemento*> memento,
			const SectionShow &params) {
		return false;
	}

protected:
	void closeLayer() {
		if (_closedCallback) {
			_closedCallback();
		}
	}
	void mousePressEvent(QMouseEvent *e) override {
		e->accept();
	}
	void resizeEvent(QResizeEvent *e) override {
		if (_resizedCallback) {
			_resizedCallback();
		}
	}
	virtual void doSetInnerFocus() {
		setFocus();
	}
	virtual void closeHook() {
	}

private:
	bool _closing = false;
	base::lambda<void()> _closedCallback;
	base::lambda<void()> _resizedCallback;

};

class LayerStackWidget : public TWidget {
	Q_OBJECT

public:
	LayerStackWidget(QWidget *parent, Controller *controller);

	Controller *controller() const {
		return _controller;
	}
	void finishAnimating();

	void showBox(
		object_ptr<BoxContent> box,
		anim::type animated);
	void showSpecialLayer(
		object_ptr<LayerWidget> layer,
		anim::type animated);
	void showMainMenu(anim::type animated);
	void appendBox(
		object_ptr<BoxContent> box,
		anim::type animated);
	void prependBox(
		object_ptr<BoxContent> box,
		anim::type animated);
	bool takeToThirdSection();

	bool canSetFocus() const;
	void setInnerFocus();

	bool contentOverlapped(const QRect &globalRect);

	void hideSpecialLayer(anim::type animated);
	void hideLayers(anim::type animated);
	void hideAll(anim::type animated);
	void hideTopLayer(anim::type animated);

	bool showSectionInternal(
		not_null<SectionMemento*> memento,
		const SectionShow &params);

	bool layerShown() const;

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
	LayerWidget *pushBox(
		object_ptr<BoxContent> box,
		anim::type animated);
	void showFinished();
	void hideCurrent(anim::type animated);

	enum class Action {
		ShowMainMenu,
		ShowSpecialLayer,
		ShowLayer,
		HideSpecialLayer,
		HideLayer,
		HideAll,
	};
	template <typename SetupNew, typename ClearOld>
	void startAnimation(
		SetupNew setupNewWidgets,
		ClearOld clearOldWidgets,
		Action action,
		anim::type animated);

	void prepareForAnimation();
	void animationDone();

	void setCacheImages();
	void clearLayers();
	void clearSpecialLayer();
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

	Controller *_controller = nullptr;

	QList<LayerWidget*> _layers;

	object_ptr<LayerWidget> _specialLayer = { nullptr };
	object_ptr<MainMenu> _mainMenu = { nullptr };

	class BackgroundWidget;
	object_ptr<BackgroundWidget> _background;

};

} // namespace Window

class MediaPreviewWidget : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	MediaPreviewWidget(QWidget *parent, not_null<Window::Controller*> controller);

	void showPreview(DocumentData *document);
	void showPreview(PhotoData *photo);
	void hidePreview();

	~MediaPreviewWidget();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	QSize currentDimensions() const;
	QPixmap currentImage() const;
	void startShow();
	void fillEmojiString();
	void resetGifAndCache();

	not_null<Window::Controller*> _controller;

	Animation _a_shown;
	bool _hiding = false;
	DocumentData *_document = nullptr;
	PhotoData *_photo = nullptr;
	Media::Clip::ReaderPointer _gif;

	int _emojiSize;
	std::vector<not_null<EmojiPtr>> _emojiList;

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

template <typename BoxType, typename ...Args>
inline object_ptr<BoxType> Box(Args&&... args) {
	auto parent = static_cast<QWidget*>(nullptr);
	return object_ptr<BoxType>(parent, std::forward<Args>(args)...);
}
