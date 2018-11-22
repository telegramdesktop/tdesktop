/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/tabbed_panel.h"

#include "ui/widgets/shadow.h"
#include "ui/image/image_prepare.h"
#include "chat_helpers/tabbed_selector.h"
#include "window/window_controller.h"
#include "mainwindow.h"
#include "messenger.h"
#include "styles/style_chat_helpers.h"

namespace ChatHelpers {
namespace {

constexpr auto kHideTimeoutMs = 300;
constexpr auto kDelayedHideTimeoutMs = 3000;

} // namespace

TabbedPanel::TabbedPanel(
	QWidget *parent,
	not_null<Window::Controller*> controller)
: TabbedPanel(
	parent,
	controller,
	object_ptr<TabbedSelector>(nullptr, controller)) {
}

TabbedPanel::TabbedPanel(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	object_ptr<TabbedSelector> selector)
: RpWidget(parent)
, _controller(controller)
, _selector(std::move(selector))
, _heightRatio(st::emojiPanHeightRatio)
, _minContentHeight(st::emojiPanMinHeight)
, _maxContentHeight(st::emojiPanMaxHeight) {
	_selector->setParent(this);
	_selector->setRoundRadius(st::buttonRadius);
	_selector->setAfterShownCallback([this](SelectorTab tab) {
		if (tab == SelectorTab::Gifs) {
			_controller->enableGifPauseReason(Window::GifPauseReason::SavedGifs);
		}
	});
	_selector->setBeforeHidingCallback([this](SelectorTab tab) {
		if (tab == SelectorTab::Gifs) {
			_controller->disableGifPauseReason(Window::GifPauseReason::SavedGifs);
		}
	});
	_selector->showRequests(
	) | rpl::start_with_next([=] {
		showFromSelector();
	}, lifetime());

	resize(QRect(0, 0, st::emojiPanWidth, st::emojiPanMaxHeight).marginsAdded(innerPadding()).size());

	_contentMaxHeight = st::emojiPanMaxHeight;
	_contentHeight = _contentMaxHeight;

	_selector->resize(st::emojiPanWidth, _contentHeight);
	_selector->move(innerRect().topLeft());

	_hideTimer.setCallback([this] { hideByTimerOrLeave(); });

	_selector->checkForHide(
	) | rpl::start_with_next([=] {
		if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
			_hideTimer.callOnce(kDelayedHideTimeoutMs);
		}
	}, lifetime());

	_selector->cancelled(
	) | rpl::start_with_next([=] {
		hideAnimated();
	}, lifetime());

	_selector->slideFinished(
	) | rpl::start_with_next([=] {
		InvokeQueued(this, [=] {
			if (_hideAfterSlide) {
				startOpacityAnimation(true);
			}
		});
	}, lifetime());

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		connect(App::wnd()->windowHandle(), &QWindow::activeChanged, this, [=] {
			windowActiveChanged();
		});
	}
	setAttribute(Qt::WA_OpaquePaintEvent, false);

	hideChildren();
}

void TabbedPanel::moveBottomRight(int bottom, int right) {
	_bottom = bottom;
	_right = right;
	updateContentHeight();
}

void TabbedPanel::setDesiredHeightValues(
		float64 ratio,
		int minHeight,
		int maxHeight) {
	_heightRatio = ratio;
	_minContentHeight = minHeight;
	_maxContentHeight = maxHeight;
	updateContentHeight();
}

void TabbedPanel::updateContentHeight() {
	if (isDestroying()) {
		return;
	}

	auto addedHeight = innerPadding().top() + innerPadding().bottom();
	auto marginsHeight = _selector->marginTop() + _selector->marginBottom();
	auto availableHeight = _bottom - marginsHeight;
	auto wantedContentHeight = qRound(_heightRatio * availableHeight) - addedHeight;
	auto contentHeight = marginsHeight + snap(
		wantedContentHeight,
		_minContentHeight,
		_maxContentHeight);
	auto resultTop = _bottom - addedHeight - contentHeight;
	if (contentHeight == _contentHeight) {
		move(x(), resultTop);
		return;
	}

	auto was = _contentHeight;
	_contentHeight = contentHeight;

	resize(QRect(0, 0, innerRect().width(), _contentHeight).marginsAdded(innerPadding()).size());
	move(x(), resultTop);

	_selector->resize(innerRect().width(), _contentHeight);

	update();
}

