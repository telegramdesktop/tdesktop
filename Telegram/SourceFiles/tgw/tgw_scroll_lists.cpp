/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_scroll.h"

#include "base/event_filter.h"
#include "data/data_session.h"
#include "history/admin_log/history_admin_log_inner.h"
#include "history/view/media/history_view_html_widget.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h"
#include "history/history.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "history/history_widget.h"
#include "main/main_session.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/scroll_area.h"
#include "ui/ui_utility.h"

#include <QtWidgets/QScrollBar>

namespace Tgw {
namespace {

using HistoryView::Element;
using HistoryView::ElementDelegate;

constexpr auto kUnpositioned = uint32(1);

enum class Edge : uchar {
	Top,
	Bottom,
	Below,
};

enum class Wheel : uchar {
	Ignored,
	Scrolls,
	CrossAxisDecides,
};

struct OwnerState {
	uint32 epoch = 0;
	uint32 consumed = 0;
	uint32 inputs = 0;
};

using OwnerStates = base::flat_map<
	not_null<const ElementDelegate*>,
	OwnerState>;

struct Laid {
	not_null<Element*> view;
	HeightLayout consumed;
	QRect bubble;
	int top = 0;
	int height = 0;
};

struct Kept {
	not_null<Element*> view;
	int top = 0;
	int target = 0;
	int shift = 0;
};

struct ExpansionLayout {
	std::vector<std::optional<int>> cards;
	int range = 0;
};

struct ExpansionState {
	std::vector<ScrollBubble> above;
	std::vector<ExpansionLayout> layouts;
	int scrollTop = 0;
};

using ExpansionStates = base::flat_map<
	not_null<const AdminLog::InnerWidget*>,
	ExpansionState>;

[[nodiscard]] OwnerStates &Owners() {
	static auto result = OwnerStates();
	return result;
}

[[nodiscard]] ExpansionStates &Expansions() {
	static auto result = ExpansionStates();
	return result;
}

[[nodiscard]] ExpansionState *ExpansionOf(
		not_null<const AdminLog::InnerWidget*> list) {
	auto &expansions = Expansions();
	const auto i = expansions.find(list);
	return (i != end(expansions)) ? &i->second : nullptr;
}

void Advance(
		not_null<const ElementDelegate*> owner,
		rpl::lifetime &lifetime) {
	auto &owners = Owners();
	const auto i = owners.find(owner);
	if (i != end(owners)) {
		i->second.epoch = std::max(i->second.epoch + 1, kUnpositioned + 1);
		return;
	}
	owners.emplace(owner, OwnerState{ .epoch = kUnpositioned + 1 });
	lifetime.add([=] {
		Owners().remove(owner);
	});
}

void Advance(History *history) {
	if (history) {
		Advance(
			history->delegateMixin()->delegate(),
			history->session().lifetime());
	}
}

template <typename Method>
void EnumerateDown(History *migrated, History *history, Method method) {
	for (const auto owner : { migrated, history }) {
		if (!owner) {
			continue;
		}
		for (const auto &block : owner->blocks) {
			for (const auto &view : block->messages) {
				if (!method(view.get())) {
					return;
				}
			}
		}
	}
}

template <typename Method>
void EnumerateDownFrom(
		History *migrated,
		History *history,
		not_null<Element*> from,
		Method method) {
	for (auto view = from->nextInBlocks(); view; view = view->nextInBlocks()) {
		if (!method(view)) {
			return;
		}
	}
	if (migrated && (from->history() == migrated)) {
		EnumerateDown(nullptr, history, method);
	}
}

template <typename Method>
void EnumerateUp(History *migrated, History *history, Method method) {
	for (const auto owner : { history, migrated }) {
		if (!owner) {
			continue;
		}
		for (const auto &block : ranges::views::reverse(owner->blocks)) {
			const auto &messages = block->messages;
			for (const auto &view : ranges::views::reverse(messages)) {
				if (!method(view.get())) {
					return;
				}
			}
		}
	}
}

[[nodiscard]] bool Capturable(
		HistoryInner *list,
		History *history,
		bool inited,
		not_null<Ui::ElasticScroll*> scroll) {
	return list
		&& history
		&& inited
		&& (scroll->scrollTopMax() > 0);
}

[[nodiscard]] bool Scrollable(
		int top,
		int bottom,
		int height,
		int minHeight) {
	return (top < bottom) && (height > minHeight);
}

[[nodiscard]] const HistoryView::HtmlWidget *WidgetOf(
		not_null<const Element*> view) {
	return dynamic_cast<const HistoryView::HtmlWidget*>(view->media());
}

[[nodiscard]] std::optional<HistoryView::HtmlWidgetLayout> LayoutOf(
		not_null<const Element*> view) {
	const auto widget = WidgetOf(view);
	return widget ? widget->heightLayout() : std::nullopt;
}

void Positioned(const ScrollSnapshot &snapshot) {
	for (const auto bubbles : { &snapshot.bubbles, &snapshot.above }) {
		for (const auto &was : *bubbles) {
			const auto view = was.view.get();
			if (const auto widget = view ? WidgetOf(view) : nullptr) {
				widget->rememberPlacement();
			}
		}
	}
}

[[nodiscard]] std::optional<ScrollBubble> Describe(
		not_null<Element*> view,
		int top) {
	const auto layout = LayoutOf(view);
	const auto was = layout ? layout->placed : std::nullopt;
	if (!was) {
		return std::nullopt;
	}
	return ScrollBubble{
		.item = view->data()->fullId(),
		.view = base::make_weak(view.get()),
		.runtime = layout->runtime,
		.bubble = was->bubble,
		.top = top - layout->moved,
		.height = was->height,
		.serial = was->serial,
		.card = was->card,
	};
}

[[nodiscard]] std::optional<Laid> Resolve(
		const ScrollBubble &was,
		Element *view,
		int top) {
	const auto runtime = was.runtime.get();
	if (!view || (view != was.view.get()) || !runtime || (top < 0)) {
		return std::nullopt;
	}
	const auto layout = LayoutOf(view);
	const auto consumed = layout ? layout->consumed : std::nullopt;
	if (!consumed
		|| (layout->runtime.get() != runtime)
		|| (consumed->serial == was.serial)
		|| (consumed->height == was.card)) {
		return std::nullopt;
	}
	return Laid{
		.view = view,
		.consumed = *consumed,
		.bubble = view->innerGeometry(),
		.top = top,
		.height = view->height(),
	};
}

[[nodiscard]] bool Intersects(
		const ScrollBubble &was,
		int scrollTop,
		int viewport) {
	const auto top = was.top + was.bubble.y();
	return (top < scrollTop + viewport)
		&& (top + was.bubble.height() > scrollTop);
}

[[nodiscard]] bool OwnGrowth(const ScrollBubble &was, const Laid &now) {
	// a bottom-anchored growth at the end follows it, which keeps its bottom
	return (now.consumed.change == HeightChange::Own)
		&& (now.consumed.height > was.card)
		&& (now.consumed.anchor == ResizeAnchor::Top);
}

[[nodiscard]] std::optional<Edge> KeptEdge(
		const ScrollBubble &was,
		const HeightLayout &consumed,
		int scrollTop,
		int viewport) {
	const auto top = was.top + was.bubble.y();
	const auto bottom = top + was.bubble.height();
	const auto till = scrollTop + viewport;
	if (bottom <= scrollTop) {
		return Edge::Below;
	} else if (top >= till) {
		return std::nullopt;
	}
	const auto topInside = (top >= scrollTop);
	const auto bottomInside = (bottom <= till);
	const auto own = (consumed.change == HeightChange::Own);
	const auto keepsBottom = (consumed.anchor == ResizeAnchor::Bottom)
		? (own || bottomInside || !topInside)
		: (!own && bottomInside && !topInside);
	return keepsBottom ? std::make_optional(Edge::Bottom) : std::nullopt;
}

[[nodiscard]] int EdgeY(Edge edge, int top, int height, QRect bubble) {
	return (edge == Edge::Below)
		? (top + height)
		: (edge == Edge::Bottom)
		? (top + bubble.y() + bubble.height())
		: (top + bubble.y());
}

[[nodiscard]] int Target(
		Edge edge,
		const ScrollBubble &was,
		const Laid &now,
		int scrollTop) {
	return scrollTop
		+ EdgeY(edge, now.top, now.height, now.bubble)
		- EdgeY(edge, was.top, was.height, was.bubble);
}

[[nodiscard]] int EdgeMove(
		Edge edge,
		const ScrollBubble &was,
		const Laid &now) {
	return EdgeY(edge, 0, now.height, now.bubble)
		- EdgeY(edge, 0, was.height, was.bubble);
}

[[nodiscard]] int EdgeShift(
		const ScrollBubble &was,
		const Laid &now,
		int scrollTop,
		int viewport) {
	const auto edge = KeptEdge(was, now.consumed, scrollTop, viewport);
	return edge ? EdgeMove(*edge, was, now) : 0;
}

[[nodiscard]] bool AddAtEnd(
		ScrollSnapshot &result,
		not_null<Element*> view,
		int top) {
	const auto from = result.scrollTop;
	if (top < 0) {
		return true;
	} else if (const auto bubble = Describe(view, top)) {
		if (bubble->top + bubble->height <= from) {
			return false;
		} else if (Intersects(*bubble, from, result.viewport)) {
			result.bubbles.push_back(*bubble);
		}
		return true;
	}
	return (view->height() <= 0) || (top + view->height() > from);
}

[[nodiscard]] bool AddBelow(
		ScrollSnapshot &result,
		not_null<Element*> view,
		int top) {
	const auto till = result.scrollTop + result.viewport;
	if (top < 0) {
		return true;
	} else if (const auto bubble = Describe(view, top)) {
		if (bubble->top >= till) {
			return false;
		} else if (Intersects(*bubble, result.scrollTop, result.viewport)) {
			result.bubbles.push_back(*bubble);
		}
		return true;
	}
	return (view->height() <= 0) || (top < till);
}

void AddHolder(
		ScrollSnapshot &result,
		not_null<Element*> view,
		int top,
		int offset) {
	if (const auto bubble = Describe(view, top)) {
		result.scrollTop = bubble->top + offset;
		result.bubbles.push_back(*bubble);
	}
}

template <typename Resolver>
[[nodiscard]] std::optional<Kept> KeepTop(
		const ScrollSnapshot &snapshot,
		Resolver &&resolve) {
	for (const auto &was : snapshot.bubbles) {
		const auto now = resolve(was);
		if (now
			&& Intersects(was, snapshot.scrollTop, snapshot.viewport)
			&& OwnGrowth(was, *now)) {
			return Kept{
				.view = now->view,
				.top = now->top,
				.target = Target(Edge::Top, was, *now, snapshot.scrollTop),
			};
		}
	}
	return std::nullopt;
}

template <typename Resolver>
[[nodiscard]] std::optional<Kept> KeepEdge(
		const ScrollSnapshot &snapshot,
		Resolver &&resolve) {
	for (const auto &was : snapshot.bubbles) {
		const auto now = resolve(was);
		if (!now) {
			continue;
		}
		const auto edge = KeptEdge(
			was,
			now->consumed,
			snapshot.scrollTop,
			snapshot.viewport);
		const auto shift = edge ? EdgeMove(*edge, was, *now) : 0;
		if (!shift) {
			return std::nullopt;
		}
		return Kept{
			.view = now->view,
			.top = now->top,
			.target = Target(*edge, was, *now, snapshot.scrollTop),
			.shift = shift,
		};
	}
	return std::nullopt;
}

template <typename Resolver>
[[nodiscard]] std::optional<Kept> KeepAnchor(
		const ScrollSnapshot &snapshot,
		Resolver &&resolve) {
	return snapshot.atEnd
		? KeepTop(snapshot, resolve)
		: KeepEdge(snapshot, resolve);
}

template <typename Resolver>
[[nodiscard]] bool AnyChanged(
		const ScrollSnapshot &snapshot,
		Resolver &&resolve) {
	return ranges::any_of(snapshot.bubbles, [&](const ScrollBubble &was) {
		return resolve(was).has_value();
	});
}

template <typename Resolver>
[[nodiscard]] std::optional<int> LocalTarget(
		const ScrollSnapshot &snapshot,
		Resolver &&resolve,
		int from,
		int max) {
	if (!snapshot.atEnd) {
		const auto kept = KeepEdge(snapshot, resolve);
		return kept ? std::make_optional(from + kept->shift) : std::nullopt;
	} else if (const auto kept = KeepTop(snapshot, resolve)) {
		return kept->target;
	}
	return AnyChanged(snapshot, resolve)
		? std::make_optional(max)
		: std::nullopt;
}

template <typename Resolver>
[[nodiscard]] int AboveChange(
		const ScrollSnapshot &snapshot,
		Resolver &&resolve) {
	auto result = 0;
	for (const auto &was : snapshot.above) {
		if (const auto now = resolve(was)) {
			result += now->consumed.height - was.card;
		}
	}
	return result;
}

template <typename Shown>
void RecordLayout(ExpansionState &state, int range, Shown &&shown) {
	auto cards = std::vector<std::optional<int>>();
	cards.reserve(state.above.size());
	for (const auto &was : state.above) {
		const auto view = was.view.get();
		const auto layout = (view && shown(view))
			? LayoutOf(view)
			: std::nullopt;
		cards.push_back((layout && layout->consumed)
			? std::make_optional(layout->consumed->height)
			: std::nullopt);
	}
	state.layouts.push_back({ .cards = std::move(cards), .range = range });
}

template <typename Resolver>
[[nodiscard]] int ExpandedAbove(
		const ExpansionState &state,
		Resolver &&resolve,
		int scrollTop) {
	const auto count = int(state.above.size());
	auto finals = std::vector<std::optional<int>>(count);
	auto control = state.scrollTop;
	auto changed = false;
	for (auto i = 0; i != count; ++i) {
		const auto &was = state.above[i];
		if (const auto now = resolve(was)) {
			finals[i] = now->consumed.height;
			control += now->consumed.height - was.card;
			changed = true;
		}
	}
	if (!changed) {
		return 0;
	}
	for (const auto &layout : state.layouts) {
		auto range = layout.range;
		for (auto i = 0; i != count; ++i) {
			if (finals[i] && layout.cards[i]) {
				range += *finals[i] - *layout.cards[i];
			}
		}
		control = std::min(control, std::max(range, 0));
	}
	return control - scrollTop;
}

template <typename Entries>
[[nodiscard]] int IndexOf(const Entries &entries, const Element *view) {
	const auto i = ranges::find_if(entries, [&](const auto &entry) {
		return (entry.view == view);
	});
	return (i != end(entries)) ? int(i - begin(entries)) : -1;
}

[[nodiscard]] bool EmulatedTouch(not_null<QWheelEvent*> e) {
	return (e->source() == Qt::MouseEventSynthesizedByApplication)
		&& (e->phase() != Qt::NoScrollPhase)
		&& (e->pixelDelta() == e->angleDelta());
}

[[nodiscard]] Wheel ClassifyWheel(
		not_null<QWheelEvent*> e,
		Ui::ScrollDirectionLock *lock,
		QSize size,
		bool crossAxis) {
	const auto phase = e->phase();
	const auto delta = Ui::ScrollDeltaF(e, EmulatedTouch(e));
	const auto pixels = delta.toPoint();
	const auto locked = lock
		? lock->update(phase, delta)
		: std::optional<Qt::Orientation>();
	if (locked) {
		return ((*locked == Qt::Vertical) && (pixels.y() != 0))
			? Wheel::Scrolls
			: Wheel::Ignored;
	}
	const auto multiply = e->modifiers()
		& (Qt::ControlModifier | Qt::ShiftModifier);
	const auto effective = multiply
		? QPoint(
			int(pixels.x() * std::max(size.width(), 120) / 120.),
			int(pixels.y() * std::max(size.height(), 120) / 120.))
		: pixels;
	if (std::abs(effective.y()) <= std::abs(effective.x())) {
		return Wheel::Ignored;
	} else if (crossAxis
		&& (!lock || (phase == Qt::NoScrollPhase))
		&& (std::abs(delta.x()) > std::abs(delta.y()))) {
		return Wheel::CrossAxisDecides;
	}
	return Wheel::Scrolls;
}

[[nodiscard]] bool KeyScrolls(not_null<QKeyEvent*> e) {
	switch (e->key()) {
	case Qt::Key_Up:
	case Qt::Key_Down:
		return !(e->modifiers() & (Qt::AltModifier | Qt::ControlModifier));
	case Qt::Key_PageUp:
	case Qt::Key_PageDown:
	case Qt::Key_Home:
	case Qt::Key_End:
		return true;
	}
	return false;
}

[[nodiscard]] bool AreaWheelScrolls(not_null<QWheelEvent*> e) {
	const auto angle = e->angleDelta();
	return (angle.y() != 0) && (std::abs(angle.x()) <= std::abs(angle.y()));
}

[[nodiscard]] bool AreaStepScrolls(int action) {
	switch (action) {
	case QAbstractSlider::SliderSingleStepAdd:
	case QAbstractSlider::SliderSingleStepSub:
	case QAbstractSlider::SliderPageStepAdd:
	case QAbstractSlider::SliderPageStepSub:
		return true;
	}
	return false;
}

[[nodiscard]] bool Pressed(not_null<QEvent*> e) {
	const auto type = e->type();
	return (type == QEvent::MouseButtonPress)
		|| (type == QEvent::MouseButtonDblClick);
}

} // namespace

uint32 ListScroll::Epoch(not_null<const ElementDelegate*> owner) {
	const auto &owners = Owners();
	const auto i = owners.find(owner);
	return (i != end(owners)) ? i->second.epoch : kUnpositioned;
}

uint32 ListScroll::Consumed(not_null<const ElementDelegate*> owner) {
	const auto &owners = Owners();
	const auto i = owners.find(owner);
	return (i != end(owners)) ? i->second.consumed : 0;
}

uint32 ListScroll::Consumed(not_null<const HistoryWidget*> widget) {
	auto result = uint32();
	for (const auto history : { widget->_migrated, widget->_history }) {
		if (history) {
			result += Consumed(history->delegateMixin()->delegate());
		}
	}
	return result;
}

void ListScroll::Consume(not_null<const ElementDelegate*> owner) {
	auto &owners = Owners();
	const auto i = owners.find(owner);
	if (i != end(owners)) {
		++i->second.consumed;
	}
}

uint32 ListScroll::Inputs(not_null<const ElementDelegate*> owner) {
	const auto &owners = Owners();
	const auto i = owners.find(owner);
	return (i != end(owners)) ? i->second.inputs : 0;
}

uint32 ListScroll::Inputs(not_null<const HistoryWidget*> widget) {
	auto result = uint32();
	for (const auto history : { widget->_migrated, widget->_history }) {
		if (history) {
			result += Inputs(history->delegateMixin()->delegate());
		}
	}
	return result;
}

void ListScroll::Input(not_null<const ElementDelegate*> owner) {
	auto &owners = Owners();
	const auto i = owners.find(owner);
	if (i != end(owners)) {
		++i->second.inputs;
	}
}

void ListScroll::Input(not_null<const HistoryWidget*> widget) {
	if (const auto history = widget->_history) {
		Input(history->delegateMixin()->delegate());
	}
}

void ListScroll::WatchInput(
		not_null<Ui::ElasticScroll*> scroll,
		Fn<void()> input,
		Fn<bool(QPoint, Qt::ScrollPhase)> crossAxis,
		bool locked,
		Fn<bool(not_null<QWheelEvent*>)> wheelProcess) {
	struct State {
		Ui::ScrollDirectionLock lock;
		bool pending = false;
	};
	const auto state = scroll->lifetime().make_state<State>();
	scroll->setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		if (wheelProcess && wheelProcess(e)) {
			return true;
		}
		const auto wheel = ClassifyWheel(
			e,
			locked ? &state->lock : nullptr,
			scroll->size(),
			bool(crossAxis));
		state->pending = (wheel == Wheel::CrossAxisDecides);
		if (wheel == Wheel::Scrolls) {
			input();
		}
		return false;
	});
	if (crossAxis) {
		scroll->setCrossAxisWheelProcess([=](
				QPoint delta,
				Qt::ScrollPhase phase) {
			const auto consumed = crossAxis(delta, phase);
			if (base::take(state->pending) && !consumed) {
				input();
			}
			return consumed;
		});
	}
	base::install_event_filter(scroll, [=](not_null<QEvent*> e) {
		if ((e->type() == QEvent::KeyPress)
			&& scroll->widget()
			&& KeyScrolls(static_cast<QKeyEvent*>(e.get()))) {
			input();
		}
		return base::EventFilterResult::Continue;
	});
	for (const auto child : scroll->children()) {
		if (const auto bar = dynamic_cast<Ui::ElasticScrollBar*>(child)) {
			base::install_event_filter(bar, [=](not_null<QEvent*> e) {
				if (Pressed(e)) {
					input();
				}
				return base::EventFilterResult::Continue;
			});
		}
	}
}

