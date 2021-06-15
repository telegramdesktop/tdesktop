/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/rect_part.h"
#include "ui/effects/animations.h"
#include "base/object_ptr.h"

namespace Window {
class SessionController;
enum class Column;
} // namespace Window

namespace Media {
namespace View {
class PlaybackProgress;
} // namespace View
} // namespace Media

namespace Media {
namespace Streaming {
class Instance;
} // namespace Streaming
} // namespace Media

namespace Media {
namespace Player {

class Float : public Ui::RpWidget, private base::Subscriber {
public:
	Float(
		QWidget *parent,
		not_null<HistoryItem*> item,
		Fn<void(bool visible)> toggleCallback,
		Fn<void(bool closed)> draggedCallback,
		Fn<void(not_null<const HistoryItem*>)> doubleClickedCallback);

	[[nodiscard]] HistoryItem *item() const {
		return _item;
	}
	void setOpacity(float64 opacity) {
		if (_opacity != opacity) {
			_opacity = opacity;
			update();
		}
	}
	[[nodiscard]] float64 countOpacityByParent() const {
		return outRatio();
	}
	[[nodiscard]] bool isReady() const {
		return (getStreamed() != nullptr);
	}
	void detach();
	[[nodiscard]] bool detached() const {
		return !_item;
	}
	[[nodiscard]] bool dragged() const {
		return _drag;
	}
	void resetMouseState() {
		_down = false;
		if (_drag) {
			finishDrag(false);
		}
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
	[[nodiscard]] float64 outRatio() const;
	[[nodiscard]] Streaming::Instance *getStreamed() const;
	[[nodiscard]] View::PlaybackProgress *getPlayback() const;
	void repaintItem();
	void prepareShadow();
	bool hasFrame() const;
	bool fillFrame();
	[[nodiscard]] QRect getInnerRect() const;
	void finishDrag(bool closed);
	void pauseResume();

	HistoryItem *_item = nullptr;
	Fn<void(bool visible)> _toggleCallback;

	float64 _opacity = 1.;

	QPixmap _shadow;
	QImage _frame;
	bool _down = false;
	QPoint _downPoint;

	bool _drag = false;
	QPoint _dragLocalPoint;
	Fn<void(bool closed)> _draggedCallback;
	Fn<void(not_null<const HistoryItem*>)> _doubleClickedCallback;

};

class FloatSectionDelegate {
public:
	virtual QRect floatPlayerAvailableRect() = 0;
	virtual bool floatPlayerHandleWheelEvent(QEvent *e) = 0;
};

class FloatDelegate {
public:
	virtual not_null<Ui::RpWidget*> floatPlayerWidget() = 0;
	virtual not_null<FloatSectionDelegate*> floatPlayerGetSection(
		Window::Column column) = 0;
	virtual void floatPlayerEnumerateSections(Fn<void(
		not_null<FloatSectionDelegate*> widget,
		Window::Column widgetColumn)> callback) = 0;
	virtual bool floatPlayerIsVisible(not_null<HistoryItem*> item) = 0;

	virtual rpl::producer<> floatPlayerCheckVisibilityRequests() {
		return _checkVisibility.events();
	}
	virtual rpl::producer<> floatPlayerHideAllRequests() {
		return _hideAll.events();
	}
	virtual rpl::producer<> floatPlayerShowVisibleRequests() {
		return _showVisible.events();
	}
	virtual rpl::producer<> floatPlayerRaiseAllRequests() {
		return _raiseAll.events();
	}
	virtual rpl::producer<> floatPlayerUpdatePositionsRequests() {
		return _updatePositions.events();
	}
	virtual rpl::producer<> floatPlayerAreaUpdates() {
		return _areaUpdates.events();
	}
	virtual void floatPlayerDoubleClickEvent(
		not_null<const HistoryItem*> item) {
	}

	struct FloatPlayerFilterWheelEventRequest {
		not_null<QObject*> object;
		not_null<QEvent*> event;
		not_null<std::optional<bool>*> result;
	};
	virtual auto floatPlayerFilterWheelEventRequests()
	-> rpl::producer<FloatPlayerFilterWheelEventRequest> {
		return _filterWheelEvent.events();
	}

	virtual ~FloatDelegate() = default;

protected:
	void floatPlayerCheckVisibility() {
		_checkVisibility.fire({});
	}
	void floatPlayerHideAll() {
		_hideAll.fire({});
	}
	void floatPlayerShowVisible() {
		_showVisible.fire({});
	}
	void floatPlayerRaiseAll() {
		_raiseAll.fire({});
	}
	void floatPlayerUpdatePositions() {
		_updatePositions.fire({});
	}
	void floatPlayerAreaUpdated() {
		_areaUpdates.fire({});
	}
	std::optional<bool> floatPlayerFilterWheelEvent(
			not_null<QObject*> object,
			not_null<QEvent*> event) {
		auto result = std::optional<bool>();
		_filterWheelEvent.fire({ object, event, &result });
		return result;
	}

private:
	rpl::event_stream<> _checkVisibility;
	rpl::event_stream<> _hideAll;
	rpl::event_stream<> _showVisible;
	rpl::event_stream<> _raiseAll;
	rpl::event_stream<> _updatePositions;
	rpl::event_stream<> _areaUpdates;
	rpl::event_stream<FloatPlayerFilterWheelEventRequest> _filterWheelEvent;

};

class FloatController : private base::Subscriber {
public:
	explicit FloatController(not_null<FloatDelegate*> delegate);

	void replaceDelegate(not_null<FloatDelegate*> delegate);
	rpl::producer<FullMsgId> closeEvents() const {
		return _closeEvents.events();
	}

private:
	struct Item {
		template <typename ToggleCallback, typename DraggedCallback>
		Item(
			not_null<QWidget*> parent,
			not_null<HistoryItem*> item,
			ToggleCallback toggle,
			DraggedCallback dragged,
			Fn<void(not_null<const HistoryItem*>)> doubleClicked);

		bool hiddenByWidget = false;
		bool hiddenByHistory = false;
		bool visible = false;
		RectPart animationSide;
		Ui::Animations::Simple visibleAnimation;
		Window::Column column;
		RectPart corner;
		QPoint dragFrom;
		Ui::Animations::Simple draggedAnimation;
		bool hiddenByDrag = false;
		object_ptr<Float> widget;
	};

	void checkCurrent();
	void create(not_null<HistoryItem*> item);
	void toggle(not_null<Item*> instance);
	void updatePosition(not_null<Item*> instance);
	void remove(not_null<Item*> instance);
	Item *current() const {
		return _items.empty() ? nullptr : _items.back().get();
	}
	void finishDrag(
		not_null<Item*> instance,
		bool closed);
	void updateColumnCorner(QPoint center);
	QPoint getPosition(not_null<Item*> instance) const;
	QPoint getHiddenPosition(
		QPoint position,
		QSize size,
		RectPart side) const;
	RectPart getSide(QPoint center) const;

	void startDelegateHandling();
	void checkVisibility();
	void hideAll();
	void showVisible();
	void raiseAll();
	void updatePositions();
	std::optional<bool> filterWheelEvent(
		not_null<QObject*> object,
		not_null<QEvent*> event);

	not_null<FloatDelegate*> _delegate;
	not_null<Ui::RpWidget*> _parent;
	std::vector<std::unique_ptr<Item>> _items;

	rpl::event_stream<FullMsgId> _closeEvents;
	rpl::lifetime _delegateLifetime;

};

} // namespace Player
} // namespace Media
