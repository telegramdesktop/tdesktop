/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_chat_menu.h"

#include "base/options.h"
#include "base/unique_qptr.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "data/data_groups.h"
#include "data/data_history_messages.h"
#include "data/data_messages.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h"
#include "history/history.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "history/history_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "storage/storage_shared_media.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_marking_read.h"
#include "test/test_widgets.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtGui/QEnterEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>

namespace Test {
namespace {

using Element = HistoryView::Element;
using ModernList = HistoryView::ListWidget;

// The test-only private-member accessor of test_notify_override.cpp, kept
// file-private here. Access checking does not apply to the names in an
// explicit instantiation, so instantiating PrivateMember with a private
// member pointer defines Get(Tag) returning it. Every use below only reads:
// the resolution the list's mouseActionUpdate() left behind, its gate, its
// visible area and its own menu. Nothing here writes product state.
template <typename Tag, typename Tag::type Member>
struct PrivateMember {
	friend typename Tag::type Get(Tag) {
		return Member;
	}
};

struct InnerMouseActiveTag {
	using type = bool HistoryInner::*;
	friend type Get(InnerMouseActiveTag);
};

template struct PrivateMember<
	InnerMouseActiveTag,
	&HistoryInner::_mouseActive>;

struct InnerDragStateItemTag {
	using type = HistoryItem *HistoryInner::*;
	friend type Get(InnerDragStateItemTag);
};

template struct PrivateMember<
	InnerDragStateItemTag,
	&HistoryInner::_dragStateItem>;

struct InnerVisibleTopTag {
	using type = int HistoryInner::*;
	friend type Get(InnerVisibleTopTag);
};

template struct PrivateMember<
	InnerVisibleTopTag,
	&HistoryInner::_visibleAreaTop>;

struct InnerVisibleBottomTag {
	using type = int HistoryInner::*;
	friend type Get(InnerVisibleBottomTag);
};

template struct PrivateMember<
	InnerVisibleBottomTag,
	&HistoryInner::_visibleAreaBottom>;

struct InnerMenuTag {
	using type = base::unique_qptr<Ui::PopupMenu> HistoryInner::*;
	friend type Get(InnerMenuTag);
};

template struct PrivateMember<InnerMenuTag, &HistoryInner::_menu>;

// The product's own gate: its history's or the migrated history's pending
// resized items.
struct InnerPendingResizeTag {
	using type = bool (HistoryInner::*)() const;
	friend type Get(InnerPendingResizeTag);
};

template struct PrivateMember<
	InnerPendingResizeTag,
	&HistoryInner::hasPendingResizedItems>;

struct ListMouseActiveTag {
	using type = bool ModernList::*;
	friend type Get(ListMouseActiveTag);
};

template struct PrivateMember<ListMouseActiveTag, &ModernList::_mouseActive>;

struct ListOverElementTag {
	using type = Element *ModernList::*;
	friend type Get(ListOverElementTag);
};

template struct PrivateMember<ListOverElementTag, &ModernList::_overElement>;

struct ListOverItemExactTag {
	using type = HistoryItem *ModernList::*;
	friend type Get(ListOverItemExactTag);
};

template struct PrivateMember<
	ListOverItemExactTag,
	&ModernList::_overItemExact>;

struct ListVisibleTopTag {
	using type = int ModernList::*;
	friend type Get(ListVisibleTopTag);
};

template struct PrivateMember<ListVisibleTopTag, &ModernList::_visibleTop>;

struct ListVisibleBottomTag {
	using type = int ModernList::*;
	friend type Get(ListVisibleBottomTag);
};

template struct PrivateMember<
	ListVisibleBottomTag,
	&ModernList::_visibleBottom>;

struct ListMenuTag {
	using type = base::unique_qptr<Ui::PopupMenu> ModernList::*;
	friend type Get(ListMenuTag);
};

template struct PrivateMember<ListMenuTag, &ModernList::_menu>;

// Read by the self-test only, for its points: the README's ownership rule
// resolves the list's own view of a message, and this is where that view
// sits in the list (HistoryInner::itemTop is public; this one is not).
struct ListItemTopTag {
	using type = int (ModernList::*)(not_null<const Element*>) const;
	friend type Get(ListItemTopTag);
};

template struct PrivateMember<ListItemTopTag, &ModernList::itemTop>;

// The few readings the reader needs from one real chat list. The base
// holds the list as a QPointer; every other method dereferences it, so
// call them only while alive() answers true.
class ListAdapter {
public:
	virtual ~ListAdapter() = default;

	[[nodiscard]] bool alive() const;
	[[nodiscard]] QWidget *widget() const;
	[[nodiscard]] HistoryItem *message(FullMsgId id) const;

	[[nodiscard]] virtual QString kind() const = 0;
	[[nodiscard]] virtual Main::Session &session() const = 0;
	// Empty when the list builds context menus at all.
	[[nodiscard]] virtual QString menuless() const = 0;
	// The list's own view of |item|, or null when it shows none.
	[[nodiscard]] virtual Element *ownView(HistoryItem *item) const = 0;
	[[nodiscard]] virtual QRect viewport() const = 0;
	[[nodiscard]] virtual bool pendingResize() const = 0;
	// Empty unless the list holds a selection of messages or text.
	[[nodiscard]] virtual QString selection() const = 0;
	[[nodiscard]] virtual ChatListResolution resolve(
		FullMsgId target) const = 0;
	[[nodiscard]] virtual QWidget *ownMenu() const = 0;

protected:
	explicit ListAdapter(not_null<QWidget*> widget);

private:
	const QPointer<QWidget> _widget;

};

class HistoryInnerAdapter final : public ListAdapter {
public:
	explicit HistoryInnerAdapter(not_null<HistoryInner*> inner);

	[[nodiscard]] QString kind() const override;
	[[nodiscard]] Main::Session &session() const override;
	[[nodiscard]] QString menuless() const override;
	[[nodiscard]] Element *ownView(HistoryItem *item) const override;
	[[nodiscard]] QRect viewport() const override;
	[[nodiscard]] bool pendingResize() const override;
	[[nodiscard]] QString selection() const override;
	[[nodiscard]] ChatListResolution resolve(
		FullMsgId target) const override;
	[[nodiscard]] QWidget *ownMenu() const override;

private:
	const not_null<HistoryInner*> _inner;

};

class ListWidgetAdapter final : public ListAdapter {
public:
	explicit ListWidgetAdapter(not_null<ModernList*> list);