void ListScroll::WatchInput(
		not_null<Ui::ScrollArea*> scroll,
		Fn<void()> input) {
	struct State {
		QEvent::Type touch = QEvent::None;
		bool scrolling = false;
		bool landed = false;
	};
	const auto state = scroll->lifetime().make_state<State>();
	scroll->setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		if (AreaWheelScrolls(e)) {
			input();
		}
		return false;
	});
	QObject::connect(
		scroll->verticalScrollBar(),
		&QAbstractSlider::actionTriggered,
		scroll.get(),
		[=](int action) {
			if (AreaStepScrolls(action)) {
				input();
			}
		});
	scroll->setCustomTouchProcess([=](not_null<QTouchEvent*> e) {
		// The area took the TouchBegin without a press: it caught a fling.
		if (base::take(state->landed)) {
			input();
		}
		state->touch = e->type();
		state->landed = state->scrolling
			&& (state->touch == QEvent::TouchBegin);
		return false;
	});
	scroll->touchMaybePressing(
	) | rpl::on_next([=](bool pressing) {
		if (pressing) {
			state->scrolling = state->landed = false;
		} else if (state->touch == QEvent::TouchUpdate) {
			state->scrolling = true;
			input();
		}
	}, scroll->lifetime());
	for (const auto child : scroll->children()) {
		if (const auto bar = dynamic_cast<Ui::ScrollBar*>(child)) {
			base::install_event_filter(bar, [=](not_null<QEvent*> e) {
				if (Pressed(e) && (bar->height() > bar->width())) {
					input();
				}
				return base::EventFilterResult::Continue;
			});
		}
	}
}

