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

#include "media/media_clip_reader.h"
#include "layerwidget.h"
#include "application.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "ui/filedialog.h"
#include "styles/style_boxes.h"
#include "styles/style_stickers.h"
#include "window/window_main_menu.h"

namespace {

constexpr int kStickerPreviewEmojiLimit = 10;

} // namespace

class LayerStackWidget::BackgroundWidget : public TWidget {
public:
	BackgroundWidget(QWidget *parent) : TWidget(parent)
	, _shadow(st::boxShadow) {
	}

	void setDoneCallback(base::lambda<void()> &&callback) {
		_doneCallback = std_::move(callback);
	}

	void setLayerBoxes(const QRect &specialLayerBox, const QRect &layerBox);
	void setCacheImages(QPixmap &&bodyCache, QPixmap &&mainMenuCache, QPixmap &&specialLayerCache, QPixmap &&layerCache);
	void startAnimation(Action action);
	void finishAnimation();

	bool animating() const {
		return _a_mainMenuShown.animating() || _a_specialLayerShown.animating() || _a_layerShown.animating();
	}

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	bool isShown() const {
		return _mainMenuShown || _specialLayerShown || _layerShown;
	}
	void checkIfDone();
	void setMainMenuShown(bool shown);
	void setSpecialLayerShown(bool shown);
	void setLayerShown(bool shown);
	void checkWasShown(bool wasShown);
	void animationCallback();

	QPixmap _bodyCache;
	QPixmap _mainMenuCache;
	QPixmap _specialLayerCache;
	QPixmap _layerCache;

	base::lambda<void()> _doneCallback;

	bool _wasAnimating = false;
	bool _inPaintEvent = false;
	Animation _a_shown;
	Animation _a_mainMenuShown;
	Animation _a_specialLayerShown;
	Animation _a_layerShown;

	Ui::RectShadow _shadow;

	QRect _specialLayerBox, _specialLayerCacheBox;
	QRect _layerBox, _layerCacheBox;
	int _mainMenuRight = 0;

	bool _mainMenuShown = false;
	bool _specialLayerShown = false;
	bool _layerShown = false;

};

void LayerStackWidget::BackgroundWidget::setCacheImages(QPixmap &&bodyCache, QPixmap &&mainMenuCache, QPixmap &&specialLayerCache, QPixmap &&layerCache) {
	_bodyCache = std_::move(bodyCache);
	_mainMenuCache = std_::move(mainMenuCache);
	_specialLayerCache = std_::move(specialLayerCache);
	_layerCache = std_::move(layerCache);
	_specialLayerCacheBox = _specialLayerBox;
	_layerCacheBox = _layerBox;
	setAttribute(Qt::WA_OpaquePaintEvent, !_bodyCache.isNull());
}

void LayerStackWidget::BackgroundWidget::startAnimation(Action action) {
	if (action == Action::ShowMainMenu) {
		setMainMenuShown(true);
	} else if (action != Action::HideLayer) {
		setMainMenuShown(false);
	}
	if (action == Action::ShowSpecialLayer) {
		setSpecialLayerShown(true);
	} else if (action == Action::ShowMainMenu || action == Action::HideAll) {
		setSpecialLayerShown(false);
	}
	if (action == Action::ShowLayer) {
		setLayerShown(true);
	} else {
		setLayerShown(false);
	}
	_wasAnimating = true;
	checkIfDone();
}

void LayerStackWidget::BackgroundWidget::checkIfDone() {
	if (!_wasAnimating || _inPaintEvent || animating()) {
		return;
	}
	_wasAnimating = false;
	_bodyCache = _mainMenuCache = _specialLayerCache = _layerCache = QPixmap();
	setAttribute(Qt::WA_OpaquePaintEvent, false);
	if (_doneCallback) {
		_doneCallback();
	}
}

