/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_results_widget.h"

#include "data/data_user.h"
#include "data/data_session.h"
#include "inline_bots/inline_bot_result.h"
#include "inline_bots/inline_results_inner.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/scroll_area.h"
#include "ui/image/image_prepare.h"
#include "ui/cached_round_corners.h"
#include "styles/style_chat_helpers.h"

namespace InlineBots {
namespace Layout {
namespace {

constexpr auto kInlineBotRequestDelay = 400;

} // namespace

Widget::Widget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _api(&_controller->session().mtp())
, _contentMaxHeight(st::emojiPanMaxHeight)
, _contentHeight(_contentMaxHeight)
, _scroll(this, st::inlineBotsScroll)
, _inlineRequestTimer([=] { onInlineRequest(); }) {
	resize(QRect(0, 0, st::emojiPanWidth, _contentHeight).marginsAdded(innerPadding()).size());
	_width = width();
	_height = height();

	_scroll->resize(st::emojiPanWidth - st::buttonRadius, _contentHeight);

	_scroll->move(verticalRect().topLeft());
	_inner = _scroll->setOwnedWidget(object_ptr<Inner>(this, controller));

	_inner->moveToLeft(0, 0, _scroll->width());

	connect(
		_scroll,
		&Ui::ScrollArea::scrolled,
		this,
		&InlineBots::Layout::Widget::onScroll);

	_inner->inlineRowsCleared(
	) | rpl::start_with_next([=] {
		hideAnimated();
		_inner->clearInlineRowsPanel();
	}, lifetime());

	macWindowDeactivateEvents(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=] {
		leaveEvent(nullptr);
	}, lifetime());

	// Inner widget has OpaquePaintEvent attribute so it doesn't repaint on scroll.
	// But we should force it to repaint so that GIFs will continue to animate without update() calls.
	// We do that by creating a transparent widget above our _inner.
	auto forceRepaintOnScroll = object_ptr<TWidget>(this);
	forceRepaintOnScroll->setGeometry(innerRect().x() + st::buttonRadius, innerRect().y() + st::buttonRadius, st::buttonRadius, st::buttonRadius);
	forceRepaintOnScroll->setAttribute(Qt::WA_TransparentForMouseEvents);
	forceRepaintOnScroll->show();

	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void Widget::moveBottom(int bottom) {
	_bottom = bottom;
	updateContentHeight();
}

void Widget::updateContentHeight() {
	auto addedHeight = innerPadding().top() + innerPadding().bottom();
	auto wantedContentHeight = qRound(st::emojiPanHeightRatio * _bottom) - addedHeight;
	auto contentHeight = snap(wantedContentHeight, st::inlineResultsMinHeight, st::inlineResultsMaxHeight);
	accumulate_min(contentHeight, _bottom - addedHeight);
	accumulate_min(contentHeight, _contentMaxHeight);
	auto resultTop = _bottom - addedHeight - contentHeight;
	if (contentHeight == _contentHeight) {
		move(x(), resultTop);
		return;
	}

	auto was = _contentHeight;
	_contentHeight = contentHeight;

	resize(QRect(0, 0, innerRect().width(), _contentHeight).marginsAdded(innerPadding()).size());
	_height = height();
	moveToLeft(0, resultTop);

	if (was > _contentHeight) {
		_scroll->resize(_scroll->width(), _contentHeight);
		auto scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _contentHeight);
	} else {
		auto scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _contentHeight);
		_scroll->resize(_scroll->width(), _contentHeight);
	}

	update();
}

void Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto opacityAnimating = _a_opacity.animating();

	auto showAnimating = _a_show.animating();
	if (_showAnimation && !showAnimating) {
		_showAnimation.reset();
		if (!opacityAnimating) {
			showChildren();
		}
	}

	if (showAnimating) {
		Assert(_showAnimation != nullptr);
		if (auto opacity = _a_opacity.value(_hiding ? 0. : 1.)) {
			_showAnimation->paintFrame(p, 0, 0, width(), _a_show.value(1.), opacity);
		}
	} else if (opacityAnimating) {
		p.setOpacity(_a_opacity.value(_hiding ? 0. : 1.));
		p.drawPixmap(0, 0, _cache);
	} else if (_hiding || isHidden()) {
		hideFinished();
	} else {
		if (!_cache.isNull()) _cache = QPixmap();
		if (!_inPanelGrab) Ui::Shadow::paint(p, innerRect(), width(), st::emojiPanAnimation.shadow);
		paintContent(p);
	}
}

void Widget::paintContent(Painter &p) {
	auto inner = innerRect();
	Ui::FillRoundRect(p, inner, st::emojiPanBg, ImageRoundRadius::Small, RectPart::FullTop | RectPart::FullBottom);

	auto horizontal = horizontalRect();
	auto sidesTop = horizontal.y();
	auto sidesHeight = horizontal.height();
	p.fillRect(myrtlrect(inner.x() + inner.width() - st::emojiScroll.width, sidesTop, st::emojiScroll.width, sidesHeight), st::emojiPanBg);
	p.fillRect(myrtlrect(inner.x(), sidesTop, st::buttonRadius, sidesHeight), st::emojiPanBg);
}

