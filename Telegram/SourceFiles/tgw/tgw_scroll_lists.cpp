/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_scroll.h"

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
#include "tgw/tgw_option.h"
#include "ui/widgets/elastic_scroll.h"

namespace Tgw {
namespace {

using HistoryView::Element;
using HistoryView::ElementDelegate;
using OwnerEpochs = base::flat_map<not_null<const ElementDelegate*>, uint32>;

constexpr auto kUnpositioned = uint32(1);

enum class Edge : uchar {
	Top,
	Bottom,
	Below,
};

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
};

[[nodiscard]] OwnerEpochs &Owners() {
	static auto result = OwnerEpochs();
	return result;
}

void Advance(
		not_null<const ElementDelegate*> owner,
		rpl::lifetime &lifetime) {
	if (!Enabled()) {
		return;
	}
	auto &owners = Owners();
	const auto i = owners.find(owner);
	if (i != end(owners)) {
		i->second = std::max(i->second + 1, kUnpositioned + 1);
		return;
	}
	owners.emplace(owner, kUnpositioned + 1);
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
	return Enabled()
		&& list
		&& history
		&& inited
		&& (scroll->scrollTopMax() > 0);
}

[[nodiscard]] bool Scrollable(
		int top,
		int bottom,
		int height,
		int minHeight) {
	return Enabled() && (top < bottom) && (height > minHeight);
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
	for (const auto &was : snapshot.bubbles) {
		const auto view = was.view.get();
		if (const auto widget = view ? WidgetOf(view) : nullptr) {
			widget->rememberPlacement();
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
	return (now.consumed.change == HeightChange::Own)
		&& (now.consumed.height > was.card);
}

[[nodiscard]] std::optional<Edge> HolderEdge(
		const ScrollBubble &was,
		HeightChange change,
		int scrollTop,
		int viewport) {
	const auto top = was.top + was.bubble.y();
	const auto bottom = top + was.bubble.height();
	if (bottom <= scrollTop) {
		return Edge::Below;
	} else if ((top < scrollTop)
		&& (bottom <= scrollTop + viewport)
		&& (change == HeightChange::Client)) {
		return Edge::Bottom;
	}
	return std::nullopt;
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

[[nodiscard]] int HolderShift(
		const ScrollBubble &was,
		const Laid &now,
		int scrollTop,
		int viewport) {
	const auto edge = HolderEdge(
		was,
		now.consumed.change,
		scrollTop,
		viewport);
	return edge
		? (EdgeY(*edge, 0, now.height, now.bubble)
			- EdgeY(*edge, 0, was.height, was.bubble))
		: 0;
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

void AddHolder(
		ScrollSnapshot &result,
		not_null<Element*> view,
		int top,
		int offset) {
	if (const auto bubble = Describe(view, top)) {
		result.offset = offset;
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
[[nodiscard]] std::optional<Kept> KeepHolder(
		const ScrollSnapshot &snapshot,
		Resolver &&resolve) {
	const auto &was = snapshot.bubbles.front();
	const auto now = resolve(was);
	const auto shift = now
		? HolderShift(was, *now, snapshot.scrollTop, snapshot.viewport)
		: 0;
	if (!shift) {
		return std::nullopt;
	}
	return Kept{
		.view = now->view,
		.top = now->top,
		.target = now->top + snapshot.offset + shift,
	};
}

template <typename Resolver>
[[nodiscard]] std::optional<Kept> KeepAnchor(
		const ScrollSnapshot &snapshot,
		Resolver &&resolve) {
	return snapshot.atEnd
		? KeepTop(snapshot, resolve)
		: KeepHolder(snapshot, resolve);
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
		const auto kept = KeepHolder(snapshot, resolve);
		return kept
			? std::make_optional(
				from + kept->target - kept->top - snapshot.offset)
			: std::nullopt;
	} else if (const auto kept = KeepTop(snapshot, resolve)) {
		return kept->target;
	}
	return AnyChanged(snapshot, resolve)
		? std::make_optional(max)
		: std::nullopt;
}

template <typename Entries>
[[nodiscard]] int IndexOf(const Entries &entries, const Element *view) {
	const auto i = ranges::find_if(entries, [&](const auto &entry) {
		return (entry.view == view);
	});
	return (i != end(entries)) ? int(i - begin(entries)) : -1;
}

} // namespace

uint32 ListScroll::Epoch(not_null<const ElementDelegate*> owner) {
	const auto &owners = Owners();
	const auto i = owners.find(owner);
	return (i != end(owners)) ? i->second : kUnpositioned;
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
			shift += HolderShift(was, *now, from, snapshot.viewport);
		}
	}
	if (shift) {
		scrollTo(from + shift);
	}
	return false;
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
	const auto holder = list->findItemByY(top);
	const auto y = list->itemTop(holder);
	AddHolder(result, holder, y, top - y);
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
			list->_visibleTopItem = kept->view;
			list->_visibleTopFromItem = kept->target - kept->top;
		}
		return;
	}
	auto &state = list->_scrollTopState;
	const auto kept = state.item
		? KeepHolder(snapshot, resolve)
		: std::nullopt;
	const auto index = kept ? list->findNearestItem(state.item) : -1;
	if (index >= 0) {
		state.shift = kept->target - list->itemTop(list->_items[index]);
	}
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
	const auto max = std::max(list->height() - list->_minHeight, 0);
	const auto target = std::clamp(kept->target, 0, max);
	list->_visibleTopItem = kept->view;
	list->_visibleTopFromItem = target - kept->top;
	list->_visibleTopDisplayIndex = index;
}

ScrollShift ListScroll::Adjust(
		not_null<AdminLog::InnerWidget*> list,
		const ScrollSnapshot &snapshot,
		int target) {
	const auto positioned = gsl::finally([&] {
		Advance(list, list->lifetime());
		Positioned(snapshot);
	});
	const auto unchanged = ScrollShift{ .target = target };
	if (snapshot.bubbles.empty()) {
		return unchanged;
	}
	const auto resolve = [&](const ScrollBubble &was) {
		const auto view = Displayed(list, was.item);
		return Resolve(was, view, view ? list->itemTop(view) : -1);
	};
	const auto max = std::max(list->height() - list->_minHeight, 0);
	const auto from = std::clamp(target, 0, max);
	const auto adjusted = LocalTarget(snapshot, resolve, from, max);
	const auto result = adjusted ? std::clamp(*adjusted, 0, max) : from;
	return (result != from)
		? ScrollShift{ .target = result, .difference = result - from }
		: unchanged;
}

} // namespace Tgw
