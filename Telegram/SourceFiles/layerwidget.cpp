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

void LayerWidget::setInnerFocus() {
	auto focused = App::wnd()->focusWidget();
	if (!isAncestorOf(focused)) {
		doSetInnerFocus();
	}
}

class LayerStackWidget::BackgroundWidget : public TWidget {
public:
	BackgroundWidget(QWidget *parent) : TWidget(parent)
	, _shadow(st::boxShadow) {
	}

	void setLayerBox(const QRect &box, const QRect &hiddenSpecialBox) {
		_box = box;
		_hiddenSpecialBox = hiddenSpecialBox;
		update();
	}
	void setOpacity(float64 opacity) {
		_opacity = opacity;
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		Painter p(this);

		p.setOpacity(st::layerAlpha * _opacity);
		if (_box.isNull()) {
			p.fillRect(rect(), st::layerBg);
		} else {
			auto clip = QRegion(rect()) - _box;
			for (auto &r : clip.rects()) {
				p.fillRect(r, st::layerBg);
			}
			p.setClipRegion(clip);
			p.setOpacity(_opacity);
			_shadow.paint(p, _box, st::boxShadowShift);
			if (!_hiddenSpecialBox.isNull()) {
				p.setClipRegion(QRegion(rect()) - _hiddenSpecialBox);
				_shadow.paint(p, _hiddenSpecialBox, st::boxShadowShift);
			}
		}
	}

private:
	QRect _box, _hiddenSpecialBox;
	float64 _opacity = 0.;

	BoxShadow _shadow;

};

LayerStackWidget::LayerStackWidget(QWidget *parent) : TWidget(parent)
, _background(this)
, a_bg(0)
, a_layer(0)
, _a_background(animation(this, &LayerStackWidget::step_background)) {
	setGeometry(parentWidget()->rect());
	hide();
}

void LayerStackWidget::paintEvent(QPaintEvent *e) {
	if (!layer() && !_specialLayer && _layerCache.isNull()) {
		return;
	}

	if (!_layerCache.isNull()) {
		Painter p(this);
		p.setClipRect(rect());
		p.setOpacity(a_layer.current());
		if (!_hiddenSpecialLayerCache.isNull()) {
			p.drawPixmap(_hiddenSpecialLayerCacheBox.topLeft(), _hiddenSpecialLayerCache);
		}
		p.drawPixmap(_layerCacheBox.topLeft(), _layerCache);
	}
}

void LayerStackWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		onCloseCurrent();
	}
}

void LayerStackWidget::mousePressEvent(QMouseEvent *e) {
	onCloseCurrent();
}

void LayerStackWidget::onCloseCurrent() {
	if (layer()) {
		onCloseLayers();
	} else {
		onClose();
	}
}

void LayerStackWidget::onCloseLayers() {
	if (_specialLayer) {
		clearLayers();
		fixOrder();
		if (App::wnd()) App::wnd()->setInnerFocus();
	} else {
		onClose();
	}
}

void LayerStackWidget::onClose() {
	startHide();
}

void LayerStackWidget::onLayerClosed(LayerWidget *l) {
	l->deleteLater();
	if (l == _specialLayer) {
		onClose();
		_specialLayer = nullptr;
	} else if (l == layer()) {
		_layers.pop_back();
		if (auto newLayer = layer()) {
			l->hide();
			newLayer->parentResized();
			if (!_a_background.animating()) {
				newLayer->show();
			}
		} else if (_specialLayer) {
			l->hide();
		} else {
			_layers.push_back(l); // For animation cache grab.
			onClose();
			_layers.pop_back();
		}
		fixOrder();
		if (App::wnd()) App::wnd()->setInnerFocus();
		updateLayerBox();
		sendFakeMouseEvent();
	} else {
		for (auto i = _layers.begin(), e = _layers.end(); i != e; ++i) {
			if (l == *i) {
				_layers.erase(i);
				break;
			}
		}
	}
}

void LayerStackWidget::onLayerResized() {
	updateLayerBox();
}

void LayerStackWidget::updateLayerBox() {
	auto getLayerBox = [this]() {
		if (!_layerCache.isNull()) {
			return _layerCacheBox;
		} else if (auto l = layer()) {
			return l->geometry();
		} else if (_specialLayer) {
			return _specialLayer->geometry();
		}
		return QRect();
	};
	auto getSpecialLayerBox = [this]() {
		if (!_layerCache.isNull()) {
			return _hiddenSpecialLayerCacheBox;
		} else if (auto l = layer()) {
			return _specialLayer ? _specialLayer->geometry() : QRect();
		}
		return QRect();
	};
	_background->setLayerBox(getLayerBox(), getSpecialLayerBox());
	update();
}

