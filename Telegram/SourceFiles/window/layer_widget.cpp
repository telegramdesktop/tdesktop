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
#include "window/layer_widget.h"

#include "lang/lang_keys.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "media/media_clip_reader.h"
#include "boxes/abstract_box.h"
#include "application.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "core/file_utilities.h"
#include "styles/style_boxes.h"
#include "styles/style_widgets.h"
#include "styles/style_chat_helpers.h"
#include "ui/widgets/shadow.h"
#include "window/window_main_menu.h"
#include "auth_session.h"
#include "chat_helpers/stickers.h"
#include "window/window_controller.h"

namespace {

constexpr int kStickerPreviewEmojiLimit = 10;

} // namespace

namespace Window {

class LayerStackWidget::BackgroundWidget : public TWidget {
public:
	BackgroundWidget(QWidget *parent) : TWidget(parent) {
	}

	void setDoneCallback(base::lambda<void()> callback) {
		_doneCallback = std::move(callback);
	}

	void setLayerBoxes(const QRect &specialLayerBox, const QRect &layerBox);
	void setCacheImages(QPixmap &&bodyCache, QPixmap &&mainMenuCache, QPixmap &&specialLayerCache, QPixmap &&layerCache);
	void startAnimation(Action action);
	void skipAnimation(Action action);
	void finishAnimating();

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
	int _mainMenuCacheWidth = 0;
	QPixmap _specialLayerCache;
	QPixmap _layerCache;

	base::lambda<void()> _doneCallback;

	bool _wasAnimating = false;
	bool _inPaintEvent = false;
	Animation _a_shown;
	Animation _a_mainMenuShown;
	Animation _a_specialLayerShown;
	Animation _a_layerShown;

	QRect _specialLayerBox, _specialLayerCacheBox;
	QRect _layerBox, _layerCacheBox;
	int _mainMenuRight = 0;

