/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

namespace Fonts {
	void start();
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
	PainterHighQualityEnabler(Painter &p) : _painter(p) {
		static constexpr QPainter::RenderHint Hints[] = {
			QPainter::Antialiasing,
			QPainter::SmoothPixmapTransform,
			QPainter::TextAntialiasing,
			QPainter::HighQualityAntialiasing
		};

		auto hints = _painter.renderHints();
		for_const (auto hint, Hints) {
			if (!(hints & hint)) {
				_hints |= hint;
			}
		}
		if (_hints) {
			_painter.setRenderHints(_hints);
		}
	}
	PainterHighQualityEnabler(const PainterHighQualityEnabler &other) = delete;
	PainterHighQualityEnabler &operator=(const PainterHighQualityEnabler &other) = delete;
	~PainterHighQualityEnabler() {
		if (_hints) {
			_painter.setRenderHints(_hints, false);
		}
	}

private:
	Painter &_painter;
	QPainter::RenderHints _hints = 0;

};

#define T_WIDGET \
public: \
	TWidget *tparent() { \
		return qobject_cast<TWidget*>(parentWidget()); \
	} \
	const TWidget *tparent() const { \
		return qobject_cast<const TWidget*>(parentWidget()); \
	} \
	virtual void leaveToChildEvent(QEvent *e, QWidget *child) { /* e -- from enterEvent() of child TWidget */ \
	} \
	virtual void enterFromChildEvent(QEvent *e, QWidget *child) { /* e -- from leaveEvent() of child TWidget */ \
	} \
	void moveToLeft(int x, int y, int outerw = 0) { \
		auto margins = getMargins(); \
		x -= margins.left(); \
		y -= margins.top(); \
		move(rtl() ? ((outerw > 0 ? outerw : parentWidget()->width()) - x - width()) : x, y); \
	} \
	void moveToRight(int x, int y, int outerw = 0) { \
		auto margins = getMargins(); \
		x -= margins.right(); \
		y -= margins.top(); \
		move(rtl() ? x : ((outerw > 0 ? outerw : parentWidget()->width()) - x - width()), y); \
	} \
	void setGeometryToLeft(int x, int y, int w, int h, int outerw = 0) { \
		auto margins = getMargins(); \
		x -= margins.left(); \
		y -= margins.top(); \
		w -= margins.left() - margins.right(); \
		h -= margins.top() - margins.bottom(); \
		setGeometry(rtl() ? ((outerw > 0 ? outerw : parentWidget()->width()) - x - w) : x, y, w, h); \
	} \
	void setGeometryToRight(int x, int y, int w, int h, int outerw = 0) { \
		auto margins = getMargins(); \
		x -= margins.right(); \
		y -= margins.top(); \
		w -= margins.left() - margins.right(); \
		h -= margins.top() - margins.bottom(); \
		setGeometry(rtl() ? x : ((outerw > 0 ? outerw : parentWidget()->width()) - x - w), y, w, h); \
	} \
	QPoint myrtlpoint(int x, int y) const { \
		return rtlpoint(x, y, width()); \
	} \
	QPoint myrtlpoint(const QPoint p) const { \
		return rtlpoint(p, width()); \
	} \
	QRect myrtlrect(int x, int y, int w, int h) const { \
		return rtlrect(x, y, w, h, width()); \
	} \
	QRect myrtlrect(const QRect &r) const { \
		return rtlrect(r, width()); \
	} \
	void rtlupdate(const QRect &r) { \
		update(myrtlrect(r)); \
	} \
	void rtlupdate(int x, int y, int w, int h) { \
		update(myrtlrect(x, y, w, h)); \
	} \
protected: \
	void enterEvent(QEvent *e) override { \
		TWidget *p(tparent()); \
		if (p) p->leaveToChildEvent(e, this); \
		return enterEventHook(e); \
	} \
	void leaveEvent(QEvent *e) override { \
		TWidget *p(tparent()); \
		if (p) p->enterFromChildEvent(e, this); \
		return leaveEventHook(e); \
	}

class TWidget : public QWidget {
	Q_OBJECT
	T_WIDGET

public:
	TWidget(QWidget *parent = nullptr) : QWidget(parent) {
	}
	virtual void grabStart() {
	}
	virtual void grabFinish() {
	}

	bool inFocusChain() const;

	void hideChildren() {
		for (auto child : children()) {
			if (auto widget = qobject_cast<QWidget*>(child)) {
				widget->hide();
			}
		}
	}
	void showChildren() {
		for (auto child : children()) {
			if (auto widget = qobject_cast<QWidget*>(child)) {
				widget->show();
			}
		}
	}