ScrollSnapshot ListScroll::Capture(
		not_null<HistoryWidget*> widget,
		bool atEnd) {
	const auto list = widget->_list.data();
	const auto history = widget->_history;
	const auto migrated = widget->_migrated;
	const auto scroll = widget->_scroll.data();
	if (!Capturable(list, history, widget->_historyInited, scroll)) {
		return {};
	}
	auto result = ScrollSnapshot{
		.scrollTop = scroll->scrollTop(),
		.viewport = scroll->height(),
		.atEnd = atEnd,
	};
	if (atEnd) {
		EnumerateUp(migrated, history, [&](not_null<Element*> view) {
			return AddAtEnd(result, view, list->itemTop(view));
		});
		ranges::reverse(result.bubbles);
		return result;
	}
	const auto anchored = [&]() -> History* {
		if (list->historyTop() >= 0 && history->scrollTopItem) {
			return history;
		} else if (list->migratedTop() >= 0
			&& migrated
			&& migrated->scrollTopItem) {
			return migrated;
		}
		return nullptr;
	}();
	const auto view = anchored ? anchored->scrollTopItem : nullptr;
	const auto top = view ? list->itemTop(view) : -1;
	if (top >= 0) {
		AddHolder(result, view, top, anchored->scrollTopOffset);
		const auto add = [&](not_null<Element*> below) {
			return AddBelow(result, below, list->itemTop(below));
		};
		EnumerateDownFrom(migrated, history, view, add);
	}
	return result;
}