void TabbedPanel::windowActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden() && !preventAutoHide()) {
		hideAnimated();
	}
}

void TabbedPanel::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();

	// This call can finish _a_show animation and destroy _showAnimation.
	auto opacityAnimating = _a_opacity.animating(ms);

	auto showAnimating = _a_show.animating(ms);
	if (_showAnimation && !showAnimating) {
		_showAnimation.reset();
		if (!opacityAnimating && !isDestroying()) {
			showChildren();
			_selector->afterShown();
		}
	}

	if (showAnimating) {
		Assert(_showAnimation != nullptr);
		if (auto opacity = _a_opacity.current(_hiding ? 0. : 1.)) {
			_showAnimation->paintFrame(p, 0, 0, width(), _a_show.current(1.), opacity);
		}
	} else if (opacityAnimating) {
		p.setOpacity(_a_opacity.current(_hiding ? 0. : 1.));
		p.drawPixmap(0, 0, _cache);
	} else if (_hiding || isHidden()) {
		hideFinished();
	} else {
		if (!_cache.isNull()) _cache = QPixmap();
		Ui::Shadow::paint(p, innerRect(), width(), st::emojiPanAnimation.shadow);
	}
}

void TabbedPanel::moveByBottom() {
	const auto right = std::max(parentWidget()->width() - _right, 0);
	moveToRight(right, y());
	updateContentHeight();
}

void TabbedPanel::enterEventHook(QEvent *e) {
	Messenger::Instance().registerLeaveSubscription(this);
	showAnimated();
}

bool TabbedPanel::preventAutoHide() const {
	if (isDestroying()) {
		return false;
	}
	return _selector->preventAutoHide();
}

void TabbedPanel::leaveEventHook(QEvent *e) {
	Messenger::Instance().unregisterLeaveSubscription(this);
	if (preventAutoHide()) {
		return;
	}
	auto ms = getms();
	if (_a_show.animating(ms) || _a_opacity.animating(ms)) {
		hideAnimated();
	} else {
		_hideTimer.callOnce(kHideTimeoutMs);
	}
	return TWidget::leaveEventHook(e);
}

void TabbedPanel::otherEnter() {
	showAnimated();
}

void TabbedPanel::otherLeave() {
	if (preventAutoHide()) {
		return;
	}

	auto ms = getms();
	if (_a_opacity.animating(ms)) {
		hideByTimerOrLeave();
	} else {
		_hideTimer.callOnce(0);
	}
}

void TabbedPanel::hideFast() {
	if (isHidden()) return;

	_hideTimer.cancel();
	_hiding = false;
	_a_opacity.finish();
	hideFinished();
}

void TabbedPanel::opacityAnimationCallback() {
	update();
	if (!_a_opacity.animating()) {
		if (_hiding || isDestroying()) {
			_hiding = false;
			hideFinished();
		} else if (!_a_show.animating()) {
			showChildren();
			_selector->afterShown();
		}
	}
}

void TabbedPanel::hideByTimerOrLeave() {
	if (isHidden() || preventAutoHide()) return;

	hideAnimated();
}

void TabbedPanel::prepareCache() {
	if (_a_opacity.animating()) return;

	auto showAnimation = base::take(_a_show);
	auto showAnimationData = base::take(_showAnimation);
	showChildren();
	_cache = Ui::GrabWidget(this);
	_showAnimation = base::take(showAnimationData);
	_a_show = base::take(showAnimation);
	if (_a_show.animating()) {
		hideChildren();
	}
}

void TabbedPanel::startOpacityAnimation(bool hiding) {
	if (_selector && !_selector->isHidden()) {
		_selector->beforeHiding();
	}
	_hiding = false;
	prepareCache();
	_hiding = hiding;
	hideChildren();
	_a_opacity.start([this] { opacityAnimationCallback(); }, _hiding ? 1. : 0., _hiding ? 0. : 1., st::emojiPanDuration);
}