void LayerStackWidget::BackgroundWidget::setMainMenuShown(bool shown) {
	auto wasShown = isShown();
	if (_mainMenuShown != shown) {
		_mainMenuShown = shown;
		_a_mainMenuShown.start([this] { animationCallback(); }, _mainMenuShown ? 0. : 1., _mainMenuShown ? 1. : 0., st::boxDuration, anim::easeOutCirc);
	}
	_mainMenuRight = _mainMenuShown ? (_mainMenuCache.width() / cIntRetinaFactor()) : 0;
	checkWasShown(wasShown);
}

void LayerStackWidget::BackgroundWidget::setSpecialLayerShown(bool shown) {
	auto wasShown = isShown();
	if (_specialLayerShown != shown) {
		_specialLayerShown = shown;
		_a_specialLayerShown.start([this] { animationCallback(); }, _specialLayerShown ? 0. : 1., _specialLayerShown ? 1. : 0., st::boxDuration);
	}
	checkWasShown(wasShown);
}

void LayerStackWidget::BackgroundWidget::setLayerShown(bool shown) {
	auto wasShown = isShown();
	if (_layerShown != shown) {
		_layerShown = shown;
		_a_layerShown.start([this] { animationCallback(); }, _layerShown ? 0. : 1., _layerShown ? 1. : 0., st::boxDuration);
	}
	checkWasShown(wasShown);
}

void LayerStackWidget::BackgroundWidget::checkWasShown(bool wasShown) {
	if (isShown() != wasShown) {
		_a_shown.start([this] { animationCallback(); }, wasShown ? 1. : 0., wasShown ? 0. : 1., st::boxDuration, anim::easeOutCirc);
	}
}

void LayerStackWidget::BackgroundWidget::setLayerBoxes(const QRect &specialLayerBox, const QRect &layerBox) {
	_specialLayerBox = specialLayerBox;
	_layerBox = layerBox;
	update();
}

void LayerStackWidget::BackgroundWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	_inPaintEvent = true;
	auto guard = base::scope_guard([this] {
		_inPaintEvent = false;
		checkIfDone();
	});

	if (!_bodyCache.isNull()) {
		p.drawPixmap(0, 0, _bodyCache);
	}

	auto specialLayerBox = _specialLayerCache.isNull() ? _specialLayerBox : _specialLayerCacheBox;
	auto layerBox = _layerCache.isNull() ? _layerBox : _layerCacheBox;

	auto ms = getms();
	auto mainMenuProgress = _a_mainMenuShown.current(ms, -1);
	auto mainMenuRight = (_mainMenuCache.isNull() || mainMenuProgress < 0) ? _mainMenuRight : (mainMenuProgress < 0) ? _mainMenuRight : anim::interpolate(0, _mainMenuCache.width() / cIntRetinaFactor(), mainMenuProgress);
	if (mainMenuRight) {
		if (!_specialLayerCache.isNull()) {
			specialLayerBox.setX(specialLayerBox.x() + mainMenuRight / 2);
		}
		if (!_layerCache.isNull()) {
			layerBox.setX(layerBox.x() + mainMenuRight / 2);
		}
	}
	auto bgOpacity = _a_shown.current(ms, isShown() ? 1. : 0.);
	auto specialLayerOpacity = _a_specialLayerShown.current(ms, _specialLayerShown ? 1. : 0.);
	auto layerOpacity = _a_layerShown.current(ms, _layerShown ? 1. : 0.);
	if (bgOpacity == 0.) {
		return;
	}

	p.setOpacity(bgOpacity);
	auto bg = myrtlrect(mainMenuRight, 0, width() - mainMenuRight, height());
	p.fillRect(bg, st::layerBg);
	if (mainMenuRight > 0) {
		_shadow.paint(p, myrtlrect(0, 0, mainMenuRight, height()), 0, Ui::RectShadow::Side::Right);
	}
	if (!specialLayerBox.isEmpty()) {
		p.setClipRegion(QRegion(bg) - specialLayerBox);
		_shadow.paint(p, specialLayerBox, st::boxShadowShift);
	}

	p.setClipping(false);
	if (!_specialLayerCache.isNull() && specialLayerOpacity > 0) {
		p.setOpacity(specialLayerOpacity);
		p.drawPixmap(specialLayerBox.topLeft(), _specialLayerCache);
	}
	if (!layerBox.isEmpty()) {
		if (!_specialLayerCache.isNull()) {
			p.setOpacity(layerOpacity * specialLayerOpacity);
			p.setClipRegion(QRegion(specialLayerBox) - layerBox);
			p.fillRect(specialLayerBox, st::layerBg);
		}
		p.setOpacity(layerOpacity);
		p.setClipRegion(QRegion(bg) - layerBox);
		_shadow.paint(p, layerBox, st::boxShadowShift);
		p.setClipping(false);
	}
	if (!_layerCache.isNull() && layerOpacity > 0) {
		p.setOpacity(layerOpacity);
		p.drawPixmap(layerBox.topLeft(), _layerCache);
	}
	if (!_mainMenuCache.isNull() && mainMenuRight > 0) {
		p.setOpacity(1.);
		auto shownWidth = mainMenuRight * cIntRetinaFactor();
		auto shownRect = rtlrect(_mainMenuCache.width() - shownWidth, 0, shownWidth, _mainMenuCache.height(), _mainMenuCache.width());
		p.drawPixmapLeft(0, 0, mainMenuRight, height(), width(), _mainMenuCache, shownRect);
	}
}

