/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"

namespace Fonts {

void Start();
QString GetOverride(const QString &familyName);

} // namespace

class TWidget;

template <typename Object>
class object_ptr;

namespace Ui {

inline bool InFocusChain(not_null<const QWidget*> widget) {
	if (auto top = widget->window()) {
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
	if (auto raw = child.data()) {
		raw->setParent(parent);
		raw->show();
		return raw;
	}
	return nullptr;
}

void SendPendingMoveResizeEvents(not_null<QWidget*> target);

QPixmap GrabWidget(
	not_null<QWidget*> target,
	QRect rect = QRect(),
	QColor bg = QColor(255, 255, 255, 0));
QImage GrabWidgetToImage(
	not_null<QWidget*> target,
	QRect rect = QRect(),
	QColor bg = QColor(255, 255, 255, 0));

void ForceFullRepaint(not_null<QWidget*> widget);

} // namespace Ui

enum class RectPart {
	None        = 0,

	TopLeft     = (1 << 0),
	Top         = (1 << 1),
	TopRight    = (1 << 2),
	Left        = (1 << 3),
	Center      = (1 << 4),
	Right       = (1 << 5),
	BottomLeft  = (1 << 6),
	Bottom      = (1 << 7),
	BottomRight = (1 << 8),

	FullTop     = TopLeft | Top | TopRight,
	NoTopBottom = Left | Center | Right,
	FullBottom  = BottomLeft | Bottom | BottomRight,
	NoTop       = NoTopBottom | FullBottom,
	NoBottom    = FullTop | NoTopBottom,

	FullLeft    = TopLeft | Left | BottomLeft,
	NoLeftRight = Top | Center | Bottom,
	FullRight   = TopRight | Right | BottomRight,
	NoLeft      = NoLeftRight | FullRight,
	NoRight     = FullLeft | NoLeftRight,

	AllCorners = TopLeft | TopRight | BottomLeft | BottomRight,
	AllSides   = Top | Bottom | Left | Right,

	Full        = FullTop | NoTop,
};
using RectParts = base::flags<RectPart>;
inline constexpr auto is_flag_type(RectPart) { return true; };

inline bool IsTopCorner(RectPart corner) {
	return (corner == RectPart::TopLeft) || (corner == RectPart::TopRight);
}

inline bool IsBottomCorner(RectPart corner) {
	return (corner == RectPart::BottomLeft) || (corner == RectPart::BottomRight);
}

inline bool IsLeftCorner(RectPart corner) {
	return (corner == RectPart::TopLeft) || (corner == RectPart::BottomLeft);
}

inline bool IsRightCorner(RectPart corner) {
	return (corner == RectPart::TopRight) || (corner == RectPart::BottomRight);
}

class Painter : public QPainter {
public:
	explicit Painter(QPaintDevice *device) : QPainter(device) {
	}

	void drawTextLeft(int x, int y, int outerw, const QString &text, int textWidth = -1) {
		QFontMetrics m(fontMetrics());
		if (rtl() && textWidth < 0) textWidth = m.width(text);
		drawText(rtl() ? (outerw - x - textWidth) : x, y + m.ascent(), text);
	}
	void drawTextRight(int x, int y, int outerw, const QString &text, int textWidth = -1) {
		QFontMetrics m(fontMetrics());
		if (!rtl() && textWidth < 0) textWidth = m.width(text);
		drawText(rtl() ? x : (outerw - x - textWidth), y + m.ascent(), text);
	}
	void drawPixmapLeft(int x, int y, int outerw, const QPixmap &pix, const QRect &from) {
		drawPixmap(QPoint(rtl() ? (outerw - x - (from.width() / pix.devicePixelRatio())) : x, y), pix, from);
	}
	void drawPixmapLeft(const QPoint &p, int outerw, const QPixmap &pix, const QRect &from) {
		return drawPixmapLeft(p.x(), p.y(), outerw, pix, from);
	}
	void drawPixmapLeft(int x, int y, int w, int h, int outerw, const QPixmap &pix, const QRect &from) {
		drawPixmap(QRect(rtl() ? (outerw - x - w) : x, y, w, h), pix, from);
	}
	void drawPixmapLeft(const QRect &r, int outerw, const QPixmap &pix, const QRect &from) {
		return drawPixmapLeft(r.x(), r.y(), r.width(), r.height(), outerw, pix, from);
	}
	void drawPixmapLeft(int x, int y, int outerw, const QPixmap &pix) {
		drawPixmap(QPoint(rtl() ? (outerw - x - (pix.width() / pix.devicePixelRatio())) : x, y), pix);
	}
	void drawPixmapLeft(const QPoint &p, int outerw, const QPixmap &pix) {
		return drawPixmapLeft(p.x(), p.y(), outerw, pix);
	}
	void drawPixmapRight(int x, int y, int outerw, const QPixmap &pix, const QRect &from) {
		drawPixmap(QPoint(rtl() ? x : (outerw - x - (from.width() / pix.devicePixelRatio())), y), pix, from);
	}
	void drawPixmapRight(const QPoint &p, int outerw, const QPixmap &pix, const QRect &from) {
		return drawPixmapRight(p.x(), p.y(), outerw, pix, from);
	}
	void drawPixmapRight(int x, int y, int w, int h, int outerw, const QPixmap &pix, const QRect &from) {
		drawPixmap(QRect(rtl() ? x : (outerw - x - w), y, w, h), pix, from);
	}
	void drawPixmapRight(const QRect &r, int outerw, const QPixmap &pix, const QRect &from) {
		return drawPixmapRight(r.x(), r.y(), r.width(), r.height(), outerw, pix, from);
	}
	void drawPixmapRight(int x, int y, int outerw, const QPixmap &pix) {
		drawPixmap(QPoint(rtl() ? x : (outerw - x - (pix.width() / pix.devicePixelRatio())), y), pix);
	}
	void drawPixmapRight(const QPoint &p, int outerw, const QPixmap &pix) {
		return drawPixmapRight(p.x(), p.y(), outerw, pix);
	}