	[[nodiscard]] QString kind() const override;
	[[nodiscard]] Main::Session &session() const override;
	[[nodiscard]] QString menuless() const override;
	[[nodiscard]] Element *ownView(HistoryItem *item) const override;
	[[nodiscard]] QRect viewport() const override;
	[[nodiscard]] bool pendingResize() const override;
	[[nodiscard]] QString selection() const override;
	[[nodiscard]] ChatListResolution resolve(
		FullMsgId target) const override;
	[[nodiscard]] QWidget *ownMenu() const override;

private:
	const not_null<ModernList*> _list;

};

[[nodiscard]] QString IdText(FullMsgId id) {
	return id
		? u"%1:%2"_q.arg(id.peer.value).arg(qint64(id.msg.bare))
		: u"none"_q;
}

[[nodiscard]] FullMsgId IdOf(const HistoryItem *item) {
	return item ? item->fullId() : FullMsgId();
}

[[nodiscard]] FullMsgId IdOf(const Element *view) {
	return view ? view->data()->fullId() : FullMsgId();
}

// An address, printed and never dereferenced, so two readings can say
// whether they name the same view.
[[nodiscard]] QString ElementIdentity(const Element *view) {
	return view
		? u"element@0x%1"_q.arg(
			qulonglong(reinterpret_cast<quintptr>(view)),
			0,
			16)
		: u"none"_q;
}

[[nodiscard]] QString SelectionText(int messages, bool text) {
	if (messages > 0) {
		return u"%1 selected messages"_q.arg(messages);
	} else if (text) {
		return u"a text selection"_q;
	}
	return QString();
}

ListAdapter::ListAdapter(not_null<QWidget*> widget)
: _widget(widget.get()) {
}

bool ListAdapter::alive() const {
	return (_widget.data() != nullptr);
}

QWidget *ListAdapter::widget() const {
	return _widget.data();
}

HistoryItem *ListAdapter::message(FullMsgId id) const {
	return id ? session().data().message(id) : nullptr;
}

HistoryInnerAdapter::HistoryInnerAdapter(not_null<HistoryInner*> inner)
: ListAdapter(inner)
, _inner(inner) {
}

QString HistoryInnerAdapter::kind() const {
	return u"HistoryInner"_q;
}

Main::Session &HistoryInnerAdapter::session() const {
	return _inner->session();
}

QString HistoryInnerAdapter::menuless() const {
	return QString();
}

Element *HistoryInnerAdapter::ownView(HistoryItem *item) const {
	// itemTop() answers -2 for a view of another history and -1 for one
	// that is not the item's main view or not shown.
	const auto view = item ? _inner->viewByItem(item) : nullptr;
	return (view && (_inner->itemTop(view) >= 0)) ? view : nullptr;
}

QRect HistoryInnerAdapter::viewport() const {
	const auto top = _inner.get()->*Get(InnerVisibleTopTag());
	const auto bottom = _inner.get()->*Get(InnerVisibleBottomTag());
	return QRect(0, top, _inner->width(), std::max(bottom - top, 0));
}

bool HistoryInnerAdapter::pendingResize() const {
	return (_inner.get()->*Get(InnerPendingResizeTag()))();
}

QString HistoryInnerAdapter::selection() const {
	const auto state = _inner->getSelectionState();
	return SelectionText(state.count, state.textSelected);
}

ChatListResolution HistoryInnerAdapter::resolve(FullMsgId target) const {
	auto result = ChatListResolution();
	result.read = true;
	const auto targetView = ownView(message(target));
	// Element's statics are cleared when their view is destroyed
	// (Data::Session::unregisterItemView), and HistoryInner::itemRemoved
	// clears _dragStateItem, so every pointer read here is live.
	const auto moused = Element::Moused();
	const auto hovered = Element::Hovered();
	const auto hoveredLink = Element::HoveredLink();
	const auto exact = _inner.get()->*Get(InnerDragStateItemTag());
	result.targetShown = (targetView != nullptr);
	result.view = IdOf(moused);
	result.viewIdentity = ElementIdentity(moused);
	result.viewOwned = moused && (_inner->itemTop(moused) >= 0);
	result.viewIsTarget = targetView && (moused == targetView);
	result.hovered = IdOf(hovered);
	result.hoveredLink = IdOf(hoveredLink);
	result.exact = IdOf(exact);
	// HistoryInner::showContextMenu's plain branch: Hovered, else
	// HoveredLink; with neither it offers only Moused's Select.
	result.menuItem = hovered
		? IdOf(hovered)
		: hoveredLink
		? IdOf(hoveredLink)
		: FullMsgId();
	result.mouseActive = _inner.get()->*Get(InnerMouseActiveTag());
	result.activeWindow = _inner->window()->isActiveWindow();
	result.pendingResize = pendingResize();
	result.gateOpen = !result.pendingResize
		&& (result.mouseActive || result.activeWindow);
	return result;
}

QWidget *HistoryInnerAdapter::ownMenu() const {
	return (_inner.get()->*Get(InnerMenuTag())).get();
}

ListWidgetAdapter::ListWidgetAdapter(not_null<ModernList*> list)
: ListAdapter(list)
, _list(list) {
}

QString ListWidgetAdapter::kind() const {
	return u"HistoryView::ListWidget"_q;
}

Main::Session &ListWidgetAdapter::session() const {
	return _list->session();
}

QString ListWidgetAdapter::menuless() const {
	return _list->controllerOrNull()
		? QString()
		: u"a HistoryView::ListWidget with no window controller (the chat "
			"preview's), whose showContextMenu builds no menu"_q;
}

Element *ListWidgetAdapter::ownView(HistoryItem *item) const {
	// The README's ownership rule: the list's own lookup answers for the
	// exact message, with an Element whose data() is that item and whose
	// delegate() is this list.
	const auto view = item
		? _list->viewByPosition(item->position())
		: nullptr;
	const auto delegate = static_cast<HistoryView::ElementDelegate*>(
		_list.get());
	return (view
		&& (view->data() == item)
		&& (view->delegate().get() == delegate))
		? view
		: nullptr;
}

QRect ListWidgetAdapter::viewport() const {
	const auto top = _list.get()->*Get(ListVisibleTopTag());
	const auto bottom = _list.get()->*Get(ListVisibleBottomTag());
	return QRect(0, top, _list->width(), std::max(bottom - top, 0));
}

bool ListWidgetAdapter::pendingResize() const {
	// ListWidget::mouseActionUpdate() has no pending-resize gate.
	return false;
}

QString ListWidgetAdapter::selection() const {
	return SelectionText(
		int(_list->getSelectedIds().size()),
		!_list->getSelectedText().empty());
}

ChatListResolution ListWidgetAdapter::resolve(FullMsgId target) const {
	auto result = ChatListResolution();
	result.read = true;
	const auto targetView = ownView(message(target));
	// ListWidget keeps both pointers valid itself: showContextMenu
	// dereferences _overElement, and ListWidget::itemRemoved clears
	// _overItemExact.
	const auto over = _list.get()->*Get(ListOverElementTag());
	const auto exact = _list.get()->*Get(ListOverItemExactTag());
	const auto delegate = static_cast<HistoryView::ElementDelegate*>(
		_list.get());
	result.targetShown = (targetView != nullptr);
	result.view = IdOf(over);
	result.viewIdentity = ElementIdentity(over);
	result.viewOwned = over && (over->delegate().get() == delegate);
	result.viewIsTarget = targetView && (over == targetView);
	result.exact = IdOf(exact);
	// ListWidget::showContextMenu's overItem.
	result.menuItem = exact ? IdOf(exact) : IdOf(over);
	result.mouseActive = _list.get()->*Get(ListMouseActiveTag());
	result.activeWindow = _list->window()->isActiveWindow();
	result.gateOpen = result.mouseActive || result.activeWindow;
	return result;
}

QWidget *ListWidgetAdapter::ownMenu() const {
	return (_list.get()->*Get(ListMenuTag())).get();
}

[[nodiscard]] std::unique_ptr<ListAdapter> MakeAdapter(
		not_null<QWidget*> widget) {
	if (const auto inner = dynamic_cast<HistoryInner*>(widget.get())) {
		return std::make_unique<HistoryInnerAdapter>(inner);
	} else if (const auto list = dynamic_cast<ModernList*>(widget.get())) {
		return std::make_unique<ListWidgetAdapter>(list);
	}
	return nullptr;
}

// What the list resolved right now, or a reading that says it was never
// taken when the list is gone.
[[nodiscard]] ChatListResolution ReadChatListResolution(
		const ListAdapter &adapter,
		FullMsgId target) {
	return adapter.alive()
		? adapter.resolve(target)
		: ChatListResolution();
}

[[nodiscard]] QString ResolutionText(const ChatListResolution &reading) {
	if (!reading.read) {
		return u"not read"_q;
	}
	const auto flag = [](bool value) {
		return value ? 1 : 0;
	};
	const auto fields = QStringList{
		u"gate=%1"_q.arg(flag(reading.gateOpen)),
		u"mouseActive=%1"_q.arg(flag(reading.mouseActive)),
		u"activeWindow=%1"_q.arg(flag(reading.activeWindow)),
		u"pendingResize=%1"_q.arg(flag(reading.pendingResize)),
		u"targetShown=%1"_q.arg(flag(reading.targetShown)),
		u"view=%1"_q.arg(IdText(reading.view)),
		u"viewIdentity=%1"_q.arg(reading.viewIdentity),
		u"viewOwned=%1"_q.arg(flag(reading.viewOwned)),
		u"viewIsTarget=%1"_q.arg(flag(reading.viewIsTarget)),
		u"hovered=%1"_q.arg(IdText(reading.hovered)),
		u"hoveredLink=%1"_q.arg(IdText(reading.hoveredLink)),
		u"exact=%1"_q.arg(IdText(reading.exact)),
		u"menuItem=%1"_q.arg(IdText(reading.menuItem)),
	};
	return fields.join(u" "_q);
}

// Fills |result| with the refusal and answers true when the request
// cannot be delivered. Nothing has been sent at this point.
[[nodiscard]] bool RefuseBeforeDelivery(
		ChatListMenuReading &result,
		const ListAdapter &adapter,
		HistoryItem *target,
		QPoint point) {
	using Refusal = ChatListMenuRefusal;
	const auto refuse = [&](Refusal refusal, const QString &reason) {
		result.refusal = refusal;
		result.reason = reason + u"; nothing was sent"_q;
		return true;
	};
	const auto menuless = adapter.menuless();
	if (!menuless.isEmpty()) {
		return refuse(Refusal::NotAChatList, menuless);
	} else if (!target) {
		return refuse(
			Refusal::NoTarget,
			u"no target message was handed to the reader"_q);
	}
	const auto view = adapter.ownView(target);
	result.requestedView = ElementIdentity(view);
	result.viewport = adapter.viewport();
	if (!view) {
		return refuse(
			Refusal::ForeignTarget,
			u"the list shows no view of its own for message %1"_q.arg(
				IdText(result.requested)));
	} else if (!result.viewport.contains(point)) {
		return refuse(
			Refusal::OutsideViewport,
			u"the point %1,%2 is outside the list's visible message "
			"viewport %3, where mouseActionUpdate() would clamp it onto "
			"another row"_q
				.arg(point.x())
				.arg(point.y())
				.arg(RectText(result.viewport)));
	} else if (adapter.pendingResize()) {
		return refuse(
			Refusal::PendingResize,
			u"the HistoryInner has pending resized items in its history or "
			"the migrated one, so its mouseActionUpdate() returns before "
			"resolving any point"_q);
	} else if (adapter.session().data().groups().find(target)) {
		return refuse(
			Refusal::AmbiguousTarget,
			u"message %1 belongs to a grouped message (album), whose menu "
			"acts on the group or on one of its parts"_q.arg(
				IdText(result.requested)));
	}
	const auto selection = adapter.selection();
	if (!selection.isEmpty()) {
		return refuse(
			Refusal::AmbiguousTarget,
			u"the list already holds %1, so its menu would be built for "
			"that selection rather than for the target"_q.arg(selection));
	}
	return false;
}

void DeliverEnter(not_null<QWidget*> list, QPoint point) {
	auto enter = QEnterEvent(
		QPointF(point),
		QPointF(list->mapTo(list->window(), point)),
		QPointF(list->mapToGlobal(point)));
	Settle([&] {
		QApplication::sendEvent(list.get(), &enter);
	});
}

void DeliverLeave(not_null<QWidget*> list) {
	auto leave = QEvent(QEvent::Leave);
	Settle([&] {
		QApplication::sendEvent(list.get(), &leave);
	});
}

// Decides a delivered request from its recorded values, the first failing
// condition naming the refusal. Leaves |result| unrefused when the menu is
// the list's own and every candidate agreed with the target.
void DecideDelivered(ChatListMenuReading &result, bool listAlive) {
	using Refusal = ChatListMenuRefusal;
	const auto refuse = [&](Refusal refusal, const QString &reason) {
		result.refusal = refusal;
		result.reason = reason;
	};
	const auto &delivery = result.afterEnter;
	const auto &build = result.atBuild;
	const auto names = [&](FullMsgId id) {
		return id && (id != result.requested);
	};
	if (!listAlive) {
		refuse(
			Refusal::ListGone,
			u"the list was destroyed during the delivery, so nothing it "
			"built is read as its menu"_q);
	} else if (!result.menuRead) {
		refuse(
			Refusal::TargetGone,
			u"the target or the list's view of it was destroyed while the "
			"enter was handled, so no context-menu event was sent"_q);
	} else if (!result.menu.built()) {
		refuse(
			Refusal::MenuRefused,
			u"the menu reader refused: %1"_q.arg(
				ContextMenuRefusalName(result.menu.refusal)));
	} else if (!result.menu.menu) {
		refuse(
			Refusal::MenuRefused,
			u"the one menu the delivery built was destroyed before the "
			"reading returned"_q);
	} else if (!result.observed || !build.targetShown) {
		refuse(
			Refusal::TargetGone,
			u"at build the list showed no view of its own for the "
			"target"_q);
	} else if (!delivery.gateOpen) {
		refuse(
			Refusal::Unresolved,
			u"the list's resolution gate was closed when the event was "
			"delivered (mouseActive=%1 activeWindow=%2 pendingResize=%3), "
			"so mouseActionUpdate() returned without resolving the point "
			"and the menu was built from the last resolution"_q
				.arg(delivery.mouseActive ? 1 : 0)
				.arg(delivery.activeWindow ? 1 : 0)
				.arg(delivery.pendingResize ? 1 : 0));
	} else if (!build.view) {
		refuse(
			Refusal::Unresolved,
			u"at build the list resolved no message at the point"_q);
	} else if (!build.viewIsTarget) {
		refuse(
			Refusal::WrongTarget,
			u"at build the list resolved message %1 (%2, owned=%3), not "
			"the requested %4 (%5)"_q
				.arg(IdText(build.view))
				.arg(build.viewIdentity)
				.arg(build.viewOwned ? 1 : 0)
				.arg(IdText(result.requested))
				.arg(result.requestedView));
	} else if (names(build.hovered)
		|| names(build.hoveredLink)
		|| names(build.exact)
		|| names(build.menuItem)) {
		refuse(
			Refusal::AmbiguousTarget,
			u"at build the list resolved the target's view, but a candidate "
			"its menu takes entries from names another message (hovered=%1 "
			"hoveredLink=%2 exact=%3 menuItem=%4)"_q
				.arg(IdText(build.hovered))
				.arg(IdText(build.hoveredLink))
				.arg(IdText(build.exact))
				.arg(IdText(build.menuItem)));
	} else if (!result.listMenu) {
		refuse(
			Refusal::ForeignMenu,
			u"the fresh menu %1 is not the list's own _menu, the one its "
			"showContextMenu builds"_q.arg(result.menu.identity));
	} else if (!result.afterLeave.targetShown) {
		refuse(
			Refusal::TargetGone,
			u"the target or the list's view of it was destroyed while the "
			"postponed calls drained or the leave was handled"_q);
	} else if (result.afterLeave.mouseActive) {
		refuse(
			Refusal::MouseStillActive,
			u"after the leave the list still reads _mouseActive true"_q);
	}
}

// Hides the one menu this delivery built - the reading's fresh menu, which
// was not alive before the call, so no other menu is ever touched - and
// clears the reading's pointer, so a trigger through it refuses menu-gone.
void DropRefusedMenu(ChatListMenuReading &result) {
	const auto menu = dynamic_cast<Ui::PopupMenu*>(result.menu.menu.data());
	if (menu) {
		Settle([&] {
			menu->hideMenu(true);
		});
		result.menuHidden = true;
	}
	result.menu.menu.clear();
}

} // namespace

QString ChatListMenuRefusalName(ChatListMenuRefusal refusal) {
	using Refusal = ChatListMenuRefusal;
	switch (refusal) {
	case Refusal::None: return u"none"_q;
	case Refusal::NoList: return u"no-list"_q;
	case Refusal::NotAChatList: return u"not-a-chat-list"_q;
	case Refusal::NoTarget: return u"no-target"_q;
	case Refusal::ForeignTarget: return u"foreign-target"_q;
	case Refusal::OutsideViewport: return u"outside-viewport"_q;
	case Refusal::PendingResize: return u"pending-resize"_q;
	case Refusal::AmbiguousTarget: return u"ambiguous-target"_q;
	case Refusal::ListGone: return u"list-gone"_q;
	case Refusal::TargetGone: return u"target-gone"_q;
	case Refusal::MenuRefused: return u"menu-refused"_q;
	case Refusal::Unresolved: return u"unresolved"_q;
	case Refusal::WrongTarget: return u"wrong-target"_q;
	case Refusal::ForeignMenu: return u"foreign-menu"_q;
	case Refusal::MouseStillActive: return u"mouse-still-active"_q;
	}
	Unexpected("Refusal in Test::ChatListMenuRefusalName.");
}

ChatListMenuReading OpenChatListContextMenu(
		QWidget *list,
		HistoryItem *target,
		QPoint point) {
	auto result = ChatListMenuReading();
	result.local = point;
	result.requested = IdOf(target);
	if (!list) {
		result.refusal = ChatListMenuRefusal::NoList;
		result.reason = u"no list widget was handed to the reader - a "
			"destroyed QPointer reads as null - so nothing was sent"_q;
		return result;
	}
	result.list = list;
	result.listIdentity = WidgetDescription(list);
	const auto adapter = MakeAdapter(list);
	if (!adapter) {
		result.refusal = ChatListMenuRefusal::NotAChatList;
		result.reason = u"the widget is neither a HistoryInner nor a "
			"HistoryView::ListWidget, so nothing was sent; read its menu "
			"with Test::OpenContextMenu"_q;
		return result;
	}
	result.kind = adapter->kind();
	if (RefuseBeforeDelivery(result, *adapter, target, point)) {
		return result;
	}
	// From here on the target is named only by its id and re-resolved
	// through the list's session after every delivery and drain.
	const auto targetId = result.requested;
	result.before = adapter->resolve(targetId);

	DeliverEnter(list, point);
	result.enterSent = true;
	result.afterEnter = ReadChatListResolution(*adapter, targetId);
	auto listMenuAtBuild = QPointer<QWidget>();
	if (adapter->alive() && adapter->ownView(adapter->message(targetId))) {
		result.menuRead = true;
		result.menu = OpenContextMenu(
			adapter->widget(),
			point,
			QContextMenuEvent::Mouse,
			[&] {
				// Synchronous, inside the delivery, with the list alive
				// (OpenContextMenu calls it only then).
				result.observed = true;
				result.atBuild = adapter->resolve(targetId);
				listMenuAtBuild = adapter->ownMenu();
			});
	}
	if (adapter->alive()) {
		DeliverLeave(adapter->widget());
		result.leaveSent = true;
	}
	result.afterLeave = ReadChatListResolution(*adapter, targetId);
	result.listMenu = (result.menu.menu.data() != nullptr)
		&& (result.menu.menu.data() == listMenuAtBuild.data());
	DecideDelivered(result, adapter->alive());
	if (result.refusal == ChatListMenuRefusal::None) {
		result.attributed = true;
	} else {
		DropRefusedMenu(result);
	}
	return result;
}

QString ChatListMenuDetails(const ChatListMenuReading &reading) {
	const auto orNone = [](const QString &value) {
		return value.isEmpty() ? u"none"_q : value;
	};
	const auto flag = [](bool value) {
		return value ? 1 : 0;
	};
	// One substitution per field, so a product's text that carries a %N
	// marker is never re-substituted by a later arg().
	const auto fields = QStringList{
		u"built=%1"_q.arg(flag(reading.built())),
		u"refusal=%1"_q.arg(ChatListMenuRefusalName(reading.refusal)),
		u"kind=%1"_q.arg(orNone(reading.kind)),
		u"list=%1"_q.arg(orNone(reading.listIdentity)),
		u"requested=%1"_q.arg(IdText(reading.requested)),
		u"requestedView=%1"_q.arg(orNone(reading.requestedView)),
		u"resolvedAtBuild=%1"_q.arg(IdText(reading.atBuild.view)),
		u"local=%1,%2"_q.arg(reading.local.x()).arg(reading.local.y()),
		u"viewport=%1"_q.arg(RectText(reading.viewport)),
		u"enter=%1"_q.arg(flag(reading.enterSent)),
		u"leave=%1"_q.arg(flag(reading.leaveSent)),
		u"observed=%1"_q.arg(flag(reading.observed)),
		u"listMenu=%1"_q.arg(flag(reading.listMenu)),
		u"menuHidden=%1"_q.arg(flag(reading.menuHidden)),
		u"before={%1}"_q.arg(ResolutionText(reading.before)),
		u"afterEnter={%1}"_q.arg(ResolutionText(reading.afterEnter)),
		u"atBuild={%1}"_q.arg(ResolutionText(reading.atBuild)),
		u"afterLeave={%1}"_q.arg(ResolutionText(reading.afterLeave)),
		u"menu={%1}"_q.arg(reading.menuRead
			? ContextMenuDetails(reading.menu)
			: u"not read"_q),
	};
	const auto line = u"chat-list menu: "_q + fields.join(u" "_q);
	return reading.reason.isEmpty()
		? line
		: (line + u" - "_q + reading.reason);
}

namespace {

using Way = Window::SectionShow::Way;

// The fixture's two fixed texts carry no link, mention, hashtag, cashtag or
// bot command, so HistoryItem::sharedMediaTypes() stays empty and the
// shared-media index - whose SparseIdsList::addNew asserts on an id above
// ServerMaxMsgId - never sees either id.
const auto kFixtureTextA = u"Harness chat-list menu fixture A"_q;
const auto kFixtureTextB = u"Harness chat-list menu fixture B"_q;

// A layout sample has to read the same for this long before its points are
// used: the item reveal animation HistoryWidget::newItemAdded starts and
// the selection-mode animation are both shorter.
constexpr auto kLayoutSettle = crl::time(400);

// The self-test's own bounded waits. Each is shorter than its stage's
// timeout, so a fixture that never becomes ready ends in a named
// fixture-gate FAIL printing its last reading, never in a stage timeout,
// which would skip the teardown stage and leave the release to onFinish.
constexpr auto kOpenBound = crl::time(20000);
constexpr auto kLayoutBound = crl::time(8000);
constexpr auto kClearBound = crl::time(3000);
constexpr auto kRestoreBound = crl::time(6000);

// The README's "Scenario teardown before quit" rule: a main-window state
// change is waited out in a stage, plus this, so no chat-background
// re-cache it started is still painting when the next self-test or the
// quit comes.
constexpr auto kRepaintSettle = crl::time(1500);

// How many ids from ServerMaxMsgId - 1 on the index reading asks for: both
// fixture ids, and room to show anything else listed there.
constexpr auto kIndexProbeLimit = 8;

// Where one fixture message sits in one list, read in the turn that uses
// it: |rect| is the view's Element::innerGeometry() in list coordinates
// and |point| its centre, inside the bubble.
struct TargetGeometry {
	QRect rect;
	QRect viewport;
	QPoint point;
	int top = -1;
	int height = 0;
	bool valid = false;
	bool inViewport = false;
};

// The Qt activation put back to the captured baseline, read in the turn
// that put it back.
struct ActivationRestore {
	WindowActivation reading;
	QString activeWindow;
	QString refusal;
	bool focusWindowSet = false;
	bool matches = false;
};

// A pending layout of the shown legacy history, processed by
// ProcessPendingLayout, with History::hasPendingResizedItems() read for it
// and the history it migrated from before and after.
struct LayoutFlush {
	QString details;
	bool pendingBefore = false;
	bool pendingAfter = false;
	bool processed = false;
};

// One list's premise, protected and trigger arms, all values.
struct PremiseLeg {
	ChatListMenuReading established;
	ChatListResolution afterEstablished;
	WindowActivation cleared;
	ChatListResolution premise;
	ContextMenuReading unprotected;
	ChatListResolution unprotectedAtBuild;
	WindowActivation clearedAgain;
	ChatListResolution beforeProtected;
	ChatListMenuReading protectedReading;
	MenuTrigger select;
	MessageIdsList selectedBefore;
	MessageIdsList selectedAfter;
	MarkingReadReading marking;
	ActivationRestore restored;
	QString points;
	QString refusal;
	int extraLeaves = 0;
	bool inactiveAtPremise = false;
	bool inactiveAtProtected = false;
	bool unprotectedObserved = false;
	bool ran = false;
};

// Clearing the selection the trigger arm made, before the controls.
struct ClearLeg {
	MessageIdsList before;
	MessageIdsList after;
	HistoryView::SelectionModeResult mode;
	QString layout;
	crl::time started = 0;
	bool settled = false;
	bool ran = false;
};

// One list's mismatch negative, refusal controls and active control.
struct ControlLeg {
	MessageIdsList selectedBefore;
	MessageIdsList selectedAfterMismatch;
	ChatListMenuReading mismatch;
	MenuTrigger mismatchSelect;
	ChatListMenuReading noList;
	ChatListMenuReading wrongWidget;
	ChatListMenuReading outside;
	ChatListMenuReading active;
	MarkingReadReading marking;
	MarkingReadReading markingAtActive;
	WindowActivation forced;
	ActivationRestore restored;
	QString wrongWidgetIdentity;
	QString points;
	QString refusal;
	QString activeRefusal;
	QPoint outsidePoint;
	bool activeAtControl = false;
	bool ran = false;
};

struct ListLeg {
	QString kind;
	QString selectionReader;
	QPointer<QWidget> list;
	QString identity;
	PremiseLeg premise;
	ClearLeg clear;
	ControlLeg control;
};

struct ReleaseReading {
	QString path;
	QStringList done;
	QString items;
	QString views;
	QString index;
	QString lists;
	QString option;
	QString chat;
	QString window;
	ActivationRestore activation;
	int menusHidden = 0;
	int menusOpen = 0;
	bool itemsGone = false;
	bool viewsGone = false;
	bool listsClean = false;
	bool optionRestored = false;
	bool chatRestored = false;
	bool windowRestored = false;
};

// The one State of the self-test. It holds the window, its session and
// the history only weakly and the lists and menus as QPointers, names the
// fixture messages by id, and keeps every reading as a value, so nothing
// it holds dangles across turns.
struct SelfTestState {
	base::weak_ptr<Window::Controller> controller;
	base::weak_ptr<Window::SessionController> navigation;
	base::weak_ptr<Main::Session> session;
	base::weak_ptr<History> history;
	PeerId self = 0;

