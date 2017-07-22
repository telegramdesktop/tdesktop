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

#include "styles/style_widgets.h"

namespace Ui {

class ToggleView;
class RippleAnimation;

class Menu : public TWidget {
	Q_OBJECT

public:
	Menu(QWidget *parent, const style::Menu &st = st::defaultMenu);
	Menu(QWidget *parent, QMenu *menu, const style::Menu &st = st::defaultMenu);

	QAction *addAction(const QString &text, const QObject *receiver, const char* member, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	QAction *addAction(const QString &text, base::lambda<void()> callback, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	QAction *addSeparator();
	void clearActions();
	void finishAnimations();

	void clearSelection();

	enum class TriggeredSource {
		Mouse,
		Keyboard,
	};
	void setChildShown(bool shown) {
		_childShown = shown;
	}
	void setShowSource(TriggeredSource source);
	void setForceWidth(int forceWidth);

	using Actions = QList<QAction*>;
	Actions &actions();

	void setResizedCallback(base::lambda<void()> callback) {
		_resizedCallback = std::move(callback);
	}

	void setActivatedCallback(base::lambda<void(QAction *action, int actionTop, TriggeredSource source)> callback) {
		_activatedCallback = std::move(callback);
	}
	void setTriggeredCallback(base::lambda<void(QAction *action, int actionTop, TriggeredSource source)> callback) {
		_triggeredCallback = std::move(callback);
	}

	void setKeyPressDelegate(base::lambda<bool(int key)> delegate) {
		_keyPressDelegate = std::move(delegate);
	}
	void handleKeyPress(int key);

	void setMouseMoveDelegate(base::lambda<void(QPoint globalPosition)> delegate) {
		_mouseMoveDelegate = std::move(delegate);
	}
	void handleMouseMove(QPoint globalPosition);

	void setMousePressDelegate(base::lambda<void(QPoint globalPosition)> delegate) {
		_mousePressDelegate = std::move(delegate);
	}
	void handleMousePress(QPoint globalPosition);

	void setMouseReleaseDelegate(base::lambda<void(QPoint globalPosition)> delegate) {
		_mouseReleaseDelegate = std::move(delegate);
	}
	void handleMouseRelease(QPoint globalPosition);

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private slots:
	void actionChanged();

private:
	struct ActionData {
		ActionData() = default;
		ActionData(const ActionData &other) = delete;
		ActionData &operator=(const ActionData &other) = delete;
		ActionData(ActionData &&other) = default;
		ActionData &operator=(ActionData &&other) = default;
		~ActionData();

		bool hasSubmenu = false;
		QString text;
		QString shortcut;
		const style::icon *icon = nullptr;
		const style::icon *iconOver = nullptr;
		std::unique_ptr<RippleAnimation> ripple;
		std::unique_ptr<ToggleView> toggle;
	};

	void updateSelected(QPoint globalPosition);
	void init();

	// Returns the new width.
	int processAction(QAction *action, int index, int width);
	QAction *addAction(QAction *a, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);

	void setSelected(int selected);
	void setPressed(int pressed);
	void clearMouseSelection();

	int itemTop(int index);
	void updateItem(int index);
	void updateSelectedItem();
	void itemPressed(TriggeredSource source);
	void itemReleased(TriggeredSource source);

	const style::Menu &_st;

	base::lambda<void()> _resizedCallback;
	base::lambda<void(QAction *action, int actionTop, TriggeredSource source)> _activatedCallback;
	base::lambda<void(QAction *action, int actionTop, TriggeredSource source)> _triggeredCallback;
	base::lambda<bool(int key)> _keyPressDelegate;
	base::lambda<void(QPoint globalPosition)> _mouseMoveDelegate;
	base::lambda<void(QPoint globalPosition)> _mousePressDelegate;
	base::lambda<void(QPoint globalPosition)> _mouseReleaseDelegate;

	QMenu *_wappedMenu = nullptr;
	Actions _actions;
	std::vector<ActionData> _actionsData;

	int _forceWidth = 0;
	int _itemHeight, _separatorHeight;

	bool _mouseSelection = false;

	int _selected = -1;
	int _pressed = -1;
	bool _childShown = false;

};

} // namespace Ui