void TabbedPanel::startShowAnimation() {
	if (!_a_show.animating()) {
		auto image = grabForAnimation();

		_showAnimation = std::make_unique<Ui::PanelAnimation>(st::emojiPanAnimation, Ui::PanelAnimation::Origin::BottomRight);
		auto inner = rect().marginsRemoved(st::emojiPanMargins);
		_showAnimation->setFinalImage(std::move(image), QRect(inner.topLeft() * cIntRetinaFactor(), inner.size() * cIntRetinaFactor()));
		auto corners = App::cornersMask(ImageRoundRadius::Small);
		_showAnimation->setCornerMasks(corners[0], corners[1], corners[2], corners[3]);
		_showAnimation->start();
	}
	hideChildren();
	_a_show.start([this] { update(); }, 0., 1., st::emojiPanShowDuration);
}

QImage TabbedPanel::grabForAnimation() {
	auto cache = base::take(_cache);
	auto opacityAnimation = base::take(_a_opacity);
	auto showAnimationData = base::take(_showAnimation);
	auto showAnimation = base::take(_a_show);

	showChildren();
	Ui::SendPendingMoveResizeEvents(this);

	auto result = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	if (_selector) {
		_selector->render(&result, _selector->geometry().topLeft());
	}

	_a_show = base::take(showAnimation);
	_showAnimation = base::take(showAnimationData);
	_a_opacity = base::take(opacityAnimation);
	_cache = base::take(_cache);

	return result;
}

void TabbedPanel::hideAnimated() {
	if (isHidden() || _hiding) {
		return;
	}

	_hideTimer.cancel();
	if (!isDestroying() && _selector->isSliding()) {
		_hideAfterSlide = true;
	} else {
		startOpacityAnimation(true);
	}
}

void TabbedPanel::toggleAnimated() {
	if (isDestroying()) {
		return;
	}
	if (isHidden() || _hiding || _hideAfterSlide) {
		showAnimated();
	} else {
		hideAnimated();
	}
}

object_ptr<TabbedSelector> TabbedPanel::takeSelector() {
	if (!isHidden() && !_hiding) {
		startOpacityAnimation(true);
	}
	return std::move(_selector);
}

QPointer<TabbedSelector> TabbedPanel::getSelector() const {
	return _selector.data();
}

void TabbedPanel::hideFinished() {
	hide();
	_a_show.finish();
	_showAnimation.reset();
	_cache = QPixmap();
	_hiding = false;
	if (isDestroying()) {
		deleteLater();
	} else {
		_selector->hideFinished();
	}
}

void TabbedPanel::showAnimated() {
	_hideTimer.cancel();
	_hideAfterSlide = false;
	showStarted();
}

void TabbedPanel::showStarted() {
	if (isDestroying()) {
		return;
	}
	if (isHidden()) {
		_selector->showStarted();
		moveByBottom();
		raise();
		show();
		startShowAnimation();
	} else if (_hiding) {
		startOpacityAnimation(false);
	}
}

bool TabbedPanel::eventFilter(QObject *obj, QEvent *e) {
	if (isDestroying()) {
		return false;
	}
	if (e->type() == QEvent::Enter) {
		otherEnter();
	} else if (e->type() == QEvent::Leave) {
		otherLeave();
	}
	return false;
}

void TabbedPanel::showFromSelector() {
	if (isHidden()) {
		moveByBottom();
		startShowAnimation();
		show();
	}
	showChildren();
	showAnimated();
}

style::margins TabbedPanel::innerPadding() const {
	return st::emojiPanMargins;
}

QRect TabbedPanel::innerRect() const {
	return rect().marginsRemoved(innerPadding());
}

bool TabbedPanel::overlaps(const QRect &globalRect) const {
	if (isHidden() || !_cache.isNull()) return false;

	auto testRect = QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size());
	auto inner = rect().marginsRemoved(st::emojiPanMargins);
	return inner.marginsRemoved(QMargins(st::buttonRadius, 0, st::buttonRadius, 0)).contains(testRect)
		|| inner.marginsRemoved(QMargins(0, st::buttonRadius, 0, st::buttonRadius)).contains(testRect);
}

TabbedPanel::~TabbedPanel() = default;

} // namespace ChatHelpers
