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

#include "window/section_widget.h"
#include "window/section_memento.h"

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Ui {
class ScrollArea;
class PlainShadow;
} // namespace Ui

namespace Profile {

class BackButton;

namespace CommonGroups {

class SectionMemento : public Window::SectionMemento {
public:
	SectionMemento(not_null<UserData*> user) : _user(user) {
	}

	object_ptr<Window::SectionWidget> createWidget(QWidget *parent, not_null<Window::Controller*> controller, const QRect &geometry) override;

	not_null<UserData*> getUser() const {
		return _user;
	}
	void setScrollTop(int scrollTop) {
		_scrollTop = scrollTop;
	}
	int getScrollTop() const {
		return _scrollTop;
	}
	void setCommonGroups(const QList<not_null<PeerData*>> &groups) {
		_commonGroups = groups;
	}
	const QList<not_null<PeerData*>> &getCommonGroups() const {
		return _commonGroups;
	}

private:
	not_null<UserData*> _user;
	int _scrollTop = 0;
	QList<not_null<PeerData*>> _commonGroups;

};

class FixedBar final : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	FixedBar(QWidget *parent);

	// When animating mode is enabled the content is hidden and the
	// whole fixed bar acts like a back button.
	void setAnimatingMode(bool enabled);

protected:
	void mousePressEvent(QMouseEvent *e) override;
	int resizeGetHeight(int newWidth) override;

public slots:
	void onBack();

private:
	object_ptr<BackButton> _backButton;

	bool _animatingMode = false;

};

class InnerWidget final : public TWidget {
	Q_OBJECT

public:
	InnerWidget(QWidget *parent, not_null<UserData*> user);

	not_null<UserData*> user() const {
		return _user;
	}

	// Updates the area that is visible inside the scroll container.
	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	void resizeToWidth(int newWidth, int minHeight) {
		_minHeight = minHeight;
		return TWidget::resizeToWidth(newWidth);
	}

	void saveState(not_null<SectionMemento*> memento);
	void restoreState(not_null<SectionMemento*> memento);

	~InnerWidget();

signals:
	void cancelled();

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private:
	void updateSelected(QPoint localPos);
	void updateRow(int index);
	void showInitial(const QList<not_null<PeerData*>> &list);
	void checkPreloadMore();
	void preloadMore();
	void updateSize();
	void paintRow(Painter &p, int index, TimeMs ms);

	not_null<UserData*> _user;

	int _minHeight = 0;
	int _rowHeight = 0;
	int _contentLeft = 0;
	int _contentTop = 0;
	int _contentWidth = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;

	struct Item {
		explicit Item(PeerData *peer);
		~Item();

		PeerData * const peer;
		Text name;
		std::unique_ptr<Ui::RippleAnimation> ripple;
	};
	Item *computeItem(PeerData *group);
	QMap<PeerData*, Item*> _dataMap;
	QList<Item*> _items;
	int _selected = -1;
	int _pressed = -1;

	int32 _preloadGroupId = 0;
	mtpRequestId _preloadRequestId = 0;
	bool _allLoaded = true;

};

class Widget final : public Window::SectionWidget {
	Q_OBJECT

public:
	Widget(QWidget *parent, not_null<Window::Controller*> controller, not_null<UserData*> user);

	not_null<UserData*> user() const;
	PeerData *peerForDialogs() const override {
		return user();
	}

	bool hasTopBarShadow() const override {
		return true;
	}

	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params) override;

	bool showInternal(not_null<Window::SectionMemento*> memento) override;
	std::unique_ptr<Window::SectionMemento> createMemento() override;

	void setInternalState(const QRect &geometry, not_null<SectionMemento*> memento);

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) override;
	QRect rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) override;

protected:
	void resizeEvent(QResizeEvent *e) override;

	void showAnimatedHook() override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;

private slots:
	void onScroll();

private:
	void updateAdaptiveLayout();
	void saveState(not_null<SectionMemento*> memento);
	void restoreState(not_null<SectionMemento*> memento);

	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<InnerWidget> _inner;
	object_ptr<FixedBar> _fixedBar;
	object_ptr<Ui::PlainShadow> _fixedBarShadow;

};

} // namespace CommonGroups
} // namespace Profile