	void setTextPalette(const style::TextPalette &palette) {
		_textPalette = &palette;
	}
	void restoreTextPalette() {
		_textPalette = nullptr;
	}
	const style::TextPalette &textPalette() const {
		return _textPalette ? *_textPalette : st::defaultTextPalette;
	}

private:
	const style::TextPalette *_textPalette = nullptr;

};

class PainterHighQualityEnabler {
public:
	PainterHighQualityEnabler(QPainter &p) : _painter(p) {
		static constexpr QPainter::RenderHint Hints[] = {
			QPainter::Antialiasing,
			QPainter::SmoothPixmapTransform,
			QPainter::TextAntialiasing,
			QPainter::HighQualityAntialiasing
		};

		const auto hints = _painter.renderHints();
		for (const auto hint : Hints) {
			if (!(hints & hint)) {
				_hints |= hint;
			}
		}
		if (_hints) {
			_painter.setRenderHints(_hints);
		}
	}

	PainterHighQualityEnabler(
		const PainterHighQualityEnabler &other) = delete;
	PainterHighQualityEnabler &operator=(
		const PainterHighQualityEnabler &other) = delete;

	~PainterHighQualityEnabler() {
		if (_hints) {
			_painter.setRenderHints(_hints, false);
		}
	}

private:
	QPainter &_painter;
	QPainter::RenderHints _hints = 0;

};

template <typename Base>
class TWidgetHelper : public Base {
public:
	using Base::Base;

	virtual QMargins getMargins() const {
		return QMargins();
	}

	void moveToLeft(int x, int y, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.left();
		y -= margins.top();
		Base::move(rtl() ? ((outerw > 0 ? outerw : Base::parentWidget()->width()) - x - Base::width()) : x, y);
	}
	void moveToRight(int x, int y, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.right();
		y -= margins.top();
		Base::move(rtl() ? x : ((outerw > 0 ? outerw : Base::parentWidget()->width()) - x - Base::width()), y);
	}
	void setGeometryToLeft(int x, int y, int w, int h, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.left();
		y -= margins.top();
		w -= margins.left() - margins.right();
		h -= margins.top() - margins.bottom();
		Base::setGeometry(rtl() ? ((outerw > 0 ? outerw : Base::parentWidget()->width()) - x - w) : x, y, w, h);
	}
	void setGeometryToRight(int x, int y, int w, int h, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.right();
		y -= margins.top();
		w -= margins.left() - margins.right();
		h -= margins.top() - margins.bottom();
		Base::setGeometry(rtl() ? x : ((outerw > 0 ? outerw : Base::parentWidget()->width()) - x - w), y, w, h);
	}
	QPoint myrtlpoint(int x, int y) const {
		return rtlpoint(x, y, Base::width());
	}
	QPoint myrtlpoint(const QPoint point) const {
		return rtlpoint(point, Base::width());
	}
	QRect myrtlrect(int x, int y, int w, int h) const {
		return rtlrect(x, y, w, h, Base::width());
	}
	QRect myrtlrect(const QRect &rect) const {
		return rtlrect(rect, Base::width());
	}
	void rtlupdate(const QRect &rect) {
		Base::update(myrtlrect(rect));
	}
	void rtlupdate(int x, int y, int w, int h) {
		Base::update(myrtlrect(x, y, w, h));
	}

