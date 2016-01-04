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

 Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
 Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
 */
#pragma once

class PopupMenu : public TWidget {
	Q_OBJECT

public:

	PopupMenu(const style::PopupMenu &st = st::defaultPopupMenu);
	PopupMenu(QMenu *menu, const style::PopupMenu &st = st::defaultPopupMenu);
	QAction *addAction(const QString &text, const QObject *receiver, const char* member);
	QAction *addAction(QAction *a);
	void resetActions();

	typedef QVector<QAction*> Actions;
	Actions &actions();

	void deleteOnHide(bool del);
	void popup(const QPoint &p);
	void hideMenu(bool fast = false);

	~PopupMenu();

protected:

	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);
	void keyPressEvent(QKeyEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void leaveEvent(QEvent *e);
	void enterEvent(QEvent *e);
	void focusOutEvent(QFocusEvent *e);
	void hideEvent(QHideEvent *e);

public slots:

	void actionChanged();

private:

	void updateSelected();

	void childHiding(PopupMenu *child);

	void step_hide(float64 ms, bool timer);

	void init();
	void hideFinish();

	enum PressSource {
		PressSourceMouse,
		PressSourceKeyboard,
	};

	void clearActions(bool force = false);
	int32 processAction(QAction *a, int32 index, int32 w);
	void setSelected(int32 selected);
	int32 itemY(int32 index);
	void updateSelectedItem();
	void itemPressed(PressSource source);
	void popupChildMenu(PressSource source);
	void showMenu(const QPoint &p, PopupMenu *parent, PressSource source);;

	const style::PopupMenu &_st;

	typedef QVector<PopupMenu*> PopupMenus;

	QMenu *_menu;
	Actions _actions;
	PopupMenus _menus;
	PopupMenu *_parent;
	QStringList _texts, _shortcutTexts;

	int32 _itemHeight, _separatorHeight;
	QRect _inner;
	style::margins _padding;

	QPoint _mouse;
	bool _mouseSelection;

	BoxShadow _shadow;
	int32 _selected, _childMenuIndex;

	QPixmap _cache;
	anim::fvalue a_opacity;
	Animation _a_hide;

	bool _deleteOnHide, _triggering, _deleteLater;

};
