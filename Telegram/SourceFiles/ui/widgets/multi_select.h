/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/style_widgets.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/object_ptr.h"

#include <set>

namespace Ui {

class InputField;
class CrossButton;
class ScrollArea;

class MultiSelect : public RpWidget {
public:
	MultiSelect(
		QWidget *parent,
		const style::MultiSelect &st,
		rpl::producer<QString> placeholder = nullptr);

	QString getQuery() const;
	void setInnerFocus();
	void clearQuery();

	void setQueryChangedCallback(Fn<void(const QString &query)> callback);
	void setSubmittedCallback(Fn<void(Qt::KeyboardModifiers)> callback);
	void setCancelledCallback(Fn<void()> callback);
	void setResizedCallback(Fn<void()> callback);

	enum class AddItemWay {
		Default,
		SkipAnimation,
	};
	using PaintRoundImage = Fn<void(Painter &p, int x, int y, int outerWidth, int size)>;
	void addItem(uint64 itemId, const QString &text, style::color color, PaintRoundImage paintRoundImage, AddItemWay way = AddItemWay::Default);
	void addItemInBunch(uint64 itemId, const QString &text, style::color color, PaintRoundImage paintRoundImage);
	void finishItemsBunch();
	void setItemText(uint64 itemId, const QString &text);

	void setItemRemovedCallback(Fn<void(uint64 itemId)> callback);
	void removeItem(uint64 itemId);

	int getItemsCount() const;
	QVector<uint64> getItems() const;
	bool hasItem(uint64 itemId) const;

	class Item;

protected:
	int resizeGetHeight(int newWidth) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void scrollTo(int activeTop, int activeBottom);

	const style::MultiSelect &_st;

	object_ptr<Ui::ScrollArea> _scroll;

	class Inner;
	QPointer<Inner> _inner;

	Fn<void()> _resizedCallback;
	Fn<void(const QString &query)> _queryChangedCallback;

};

// This class is hold in header because it requires Qt preprocessing.
class MultiSelect::Inner : public TWidget {
public:
	using ScrollCallback = Fn<void(int activeTop, int activeBottom)>;
	Inner(
		QWidget *parent,
		const style::MultiSelect &st,
		rpl::producer<QString> placeholder,
		ScrollCallback callback);

	QString getQuery() const;
	bool setInnerFocus();
	void clearQuery();

	void setQueryChangedCallback(Fn<void(const QString &query)> callback);
	void setSubmittedCallback(Fn<void(Qt::KeyboardModifiers)> callback);
	void setCancelledCallback(Fn<void()> callback);

	void addItemInBunch(std::unique_ptr<Item> item);
	void finishItemsBunch(AddItemWay way);
	void setItemText(uint64 itemId, const QString &text);

	void setItemRemovedCallback(Fn<void(uint64 itemId)> callback);
	void removeItem(uint64 itemId);

	int getItemsCount() const;
	QVector<uint64> getItems() const;
	bool hasItem(uint64 itemId) const;

	void setResizedCallback(Fn<void(int heightDelta)> callback);

	~Inner();

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void submitted(Qt::KeyboardModifiers modifiers);
	void cancelled();
	void queryChanged();
	void fieldFocused();
	void computeItemsGeometry(int newWidth);
	void updateItemsGeometry();
	void updateFieldGeometry();
	void updateHasAnyItems(bool hasAnyItems);
	void updateSelection(QPoint mousePosition);
	void clearSelection() {
		updateSelection(QPoint(-1, -1));
	}
	void updateCursor();
	void updateHeightStep();
	void finishHeightAnimation();
	enum class ChangeActiveWay {
		Default,
		SkipSetFocus,
	};
	void setActiveItem(int active, ChangeActiveWay skipSetFocus = ChangeActiveWay::Default);
	void setActiveItemPrevious();
	void setActiveItemNext();

	QMargins itemPaintMargins() const;

	const style::MultiSelect &_st;
	Ui::Animations::Simple _iconOpacity;

	ScrollCallback _scrollCallback;

	std::set<uint64> _idsMap;
	std::vector<std::unique_ptr<Item>> _items;
	std::set<std::unique_ptr<Item>> _removingItems;

	int _selected = -1;
	int _active = -1;
	bool _overDelete = false;

	int _fieldLeft = 0;
	int _fieldTop = 0;
	int _fieldWidth = 0;
	object_ptr<Ui::InputField> _field;
	object_ptr<Ui::CrossButton> _cancel;

	int _newHeight = 0;
	Ui::Animations::Simple _height;

	Fn<void(const QString &query)> _queryChangedCallback;
	Fn<void(Qt::KeyboardModifiers)> _submittedCallback;
	Fn<void()> _cancelledCallback;
	Fn<void(uint64 itemId)> _itemRemovedCallback;
	Fn<void(int heightDelta)> _resizedCallback;

};

class MultiSelect::Item {
public:
	Item(const style::MultiSelectItem &st, uint64 id, const QString &text, style::color color, PaintRoundImage &&paintRoundImage);

	uint64 id() const {
		return _id;
	}
	int getWidth() const {
		return _width;
	}
	QRect rect() const {
		return QRect(_x, _y, _width, _st.height);
	}
	bool isOverDelete() const {
		return _overDelete;
	}
	void setActive(bool active) {
		_active = active;
	}
	void setPosition(int x, int y, int outerWidth, int maxVisiblePadding);
	QRect paintArea(int outerWidth) const;

	void setUpdateCallback(Fn<void()> updateCallback) {
		_updateCallback = updateCallback;
	}
	void setText(const QString &text);
	void paint(Painter &p, int outerWidth);

	void mouseMoveEvent(QPoint point);
	void leaveEvent();

	void showAnimated() {
		setVisibleAnimated(true);
	}
	void hideAnimated() {
		setVisibleAnimated(false);
	}
	bool hideFinished() const {
		return (_hiding && !_visibility.animating());
	}


private:
	void setOver(bool over);
	void paintOnce(Painter &p, int x, int y, int outerWidth);
	void paintDeleteButton(Painter &p, int x, int y, int outerWidth, float64 overOpacity);
	bool paintCached(Painter &p, int x, int y, int outerWidth);
	void prepareCache();
	void setVisibleAnimated(bool visible);

	const style::MultiSelectItem &_st;

	uint64 _id;
	struct SlideAnimation {
		SlideAnimation(Fn<void()> updateCallback, int fromX, int toX, int y, float64 duration)
			: fromX(fromX)
			, toX(toX)
			, y(y) {
			x.start(updateCallback, fromX, toX, duration);
		}
		Ui::Animations::Simple x;
		int fromX, toX;
		int y;
	};
	std::vector<SlideAnimation> _copies;
	int _x = -1;
	int _y = -1;
	int _width = 0;
	Text::String _text;
	style::color _color;
	bool _over = false;
	QPixmap _cache;
	Ui::Animations::Simple _visibility;
	Ui::Animations::Simple _overOpacity;
	bool _overDelete = false;
	bool _active = false;
	PaintRoundImage _paintRoundImage;
	Fn<void()> _updateCallback;
	bool _hiding = false;

};

} // namespace Ui