	QPoint mapFromGlobal(const QPoint &point) const {
		return Base::mapFromGlobal(point);
	}
	QPoint mapToGlobal(const QPoint &point) const {
		return Base::mapToGlobal(point);
	}
	QRect mapFromGlobal(const QRect &rect) const {
		return QRect(mapFromGlobal(rect.topLeft()), rect.size());
	}
	QRect mapToGlobal(const QRect &rect) const {
		return QRect(mapToGlobal(rect.topLeft()), rect.size());
	}

protected:
	void enterEvent(QEvent *e) final override {
		if (auto parent = tparent()) {
			parent->leaveToChildEvent(e, this);
		}
		return enterEventHook(e);
	}
	virtual void enterEventHook(QEvent *e) {
		return Base::enterEvent(e);
	}

	void leaveEvent(QEvent *e) final override {
		if (auto parent = tparent()) {
			parent->enterFromChildEvent(e, this);
		}
		return leaveEventHook(e);
	}
	virtual void leaveEventHook(QEvent *e) {
		return Base::leaveEvent(e);
	}

	// e - from enterEvent() of child TWidget
	virtual void leaveToChildEvent(QEvent *e, QWidget *child) {
	}

	// e - from leaveEvent() of child TWidget
	virtual void enterFromChildEvent(QEvent *e, QWidget *child) {
	}

private:
	TWidget *tparent() {
		return qobject_cast<TWidget*>(Base::parentWidget());
	}
	const TWidget *tparent() const {
		return qobject_cast<const TWidget*>(Base::parentWidget());
	}

	template <typename OtherBase>
	friend class TWidgetHelper;

};

class TWidget : public TWidgetHelper<QWidget> {
	Q_OBJECT

public:
	TWidget(QWidget *parent = nullptr) : TWidgetHelper<QWidget>(parent) {
	}

	bool inFocusChain() const {
		return Ui::InFocusChain(this);
	}

	void hideChildren() {
		for (auto child : children()) {
			if (child->isWidgetType()) {
				static_cast<QWidget*>(child)->hide();
			}
		}
	}
	void showChildren() {
		for (auto child : children()) {
			if (child->isWidgetType()) {
				static_cast<QWidget*>(child)->show();
			}
		}
	}

	// Get the size of the widget as it should be.
	// Negative return value means no default width.
	virtual int naturalWidth() const {
		return -1;
	}

	// Count new height for width=newWidth and resize to it.
	void resizeToWidth(int newWidth) {
		auto margins = getMargins();
		auto fullWidth = margins.left() + newWidth + margins.right();
		auto fullHeight = margins.top() + resizeGetHeight(newWidth) + margins.bottom();
		auto newSize = QSize(fullWidth, fullHeight);
		if (newSize != size()) {
			resize(newSize);
			update();
		}
	}

	// Resize to minimum of natural width and available width.
	void resizeToNaturalWidth(int newWidth) {
		auto maxWidth = naturalWidth();
		resizeToWidth((maxWidth >= 0) ? qMin(newWidth, maxWidth) : newWidth);
	}

	QRect rectNoMargins() const {
		return rect().marginsRemoved(getMargins());
	}

	int widthNoMargins() const {
		return rectNoMargins().width();
	}

	int heightNoMargins() const {
		return rectNoMargins().height();
	}

	int bottomNoMargins() const {
		auto rectWithoutMargins = rectNoMargins();
		return y() + rectWithoutMargins.y() + rectWithoutMargins.height();
	}

	QSize sizeNoMargins() const {
		return rectNoMargins().size();
	}

	// Updates the area that is visible inside the scroll container.
	void setVisibleTopBottom(int visibleTop, int visibleBottom) {
		auto max = height();
		visibleTopBottomUpdated(
			snap(visibleTop, 0, max),
			snap(visibleBottom, 0, max));
	}

signals:
	// Child widget is responsible for emitting this signal.
	void heightUpdated();

protected:
	void setChildVisibleTopBottom(
			TWidget *child,
			int visibleTop,
			int visibleBottom) {
		if (child) {
			auto top = child->y();
			child->setVisibleTopBottom(
				visibleTop - top,
				visibleBottom - top);
		}
	}

	// Resizes content and counts natural widget height for the desired width.
	virtual int resizeGetHeight(int newWidth) {
		return heightNoMargins();
	}

	virtual void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	}

};