void LayerStackWidget::startShow() {
	startAnimation(1);
	show();
}

void LayerStackWidget::showFast() {
	if (_a_background.animating()) {
		_a_background.step(getms() + st::layerSlideDuration + 1);
	}
}

void LayerStackWidget::startHide() {
	if (isHidden() || _hiding) {
		return;
	}

	_hiding = true;
	startAnimation(0);
}

void LayerStackWidget::startAnimation(float64 toOpacity) {
	if (App::app()) App::app()->mtpPause();
	a_bg.start(toOpacity);
	a_layer.start(toOpacity);
	_a_background.start();
	if (_layerCache.isNull()) {
		if (auto cacheLayer = layer() ? layer() : _specialLayer.ptr()) {
			_layerCache = myGrab(cacheLayer);
			_layerCacheBox = cacheLayer->geometry();
			if (layer() && _specialLayer) {
				_hiddenSpecialLayerCache = myGrab(_specialLayer);
				_hiddenSpecialLayerCacheBox = _specialLayer->geometry();
			}
		}
	}
	if (_specialLayer) {
		_specialLayer->hide();
	}
	if (auto l = layer()) {
		l->hide();
	}
	updateLayerBox();
	if (App::wnd()) App::wnd()->setInnerFocus();
}

bool LayerStackWidget::canSetFocus() const {
	return (layer() || _specialLayer) && !_hiding;
}

void LayerStackWidget::setInnerFocus() {
	if (_a_background.animating()) {
		setFocus();
	} else if (auto l = layer()) {
		l->setInnerFocus();
	} else if (_specialLayer) {
		_specialLayer->setInnerFocus();
	}
}

bool LayerStackWidget::contentOverlapped(const QRect &globalRect) {
	if (isHidden()) {
		return false;
	}
	if (_specialLayer && _specialLayer->overlaps(globalRect)) {
		return true;
	}
	if (auto l = layer()) {
		return l->overlaps(globalRect);
	}
	return false;
}

void LayerStackWidget::resizeEvent(QResizeEvent *e) {
	_background->setGeometry(rect());
	if (_specialLayer) {
		_specialLayer->parentResized();
	}
	if (auto l = layer()) {
		l->parentResized();
	}
	updateLayerBox();
}

void LayerStackWidget::showLayer(LayerWidget *l) {
	clearLayers();
	appendLayer(l);
}

void LayerStackWidget::showSpecialLayer(LayerWidget *l) {
	clearLayers();
	if (_specialLayer) {
		_specialLayer->hide();
		_specialLayer->deleteLater();
	}
	_specialLayer = l;
	activateLayer(l);
}

void LayerStackWidget::appendLayer(LayerWidget *l) {
	if (auto oldLayer = layer()) {
		oldLayer->hide();
	}
	_layers.push_back(l);
	activateLayer(l);
}

void LayerStackWidget::prependLayer(LayerWidget *l) {
	if (_layers.empty()) {
		showLayer(l);
	} else {
		l->hide();
		_layers.push_front(l);
		initChildLayer(l);
	}
}

void LayerStackWidget::clearLayers() {
	for_const (auto oldLayer, _layers) {
		oldLayer->hide();
		oldLayer->deleteLater();
	}
	_layers.clear();
	updateLayerBox();
	sendFakeMouseEvent();
}

void LayerStackWidget::initChildLayer(LayerWidget *l) {
	l->setParent(this);
	connect(l, SIGNAL(closed(LayerWidget*)), this, SLOT(onLayerClosed(LayerWidget*)));
	connect(l, SIGNAL(resized()), this, SLOT(onLayerResized()));
	connect(l, SIGNAL(destroyed(QObject*)), this, SLOT(onLayerDestroyed(QObject*)));
	l->parentResized();
	fixOrder();
}

void LayerStackWidget::activateLayer(LayerWidget *l) {
	initChildLayer(l);
	if (isHidden()) {
		startShow();
	} else {
		l->show();
		if (App::wnd()) App::wnd()->setInnerFocus();
		updateLayerBox();
	}
	fixOrder();
	sendFakeMouseEvent();
}

void LayerStackWidget::fixOrder() {
	if (auto l = layer()) {
		_background->raise();
		l->raise();
	} else if (_specialLayer) {
		_specialLayer->raise();
	}
}

void LayerStackWidget::sendFakeMouseEvent() {
	sendSynteticMouseEvent(this, QEvent::MouseMove, Qt::NoButton);
}

