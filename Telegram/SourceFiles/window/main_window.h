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

class BoxContent;
class MediaView;

namespace Window {

class Controller;
class TitleWidget;
struct TermsLock;

QImage LoadLogo();
QImage LoadLogoNoMargin();
QIcon CreateIcon();

class MainWindow : public Ui::RpWidget, protected base::Subscriber {
	Q_OBJECT

public:
	MainWindow();

	Window::Controller *controller() const {
		return _controller.get();
	}
	void setInactivePress(bool inactive);
	bool wasInactivePress() const {
		return _wasInactivePress;
	}

	bool hideNoQuit();

	void init();
	HitTestResult hitTest(const QPoint &p) const;
	void updateIsActive(int timeout);
	bool isActive() const {
		return _isActive;
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

	virtual void updateTrayMenu(bool force = false) {
	}

	virtual ~MainWindow();

	TWidget *bodyWidget() {
		return _body.data();
	}

	void launchDrag(std::unique_ptr<QMimeData> data);
	base::Observable<void> &dragFinished() {
		return _dragFinished;
	}

	rpl::producer<> leaveEvents() const;

public slots:
	bool minimizeToTray();
	void updateGlobalMenu() {
		updateGlobalMenuHook();
	}

protected:
	void resizeEvent(QResizeEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void savePosition(Qt::WindowState state = Qt::WindowActive);
	void handleStateChanged(Qt::WindowState state);
	void handleActiveChanged();

	virtual void initHook() {
	}

	virtual void updateIsActiveHook() {
	}

	virtual void handleActiveChangedHook() {
	}

	void clearWidgets();
	virtual void clearWidgetsHook() {
	}

	virtual void updateWindowIcon();

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

	virtual bool hasTrayIcon() const {
		return false;
	}
	virtual void showTrayTooltip() {
	}

	virtual void workmodeUpdated(DBIWorkMode mode) {
	}

	virtual void updateControlsGeometry();

	// This one is overriden in Windows for historical reasons.
	virtual int32 screenNameChecksum(const QString &name) const;

	void setPositionInited();

private:
	void checkAuthSession();
	void updatePalette();
	void updateUnreadCounter();
	void initSize();

	bool computeIsActive() const;
	void checkLockByTerms();
	void showTermsDecline();
	void showTermsDelete();

	base::Timer _positionUpdatedTimer;
	bool _positionInited = false;

	std::unique_ptr<Window::Controller> _controller;
	object_ptr<TitleWidget> _title = { nullptr };
	object_ptr<TWidget> _body;
	object_ptr<TWidget> _rightColumn = { nullptr };
	QPointer<BoxContent> _termsBox;

	QIcon _icon;
	QString _titleText;

	bool _isActive = false;
	base::Timer _isActiveTimer;
	bool _wasInactivePress = false;
	base::Timer _inactivePressTimer;

	base::Observable<void> _dragFinished;
	rpl::event_stream<> _leaveEvents;

};

} // namespace Window