	bool _mainMenuShown = false;
	bool _specialLayerShown = false;
	bool _layerShown = false;

};

void LayerStackWidget::BackgroundWidget::setCacheImages(QPixmap &&bodyCache, QPixmap &&mainMenuCache, QPixmap &&specialLayerCache, QPixmap &&layerCache) {
	_bodyCache = std::move(bodyCache);
	_mainMenuCache = std::move(mainMenuCache);
	_specialLayerCache = std::move(specialLayerCache);
	_layerCache = std::move(layerCache);
	_specialLayerCacheBox = _specialLayerBox;
	_layerCacheBox = _layerBox;
	setAttribute(Qt::WA_OpaquePaintEvent, !_bodyCache.isNull());
}

void LayerStackWidget::BackgroundWidget::startAnimation(Action action) {
	if (action == Action::ShowMainMenu) {
		setMainMenuShown(true);
	} else if (action != Action::HideLayer
		&& action != Action::HideSpecialLayer) {
		setMainMenuShown(false);
	}
	if (action == Action::ShowSpecialLayer) {
		setSpecialLayerShown(true);
	} else if (action == Action::ShowMainMenu
		|| action == Action::HideAll
		|| action == Action::HideSpecialLayer) {
		setSpecialLayerShown(false);
	}
	if (action == Action::ShowLayer) {
		setLayerShown(true);
	} else if (action != Action::ShowSpecialLayer
		&& action != Action::HideSpecialLayer) {
		setLayerShown(false);
	}
	_wasAnimating = true;
	checkIfDone();
}

void LayerStackWidget::BackgroundWidget::skipAnimation(Action action) {
	startAnimation(action);
	finishAnimating();
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
	_mainMenuCacheWidth = (_mainMenuCache.width() / cIntRetinaFactor()) - st::boxRoundShadow.extend.right();
	_mainMenuRight = _mainMenuShown ? _mainMenuCacheWidth : 0;
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
	auto guard = gsl::finally([this] {
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
	auto mainMenuRight = (_mainMenuCache.isNull() || mainMenuProgress < 0) ? _mainMenuRight : (mainMenuProgress < 0) ? _mainMenuRight : anim::interpolate(0, _mainMenuCacheWidth, mainMenuProgress);
	if (mainMenuRight) {
		// Move showing boxes to the right while main menu is hiding.
		if (!_specialLayerCache.isNull()) {
			specialLayerBox.moveLeft(specialLayerBox.left() + mainMenuRight / 2);
		}
		if (!_layerCache.isNull()) {
			layerBox.moveLeft(layerBox.left() + mainMenuRight / 2);
		}
	}
	auto bgOpacity = _a_shown.current(ms, isShown() ? 1. : 0.);
	auto specialLayerOpacity = _a_specialLayerShown.current(ms, _specialLayerShown ? 1. : 0.);
	auto layerOpacity = _a_layerShown.current(ms, _layerShown ? 1. : 0.);
	if (bgOpacity == 0.) {
		return;
	}

	p.setOpacity(bgOpacity);
	auto overSpecialOpacity = (layerOpacity * specialLayerOpacity);
	auto bg = myrtlrect(mainMenuRight, 0, width() - mainMenuRight, height());

	if (_mainMenuCache.isNull() && mainMenuRight > 0) {
		// All cache images are taken together with their shadows,
		// so we paint shadow only when there is no cache.
		Ui::Shadow::paint(p, myrtlrect(0, 0, mainMenuRight, height()), width(), st::boxRoundShadow, RectPart::Right);
	}

	if (_specialLayerCache.isNull() && !specialLayerBox.isEmpty()) {
		// All cache images are taken together with their shadows,
		// so we paint shadow only when there is no cache.
		auto sides = RectPart::Left | RectPart::Right;
		auto topCorners = (specialLayerBox.y() > 0);
		auto bottomCorners = (specialLayerBox.y() + specialLayerBox.height() < height());
		if (topCorners) {
			sides |= RectPart::Top;
		}
		if (bottomCorners) {
			sides |= RectPart::Bottom;
		}
		if (topCorners || bottomCorners) {
			p.setClipRegion(QRegion(rect()) - specialLayerBox.marginsRemoved(QMargins(st::boxRadius, 0, st::boxRadius, 0)) - specialLayerBox.marginsRemoved(QMargins(0, st::boxRadius, 0, st::boxRadius)));
		}
		Ui::Shadow::paint(p, specialLayerBox, width(), st::boxRoundShadow, sides);

		if (topCorners || bottomCorners) {
			// In case of painting the shadow above the special layer we get
			// glitches in the corners, so we need to paint the corners once more.
			p.setClipping(false);
			auto parts = (topCorners ? (RectPart::TopLeft | RectPart::TopRight) : RectPart::None)
				| (bottomCorners ? (RectPart::BottomLeft | RectPart::BottomRight) : RectPart::None);
			App::roundRect(p, specialLayerBox, st::boxBg, BoxCorners, nullptr, parts);
		}
	}

	if (!layerBox.isEmpty() && !_specialLayerCache.isNull() && overSpecialOpacity < bgOpacity) {
		// In case of moving special layer below the background while showing a box
		// we need to fill special layer rect below its cache with a complex opacity
		// (alpha_final - alpha_current) / (1 - alpha_current) so we won't get glitches
		// in the transparent special layer cache corners after filling special layer
		// rect above its cache with alpha_current opacity.
		auto region = QRegion(bg) - specialLayerBox;
		for (auto rect : region.rects()) {
			p.fillRect(rect, st::layerBg);
		}
		p.setOpacity((bgOpacity - overSpecialOpacity) / (1. - (overSpecialOpacity * st::layerBg->c.alphaF())));
		p.fillRect(specialLayerBox, st::layerBg);
		p.setOpacity(bgOpacity);
	} else {
		p.fillRect(bg, st::layerBg);
	}

	if (!_specialLayerCache.isNull() && specialLayerOpacity > 0) {
		p.setOpacity(specialLayerOpacity);
		auto cacheLeft = specialLayerBox.x() - st::boxRoundShadow.extend.left();
		auto cacheTop = specialLayerBox.y() - (specialLayerBox.y() > 0 ? st::boxRoundShadow.extend.top() : 0);
		p.drawPixmapLeft(cacheLeft, cacheTop, width(), _specialLayerCache);
	}
	if (!layerBox.isEmpty()) {
		if (!_specialLayerCache.isNull()) {
			p.setOpacity(overSpecialOpacity);
			p.fillRect(specialLayerBox, st::layerBg);
		}
		if (_layerCache.isNull()) {
			p.setOpacity(layerOpacity);
			Ui::Shadow::paint(p, layerBox, width(), st::boxRoundShadow);
		}
	}
	if (!_layerCache.isNull() && layerOpacity > 0) {
		p.setOpacity(layerOpacity);
		p.drawPixmapLeft(layerBox.topLeft() - QPoint(st::boxRoundShadow.extend.left(), st::boxRoundShadow.extend.top()), width(), _layerCache);
	}
	if (!_mainMenuCache.isNull() && mainMenuRight > 0) {
		p.setOpacity(1.);
		auto shownWidth = mainMenuRight + st::boxRoundShadow.extend.right();
		auto sourceWidth = shownWidth * cIntRetinaFactor();
		auto sourceRect = rtlrect(_mainMenuCache.width() - sourceWidth, 0, sourceWidth, _mainMenuCache.height(), _mainMenuCache.width());
		p.drawPixmapLeft(0, 0, shownWidth, height(), width(), _mainMenuCache, sourceRect);
	}
}

void LayerStackWidget::BackgroundWidget::finishAnimating() {
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

LayerStackWidget::LayerStackWidget(
	QWidget *parent,
	Controller *controller)
: TWidget(parent)
, _controller(controller)
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

bool LayerWidget::overlaps(const QRect &globalRect) {
	if (isHidden()) {
		return false;
	}
	auto testRect = QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size());
	if (testAttribute(Qt::WA_OpaquePaintEvent)) {
		return rect().contains(testRect);
	}
	if (QRect(0, st::boxRadius, width(), height() - 2 * st::boxRadius).contains(testRect)) {
		return true;
	}
	if (QRect(st::boxRadius, 0, width() - 2 * st::boxRadius, height()).contains(testRect)) {
		return true;
	}
	return false;
}

void LayerStackWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		hideCurrent(anim::type::normal);
	}
}

void LayerStackWidget::mousePressEvent(QMouseEvent *e) {
	hideCurrent(anim::type::normal);
}

void LayerStackWidget::hideCurrent(anim::type animated) {
	return currentLayer() ? hideLayers(animated) : hideAll(animated);
}

void LayerStackWidget::hideLayers(anim::type animated) {
	startAnimation([] {}, [this] {
		clearLayers();
	}, Action::HideLayer, animated);
}

void LayerStackWidget::hideAll(anim::type animated) {
	startAnimation([] {}, [this] {
		clearLayers();
		clearSpecialLayer();
		_mainMenu.destroyDelayed();
	}, Action::HideAll, animated);
}

void LayerStackWidget::hideTopLayer(anim::type animated) {
	if (_specialLayer) {
		hideLayers(animated);
	} else {
		hideAll(animated);
	}
}

bool LayerStackWidget::layerShown() const {
	return _specialLayer || currentLayer();
}

void LayerStackWidget::setCacheImages() {
	auto bodyCache = QPixmap(), mainMenuCache = QPixmap();
	auto specialLayerCache = QPixmap();
	if (_specialLayer) {
		myEnsureResized(_specialLayer);
		auto sides = RectPart::Left | RectPart::Right;
		if (_specialLayer->y() > 0) {
			sides |= RectPart::Top;
		}
		if (_specialLayer->y() + _specialLayer->height() < height()) {
			sides |= RectPart::Bottom;
		}
		specialLayerCache = Ui::Shadow::grab(_specialLayer, st::boxRoundShadow, sides);
	}
	auto layerCache = QPixmap();
	if (auto layer = currentLayer()) {
		layerCache = Ui::Shadow::grab(layer, st::boxRoundShadow);
	}
	if (isAncestorOf(App::wnd()->focusWidget())) {
		setFocus();
	}
	if (_mainMenu) {
		setAttribute(Qt::WA_OpaquePaintEvent, false);
		hideChildren();
		bodyCache = myGrab(App::wnd()->bodyWidget());
		showChildren();
		mainMenuCache = Ui::Shadow::grab(_mainMenu, st::boxRoundShadow, RectPart::Right);
	}
	setAttribute(Qt::WA_OpaquePaintEvent, !bodyCache.isNull());
	updateLayerBoxes();
	_background->setCacheImages(std::move(bodyCache), std::move(mainMenuCache), std::move(specialLayerCache), std::move(layerCache));
}

void LayerStackWidget::onLayerClosed(LayerWidget *layer) {
	if (!layer->setClosing()) {
		// This layer is already closing.
		return;
	}
	layer->deleteLater();
	if (layer == _specialLayer) {
		hideAll(anim::type::normal);
	} else if (layer == currentLayer()) {
		if (_layers.size() == 1) {
			hideCurrent(anim::type::normal);
		} else {
			if (layer->inFocusChain()) setFocus();
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

void LayerStackWidget::finishAnimating() {
	_background->finishAnimating();
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
void LayerStackWidget::startAnimation(
		SetupNew setupNewWidgets,
		ClearOld clearOldWidgets,
		Action action,
		anim::type animated) {
	if (App::quitting()) return;

	if (animated == anim::type::instant) {
		setupNewWidgets();
		clearOldWidgets();
		prepareForAnimation();
		_background->skipAnimation(action);
	} else {
		setupNewWidgets();
		setCacheImages();
		clearOldWidgets();
		prepareForAnimation();
		_background->startAnimation(action);
	}
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

void LayerStackWidget::showBox(
		object_ptr<BoxContent> box,
		anim::type animated) {
	auto pointer = pushBox(std::move(box), animated);
	while (!_layers.isEmpty() && _layers.front() != pointer) {
		auto removingLayer = _layers.front();
		_layers.pop_front();

		removingLayer->setClosing();
		if (removingLayer->inFocusChain()) setFocus();
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

void LayerStackWidget::showSpecialLayer(
		object_ptr<LayerWidget> layer,
		anim::type animated) {
	startAnimation([this, layer = std::move(layer)]() mutable {
		_specialLayer.destroyDelayed();
		_specialLayer = std::move(layer);
		initChildLayer(_specialLayer);
	}, [this] {
		_mainMenu.destroyDelayed();
	}, Action::ShowSpecialLayer, animated);
}

bool LayerStackWidget::showSectionInternal(
		not_null<Window::SectionMemento *> memento,
		const SectionShow &params) {
	if (_specialLayer) {
		return _specialLayer->showSectionInternal(memento, params);
	}
	return false;
}

void LayerStackWidget::hideSpecialLayer(anim::type animated) {
	startAnimation([] {}, [this] {
		clearSpecialLayer();
		_mainMenu.destroyDelayed();
	}, Action::HideSpecialLayer, animated);
}

void LayerStackWidget::showMainMenu(anim::type animated) {
	startAnimation([this] {
		_mainMenu.create(this, _controller);
		_mainMenu->setGeometryToLeft(0, 0, _mainMenu->width(), height());
		_mainMenu->setParent(this);
	}, [this] {
		clearLayers();
		_specialLayer.destroyDelayed();
	}, Action::ShowMainMenu, animated);
}

void LayerStackWidget::appendBox(
		object_ptr<BoxContent> box,
		anim::type animated) {
	pushBox(std::move(box), animated);
}

LayerWidget *LayerStackWidget::pushBox(
		object_ptr<BoxContent> box,
		anim::type animated) {
	auto oldLayer = currentLayer();
	if (oldLayer) {
		if (oldLayer->inFocusChain()) setFocus();
		oldLayer->hide();
	}
	auto layer = object_ptr<AbstractBox>(
		this,
		_controller,
		std::move(box));
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
		}, Action::ShowLayer, animated);
	}

	return layer.data();
}

void LayerStackWidget::prependBox(
		object_ptr<BoxContent> box,
		anim::type animated) {
	if (_layers.empty()) {
		return showBox(std::move(box), animated);
	}
	auto layer = object_ptr<AbstractBox>(this, _controller, std::move(box));
	layer->hide();
	_layers.push_front(layer);
	initChildLayer(layer);
}

bool LayerStackWidget::takeToThirdSection() {
	return _specialLayer
		? _specialLayer->takeToThirdSection()
		: false;
}

void LayerStackWidget::clearLayers() {
	for (auto layer : base::take(_layers)) {
		layer->setClosing();
		if (layer->inFocusChain()) setFocus();
		layer->hide();
		layer->deleteLater();
	}
}

void LayerStackWidget::clearSpecialLayer() {
	if (_specialLayer) {
		_specialLayer->setClosing();
		_specialLayer.destroyDelayed();
	}
}

void LayerStackWidget::initChildLayer(LayerWidget *layer) {
	layer->setParent(this);
	layer->setClosedCallback([this, layer] { onLayerClosed(layer); });
	layer->setResizedCallback([this] { onLayerResized(); });
	connect(layer, SIGNAL(destroyed(QObject*)), this, SLOT(onLayerDestroyed(QObject*)));
	myEnsureResized(layer);
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
		hideAll(anim::type::normal);
	} else if (obj == currentLayer()) {
		_layers.pop_back();
		if (auto newLayer = currentLayer()) {
			newLayer->parentResized();
			if (!_background->animating()) {
				newLayer->show();
				showFinished();
			}
		} else if (!_specialLayer) {
			hideAll(anim::type::normal);
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
	// We must destroy all layers before we destroy LayerStackWidget.
	// Some layers in destructor call layer-related methods, like hiding
	// other layers, that call methods of LayerStackWidget and access
	// its fields, so if it is destroyed already everything crashes.
	for (auto layer : base::take(_layers)) {
		layer->setClosing();
		layer->hide();
		delete layer;
	}
	if (App::wnd()) App::wnd()->noLayerStack(this);
}

} // namespace Window

MediaPreviewWidget::MediaPreviewWidget(QWidget *parent, not_null<Window::Controller*> controller) : TWidget(parent)
, _controller(controller)
, _emojiSize(Ui::Emoji::Size(Ui::Emoji::Index() + 1) / cIntRetinaFactor()) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	subscribe(Auth().downloaderTaskFinished(), [this] { update(); });
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
			_controller->disableGifPauseReason(Window::GifPauseReason::MediaPreview);
			return;
		}
	} else {
		p.setOpacity(shown);
//		w = qMax(qRound(w * (st::stickerPreviewMin + ((1. - st::stickerPreviewMin) * shown)) / 2.) * 2 + int(w % 2), 1);
//		h = qMax(qRound(h * (st::stickerPreviewMin + ((1. - st::stickerPreviewMin) * shown)) / 2.) * 2 + int(h % 2), 1);
	}
	p.fillRect(r, st::stickerPreviewBg);
	p.drawPixmap((width() - w) / 2, (height() - h) / 2, image);
	if (!_emojiList.empty()) {
		auto emojiCount = _emojiList.size();
		auto emojiWidth = (emojiCount * _emojiSize) + (emojiCount - 1) * st::stickerEmojiSkip;
		auto emojiLeft = (width() - emojiWidth) / 2;
		auto esize = Ui::Emoji::Size(Ui::Emoji::Index() + 1);
		for (auto emoji : _emojiList) {
			p.drawPixmapLeft(emojiLeft, (height() - h) / 2 - (_emojiSize * 2), width(), App::emojiLarge(), QRect(emoji->x() * esize, emoji->y() * esize, esize, esize));
			emojiLeft += _emojiSize + st::stickerEmojiSkip;
		}
	}
}

void MediaPreviewWidget::resizeEvent(QResizeEvent *e) {
	update();
}

void MediaPreviewWidget::showPreview(DocumentData *document) {
	if (!document
		|| (!document->isAnimation() && !document->sticker())
		|| document->isVideoMessage()) {
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
		if (isHidden()) {
			show();
			_controller->enableGifPauseReason(Window::GifPauseReason::MediaPreview);
		}
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
	_emojiList.clear();
	if (_photo) {
		return;
	}
	if (auto sticker = _document->sticker()) {
		if (auto list = Stickers::GetEmojiListFromSet(_document)) {
			_emojiList = std::move(*list);
			while (_emojiList.size() > kStickerPreviewEmojiLimit) {
				_emojiList.pop_back();
			}
		} else if (auto emoji = Ui::Emoji::Find(sticker->alt)) {
			_emojiList.push_back(emoji);
		}
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
					that->_gif = Media::Clip::MakeReader(_document, FullMsgId(), [this, that](Media::Clip::Notification notification) {
						that->clipCallback(notification);
					});
					if (_gif) _gif->setAutoplay();
				}
			}
			if (_gif && _gif->started()) {
				auto s = currentDimensions();
				auto paused = _controller->isGifPausedAtLeastFor(Window::GifPauseReason::MediaPreview);
				return _gif->current(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, ImageRoundCorner::None, paused ? 0 : getms());
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
