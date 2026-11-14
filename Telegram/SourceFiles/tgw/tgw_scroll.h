/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "ui/effects/animation_value.h"

class HistoryWidget;

namespace AdminLog {
class InnerWidget;
} // namespace AdminLog

namespace Data {
struct MessagePosition;
} // namespace Data

namespace HistoryView {
class Element;
class ElementDelegate;
class ListWidget;
} // namespace HistoryView

namespace Ui {
class ElasticScroll;
class ScrollArea;
} // namespace Ui

namespace Ui::Animations {
class Simple;
} // namespace Ui::Animations

namespace Tgw {

class Runtime;

struct ScrollBubble {
	FullMsgId item;
	base::weak_ptr<HistoryView::Element> view;
	base::weak_ptr<Runtime> runtime;
	QRect bubble;
	int top = 0;
	int height = 0;
	int serial = 0;
	int card = 0;
};

struct ScrollSnapshot {
	std::vector<ScrollBubble> bubbles;
	std::vector<ScrollBubble> above;
	int scrollTop = 0;
	int viewport = 0;
	bool atEnd = false;
	bool direct = false;
	bool saved = false;
};

struct ScrollShift {
	int target = 0;
	int difference = 0;
	int above = 0; // for starting positions only, targets already include it
};

class ListScroll final {
public:
	class Expansion final {
	public:
		Expansion(const Expansion &other) = delete;
		Expansion &operator=(const Expansion &other) = delete;
		~Expansion();

	private:
		friend class ListScroll;

		explicit Expansion(const AdminLog::InnerWidget *list);

		const AdminLog::InnerWidget *_list = nullptr;

	};

	[[nodiscard]] static uint32 Epoch(
		not_null<const HistoryView::ElementDelegate*> owner);
	[[nodiscard]] static uint32 Consumed(
		not_null<const HistoryView::ElementDelegate*> owner);
	[[nodiscard]] static uint32 Consumed(
		not_null<const HistoryWidget*> widget);
	static void Consume(
		not_null<const HistoryView::ElementDelegate*> owner);
	[[nodiscard]] static uint32 Inputs(
		not_null<const HistoryView::ElementDelegate*> owner);
	[[nodiscard]] static uint32 Inputs(
		not_null<const HistoryWidget*> widget);
	static void Input(
		not_null<const HistoryView::ElementDelegate*> owner);
	static void Input(not_null<const HistoryWidget*> widget);
	static void WatchInput(
		not_null<Ui::ElasticScroll*> scroll,
		Fn<void()> input,
		Fn<bool(QPoint, Qt::ScrollPhase)> crossAxis = nullptr,
		bool locked = true,
		Fn<bool(not_null<QWheelEvent*>)> wheelProcess = nullptr);
	static void WatchInput(
		not_null<Ui::ScrollArea*> scroll,
		Fn<void()> input);

	[[nodiscard]] static ScrollSnapshot Capture(
		not_null<HistoryWidget*> widget,
		bool atEnd);
	[[nodiscard]] static ScrollSnapshot CaptureDirect(
		not_null<HistoryWidget*> widget);
	[[nodiscard]] static bool Keep(
		not_null<HistoryWidget*> widget,
		const ScrollSnapshot &snapshot);
	[[nodiscard]] static Fn<std::optional<int>()> Placement(
		not_null<const HistoryWidget*> widget,
		FullMsgId id);

	[[nodiscard]] static ScrollSnapshot Capture(
		not_null<HistoryView::ListWidget*> list);
	[[nodiscard]] static ScrollSnapshot CaptureSaved(
		not_null<HistoryView::ListWidget*> list);
	static void Keep(
		not_null<HistoryView::ListWidget*> list,
		const ScrollSnapshot &snapshot);
	[[nodiscard]] static Fn<std::optional<int>()> Placement(
		not_null<const HistoryView::ListWidget*> list,
		Data::MessagePosition position);

	[[nodiscard]] static ScrollSnapshot Capture(
		not_null<const AdminLog::InnerWidget*> list);
	static void CaptureAbove(
		not_null<const AdminLog::InnerWidget*> list,
		const HistoryView::Element *anchor,
		ScrollSnapshot &snapshot);
	[[nodiscard]] static Expansion TrackExpansion(
		not_null<const AdminLog::InnerWidget*> list,
		const ScrollSnapshot &snapshot);
	static void Keep(
		not_null<AdminLog::InnerWidget*> list,
		const ScrollSnapshot &snapshot);
	[[nodiscard]] static ScrollShift Adjust(
		not_null<AdminLog::InnerWidget*> list,
		const ScrollSnapshot &snapshot,
		int target,
		const Expansion *expansion = nullptr);
	[[nodiscard]] static Fn<std::optional<int>()> Placement(
		not_null<AdminLog::InnerWidget*> list,
		FullMsgId id,
		MessageHighlightId highlight);
	[[nodiscard]] static Fn<std::optional<int>()> Placement(
		not_null<AdminLog::InnerWidget*> list,
		HistoryView::Element *anchor,
		int delta,
		int target);

private:
	[[nodiscard]] static std::optional<int> Destination(
		not_null<const HistoryWidget*> widget,
		FullMsgId id);
	[[nodiscard]] static std::optional<int> Destination(
		not_null<const HistoryView::ListWidget*> list,
		Data::MessagePosition position);
	[[nodiscard]] static std::optional<int> Destination(
		not_null<AdminLog::InnerWidget*> list,
		FullMsgId id,
		const MessageHighlightId &highlight);
	[[nodiscard]] static std::optional<int> Destination(
		not_null<AdminLog::InnerWidget*> list,
		const base::weak_ptr<HistoryView::Element> &anchor,
		int delta,
		int offset);
	[[nodiscard]] static HistoryView::Element *Displayed(
		not_null<AdminLog::InnerWidget*> list,
		FullMsgId id);

};

class ScrollFlight final {
public:
	void start(
		int from,
		int to,
		Fn<uint32()> consumed,
		uint32 count,
		Fn<uint32()> inputs,
		Fn<std::optional<int>()> end);
	void animate(
		not_null<Ui::Animations::Simple*> animation,
		Fn<void()> callback,
		float64 from,
		float64 to,
		crl::time duration,
		anim::transition transition);
	void frame(
		int reference,
		float64 value,
		int scrollTop,
		bool finished,
		const Fn<void(int)> &apply);

private:
	enum class State : uchar {
		Inactive,
		Riding,
		Continuation,
		Completed,
		Cancelled,
	};

	[[nodiscard]] bool takenOver() const;
	[[nodiscard]] bool continueFrom(int start);
	[[nodiscard]] int place(float64 progress) const;

	Fn<uint32()> _consumed;
	Fn<uint32()> _inputs;
	Fn<std::optional<int>()> _end;
	Fn<void()> _callback;
	Ui::Animations::Simple *_animation = nullptr;
	float64 _animationFrom = 0.;
	float64 _animationTo = 0.;
	float64 _progress = 0.;
	float64 _anchorProgress = 0.;
	int _from = 0;
	int _to = 0;
	int _placed = 0;
	int _anchor = 0;
	int _target = 0;
	int _travel = 0;
	int _desired = 0;
	uint32 _seen = 0;
	uint32 _inputsSeen = 0;
	uint32 _generation = 0;
	State _state = State::Inactive;
	bool _pending = false;

};

} // namespace Tgw
