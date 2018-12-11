/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
#include "ui/widgets/shadow.h"
#include "ui/image/image.h"
#include "ui/emoji_config.h"
#include "window/window_main_menu.h"
#include "auth_session.h"
#include "chat_helpers/stickers.h"
#include "window/window_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_widgets.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_history.h"

namespace {

constexpr int kStickerPreviewEmojiLimit = 10;

} // namespace

namespace Window {

class LayerStackWidget::BackgroundWidget : public TWidget {
public:
	BackgroundWidget(QWidget *parent) : TWidget(parent) {
	}

	void setDoneCallback(Fn<void()> callback) {
		_doneCallback = std::move(callback);
	}

	void setLayerBoxes(const QRect &specialLayerBox, const QRect &layerBox);
	void setCacheImages(
		QPixmap &&bodyCache,
		QPixmap &&mainMenuCache,
		QPixmap &&specialLayerCache,
		QPixmap &&layerCache);
	void removeBodyCache();
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

	Fn<void()> _doneCallback;

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

void LayerStackWidget::BackgroundWidget::setCacheImages(
		QPixmap &&bodyCache,
		QPixmap &&mainMenuCache,
		QPixmap &&specialLayerCache,
		QPixmap &&layerCache) {
	_bodyCache = std::move(bodyCache);
	_mainMenuCache = std::move(mainMenuCache);
	_specialLayerCache = std::move(specialLayerCache);
	_layerCache = std::move(layerCache);
	_specialLayerCacheBox = _specialLayerBox;
	_layerCacheBox = _layerBox;
	setAttribute(Qt::WA_OpaquePaintEvent, !_bodyCache.isNull());
}

void LayerStackWidget::BackgroundWidget::removeBodyCache() {
	if (!_bodyCache.isNull()) {
		_bodyCache = {};
		setAttribute(Qt::WA_OpaquePaintEvent, false);
	}
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
	_mainMenuCache = _specialLayerCache = _layerCache = QPixmap();
	removeBodyCache();
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
		crl::on_main(this, [=] { checkIfDone(); });
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

LayerStackWidget::LayerStackWidget(QWidget *parent)
: RpWidget(parent)
, _background(this) {
	setGeometry(parentWidget()->rect());
	hide();
	_background->setDoneCallback([this] { animationDone(); });
}

void LayerWidget::setInnerFocus() {
	if (!isAncestorOf(window()->focusWidget())) {
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

void LayerStackWidget::setHideByBackgroundClick(bool hide) {
	_hideByBackgroundClick = hide;
}

void LayerStackWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		hideCurrent(anim::type::normal);
	}
}

void LayerStackWidget::mousePressEvent(QMouseEvent *e) {
	if (_hideByBackgroundClick) {
		if (const auto layer = currentLayer()) {
			if (!layer->closeByOutsideClick()) {
				return;
			}
		} else if (const auto special = _specialLayer.data()) {
			if (!special->closeByOutsideClick()) {
				return;
			}
		}
		hideCurrent(anim::type::normal);
	}
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
	startAnimation([] {}, [&] {
		clearLayers();
		clearSpecialLayer();
		_mainMenu.destroy();
	}, Action::HideAll, animated);
}

void LayerStackWidget::hideTopLayer(anim::type animated) {
	if (_specialLayer || _mainMenu) {
		hideLayers(animated);
	} else {
		hideAll(animated);
	}
}

void LayerStackWidget::removeBodyCache() {
	_background->removeBodyCache();
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

bool LayerStackWidget::layerShown() const {
	return _specialLayer || currentLayer() || _mainMenu;
}

void LayerStackWidget::setCacheImages() {
	auto bodyCache = QPixmap(), mainMenuCache = QPixmap();
	auto specialLayerCache = QPixmap();
	if (_specialLayer) {
		Ui::SendPendingMoveResizeEvents(_specialLayer);
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
	if (isAncestorOf(window()->focusWidget())) {
		setFocus();
	}
	if (_mainMenu) {
		removeBodyCache();
		hideChildren();
		bodyCache = Ui::GrabWidget(parentWidget());
		showChildren();
		mainMenuCache = Ui::Shadow::grab(_mainMenu, st::boxRoundShadow, RectPart::Right);
	}
	setAttribute(Qt::WA_OpaquePaintEvent, !bodyCache.isNull());
	updateLayerBoxes();
	_background->setCacheImages(std::move(bodyCache), std::move(mainMenuCache), std::move(specialLayerCache), std::move(layerCache));
}

void LayerStackWidget::closeLayer(not_null<LayerWidget*> layer) {
	const auto weak = make_weak(layer.get());
	if (weak->inFocusChain()) {
		setFocus();
	}
	if (!weak || !weak->setClosing()) {
		// This layer is already closing.
		return;
	} else if (!weak) {
		// setClosing() could've killed the layer.
		return;
	}

	if (layer == _specialLayer) {
		hideAll(anim::type::normal);
	} else if (layer == currentLayer()) {
		if (_layers.size() == 1) {
			hideCurrent(anim::type::normal);
		} else {
			auto taken = std::move(_layers.back());
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
			if (layer == i->get()) {
				_layers.erase(i);
				break;
			}
		}
	}
}

void LayerStackWidget::updateLayerBoxes() {
	const auto layerBox = [&] {
		if (const auto layer = currentLayer()) {
			return layer->geometry();
		}
		return QRect();
	}();
	const auto specialLayerBox = _specialLayer
		? _specialLayer->geometry()
		: QRect();
	_background->setLayerBoxes(specialLayerBox, layerBox);
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
		const auto weak = make_weak(this);
		clearOldWidgets();
		if (weak) {
			prepareForAnimation();
			_background->startAnimation(action);
		}
	}
}

void LayerStackWidget::resizeEvent(QResizeEvent *e) {
	const auto weak = make_weak(this);
	_background->setGeometry(rect());
	if (!weak) {
		return;
	}
	if (_specialLayer) {
		_specialLayer->parentResized();
		if (!weak) {
			return;
		}
	}
	if (const auto layer = currentLayer()) {
		layer->parentResized();
		if (!weak) {
			return;
		}
	}
	if (_mainMenu) {
		_mainMenu->resize(_mainMenu->width(), height());
		if (!weak) {
			return;
		}
	}
	updateLayerBoxes();
}

void LayerStackWidget::showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) {
	if (options & LayerOption::KeepOther) {
		if (options & LayerOption::ShowAfterOther) {
			prependBox(std::move(box), animated);
		} else {
			appendBox(std::move(box), animated);
		}
	} else {
		replaceBox(std::move(box), animated);
	}
}

void LayerStackWidget::replaceBox(
		object_ptr<BoxContent> box,
		anim::type animated) {
	const auto pointer = pushBox(std::move(box), animated);
	while (!_layers.empty() && _layers.front().get() != pointer) {
		auto removingLayer = std::move(_layers.front());
		_layers.erase(begin(_layers));

		if (removingLayer->inFocusChain()) {
			setFocus();
		}
		removingLayer->setClosing();
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
	if (const auto layer = currentLayer()) {
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
	setAttribute(Qt::WA_OpaquePaintEvent, false);
	if (hidden) {
		_hideFinishStream.fire({});
	} else {
		showFinished();
	}
}

rpl::producer<> LayerStackWidget::hideFinishEvents() const {
	return _hideFinishStream.events();
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
	if (canSetFocus()) {
		setInnerFocus();
	}
}

void LayerStackWidget::showSpecialLayer(
		object_ptr<LayerWidget> layer,
		anim::type animated) {
	startAnimation([&] {
		_specialLayer.destroy();
		_specialLayer = std::move(layer);
		initChildLayer(_specialLayer);
	}, [&] {
		_mainMenu.destroy();
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
	startAnimation([] {}, [&] {
		clearSpecialLayer();
		_mainMenu.destroy();
	}, Action::HideSpecialLayer, animated);
}

void LayerStackWidget::showMainMenu(
		not_null<Window::Controller*> controller,
		anim::type animated) {
	startAnimation([&] {
		_mainMenu.create(this, controller);
		_mainMenu->setGeometryToLeft(0, 0, _mainMenu->width(), height());
		_mainMenu->setParent(this);
	}, [&] {
		clearLayers();
		_specialLayer.destroy();
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
	_layers.push_back(std::make_unique<AbstractBox>(this, std::move(box)));
	const auto raw = _layers.back().get();
	initChildLayer(raw);

	if (_layers.size() > 1) {
		if (!_background->animating()) {
			raw->setVisible(true);
			showFinished();
		}
	} else {
		startAnimation([] {}, [&] {
			_mainMenu.destroy();
		}, Action::ShowLayer, animated);
	}

	return raw;
}

void LayerStackWidget::prependBox(
		object_ptr<BoxContent> box,
		anim::type animated) {
	if (_layers.empty()) {
		replaceBox(std::move(box), animated);
		return;
	}
	_layers.insert(
		begin(_layers),
		std::make_unique<AbstractBox>(this, std::move(box)));
	const auto raw = _layers.front().get();
	raw->hide();
	initChildLayer(raw);
}

bool LayerStackWidget::takeToThirdSection() {
	return _specialLayer
		? _specialLayer->takeToThirdSection()
		: false;
}

void LayerStackWidget::clearLayers() {
	for (auto &layer : base::take(_layers)) {
		if (layer->inFocusChain()) {
			setFocus();
		}
		layer->setClosing();
	}
}

void LayerStackWidget::clearSpecialLayer() {
	if (_specialLayer) {
		_specialLayer->setClosing();
		_specialLayer.destroy();
	}
}

void LayerStackWidget::initChildLayer(LayerWidget *layer) {
	layer->setParent(this);
	layer->setClosedCallback([=] { closeLayer(layer); });
	layer->setResizedCallback([=] { updateLayerBoxes(); });
	Ui::SendPendingMoveResizeEvents(layer);
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

LayerStackWidget::~LayerStackWidget() = default;

} // namespace Window

MediaPreviewWidget::MediaPreviewWidget(QWidget *parent, not_null<Window::Controller*> controller) : TWidget(parent)
, _controller(controller)
, _emojiSize(Ui::Emoji::GetSizeLarge() / cIntRetinaFactor()) {
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
		const auto emojiCount = _emojiList.size();
		const auto emojiWidth = (emojiCount * _emojiSize) + (emojiCount - 1) * st::stickerEmojiSkip;
		auto emojiLeft = (width() - emojiWidth) / 2;
		const auto esize = Ui::Emoji::GetSizeLarge();
		for (const auto emoji : _emojiList) {
			Ui::Emoji::Draw(
				p,
				emoji,
				esize,
				emojiLeft,
				(height() - h) / 2 - (_emojiSize * 2));
			emojiLeft += _emojiSize + st::stickerEmojiSkip;
		}
	}
}

void MediaPreviewWidget::resizeEvent(QResizeEvent *e) {
	update();
}

void MediaPreviewWidget::showPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document) {
	if (!document
		|| (!document->isAnimation() && !document->sticker())
		|| document->isVideoMessage()) {
		hidePreview();
		return;
	}

	startShow();
	_origin = origin;
	_photo = nullptr;
	_document = document;
	fillEmojiString();
	resetGifAndCache();
}

void MediaPreviewWidget::showPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo) {
	if (photo->full->isNull()) {
		hidePreview();
		return;
	}

	startShow();
	_origin = origin;
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
	result = QSize(qMax(ConvertScale(result.width()), 1), qMax(ConvertScale(result.height()), 1));
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
				if (const auto image = _document->getStickerImage()) {
					QSize s = currentDimensions();
					_cache = image->pix(_origin, s.width(), s.height());
					_cacheStatus = CacheLoaded;
				} else if (_cacheStatus != CacheThumbLoaded && _document->thumb->loaded()) {
					QSize s = currentDimensions();
					_cache = _document->thumb->pixBlurred(_origin, s.width(), s.height());
					_cacheStatus = CacheThumbLoaded;
				}
			}
		} else {
			_document->automaticLoad(_origin, nullptr);
			if (_document->loaded()) {
				if (!_gif && !_gif.isBad()) {
					auto that = const_cast<MediaPreviewWidget*>(this);
					that->_gif = Media::Clip::MakeReader(_document, FullMsgId(), [=](Media::Clip::Notification notification) {
						that->clipCallback(notification);
					});
					if (_gif) _gif->setAutoplay();
				}
			}
			if (_gif && _gif->started()) {
				auto s = currentDimensions();
				auto paused = _controller->isGifPausedAtLeastFor(Window::GifPauseReason::MediaPreview);
				return _gif->current(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None, paused ? 0 : getms());
			}
			if (_cacheStatus != CacheThumbLoaded && _document->thumb->loaded()) {
				QSize s = currentDimensions();
				_cache = _document->thumb->pixBlurred(_origin, s.width(), s.height());
				_cacheStatus = CacheThumbLoaded;
			}
		}
	} else if (_photo) {
		if (_cacheStatus != CacheLoaded) {
			if (_photo->full->loaded()) {
				QSize s = currentDimensions();
				_cache = _photo->full->pix(_origin, s.width(), s.height());
				_cacheStatus = CacheLoaded;
			} else {
				if (_cacheStatus != CacheThumbLoaded && _photo->thumb->loaded()) {
					QSize s = currentDimensions();
					_cache = _photo->thumb->pixBlurred(_origin, s.width(), s.height());
					_cacheStatus = CacheThumbLoaded;
				}
				_photo->thumb->load(_origin);
				_photo->full->load(_origin);
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
			_gif->start(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None);
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
