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
 Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
 */
#pragma once

#include "styles/style_widgets.h"
#include "ui/widgets/menu.h"
#include "ui/effects/panel_animation.h"

namespace Ui {

class PopupMenu : public TWidget, private base::Subscriber {
public:
	PopupMenu(QWidget*, const style::PopupMenu &st = st::defaultPopupMenu);
	PopupMenu(QWidget*, QMenu *menu, const style::PopupMenu &st = st::defaultPopupMenu);

	QAction *addAction(const QString &text, const QObject *receiver, const char* member, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	QAction *addAction(const QString &text, base::lambda<void()> callback, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	QAction *addSeparator();
	void clearActions();

	using Actions = Ui::Menu::Actions;
	Actions &actions();

	void deleteOnHide(bool del);
	void popup(const QPoint &p);
	void hideMenu(bool fast = false);

	void setDestroyedCallback(base::lambda<void()> callback) {
		_destroyedCallback = std::move(callback);
	}

	~PopupMenu();

protected:
	void paintEvent(QPaintEvent *e) override;
	void focusOutEvent(QFocusEvent *e) override;
	void hideEvent(QHideEvent *e) override;

	void keyPressEvent(QKeyEvent *e) override {
		forwardKeyPress(e->key());
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		forwardMouseMove(e->globalPos());
	}
	void mousePressEvent(QMouseEvent *e) override {
		forwardMousePress(e->globalPos());
	}

private:
	void paintBg(Painter &p);
	void hideFast();
	void setOrigin(PanelAnimation::Origin origin);
	void showAnimated(PanelAnimation::Origin origin);
	void hideAnimated();

	QImage grabForPanelAnimation();
	void startShowAnimation();
	void startOpacityAnimation(bool hiding);
	void prepareCache();
	void childHiding(PopupMenu *child);

	void showAnimationCallback();
	void opacityAnimationCallback();

	void init();

	void hideFinished();
	void showStarted();

	using TriggeredSource = Ui::Menu::TriggeredSource;
	void handleCompositingUpdate();
	void handleMenuResize();
	void handleActivated(QAction *action, int actionTop, TriggeredSource source);
	void handleTriggered(QAction *action, int actionTop, TriggeredSource source);
	void forwardKeyPress(int key);
	bool handleKeyPress(int key);
	void forwardMouseMove(QPoint globalPosition) {
		_menu->handleMouseMove(globalPosition);
	}
	void handleMouseMove(QPoint globalPosition);
	void forwardMousePress(QPoint globalPosition) {
		_menu->handleMousePress(globalPosition);
	}
	void handleMousePress(QPoint globalPosition);
	void forwardMouseRelease(QPoint globalPosition) {
		_menu->handleMouseRelease(globalPosition);
	}
	void handleMouseRelease(QPoint globalPosition);

	using SubmenuPointer = QPointer<PopupMenu>;
	bool popupSubmenuFromAction(QAction *action, int actionTop, TriggeredSource source);
	void popupSubmenu(SubmenuPointer submenu, int actionTop, TriggeredSource source);
	void showMenu(const QPoint &p, PopupMenu *parent, TriggeredSource source);

	const style::PopupMenu &_st;

	object_ptr<Ui::Menu> _menu;

	using Submenus = QMap<QAction*, SubmenuPointer>;
	Submenus _submenus;

	PopupMenu *_parent = nullptr;

	QRect _inner;
	style::margins _padding;

	SubmenuPointer _activeSubmenu;

	PanelAnimation::Origin _origin = PanelAnimation::Origin::TopLeft;
	std::unique_ptr<PanelAnimation> _showAnimation;
	Animation _a_show;

	bool _useTransparency = true;
	bool _hiding = false;
	QPixmap _cache;
	Animation _a_opacity;

	bool _deleteOnHide = true;
	bool _triggering = false;
	bool _deleteLater = false;

	base::lambda<void()> _destroyedCallback;

};

} // namespace Ui
