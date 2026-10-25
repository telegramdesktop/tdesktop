/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/data_msg_id.h"

class HistoryWidget;

namespace AdminLog {
class InnerWidget;
} // namespace AdminLog

namespace HistoryView {
class Element;
class ElementDelegate;
class ListWidget;
} // namespace HistoryView

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
	int scrollTop = 0;
	int viewport = 0;
	int offset = 0;
	bool atEnd = false;
	bool direct = false;
	bool saved = false;
};

struct ScrollShift {
	int target = 0;
	int difference = 0;
};

class ListScroll final {
public:
	[[nodiscard]] static uint32 Epoch(
		not_null<const HistoryView::ElementDelegate*> owner);

	[[nodiscard]] static ScrollSnapshot Capture(
		not_null<HistoryWidget*> widget,
		bool atEnd);
	[[nodiscard]] static ScrollSnapshot CaptureDirect(
		not_null<HistoryWidget*> widget);
	[[nodiscard]] static bool Keep(
		not_null<HistoryWidget*> widget,
		const ScrollSnapshot &snapshot);

	[[nodiscard]] static ScrollSnapshot Capture(
		not_null<HistoryView::ListWidget*> list);
	[[nodiscard]] static ScrollSnapshot CaptureSaved(
		not_null<HistoryView::ListWidget*> list);
	static void Keep(
		not_null<HistoryView::ListWidget*> list,
		const ScrollSnapshot &snapshot);

	[[nodiscard]] static ScrollSnapshot Capture(
		not_null<const AdminLog::InnerWidget*> list);
	static void Keep(
		not_null<AdminLog::InnerWidget*> list,
		const ScrollSnapshot &snapshot);
	[[nodiscard]] static ScrollShift Adjust(
		not_null<AdminLog::InnerWidget*> list,
		const ScrollSnapshot &snapshot,
		int target);

private:
	[[nodiscard]] static HistoryView::Element *Displayed(
		not_null<AdminLog::InnerWidget*> list,
		FullMsgId id);

};

} // namespace Tgw