ScrollSnapshot ListScroll::CaptureDirect(not_null<HistoryWidget*> widget) {
	const auto list = widget->_list.data();
	const auto scroll = widget->_scroll.data();
	const auto inited = widget->_historyInited;
	if (!Capturable(list, widget->_history, inited, scroll)) {
		return {};
	}
	auto result = ScrollSnapshot{
		.scrollTop = scroll->scrollTop(),
		.viewport = scroll->height(),
		.atEnd = (scroll->scrollTop() >= scroll->scrollTopMax()),
		.direct = true,
	};
	const auto till = result.scrollTop + result.viewport;
	const auto add = [&](not_null<Element*> view) {
		const auto top = list->itemTop(view);
		if (top < 0) {
			return true;
		} else if (const auto bubble = Describe(view, top)) {
			if (bubble->top >= till) {
				return false;
			}
			result.bubbles.push_back(*bubble);
			return true;
		}
		return (view->height() <= 0) || (top < till);
	};
	EnumerateDown(widget->_migrated, widget->_history, add);
	return result;
}

bool ListScroll::Keep(
		not_null<HistoryWidget*> widget,
		const ScrollSnapshot &snapshot) {
	const auto positioned = gsl::finally([&] {
		Advance(widget->_migrated);
		Advance(widget->_history);
		Positioned(snapshot);
	});
	const auto list = widget->_list.data();
	const auto history = widget->_history;
	if (snapshot.bubbles.empty() || !list || !history) {
		return false;
	}
	const auto &owner = history->owner();
	const auto resolve = [&](const ScrollBubble &was) {
		const auto item = owner.message(was.item);
		const auto view = item ? item->mainView() : nullptr;
		return Resolve(was, view, view ? list->itemTop(view) : -1);
	};
	const auto scrollTo = [&](int top) {
		const auto max = widget->_scroll->scrollTopMax();
		widget->synteticScrollToY(std::clamp(top, 0, max));
	};
	const auto from = snapshot.scrollTop;
	if (!snapshot.direct) {
		const auto kept = KeepAnchor(snapshot, resolve);
		if (kept) {
			if (snapshot.atEnd) {
				// before the anchor: its scroll notification forgets it
				widget->_scroll->absorbBottomOverscroll(kept->target);
			}
			const auto in = kept->view->history();
			in->scrollTopItem = kept->view;
			in->scrollTopOffset = kept->target - kept->top;
			const auto other = (in == history) ? widget->_migrated : history;
			if (other) {
				other->forgetScrollState();
			}
		}
		return snapshot.atEnd && kept;
	} else if (snapshot.atEnd) {
		if (const auto kept = KeepTop(snapshot, resolve)) {
			widget->_scroll->absorbBottomOverscroll(kept->target);
			scrollTo(kept->target);
			return true;
		} else if (AnyChanged(snapshot, resolve)) {
			scrollTo(widget->_scroll->scrollTopMax());
		}
		return false;
	}
	auto shift = 0;
	for (const auto &was : snapshot.bubbles) {
		if (const auto now = resolve(was)) {
			shift += EdgeShift(was, *now, from, snapshot.viewport);
		}
	}
	if (shift) {
		scrollTo(from + shift);
	}
	return false;
}