void LayerStackWidget::BackgroundWidget::finishAnimation() {
	_a_shown.finish();
	_a_mainMenuShown.finish();
	_a_specialLayerShown.finish();
	_a_layerShown.finish();
	checkIfDone();
}

void LayerStackWidget::BackgroundWidget::animationCallback() {
	update();
	checkIfDone();
}

LayerStackWidget::LayerStackWidget(QWidget *parent) : TWidget(parent)
, _background(this) {
	setGeometry(parentWidget()->rect());
	hide();
	_background->setDoneCallback([this] { animationDone(); });
}

void LayerWidget::setInnerFocus() {
	if (!isAncestorOf(App::wnd()->focusWidget())) {
		doSetInnerFocus();
	}
}

void LayerStackWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		hideCurrent();
	}
}

void LayerStackWidget::mousePressEvent(QMouseEvent *e) {
	hideCurrent();
}

void LayerStackWidget::hideCurrent() {
	return currentLayer() ? hideLayers() : hideAll();
}

void LayerStackWidget::hideLayers() {
	startAnimation([] {}, [this] {
		clearLayers();
	}, Action::HideLayer);
}

void LayerStackWidget::hideAll() {
	startAnimation([] {}, [this] {
		clearLayers();
		_specialLayer.destroyDelayed();
		_mainMenu.destroyDelayed();
	}, Action::HideAll);
}

void LayerStackWidget::setCacheImages() {
	auto bodyCache = QPixmap(), mainMenuCache = QPixmap();
	if (isAncestorOf(App::wnd()->focusWidget())) {
		setFocus();
	}
	if (_mainMenu) {
		hideChildren();
		bodyCache = myGrab(App::wnd()->bodyWidget());
		showChildren();
		mainMenuCache = myGrab(_mainMenu);
	}
	auto specialLayerCache = _specialLayer ? myGrab(_specialLayer) : QPixmap();
	auto layerCache = QPixmap();
	if (auto layer = currentLayer()) {
		layerCache = myGrab(layer);
	}
	setAttribute(Qt::WA_OpaquePaintEvent, !bodyCache.isNull());
	updateLayerBoxes();
	_background->setCacheImages(std_::move(bodyCache), std_::move(mainMenuCache), std_::move(specialLayerCache), std_::move(layerCache));
}

