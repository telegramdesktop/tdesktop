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
#include "ui/effects/radial_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/text/text_utilities.h"
#include "base/timer.h"
#include "mainwidget.h"
#include "mainwindow.h"

struct AbstractBox::LoadingProgress {
	LoadingProgress(
		Fn<void()> &&callback,
		const style::InfiniteRadialAnimation &st);

	Ui::InfiniteRadialAnimation animation;
	base::Timer removeTimer;
};

AbstractBox::LoadingProgress::LoadingProgress(
	Fn<void()> &&callback,
	const style::InfiniteRadialAnimation &st)
: animation(std::move(callback), st) {
}

void BoxContent::setTitle(rpl::producer<QString> title) {
	getDelegate()->setTitle(std::move(title) | Ui::Text::ToWithEntities());
}

QPointer<Ui::RoundButton> BoxContent::addButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback) {
	return addButton(
		std::move(text),
		std::move(clickCallback),
		st::defaultBoxButton);
}

QPointer<Ui::RoundButton> BoxContent::addLeftButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback) {
	return getDelegate()->addLeftButton(
		std::move(text),
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

	if (!_scroll->isHidden()) {
		_scroll->show();
	}
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

void BoxContent::setDimensionsToContent(
		int newWidth,
		not_null<Ui::RpWidget*> content) {
	content->resizeToWidth(newWidth);
	content->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(newWidth, height);
	}, content->lifetime());
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

void BoxContent::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape && !_closeByEscape) {
		e->accept();
	} else {
		RpWidget::keyPressEvent(e);
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

AbstractBox::AbstractBox(
	not_null<Window::LayerStackWidget*> layer,
	object_ptr<BoxContent> content)
: LayerWidget(layer)
, _layer(layer)
, _content(std::move(content)) {
	subscribe(Lang::Current().updated(), [=] { refreshLang(); });
	_content->setParent(this);
	_content->setDelegate(this);

	_additionalTitle.changes(
	) | rpl::start_with_next([=] {
		updateSize();
		update();
	}, lifetime());
}

AbstractBox::~AbstractBox() = default;

void AbstractBox::setLayerType(bool layerType) {
	_layerType = layerType;
	updateTitlePosition();
}

int AbstractBox::titleHeight() const {
	return _layerType ? st::boxLayerTitleHeight : st::boxTitleHeight;
}

int AbstractBox::buttonsHeight() const {
	const auto padding = _layerType
		? st::boxLayerButtonPadding
		: st::boxButtonPadding;
	return padding.top() + st::defaultBoxButton.height + padding.bottom();
}

int AbstractBox::buttonsTop() const {
	const auto padding = _layerType
		? st::boxLayerButtonPadding
		: st::boxButtonPadding;
	return height() - padding.bottom() - st::defaultBoxButton.height;
}

QRect AbstractBox::loadingRect() const {
	const auto padding = _layerType
		? st::boxLayerButtonPadding
		: st::boxButtonPadding;
	const auto size = st::boxLoadingSize;
	const auto skipx = _layerType
		? st::boxLayerTitlePosition.x()
		: st::boxTitlePosition.x();
	const auto skipy = (st::defaultBoxButton.height - size) / 2;
	return QRect(
		skipx,
		height() - padding.bottom() - skipy - size,
		size,
		size);
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
	if (!_additionalTitle.current().isEmpty()
		&& clip.intersects(QRect(0, 0, width(), titleHeight()))) {
		paintAdditionalTitle(p);
	}
	if (_loadingProgress) {
		const auto rect = loadingRect();
		_loadingProgress->animation.draw(
			p,
			rect.topLeft(),
			rect.size(),
			width());
	}
}

void AbstractBox::paintAdditionalTitle(Painter &p) {
	p.setFont(st::boxLayerTitleAdditionalFont);
	p.setPen(st::boxTitleAdditionalFg);
	p.drawTextLeft(_titleLeft + (_title ? _title->width() : 0) + st::boxLayerTitleAdditionalSkip, _titleTop + st::boxTitleFont->ascent - st::boxLayerTitleAdditionalFont->ascent, width(), _additionalTitle.current());
}

void AbstractBox::parentResized() {
	auto newHeight = countRealHeight();
	auto parentSize = parentWidget()->size();
	setGeometry((parentSize.width() - width()) / 2, (parentSize.height() - newHeight) / 2, width(), newHeight);
	update();
}

void AbstractBox::setTitle(rpl::producer<TextWithEntities> title) {
	const auto wasTitle = hasTitle();
	if (title) {
		_title.create(this, std::move(title), st::boxTitle);
		_title->show();
		updateTitlePosition();
	} else {
		_title.destroy();
	}
	if (wasTitle != hasTitle()) {
		updateSize();
	}
}

void AbstractBox::setAdditionalTitle(rpl::producer<QString> additional) {
	_additionalTitle = std::move(additional);
}

void AbstractBox::setCloseByOutsideClick(bool close) {
	_closeByOutsideClick = close;
}

bool AbstractBox::closeByOutsideClick() const {
	return _closeByOutsideClick;
}

void AbstractBox::refreshLang() {
	InvokeQueued(this, [this] { updateButtonsPositions(); });
}

bool AbstractBox::hasTitle() const {
	return (_title != nullptr) || !_additionalTitle.current().isEmpty();
}

void AbstractBox::showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) {
	_layer->showBox(std::move(box), options, animated);
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
		for (const auto &button : _buttons) {
			button->moveToRight(right, top);
			right += button->width() + padding.left();
		}
	}
	if (_topButton) {
		_topButton->moveToRight(0, 0);
	}
}

