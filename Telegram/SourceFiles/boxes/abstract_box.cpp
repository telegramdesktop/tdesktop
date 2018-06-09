/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/abstract_box.h"

#include "styles/style_boxes.h"
#include "styles/style_profile.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "mainwidget.h"
#include "mainwindow.h"

QPointer<Ui::RoundButton> BoxContent::addButton(
		base::lambda<QString()> textFactory,
		base::lambda<void()> clickCallback) {
	return addButton(
		std::move(textFactory),
		std::move(clickCallback),
		st::defaultBoxButton);
}

QPointer<Ui::RoundButton> BoxContent::addLeftButton(
		base::lambda<QString()> textFactory,
		base::lambda<void()> clickCallback) {
	return getDelegate()->addLeftButton(
		std::move(textFactory), 
		std::move(clickCallback), 
		st::defaultBoxButton);
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
			_topShadow.create(this);
			_bottomShadow.create(this);
		}
		if (!_preparing) {
			// We didn't set dimensions yet, this will be called from finishPrepare();
			finishScrollCreate();
		}
	} else {
		getDelegate()->setLayerType(false);
		_scroll.destroyDelayed();
		_topShadow.destroyDelayed();
		_bottomShadow.destroyDelayed();
	}
}

void BoxContent::finishPrepare() {
	_preparing = false;
	if (_scroll) {
		finishScrollCreate();
	}
	setInnerFocus();
}

void BoxContent::finishScrollCreate() {
	Expects(_scroll != nullptr);
	updateScrollAreaGeometry();
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(_scroll, SIGNAL(innerResized()), this, SLOT(onInnerResize()));
}

void BoxContent::scrollToWidget(not_null<QWidget*> widget) {
	if (_scroll) {
		_scroll->scrollToWidget(widget);
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
	_topShadow->toggle(
		(top > 0 || _innerTopSkip > 0),
		anim::type::normal);
	_bottomShadow->toggle(
		(top < _scroll->scrollTopMax() || _innerBottomSkip > 0),
		anim::type::normal);
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
		if (_scroll && width() > 0) {
			auto scrollTopWas = _scroll->scrollTop();
			updateScrollAreaGeometry();
			if (scrollBottomFixed) {
				_scroll->scrollToY(scrollTopWas + delta);
			}
		}
	}
}