Fn<std::optional<int>()> ListScroll::Placement(
		not_null<const HistoryWidget*> widget,
		FullMsgId id) {
	return [=] {
		return Destination(widget, id);
	};
}

std::optional<int> ListScroll::Destination(
		not_null<const HistoryWidget*> widget,
		FullMsgId id) {
	const auto list = widget->_list.data();
	if (!list || widget->hasPendingResizedItems()) {
		return std::nullopt;
	}
	const auto max = widget->_scroll->scrollTopMax();
	if (!id) {
		return max;
	}
	const auto &owner = widget->session().data();
	const auto item = owner.message(id);
	const auto view = item ? item->mainView() : nullptr;
	if (!view) {
		return std::nullopt;
	}
	const auto group = owner.groups().find(item);
	const auto leader = group ? group->items.front()->mainView() : nullptr;
	const auto shown = leader ? leader : view;
	if (!shown->block()
		|| shown->pendingResize()
		|| (list->itemTop(shown) < 0)) {
		return std::nullopt;
	}
	return std::clamp(widget->itemTopForHighlight(view), 0, max);
}

ScrollSnapshot ListScroll::Capture(not_null<HistoryView::ListWidget*> list) {
	const auto top = list->_visibleTop;
	const auto bottom = list->_visibleBottom;
	const auto anchor = list->_visibleTopItem;
	if (!Scrollable(top, bottom, list->height(), list->_minHeight)
		|| list->_inverted
		|| list->_items.empty()
		|| (!anchor && !list->atNewestEdge())) {
		return {};
	}
	auto result = ScrollSnapshot{
		.scrollTop = top,
		.viewport = bottom - top,
		.atEnd = !anchor,
	};
	if (anchor) {
		const auto from = list->_visibleTopFromItem;
		AddHolder(result, anchor, list->itemTop(anchor), from);
		const auto &items = list->_items;
		const auto i = ranges::find(items, not_null{ anchor });
		const auto below = (i != end(items)) ? (i + 1) : i;
		for (const auto &view : ranges::make_subrange(below, end(items))) {
			if (!AddBelow(result, view, list->itemTop(view))) {
				break;
			}
		}
		return result;
	}
	for (const auto &view : ranges::views::reverse(list->_items)) {
		if (!AddAtEnd(result, view, list->itemTop(view))) {
			break;
		}
	}
	ranges::reverse(result.bubbles);
	return result;
}

