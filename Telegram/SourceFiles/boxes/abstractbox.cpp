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
#include "boxes/abstractbox.h"

#include "styles/style_boxes.h"
#include "localstorage.h"
#include "lang.h"
#include "ui/effects/widget_fade_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "mainwidget.h"
#include "mainwindow.h"

BoxLayerTitleShadow::BoxLayerTitleShadow(QWidget *parent) : Ui::PlainShadow(parent, st::boxLayerTitleShadow) {
}

QPointer<Ui::RoundButton> BoxContent::addButton(const QString &text, base::lambda<void()> clickCallback) {
	return addButton(text, std::move(clickCallback), st::defaultBoxButton);
}

QPointer<Ui::RoundButton> BoxContent::addLeftButton(const QString &text, base::lambda<void()> clickCallback) {
	return getDelegate()->addLeftButton(text, std::move(clickCallback), st::defaultBoxButton);
}

void BoxContent::setInner(object_ptr<TWidget> inner) {
	setInner(std::move(inner), st::boxLayerScroll);
}

void BoxContent::setInner(object_ptr<TWidget> inner, const style::ScrollArea &st) {
	if (inner) {
		getDelegate()->setLayerType(true);
		_scroll.create(this, st);
		_scroll->setGeometryToLeft(0, _innerTopSkip, width(), 0);
		_scroll->setOwnedWidget(std::move(inner));
		if (_topShadow) {
			_topShadow->raise();
			_bottomShadow->raise();
		} else {
			_topShadow.create(this, object_ptr<BoxLayerTitleShadow>(this));
			_bottomShadow.create(this, object_ptr<BoxLayerTitleShadow>(this));
		}
		updateScrollAreaGeometry();
		connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
		connect(_scroll, SIGNAL(innerResized()), this, SLOT(onInnerResize()));
	} else {
		getDelegate()->setLayerType(false);
		_scroll.destroyDelayed();
		_topShadow.destroyDelayed();
		_bottomShadow.destroyDelayed();
	}
}

void BoxContent::onScrollToY(int top, int bottom) {
	if (_scroll) {
		_scroll->scrollToY(top, bottom);
	}
}

void BoxContent::onDraggingScrollDelta(int delta) {
	_draggingScrollDelta = _scroll ? delta : 0;
	if (_draggingScrollDelta) {
		if (!_draggingScrollTimer) {
			_draggingScrollTimer.create(this);
			_draggingScrollTimer->setSingleShot(false);
			connect(_draggingScrollTimer, SIGNAL(timeout()), this, SLOT(onDraggingScrollTimer()));
		}
		_draggingScrollTimer->start(15);
	} else {
		_draggingScrollTimer.destroy();
	}
}

void BoxContent::onDraggingScrollTimer() {
	auto delta = (_draggingScrollDelta > 0) ? qMin(_draggingScrollDelta * 3 / 20 + 1, int32(MaxScrollSpeed)) : qMax(_draggingScrollDelta * 3 / 20 - 1, -int32(MaxScrollSpeed));
	_scroll->scrollToY(_scroll->scrollTop() + delta);
}

void BoxContent::updateInnerVisibleTopBottom() {
	if (auto widget = static_cast<TWidget*>(_scroll ? _scroll->widget() : nullptr)) {
		auto top = _scroll->scrollTop();
		widget->setVisibleTopBottom(top, top + _scroll->height());
	}
}

void BoxContent::updateShadowsVisibility() {
	if (!_scroll) return;

	auto top = _scroll->scrollTop();
	if (top > 0 || _innerTopSkip > 0) {
		_topShadow->showAnimated();
	} else {
		_topShadow->hideAnimated();
	}
	if (top < _scroll->scrollTopMax()) {
		_bottomShadow->showAnimated();
	} else {
		_bottomShadow->hideAnimated();
	}
}

void BoxContent::onScroll() {
	updateInnerVisibleTopBottom();
	updateShadowsVisibility();
}

void BoxContent::onInnerResize() {
	updateInnerVisibleTopBottom();
	updateShadowsVisibility();
}

void BoxContent::setInnerTopSkip(int innerTopSkip, bool scrollBottomFixed) {
	if (_innerTopSkip != innerTopSkip) {
		auto delta = innerTopSkip - _innerTopSkip;
		_innerTopSkip = innerTopSkip;
		if (_scroll) {
			auto scrollTopWas = _scroll->scrollTop();
			updateScrollAreaGeometry();
			if (scrollBottomFixed) {
				_scroll->scrollToY(scrollTopWas + delta);
			}
		}
	}
}

