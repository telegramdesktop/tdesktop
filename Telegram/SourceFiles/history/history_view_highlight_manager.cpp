/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_view_highlight_manager.h"

#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "ui/chat/chat_style.h"

namespace HistoryView {

ElementHighlighter::ElementHighlighter(
	not_null<Data::Session*> data,
	ViewForItem viewForItem,
	RepaintView repaintView)
: _data(data)
, _viewForItem(std::move(viewForItem))
, _repaintView(std::move(repaintView))
, _animation(*this) {
}

void ElementHighlighter::enqueue(const SelectedQuote &quote) {
	const auto data = computeHighlight(quote);
	if (_queue.empty() && !_animation.animating()) {
		highlight(data);
	} else if (_highlighted != data && !base::contains(_queue, data)) {
		_queue.push_back(data);
		checkNextHighlight();
	}
}

void ElementHighlighter::highlight(const SelectedQuote &quote) {
	highlight(computeHighlight(quote));
}

void ElementHighlighter::checkNextHighlight() {
	if (_animation.animating()) {
		return;
	}
	const auto next = [&] {
		while (!_queue.empty()) {
			const auto highlight = _queue.front();
			_queue.pop_front();
			if (const auto item = _data->message(highlight.itemId)) {
				if (_viewForItem(item)) {
					return highlight;
				}
			}
		}
		return Highlight();
	}();
	if (next) {
		highlight(next);
	}
}

Ui::ChatPaintHighlight ElementHighlighter::state(
		not_null<const HistoryItem*> item) const {
	if (item->fullId() == _highlighted.itemId) {
		auto result = _animation.state();
		result.range = _highlighted.part;
		return result;
	}
	return {};
}

ElementHighlighter::Highlight ElementHighlighter::computeHighlight(
		const SelectedQuote &quote) {
	Assert(quote.item != nullptr);

	const auto item = not_null(quote.item);
	const auto owner = &item->history()->owner();
	if (const auto group = owner->groups().find(item)) {
		const auto leader = group->items.front();
		const auto leaderId = leader->fullId();
		const auto i = ranges::find(group->items, item);
		if (i != end(group->items)) {
			const auto index = int(i - begin(group->items));
			if (quote.text.empty()) {
				return { leaderId, AddGroupItemSelection({}, index) };
			} else if (const auto leaderView = _viewForItem(leader)) {
				return { leaderId, leaderView->selectionFromQuote(quote) };
			}
		}
		return { leaderId };
	} else if (quote.text.empty()) {
		return { item->fullId() };
	} else if (const auto view = _viewForItem(item)) {
		return { item->fullId(), view->selectionFromQuote(quote) };
	}
	return { item->fullId() };
}

void ElementHighlighter::highlight(Highlight data) {
	if (const auto item = _data->message(data.itemId)) {
		if (const auto view = _viewForItem(item)) {
			if (_highlighted && _highlighted.itemId != data.itemId) {
				if (const auto was = _data->message(_highlighted.itemId)) {
					if (const auto view = _viewForItem(was)) {
						repaintHighlightedItem(view);
					}
				}
			}
			_highlighted = data;
			_animation.start(!data.part.empty()
				&& !IsSubGroupSelection(data.part));

			repaintHighlightedItem(view);
		}
	}
}

void ElementHighlighter::repaintHighlightedItem(
		not_null<const Element*> view) {
	if (view->isHiddenByGroup()) {
		if (const auto group = _data->groups().find(view->data())) {
			if (const auto leader = _viewForItem(group->items.front())) {
				if (!leader->isHiddenByGroup()) {
					_repaintView(leader);
					return;
				}
			}
		}
	}
	_repaintView(view);
}

void ElementHighlighter::updateMessage() {
	if (const auto item = _data->message(_highlighted.itemId)) {
		if (const auto view = _viewForItem(item)) {
			repaintHighlightedItem(view);
		}
	}
}

void ElementHighlighter::clear() {
	_animation.cancel();
	_highlighted = {};
	_lastHighlightedMessageId = FullMsgId();
	_queue.clear();
}

ElementHighlighter::AnimationManager::AnimationManager(
	ElementHighlighter &parent)
: _parent(parent) {
}

bool ElementHighlighter::AnimationManager::animating() const {
	if (_timer && _timer->isActive()) {
		return true;
	} else if (!anim::Disabled()) {
		return _simple.animating();
	}
	return false;
}

Ui::ChatPaintHighlight ElementHighlighter::AnimationManager::state() const {
	if (anim::Disabled()) {
		return {
			.opacity = !_timer ? 0. : 1.,
			.collapsion = !_timer ? 0. : _fadingOut ? 1. : 0.,
		};
	}
	return {
		.opacity = ((!_fadingOut && _collapsing)
			? 1.
			: _simple.value(_fadingOut ? 0. : 1.)),
		.collapsion = ((!_withTextPart || !_collapsing)
			? 0.
			: _fadingOut
			? 1.
			: _simple.value(1.)),
	};
}

MsgId ElementHighlighter::latestSingleHighlightedMsgId() const {
	return _highlighted.itemId
		? _highlighted.itemId.msg
		: _lastHighlightedMessageId.msg;
}

void ElementHighlighter::AnimationManager::start(bool withTextPart) {
	_withTextPart = withTextPart;
	const auto finish = [=] {
		cancel();
		_parent._lastHighlightedMessageId = base::take(
			_parent._highlighted.itemId);
		_parent.checkNextHighlight();
	};
	cancel();
	if (anim::Disabled()) {
		_timer.emplace([=] {
			_parent.updateMessage();
			if (_withTextPart && !_fadingOut) {
				_fadingOut = true;
				_timer->callOnce(st::activeFadeOutDuration);
			} else {
				finish();
			}
		});
		_timer->callOnce(_withTextPart
			? st::activeFadeInDuration
			: st::activeFadeOutDuration);
		_parent.updateMessage();
	} else {
		_simple.start(
			[=](float64 value) {
				_parent.updateMessage();
				if (value == 1.) {
					if (_withTextPart) {
						_timer.emplace([=] {
							_parent.updateMessage();
							if (_collapsing) {
								_fadingOut = true;
							} else {
								_collapsing = true;
							}
							_simple.start([=](float64 value) {
								_parent.updateMessage();
								if (_fadingOut && value == 0.) {
									finish();
								} else if (!_fadingOut && value == 1.) {
									_timer->callOnce(
										st::activeFadeOutDuration);
								}
							},
							_fadingOut ? 1. : 0.,
							_fadingOut ? 0. : 1.,
							(_fadingOut
								? st::activeFadeInDuration
								: st::fadeWrapDuration));
						});
						_timer->callOnce(st::activeFadeInDuration);
					} else {
						_fadingOut = true;
						_simple.start([=](float64 value) {
							_parent.updateMessage();
							if (value == 0.) {
								finish();
							}
						},
						1.,
						0.,
						st::activeFadeOutDuration);
					}
				}
			},
			0.,
			1.,
			st::activeFadeInDuration);
	}
}

void ElementHighlighter::AnimationManager::cancel() {
	_simple.stop();
	_timer.reset();
	_fadingOut = false;
	_collapsed = false;
	_collapsing = false;
}

} // namespace HistoryView
