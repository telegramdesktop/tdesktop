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
#include "chat_helpers/tabbed_panel.h"

#include "ui/widgets/shadow.h"
#include "styles/style_chat_helpers.h"
#include "chat_helpers/tabbed_selector.h"
#include "mainwindow.h"

namespace ChatHelpers {
namespace {

constexpr auto kHideTimeoutMs = 300;
constexpr auto kDelayedHideTimeoutMs = 3000;

} // namespace

TabbedPanel::TabbedPanel(QWidget *parent, gsl::not_null<Window::Controller*> controller) : TWidget(parent)
, _selector(this, controller) {
	_selector->setRoundRadius(st::buttonRadius);

	resize(QRect(0, 0, st::emojiPanWidth, st::emojiPanMaxHeight).marginsAdded(innerPadding()).size());

	_contentMaxHeight = st::emojiPanMaxHeight;
	_contentHeight = _contentMaxHeight;

	_selector->resize(st::emojiPanWidth, _contentHeight);
	_selector->move(innerRect().topLeft());

	_hideTimer.setCallback([this] { hideByTimerOrLeave(); });

	connect(_selector, SIGNAL(checkForHide()), this, SLOT(onCheckForHide()));
	connect(_selector, SIGNAL(emojiSelected(EmojiPtr)), this, SIGNAL(emojiSelected(EmojiPtr)));
	connect(_selector, SIGNAL(stickerSelected(DocumentData*)), this, SIGNAL(stickerSelected(DocumentData*)));
	connect(_selector, SIGNAL(photoSelected(PhotoData*)), this, SIGNAL(photoSelected(PhotoData*)));
	connect(_selector, SIGNAL(inlineResultSelected(InlineBots::Result*,UserData*)), this, SIGNAL(inlineResultSelected(InlineBots::Result*,UserData*)));
	connect(_selector, &TabbedSelector::cancelled, this, [this] {
		hideAnimated();
	});
	connect(_selector, &TabbedSelector::slideFinished, this, [this] {
		InvokeQueued(this, [this] {
			if (_hideAfterSlide) {
				startOpacityAnimation(true);
			}
		});
	});

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWndActiveChanged()));
	}
	setAttribute(Qt::WA_OpaquePaintEvent, false);

	hideChildren();
}

void TabbedPanel::moveBottom(int bottom) {
	_bottom = bottom;
	updateContentHeight();
}

void TabbedPanel::updateContentHeight() {
	auto addedHeight = innerPadding().top() + innerPadding().bottom();
	auto wantedContentHeight = qRound(st::emojiPanHeightRatio * _bottom) - addedHeight;
	auto contentHeight = snap(wantedContentHeight, st::emojiPanMinHeight, st::emojiPanMaxHeight);
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

void TabbedPanel::onWndActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(0);
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
		if (!opacityAnimating) {
			showChildren();
			_selector->afterShown();
		}
	}

	if (showAnimating) {
		t_assert(_showAnimation != nullptr);
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
	moveToRight(0, y());
	updateContentHeight();
}

void TabbedPanel::enterEventHook(QEvent *e) {
	showAnimated();
}

bool TabbedPanel::preventAutoHide() const {
	return _selector->preventAutoHide();
}

void TabbedPanel::leaveEventHook(QEvent *e) {
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

void TabbedPanel::refreshStickers() {
	_selector->refreshStickers();
}

void TabbedPanel::opacityAnimationCallback() {
	update();
	if (!_a_opacity.animating()) {
		if (_hiding) {
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
	_cache = myGrab(this);
	_showAnimation = base::take(showAnimationData);
	_a_show = base::take(showAnimation);
	if (_a_show.animating()) {
		hideChildren();
	}
}

void TabbedPanel::startOpacityAnimation(bool hiding) {
	if (!_selector->isHidden()) {
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
		_showAnimation->setCornerMasks(QImage(*corners[0]), QImage(*corners[1]), QImage(*corners[2]), QImage(*corners[3]));
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
	myEnsureResized(this);
	myEnsureResized(_selector);

	auto result = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	_selector->render(&result, _selector->geometry().topLeft());

	_a_show = base::take(showAnimation);
	_showAnimation = base::take(showAnimationData);
	_a_opacity = base::take(opacityAnimation);
	_cache = base::take(_cache);

	return result;
}

void TabbedPanel::hideAnimated() {
	if (isHidden()) return;
	if (_hiding) return;

	_hideTimer.cancel();
	if (_selector->isSliding()) {
		_hideAfterSlide = true;
	} else {
		startOpacityAnimation(true);
	}
}

TabbedPanel::~TabbedPanel() = default;

void TabbedPanel::hideFinished() {
	hide();
	_selector->hideFinished();
	_a_show.finish();
	_showAnimation.reset();
	_cache = QPixmap();
	_hiding = false;
}

void TabbedPanel::showAnimated() {
	_hideTimer.cancel();
	_hideAfterSlide = false;
	showStarted();
}

void TabbedPanel::showStarted() {
	if (isHidden()) {
		_selector->showStarted();
		moveByBottom();
		show();
		startShowAnimation();
	} else if (_hiding) {
		startOpacityAnimation(false);
	}
}

bool TabbedPanel::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		otherEnter();
	} else if (e->type() == QEvent::Leave) {
		otherLeave();
	} else if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton/* && !dynamic_cast<StickerPan*>(obj)*/) {
		if (isHidden() || _hiding || _hideAfterSlide) {
			showAnimated();
		} else {
			hideAnimated();
		}
	}
	return false;
}

void TabbedPanel::stickersInstalled(uint64 setId) {
	_selector->stickersInstalled(setId);
	if (isHidden()) {
		moveByBottom();
		startShowAnimation();
		show();
	}
	showChildren();
	showAnimated();
}

void TabbedPanel::setInlineQueryPeer(PeerData *peer) {
	_selector->setInlineQueryPeer(peer);
}

style::margins TabbedPanel::innerPadding() const {
	return st::emojiPanMargins;
}

QRect TabbedPanel::innerRect() const {
	return rect().marginsRemoved(innerPadding());
}

QRect TabbedPanel::horizontalRect() const {
	return innerRect().marginsRemoved(style::margins(0, st::buttonRadius, 0, st::buttonRadius));
}

QRect TabbedPanel::verticalRect() const {
	return innerRect().marginsRemoved(style::margins(st::buttonRadius, 0, st::buttonRadius, 0));
}

void TabbedPanel::onCheckForHide() {
	if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
		_hideTimer.callOnce(kDelayedHideTimeoutMs);
	}
}

bool TabbedPanel::overlaps(const QRect &globalRect) const {
	if (isHidden() || !_cache.isNull()) return false;

	auto testRect = QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size());
	auto inner = rect().marginsRemoved(st::emojiPanMargins);
	return inner.marginsRemoved(QMargins(st::buttonRadius, 0, st::buttonRadius, 0)).contains(testRect)
		|| inner.marginsRemoved(QMargins(0, st::buttonRadius, 0, st::buttonRadius)).contains(testRect);
}

} // namespace ChatHelpers
