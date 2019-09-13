/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/style/style_core_direction.h"
#include "ui/ui_utility.h"

#include <rpl/event_stream.h>
#include <rpl/map.h>
#include <rpl/distinct_until_changed.h>

#include <QtWidgets/QWidget>
#include <QtCore/QPointer>

class TWidget;

template <typename Base>
class TWidgetHelper : public Base {
public:
	using Base::Base;

	virtual QMargins getMargins() const {
		return QMargins();
	}

	bool inFocusChain() const {
		return Ui::InFocusChain(this);
	}

	void hideChildren() {
		for (auto child : Base::children()) {
			if (child->isWidgetType()) {
				static_cast<QWidget*>(child)->hide();
			}
		}
	}
	void showChildren() {
		for (auto child : Base::children()) {
			if (child->isWidgetType()) {
				static_cast<QWidget*>(child)->show();
			}
		}
	}

	void moveToLeft(int x, int y, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.left();
		y -= margins.top();
		Base::move(style::RightToLeft() ? ((outerw > 0 ? outerw : Base::parentWidget()->width()) - x - Base::width()) : x, y);
	}
	void moveToRight(int x, int y, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.right();
		y -= margins.top();
		Base::move(style::RightToLeft() ? x : ((outerw > 0 ? outerw : Base::parentWidget()->width()) - x - Base::width()), y);
	}
	void setGeometryToLeft(int x, int y, int w, int h, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.left();
		y -= margins.top();
		w -= margins.left() - margins.right();
		h -= margins.top() - margins.bottom();
		Base::setGeometry(style::RightToLeft() ? ((outerw > 0 ? outerw : Base::parentWidget()->width()) - x - w) : x, y, w, h);
	}
	void setGeometryToRight(int x, int y, int w, int h, int outerw = 0) {
		auto margins = getMargins();
		x -= margins.right();
		y -= margins.top();
		w -= margins.left() - margins.right();
		h -= margins.top() - margins.bottom();
		Base::setGeometry(style::RightToLeft() ? x : ((outerw > 0 ? outerw : Base::parentWidget()->width()) - x - w), y, w, h);
	}
	QPoint myrtlpoint(int x, int y) const {
		return style::rtlpoint(x, y, Base::width());
	}
	QPoint myrtlpoint(const QPoint point) const {
		return style::rtlpoint(point, Base::width());
	}
	QRect myrtlrect(int x, int y, int w, int h) const {
		return style::rtlrect(x, y, w, h, Base::width());
	}
	QRect myrtlrect(const QRect &rect) const {
		return style::rtlrect(rect, Base::width());
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
	// The Q_OBJECT meta info is used for qobject_cast!
	Q_OBJECT

public:
	TWidget(QWidget *parent = nullptr) : TWidgetHelper<QWidget>(parent) {
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
			std::clamp(visibleTop, 0, max),
			std::clamp(visibleBottom, 0, max));
	}

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

namespace Ui {

class RpWidget;

void ResizeFitChild(not_null<RpWidget*> parent, not_null<RpWidget*> child);

template <typename Widget>
using RpWidgetParent = std::conditional_t<
	std::is_same_v<Widget, QWidget>,
	TWidget,
	TWidgetHelper<Widget>>;

template <typename Widget>
class RpWidgetWrap;

class RpWidgetMethods {
public:
	rpl::producer<QRect> geometryValue() const;
	rpl::producer<QSize> sizeValue() const;
	rpl::producer<int> heightValue() const;
	rpl::producer<int> widthValue() const;
	rpl::producer<QPoint> positionValue() const;
	rpl::producer<int> leftValue() const;
	rpl::producer<int> topValue() const;
	virtual rpl::producer<int> desiredHeightValue() const;
	rpl::producer<bool> shownValue() const;
	rpl::producer<QRect> paintRequest() const;
	rpl::producer<> alive() const;
	rpl::producer<> windowDeactivateEvents() const;
	rpl::producer<> macWindowDeactivateEvents() const;

	template <typename Error, typename Generator>
	void showOn(rpl::producer<bool, Error, Generator> &&shown) {
		std::move(
			shown
		) | rpl::start_with_next([this](bool visible) {
			callSetVisible(visible);
		}, lifetime());
	}


	rpl::lifetime &lifetime();

	virtual ~RpWidgetMethods() = default;

protected:
	bool handleEvent(QEvent *event);
	virtual bool eventHook(QEvent *event) = 0;

private:
	template <typename Widget>
	friend class RpWidgetWrap;

	struct EventStreams {
		rpl::event_stream<QRect> geometry;
		rpl::event_stream<QRect> paint;
		rpl::event_stream<bool> shown;
		rpl::event_stream<> alive;
	};
	struct Initer {
		Initer(QWidget *parent);
	};

	virtual void callSetVisible(bool visible) = 0;
	virtual QWidget *callGetWidget() = 0;
	virtual const QWidget *callGetWidget() const = 0;
	virtual QPointer<QObject> callCreateWeak() = 0;
	virtual QRect callGetGeometry() const = 0;
	virtual bool callIsHidden() const = 0;

	void visibilityChangedHook(bool wasVisible, bool nowVisible);
	EventStreams &eventStreams() const;

	mutable std::unique_ptr<EventStreams> _eventStreams;
	rpl::lifetime _lifetime;

};

template <typename Widget>
class RpWidgetWrap
	: public RpWidgetParent<Widget>
	, public RpWidgetMethods {
	using Self = RpWidgetWrap<Widget>;
	using Parent = RpWidgetParent<Widget>;

public:
	using Parent::Parent;

	void setVisible(bool visible) final override {
		auto wasVisible = !this->isHidden();
		setVisibleHook(visible);
		visibilityChangedHook(wasVisible, !this->isHidden());
	}

	~RpWidgetWrap() {
		base::take(_lifetime);
		base::take(_eventStreams);
	}

protected:
	bool event(QEvent *event) final override {
		return handleEvent(event);
	}
	bool eventHook(QEvent *event) override {
		return Parent::event(event);
	}
	virtual void setVisibleHook(bool visible) {
		Parent::setVisible(visible);
	}

private:
	void callSetVisible(bool visible) override {
		Self::setVisible(visible);
	}
	QWidget *callGetWidget() override {
		return this;
	}
	const QWidget *callGetWidget() const override {
		return this;
	}
	QPointer<QObject> callCreateWeak() override {
		return QPointer<QObject>((QObject*)this);
	}
	QRect callGetGeometry() const override {
		return this->geometry();
	}
	bool callIsHidden() const override {
		return this->isHidden();
	}

	Initer _initer = { this };

};

class RpWidget : public RpWidgetWrap<QWidget> {
public:
	using RpWidgetWrap<QWidget>::RpWidgetWrap;

};

} // namespace Ui