template <typename Widget>
QPointer<Widget> make_weak(Widget *object) {
	return QPointer<Widget>(object);
}

template <typename Widget>
QPointer<const Widget> make_weak(const Widget *object) {
	return QPointer<const Widget>(object);
}

template <typename Widget>
QPointer<Widget> make_weak(not_null<Widget*> object) {
	return QPointer<Widget>(object.get());
}

template <typename Widget>
QPointer<const Widget> make_weak(not_null<const Widget*> object) {
	return QPointer<const Widget>(object.get());
}

class SingleQueuedInvokation : public QObject {
public:
	SingleQueuedInvokation(Fn<void()> callback) : _callback(callback) {
	}
	void call() {
		if (_pending.testAndSetAcquire(0, 1)) {
			InvokeQueued(this, [this] {
				if (_pending.testAndSetRelease(1, 0)) {
					_callback();
				}
			});
		}
	}

private:
	Fn<void()> _callback;
	QAtomicInt _pending = { 0 };

};

// Smart pointer for QObject*, has move semantics, destroys object if it doesn't have a parent.
template <typename Object>
class object_ptr {
public:
	object_ptr(std::nullptr_t) noexcept {
	}

	// No default constructor, but constructors with at least
	// one argument are simply make functions.
	template <typename Parent, typename... Args>
	explicit object_ptr(Parent &&parent, Args&&... args)
	: _object(new Object(std::forward<Parent>(parent), std::forward<Args>(args)...)) {
	}
	static object_ptr<Object> fromRaw(Object *value) noexcept {
		object_ptr<Object> result = { nullptr };
		result._object = value;
		return result;
	}

	object_ptr(const object_ptr &other) = delete;
	object_ptr &operator=(const object_ptr &other) = delete;
	object_ptr(object_ptr &&other) noexcept : _object(base::take(other._object)) {
	}
	object_ptr &operator=(object_ptr &&other) noexcept {
		auto temp = std::move(other);
		destroy();
		std::swap(_object, temp._object);
		return *this;
	}

	template <
		typename OtherObject,
		typename = std::enable_if_t<
			std::is_base_of_v<Object, OtherObject>>>
	object_ptr(object_ptr<OtherObject> &&other) noexcept
	: _object(base::take(other._object)) {
	}

	template <
		typename OtherObject,
		typename = std::enable_if_t<
			std::is_base_of_v<Object, OtherObject>>>
	object_ptr &operator=(object_ptr<OtherObject> &&other) noexcept {
		_object = base::take(other._object);
		return *this;
	}

	object_ptr &operator=(std::nullptr_t) noexcept {
		_object = nullptr;
		return *this;
	}

	// So we can pass this pointer to methods like connect().
	Object *data() const noexcept {
		return static_cast<Object*>(_object.data());
	}
	operator Object*() const noexcept {
		return data();
	}

	explicit operator bool() const noexcept {
		return _object != nullptr;
	}

	Object *operator->() const noexcept {
		return data();
	}
	Object &operator*() const noexcept {
		return *data();
	}

	// Use that instead "= new Object(parent, ...)"
	template <typename Parent, typename... Args>
	Object *create(Parent &&parent, Args&&... args) {
		destroy();
		_object = new Object(
			std::forward<Parent>(parent),
			std::forward<Args>(args)...);
		return data();
	}
	void destroy() noexcept {
		delete base::take(_object);
	}
	void destroyDelayed() {
		if (_object) {
			if (auto widget = base::up_cast<QWidget*>(data())) {
				widget->hide();
			}
			base::take(_object)->deleteLater();
		}
	}

	~object_ptr() noexcept {
		if (auto pointer = _object) {
			if (!pointer->parent()) {
				destroy();
			}
		}
	}

	template <typename ResultType, typename SourceType>
	friend object_ptr<ResultType> static_object_cast(
		object_ptr<SourceType> source);

private:
	template <typename OtherObject>
	friend class object_ptr;

	QPointer<QObject> _object;

};

template <typename ResultType, typename SourceType>
inline object_ptr<ResultType> static_object_cast(
		object_ptr<SourceType> source) {
	auto result = object_ptr<ResultType>(nullptr);
	result._object = static_cast<ResultType*>(
		base::take(source._object).data());
	return std::move(result);
}

void sendSynteticMouseEvent(
	QWidget *widget,
	QEvent::Type type,
	Qt::MouseButton button,
	const QPoint &globalPoint);

inline void sendSynteticMouseEvent(
		QWidget *widget,
		QEvent::Type type,
		Qt::MouseButton button) {
	return sendSynteticMouseEvent(widget, type, button, QCursor::pos());
}
