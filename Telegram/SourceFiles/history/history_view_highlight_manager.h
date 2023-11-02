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

namespace Ui {
struct ChatPaintHighlight;
} // namespace Ui

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

	void enqueue(not_null<Element*> view, const TextWithEntities &part);
	void highlight(not_null<Element*> view, const TextWithEntities &part);
	void clear();

	[[nodiscard]] Ui::ChatPaintHighlight state(
		not_null<const HistoryItem*> item) const;
	[[nodiscard]] MsgId latestSingleHighlightedMsgId() const;

private:
	class AnimationManager final {
	public:
		AnimationManager(ElementHighlighter &parent);
		[[nodiscard]] bool animating() const;
		[[nodiscard]] Ui::ChatPaintHighlight state() const;
		void start(bool withTextPart);
		void cancel();

	private:
		ElementHighlighter &_parent;
		Ui::Animations::Simple _simple;
		std::optional<base::Timer> _timer;
		bool _withTextPart = false;
		bool _collapsing = false;
		bool _collapsed = false;
		bool _fadingOut = false;

	};

	struct Highlight {
		FullMsgId itemId;
		TextSelection part;

		explicit operator bool() const {
			return itemId.operator bool();
		}
		friend inline bool operator==(Highlight, Highlight) = default;
	};

	[[nodiscard]] Highlight computeHighlight(
		not_null<const Element*> view,
		const TextWithEntities &part);
	void highlight(Highlight data);
	void checkNextHighlight();
	void repaintHighlightedItem(not_null<const Element*> view);
	void updateMessage();

	const not_null<Data::Session*> _data;
	const ViewForItem _viewForItem;
	const RepaintView _repaintView;

	Highlight _highlighted;
	FullMsgId _lastHighlightedMessageId;
	std::deque<Highlight> _queue;

	AnimationManager _animation;

};

} // namespace HistoryView