void BoxContent::setInnerVisible(bool scrollAreaVisible) {
	if (_scroll) {
		_scroll->setVisible(scrollAreaVisible);
	}
}

QPixmap BoxContent::grabInnerCache() {
	auto isTopShadowVisible = !_topShadow->isHidden();
	auto isBottomShadowVisible = !_bottomShadow->isHidden();
	if (isTopShadowVisible) _topShadow->hide();
	if (isBottomShadowVisible) _bottomShadow->hide();
	auto result = myGrab(this, _scroll->geometry());
	if (isTopShadowVisible) _topShadow->show();
	if (isBottomShadowVisible) _bottomShadow->show();
	return result;
}

void BoxContent::resizeEvent(QResizeEvent *e) {
	if (_scroll) {
		updateScrollAreaGeometry();
	}
}

void BoxContent::updateScrollAreaGeometry() {
	auto newScrollHeight = height() - _innerTopSkip;
	auto changed = (_scroll->height() != newScrollHeight);
	_scroll->setGeometryToLeft(0, _innerTopSkip, width(), newScrollHeight);
	_topShadow->entity()->resize(width(), st::lineWidth);
	_topShadow->moveToLeft(0, _innerTopSkip);
	_bottomShadow->entity()->resize(width(), st::lineWidth);
	_bottomShadow->moveToLeft(0, height() - st::lineWidth);
	if (changed) {
		updateInnerVisibleTopBottom();

		auto top = _scroll->scrollTop();
		if (top > 0 || _innerTopSkip > 0) {
			_topShadow->showFast();
		} else {
			_topShadow->hideFast();
		}
		if (top < _scroll->scrollTopMax()) {
			_bottomShadow->showFast();
		} else {
			_bottomShadow->hideFast();
		}
	}
}

object_ptr<TWidget> BoxContent::doTakeInnerWidget() {
	return _scroll->takeWidget<TWidget>();
}

void BoxContent::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (testAttribute(Qt::WA_OpaquePaintEvent)) {
		for (auto rect : e->region().rects()) {
			p.fillRect(rect, st::boxBg);
		}
	}
}

AbstractBox::AbstractBox(QWidget *parent, object_ptr<BoxContent> content) : LayerWidget(parent)
, _content(std::move(content)) {
	_content->setParent(this);
	_content->setDelegate(this);
}

void AbstractBox::setLayerType(bool layerType) {
	_layerType = layerType;
}

int AbstractBox::titleHeight() const {
	return _layerType ? st::boxLayerTitleHeight : st::boxTitleHeight;
}

int AbstractBox::buttonsHeight() const {
	auto padding = _layerType ? st::boxLayerButtonPadding : st::boxButtonPadding;
	return padding.top() + st::defaultBoxButton.height + padding.bottom();
}

int AbstractBox::buttonsTop() const {
	auto padding = _layerType ? st::boxLayerButtonPadding : st::boxButtonPadding;
	return height() - padding.bottom() - st::defaultBoxButton.height;
}

void AbstractBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	auto paintTopRounded = clip.intersects(QRect(0, 0, width(), st::boxRadius));
	auto paintBottomRounded = clip.intersects(QRect(0, height() - st::boxRadius, width(), st::boxRadius));
	if (paintTopRounded || paintBottomRounded) {
		auto parts = qFlags(App::RectPart::None);
		if (paintTopRounded) parts |= App::RectPart::TopFull;
		if (paintBottomRounded) parts |= App::RectPart::BottomFull;
		App::roundRect(p, rect(), st::boxBg, BoxCorners, nullptr, parts);
	}
	auto other = e->region().intersected(QRect(0, st::boxRadius, width(), height() - 2 * st::boxRadius));
	if (!other.isEmpty()) {
		for (auto rect : other.rects()) {
			p.fillRect(rect, st::boxBg);
		}
	}
	if (!_title.isEmpty() && clip.intersects(QRect(0, 0, width(), titleHeight()))) {
		paintTitle(p, _title, _additionalTitle);
	}
}

