/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/effects/animations.h"

class HistoryItem;

namespace Data {
class Session;
} // namespace Data

namespace HistoryView {

class Element;

class ElementHighlighter final {
public:
	using ViewForItem = Fn<Element*(const HistoryItem*)>;
	using RepaintView = Fn<void(const Element*)>;
	ElementHighlighter(
		not_null<Data::Session*> data,
		ViewForItem viewForItem,
		RepaintView repaintView);

	void enqueue(not_null<Element*> view);
	void highlight(FullMsgId itemId);
	void clear();

	[[nodiscard]] float64 progress(not_null<const HistoryItem*> item) const;

private:
	void checkNextHighlight();
	void repaintHighlightedItem(not_null<const Element*> view);
	void updateMessage();

	class AnimationManager final {
	public:
		AnimationManager(ElementHighlighter &parent);
		[[nodiscard]] bool animating() const;
		[[nodiscard]] float64 progress() const;
		void start();
		void cancel();
	private:
		ElementHighlighter &_parent;
		Ui::Animations::Simple _simple;
		std::optional<base::Timer> _timer;
	};

	const not_null<Data::Session*> _data;
	const ViewForItem _viewForItem;
	const RepaintView _repaintView;

	FullMsgId _highlightedMessageId;
	std::deque<FullMsgId> _queue;

	AnimationManager _animation;
};

} // namespace HistoryView
