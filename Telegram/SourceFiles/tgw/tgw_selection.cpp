/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_selection.h"

#include "base/platform/base_platform_info.h"
#include "base/event_filter.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_runtime.h"
#include "ui/layers/show.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/popup_menu.h"
#include "ui/integration.h"

#include <QtWidgets/QApplication>

namespace Tgw {
namespace {

using HistoryView::CopyRestrictionType;

struct Pressed {
	RendererCore *core = nullptr;
	HistoryItem *item = nullptr;
};

[[nodiscard]] Pressed PressedWidget() {
	const auto view = HistoryView::Element::PressedLink();
	const auto runtime = view ? FindRuntime(view) : nullptr;
	const auto core = runtime ? runtime->core() : nullptr;
	return (core && core->pressed())
		? Pressed{ .core = core, .item = runtime->item() }
		: Pressed();
}

[[nodiscard]] CopyRestrictionType Restriction(not_null<HistoryItem*> item) {
	return HistoryView::CopyRestrictionTypeFor(item->history()->peer, item);
}

void TextActionsUpdated() {
	Ui::Integration::Instance().textActionsUpdated();
}

} // namespace

WidgetSelection::WidgetSelection(WidgetSelectionArgs &&args)
: _list(args.list)
, _clearText(std::move(args.clearText))
, _changed(std::move(args.changed))
, _show(std::move(args.show)) {
}

WidgetSelection::~WidgetSelection() {
	if (const auto core = _held.get()) {
		core->clearSelection();
	}
	if (const auto core = _pressed.get()) {
		core->clearSelection();
	}
}

void WidgetSelection::listPress(Qt::MouseButton button, bool menuShown) {
	if (button != Qt::RightButton
		&& (menuShown || button != Qt::LeftButton)) {
		dismiss();
	}
}

void WidgetSelection::press() {
	const auto pressed = PressedWidget();
	_restore = drop(pressed.core);
	_pressed = pressed.core;
	_pressedItem = pressed.item;
	if (_restore) {
		_changed(false);
	}
}

void WidgetSelection::update() {
	const auto core = _pressed.get();
	if (core && core->hasSelection()) {
		adopt();
	}
}

void WidgetSelection::release() {
	update();
	_pressed = nullptr;
	_pressedItem = nullptr;
	const auto restore = base::take(_restore);
	if (has()) {
		return;
	}
	const auto dropped = drop(nullptr);
	if (dropped || restore) {
		_changed(restore);
	}
}

void WidgetSelection::dismiss() {
	if (clear()) {
		_changed(true);
	}
}

bool WidgetSelection::clear() {
	if (const auto core = base::take(_pressed).get()) {
		core->clearSelection();
	}
	_pressedItem = nullptr;
	return drop(nullptr);
}

void WidgetSelection::watch(Ui::PopupMenu *menu) {
	if (!menu || !_heldItem || _watched.get() == menu) {
		return;
	}
	_watched = menu;
	// The popup takes the press that dismisses it, the list never sees it.
	base::install_event_filter(menu, [weak = base::make_weak(this)](
			not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			if (const auto strong = weak.get()) {
				strong->menuPress(static_cast<QMouseEvent*>(e.get()));
			}
		}
		return base::EventFilterResult::Continue;
	});
}

bool WidgetSelection::has() const {
	const auto core = _held.get();
	return core && core->hasSelection();
}

HistoryItem *WidgetSelection::item() const {
	return has() ? _heldItem : nullptr;
}

TextForMimeData WidgetSelection::text() const {
	const auto core = _held.get();
	return core
		? TextForMimeData::Simple(core->selectedText())
		: TextForMimeData();
}

bool WidgetSelection::copyRestricted() const {
	const auto item = this->item();
	return item && (Restriction(item) != CopyRestrictionType::None);
}

bool WidgetSelection::showCopyRestriction() const {
	const auto item = this->item();
	return item && ShowCopyRestriction(Restriction(item), _show);
}

bool WidgetSelection::drop(const RendererCore *pressed) {
	if (!_heldItem) {
		return false;
	}
	const auto core = base::take(_held).get();
	_heldItem = nullptr;
	if (core && core != pressed) {
		core->clearSelection();
	}
	TextActionsUpdated();
	return true;
}

void WidgetSelection::adopt() {
	// clearText() re-enters clear(), which must not find this selection.
	auto core = base::take(_pressed);
	const auto item = base::take(_pressedItem);
	_clearText();
	_held = std::move(core);
	_heldItem = item;
	_restore = true;
	_list->setFocus();
	_changed(false);
	TextActionsUpdated();
}

void WidgetSelection::menuPress(not_null<QMouseEvent*> e) {
	const auto over = QApplication::widgetAt(e->globalPosition().toPoint());
	if (_list->isAncestorOf(over)) {
		listPress(e->button(), true);
	}
}

bool ShowCopyRestriction(
		CopyRestrictionType type,
		const Fn<std::shared_ptr<Ui::Show>()> &show) {
	if (type == CopyRestrictionType::None) {
		return false;
	}
	show()->showToast((type == CopyRestrictionType::Channel)
		? tr::lng_error_nocopy_channel(tr::now)
		: (type == CopyRestrictionType::User)
		? tr::lng_error_nocopy_user(tr::now)
		: tr::lng_error_nocopy_group(tr::now));
	return true;
}

void RefreshNativeCopy() {
	if constexpr (Platform::IsMac()) {
		TextActionsUpdated();
	}
}

} // namespace Tgw