void LayerStackWidget::onLayerClosed(LayerWidget *layer) {
	layer->deleteLater();
	if (layer == _specialLayer) {
		hideAll();
	} else if (layer == currentLayer()) {
		if (_layers.size() == 1) {
			hideCurrent();
		} else {
			layer->hide();
			_layers.pop_back();
			layer = currentLayer();
			layer->parentResized();
			if (!_background->animating()) {
				layer->show();
				showFinished();
			}
		}
	} else {
		for (auto i = _layers.begin(), e = _layers.end(); i != e; ++i) {
			if (layer == *i) {
				_layers.erase(i);
				break;
			}
		}
	}
}

void LayerStackWidget::onLayerResized() {
	updateLayerBoxes();
}

void LayerStackWidget::updateLayerBoxes() {
	auto getLayerBox = [this]() {
		if (auto layer = currentLayer()) {
			return layer->geometry();
		}
		return QRect();
	};
	auto getSpecialLayerBox = [this]() {
		return _specialLayer ? _specialLayer->geometry() : QRect();
	};
	_background->setLayerBoxes(getSpecialLayerBox(), getLayerBox());
	update();
}

void LayerStackWidget::finishAnimation() {
	_background->finishAnimation();
}

bool LayerStackWidget::canSetFocus() const {
	return (currentLayer() || _specialLayer || _mainMenu);
}

void LayerStackWidget::setInnerFocus() {
	if (_background->animating()) {
		setFocus();
	} else if (auto l = currentLayer()) {
		l->setInnerFocus();
	} else if (_specialLayer) {
		_specialLayer->setInnerFocus();
	} else if (_mainMenu) {
		_mainMenu->setInnerFocus();
	}
}

bool LayerStackWidget::contentOverlapped(const QRect &globalRect) {
	if (isHidden()) {
		return false;
	}
	if (_specialLayer && _specialLayer->overlaps(globalRect)) {
		return true;
	}
	if (auto layer = currentLayer()) {
		return layer->overlaps(globalRect);
	}
	return false;
}

template <typename SetupNew, typename ClearOld>
void LayerStackWidget::startAnimation(SetupNew setupNewWidgets, ClearOld clearOldWidgets, Action action) {
	if (App::quitting()) return;

	setupNewWidgets();
	setCacheImages();
	clearOldWidgets();
	prepareForAnimation();
	_background->startAnimation(action);
}

void LayerStackWidget::resizeEvent(QResizeEvent *e) {
	_background->setGeometry(rect());
	if (_specialLayer) {
		_specialLayer->parentResized();
	}
	if (auto layer = currentLayer()) {
		layer->parentResized();
	}
	if (_mainMenu) {
		_mainMenu->resize(_mainMenu->width(), height());
	}
	updateLayerBoxes();
}

void LayerStackWidget::showLayer(LayerWidget *layer) {
	appendLayer(layer);
	while (!_layers.isEmpty() && _layers.front() != layer) {
		auto removingLayer = _layers.front();
		_layers.pop_front();

		removingLayer->hide();
		removingLayer->deleteLater();
	}
}

void LayerStackWidget::prepareForAnimation() {
	if (isHidden()) {
		show();
	}
	if (_mainMenu) {
		_mainMenu->hide();
	}
	if (_specialLayer) {
		_specialLayer->hide();
	}
	if (auto layer = currentLayer()) {
		layer->hide();
	}
}

