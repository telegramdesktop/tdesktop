/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/rect_part.h"
#include "ui/ui_integration.h"

#include <QtCore/QEvent>

class QPixmap;
class QImage;

enum class RectPart;
using RectParts = base::flags<RectPart>;

template <typename Object>
class object_ptr;

namespace Ui {
namespace details {

struct ForwardTag {
};

struct InPlaceTag {
};

template <typename Value>
class AttachmentOwner : public QObject {
public:
	template <typename OtherValue>
	AttachmentOwner(QObject *parent, const ForwardTag&, OtherValue &&value)
	: QObject(parent)
	, _value(std::forward<OtherValue>(value)) {
	}

	template <typename ...Args>
	AttachmentOwner(QObject *parent, const InPlaceTag&, Args &&...args)
	: QObject(parent)
	, _value(std::forward<Args>(args)...) {
	}

	not_null<Value*> value() {
		return &_value;
	}

private:
	Value _value;

};

} // namespace details

template <typename Widget, typename ...Args>
inline base::unique_qptr<Widget> CreateObject(Args &&...args) {
	return base::make_unique_q<Widget>(
		nullptr,
		std::forward<Args>(args)...);
}

template <typename Value, typename Parent, typename ...Args>
inline Value *CreateChild(
		Parent *parent,
		Args &&...args) {
	Expects(parent != nullptr);

	if constexpr (std::is_base_of_v<QObject, Value>) {
		return new Value(parent, std::forward<Args>(args)...);
	} else {
		return CreateChild<details::AttachmentOwner<Value>>(
			parent,
			details::InPlaceTag{},
			std::forward<Args>(args)...)->value();
	}
}

inline void DestroyChild(QWidget *child) {
	delete child;
}

template <typename ...Args>
inline auto Connect(Args &&...args) {
	return QObject::connect(std::forward<Args>(args)...);
}

template <typename Value>
inline not_null<std::decay_t<Value>*> AttachAsChild(
		not_null<QObject*> parent,
		Value &&value) {
	return CreateChild<details::AttachmentOwner<std::decay_t<Value>>>(
		parent.get(),
		details::ForwardTag{},
		std::forward<Value>(value))->value();
}

[[nodiscard]] inline bool InFocusChain(not_null<const QWidget*> widget) {
	if (const auto top = widget->window()) {
		if (auto focused = top->focusWidget()) {
			return !widget->isHidden()
				&& (focused == widget
					|| widget->isAncestorOf(focused));
		}
	}
	return false;
}

template <typename ChildWidget>
inline ChildWidget *AttachParentChild(
		not_null<QWidget*> parent,
		const object_ptr<ChildWidget> &child) {
	if (const auto raw = child.data()) {
		raw->setParent(parent);
		raw->show();
		return raw;
	}
	return nullptr;
}

void SendPendingMoveResizeEvents(not_null<QWidget*> target);

[[nodiscard]] QPixmap GrabWidget(
	not_null<QWidget*> target,
	QRect rect = QRect(),
	QColor bg = QColor(255, 255, 255, 0));

[[nodiscard]] QImage GrabWidgetToImage(
	not_null<QWidget*> target,
	QRect rect = QRect(),
	QColor bg = QColor(255, 255, 255, 0));

void RenderWidget(
	QPainter &painter,
	not_null<QWidget*> source,
	const QPoint &targetOffset = QPoint(),
	const QRegion &sourceRegion = QRegion(),
	QWidget::RenderFlags renderFlags
	= QWidget::DrawChildren | QWidget::IgnoreMask);

void ForceFullRepaint(not_null<QWidget*> widget);

void PostponeCall(FnMut<void()> &&callable);

template <
	typename Guard,
	typename Callable,
	typename GuardTraits = crl::guard_traits<std::decay_t<Guard>>,
	typename = std::enable_if_t<
	sizeof(GuardTraits) != crl::details::dependent_zero<GuardTraits>>>
inline void PostponeCall(Guard && object, Callable && callable) {
	return PostponeCall(crl::guard(
		std::forward<Guard>(object),
		std::forward<Callable>(callable)));
}

void SendSynteticMouseEvent(
	QWidget *widget,
	QEvent::Type type,
	Qt::MouseButton button,
	const QPoint &globalPoint);

inline void SendSynteticMouseEvent(
		QWidget *widget,
		QEvent::Type type,
		Qt::MouseButton button) {
	return SendSynteticMouseEvent(widget, type, button, QCursor::pos());
}

template <typename Widget>
QPointer<Widget> MakeWeak(Widget *object) {
	return QPointer<Widget>(object);
}

template <typename Widget>
QPointer<const Widget> MakeWeak(const Widget *object) {
	return QPointer<const Widget>(object);
}

template <typename Widget>
QPointer<Widget> MakeWeak(not_null<Widget*> object) {
	return QPointer<Widget>(object.get());
}

template <typename Widget>
QPointer<const Widget> MakeWeak(not_null<const Widget*> object) {
	return QPointer<const Widget>(object.get());
}

[[nodiscard]] QPixmap PixmapFromImage(QImage &&image);

} // namespace Ui