void BoxContent::setInnerBottomSkip(int innerBottomSkip) {
	if (_innerBottomSkip != innerBottomSkip) {
		auto delta = innerBottomSkip - _innerBottomSkip;
		_innerBottomSkip = innerBottomSkip;
		if (_scroll && width() > 0) {
			updateScrollAreaGeometry();
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
	if (isTopShadowVisible) _topShadow->setVisible(false);
	if (isBottomShadowVisible) _bottomShadow->setVisible(false);
	auto result = Ui::GrabWidget(this, _scroll->geometry());
	if (isTopShadowVisible) _topShadow->setVisible(true);
	if (isBottomShadowVisible) _bottomShadow->setVisible(true);
	return result;
}

void BoxContent::resizeEvent(QResizeEvent *e) {
	if (_scroll) {
		updateScrollAreaGeometry();
	}
}

void BoxContent::updateScrollAreaGeometry() {
	auto newScrollHeight = height() - _innerTopSkip - _innerBottomSkip;
	auto changed = (_scroll->height() != newScrollHeight);
	_scroll->setGeometryToLeft(0, _innerTopSkip, width(), newScrollHeight);
	_topShadow->entity()->resize(width(), st::lineWidth);
	_topShadow->moveToLeft(0, _innerTopSkip);
	_bottomShadow->entity()->resize(width(), st::lineWidth);
	_bottomShadow->moveToLeft(
		0,
		height() - _innerBottomSkip - st::lineWidth);
	if (changed) {
		updateInnerVisibleTopBottom();

		auto top = _scroll->scrollTop();
		_topShadow->toggle(
			(top > 0 || _innerTopSkip > 0),
			anim::type::instant);
		_bottomShadow->toggle(
			(top < _scroll->scrollTopMax() || _innerBottomSkip > 0),
			anim::type::instant);
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

AbstractBox::AbstractBox(QWidget *parent, Window::Controller *controller, object_ptr<BoxContent> content) : LayerWidget(parent)
, _controller(controller)
, _content(std::move(content)) {
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });
	_content->setParent(this);
	_content->setDelegate(this);
}

void AbstractBox::setLayerType(bool layerType) {
	_layerType = layerType;
	updateTitlePosition();
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
		auto parts = RectPart::None | 0;
		if (paintTopRounded) parts |= RectPart::FullTop;
		if (paintBottomRounded) parts |= RectPart::FullBottom;
		App::roundRect(p, rect(), st::boxBg, BoxCorners, nullptr, parts);
	}
	auto other = e->region().intersected(QRect(0, st::boxRadius, width(), height() - 2 * st::boxRadius));
	if (!other.isEmpty()) {
		for (auto rect : other.rects()) {
			p.fillRect(rect, st::boxBg);
		}
	}
	if (!_additionalTitle.isEmpty() && clip.intersects(QRect(0, 0, width(), titleHeight()))) {
		paintAdditionalTitle(p);
	}
}

void AbstractBox::paintAdditionalTitle(Painter &p) {
	p.setFont(st::boxLayerTitleAdditionalFont);
	p.setPen(st::boxTitleAdditionalFg);
	p.drawTextLeft(_titleLeft + (_title ? _title->width() : 0) + st::boxLayerTitleAdditionalSkip, _titleTop + st::boxTitleFont->ascent - st::boxLayerTitleAdditionalFont->ascent, width(), _additionalTitle);
}

void AbstractBox::parentResized() {
	auto newHeight = countRealHeight();
	auto parentSize = parentWidget()->size();
	setGeometry((parentSize.width() - width()) / 2, (parentSize.height() - newHeight) / 2, width(), newHeight);
	update();
}

void AbstractBox::setTitle(base::lambda<TextWithEntities()> titleFactory) {
	_titleFactory = std::move(titleFactory);
	refreshTitle();
}

void AbstractBox::refreshTitle() {
	auto wasTitle = hasTitle();
	if (_titleFactory) {
		if (!_title) {
			_title.create(this, st::boxTitle);
		}
		_title->setMarkedText(_titleFactory());
		updateTitlePosition();
	} else {
		_title.destroy();
	}
	if (wasTitle != hasTitle()) {
		updateSize();
	}
}

void AbstractBox::setAdditionalTitle(base::lambda<QString()> additionalFactory) {
	_additionalTitleFactory = std::move(additionalFactory);
	refreshAdditionalTitle();
}

void AbstractBox::refreshAdditionalTitle() {
	_additionalTitle = _additionalTitleFactory ? _additionalTitleFactory() : QString();
	update();
}

void AbstractBox::refreshLang() {
	refreshTitle();
	refreshAdditionalTitle();
	InvokeQueued(this, [this] { updateButtonsPositions(); });
}

bool AbstractBox::hasTitle() const {
	return (_title != nullptr) || !_additionalTitle.isEmpty();
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

void AbstractBox::updateTitlePosition() {
	_titleLeft = _layerType ? st::boxLayerTitlePosition.x() : st::boxTitlePosition.x();
	_titleTop = _layerType ? st::boxLayerTitlePosition.y() : st::boxTitlePosition.y();
	if (_title) {
		_title->resizeToWidth(qMin(_title->naturalWidth(), width() - _titleLeft * 2));
		_title->moveToLeft(_titleLeft, _titleTop);
	}
}

void AbstractBox::clearButtons() {
	for (auto &button : base::take(_buttons)) {
		button.destroy();
	}
	_leftButton.destroy();
}

QPointer<Ui::RoundButton> AbstractBox::addButton(base::lambda<QString()> textFactory, base::lambda<void()> clickCallback, const style::RoundButton &st) {
	_buttons.push_back(object_ptr<Ui::RoundButton>(this, std::move(textFactory), st));
	auto result = QPointer<Ui::RoundButton>(_buttons.back());
	result->setClickedCallback(std::move(clickCallback));
	result->show();
	updateButtonsPositions();
	return result;
}

QPointer<Ui::RoundButton> AbstractBox::addLeftButton(base::lambda<QString()> textFactory, base::lambda<void()> clickCallback, const style::RoundButton &st) {
	_leftButton = object_ptr<Ui::RoundButton>(this, std::move(textFactory), st);
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
	updateTitlePosition();

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

BoxContentDivider::BoxContentDivider(QWidget *parent) : RpWidget(parent) {
}

int BoxContentDivider::resizeGetHeight(int newWidth) {
	return st::rightsDividerHeight;
}

void BoxContentDivider::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), st::contactsAboutBg);
	auto dividerFillTop = myrtlrect(0, 0, width(), st::profileDividerTop.height());
	st::profileDividerTop.fill(p, dividerFillTop);
	auto dividerFillBottom = myrtlrect(0, height() - st::profileDividerBottom.height(), width(), st::profileDividerBottom.height());
	st::profileDividerBottom.fill(p, dividerFillBottom);
}
