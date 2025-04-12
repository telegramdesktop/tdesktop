/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/swipe_handler.h"

#include "base/debug_log.h"

#include "base/platform/base_platform_haptic.h"
#include "base/platform/base_platform_info.h"
#include "base/qt/qt_common_adapters.h"
#include "base/event_filter.h"
#include "ui/chat/chat_style.h"
#include "ui/controls/swipe_handler_data.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/scroll_area.h"
#include "styles/style_chat.h"

#include <QtWidgets/QApplication>

namespace Ui::Controls {
namespace {

constexpr auto kSwipeSlow = 0.2;

constexpr auto kMsgBareIdSwipeBack = std::numeric_limits<int64>::max() - 77;
constexpr auto kSwipedBackSpeedRatio = 0.35;

float64 InterpolationRatio(float64 from, float64 to, float64 result) {
	return (result - from) / (to - from);
};

class RatioRange final {
public:
	[[nodiscard]] float64 calcRatio(float64 value) {
		if (value < _min) {
			const auto shift = _min - value;
			_min -= shift;
			_max -= shift;
			_max = _min + 1;
		} else if (value > _max) {
			const auto shift = value - _max;
			_min += shift;
			_max += shift;
			_max = _min + 1;
		}
		return InterpolationRatio(_min, _max, value);
	}

private:
	float64 _min = 0;
	float64 _max = 1;

};

} // namespace

void SetupSwipeHandler(SwipeHandlerArgs &&args) {
	static constexpr auto kThresholdWidth = 50;
	static constexpr auto kMaxRatio = 1.5;

	const auto widget = std::move(args.widget);
	const auto scroll = std::move(args.scroll);
	const auto update = std::move(args.update);

	struct UpdateArgs {
		QPoint globalCursor;
		QPointF position;
		QPointF delta;
		bool touch = false;
	};
	struct State {
		base::unique_qptr<QObject> filter;
		Ui::Animations::Simple animationReach;
		Ui::Animations::Simple animationEnd;
		SwipeContextData data;
		SwipeHandlerFinishData finishByTopData;
		std::optional<Qt::Orientation> orientation;
		std::optional<Qt::LayoutDirection> direction;
		float64 threshold = style::ConvertFloatScale(kThresholdWidth);
		RatioRange ratioRange;
		int directionInt = 1.;
		QPointF startAt;
		QPointF delta;
		int cursorTop = 0;
		bool dontStart = false;
		bool started = false;
		bool reached = false;
		bool touch = false;

		rpl::lifetime lifetime;
	};
	auto &useLifetime = args.onLifetime
		? *(args.onLifetime)
		: args.widget->lifetime();
	const auto state = useLifetime.make_state<State>();
	if (args.dontStart) {
		std::move(
			args.dontStart
		) | rpl::start_with_next([=](bool dontStart) {
			state->dontStart = dontStart;
		}, state->lifetime);
	} else {
		v::match(scroll, [](v::null_t) {
		}, [&](const auto &scroll) {
			scroll->touchMaybePressing(
			) | rpl::start_with_next([=](bool maybePressing) {
				state->dontStart = maybePressing;
			}, state->lifetime);
		});
	}

	const auto updateRatio = [=](float64 ratio) {
		ratio = std::max(ratio, 0.);
		state->data.ratio = ratio;
		const auto overscrollRatio = std::max(ratio - 1., 0.);
		const auto translation = int(
			base::SafeRound(-std::min(ratio, 1.) * state->threshold)
		) + Ui::OverscrollFromAccumulated(int(
			base::SafeRound(-overscrollRatio * state->threshold)
		));
		state->data.msgBareId = state->finishByTopData.msgBareId;
		state->data.translation = translation
			* state->directionInt;
		state->data.cursorTop = state->cursorTop;
		update(state->data);
	};
	const auto setOrientation = [=](std::optional<Qt::Orientation> o) {
		state->orientation = o;
		const auto isHorizontal = (o == Qt::Horizontal);
		v::match(scroll, [](v::null_t) {
		}, [&](const auto &scroll) {
			if (const auto viewport = scroll->viewport()) {
				if (viewport != widget) {
					viewport->setAttribute(
						Qt::WA_AcceptTouchEvents,
						!isHorizontal);
				}
			}
			scroll->disableScroll(isHorizontal);
		});
	};
	const auto processEnd = [=](std::optional<QPointF> delta = {}) {
		if (state->orientation == Qt::Horizontal) {
			const auto rawRatio = delta.value_or(state->delta).x()
				/ state->threshold
				* state->directionInt;
			const auto ratio = std::clamp(
				state->finishByTopData.keepRatioWithinRange
					? state->ratioRange.calcRatio(rawRatio)
					: rawRatio,
				0.,
				kMaxRatio);
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
				std::min(1., ratio) * st::slideWrapDuration);
		}
		setOrientation(std::nullopt);
		state->started = false;
		state->reached = false;
		state->direction = std::nullopt;
		state->startAt = {};
		state->delta = {};
	};
	v::match(scroll, [](v::null_t) {
	}, [&](const auto &scroll) {
		scroll->scrolls() | rpl::start_with_next([=] {
			if (state->orientation != Qt::Vertical) {
				processEnd();
			}
		}, state->lifetime);
	});
	const auto animationReachCallback = [=](float64 value) {
		state->data.reachRatio = value;
		update(state->data);
	};
	const auto updateWith = [=, generateFinish = args.init](UpdateArgs args) {
		const auto fillFinishByTop = [&] {
			if (!args.delta.x()) {
				LOG(("SKIPPING fillFinishByTop."));
				return;
			}
			LOG(("SETTING DIRECTION"));
			state->direction = (args.delta.x() < 0)
				? Qt::RightToLeft
				: Qt::LeftToRight;
			state->directionInt = (state->direction == Qt::LeftToRight)
				? 1
				: -1;
			state->finishByTopData = generateFinish(
				state->cursorTop,
				*state->direction);
			state->threshold = style::ConvertFloatScale(kThresholdWidth)
				* state->finishByTopData.speedRatio;
			if (!state->finishByTopData.callback) {
				setOrientation(Qt::Vertical);
			}
		};
		if (!state->started || state->touch != args.touch) {
			LOG(("STARTING"));
			state->started = true;
			state->data.reachRatio = 0.;
			state->touch = args.touch;
			state->startAt = args.position;
			state->cursorTop = widget->mapFromGlobal(args.globalCursor).y();
			if (!state->touch) {
				// args.delta already is valid.
				fillFinishByTop();
			} else {
				// args.delta depends on state->startAt, so it's invalid.
				state->direction = std::nullopt;
			}
			state->delta = QPointF();
		} else if (!state->direction) {
			fillFinishByTop();
		} else if (!state->orientation) {
			state->delta = args.delta;
			const auto diffXtoY = std::abs(args.delta.x())
				- std::abs(args.delta.y());
			constexpr auto kOrientationThreshold = 1.;
			LOG(("SETTING ORIENTATION WITH: %1,%2, diff %3"
				).arg(args.delta.x()
				).arg(args.delta.y()
				).arg(diffXtoY));
			if (diffXtoY > kOrientationThreshold) {
				if (!state->dontStart) {
					setOrientation(Qt::Horizontal);
				}
			} else if (diffXtoY < -kOrientationThreshold) {
				setOrientation(Qt::Vertical);
			} else {
				setOrientation(std::nullopt);
			}
		} else if (*state->orientation == Qt::Horizontal) {
			state->delta = args.delta;
			const auto rawRatio = 0
				+ args.delta.x() * state->directionInt / state->threshold;
			const auto ratio = state->finishByTopData.keepRatioWithinRange
				? state->ratioRange.calcRatio(rawRatio)
				: rawRatio;
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
					state->finishByTopData.reachRatioDuration
						? state->finishByTopData.reachRatioDuration
						: kBounceDuration);
				base::Platform::Haptic();
			} else if (state->reached
				&& ratio < kResetReachedOn) {
				if (state->finishByTopData.provideReachOutRatio) {
					state->animationReach.stop();
					state->animationReach.start(
						animationReachCallback,
						1.,
						0.,
						state->finishByTopData.reachRatioDuration
							? state->finishByTopData.reachRatioDuration
							: kBounceDuration);
				}
				state->reached = false;
			}
		}
	};
	const auto filter = [=](not_null<QEvent*> e) {
		if (!widget->testAttribute(Qt::WA_AcceptTouchEvents)) {
			[[maybe_unused]] int a = 0;
		}
		const auto type = e->type();
		switch (type) {
		case QEvent::Leave: {
			if (state->orientation == Qt::Horizontal) {
				processEnd();
			}
		} break;
		case QEvent::MouseMove: {
			if (state->orientation == Qt::Horizontal) {
				const auto m = static_cast<QMouseEvent*>(e.get());
				if (std::abs(m->pos().y() - state->cursorTop)
					> QApplication::startDragDistance()) {
					processEnd();
				}
			}
		} break;
		case QEvent::TouchBegin:
		case QEvent::TouchUpdate:
		case QEvent::TouchEnd:
		case QEvent::TouchCancel: {
			const auto t = static_cast<QTouchEvent*>(e.get());
			const auto touchscreen = t->device()
				&& (t->device()->type() == base::TouchDevice::TouchScreen);
			if (!touchscreen && type != QEvent::TouchCancel) {
				break;
			} else if (type == QEvent::TouchBegin) {
				// Reset state in case we lost some TouchEnd.
				processEnd();
			}
			const auto &touches = t->touchPoints();
			const auto released = [&](int index) {
				return (touches.size() > index)
					&& (int(touches.at(index).state())
						& int(Qt::TouchPointReleased));
			};
			const auto cancel = released(0)
				|| released(1)
				|| (touches.size() != (touchscreen ? 1 : 2))
				|| (type == QEvent::TouchEnd)
				|| (type == QEvent::TouchCancel);
			if (cancel) {
				processEnd(touches.empty()
					? std::optional<QPointF>()
					: (state->startAt - touches[0].pos()));
			} else {
				const auto args = UpdateArgs{
					.globalCursor = (touchscreen
						? touches[0].screenPos().toPoint()
						: QCursor::pos()),
					.position = touches[0].pos(),
					.delta = state->startAt - touches[0].pos(),
					.touch = true,
				};
				LOG(("ORIENTATION UPDATING WITH: %1, %2").arg(args.delta.x()).arg(args.delta.y()));
				updateWith(args);
			}
			LOG(("ORIENTATION: %1").arg(!state->orientation ? "none" : (state->orientation == Qt::Horizontal) ? "horizontal" : "vertical"));
			return (touchscreen && state->orientation != Qt::Horizontal)
				? base::EventFilterResult::Continue
				: base::EventFilterResult::Cancel;
		} break;
		case QEvent::Wheel: {
			const auto w = static_cast<QWheelEvent*>(e.get());
			const auto phase = w->phase();
			if (phase == Qt::NoScrollPhase) {
				break;
			} else if (phase == Qt::ScrollBegin) {
				// Reset state in case we lost some TouchEnd.
				processEnd();
			}
			const auto cancel = w->buttons()
				|| (phase == Qt::ScrollEnd)
				|| (phase == Qt::ScrollMomentum);
			if (cancel) {
				processEnd();
			} else {
				const auto invert = (w->inverted() ? -1 : 1);
				const auto delta = Ui::ScrollDeltaF(w) * invert;
				updateWith({
					.globalCursor = w->globalPosition().toPoint(),
					.position = QPointF(),
					.delta = state->delta + delta * kSwipeSlow,
					.touch = false,
				});
			}
		} break;
		}
		return base::EventFilterResult::Continue;
	};
	widget->setAttribute(Qt::WA_AcceptTouchEvents);
	state->filter = base::unique_qptr<QObject>(
		base::install_event_filter(widget, filter));
}