	// The baseline, read before anything is changed.
	Qt::WindowStates windowState;
	QPointer<QWidget> activeWindow;
	QString activeWindowIdentity;
	QString focusWidgetIdentity;
	QString baselineActivation;
	QString baselineChat;
	QString baselineSelection;
	PeerId baselinePeer = 0;
	bool windowHidden = false;
	bool focusWindowSet = false;
	bool optionFound = false;
	bool captured = false;

	// Every mutation is marked the moment it is made, so the release knows
	// what to undo on any path.
	bool leverApplied = false;
	bool optionChanged = false;
	bool chatOpened = false;
	FullMsgId idA;
	FullMsgId idB;
	Data::MessagePosition positionA;
	Data::MessagePosition positionB;
	std::vector<QPointer<QWidget>> menus;

	ListLeg inner;
	ListLeg modern;

	// The bounded waits.
	MarkingReadReading lever;
	QString predicates;
	QString sample;
	QString waitDetails;
	crl::time waitStarted = 0;
	crl::time stableSince = 0;
	bool predicatesHold = false;

	// The release, idempotent across the teardown stage and onFinish.
	ReleaseReading release;
	crl::time restoredAt = 0;
	bool itemsReleased = false;
	bool optionRestored = false;
	bool chatClosed = false;
	bool windowRestored = false;