void Widget::moveByBottom() {
	updateContentHeight();
}

void Widget::hideFast() {
	if (isHidden()) return;

	_hiding = false;
	_a_opacity.stop();
	hideFinished();
}

void Widget::opacityAnimationCallback() {
	update();
	if (!_a_opacity.animating()) {
		if (_hiding) {
			_hiding = false;
			hideFinished();
		} else if (!_a_show.animating()) {
			showChildren();
		}
	}
}

void Widget::prepareCache() {
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

void Widget::startOpacityAnimation(bool hiding) {
	_hiding = false;
	prepareCache();
	_hiding = hiding;
	hideChildren();
	_a_opacity.start([this] { opacityAnimationCallback(); }, _hiding ? 1. : 0., _hiding ? 0. : 1., st::emojiPanDuration);
}

void Widget::startShowAnimation() {
	if (!_a_show.animating()) {
		auto cache = base::take(_cache);
		auto opacityAnimation = base::take(_a_opacity);
		showChildren();
		auto image = grabForPanelAnimation();
		_a_opacity = base::take(opacityAnimation);
		_cache = base::take(_cache);

		_showAnimation = std::make_unique<Ui::PanelAnimation>(st::emojiPanAnimation, Ui::PanelAnimation::Origin::BottomLeft);
		auto inner = rect().marginsRemoved(st::emojiPanMargins);
		_showAnimation->setFinalImage(std::move(image), QRect(inner.topLeft() * cIntRetinaFactor(), inner.size() * cIntRetinaFactor()));
		_showAnimation->setCornerMasks(Images::CornersMask(ImageRoundRadius::Small));
		_showAnimation->start();
	}
	hideChildren();
	_a_show.start([this] { update(); }, 0., 1., st::emojiPanShowDuration);
}

QImage Widget::grabForPanelAnimation() {
	Ui::SendPendingMoveResizeEvents(this);
	auto result = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	_inPanelGrab = true;
	render(&result);
	_inPanelGrab = false;
	return result;
}

void Widget::setResultSelectedCallback(Fn<void(ResultSelected)> callback) {
	_inner->setResultSelectedCallback(std::move(callback));
}

void Widget::setSendMenuType(Fn<SendMenu::Type()> &&callback) {
	_inner->setSendMenuType(std::move(callback));
}

void Widget::setCurrentDialogsEntryState(Dialogs::EntryState state) {
	_inner->setCurrentDialogsEntryState(state);
}

void Widget::hideAnimated() {
	if (isHidden()) return;
	if (_hiding) return;

	startOpacityAnimation(true);
}

Widget::~Widget() = default;

void Widget::hideFinished() {
	hide();
	_controller->disableGifPauseReason(
		Window::GifPauseReason::InlineResults);

	_inner->hideFinished();
	_a_show.stop();
	_showAnimation.reset();
	_cache = QPixmap();
	_horizontal = false;
	_hiding = false;

	_scroll->scrollToY(0);
}

void Widget::showAnimated() {
	showStarted();
}

void Widget::showStarted() {
	if (isHidden()) {
		recountContentMaxHeight();
		_inner->preloadImages();
		show();
		_controller->enableGifPauseReason(
			Window::GifPauseReason::InlineResults);
		startShowAnimation();
	} else if (_hiding) {
		startOpacityAnimation(false);
	}
}

void Widget::onScroll() {
	auto st = _scroll->scrollTop();
	if (st + _scroll->height() > _scroll->scrollTopMax()) {
		onInlineRequest();
	}
	_inner->setVisibleTopBottom(st, st + _scroll->height());
}

style::margins Widget::innerPadding() const {
	return st::emojiPanMargins;
}

QRect Widget::innerRect() const {
	return rect().marginsRemoved(innerPadding());
}

QRect Widget::horizontalRect() const {
	return innerRect().marginsRemoved(style::margins(0, st::buttonRadius, 0, st::buttonRadius));
}

QRect Widget::verticalRect() const {
	return innerRect().marginsRemoved(style::margins(st::buttonRadius, 0, st::buttonRadius, 0));
}

void Widget::clearInlineBot() {
	inlineBotChanged();
}

bool Widget::overlaps(const QRect &globalRect) const {
	if (isHidden() || !_cache.isNull()) return false;

	auto testRect = QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size());
	auto inner = rect().marginsRemoved(st::emojiPanMargins);
	return inner.marginsRemoved(QMargins(st::buttonRadius, 0, st::buttonRadius, 0)).contains(testRect)
		|| inner.marginsRemoved(QMargins(0, st::buttonRadius, 0, st::buttonRadius)).contains(testRect);
}