void LayerStackWidget::step_background(float64 ms, bool timer) {
	float64 dt = ms / (_hiding ? st::layerHideDuration : st::layerSlideDuration);
	if (dt >= 1) {
		a_bg.finish();
		a_layer.finish();
		_a_background.stop();
		_layerCache = _hiddenSpecialLayerCache = QPixmap();
		if (_hiding) {
			App::wnd()->layerFinishedHide(this);
		} else {
			if (_specialLayer) {
				_specialLayer->show();
				_specialLayer->showDone();
			}
			if (auto l = layer()) {
				l->show();
				l->showDone();
			}
			fixOrder();
			if (App::wnd()) App::wnd()->setInnerFocus();
		}
		updateLayerBox();
		if (App::app()) App::app()->mtpUnpause();
	} else {
		a_bg.update(dt, anim::easeOutCirc);
		a_layer.update(dt, anim::linear);
	}
	_background->setOpacity(a_bg.current());
	if (timer) {
		_background->update();
		update();
	}
}

void LayerStackWidget::onLayerDestroyed(QObject *obj) {
	if (obj == _specialLayer) {
		_specialLayer = nullptr;
		onClose();
	} else if (obj == layer()) {
		_layers.pop_back();
		if (auto newLayer = layer()) {
			newLayer->parentResized();
			if (!_a_background.animating()) {
				newLayer->show();
			}
		} else if (!_specialLayer) {
			onClose();
		}
		fixOrder();
		if (App::wnd()) App::wnd()->setInnerFocus();
		updateLayerBox();
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
, a_shown(0, 0)
, _a_shown(animation(this, &MediaPreviewWidget::step_shown)) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(update()));
}

void MediaPreviewWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r(e->rect());

	const QPixmap &draw(currentImage());
	int w = draw.width() / cIntRetinaFactor(), h = draw.height() / cIntRetinaFactor();
	if (_a_shown.animating()) {
		float64 shown = a_shown.current();
		p.setOpacity(shown);
//		w = qMax(qRound(w * (st::stickerPreviewMin + ((1. - st::stickerPreviewMin) * shown)) / 2.) * 2 + int(w % 2), 1);
//		h = qMax(qRound(h * (st::stickerPreviewMin + ((1. - st::stickerPreviewMin) * shown)) / 2.) * 2 + int(h % 2), 1);
	}
	p.fillRect(r, st::stickerPreviewBg);
	p.drawPixmap((width() - w) / 2, (height() - h) / 2, draw);
}

void MediaPreviewWidget::resizeEvent(QResizeEvent *e) {
	update();
}

void MediaPreviewWidget::step_shown(float64 ms, bool timer) {
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

void MediaPreviewWidget::showPreview(DocumentData *document) {
	if (!document || (!document->isAnimation() && !document->sticker())) {
		hidePreview();
		return;
	}

	startShow();
	_photo = nullptr;
	_document = document;
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
	resetGifAndCache();
}

void MediaPreviewWidget::startShow() {
	_cache = QPixmap();
	if (isHidden() || _a_shown.animating()) {
		if (isHidden()) show();
		a_shown.start(1);
		_a_shown.start();
	} else {
		update();
	}
}

void MediaPreviewWidget::hidePreview() {
	if (isHidden()) {
		return;
	}
	if (_gif) _cache = currentImage();
	a_shown.start(0);
	_a_shown.start();
	_photo = nullptr;
	_document = nullptr;
	resetGifAndCache();
}

void MediaPreviewWidget::resetGifAndCache() {
	if (_gif) {
		if (gif()) {
			delete _gif;
		}
		_gif = nullptr;
	}
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
		if (gif() && _gif->ready()) {
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
				if (!_gif && _gif != Media::Clip::BadReader) {
					auto that = const_cast<MediaPreviewWidget*>(this);
					that->_gif = new Media::Clip::Reader(_document->location(), _document->data(), func(that, &MediaPreviewWidget::clipCallback));
					if (gif()) _gif->setAutoplay();
				}
			}
			if (gif() && _gif->started()) {
				QSize s = currentDimensions();
				return _gif->current(s.width(), s.height(), s.width(), s.height(), getms());
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
				LOG(("DIMENSIONS: %1 %2").arg(s.width()).arg(s.height()));
				_cache = _photo->full->pix(s.width(), s.height());
				_cacheStatus = CacheLoaded;
			} else {
				if (_cacheStatus != CacheThumbLoaded && _photo->thumb->loaded()) {
					QSize s = currentDimensions();
					LOG(("DIMENSIONS: %1 %2").arg(s.width()).arg(s.height()));
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
		if (gif() && _gif->state() == State::Error) {
			delete _gif;
			_gif = BadReader;
		}

		if (gif() && _gif->ready() && !_gif->started()) {
			QSize s = currentDimensions();
			_gif->start(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None);
		}

		update();
	} break;

	case NotificationRepaint: {
		if (gif() && !_gif->currentDisplayed()) {
			update();
		}
	} break;
	}
}

MediaPreviewWidget::~MediaPreviewWidget() {
}