	virtual QMargins getMargins() const {
		return QMargins();
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
	virtual void setVisibleTopBottom(int visibleTop, int visibleBottom) {
	}

signals:
	// Child widget is responsible for emitting this signal.
	void heightUpdated();

protected:
	void enterEventHook(QEvent *e) {
		return QWidget::enterEvent(e);
	}
	void leaveEventHook(QEvent *e) {
		return QWidget::leaveEvent(e);
	}

	// Resizes content and counts natural widget height for the desired width.
	virtual int resizeGetHeight(int newWidth) {
		return height();
	}

};

template <typename Widget>
QPointer<Widget> weak(Widget *object) {
	return QPointer<Widget>(object);
}

template <typename Widget>
QPointer<const Widget> weak(const Widget *object) {
	return QPointer<const Widget>(object);
}

void myEnsureResized(QWidget *target);
QPixmap myGrab(TWidget *target, QRect rect = QRect(), QColor bg = QColor(255, 255, 255, 0));
QImage myGrabImage(TWidget *target, QRect rect = QRect(), QColor bg = QColor(255, 255, 255, 0));

class SingleDelayedCall : public QObject {
	Q_OBJECT

public:
	SingleDelayedCall(QObject *parent, const char *member) : QObject(parent), _member(member) {
	}
	void call() {
		if (_pending.testAndSetOrdered(0, 1)) {
			QMetaObject::invokeMethod(this, "makeDelayedCall", Qt::QueuedConnection);
		}
	}

private slots:
	void makeDelayedCall() {
		if (_pending.testAndSetOrdered(1, 0)) {
			QMetaObject::invokeMethod(parent(), _member);
		}
	}

private:
	QAtomicInt _pending = { 0 };
	const char *_member;

};

// Smart pointer for QObject*, has move semantics, destroys object if it doesn't have a parent.
template <typename Object>
class object_ptr {
public:
	object_ptr(std_::nullptr_t) {
	}

	// No default constructor, but constructors with at least
	// one argument are simply make functions.
	template <typename Parent, typename... Args>
	explicit object_ptr(Parent &&parent, Args&&... args) : _object(new Object(std_::forward<Parent>(parent), std_::forward<Args>(args)...)) {
	}

	object_ptr(const object_ptr &other) = delete;
	object_ptr &operator=(const object_ptr &other) = delete;
	object_ptr(object_ptr &&other) : _object(base::take(other._object)) {
	}
	object_ptr &operator=(object_ptr &&other) {
		auto temp = std_::move(other);
		destroy();
		std_::swap_moveable(_object, temp._object);
		return *this;
	}

	template <typename OtherObject, typename = std_::enable_if_t<std_::is_base_of<Object, OtherObject>::value>>
	object_ptr(object_ptr<OtherObject> &&other) : _object(base::take(other._object)) {
	}

	template <typename OtherObject, typename = std_::enable_if_t<std_::is_base_of<Object, OtherObject>::value>>
	object_ptr &operator=(object_ptr<OtherObject> &&other) {
		_object = base::take(other._object);
		return *this;
	}

	object_ptr &operator=(std_::nullptr_t) {
		_object = nullptr;
		return *this;
	}

	// So we can pass this pointer to methods like connect().
	Object *data() const {
		return static_cast<Object*>(_object);
	}
	operator Object*() const {
		return data();
	}

	explicit operator bool() const {
		return _object != nullptr;
	}

	Object *operator->() const {
		return data();
	}
	Object &operator*() const {
		return *data();
	}

	// Use that instead "= new Object(parent, ...)"
	template <typename Parent, typename... Args>
	void create(Parent &&parent, Args&&... args) {
		destroy();
		_object = new Object(std_::forward<Parent>(parent), std_::forward<Args>(args)...);
	}
	void destroy() {
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

	~object_ptr() {
		if (auto pointer = _object) {
			if (!pointer->parent()) {
				destroy();
			}
		}
	}

	template <typename ResultType, typename SourceType>
	friend object_ptr<ResultType> static_object_cast(object_ptr<SourceType> source);

private:
	template <typename OtherObject>
	friend class object_ptr;

	QObject *_object = nullptr;

};

template <typename ResultType, typename SourceType>
inline object_ptr<ResultType> static_object_cast(object_ptr<SourceType> source) {
	auto result = object_ptr<ResultType>(nullptr);
	result._object = static_cast<ResultType*>(base::take(source._object));
	return std_::move(result);
}

void sendSynteticMouseEvent(QWidget *widget, QEvent::Type type, Qt::MouseButton button, const QPoint &globalPoint);

inline void sendSynteticMouseEvent(QWidget *widget, QEvent::Type type, Qt::MouseButton button) {
	return sendSynteticMouseEvent(widget, type, button, QCursor::pos());
}