	QString gate;
};

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString IdsText(const MessageIdsList &ids) {
	auto list = QStringList();
	for (const auto &id : ids) {
		list.push_back(IdText(id));
	}
	return u"["_q + list.join(u","_q) + u"]"_q;
}

[[nodiscard]] QString FixtureIdsText(const SelfTestState &state) {
	return u"A="_q + IdText(state.idA) + u" B="_q + IdText(state.idB);
}

[[nodiscard]] bool LeverHolds(const MarkingReadReading &reading) {
	return reading.refusal.isEmpty()
		&& reading.isMinimized
		&& !reading.isHidden
		&& !reading.markingAsRead;
}

// Qt::WindowActive is a reading, not a state to restore: setting it back
// through QWidget::setWindowState would activate the window.
[[nodiscard]] Qt::WindowStates WithoutActive(Qt::WindowStates states) {
	auto result = states;
	result.setFlag(Qt::WindowActive, false);
	return result;
}

[[nodiscard]] ::MainWindow *WindowOf(const SelfTestState &state) {
	const auto controller = state.controller.get();
	return controller ? controller->widget().get() : nullptr;
}

[[nodiscard]] MainWidget *ContentOf(const SelfTestState &state) {
	const auto window = WindowOf(state);
	return window ? window->sessionContent() : nullptr;
}

[[nodiscard]] HistoryItem *Message(const SelfTestState &state, FullMsgId id) {
	const auto session = state.session.get();
	return (session && id) ? session->data().message(id) : nullptr;
}

[[nodiscard]] QString WindowStateText(Qt::WindowStates states) {
	return u"0x"_q + QString::number(WithoutActive(states).toInt(), 16);
}

[[nodiscard]] int ViewTop(QWidget *list, not_null<Element*> view) {
	if (const auto inner = dynamic_cast<HistoryInner*>(list)) {
		return inner->itemTop(view.get());
	} else if (const auto modern = dynamic_cast<ModernList*>(list)) {
		return (modern->*Get(ListItemTopTag()))(view.get());
	}
	return -1;
}

[[nodiscard]] TargetGeometry ReadTargetGeometry(
		QWidget *list,
		HistoryItem *item) {
	auto result = TargetGeometry();
	const auto adapter = list ? MakeAdapter(list) : nullptr;
	if (!adapter || !item) {
		return result;
	}
	result.viewport = adapter->viewport();
	const auto view = adapter->ownView(item);
	if (!view) {
		return result;
	}
	result.top = ViewTop(list, view);
	result.height = view->height();
	if (result.top < 0 || result.height <= 0) {
		return result;
	}
	result.rect = view->innerGeometry().translated(0, result.top);
	result.point = result.rect.center();
	result.valid = !result.rect.isEmpty();
	result.inViewport = result.valid
		&& result.viewport.contains(result.point);
	return result;
}

[[nodiscard]] QString TargetText(const TargetGeometry &geometry) {
	const auto fields = QStringList{
		u"valid=%1"_q.arg(FlagText(geometry.valid)),
		u"top=%1"_q.arg(geometry.top),
		u"height=%1"_q.arg(geometry.height),
		u"inner=%1"_q.arg(RectText(geometry.rect)),
		u"point=%1,%2"_q.arg(geometry.point.x()).arg(geometry.point.y()),
		u"viewport=%1"_q.arg(RectText(geometry.viewport)),
		u"inViewport=%1"_q.arg(FlagText(geometry.inViewport)),
	};
	return fields.join(u" "_q);
}

[[nodiscard]] MessageIdsList SelectedIds(QWidget *list) {
	if (const auto inner = dynamic_cast<HistoryInner*>(list)) {
		return inner->getSelectedItems();
	} else if (const auto modern = dynamic_cast<ModernList*>(list)) {
		return modern->getSelectedIds();
	}
	return {};
}

// Clears the selection of a list the self-test opened itself, which held
// none before the self-test touched it (a fixture gate), so only the
// test's own selection is cleared.
void ClearListSelection(QWidget *list) {
	if (const auto inner = dynamic_cast<HistoryInner*>(list)) {
		inner->clearSelected();
	} else if (const auto modern = dynamic_cast<ModernList*>(list)) {
		modern->cancelSelection();
	}
}

// Both lists start their selection-mode animation lazily, from the first
// inSelectionMode() reading after the selection changed - normally their
// paint path, which this minimized window never runs. So the first reading
// after a change also starts that animation; later ones only read it.
[[nodiscard]] HistoryView::SelectionModeResult SelectionMode(
		QWidget *list) {
	if (const auto inner = dynamic_cast<HistoryInner*>(list)) {
		return inner->inSelectionMode();
	} else if (const auto modern = dynamic_cast<ModernList*>(list)) {
		return modern->elementInSelectionMode(nullptr);
	}
	return {};
}

[[nodiscard]] QString SelectionModeText(
		const HistoryView::SelectionModeResult &mode) {
	return u"inSelectionMode=%1 progress=%2"_q
		.arg(FlagText(mode.inSelectionMode))
		.arg(mode.progress, 0, 'f', 3);
}

// Every menu the self-test made a list build is closed in the turn it was
// read, and remembered, so the release can prove none stayed open.
void HideSelfTestMenu(SelfTestState &state, QWidget *widget) {
	const auto menu = dynamic_cast<Ui::PopupMenu*>(widget);
	if (!menu) {
		return;
	}
	state.menus.push_back(menu);
	if (!menu->isHidden()) {
		Settle([&] {
			menu->hideMenu(true);
		});
	}
}

// Puts the Qt activation captured at the baseline back: the window that
// was active is activated through Test::ForceWindowActive, and a baseline
// with no focus window - the usual state after a default background macOS
// launch - is restored with Test::ClearWindowActive, which is the task's
// restore-to-found rule for this self-test only. With |guardMarking| a
// window is activated only after the not-marking-read lever was
// re-asserted and holds, because the injected messages exist then.
[[nodiscard]] ActivationRestore RestoreQtActivation(
		const SelfTestState &state,
		bool guardMarking) {
	auto result = ActivationRestore();
	if (!state.focusWindowSet) {
		result.reading = ClearWindowActive();
	} else if (const auto widget = state.activeWindow.data()) {
		const auto lever = guardMarking
			? KeepMainWindowNotMarkingRead(state.controller.get())
			: MarkingReadReading();
		if (guardMarking && !LeverHolds(lever)) {
			result.reading = ReadWindowActivation(widget);
			result.refusal = u"the main window is not minimized and not "
				"marking read while the injected messages exist, so nothing "
				"was activated: "_q + MarkingReadDetails(lever);
		} else {
			result.reading = ForceWindowActive(widget);
		}
	} else {
		result.reading = ReadWindowActivation(nullptr);
		result.refusal = u"the window active at the baseline ("_q
			+ state.activeWindowIdentity
			+ u") is gone, so nothing was activated"_q;
	}
	const auto active = QApplication::activeWindow();
	result.focusWindowSet = (QGuiApplication::focusWindow() != nullptr);
	result.activeWindow = active ? WidgetDescription(active) : u"none"_q;
	result.matches = (result.focusWindowSet == state.focusWindowSet)
		&& (active == state.activeWindow.data());
	return result;
}

[[nodiscard]] QString ActivationRestoreText(
		const SelfTestState &state,
		const ActivationRestore &restore) {
	const auto line = u"matches="_q
		+ FlagText(restore.matches)
		+ u" baseline{focusWindowSet="_q
		+ FlagText(state.focusWindowSet)
		+ u" activeWindow="_q
		+ state.activeWindowIdentity
		+ u"} now{focusWindowSet="_q
		+ FlagText(restore.focusWindowSet)
		+ u" activeWindow="_q
		+ restore.activeWindow
		+ u"} | "_q
		+ WindowActivationDetails(restore.reading);
	return restore.refusal.isEmpty()
		? line
		: (line + u" - "_q + restore.refusal);
}

// The selections of every chat list the main window shows, as found.
[[nodiscard]] QString ReadListSelections(QWidget *root) {
	auto parts = QStringList();
	for (const auto inner : FindAll<HistoryInner>(root)) {
		const auto text = HistoryInnerAdapter(inner).selection();
		parts.push_back(WidgetDescription(inner)
			+ u"="_q
			+ (text.isEmpty() ? u"none"_q : text));
	}
	for (const auto list : FindAll<ModernList>(root)) {
		const auto text = ListWidgetAdapter(list).selection();
		parts.push_back(WidgetDescription(list)
			+ u"="_q
			+ (text.isEmpty() ? u"none"_q : text));
	}
	return parts.isEmpty() ? u"no chat list"_q : parts.join(u"; "_q);
}

[[nodiscard]] QString BaselineText(const SelfTestState &state) {
	const auto fields = QStringList{
		u"windowState=%1"_q.arg(WindowStateText(state.windowState)),
		u"hidden=%1"_q.arg(FlagText(state.windowHidden)),
		u"focusWindowSet=%1"_q.arg(FlagText(state.focusWindowSet)),
		u"activeWindow=%1"_q.arg(state.activeWindowIdentity),
		u"focusWidget=%1"_q.arg(state.focusWidgetIdentity),
		u"use-modern-chat-view=%1"_q.arg(FlagText(state.optionFound)),
		u"chat={%1}"_q.arg(state.baselineChat),
		u"selections={%1}"_q.arg(state.baselineSelection),
		u"activation=[%1]"_q.arg(state.baselineActivation),
	};
	return fields.join(u" "_q);
}

// Reads the baseline. Answers why the self-test cannot run, or an empty
// string; nothing is changed here.
[[nodiscard]] QString CaptureBaseline(SelfTestState &state) {
	const auto controller = Core::App().activePrimaryWindow();
	const auto navigation = controller
		? controller->sessionController()
		: nullptr;
	if (!controller) {
		return u"Core::App().activePrimaryWindow() is null"_q;
	} else if (!navigation) {
		return u"the primary window has no session controller"_q;
	}
	const auto window = controller->widget().get();
	const auto content = window->sessionContent();
	if (!content) {
		return u"the primary window shows no session content"_q;
	}
	const auto session = &navigation->session();
	state.controller = base::make_weak(controller);
	state.navigation = base::make_weak(navigation);
	state.session = base::make_weak(session);
	state.self = session->userPeerId();
	state.history = base::make_weak(
		session->data().history(state.self).get());
	state.windowState = WithoutActive(window->windowState());
	state.windowHidden = window->isHidden();
	state.activeWindow = QApplication::activeWindow();
	state.activeWindowIdentity = state.activeWindow
		? WidgetDescription(state.activeWindow.data())
		: u"none"_q;
	const auto focused = QApplication::focusWidget();
	state.focusWidgetIdentity = focused
		? WidgetDescription(focused)
		: u"none"_q;
	state.focusWindowSet = (QGuiApplication::focusWindow() != nullptr);
	state.baselineActivation = WindowActivationDetails(
		ReadWindowActivation(window));
	state.optionFound = base::options::lookup<bool>(
		kOptionUseNewChatView).value();
	const auto active = navigation->activeChatCurrent();
	const auto peer = active.peer();
	state.baselinePeer = peer ? peer->id : PeerId();
	state.baselineChat = peer
		? (u"peer="_q + QString::number(peer->id.value))
		: u"none"_q;
	state.baselineSelection = ReadListSelections(content);
	state.captured = true;
	return QString();
}

// Starts a bounded wait and its layout-settle bookkeeping.
void StartWait(SelfTestState &state) {
	state.waitStarted = crl::now();
	state.stableSince = 0;
	state.sample = QString();
	state.waitDetails = QString();
}

// True once |sample| read the same for kLayoutSettle. Only the wait's own
// bookkeeping is written.
[[nodiscard]] bool SampleSettled(
		SelfTestState &state,
		const QString &sample) {
	const auto now = crl::now();
	if (sample != state.sample) {
		state.sample = sample;
		state.stableSince = now;
		return false;
	}
	return (now - state.stableSince) >= kLayoutSettle;
}

[[nodiscard]] bool WaitBoundReached(
		const SelfTestState &state,
		crl::time bound) {
	return (crl::now() - state.waitStarted) >= bound;
}

// Shows Saved Messages in the background, at its end: in the legacy
// HistoryWidget with |modern| false and in a HistoryView::ChatWidget with
// it true, through the existing use-modern-chat-view option. Background, so
// MainWidget::showHistory neither activates the window nor clears its
// minimized bit; the lever is re-asserted anyway.
void OpenSavedMessages(SelfTestState &state, bool modern) {
	StartWait(state);
	auto &option = base::options::lookup<bool>(kOptionUseNewChatView);
	if (option.value() != modern) {
		state.optionChanged = true;
		option.set(modern);
	}
	state.lever = KeepMainWindowNotMarkingRead(state.controller.get());
	if (const auto navigation = state.navigation.get()) {
		state.chatOpened = true;
		navigation->showPeerHistory(
			state.self,
			Window::SectionShow(
				Way::ClearStack,
				anim::type::instant,
				anim::activation::background),
			ShowAtTheEndMsgId);
	}
}

[[nodiscard]] Element *LastView(not_null<History*> history) {
	if (history->blocks.empty()) {
		return nullptr;
	}
	const auto &block = history->blocks.back();
	return block->messages.empty() ? nullptr : block->messages.back().get();
}

// The HistoryInner of the legacy HistoryWidget showing Saved Messages,
// found by RTTI under the main window's session content and accepted only
// when it owns the history's last view: HistoryInner::itemTop answers -2
// for a view of another history and -1 for one it does not show.
[[nodiscard]] HistoryInner *ResolveLegacyInner(const SelfTestState &state) {
	const auto content = ContentOf(state);
	const auto history = state.history.get();
	const auto last = history ? LastView(history) : nullptr;
	if (!content || !last) {
		return nullptr;
	}
	for (const auto widget : FindAll<HistoryWidget>(content)) {
		if (widget->history() != history) {
			continue;
		}
		for (const auto inner : FindAll<HistoryInner>(widget)) {
			if (inner->itemTop(last) >= 0) {
				return inner;
			}
		}
	}
	return nullptr;
}

// The HistoryView::ListWidget showing both fixture messages, accepted by
// the README's ownership rule (viewByPosition, data() and delegate()) and
// only with a window controller, so the chat preview's list never is.
[[nodiscard]] ModernList *ResolveModernList(const SelfTestState &state) {
	const auto content = ContentOf(state);
	const auto itemA = Message(state, state.idA);
	const auto itemB = Message(state, state.idB);
	if (!content || !itemA || !itemB) {
		return nullptr;
	}
	for (const auto list : FindAll<ModernList>(content)) {
		if (!list->controllerOrNull()) {
			continue;
		}
		const auto adapter = ListWidgetAdapter(list);
		if (adapter.ownView(itemA) && adapter.ownView(itemB)) {
			return list;
		}
	}
	return nullptr;
}

[[nodiscard]] bool LegacyReady(SelfTestState &state) {
	state.lever = KeepMainWindowNotMarkingRead(state.controller.get());
	const auto inner = ResolveLegacyInner(state);
	const auto history = state.history.get();
	const auto ready = inner
		&& history
		&& history->loadedAtBottom()
		&& !history->hasPendingResizedItems()
		&& (inner->height() > 0);
	const auto last = (history && inner) ? LastView(history) : nullptr;
	const auto sample = u"ready="_q
		+ FlagText(ready)
		+ u" inner="_q
		+ (inner ? WidgetDescription(inner) : u"none"_q)
		+ u" height="_q
		+ QString::number(inner ? inner->height() : -1)
		+ u" lastTop="_q
		+ QString::number(last ? inner->itemTop(last) : -1)
		+ u" viewport="_q
		+ (inner ? RectText(HistoryInnerAdapter(inner).viewport()) : u""_q)
		+ u" loadedAtBottom="_q
		+ FlagText(history && history->loadedAtBottom());
	state.waitDetails = sample + u" | "_q + MarkingReadDetails(state.lever);
	const auto settled = SampleSettled(state, sample);
	return (ready && settled) || WaitBoundReached(state, kOpenBound);
}

void CheckLegacyOpened(SelfTestState &state) {
	const auto inner = ResolveLegacyInner(state);
	const auto history = state.history.get();
	state.inner.list = inner;
	state.inner.identity = inner ? WidgetDescription(inner) : u"none"_q;
	auto missing = QStringList();
	if (!history) {
		missing.push_back(u"the Saved Messages history is gone"_q);
	} else {
		if (!inner) {
			missing.push_back(u"no HistoryInner of a legacy HistoryWidget "
				"owns the Saved Messages views"_q);
		}
		if (!history->folderKnown()) {
			missing.push_back(u"its folder is not known, so "
				"History::newItemAdded would request a dialog entry"_q);
		}
		if (!history->loadedAtBottom()) {
			missing.push_back(u"it is not loaded at its bottom, so "
				"History::addNewItem would keep A and B out of its blocks"_q);
		}
		if (history->maybeMessages()) {
			missing.push_back(u"a History::messages() index already exists, "
				"and SparseIdsList::addNew asserts on an id above "
				"ServerMaxMsgId"_q);
		}
		if (!history->loadedAtTop() && !IsServerMsgId(history->minMsgId())) {
			missing.push_back(u"its first loaded regular message is no "
				"server message, so History::messages() would later be "
				"seeded with a range that starts above ServerMaxMsgId"_q);
		}
		if (history->hasPendingResizedItems()) {
			missing.push_back(u"it still has pending resized items"_q);
		}
	}
	const auto selection = inner
		? HistoryInnerAdapter(inner).selection()
		: QString();
	if (!selection.isEmpty()) {
		missing.push_back(u"the list already holds "_q + selection);
	}
	if (!LeverHolds(state.lever)) {
		missing.push_back(u"the main window is not minimized and not "
			"marking read"_q);
	}
	const auto details = u"inner="_q
		+ state.inner.identity
		+ u" folderKnown="_q
		+ FlagText(history && history->folderKnown())
		+ u" loadedAtBottom="_q
		+ FlagText(history && history->loadedAtBottom())
		+ u" loadedAtTop="_q
		+ FlagText(history && history->loadedAtTop())
		+ u" minMsgId="_q
		+ QString::number(history ? history->minMsgId().bare : 0)
		+ u" messagesIndex="_q
		+ FlagText(history && history->maybeMessages())
		+ u" | "_q
		+ state.waitDetails;
	const auto what = u"fixture gate: chat-list menu self-test: Saved "
		"Messages is shown by its own HistoryInner in the legacy "
		"HistoryWidget, loaded at its bottom with a server message first, "
		"its folder known, no History::messages() index, no selection and "
		"the window minimized and not marking read"_q;
	Check(
		missing.isEmpty(),
		what,
		missing.isEmpty()
			? details
			: (missing.join(u"; "_q) + u" - "_q + details));
	if (!missing.isEmpty()) {
		state.gate = what + u" - "_q + missing.join(u"; "_q);
	}
}

[[nodiscard]] bool FixtureShapeHolds(not_null<HistoryItem*> item) {
	return item->isRegular()
		&& item->isHistoryEntry()
		&& !item->isService()
		&& !item->isLocal()
		&& item->out()
		&& item->canBeSelected()
		&& !IsServerMsgId(item->id)
		&& !IsClientMsgId(item->id)
		&& !item->sharedMediaTypes()
		&& !item->history()->owner().groups().find(item)
		&& (item->mainView() != nullptr);
}

// One FIXTURE_TEXT row per message, with every predicate the menus and the
// selection gate on, so a refused menu entry is diagnosable from the log.
[[nodiscard]] QString FixturePredicates(
		not_null<HistoryItem*> item,
		const QString &tag) {
	const auto history = item->history();
	const auto fields = QStringList{
		u"tag=%1"_q.arg(tag),
		u"id=%1"_q.arg(IdText(item->fullId())),
		u"clientMsgId=%1"_q.arg(FlagText(IsClientMsgId(item->id))),
		u"serverMsgId=%1"_q.arg(FlagText(IsServerMsgId(item->id))),
		u"isService=%1"_q.arg(FlagText(item->isService())),
		u"isRegular=%1"_q.arg(FlagText(item->isRegular())),
		u"isHistoryEntry=%1"_q.arg(FlagText(item->isHistoryEntry())),
		u"isLocal=%1"_q.arg(FlagText(item->isLocal())),
		u"canBeSelected=%1"_q.arg(FlagText(item->canBeSelected())),
		u"out=%1"_q.arg(FlagText(item->out())),
		u"sharedMedia=%1"_q.arg(FlagText(bool(item->sharedMediaTypes()))),
		u"grouped=%1"_q.arg(
			FlagText(history->owner().groups().find(item) != nullptr)),
		u"view=%1"_q.arg(FlagText(item->mainView() != nullptr)),
		u"messagesIndex=%1"_q.arg(FlagText(history->maybeMessages())),
		u"lastMessage=%1"_q.arg(
			FlagText(history->lastMessage() == item.get())),
	};
	return fields.join(u" "_q);
}

// A regular outgoing plain-text message, the shape
// GenerateForwardedItem (settings_privacy_controllers.cpp) builds with the
// current MTP_message argument list, added the way
// Test::InjectServiceMessage adds its service items: an id from
// Data::Session::nextNonHistoryEntryId(), above ServerMaxMsgId and outside
// the client range, no MessageFlag::Local, and nothing sent to the server.
[[nodiscard]] not_null<HistoryItem*> InjectFixtureText(
		not_null<History*> history,
		const QString &text) {
	using Flag = MTPDmessage::Flag;
	return history->addNewMessage(
		history->owner().nextNonHistoryEntryId(),
		MTP_message(
			MTP_flags(Flag::f_out | Flag::f_from_id),
			MTP_int(0), // id, replaced by the MsgId argument
			peerToMTP(history->peer->id), // from_id
			MTPint(), // from_boosts_applied
			MTPstring(), // from_rank
			peerToMTP(history->peer->id), // peer_id
			MTPPeer(), // saved_peer_id
			MTPMessageFwdHeader(), // fwd_from
			MTPlong(), // via_bot_id
			MTPlong(), // via_business_bot_id
			MTPPeer(), // guestchat_via_from
			MTPMessageReplyHeader(), // reply_to
			MTP_int(base::unixtime::now()), // date
			MTP_string(text), // message
			MTPMessageMedia(), // media
			MTPReplyMarkup(), // reply_markup
			MTPVector<MTPMessageEntity>(), // entities
			MTPint(), // views
			MTPint(), // forwards
			MTPMessageReplies(), // replies
			MTPint(), // edit_date
			MTPstring(), // post_author
			MTPlong(), // grouped_id
			MTPMessageReactions(), // reactions
			MTPVector<MTPRestrictionReason>(), // restriction_reason
			MTPint(), // ttl_period
			MTPint(), // quick_reply_shortcut_id
			MTPlong(), // effect
			MTPFactCheck(), // factcheck
			MTPint(), // report_delivery_until_date
			MTPlong(), // paid_message_stars
			MTPSuggestedPost(), // suggested_post
			MTPint(), // schedule_repeat_period
			MTPstring(), // summary_from_language
			MTPRichMessage()), // rich_message
		MessageFlags(),
		NewMessageType::Unread);
}

// The gates the injection depends on, read again in the turn that
// injects: the history, its folder, its bottom, no messages index, the
// legacy list and the lever.
[[nodiscard]] QString InjectionRefusal(const SelfTestState &state) {
	const auto history = state.history.get();
	if (!history) {
		return u"the Saved Messages history is gone"_q;
	} else if (!history->folderKnown()) {
		return u"its folder is not known"_q;
	} else if (!history->loadedAtBottom()) {
		return u"it is not loaded at its bottom"_q;
	} else if (history->maybeMessages()) {
		return u"a History::messages() index exists"_q;
	} else if (!state.inner.list) {
		return u"the HistoryInner is gone"_q;
	} else if (!LeverHolds(state.lever)) {
		return u"the main window is not minimized and not marking read: "_q
			+ MarkingReadDetails(state.lever);
	}
	return QString();
}

// Processes a pending layout of the shown legacy history - and of the
// history it migrated from, the two HistoryInner::hasPendingResizedItems()
// combines - through the product's own public route, for a window that
// gets no paints. HistoryWidget::paintEvent is what normally recounts such
// a layout, and History::addNewItem ends with
// Data::Session::notifyHistoryChangeDelayed, which then waits for whoever
// calls sendHistoryChangeNotifications next. Here notifyHistoryChangeDelayed
// queues each pending history, sendHistoryChangeNotifications fires
// historyChanged, and HistoryWidget::handleHistoryChange answers with
// handlePendingHistoryUpdate(): updateHistoryGeometry(), updateListSize(),
// HistoryInner::recountHistoryGeometry() and History::resizeToWidth(),
// which clears the flag. updateHistoryGeometry() keeps it set when it
// returns early - a show animation, no scroll height, a history not yet
// inited, a first-load request - and so does an element that asks for
// another resize while it is laid out, so the flag is read again
// afterwards, never assumed. Acts only while the flag is set and the
// not-marking-read lever holds: the recount scrolls the list, and
// HistoryInner::visibleAreaUpdated() then runs checkActivation(), which in
// a window marking read reaches Histories::readInboxTill with the injected
// ids. Writes one NOTE row when the flag was set. It changes the layout, so
// never call it from an until.
LayoutFlush ProcessPendingLayout(
		const SelfTestState &state,
		const QString &where) {
	const auto read = [&](bool &pending) {
		const auto history = state.history.get();
		const auto migrated = history ? history->migrateFrom() : nullptr;
		const auto flag = [](History *each) {
			return each
				? FlagText(each->hasPendingResizedItems())
				: u"none"_q;
		};
		pending = (history && history->hasPendingResizedItems())
			|| (migrated && migrated->hasPendingResizedItems());
		return u"history="_q
			+ flag(history)
			+ u" migrated="_q
			+ flag(migrated);
	};
	auto result = LayoutFlush();
	const auto before = read(result.pendingBefore);
	result.pendingAfter = result.pendingBefore;
	if (!result.pendingBefore) {
		result.details = u"nothing pending: "_q + before;
		return result;
	}
	const auto lever = KeepMainWindowNotMarkingRead(state.controller.get());
	if (!LeverHolds(lever)) {
		result.details = u"processed=0 before{"_q
			+ before
			+ u"} - the main window is not minimized and not marking read, "
				"so the layout was left pending: "_q
			+ MarkingReadDetails(lever);
		Note(u"chat-list menu self-test: left a pending layout unprocessed "_q
			+ where
			+ u": "_q
			+ result.details);
		return result;
	}
	const auto history = state.history.get();
	const auto migrated = history ? history->migrateFrom() : nullptr;
	for (const auto each : { history, migrated }) {
		if (each && each->hasPendingResizedItems()) {
			each->owner().notifyHistoryChangeDelayed(each);
		}
	}
	if (history) {
		history->owner().sendHistoryChangeNotifications();
		result.processed = true;
	}
	const auto after = read(result.pendingAfter);
	result.details = u"processed="_q
		+ FlagText(result.processed)
		+ u" before{"_q
		+ before
		+ u"} after{"_q
		+ after
		+ u"}"_q;
	Note(u"chat-list menu self-test: processed a pending layout "_q
		+ where
		+ u" through Data::Session::notifyHistoryChangeDelayed and "
			"sendHistoryChangeNotifications: "_q
		+ result.details
		+ u" | "_q
		+ MarkingReadDetails(lever));
	return result;
}

void InjectFixture(SelfTestState &state) {
	StartWait(state);
	state.lever = KeepMainWindowNotMarkingRead(state.controller.get());
	const auto refusal = InjectionRefusal(state);
	if (!refusal.isEmpty()) {
		const auto what = u"fixture gate: chat-list menu self-test: the "
			"injection preconditions still hold in the turn that injects A "
			"and B"_q;
		Check(false, what, refusal + u"; nothing was injected"_q);
		state.gate = what + u" - "_q + refusal;
		return;
	}
	const auto history = state.history.get();
	const auto a = InjectFixtureText(history, kFixtureTextA);
	state.idA = a->fullId();
	state.positionA = a->position();
	const auto rowA = FixturePredicates(a, u"chat-list menu A"_q);
	LogRaw(u"FIXTURE_TEXT: "_q + rowA);
	const auto b = InjectFixtureText(history, kFixtureTextB);
	state.idB = b->fullId();
	state.positionB = b->position();
	const auto rowB = FixturePredicates(b, u"chat-list menu B"_q);
	LogRaw(u"FIXTURE_TEXT: "_q + rowB);
	const auto stillA = Message(state, state.idA);
	state.predicatesHold = stillA
		&& FixtureShapeHolds(stillA)
		&& FixtureShapeHolds(b)
		&& (state.idA != state.idB);
	state.predicates = u"A{"_q + rowA + u"} B{"_q + rowB + u"}"_q;

	// B's History::addNewItem left its notifyHistoryChangeDelayed pending,
	// which this minimized window never paints away: process it before the
	// layout wait starts, so the wait observes the recounted layout settle.
	ProcessPendingLayout(state, u"after the injection"_q);
}

// Pure but for the documented lever re-assertion and the wait's own
// bookkeeping: both messages laid out in |list| with their points inside
// its visible message viewport, the same layout for kLayoutSettle.
[[nodiscard]] bool FixtureLaidOut(
		SelfTestState &state,
		QWidget *list,
		crl::time bound) {
	if (!state.gate.isEmpty()) {
		return true;
	}
	state.lever = KeepMainWindowNotMarkingRead(state.controller.get());
	const auto a = ReadTargetGeometry(list, Message(state, state.idA));
	const auto b = ReadTargetGeometry(list, Message(state, state.idB));
	const auto adapter = list ? MakeAdapter(list) : nullptr;
	const auto pending = adapter && adapter->pendingResize();
	const auto ready = a.inViewport && b.inViewport && !pending;
	const auto sample = u"ready="_q
		+ FlagText(ready)
		+ u" list="_q
		+ (list ? WidgetDescription(list) : u"none"_q)
		+ u" pendingResize="_q
		+ FlagText(pending)
		+ u" A={"_q
		+ TargetText(a)
		+ u"} B={"_q
		+ TargetText(b)
		+ u"}"_q;
	state.waitDetails = sample + u" | "_q + MarkingReadDetails(state.lever);
	const auto settled = SampleSettled(state, sample);
	return (ready && settled) || WaitBoundReached(state, bound);
}

// The gate after a list resolved: both fixture messages in |leg|'s list,
// at points inside its visible message viewport, no selection there, and
// the window still minimized and not marking read.
void CheckFixtureLaidOut(SelfTestState &state, ListLeg &leg) {
	const auto list = leg.list.data();
	const auto a = ReadTargetGeometry(list, Message(state, state.idA));
	const auto b = ReadTargetGeometry(list, Message(state, state.idB));
	const auto adapter = list ? MakeAdapter(list) : nullptr;
	auto missing = QStringList();
	if (!list) {
		missing.push_back(u"no "_q + leg.kind + u" was resolved"_q);
	} else {
		if (!a.inViewport || !b.inViewport) {
			missing.push_back(u"a fixture point is not inside the visible "
				"message viewport"_q);
		}
		if (adapter->pendingResize()) {
			missing.push_back(u"the list has pending resized items"_q);
		}
		const auto selection = adapter->selection();
		if (!selection.isEmpty()) {
			missing.push_back(u"the list already holds "_q + selection);
		}
	}
	if (!LeverHolds(state.lever)) {
		missing.push_back(u"the main window is not minimized and not "
			"marking read"_q);
	}
	const auto details = FixtureIdsText(state)
		+ u" list="_q
		+ leg.identity
		+ u" | "_q
		+ state.waitDetails;
	const auto what = u"fixture gate: chat-list menu self-test: "_q
		+ leg.kind
		+ u" lays out A and B with both points inside its visible message "
			"viewport, holds no selection, and the window stays minimized "
			"and not marking read"_q;
	Check(
		missing.isEmpty(),
		what,
		missing.isEmpty()
			? details
			: (missing.join(u"; "_q) + u" - "_q + details));
	if (!missing.isEmpty()) {
		state.gate = what + u" - "_q + missing.join(u"; "_q);
	}
}

void CheckInjected(SelfTestState &state) {
	if (!state.gate.isEmpty()) {
		return;
	}
	const auto what = u"fixture gate: chat-list menu self-test: A and B are "
		"regular, non-service, non-local, outgoing, selectable, ungrouped "
		"plain-text history entries whose ids are neither server nor client "
		"ids"_q;
	Check(state.predicatesHold, what, state.predicates);
	if (!state.predicatesHold) {
		state.gate = what + u" - "_q + state.predicates;
		return;
	}
	state.inner.identity = state.inner.list
		? WidgetDescription(state.inner.list.data())
		: u"none"_q;

	// The wait can end at its bound with the layout pending again: process
	// it, read the points again, and judge that reading. A flag that
	// survives the processing still fails the gate by name.
	const auto flush = ProcessPendingLayout(
		state,
		u"at the HistoryInner layout gate"_q);
	if (flush.pendingBefore) {
		const auto list = state.inner.list.data();
		const auto a = ReadTargetGeometry(list, Message(state, state.idA));
		const auto b = ReadTargetGeometry(list, Message(state, state.idB));
		state.waitDetails = u"the wait ended with the layout pending: "_q
			+ flush.details
			+ u" | after that A={"_q
			+ TargetText(a)
			+ u"} B={"_q
			+ TargetText(b)
			+ u"} | at the wait's end: "_q
			+ state.waitDetails;
	}
	CheckFixtureLaidOut(state, state.inner);
}

// One list's premise, protected and trigger arms, in this one synchronous
// turn. Every activation change here is put back to the baseline before
// the turn ends, on every path out of the function.
void RunPremiseLeg(SelfTestState &state, ListLeg &leg) {
	auto &r = leg.premise;
	// HistoryInner resolves no point while its layout is pending; the list,
	// the messages and the points are all read after this.
	if (dynamic_cast<HistoryInner*>(leg.list.data())) {
		ProcessPendingLayout(state, u"before the HistoryInner premise leg"_q);
	}
	const auto list = leg.list.data();
	const auto itemA = Message(state, state.idA);
	const auto itemB = Message(state, state.idB);
	if (!list || !itemA || !itemB) {
		r.refusal = u"the list, A or B is gone: list="_q
			+ FlagText(list != nullptr)
			+ u" "_q
			+ FixtureIdsText(state);
		return;
	}
	r.marking = KeepMainWindowNotMarkingRead(state.controller.get());
	if (!LeverHolds(r.marking)) {
		r.refusal = u"the main window is not minimized and not marking "
			"read, so no activation was changed: "_q
			+ MarkingReadDetails(r.marking);
		return;
	}
	const auto a = ReadTargetGeometry(list, itemA);
	const auto b = ReadTargetGeometry(list, itemB);
	r.points = u"A={"_q + TargetText(a) + u"} B={"_q + TargetText(b) + u"}"_q;
	if (!a.inViewport || !b.inViewport) {
		r.refusal = u"a fixture point left the visible message viewport: "_q
			+ r.points;
		return;
	}
	const auto adapter = MakeAdapter(list);
	r.ran = true;
	const auto restore = gsl::finally([&] {
		r.restored = RestoreQtActivation(state, true);
	});

	// A becomes the last message the list resolved, through the attributed
	// reading on A; its menu is closed in this turn.
	r.established = OpenChatListContextMenu(list, itemA, a.point);
	HideSelfTestMenu(state, r.established.menu.menu.data());
	if (!adapter->alive()) {
		return;
	}

	// The mouse is outside the list - that reading's own leave, and another
	// one if something re-entered it - and the window is made inactive.
	r.afterEstablished = adapter->resolve(state.idB);
	if (r.afterEstablished.mouseActive) {
		DeliverLeave(adapter->widget());
		++r.extraLeaves;
	}
	r.cleared = ClearWindowActive();
	if (!adapter->alive()) {
		return;
	}
	r.premise = adapter->resolve(state.idB);
	r.inactiveAtPremise = !adapter->widget()->window()->isActiveWindow();

	// The unprotected delivery at B's point, with the list's actual
	// resolution read at build.
	r.unprotected = OpenContextMenu(
		adapter->widget(),
		b.point,
		QContextMenuEvent::Mouse,
		[&] {
			r.unprotectedObserved = true;
			r.unprotectedAtBuild = adapter->resolve(state.idB);
		});
	HideSelfTestMenu(state, r.unprotected.menu.data());
	if (!adapter->alive()) {
		return;
	}

	// The same conditions again: the popup may itself have changed the
	// activation or the hover.
	if (adapter->resolve(state.idB).mouseActive) {
		DeliverLeave(adapter->widget());
		++r.extraLeaves;
	}
	r.clearedAgain = ClearWindowActive();
	if (!adapter->alive()) {
		return;
	}
	r.beforeProtected = adapter->resolve(state.idB);
	r.inactiveAtProtected = !adapter->widget()->window()->isActiveWindow();

	// The protected reading of B and, from it, "Select".
	r.protectedReading = OpenChatListContextMenu(
		adapter->widget(),
		Message(state, state.idB),
		b.point);
	if (r.protectedReading.built() && adapter->alive()) {
		r.selectedBefore = SelectedIds(adapter->widget());
		r.select = TriggerContextMenuAction(
			r.protectedReading.menu,
			tr::lng_context_select_msg(tr::now));
		if (adapter->alive()) {
			r.selectedAfter = SelectedIds(adapter->widget());
		}
	}
	HideSelfTestMenu(state, r.protectedReading.menu.menu.data());
}

void CheckPremiseLeg(const SelfTestState &state, const ListLeg &leg) {
	const auto &r = leg.premise;
	const auto prefix = u"chat-list menu self-test: "_q + leg.kind + u": "_q;
	if (!r.ran) {
		Check(
			false,
			prefix + u"the premise, protected and trigger arms delivered "
				"their events"_q,
			r.refusal);
		return;
	}
	const auto ids = FixtureIdsText(state) + u" "_q + r.points;
	Check(
		r.established.built() && (r.established.atBuild.view == state.idA),
		prefix + u"premise - A is the last message the list resolved: the "
			"attributed reading on A was built for A"_q,
		ids + u" "_q + ChatListMenuDetails(r.established));
	Check(
		r.premise.read
			&& !r.premise.mouseActive
			&& !r.premise.activeWindow
			&& !r.premise.gateOpen
			&& r.inactiveAtPremise
			&& (r.premise.view == state.idA),
		prefix + u"premise - right before the unprotected delivery at B the "
			"mouse has left the list (_mouseActive false), its window reads "
			"isActiveWindow() false and its last resolution is A"_q,
		ids
			+ u" premise={"_q
			+ ResolutionText(r.premise)
			+ u"} inactiveWindow="_q
			+ FlagText(r.inactiveAtPremise)
			+ u" extraLeaves="_q
			+ QString::number(r.extraLeaves)
			+ u" afterEstablished={"_q
			+ ResolutionText(r.afterEstablished)
			+ u"} cleared=["_q
			+ WindowActivationDetails(r.cleared)
			+ u"]"_q);
	const auto &build = r.unprotectedAtBuild;
	Check(
		r.unprotected.built()
			&& r.unprotectedObserved
			&& build.read
			&& !build.gateOpen
			&& !build.viewIsTarget
			&& (build.view != state.idB)
			&& (!build.view || (build.view == state.idA)),
		prefix + u"premise - an unprotected Test::OpenContextMenu at B's "
			"point builds a menu the list did not resolve to B (it resolved "
			"A or none)"_q,
		ids
			+ u" resolvedAtBuild="_q
			+ IdText(build.view)
			+ u" observed="_q
			+ FlagText(r.unprotectedObserved)
			+ u" atBuild={"_q
			+ ResolutionText(build)
			+ u"} menu={"_q
			+ ContextMenuDetails(r.unprotected)
			+ u"}"_q);
	const auto &reading = r.protectedReading;
	Check(
		r.inactiveAtProtected
			&& r.beforeProtected.read
			&& !r.beforeProtected.mouseActive
			&& !r.beforeProtected.activeWindow
			&& !reading.before.mouseActive
			&& !reading.before.activeWindow,
		prefix + u"protected arm - the same conditions hold right before "
			"the protected delivery: the mouse is outside the list and its "
			"window is inactive"_q,
		ids
			+ u" beforeProtected={"_q
			+ ResolutionText(r.beforeProtected)
			+ u"} inactiveWindow="_q
			+ FlagText(r.inactiveAtProtected)
			+ u" clearedAgain=["_q
			+ WindowActivationDetails(r.clearedAgain)
			+ u"]"_q);
	const auto attributed = reading.built()
		&& (reading.atBuild.view == state.idB)
		&& reading.atBuild.viewIsTarget;
	const auto namedRefusal = !reading.built()
		&& (reading.refusal != ChatListMenuRefusal::None)
		&& !reading.menu.menu;
	const auto readingDetails = ids + u" "_q + ChatListMenuDetails(reading);
	Check(
		attributed || namedRefusal,
		prefix + u"protected arm - the reading for B is B's menu with B "
			"resolved at build, or a named refusal holding no usable menu; "
			"never A's menu"_q,
		readingDetails);
	Check(
		attributed,
		prefix + u"protected arm - under the premise conditions the reading "
			"for B is built and attributed to B"_q,
		readingDetails);
	Check(
		attributed
			&& r.select.triggered
			&& r.selectedBefore.empty()
			&& (r.selectedAfter == MessageIdsList{ state.idB }),
		prefix + u"trigger arm - \"Select\" from the protected reading of B "
			"leaves exactly B selected ("_q + leg.selectionReader + u")"_q,
		FixtureIdsText(state)
			+ u" selectedBefore="_q
			+ IdsText(r.selectedBefore)
			+ u" selectedAfter="_q
			+ IdsText(r.selectedAfter)
			+ u" trigger={"_q
			+ MenuTriggerDetails(r.select)
			+ u"}"_q);
	Check(
		r.restored.matches,
		prefix + u"the activation the premise cleared is put back to the "
			"captured baseline in the same turn"_q,
		ActivationRestoreText(state, r.restored));
}

void StartClear(SelfTestState &state, ListLeg &leg) {
	auto &r = leg.clear;
	StartWait(state);
	r.started = state.waitStarted;
	const auto list = leg.list.data();
	if (!list) {
		return;
	}
	r.ran = true;
	r.before = SelectedIds(list);
	ClearListSelection(list);
	r.mode = SelectionMode(list);
}

// Pure but for the wait's own bookkeeping and the selection-mode reading
// above: the selection is empty, its animation finished, and both fixture
// points are back inside the visible message viewport with a layout that
// held for kLayoutSettle - leaving selection mode gives the compose area
// back to the section, which moves the viewport.
[[nodiscard]] bool ClearSettled(SelfTestState &state, ListLeg &leg) {
	auto &r = leg.clear;
	const auto list = leg.list.data();
	if (!list || !r.ran) {
		return true;
	}
	r.after = SelectedIds(list);
	r.mode = SelectionMode(list);
	const auto a = ReadTargetGeometry(list, Message(state, state.idA));
	const auto b = ReadTargetGeometry(list, Message(state, state.idB));
	const auto cleared = r.after.empty()
		&& !r.mode.inSelectionMode
		&& (r.mode.progress <= 0.);
	r.layout = u"A={"_q + TargetText(a) + u"} B={"_q + TargetText(b) + u"}"_q;
	const auto ready = cleared && a.inViewport && b.inViewport;
	const auto sample = u"ready="_q + FlagText(ready) + u" "_q + r.layout;
	r.settled = SampleSettled(state, sample) && ready;
	return r.settled || WaitBoundReached(state, kClearBound);
}

void CheckClear(const ListLeg &leg) {
	const auto &r = leg.clear;
	Check(
		r.ran && leg.list && r.settled,
		u"chat-list menu self-test: "_q
			+ leg.kind
			+ u": the test's own selection is cleared, its selection-mode "
				"animation has finished and both fixture points are back "
				"inside the visible message viewport before the controls"_q,
		u"list="_q
			+ FlagText(leg.list != nullptr)
			+ u" before="_q
			+ IdsText(r.before)
			+ u" after="_q
			+ IdsText(r.after)
			+ u" "_q
			+ SelectionModeText(r.mode)
			+ u" waitedMs="_q
			+ QString::number(crl::now() - r.started)
			+ u" "_q
			+ r.layout);
}

// One list's mismatch negative, refusal controls and active control, in
// this one synchronous turn, the activation put back on every path out.
void RunControlLeg(SelfTestState &state, ListLeg &leg) {
	auto &r = leg.control;
	// As in RunPremiseLeg: the pending layout first, the points after it.
	if (dynamic_cast<HistoryInner*>(leg.list.data())) {
		ProcessPendingLayout(state, u"before the HistoryInner control leg"_q);
	}
	const auto list = leg.list.data();
	const auto itemA = Message(state, state.idA);
	const auto itemB = Message(state, state.idB);
	if (!list || !itemA || !itemB) {
		r.refusal = u"the list, A or B is gone: list="_q
			+ FlagText(list != nullptr)
			+ u" "_q
			+ FixtureIdsText(state);
		return;
	}
	r.marking = KeepMainWindowNotMarkingRead(state.controller.get());
	if (!LeverHolds(r.marking)) {
		r.refusal = u"the main window is not minimized and not marking "
			"read, so nothing was delivered: "_q
			+ MarkingReadDetails(r.marking);
		return;
	}
	const auto a = ReadTargetGeometry(list, itemA);
	const auto b = ReadTargetGeometry(list, itemB);
	r.points = u"A={"_q + TargetText(a) + u"} B={"_q + TargetText(b) + u"}"_q;
	if (!a.inViewport || !b.inViewport) {
		r.refusal = u"a fixture point left the visible message viewport: "_q
			+ r.points;
		return;
	}
	const auto adapter = MakeAdapter(list);
	r.ran = true;
	const auto restore = gsl::finally([&] {
		r.restored = RestoreQtActivation(state, true);
	});

	// The mismatch: B requested at A's valid point.
	r.selectedBefore = SelectedIds(list);
	r.mismatch = OpenChatListContextMenu(list, itemB, a.point);
	HideSelfTestMenu(state, r.mismatch.menu.menu.data());
	r.mismatchSelect = TriggerContextMenuAction(
		r.mismatch.menu,
		tr::lng_context_select_msg(tr::now));
	if (!adapter->alive()) {
		return;
	}
	r.selectedAfterMismatch = SelectedIds(adapter->widget());

	// Refused before anything is delivered.
	r.noList = OpenChatListContextMenu(
		nullptr,
		Message(state, state.idB),
		b.point);
	const auto parent = adapter->widget()->parentWidget();
	r.wrongWidgetIdentity = parent ? WidgetDescription(parent) : u"none"_q;
	r.wrongWidget = OpenChatListContextMenu(
		parent,
		Message(state, state.idB),
		b.point);
	r.outsidePoint = QPoint(
		b.point.x(),
		b.viewport.y() + b.viewport.height());
	r.outside = OpenChatListContextMenu(
		adapter->widget(),
		Message(state, state.idB),
		r.outsidePoint);

	// The active control: the window active in Qt's sense, never while it
	// could mark the injected messages read.
	r.markingAtActive = KeepMainWindowNotMarkingRead(
		state.controller.get());
	if (!LeverHolds(r.markingAtActive)) {
		r.activeRefusal = u"the main window is not minimized and not "
			"marking read, so it was not activated: "_q
			+ MarkingReadDetails(r.markingAtActive);
		return;
	}
	r.forced = ForceWindowActive(adapter->widget());
	r.activeAtControl = adapter->widget()->window()->isActiveWindow();
	r.markingAtActive = ReadMainWindowMarking(state.controller.get());
	r.active = OpenChatListContextMenu(
		adapter->widget(),
		Message(state, state.idB),
		b.point);
	HideSelfTestMenu(state, r.active.menu.menu.data());
}

[[nodiscard]] bool RefusedUndelivered(
		const ChatListMenuReading &reading,
		ChatListMenuRefusal refusal) {
	return (reading.refusal == refusal)
		&& !reading.built()
		&& !reading.enterSent
		&& !reading.menuRead
		&& !reading.menu.menu;
}

void CheckControlLeg(const SelfTestState &state, const ListLeg &leg) {
	const auto &r = leg.control;
	const auto prefix = u"chat-list menu self-test: "_q + leg.kind + u": "_q;
	if (!r.ran) {
		Check(
			false,
			prefix + u"the mismatch, refusal and active controls delivered "
				"their events"_q,
			r.refusal);
		return;
	}
	const auto ids = FixtureIdsText(state) + u" "_q + r.points;
	const auto &mismatch = r.mismatch;
	Check(
		(mismatch.refusal == ChatListMenuRefusal::WrongTarget)
			&& (mismatch.atBuild.view == state.idA)
			&& !mismatch.menu.menu
			&& mismatch.menuHidden,
		prefix + u"mismatch - a reading for B delivered at A's point is "
			"refused as wrong-target with A resolved at build, and the menu "
			"it built is hidden and not triggerable"_q,
		ids + u" "_q + ChatListMenuDetails(mismatch));
	Check(
		(r.mismatchSelect.refusal == MenuTriggerRefusal::MenuGone)
			&& !r.mismatchSelect.triggered
			&& (r.selectedAfterMismatch == r.selectedBefore),
		prefix + u"mismatch - \"Select\" through the refused reading refuses "
			"menu-gone and leaves the selection unchanged"_q,
		u"selectedBefore="_q
			+ IdsText(r.selectedBefore)
			+ u" selectedAfter="_q
			+ IdsText(r.selectedAfterMismatch)
			+ u" trigger={"_q
			+ MenuTriggerDetails(r.mismatchSelect)
			+ u"}"_q);
	Check(
		RefusedUndelivered(r.noList, ChatListMenuRefusal::NoList),
		prefix + u"refusal control - a null list is refused as no-list and "
			"nothing is delivered"_q,
		ChatListMenuDetails(r.noList));
	Check(
		RefusedUndelivered(r.wrongWidget, ChatListMenuRefusal::NotAChatList),
		prefix + u"refusal control - a widget that is no chat list (the "
			"list's parent) is refused as not-a-chat-list and nothing is "
			"delivered"_q,
		u"widget="_q
			+ r.wrongWidgetIdentity
			+ u" "_q
			+ ChatListMenuDetails(r.wrongWidget));
	Check(
		RefusedUndelivered(r.outside, ChatListMenuRefusal::OutsideViewport),
		prefix + u"refusal control - a point below the list's visible "
			"message viewport is refused as outside-viewport and nothing is "
			"delivered"_q,
		u"point="_q
			+ QString::number(r.outsidePoint.x())
			+ u","_q
			+ QString::number(r.outsidePoint.y())
			+ u" "_q
			+ ChatListMenuDetails(r.outside));
	const auto &active = r.active;
	Check(
		r.activeRefusal.isEmpty()
			&& r.activeAtControl
			&& !r.markingAtActive.markingAsRead
			&& r.markingAtActive.isMinimized
			&& active.built()
			&& active.before.activeWindow
			&& (active.atBuild.view == state.idB)
			&& active.atBuild.viewIsTarget,
		prefix + u"active control - with the window active "
			"(Test::ForceWindowActive) and still minimized and not marking "
			"read, the reading for B resolves B"_q,
		r.activeRefusal.isEmpty()
			? (ids
				+ u" activeWindow="_q
				+ FlagText(r.activeAtControl)
				+ u" forced=["_q
				+ WindowActivationDetails(r.forced)
				+ u"] "_q
				+ MarkingReadDetails(r.markingAtActive)
				+ u" "_q
				+ ChatListMenuDetails(active))
			: r.activeRefusal);
	Check(
		r.restored.matches,
		prefix + u"the activation the active control forced is put back to "
			"the captured baseline in the same turn"_q,
		ActivationRestoreText(state, r.restored));
}

[[nodiscard]] bool ModernReady(SelfTestState &state) {
	return !state.gate.isEmpty()
		|| FixtureLaidOut(state, ResolveModernList(state), kOpenBound);
}

void CheckModernOpened(SelfTestState &state) {
	if (!state.gate.isEmpty()) {
		return;
	}
	const auto list = ResolveModernList(state);
	state.modern.list = list;
	state.modern.identity = list ? WidgetDescription(list) : u"none"_q;
	const auto history = state.history.get();
	Note(u"chat-list menu self-test: the ChatWidget is shown - legacy "
		"HistoryInner alive="_q
		+ FlagText(state.inner.list != nullptr)
		+ u" messagesIndex="_q
		+ FlagText(history && history->maybeMessages())
		+ u" list="_q
		+ state.modern.identity);
	CheckFixtureLaidOut(state, state.modern);
}

// What the History::messages() index lists from ServerMaxMsgId - 1 on, in
// one FIXTURE_INDEX row. SparseIdsList::removeOne finds an id only in a
// slice whose range.till is at or above it, and every slice ends at or
// below ServerMaxMsgId, so History::destroyMessage cannot take an id above
// it out of the bottom slice the index was seeded with; every reader of
// the index looks each id up through Data::Session::message() and skips
// one that resolves to nothing (ListWidget::refreshRows,
// AppendHistoryClientSideMessages, countStillUnreadLocalFromMessages).
[[nodiscard]] QString ReadIndex(const SelfTestState &state) {
	const auto history = state.history.get();
	const auto messages = history ? history->maybeMessages() : nullptr;
	auto result = QString();
	if (!history) {
		result = u"history=gone"_q;
	} else if (!messages) {
		result = u"messagesIndex=0"_q;
	} else {
		const auto snapshot = messages->snapshot(Storage::SparseIdsListQuery(
			ServerMaxMsgId - 1,
			0,
			kIndexProbeLimit));
		auto listed = QStringList();
		for (const auto id : snapshot.messageIds) {
			listed.push_back(QString::number(id.bare));
		}
		const auto lists = [&](FullMsgId id) {
			return id && snapshot.messageIds.contains(id.msg);
		};
		result = u"messagesIndex=1 listsA="_q
			+ FlagText(lists(state.idA))
			+ u" listsB="_q
			+ FlagText(lists(state.idB))
			+ u" fromServerMaxMinusOne=["_q
			+ listed.join(u","_q)
			+ u"] aResolves="_q
			+ FlagText(Message(state, state.idA) != nullptr)
			+ u" bResolves="_q
			+ FlagText(Message(state, state.idB) != nullptr);
	}
	return result;
}

[[nodiscard]] bool WindowMatches(const SelfTestState &state) {
	const auto window = WindowOf(state);
	return window
		&& (WithoutActive(window->windowState()) == state.windowState)
		&& (window->isHidden() == state.windowHidden);
}

[[nodiscard]] QString WindowText(const SelfTestState &state) {
	const auto window = WindowOf(state);
	if (!window) {
		return u"window=gone"_q;
	}
	return u"captured{state="_q
		+ WindowStateText(state.windowState)
		+ u" hidden="_q
		+ FlagText(state.windowHidden)
		+ u"} now{state="_q
		+ WindowStateText(window->windowState())
		+ u" hidden="_q
		+ FlagText(window->isHidden())
		+ u"}"_q;
}

// Puts the found chat back: the found peer's history, or no chat at all
// when none was open. Background, so nothing is activated.
void RestoreChat(SelfTestState &state) {
	const auto navigation = state.navigation.get();
	if (!navigation) {
		return;
	}
	const auto params = Window::SectionShow(
		Way::ClearStack,
		anim::type::instant,
		anim::activation::background);
	if (state.baselinePeer) {
		navigation->showPeerHistory(
			state.baselinePeer,
			params,
			ShowAtUnreadMsgId);
	} else {
		navigation->clearSectionStack(params);
	}
}

// The one release, run by the teardown stage and again by Runner::onFinish.
// Idempotent: a step already done is only read again, never repeated. In
// order: the self-test's menus, the selections and hover on the lists it
// opened, the injected messages - destroyed first and proven gone - the
// option, the chat, and only then the window state and Qt activation,
// never while an injected message still exists.
[[nodiscard]] ReleaseReading Release(
		SelfTestState &state,
		const QString &path) {
	auto result = ReleaseReading();
	result.path = path;
	if (!state.captured) {
		result.done.push_back(u"nothing was captured or changed"_q);
		result.itemsGone = true;
		result.viewsGone = true;
		result.listsClean = true;
		result.optionRestored = true;
		result.chatRestored = true;
		result.windowRestored = true;
		return result;
	}

	for (const auto &menu : state.menus) {
		if (const auto popup = dynamic_cast<Ui::PopupMenu*>(menu.data())) {
			if (!popup->isHidden()) {
				Settle([&] {
					popup->hideMenu(true);
				});
				++result.menusHidden;
			}
		}
	}
	for (const auto &menu : state.menus) {
		if (menu && !menu->isHidden()) {
			++result.menusOpen;
		}
	}

	auto lists = QStringList();
	result.listsClean = true;
	for (const auto leg : { &state.inner, &state.modern }) {
		if (!leg->list) {
			lists.push_back(leg->kind + u"=gone"_q);
			continue;
		}
		if (!SelectedIds(leg->list.data()).empty()
			|| !MakeAdapter(leg->list.data())->selection().isEmpty()) {
			ClearListSelection(leg->list.data());
			result.done.push_back(leg->kind + u" selection cleared"_q);
		}
		DeliverLeave(leg->list.data());
		if (!leg->list) {
			lists.push_back(leg->kind + u"=gone after the leave"_q);
			continue;
		}
		const auto adapter = MakeAdapter(leg->list.data());
		const auto selection = adapter->selection();
		const auto mouseActive = adapter->resolve(FullMsgId()).mouseActive;
		result.listsClean = result.listsClean
			&& selection.isEmpty()
			&& !mouseActive;
		lists.push_back(leg->kind
			+ u"{selection="_q
			+ (selection.isEmpty() ? u"none"_q : selection)
			+ u" mouseActive="_q
			+ FlagText(mouseActive)
			+ u"}"_q);
	}
	result.lists = lists.join(u" "_q)
		+ u" menusHidden="_q
		+ QString::number(result.menusHidden)
		+ u" menusOpen="_q
		+ QString::number(result.menusOpen);

	if (!state.itemsReleased && (state.idA || state.idB)) {
		state.itemsReleased = true;
		const auto fixture = {
			std::pair{ state.idA, u"chat-list menu A"_q },
			std::pair{ state.idB, u"chat-list menu B"_q },
		};
		for (const auto &[id, tag] : fixture) {
			if (!id) {
				continue;
			}
			const auto item = Message(state, id);
			LogRaw(u"FIXTURE_REMOVE: tag="_q
				+ tag
				+ u" id="_q
				+ IdText(id)
				+ u" sessionAlive="_q
				+ FlagText(state.session.get() != nullptr)
				+ u" present="_q
				+ FlagText(item != nullptr)
				+ u" path="_q
				+ path);
			if (item) {
				item->destroy();
			}
		}
		result.done.push_back(u"injected messages destroyed"_q);
	}
	const auto leftA = (Message(state, state.idA) != nullptr);
	const auto leftB = (Message(state, state.idB) != nullptr);
	result.itemsGone = !leftA && !leftB;
	result.items = FixtureIdsText(state)
		+ u" resolvesA="_q
		+ FlagText(leftA)
		+ u" resolvesB="_q
		+ FlagText(leftB);
	auto viewA = false;
	auto viewB = false;
	const auto modern = dynamic_cast<ModernList*>(state.modern.list.data());
	if (modern) {
		viewA = state.idA && modern->viewByPosition(state.positionA);
		viewB = state.idB && modern->viewByPosition(state.positionB);
	}
	result.viewsGone = !viewA && !viewB;
	result.views = u"ListWidget alive="_q
		+ FlagText(modern != nullptr)
		+ u" viewA="_q
		+ FlagText(viewA)
		+ u" viewB="_q
		+ FlagText(viewB);
	result.index = ReadIndex(state);
	LogRaw(u"FIXTURE_INDEX: tag=chat-list menu path="_q
		+ path
		+ u" "_q
		+ result.index);

	auto &option = base::options::lookup<bool>(kOptionUseNewChatView);
	if (state.optionChanged && !state.optionRestored) {
		state.optionRestored = true;
		option.set(state.optionFound);
		result.done.push_back(u"option restored"_q);
	}
	result.optionRestored = (option.value() == state.optionFound);
	result.option = u"use-modern-chat-view found="_q
		+ FlagText(state.optionFound)
		+ u" now="_q
		+ FlagText(option.value())
		+ u" changed="_q
		+ FlagText(state.optionChanged);

	if (state.chatOpened && !state.chatClosed) {
		state.chatClosed = true;
		RestoreChat(state);
		result.done.push_back(u"chat restored"_q);
	}
	const auto navigation = state.navigation.get();
	const auto activePeer = navigation
		? navigation->activeChatCurrent().peer()
		: nullptr;
	const auto activeId = activePeer ? activePeer->id : PeerId();
	result.chatRestored = navigation && (activeId == state.baselinePeer);
	result.chat = u"found={"_q
		+ state.baselineChat
		+ u"} now=peer="_q
		+ QString::number(activeId.value)
		+ u" opened="_q
		+ FlagText(state.chatOpened)
		+ u" navigation="_q
		+ FlagText(navigation != nullptr);

	const auto window = WindowOf(state);
	if (!state.leverApplied) {
		result.windowRestored = WindowMatches(state);
		result.window = u"not changed: "_q + WindowText(state);
	} else if (state.windowRestored) {
		result.windowRestored = WindowMatches(state);
		result.window = u"restored earlier: "_q + WindowText(state);
	} else if (!result.itemsGone) {
		result.window = u"left minimized and not activated: an injected "
			"message still exists, and showing or activating the window "
			"could mark it read - "_q + WindowText(state);
	} else if (!window) {
		result.window = u"the main window is gone"_q;
	} else {
		state.windowRestored = true;
		state.restoredAt = crl::now();
		window->setWindowState(state.windowState);
		result.activation = RestoreQtActivation(state, false);
		result.windowRestored = WindowMatches(state);
		result.window = u"restored: "_q
			+ WindowText(state)
			+ u" activation={"_q
			+ ActivationRestoreText(state, result.activation)
			+ u"}"_q;
		result.done.push_back(u"window state and Qt activation restored"_q);
	}
	return result;
}

[[nodiscard]] QString ReleaseText(const ReleaseReading &reading) {
	const auto fields = QStringList{
		u"path="_q + reading.path,
		u"done=["_q + reading.done.join(u", "_q) + u"]"_q,
		u"items={"_q + reading.items + u"}"_q,
		u"views={"_q + reading.views + u"}"_q,
		u"index={"_q + reading.index + u"}"_q,
		u"lists={"_q + reading.lists + u"}"_q,
		u"option={"_q + reading.option + u"}"_q,
		u"chat={"_q + reading.chat + u"}"_q,
		u"window={"_q + reading.window + u"}"_q,
	};
	return fields.join(u" "_q);
}

void CheckRelease(const SelfTestState &state) {
	const auto &r = state.release;
	const auto prefix = u"chat-list menu self-test: teardown - "_q;
	Check(
		r.itemsGone && r.viewsGone,
		prefix + u"both injected messages are destroyed first: neither id "
			"resolves to a message or to a view of the "
			"HistoryView::ListWidget"_q,
		r.items + u" "_q + r.views);
	Check(
		r.listsClean && !r.menusOpen,
		prefix + u"no selection, mouse-active hover or self-test menu "
			"remains on the lists the self-test opened"_q,
		r.lists);
	Check(
		r.optionRestored,
		prefix + u"the use-modern-chat-view option is back to the value it was "
			"found with"_q,
		r.option);
	Check(
		r.chatRestored,
		prefix + u"the chat the self-test opened is closed and the chat "
			"found at the baseline (or none) is shown again"_q,
		r.chat);
	Note(prefix + u"History::messages() index after the release: "_q
		+ r.index
		+ u" - an id listed above ServerMaxMsgId is one "
			"SparseIdsList::removeOne could not reach, with no message "
			"behind it"_q);
	Note(prefix + ReleaseText(r));
}

void CheckSettled(SelfTestState &state) {
	const auto prefix = u"chat-list menu self-test: settle - "_q;
	const auto waited = crl::now()
		- (state.restoredAt ? state.restoredAt : state.waitStarted);
	Check(
		state.windowRestored && WindowMatches(state),
		prefix + u"the main window's state is the captured one again and "
			"held it through the repaint settle"_q,
		WindowText(state)
			+ u" restored="_q
			+ FlagText(state.windowRestored)
			+ u" waitedMs="_q
			+ QString::number(waited));
	if (!state.windowRestored) {
		return;
	}
	// The OS may have reacted to the restored window state between turns;
	// the found Qt activation is put back and read in this turn.
	const auto activation = RestoreQtActivation(state, false);
	Check(
		activation.matches,
		prefix + u"Qt activation is the captured one again: the same focus "
			"window presence and the same active window"_q,
		ActivationRestoreText(state, activation));
	Note(prefix + u"after the restore: "_q
		+ MarkingReadDetails(ReadMainWindowMarking(state.controller.get())));
}

} // namespace

void AppendChatListMenuSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<SelfTestState>();
	state->inner.kind = u"HistoryInner"_q;
	state->inner.selectionReader = u"HistoryInner::getSelectedItems()"_q;
	state->modern.kind = u"HistoryView::ListWidget"_q;
	state->modern.selectionReader = u"ListWidget::getSelectedIds()"_q;