void LayerStackWidget::animationDone() {
	bool hidden = true;
	if (_mainMenu) {
		_mainMenu->show();
		hidden = false;
	}
	if (_specialLayer) {
		_specialLayer->show();
		hidden = false;
	}
	if (auto layer = currentLayer()) {
		layer->show();
		hidden = false;
	}
	if (hidden) {
		App::wnd()->layerFinishedHide(this);
	} else {
		showFinished();
	}
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void LayerStackWidget::showFinished() {
	fixOrder();
	sendFakeMouseEvent();
	updateLayerBoxes();
	if (_mainMenu) {
		_mainMenu->showFinished();
	}
	if (_specialLayer) {
		_specialLayer->showFinished();
	}
	if (auto layer = currentLayer()) {
		layer->showFinished();
	}
	if (auto window = App::wnd()) {
		window->setInnerFocus();
	}
}

void LayerStackWidget::showSpecialLayer(LayerWidget *layer) {
	startAnimation([this, layer] {
		_specialLayer.destroyDelayed();
		_specialLayer = layer;
		initChildLayer(_specialLayer);
	}, [this] {
		clearLayers();
		_mainMenu.destroyDelayed();
	}, Action::ShowSpecialLayer);
}

void LayerStackWidget::showMainMenu() {
	startAnimation([this] {
		_mainMenu.create(this);
		_mainMenu->setGeometryToLeft(0, 0, _mainMenu->width(), height());
		_mainMenu->setParent(this);
	}, [this] {
		clearLayers();
		_specialLayer.destroyDelayed();
	}, Action::ShowMainMenu);
}

void LayerStackWidget::appendLayer(LayerWidget *layer) {
	auto oldLayer = currentLayer();
	if (oldLayer) {
		oldLayer->hide();
	}
	_layers.push_back(layer);
	initChildLayer(layer);

	if (_layers.size() > 1) {
		if (!_background->animating()) {
			layer->show();
			showFinished();
		}
	} else {
		startAnimation([] {}, [this] {
			_mainMenu.destroyDelayed();
		}, Action::ShowLayer);
	}
}

void LayerStackWidget::prependLayer(LayerWidget *layer) {
	if (_layers.empty()) {
		return showLayer(layer);
	}
	layer->hide();
	_layers.push_front(layer);
	initChildLayer(layer);
}

void LayerStackWidget::clearLayers() {
	for (auto layer : base::take(_layers)) {
		layer->hide();
		layer->deleteLater();
	}
}

void LayerStackWidget::initChildLayer(LayerWidget *layer) {
	layer->setParent(this);
	connect(layer, SIGNAL(closed(LayerWidget*)), this, SLOT(onLayerClosed(LayerWidget*)));
	connect(layer, SIGNAL(resized()), this, SLOT(onLayerResized()));
	connect(layer, SIGNAL(destroyed(QObject*)), this, SLOT(onLayerDestroyed(QObject*)));
	layer->parentResized();
}

void LayerStackWidget::fixOrder() {
	if (auto layer = currentLayer()) {
		_background->raise();
		layer->raise();
	} else if (_specialLayer) {
		_specialLayer->raise();
	}
	if (_mainMenu) {
		_mainMenu->raise();
	}
}

void LayerStackWidget::sendFakeMouseEvent() {
	sendSynteticMouseEvent(this, QEvent::MouseMove, Qt::NoButton);
}

void LayerStackWidget::onLayerDestroyed(QObject *obj) {
	if (obj == _specialLayer) {
		_specialLayer = nullptr;
		hideAll();
	} else if (obj == currentLayer()) {
		_layers.pop_back();
		if (auto newLayer = currentLayer()) {
			newLayer->parentResized();
			if (!_background->animating()) {
				newLayer->show();
				showFinished();
			}
		} else if (!_specialLayer) {
			hideAll();
		}
	} else {
		for (auto i = _layers.begin(), e = _layers.end(); i != e; ++i) {
			if (obj == *i) {
				_layers.erase(i);
				break;
			}
		}
	}
}

LayerStackWidget::~LayerStackWidget() {
	if (App::wnd()) App::wnd()->noLayerStack(this);
}

MediaPreviewWidget::MediaPreviewWidget(QWidget *parent) : TWidget(parent)
, _emojiSize(EmojiSizes[EIndex + 1] / cIntRetinaFactor()) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	subscribe(FileDownload::ImageLoaded(), [this] { update(); });
}

void MediaPreviewWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r(e->rect());

	auto image = currentImage();
	int w = image.width() / cIntRetinaFactor(), h = image.height() / cIntRetinaFactor();
	auto shown = _a_shown.current(getms(), _hiding ? 0. : 1.);
	if (!_a_shown.animating()) {
		if (_hiding) {
			hide();
			return;
		}
	} else {
		p.setOpacity(shown);
//		w = qMax(qRound(w * (st::stickerPreviewMin + ((1. - st::stickerPreviewMin) * shown)) / 2.) * 2 + int(w % 2), 1);
//		h = qMax(qRound(h * (st::stickerPreviewMin + ((1. - st::stickerPreviewMin) * shown)) / 2.) * 2 + int(h % 2), 1);
	}
	p.fillRect(r, st::stickerPreviewBg);
	p.drawPixmap((width() - w) / 2, (height() - h) / 2, image);
	if (!_emojiList.isEmpty()) {
		int emojiCount = _emojiList.size();
		int emojiWidth = (emojiCount * _emojiSize) + (emojiCount - 1) * st::stickerEmojiSkip;
		int emojiLeft = (width() - emojiWidth) / 2;
		int esize = EmojiSizes[EIndex + 1];
		for_const (auto emoji, _emojiList) {
			p.drawPixmapLeft(emojiLeft, (height() - h) / 2 - (_emojiSize * 2), width(), App::emojiLarge(), QRect(emoji->x * esize, emoji->y * esize, esize, esize));
			emojiLeft += _emojiSize + st::stickerEmojiSkip;
		}
	}
}

void MediaPreviewWidget::resizeEvent(QResizeEvent *e) {
	update();
}

void MediaPreviewWidget::showPreview(DocumentData *document) {
	if (!document || (!document->isAnimation() && !document->sticker())) {
		hidePreview();
		return;
	}

	startShow();
	_photo = nullptr;
	_document = document;
	fillEmojiString();
	resetGifAndCache();
}

void MediaPreviewWidget::showPreview(PhotoData *photo) {
	if (!photo || photo->full->isNull()) {
		hidePreview();
		return;
	}

	startShow();
	_photo = photo;
	_document = nullptr;
	fillEmojiString();
	resetGifAndCache();
}

void MediaPreviewWidget::startShow() {
	_cache = QPixmap();
	if (isHidden() || _a_shown.animating()) {
		if (isHidden()) show();
		_hiding = false;
		_a_shown.start([this] { update(); }, 0., 1., st::stickerPreviewDuration);
	} else {
		update();
	}
}

void MediaPreviewWidget::hidePreview() {
	if (isHidden()) {
		return;
	}
	if (_gif) _cache = currentImage();
	_hiding = true;
	_a_shown.start([this] { update(); }, 1., 0., st::stickerPreviewDuration);
	_photo = nullptr;
	_document = nullptr;
	resetGifAndCache();
}

void MediaPreviewWidget::fillEmojiString() {
	auto getStickerEmojiList = [this](uint64 setId) {
		QList<EmojiPtr> result;
		auto &sets = Global::StickerSets();
		auto it = sets.constFind(setId);
		if (it == sets.cend()) {
			return result;
		}
		for (auto i = it->emoji.cbegin(), e = it->emoji.cend(); i != e; ++i) {
			for_const (auto document, *i) {
				if (document == _document) {
					result.append(i.key());
					if (result.size() >= kStickerPreviewEmojiLimit) {
						return result;
					}
				}
			}
		}
		return result;
	};

	if (_photo) {
		_emojiList.clear();
	} else if (auto sticker = _document->sticker()) {
		auto &inputSet = sticker->set;
		if (inputSet.type() == mtpc_inputStickerSetID) {
			_emojiList = getStickerEmojiList(inputSet.c_inputStickerSetID().vid.v);
		} else {
			_emojiList.clear();
			if (auto emoji = emojiFromText(sticker->alt)) {
				_emojiList.append(emoji);
			}
		}
	} else {
		_emojiList.clear();
	}
}

void MediaPreviewWidget::resetGifAndCache() {
	_gif.reset();
	_cacheStatus = CacheNotLoaded;
	_cachedSize = QSize();
}