SwipeBackResult SetupSwipeBack(
		not_null<Ui::RpWidget*> widget,
		Fn<std::pair<QColor, QColor>()> colors,
		bool mirrored,
		bool iconMirrored) {
	struct State {
		base::unique_qptr<Ui::RpWidget> back;
		SwipeContextData data;
	};

	constexpr auto kMaxInnerOffset = 0.5;
	constexpr auto kMaxOuterOffset = 0.8;
	constexpr auto kIdealSize = 100;
	const auto maxOffset = st::swipeBackSize * kMaxInnerOffset;
	const auto sizeRatio = st::swipeBackSize
		/ style::ConvertFloatScale(kIdealSize);

	auto lifetime = rpl::lifetime();
	const auto state = lifetime.make_state<State>();

	const auto paintCallback = [=] {
		const auto [bg, fg] = colors();
		const auto arrowPen = QPen(
			fg,
			st::lineWidth * 3 * sizeRatio,
			Qt::SolidLine,
			Qt::RoundCap);
		return [=] {
			auto p = QPainter(state->back);

			constexpr auto kBouncePart = 0.25;
			constexpr auto kStrokeWidth = 2.;
			constexpr auto kWaveWidth = 10.;
			const auto ratio = std::min(state->data.ratio, 1.);
			const auto reachRatio = state->data.reachRatio;
			const auto rect = state->back->rect()
				- Margins(state->back->width() / 4);
			const auto center = rect::center(rect);
			const auto strokeWidth = style::ConvertFloatScale(kStrokeWidth)
				* sizeRatio;

			const auto reachScale = std::clamp(
				(reachRatio > kBouncePart)
					? (kBouncePart * 2 - reachRatio)
					: reachRatio,
				0.,
				1.);
			auto pen = QPen(bg);
			pen.setWidthF(strokeWidth - (1. * (reachScale / kBouncePart)));
			const auto arcRect = rect - Margins(strokeWidth);
			auto hq = PainterHighQualityEnabler(p);
			p.setOpacity(ratio);
			if (reachScale || mirrored) {
				const auto scale = (1. + 1. * reachScale);
				p.translate(center);
				p.scale(scale * (mirrored ? -1 : 1), scale);
				p.translate(-center);
			}
			{
				p.setPen(Qt::NoPen);
				p.setBrush(bg);
				p.drawEllipse(rect);
				p.drawEllipse(rect);
				p.setPen(arrowPen);
				p.setBrush(Qt::NoBrush);
				const auto halfSize = rect.width() / 2;
				const auto arrowSize = halfSize / 2;
				const auto arrowHalf = arrowSize / 2;
				const auto arrowX = st::swipeBackSize / 8
					+ rect.x()
					+ halfSize;
				const auto arrowY = rect.y() + halfSize;

				auto arrowPath = QPainterPath();
				const auto direction = iconMirrored ? -1 : 1;

				arrowPath.moveTo(arrowX + direction * arrowSize, arrowY);
				arrowPath.lineTo(arrowX, arrowY);
				arrowPath.lineTo(
					arrowX + direction * arrowHalf,
					arrowY - arrowHalf);
				arrowPath.moveTo(arrowX, arrowY);
				arrowPath.lineTo(
					arrowX + direction * arrowHalf,
					arrowY + arrowHalf);
				arrowPath.translate(-direction * arrowHalf, 0);
				p.drawPath(arrowPath);
			}
			if (reachRatio) {
				p.setPen(pen);
				p.setBrush(Qt::NoBrush);
				const auto w = style::ConvertFloatScale(kWaveWidth)
					* sizeRatio;
				p.setOpacity(ratio - reachRatio);
				p.drawArc(
					arcRect + Margins(reachRatio * reachRatio * w),
					arc::kQuarterLength,
					arc::kFullLength);
			}
		};
	};

	const auto callback = ([=](SwipeContextData data) {
		const auto ratio = std::min(1.0, data.ratio);
		state->data = std::move(data);
		if (ratio > 0) {
			if (!state->back) {
				state->back = base::make_unique_q<Ui::RpWidget>(widget);
				const auto raw = state->back.get();
				raw->paintRequest(
				) | rpl::start_with_next(paintCallback(), raw->lifetime());
				raw->setAttribute(Qt::WA_TransparentForMouseEvents);
				raw->resize(Size(st::swipeBackSize));
				raw->show();
				raw->raise();
			}
			if (!mirrored) {
				state->back->moveToLeft(
					anim::interpolate(
						-st::swipeBackSize * kMaxOuterOffset,
						maxOffset - st::swipeBackSize,
						ratio),
					(widget->height() - state->back->height()) / 2);
			} else {
				state->back->moveToLeft(
					anim::interpolate(
						widget->width() + st::swipeBackSize * kMaxOuterOffset,
						widget->width() - maxOffset,
						ratio),
					(widget->height() - state->back->height()) / 2);
			}
			state->back->update();
		} else if (state->back) {
			state->back = nullptr;
		}
	});
	return { std::move(lifetime), std::move(callback) };
}

SwipeHandlerFinishData DefaultSwipeBackHandlerFinishData(
		Fn<void(void)> callback) {
	return {
		.callback = std::move(callback),
		.msgBareId = kMsgBareIdSwipeBack,
		.speedRatio = kSwipedBackSpeedRatio,
		.keepRatioWithinRange = true,
	};
}

} // namespace Ui::Controls