ScrollSnapshot ListScroll::CaptureSaved(
		not_null<HistoryView::ListWidget*> list) {
	const auto top = list->_visibleTop;
	const auto bottom = list->_visibleBottom;
	const auto &state = list->_scrollTopState;
	if (!Scrollable(top, bottom, list->height(), list->_minHeight)
		|| list->_inverted
		|| list->_items.empty()
		|| !state.item) {
		return {};
	}
	const auto viewport = list->countScrollState();
	if ((viewport.item != state.item) || (viewport.shift != state.shift)) {
		return {};
	}
	auto result = ScrollSnapshot{
		.scrollTop = top,
		.viewport = bottom - top,
		.saved = true,
	};
	const auto &items = list->_items;
	const auto index = list->findItemIndexByY(top);
	const auto holder = items[index];
	const auto y = list->itemTop(holder);
	AddHolder(result, holder, y, top - y);
	const auto below = begin(items) + index + 1;
	for (const auto &view : ranges::make_subrange(below, end(items))) {
		if (!AddBelow(result, view, list->itemTop(view))) {
			break;
		}
	}
	return result;
}

void ListScroll::Keep(
		not_null<HistoryView::ListWidget*> list,
		const ScrollSnapshot &snapshot) {
	const auto positioned = gsl::finally([&] {
		Advance(list, list->lifetime());
		Positioned(snapshot);
	});
	if (snapshot.bubbles.empty()) {
		return;
	}
	const auto resolve = [&](const ScrollBubble &was) {
		const auto view = list->viewForItem(was.item);
		const auto shown = view
			&& ranges::contains(list->_items, not_null{ view });
		return Resolve(
			was,
			shown ? view : nullptr,
			shown ? list->itemTop(view) : -1);
	};
	if (!snapshot.saved) {
		if (const auto kept = KeepAnchor(snapshot, resolve)) {
			const auto scroll = snapshot.atEnd
				? list->_delegate->listScrollArea()
				: nullptr;
			if (scroll) {
				scroll->absorbBottomOverscroll(kept->target);
			}
			list->_visibleTopItem = kept->view;
			list->_visibleTopFromItem = kept->target - kept->top;
		}
		return;
	}
	auto &state = list->_scrollTopState;
	const auto kept = state.item
		? KeepEdge(snapshot, resolve)
		: std::nullopt;
	const auto index = kept ? list->findNearestItem(state.item) : -1;
	if (index >= 0) {
		state.shift = kept->target - list->itemTop(list->_items[index]);
	}
}