QPointer<QWidget> AbstractBox::outerContainer() {
	return parentWidget();
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
	_topButton = nullptr;
}

QPointer<Ui::RoundButton> AbstractBox::addButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback,
		const style::RoundButton &st) {
	_buttons.emplace_back(this, std::move(text), st);
	auto result = QPointer<Ui::RoundButton>(_buttons.back());
	result->setClickedCallback(std::move(clickCallback));
	result->show();
	updateButtonsPositions();
	return result;
}

QPointer<Ui::RoundButton> AbstractBox::addLeftButton(
		rpl::producer<QString> text,
		Fn<void()> clickCallback,
		const style::RoundButton &st) {
	_leftButton = object_ptr<Ui::RoundButton>(this, std::move(text), st);
	auto result = QPointer<Ui::RoundButton>(_leftButton);
	result->setClickedCallback(std::move(clickCallback));
	result->show();
	updateButtonsPositions();
	return result;
}

QPointer<Ui::IconButton> AbstractBox::addTopButton(const style::IconButton &st, Fn<void()> clickCallback) {
	_topButton = base::make_unique_q<Ui::IconButton>(this, st);
	auto result = QPointer<Ui::IconButton>(_topButton.get());
	result->setClickedCallback(std::move(clickCallback));
	result->show();
	updateButtonsPositions();
	return result;
}

void AbstractBox::showLoading(bool show) {
	const auto &st = st::boxLoadingAnimation;
	if (!show) {
		if (_loadingProgress && !_loadingProgress->removeTimer.isActive()) {
			_loadingProgress->removeTimer.callOnce(
				st.sineDuration + st.sinePeriod);
			_loadingProgress->animation.stop();
		}
		return;
	}
	if (!_loadingProgress) {
		const auto callback = [=] {
			if (!anim::Disabled()) {
				const auto t = st::boxLoadingAnimation.thickness;
				update(loadingRect().marginsAdded({ t, t, t, t }));
			}
		};
		_loadingProgress = std::make_unique<LoadingProgress>(
			callback,
			st::boxLoadingAnimation);
		_loadingProgress->removeTimer.setCallback([=] {
			_loadingProgress = nullptr;
		});
	} else {
		_loadingProgress->removeTimer.cancel();
	}
	_loadingProgress->animation.start();
}


void AbstractBox::setDimensions(int newWidth, int maxHeight, bool forceCenterPosition) {
	_maxContentHeight = maxHeight;

	auto fullHeight = countFullHeight();
	if (width() != newWidth || _fullHeight != fullHeight) {
		_fullHeight = fullHeight;
		if (parentWidget()) {
			auto oldGeometry = geometry();
			resize(newWidth, countRealHeight());
			auto newGeometry = geometry();
			auto parentHeight = parentWidget()->height();
			if (newGeometry.top() + newGeometry.height() + st::boxVerticalMargin > parentHeight
				|| forceCenterPosition) {
				const auto top1 = parentHeight - int(st::boxVerticalMargin) - newGeometry.height();
				const auto top2 = (parentHeight - newGeometry.height()) / 2;
				const auto newTop = forceCenterPosition
					? std::min(top1, top2)
					: std::max(top1, top2);
				if (newTop != newGeometry.top()) {
					move(newGeometry.left(), newTop);
					resizeEvent(0);
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

BoxContentDivider::BoxContentDivider(QWidget *parent)
: BoxContentDivider(parent, st::rightsDividerHeight) {
}

BoxContentDivider::BoxContentDivider(QWidget *parent, int height)
: RpWidget(parent) {
	resize(width(), height);
}

void BoxContentDivider::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), st::contactsAboutBg);
	auto dividerFillTop = myrtlrect(0, 0, width(), st::profileDividerTop.height());
	st::profileDividerTop.fill(p, dividerFillTop);
	auto dividerFillBottom = myrtlrect(0, height() - st::profileDividerBottom.height(), width(), st::profileDividerBottom.height());
	st::profileDividerBottom.fill(p, dividerFillBottom);
}