	// Registered before anything is changed: every path that reaches
	// Runner::finish() - a stage timeout, the watchdog, skip-to-end or the
	// normal end - runs the same idempotent release, which after a normal
	// teardown only reads again.
	runner->onFinish([=] {
		const auto reading = Release(*state, u"onFinish"_q);
		Note(u"chat-list menu self-test: onFinish release: "_q
			+ ReleaseText(reading));
	});
	const auto gate = [=] {
		return state->gate;
	};

	runner->add({
		.name = u"chat-list menu self-test: capture the baseline and keep "
			"the window from marking read"_q,
		.run = [=] {
			const auto missing = CaptureBaseline(*state);
			const auto what = u"fixture gate: chat-list menu self-test: a "
				"session main window, its session controller and session "
				"content"_q;
			Check(
				missing.isEmpty(),
				what,
				missing.isEmpty() ? BaselineText(*state) : missing);
			if (!missing.isEmpty()) {
				state->gate = what + u" - "_q + missing;
				return;
			}
			StartWait(*state);
			state->leverApplied = true;
			state->lever = KeepMainWindowNotMarkingRead(
				state->controller.get());
		},
		.until = [=] {
			if (!state->gate.isEmpty()) {
				return true;
			}
			// The lever's own bounded wait, re-asserted on every poll as
			// AppendMainWindowNotMarkingReadSelfTest does.
			state->lever = KeepMainWindowNotMarkingRead(
				state->controller.get(),
				crl::now() - state->waitStarted);
			return !state->lever.refusal.isEmpty()
				|| LeverHolds(state->lever);
		},
		.then = [=] {
			if (!state->gate.isEmpty()) {
				return;
			}
			const auto held = LeverHolds(state->lever);
			const auto what = u"fixture gate: chat-list menu self-test: the "
				"main window is minimized and not marking messages read "
				"before anything is injected or activated"_q;
			Check(held, what, MarkingReadDetails(state->lever));
			if (!held) {
				state->gate = what + u" - "_q
					+ MarkingReadDetails(state->lever);
			}
		},
		.timeout = kNotMarkingReadBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return MarkingReadDetails(state->lever);
		},
	});

	runner->add({
		.name = u"chat-list menu self-test: open Saved Messages in the "
			"legacy HistoryWidget"_q,
		.skipReason = gate,
		.run = [=] {
			OpenSavedMessages(*state, false);
		},
		.until = [=] {
			return LegacyReady(*state);
		},
		.then = [=] {
			CheckLegacyOpened(*state);
		},
		.timeout = kOpenBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->waitDetails;
		},
	});

	runner->add({
		.name = u"chat-list menu self-test: inject messages A and B"_q,
		.skipReason = gate,
		.run = [=] {
			InjectFixture(*state);
		},
		.until = [=] {
			return FixtureLaidOut(
				*state,
				state->inner.list.data(),
				kLayoutBound);
		},
		.then = [=] {
			CheckInjected(*state);
		},
		.timeout = kLayoutBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->waitDetails;
		},
	});

	const auto appendLeg = [=](ListLeg *leg, const QString &name) {
		runner->add({
			.name = u"chat-list menu self-test: "_q
				+ name
				+ u" premise, protected reading and Select"_q,
			.skipReason = gate,
			.run = [=] {
				RunPremiseLeg(*state, *leg);
			},
			.then = [=] {
				CheckPremiseLeg(*state, *leg);
			},
		});
		runner->add({
			.name = u"chat-list menu self-test: "_q
				+ name
				+ u" clears the test's selection"_q,
			.skipReason = gate,
			.run = [=] {
				StartClear(*state, *leg);
			},
			.until = [=] {
				return ClearSettled(*state, *leg);
			},
			.then = [=] {
				CheckClear(*leg);
			},
			.timeout = kClearBound + kDefaultStageTimeout,
			.timeoutDetails = [=] {
				return IdsText(leg->clear.after)
					+ u" "_q
					+ SelectionModeText(leg->clear.mode)
					+ u" "_q
					+ leg->clear.layout;
			},
		});
		runner->add({
			.name = u"chat-list menu self-test: "_q
				+ name
				+ u" mismatch, refusals and active control"_q,
			.skipReason = gate,
			.run = [=] {
				RunControlLeg(*state, *leg);
			},
			.then = [=] {
				CheckControlLeg(*state, *leg);
			},
		});
	};

	appendLeg(&state->inner, u"HistoryInner"_q);

	runner->add({
		.name = u"chat-list menu self-test: open Saved Messages in the "
			"HistoryView::ChatWidget"_q,
		.skipReason = gate,
		.run = [=] {
			// Nothing is injected again: ChatWidget's
			// Data::HistoryMessagesViewer seeds History::messages() from the
			// blocks, A and B included, and a later History::addNewItem with
			// these ids would reach SparseIdsList::addNew's assertion.
			OpenSavedMessages(*state, true);
		},
		.until = [=] {
			return ModernReady(*state);
		},
		.then = [=] {
			CheckModernOpened(*state);
		},
		.timeout = kOpenBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->waitDetails;
		},
	});

	appendLeg(&state->modern, u"ListWidget"_q);

	runner->add({
		.name = u"chat-list menu self-test: teardown"_q,
		.skipReason = [=] {
			return state->captured
				? QString()
				: u"nothing was captured, so nothing was changed"_q;
		},
		.run = [=] {
			state->release = Release(*state, u"teardown"_q);
		},
		.then = [=] {
			CheckRelease(*state);
		},
	});

	runner->add({
		.name = u"chat-list menu self-test: settle after the teardown"_q,
		.skipReason = [=] {
			return state->captured
				? QString()
				: u"nothing was captured, so nothing was changed"_q;
		},
		.run = [=] {
			state->waitStarted = crl::now();
		},
		.until = [=] {
			const auto since = state->restoredAt
				? state->restoredAt
				: state->waitStarted;
			return ((crl::now() - since) >= kRepaintSettle
					&& WindowMatches(*state))
				|| WaitBoundReached(*state, kRestoreBound);
		},
		.then = [=] {
			CheckSettled(*state);
		},
		.timeout = kRestoreBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return WindowText(*state);
		},
	});
}

} // namespace Test

#endif // _DEBUG
