/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/thanos_effect_controller.h"

#include "ui/effects/thanos_effect.h"
#include "ui/widgets/scroll_area.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "styles/style_basic.h"

namespace Ui {
namespace {

constexpr auto kBaseDuration = crl::time(400);
constexpr auto kPerPixelDuration = float64(0.15);
constexpr auto kMaxDuration = crl::time(600);

} // namespace

ThanosEffectController::ThanosEffectController(
	not_null<Main::Session*> session,
	Delegate delegate,
	rpl::lifetime &lifetime)
: _session(session)
, _delegate(std::move(delegate)) {
	_session->data().itemsAboutToBeDestroyed(
	) | rpl::on_next([=](const auto &items) {
		captureItemsBatch(items);
	}, lifetime);
}

ThanosEffectController::~ThanosEffectController() {
	_collapseAnimation.stop();
}

void ThanosEffectController::captureItemsBatch(
		const std::vector<not_null<HistoryItem*>> &items) {
	if (!ThanosEffect::Supported()) {
		return;
	}
	auto anyFound = false;
	for (const auto &item : items) {
		if (_delegate.viewForItem(item)) {
			anyFound = true;
			break;
		}
	}
	if (!anyFound) {
		return;
	}
	ensureScrollBaseline();
	for (const auto &item : items) {
		if (const auto view = _delegate.viewForItem(item)) {
			const auto top = _delegate.itemTop(view);
			const auto height = view->height();
			if (captureView(view, height, top)) {
				_preCaptured.emplace(item->fullId(), PreCapturedView{
					.height = height,
					.top = top,
				});
			}
		}
	}
}

void ThanosEffectController::clearPreCaptured() {
	_preCaptured.clear();
	_restoreScrollPending = false;
	_wasAtBottom = false;
}

void ThanosEffectController::captureOnRemoval(
		not_null<const HistoryItem*> item) {
	if (!ThanosEffect::Supported()) {
		return;
	}
	const auto view = _delegate.viewForItem(item);
	if (!view) {
		return;
	}
	if (const auto it = _preCaptured.find(item->fullId());
		it != end(_preCaptured)) {
		const auto saved = it->second;
		_preCaptured.erase(it);
		if (saved.top >= 0) {
			startCollapseAnimation(saved.height, saved.top);
		}
		return;
	}
	const auto top = _delegate.itemTop(view);
	const auto height = view->height();

	if ((!item->isRegular() && !item->isEphemeral())
		|| item->isService()
		|| top < 0
		|| height <= 0
		|| _delegate.contentWidth() <= 0) {
		return;
	}
	[[maybe_unused]] const auto dissolved = captureView(view, height, top);

	ensureScrollBaseline();
	startCollapseAnimation(height, top);
}

void ThanosEffectController::ensureScrollBaseline() {
	if (!_restoreScrollPending) {
		_savedScrollTop = _delegate.scrollTop();
		_wasAtBottom = (_savedScrollTop >= _delegate.scrollTopMax());
		_restoreScrollPending = true;
	}
}

bool ThanosEffectController::captureView(
		not_null<const HistoryView::Element*> view,
		int viewHeight,
		int viewTop) {
	const auto item = view->data();
	if ((!item->isRegular() && !item->isEphemeral()) || item->isService()) {
		return false;
	}
	if (viewTop < 0) {
		return false;
	}
	const auto viewWidth = _delegate.contentWidth();
	if (viewWidth <= 0 || viewHeight <= 0) {
		return false;
	}
	const auto visibleTop = _delegate.visibleAreaTop();
	const auto visibleBottom = _delegate.visibleAreaBottom();
	const auto visibleHeight = visibleBottom - visibleTop;
	const auto screenTop = viewTop - visibleTop;
	if (screenTop + viewHeight <= 0 || screenTop >= visibleHeight) {
		return false;
	}
	auto gapOffset = 0;
	for (const auto &gap : _renderGaps) {
		if (gap.absY >= 0 && viewTop >= gap.absY) {
			gapOffset += gap.height;
		}
	}
	const auto adjustedScreenTop = screenTop + gapOffset;
	const auto captureTop = std::clamp(-adjustedScreenTop, 0, viewHeight);
	const auto captureBottom = std::clamp(
		visibleHeight - adjustedScreenTop,
		0,
		viewHeight);
	if (captureTop >= captureBottom) {
		return false;
	}
	const auto captureHeight = captureBottom - captureTop;

	const auto dpr = style::DevicePixelRatio();
	auto image = QImage(
		QSize(viewWidth, captureHeight) * dpr,
		QImage::Format_RGBA8888_Premultiplied);
	image.setDevicePixelRatio(dpr);
	image.fill(Qt::transparent);
	{
		Painter p(&image);
		p.translate(0, -captureTop);
		auto clip = QRect(0, captureTop, viewWidth, captureHeight);
		auto context = _delegate.preparePaintContext(clip);
		const auto renderedTop = viewTop + gapOffset;
		context.translate(0, -renderedTop);
		context.clip = clip;
		context.skipSelectionCheck = true;
		context.outbg = view->hasOutLayout();
		view->draw(p, context);
	}

	const auto topLevel = _delegate.window();
	if (!topLevel) {
		return false;
	}

	if (!_thanosEffect) {
		_thanosEffect = std::make_unique<ThanosEffect>(topLevel);
	}
	_thanosEffect->setGeometry(QRect(QPoint(), topLevel->size()));
	_thanosEffect->raise();

	const auto scroll = _delegate.scrollWidget();
	const auto globalPos = scroll->mapTo(
		topLevel,
		QPoint(0, adjustedScreenTop + captureTop));
	_thanosEffect->addItem(
		std::move(image),
		QRect(globalPos, QSize(viewWidth, captureHeight)));
	return true;
}

void ThanosEffectController::startCollapseAnimation(
		int height,
		int itemTop) {
	if (height <= 0) {
		return;
	}

	const auto scrollTop = _delegate.scrollTop();
	const auto scrollBottom = scrollTop + _delegate.scrollWidget()->height();

	if (itemTop >= scrollBottom) {
		return;
	}

	if (_collapseAnimation.animating()) {
		for (auto &gap : _collapseGaps) {
			gap.startHeight = gap.currentHeight;
		}
	}

	auto merged = false;
	for (auto &gap : _collapseGaps) {
		if (gap.absY + gap.originalHeight == itemTop
			|| (gap.absY <= itemTop
				&& itemTop <= gap.absY + gap.originalHeight)) {
			gap.startHeight += height;
			gap.currentHeight += height;
			gap.originalHeight += height;
			merged = true;
			break;
		}
		if (itemTop + height == gap.absY) {
			gap.absY = itemTop;
			gap.startHeight += height;
			gap.currentHeight += height;
			gap.originalHeight += height;
			merged = true;
			break;
		}
	}
	if (!merged) {
		const auto it = std::lower_bound(
			_collapseGaps.begin(),
			_collapseGaps.end(),
			itemTop,
			[](const auto &gap, int top) { return gap.absY < top; });
		_collapseGaps.insert(it, {
			.absY = itemTop,
			.startHeight = height,
			.currentHeight = height,
			.originalHeight = height,
		});
	}

	syncCollapseGapsToHost();

	auto totalHeight = 0;
	for (const auto &gap : _collapseGaps) {
		totalHeight += gap.currentHeight;
	}
	const auto duration = crl::time(std::clamp(
		kBaseDuration + totalHeight * kPerPixelDuration,
		float64(kBaseDuration),
		float64(kMaxDuration)));

	_collapseAnimation.start(
		[=] { collapseAnimationCallback(); },
		0.,
		1.,
		duration,
		anim::halfSine);
}

void ThanosEffectController::collapseAnimationCallback() {
	const auto progress = _collapseAnimation.value(1.);

	auto sumOriginal = 0;
	auto sumCurrent = 0;
	for (auto &gap : _collapseGaps) {
		gap.currentHeight = anim::interpolate(gap.startHeight, 0, progress);
		sumOriginal += gap.originalHeight;
		sumCurrent += gap.currentHeight;
	}

	if (!_collapseGaps.empty()) {
		syncCollapseGapsToHost();

		if (_wasAtBottom) {
			_delegate.scrollToY(_delegate.scrollTopMax());
		} else {
			const auto collapsed = sumOriginal - sumCurrent;
			const auto target = std::max(_savedScrollTop - collapsed, 0);
			_delegate.scrollToY(target);
		}
	}

	if (!_collapseAnimation.animating()) {
		_collapseGaps.clear();
		_renderGaps.clear();
		_delegate.setCollapseGaps({});
		_collapseAnimation = {};
		_restoreScrollPending = false;
		_wasAtBottom = false;
	}
}

void ThanosEffectController::syncCollapseGapsToHost() {
	auto gaps = std::vector<CollapseGap>();
	gaps.reserve(_collapseGaps.size());
	auto cumulativeOriginal = 0;
	for (const auto &g : _collapseGaps) {
		gaps.push_back({
			.absY = g.absY - cumulativeOriginal,
			.height = g.currentHeight,
		});
		cumulativeOriginal += g.originalHeight;
	}
	_renderGaps = gaps;
	_delegate.setCollapseGaps(std::move(gaps));
}

} // namespace Ui
