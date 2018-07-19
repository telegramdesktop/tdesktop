/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/style_widgets.h"
#include "ui/rp_widget.h"
#include "ui/widgets/menu.h"
#include "ui/effects/panel_animation.h"

namespace Ui {

class PopupMenu : public Ui::RpWidget, private base::Subscriber {
public:
	PopupMenu(QWidget *parent, const style::PopupMenu &st = st::defaultPopupMenu);
	PopupMenu(QWidget *parent, QMenu *menu, const style::PopupMenu &st = st::defaultPopupMenu);

	QAction *addAction(const QString &text, const QObject *receiver, const char* member, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	QAction *addAction(const QString &text, Fn<void()> callback, const style::icon *icon = nullptr, const style::icon *iconOver = nullptr);
	QAction *addSeparator();
	void clearActions();

	using Actions = Ui::Menu::Actions;
	Actions &actions();

	void deleteOnHide(bool del);
	void popup(const QPoint &p);
	void hideMenu(bool fast = false);

	void setDestroyedCallback(Fn<void()> callback) {
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

	Fn<void()> _destroyedCallback;

};

} // namespace Ui
