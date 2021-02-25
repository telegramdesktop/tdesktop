/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/window_title.h"
#include "ui/rp_widget.h"
#include "base/timer.h"
#include "base/object_ptr.h"

#include <QtWidgets/QSystemTrayIcon>

namespace Main {
class Session;
class Account;
} // namespace Main

namespace Ui {
class BoxContent;
class PlainShadow;
} // namespace Ui

namespace Window {

class Controller;
class SessionController;
class TitleWidget;
struct TermsLock;

QImage LoadLogo();
QImage LoadLogoNoMargin();
QIcon CreateIcon(Main::Session *session = nullptr);
void ConvertIconToBlack(QImage &image);

class MainWindow : public Ui::RpWidget, protected base::Subscriber {
public:
	explicit MainWindow(not_null<Controller*> controller);
	virtual ~MainWindow();

	[[nodiscard]] Window::Controller &controller() const {
		return *_controller;
	}
	[[nodiscard]] Main::Account &account() const;
	[[nodiscard]] Window::SessionController *sessionController() const;

	bool hideNoQuit();

	void showFromTray();
	void quitFromTray();
	void activate();
	virtual void showFromTrayMenu() {
		showFromTray();
	}

	[[nodiscard]] QRect desktopRect() const;

	void init();
	[[nodiscard]] HitTestResult hitTest(const QPoint &p) const;

	void updateIsActive();

	[[nodiscard]] bool isActive() const {
		return _isActive;
	}
	[[nodiscard]] virtual bool isActiveForTrayMenu() {
		updateIsActive();
		return isActive();
	}

	bool positionInited() const {
		return _positionInited;
	}
	void positionUpdated();

	bool titleVisible() const;
	void setTitleVisible(bool visible);
	QString titleText() const {
		return _titleText;
	}

	void reActivateWindow();

	void showRightColumn(object_ptr<TWidget> widget);
	int maximalExtendBy() const;
	bool canExtendNoMove(int extendBy) const;

	// Returns how much could the window get extended.
	int tryToExtendWidthBy(int addToWidth);

	virtual void updateTrayMenu() {
	}
	virtual void fixOrder() {
	}
	virtual void setInnerFocus() {
		setFocus();
	}

	Ui::RpWidget *bodyWidget() {
		return _body.data();
	}

	void launchDrag(std::unique_ptr<QMimeData> data);
	base::Observable<void> &dragFinished() {
		return _dragFinished;
	}

	rpl::producer<> leaveEvents() const;

	virtual void updateWindowIcon();

	void clearWidgets();

	QRect inner() const;
	int computeMinWidth() const;
	int computeMinHeight() const;

	void recountGeometryConstraints();
	virtual void updateControlsGeometry();

	bool hasShadow() const;

	bool minimizeToTray();
	void updateGlobalMenu() {
		updateGlobalMenuHook();
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void savePosition(Qt::WindowState state = Qt::WindowActive);
	void handleStateChanged(Qt::WindowState state);
	void handleActiveChanged();
	void handleVisibleChanged(bool visible);

	virtual void initHook() {
	}

	virtual void updateIsActiveHook() {
	}

	virtual void handleActiveChangedHook() {
	}

	virtual void handleVisibleChangedHook(bool visible) {
	}

	virtual void clearWidgetsHook() {
	}

	virtual void stateChangedHook(Qt::WindowState state) {
	}

	virtual void titleVisibilityChangedHook() {
	}

	virtual void unreadCounterChangedHook() {
	}

	virtual void closeWithoutDestroy() {
		hide();
	}

	virtual void updateGlobalMenuHook() {
	}

	virtual void initTrayMenuHook() {
	}
	virtual bool hasTrayIcon() const {
		return false;
	}
	virtual void showTrayTooltip() {
	}

	virtual void workmodeUpdated(DBIWorkMode mode) {
	}

	virtual void createGlobalMenu() {
	}
	virtual void initShadows() {
	}
	virtual void firstShadowsUpdate() {
	}

	virtual bool initSizeFromSystem() {
		return false;
	}

	// This one is overriden in Windows for historical reasons.
	virtual int32 screenNameChecksum(const QString &name) const;

	void setPositionInited();
	void attachToTrayIcon(not_null<QSystemTrayIcon*> icon);
	virtual void handleTrayIconActication(
		QSystemTrayIcon::ActivationReason reason) = 0;
	void updateUnreadCounter();

	virtual QRect computeDesktopRect() const;

private:
	void refreshTitleWidget();
	void updateMinimumSize();
	void updateShadowSize();
	void updatePalette();
	void initSize();

	bool computeIsActive() const;

	not_null<Window::Controller*> _controller;

	base::Timer _positionUpdatedTimer;
	bool _positionInited = false;

	object_ptr<TitleWidget> _title = { nullptr };
	object_ptr<Ui::PlainShadow> _titleShadow = { nullptr };
	object_ptr<Ui::RpWidget> _outdated;
	object_ptr<Ui::RpWidget> _body;
	object_ptr<TWidget> _rightColumn = { nullptr };

	QIcon _icon;
	bool _usingSupportIcon = false;
	QString _titleText;
	style::margins _padding;

	bool _isActive = false;

	base::Observable<void> _dragFinished;
	rpl::event_stream<> _leaveEvents;

	bool _maximizedBeforeHide = false;

	mutable QRect _monitorRect;
	mutable crl::time _monitorLastGot = 0;

};

} // namespace Window