Fn<std::optional<int>()> ListScroll::Placement(
		not_null<const HistoryView::ListWidget*> list,
		Data::MessagePosition position) {
	return [=] {
		return Destination(list, position);
	};
}

std::optional<int> ListScroll::Destination(
		not_null<const HistoryView::ListWidget*> list,
		Data::MessagePosition position) {
	const auto pending = [](not_null<Element*> view) {
		return view->pendingResize();
	};
	if (ranges::any_of(list->_items, pending)) {
		return std::nullopt;
	}
	const auto top = list->scrollTopForPosition(position);
	if (!top) {
		return std::nullopt;
	}
	const auto viewport = list->_visibleBottom - list->_visibleTop;
	return std::clamp(*top, 0, std::max(list->height() - viewport, 0));
}

ScrollSnapshot ListScroll::Capture(
		not_null<const AdminLog::InnerWidget*> list) {
	const auto top = list->_visibleTop;
	const auto bottom = list->_visibleBottom;
	if (!Scrollable(top, bottom, list->height(), list->_minHeight)
		|| list->_skipScrollRestore
		|| list->_displayItems.empty()) {
		return {};
	}
	const auto anchor = list->_visibleTopItem;
	auto result = ScrollSnapshot{
		.scrollTop = top,
		.viewport = bottom - top,
		.atEnd = !anchor,
	};
	if (anchor) {
		const auto from = list->_visibleTopFromItem;
		AddHolder(result, anchor, list->itemTop(anchor), from);
		const auto &entries = list->_displayItems;
		for (auto i = IndexOf(entries, anchor) - 1; i >= 0; --i) {
			const auto &entry = entries[i];
			if (!AddBelow(result, entry.view, list->itemTop(entry.view))) {
				break;
			}
		}
		return result;
	}
	for (const auto &entry : list->_displayItems) {
		if (!AddAtEnd(result, entry.view, list->itemTop(entry.view))) {
			break;
		}
	}
	ranges::reverse(result.bubbles);
	return result;
}

void ListScroll::CaptureAbove(
		not_null<const AdminLog::InnerWidget*> list,
		const Element *anchor,
		ScrollSnapshot &snapshot) {
	const auto top = list->_visibleTop;
	const auto bottom = list->_visibleBottom;
	if (!anchor
		|| !Scrollable(top, bottom, list->height(), list->_minHeight)
		|| list->_skipScrollRestore) {
		return;
	}
	const auto &entries = list->_displayItems;
	const auto index = IndexOf(entries, anchor);
	if (index < 0) {
		return;
	}
	const auto captured = [&](const ScrollBubble &bubble) {
		return ranges::contains(
			snapshot.bubbles,
			bubble.item,
			&ScrollBubble::item);
	};
	for (auto i = index + 1, count = int(entries.size()); i != count; ++i) {
		const auto view = entries[i].view;
		const auto bubble = Describe(view, list->itemTop(view));
		if (bubble && !captured(*bubble)) {
			snapshot.above.push_back(*bubble);
		}
	}
}

ListScroll::Expansion::Expansion(const AdminLog::InnerWidget *list)
: _list(list) {
}

ListScroll::Expansion::~Expansion() {
	if (_list) {
		Expansions().remove(not_null{ _list });
	}
}