void Widget::inlineBotChanged() {
	if (!_inlineBot) {
		return;
	}

	if (!isHidden() && !_hiding) {
		hideAnimated();
	}

	_api.request(base::take(_inlineRequestId)).cancel();
	_inlineQuery = _inlineNextQuery = _inlineNextOffset = QString();
	_inlineBot = nullptr;
	_inlineCache.clear();
	_inner->inlineBotChanged();
	_inner->hideInlineRowsPanel();

	_requesting.fire(false);
}

void Widget::inlineResultsDone(const MTPmessages_BotResults &result) {
	_inlineRequestId = 0;
	_requesting.fire(false);

	auto it = _inlineCache.find(_inlineQuery);
	auto adding = (it != _inlineCache.cend());
	if (result.type() == mtpc_messages_botResults) {
		auto &d = result.c_messages_botResults();
		_controller->session().data().processUsers(d.vusers());

		auto &v = d.vresults().v;
		auto queryId = d.vquery_id().v;

		if (it == _inlineCache.cend()) {
			it = _inlineCache.emplace(
				_inlineQuery,
				std::make_unique<CacheEntry>()).first;
		}
		auto entry = it->second.get();
		entry->nextOffset = qs(d.vnext_offset().value_or_empty());
		if (const auto switchPm = d.vswitch_pm()) {
			switchPm->match([&](const MTPDinlineBotSwitchPM &data) {
				entry->switchPmText = qs(data.vtext());
				entry->switchPmStartToken = qs(data.vstart_param());
			});
		}

		if (auto count = v.size()) {
			entry->results.reserve(entry->results.size() + count);
		}
		auto added = 0;
		for (const auto &res : v) {
			auto result = InlineBots::Result::Create(
				&_controller->session(),
				queryId,
				res);
			if (result) {
				++added;
				entry->results.push_back(std::move(result));
			}
		}

		if (!added) {
			entry->nextOffset = QString();
		}
	} else if (adding) {
		it->second->nextOffset = QString();
	}

	if (!showInlineRows(!adding)) {
		it->second->nextOffset = QString();
	}
	onScroll();
}

void Widget::queryInlineBot(UserData *bot, PeerData *peer, QString query) {
	bool force = false;
	_inlineQueryPeer = peer;
	if (bot != _inlineBot) {
		inlineBotChanged();
		_inlineBot = bot;
		force = true;
	}

	if (_inlineQuery != query || force) {
		if (_inlineRequestId) {
			_api.request(_inlineRequestId).cancel();
			_inlineRequestId = 0;
			_requesting.fire(false);
		}
		if (_inlineCache.find(query) != _inlineCache.cend()) {
			_inlineRequestTimer.cancel();
			_inlineQuery = _inlineNextQuery = query;
			showInlineRows(true);
		} else {
			_inlineNextQuery = query;
			_inlineRequestTimer.callOnce(kInlineBotRequestDelay);
		}
	}
}

void Widget::onInlineRequest() {
	if (_inlineRequestId || !_inlineBot || !_inlineQueryPeer) return;
	_inlineQuery = _inlineNextQuery;

	QString nextOffset;
	auto it = _inlineCache.find(_inlineQuery);
	if (it != _inlineCache.cend()) {
		nextOffset = it->second->nextOffset;
		if (nextOffset.isEmpty()) {
			return;
		}
	}
	_requesting.fire(true);
	_inlineRequestId = _api.request(MTPmessages_GetInlineBotResults(
		MTP_flags(0),
		_inlineBot->inputUser,
		_inlineQueryPeer->input,
		MTPInputGeoPoint(),
		MTP_string(_inlineQuery),
		MTP_string(nextOffset)
	)).done([=](const MTPmessages_BotResults &result) {
		inlineResultsDone(result);
	}).fail([=](const RPCError &error) {
		// show error?
		_requesting.fire(false);
		_inlineRequestId = 0;
	}).handleAllErrors().send();
}

bool Widget::refreshInlineRows(int *added) {
	auto it = _inlineCache.find(_inlineQuery);
	const CacheEntry *entry = nullptr;
	if (it != _inlineCache.cend()) {
		if (!it->second->results.empty() || !it->second->switchPmText.isEmpty()) {
			entry = it->second.get();
		}
		_inlineNextOffset = it->second->nextOffset;
	}
	if (!entry) prepareCache();
	auto result = _inner->refreshInlineRows(_inlineQueryPeer, _inlineBot, entry, false);
	if (added) *added = result;
	return (entry != nullptr);
}

int Widget::showInlineRows(bool newResults) {
	auto added = 0;
	auto clear = !refreshInlineRows(&added);
	if (newResults) {
		_scroll->scrollToY(0);
	}

	auto hidden = isHidden();
	if (!hidden && !clear) {
		recountContentMaxHeight();
	}
	if (clear) {
		if (!hidden) {
			hideAnimated();
		} else if (!_hiding) {
			_cache = QPixmap(); // clear after refreshInlineRows()
		}
	} else {
		if (hidden || _hiding) {
			showAnimated();
		}
	}

	return added;
}

void Widget::recountContentMaxHeight() {
	_contentMaxHeight = _inner->countHeight();
	updateContentHeight();
}

} // namespace Layout
} // namespace InlineBots
