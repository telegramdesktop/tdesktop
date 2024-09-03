/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_view_swipe.h"

#include "base/event_filter.h"
#include "base/platform/base_platform_haptic.h"
#include "history/history_view_swipe_data.h"
#include "ui/chat/chat_style.h"
#include "ui/ui_utility.h"
#include "ui/widgets/scroll_area.h"

#include <QtWidgets/QApplication>

namespace HistoryView {

void SetupSwipeHandler(
		not_null<Ui::RpWidget*> widget,
		not_null<Ui::ScrollArea*> scroll,
		Fn<void(ChatPaintGestureHorizontalData)> update,
		Fn<SwipeHandlerFinishData(int)> generateFinishByTop) {
	constexpr auto kThresholdWidth = 50;
	const auto threshold = style::ConvertFloatScale(kThresholdWidth);
	struct State {
		base::unique_qptr<QObject> filter;
		Ui::Animations::Simple animationReach;
		Ui::Animations::Simple animationEnd;
		SwipeHandlerFinishData finishByTopData;
		std::optional<Qt::Orientation> orientation;
		QPointF startAt;
		QPointF lastAt;
		int cursorTop = 0;
		bool reached = false;

		rpl::lifetime lifetime;
	};
	const auto state = widget->lifetime().make_state<State>();
	const auto updateRatio = [=](float64 ratio) {
		update({
			.ratio = std::clamp(ratio, 0., 1.5),
			.reachRatio = state->animationReach.value(0.),
			.translation = (-std::clamp(ratio, 0., 1.5) * threshold),
			.msgBareId = state->finishByTopData.msgBareId,
			.cursorTop = state->cursorTop,
		});
	};
	const auto setOrientation = [=](const std::optional<Qt::Orientation> &o) {
		state->orientation = o;
		const auto isHorizontal = o.value_or(Qt::Vertical) == Qt::Horizontal;
		scroll->viewport()->setAttribute(
			Qt::WA_AcceptTouchEvents,
			!isHorizontal);
		scroll->disableScroll(isHorizontal);
	};
	const auto processEnd = [=](QTouchEvent *t) {
		if (state->orientation) {
			if ((*state->orientation) == Qt::Horizontal) {
				if (t && t->touchPoints().size() > 0) {
					state->lastAt = t->touchPoints().at(0).pos();
				}
				const auto delta = state->startAt - state->lastAt;
				const auto ratio = delta.x() / threshold;
				if ((ratio >= 1) && state->finishByTopData.callback) {
					Ui::PostponeCall(
						widget,
						state->finishByTopData.callback);
				}
				state->animationEnd.stop();
				state->animationEnd.start(
					updateRatio,
					ratio,
					0.,
					st::slideWrapDuration);
			}
		}
		setOrientation(std::nullopt);
		state->startAt = QPointF();
		state->reached = false;
	};
	scroll->scrolls() | rpl::start_with_next([=] {
		processEnd(nullptr);
	}, state->lifetime);
	const auto animationReachCallback = [=] {
		updateRatio((state->startAt - state->lastAt).x() / threshold);
	};
	const auto filter = [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Leave && state->orientation) {
			processEnd(nullptr);
		}
		if (e->type() == QEvent::MouseMove && state->orientation) {
			const auto m = static_cast<QMouseEvent*>(e.get());
			if (std::abs(m->pos().y() - state->cursorTop)
					> QApplication::startDragDistance()) {
				processEnd(nullptr);
			}
		}
		if (e->type() == QEvent::TouchBegin
				|| e->type() == QEvent::TouchUpdate
				|| e->type() == QEvent::TouchEnd
				|| e->type() == QEvent::TouchCancel) {
			const auto t = static_cast<QTouchEvent*>(e.get());
			const auto &touches = t->touchPoints();
			const auto anyReleased = (touches.size() == 2)
				? ((touches.at(0).state() & Qt::TouchPointReleased)
					+ (touches.at(1).state() & Qt::TouchPointReleased))
				: (touches.size() == 1)
				? (touches.at(0).state() & Qt::TouchPointReleased)
				: 0;
			if (touches.size() == 2) {
				if ((e->type() == QEvent::TouchBegin)
					|| (e->type() == QEvent::TouchUpdate)) {
					if (state->startAt.isNull()) {
						state->startAt = touches.at(0).pos();
						state->cursorTop = widget->mapFromGlobal(
							QCursor::pos()).y();
						state->finishByTopData = generateFinishByTop(
							state->cursorTop);
						if (!state->finishByTopData.callback) {
							setOrientation(Qt::Vertical);
						}
					} else if (state->orientation) {
						if ((*state->orientation) == Qt::Horizontal) {
							state->lastAt = touches.at(0).pos();
							const auto delta = state->startAt - state->lastAt;
							const auto ratio = delta.x() / threshold;
							updateRatio(ratio);
							constexpr auto kResetReachedOn = 0.95;
							constexpr auto kBounceDuration = crl::time(500);
							if (!state->reached && ratio >= 1.) {
								state->reached = true;
								state->animationReach.stop();
								state->animationReach.start(
									animationReachCallback,
									0.,
									1.,
									kBounceDuration);
								base::Platform::Haptic();
							} else if (state->reached
									&& ratio < kResetReachedOn) {
								state->reached = false;
							}
						}
					} else {
						state->lastAt = touches.at(0).pos();
						const auto delta = state->startAt - state->lastAt;
						const auto diffXtoY = std::abs(delta.x())
							- std::abs(delta.y());
						if (diffXtoY > 0) {
							setOrientation(Qt::Horizontal);
						} else if (diffXtoY < 0) {
							setOrientation(Qt::Vertical);
						} else {
							setOrientation(std::nullopt);
						}
					}
				}
			}
			if ((e->type() == QEvent::TouchEnd)
					|| touches.empty()
					|| anyReleased
					|| (touches.size() > 2)) {
				processEnd(t);
			}
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	};
	state->filter = base::make_unique_q<QObject>(
		base::install_event_filter(widget, filter));
}

} // namespace HistoryView