ListScroll::Expansion ListScroll::TrackExpansion(
		not_null<const AdminLog::InnerWidget*> list,
		const ScrollSnapshot &snapshot) {
	if (snapshot.above.empty()) {
		return Expansion(nullptr);
	}
	const auto inserted = Expansions().try_emplace(list, ExpansionState{
		.above = snapshot.above,
		.scrollTop = list->_visibleTop,
	}).second;
	return Expansion(inserted ? list.get() : nullptr);
}

Element *ListScroll::Displayed(
		not_null<AdminLog::InnerWidget*> list,
		FullMsgId id) {
	const auto item = list->_history->owner().message(id);
	const auto view = item ? list->viewForItem(item) : nullptr;
	return (view && (IndexOf(list->_displayItems, view) >= 0))
		? view
		: nullptr;
}

void ListScroll::Keep(
		not_null<AdminLog::InnerWidget*> list,
		const ScrollSnapshot &snapshot) {
	const auto positioned = gsl::finally([&] {
		Advance(list, list->lifetime());
		Positioned(snapshot);
	});
	if (const auto tracked = ExpansionOf(list)) {
		RecordLayout(
			*tracked,
			list->height() - list->_itemsTop - list->_minHeight,
			[&](not_null<Element*> view) {
				return (list->viewForItem(view->data()) == view.get());
			});
	}
	if (snapshot.bubbles.empty()) {
		return;
	}
	const auto resolve = [&](const ScrollBubble &was) {
		const auto view = Displayed(list, was.item);
		return Resolve(was, view, view ? list->itemTop(view) : -1);
	};
	const auto kept = KeepAnchor(snapshot, resolve);
	const auto index = kept ? IndexOf(list->_displayItems, kept->view) : -1;
	if (index < 0) {
		return;
	}
	const auto scroll = snapshot.atEnd
		? dynamic_cast<Ui::ElasticScroll*>(list->parentWidget())
		: nullptr;
	if (scroll) {
		scroll->absorbBottomOverscroll(kept->target);
	}
	const auto max = std::max(list->height() - list->_minHeight, 0);
	const auto target = std::clamp(kept->target, 0, max);
	list->_visibleTopItem = kept->view;
	list->_visibleTopFromItem = target - kept->top;
	list->_visibleTopDisplayIndex = index;
}

ScrollShift ListScroll::Adjust(
		not_null<AdminLog::InnerWidget*> list,
		const ScrollSnapshot &snapshot,
		int target,
		const Expansion *expansion) {
	const auto positioned = gsl::finally([&] {
		Advance(list, list->lifetime());
		Positioned(snapshot);
	});
	// WHY: every caller has just rebuilt the display, so the view map holds
	// exactly the displayed items and needs no rescan for each above entry.
	const auto &owner = list->_history->owner();
	const auto resolve = [&](const ScrollBubble &was) {
		const auto item = owner.message(was.item);
		const auto view = item ? list->viewForItem(item) : nullptr;
		return Resolve(was, view, view ? list->itemTop(view) : -1);
	};
	const auto tracked = (expansion && (expansion->_list == list.get()))
		? ExpansionOf(list)
		: nullptr;
	const auto above = tracked
		? ExpandedAbove(*tracked, resolve, list->_visibleTop)
		: AboveChange(snapshot, resolve);
	const auto unchanged = ScrollShift{ .target = target, .above = above };
	if (snapshot.bubbles.empty()) {
		return unchanged;
	}
	const auto max = std::max(list->height() - list->_minHeight, 0);
	const auto from = std::clamp(target, 0, max);
	const auto adjusted = LocalTarget(snapshot, resolve, from, max);
	const auto result = adjusted ? std::clamp(*adjusted, 0, max) : from;
	if (result == from) {
		return unchanged;
	}
	return ScrollShift{
		.target = result,
		.difference = result - from,
		.above = above,
	};
}

Fn<std::optional<int>()> ListScroll::Placement(
		not_null<AdminLog::InnerWidget*> list,
		FullMsgId id,
		MessageHighlightId highlight) {
	return [=] {
		return Destination(list, id, highlight);
	};
}

std::optional<int> ListScroll::Destination(
		not_null<AdminLog::InnerWidget*> list,
		FullMsgId id,
		const MessageHighlightId &highlight) {
	const auto view = Displayed(list, id);
	if (!view || view->pendingResize()) {
		return std::nullopt;
	}
	return list->jumpTarget(view, highlight);
}

Fn<std::optional<int>()> ListScroll::Placement(
		not_null<AdminLog::InnerWidget*> list,
		Element *anchor,
		int delta,
		int target) {
	const auto start = anchor
		? list->computeScrollFromAnchor({ anchor, delta })
		: ScrollMax;
	if (start == ScrollMax) {
		return [] { return std::optional<int>(); };
	}
	const auto weak = base::make_weak(anchor);
	const auto offset = target - start;
	return [=] {
		return Destination(list, weak, delta, offset);
	};
}

std::optional<int> ListScroll::Destination(
		not_null<AdminLog::InnerWidget*> list,
		const base::weak_ptr<Element> &anchor,
		int delta,
		int offset) {
	const auto view = anchor.get();
	const auto now = view
		? list->computeScrollFromAnchor({ view, delta })
		: ScrollMax;
	return (now != ScrollMax)
		? std::make_optional(now + offset)
		: std::nullopt;
}

} // namespace Tgw