void AbstractBox::paintTitle(Painter &p, const QString &title, const QString &additional) {
	p.setFont(st::boxTitleFont);
	p.setPen(st::boxTitleFg);
	auto titleWidth = st::boxTitleFont->width(title);
	auto titleLeft = _layerType ? st::boxLayerTitlePosition.x() : st::boxTitlePosition.x();
	auto titleTop = _layerType ? st::boxLayerTitlePosition.y() : st::boxTitlePosition.y();
	p.drawTextLeft(titleLeft, titleTop, width(), title, titleWidth);
	if (!additional.isEmpty()) {
		p.setFont(st::boxLayerTitleAdditionalFont);
		p.setPen(st::boxTitleAdditionalFg);
		p.drawTextLeft(titleLeft + titleWidth + st::boxLayerTitleAdditionalSkip, titleTop + st::boxTitleFont->ascent - st::boxLayerTitleAdditionalFont->ascent, width(), additional);
	}
}

void AbstractBox::parentResized() {
	auto newHeight = countRealHeight();
	auto parentSize = parentWidget()->size();
	setGeometry((parentSize.width() - width()) / 2, (parentSize.height() - newHeight) / 2, width(), newHeight);
	update();
}

void AbstractBox::setTitle(const QString &title, const QString &additional) {
	auto wasTitle = hasTitle();
	_title = title;
	_additionalTitle = additional;
	update();
	if (wasTitle != hasTitle()) {
		updateSize();
	}
}

bool AbstractBox::hasTitle() const {
	return !_title.isEmpty() || !_additionalTitle.isEmpty();
}

void AbstractBox::updateSize() {
	setDimensions(width(), _maxContentHeight);
}

void AbstractBox::updateButtonsPositions() {
	if (!_buttons.empty() || _leftButton) {
		auto padding = _layerType ? st::boxLayerButtonPadding : st::boxButtonPadding;
		auto right = padding.right();
		auto top = buttonsTop();
		if (_leftButton) {
			_leftButton->moveToLeft(right, top);
		}
		for_const (auto &button, _buttons) {
			button->moveToRight(right, top);
			right += button->width() + padding.left();
		}
	}
}

void AbstractBox::clearButtons() {
	for (auto &button : base::take(_buttons)) {
		button.destroy();
	}
	_leftButton.destroy();
}

QPointer<Ui::RoundButton> AbstractBox::addButton(const QString &text, base::lambda<void()> clickCallback, const style::RoundButton &st) {
	_buttons.push_back(object_ptr<Ui::RoundButton>(this, text, st));
	auto result = QPointer<Ui::RoundButton>(_buttons.back());
	result->setClickedCallback(std::move(clickCallback));
	result->show();
	updateButtonsPositions();
	return result;
}

QPointer<Ui::RoundButton> AbstractBox::addLeftButton(const QString &text, base::lambda<void()> clickCallback, const style::RoundButton &st) {
	_leftButton = object_ptr<Ui::RoundButton>(this, text, st);
	auto result = QPointer<Ui::RoundButton>(_leftButton);
	result->setClickedCallback(std::move(clickCallback));
	result->show();
	updateButtonsPositions();
	return result;
}

void AbstractBox::setDimensions(int newWidth, int maxHeight) {
	_maxContentHeight = maxHeight;

	auto fullHeight = countFullHeight();
	if (width() != newWidth || _fullHeight != fullHeight) {
		_fullHeight = fullHeight;
		if (parentWidget()) {
			auto oldGeometry = geometry();
			resize(newWidth, countRealHeight());
			auto newGeometry = geometry();
			auto parentHeight = parentWidget()->height();
			if (newGeometry.top() + newGeometry.height() + st::boxVerticalMargin > parentHeight) {
				auto newTop = qMax(parentHeight - int(st::boxVerticalMargin) - newGeometry.height(), (parentHeight - newGeometry.height()) / 2);
				if (newTop != newGeometry.top()) {
					move(newGeometry.left(), newTop);
				}
			}
			parentWidget()->update(oldGeometry.united(geometry()).marginsAdded(st::boxRoundShadow.extend));
		} else {
			resize(newWidth, 0);
		}
	}
}

int AbstractBox::countRealHeight() const {
	return qMin(_fullHeight, parentWidget()->height() - 2 * st::boxVerticalMargin);
}

int AbstractBox::countFullHeight() const {
	return contentTop() + _maxContentHeight + buttonsHeight();
}

int AbstractBox::contentTop() const {
	return hasTitle() ? titleHeight() : (_noContentMargin ? 0 : st::boxTopMargin);
}

void AbstractBox::resizeEvent(QResizeEvent *e) {
	updateButtonsPositions();

	auto top = contentTop();
	_content->resize(width(), height() - top - buttonsHeight());
	_content->moveToLeft(0, top);

	LayerWidget::resizeEvent(e);
}

void AbstractBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		closeBox();
	} else {
		LayerWidget::keyPressEvent(e);
	}
}
