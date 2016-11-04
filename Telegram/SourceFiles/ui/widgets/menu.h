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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "styles/style_widgets.h"

namespace Ui {

class Menu : public TWidget {
	Q_OBJECT

public:
	Menu(QWidget *parent, const style::Menu &st = st::defaultMenu);
	Menu(QWidget *parent, QMenu *menu, const style::Menu &st = st::defaultMenu);

	QAction *addAction(const QString &text, const QObject *receiver, const char* member, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	QAction *addAction(const QString &text, base::lambda_unique<void()> callback, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	QAction *addSeparator();
	void clearActions();

	void clearSelection();

	enum class TriggeredSource {
		Mouse,
		Keyboard,
	};
	void setChildShown(bool shown) {
		_childShown = shown;
	}
	void setShowSource(TriggeredSource source);

	using Actions = QList<QAction*>;
	Actions &actions();

	void setResizedCallback(base::lambda_unique<void()> callback) {
		_resizedCallback = std_::move(callback);
	}

	void setActivatedCallback(base::lambda_unique<void(QAction *action, int actionTop, TriggeredSource source)> callback) {
		_activatedCallback = std_::move(callback);
	}
	void setTriggeredCallback(base::lambda_unique<void(QAction *action, int actionTop, TriggeredSource source)> callback) {
		_triggeredCallback = std_::move(callback);
	}

	void setKeyPressDelegate(base::lambda_unique<bool(int key)> delegate) {
		_keyPressDelegate = std_::move(delegate);
	}
	void handleKeyPress(int key);

	void setMouseMoveDelegate(base::lambda_unique<void(QPoint globalPosition)> delegate) {
		_mouseMoveDelegate = std_::move(delegate);
	}
	void handleMouseMove(QPoint globalPosition);

	void setMousePressDelegate(base::lambda_unique<void(QPoint globalPosition)> delegate) {
		_mousePressDelegate = std_::move(delegate);
	}
	void handleMousePress(QPoint globalPosition);

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;

private slots:
	void actionChanged();

private:
	void updateSelected(QPoint globalPosition);
	void init();

	// Returns the new width.
	int processAction(QAction *action, int index, int width);
	QAction *addAction(QAction *a, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);

	void setSelected(int selected);
	void clearMouseSelection();

	int itemTop(int index);
	void updateSelectedItem();
	void itemPressed(TriggeredSource source);

	const style::Menu &_st;

	base::lambda_unique<void()> _resizedCallback;
	base::lambda_unique<void(QAction *action, int actionTop, TriggeredSource source)> _activatedCallback;
	base::lambda_unique<void(QAction *action, int actionTop, TriggeredSource source)> _triggeredCallback;
	base::lambda_unique<bool(int key)> _keyPressDelegate;
	base::lambda_unique<void(QPoint globalPosition)> _mouseMoveDelegate;
	base::lambda_unique<void(QPoint globalPosition)> _mousePressDelegate;

	struct ActionData {
		bool hasSubmenu = false;
		QString text;
		QString shortcut;
		const style::icon *icon = nullptr;
		const style::icon *iconOver = nullptr;
	};
	using ActionsData = QList<ActionData>;

	QMenu *_wappedMenu = nullptr;
	Actions _actions;
	ActionsData _actionsData;

	int _itemHeight, _separatorHeight;

	bool _mouseSelection = false;

	int _selected = -1;
	bool _childShown = false;

};

} // namespace Ui