QSize MediaPreviewWidget::currentDimensions() const {
	if (!_cachedSize.isEmpty()) {
		return _cachedSize;
	}
	if (!_document && !_photo) {
		_cachedSize = QSize(_cache.width() / cIntRetinaFactor(), _cache.height() / cIntRetinaFactor());
		return _cachedSize;
	}

	QSize result, box;
	if (_photo) {
		result = QSize(_photo->full->width(), _photo->full->height());
		box = QSize(width() - 2 * st::boxVerticalMargin, height() - 2 * st::boxVerticalMargin);
	} else {
		result = _document->dimensions;
		if (_gif && _gif->ready()) {
			result = QSize(_gif->width(), _gif->height());
		}
		if (_document->sticker()) {
			box = QSize(st::maxStickerSize, st::maxStickerSize);
		} else {
			box = QSize(2 * st::maxStickerSize, 2 * st::maxStickerSize);
		}
	}
	result = QSize(qMax(convertScale(result.width()), 1), qMax(convertScale(result.height()), 1));
	if (result.width() > box.width()) {
		result.setHeight(qMax((box.width() * result.height()) / result.width(), 1));
		result.setWidth(box.width());
	}
	if (result.height() > box.height()) {
		result.setWidth(qMax((box.height() * result.width()) / result.height(), 1));
		result.setHeight(box.height());
	}
	if (_photo) {
		_cachedSize = result;
	}
	return result;
}

QPixmap MediaPreviewWidget::currentImage() const {
	if (_document) {
		if (_document->sticker()) {
			if (_cacheStatus != CacheLoaded) {
				_document->checkSticker();
				if (_document->sticker()->img->isNull()) {
					if (_cacheStatus != CacheThumbLoaded && _document->thumb->loaded()) {
						QSize s = currentDimensions();
						_cache = _document->thumb->pixBlurred(s.width(), s.height());
						_cacheStatus = CacheThumbLoaded;
					}
				} else {
					QSize s = currentDimensions();
					_cache = _document->sticker()->img->pix(s.width(), s.height());
					_cacheStatus = CacheLoaded;
				}
			}
		} else {
			_document->automaticLoad(nullptr);
			if (_document->loaded()) {
				if (!_gif && !_gif.isBad()) {
					auto that = const_cast<MediaPreviewWidget*>(this);
					that->_gif = Media::Clip::MakeReader(_document->location(), _document->data(), [this, that](Media::Clip::Notification notification) {
						that->clipCallback(notification);
					});
					if (_gif) _gif->setAutoplay();
				}
			}
			if (_gif && _gif->started()) {
				QSize s = currentDimensions();
				return _gif->current(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, ImageRoundCorner::None, getms());
			}
			if (_cacheStatus != CacheThumbLoaded && _document->thumb->loaded()) {
				QSize s = currentDimensions();
				_cache = _document->thumb->pixBlurred(s.width(), s.height());
				_cacheStatus = CacheThumbLoaded;
			}
		}
	} else if (_photo) {
		if (_cacheStatus != CacheLoaded) {
			if (_photo->full->loaded()) {
				QSize s = currentDimensions();
				_cache = _photo->full->pix(s.width(), s.height());
				_cacheStatus = CacheLoaded;
			} else {
				if (_cacheStatus != CacheThumbLoaded && _photo->thumb->loaded()) {
					QSize s = currentDimensions();
					_cache = _photo->thumb->pixBlurred(s.width(), s.height());
					_cacheStatus = CacheThumbLoaded;
				}
				_photo->thumb->load();
				_photo->full->load();
			}
		}

	}
	return _cache;
}

void MediaPreviewWidget::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gif && _gif->state() == State::Error) {
			_gif.setBad();
		}

		if (_gif && _gif->ready() && !_gif->started()) {
			QSize s = currentDimensions();
			_gif->start(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, ImageRoundCorner::None);
		}

		update();
	} break;

	case NotificationRepaint: {
		if (_gif && !_gif->currentDisplayed()) {
			update();
		}
	} break;
	}
}

MediaPreviewWidget::~MediaPreviewWidget() {
}
