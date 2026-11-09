/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/weak_qptr.h"

class HistoryInner;
class HistoryItem;
class QMouseEvent;
class QWidget;
struct TextForMimeData;

namespace AdminLog {
class InnerWidget;
} // namespace AdminLog

namespace HistoryView {
class ListWidget;
enum class CopyRestrictionType : char;
} // namespace HistoryView

namespace Ui {
class PopupMenu;
class Show;
} // namespace Ui

namespace Tgw {

class RendererCore;

struct WidgetSelectionArgs {
	not_null<QWidget*> list;
	Fn<void()> clearText;
	Fn<void(bool restoreFocus)> changed;
	Fn<std::shared_ptr<Ui::Show>()> show;
};

class WidgetSelection final : public base::has_weak_ptr {
public:
	explicit WidgetSelection(WidgetSelectionArgs &&args);
	~WidgetSelection();

	void listPress(Qt::MouseButton button, bool menuShown);
	void press();
	void update();
	void release();
	void dismiss();
	bool clear();
	void watch(Ui::PopupMenu *menu);

	[[nodiscard]] bool has() const;
	[[nodiscard]] HistoryItem *item() const;
	[[nodiscard]] TextForMimeData text() const;
	[[nodiscard]] bool copyRestricted() const;
	bool showCopyRestriction() const;

private:
	bool drop(const RendererCore *pressed);
	void adopt();
	void menuPress(not_null<QMouseEvent*> e);

	const not_null<QWidget*> _list;
	const Fn<void()> _clearText;
	const Fn<void(bool)> _changed;
	const Fn<std::shared_ptr<Ui::Show>()> _show;
	base::weak_ptr<RendererCore> _held;
	base::weak_ptr<RendererCore> _pressed;
	base::weak_qptr<Ui::PopupMenu> _watched;
	HistoryItem *_heldItem = nullptr;
	HistoryItem *_pressedItem = nullptr;
	bool _restore = false;

};

class ListSelection final {
public:
	[[nodiscard]] static std::unique_ptr<WidgetSelection> Create(
		not_null<HistoryInner*> list);
	[[nodiscard]] static std::unique_ptr<WidgetSelection> Create(
		not_null<HistoryView::ListWidget*> list);
	[[nodiscard]] static std::unique_ptr<WidgetSelection> Create(
		not_null<AdminLog::InnerWidget*> list);

	static void FillMenu(not_null<HistoryInner*> list, HistoryItem *item);
	static void FillMenu(
		not_null<AdminLog::InnerWidget*> list,
		HistoryItem *item);
	static bool ShowRestriction(not_null<HistoryView::ListWidget*> list);
	static void Watch(
		not_null<QWidget*> list,
		not_null<Ui::PopupMenu*> menu);

};

bool ShowCopyRestriction(
	HistoryView::CopyRestrictionType type,
	const Fn<std::shared_ptr<Ui::Show>()> &show);
void RefreshNativeCopy();

} // namespace Tgw
