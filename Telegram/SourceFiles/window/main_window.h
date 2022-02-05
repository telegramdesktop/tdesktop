/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/rp_window.h"
#include "base/timer.h"
#include "base/object_ptr.h"
#include "core/core_settings.h"
#include "base/required.h"

#include <QtWidgets/QSystemTrayIcon>

namespace Main {
class Session;
class Account;
} // namespace Main

namespace Ui {
class BoxContent;
class PlainShadow;
} // namespace Ui

namespace Core {
struct WindowPosition;
enum class QuitReason;
} // namespace Core

namespace Window {

class Controller;
class SessionController;
class TitleWidget;
struct TermsLock;

[[nodiscard]] const QImage &Logo();
[[nodiscard]] const QImage &LogoNoMargin();
[[nodiscard]] QIcon CreateIcon(
	Main::Session *session = nullptr,
	bool returnNullIfDefault = false);
void ConvertIconToBlack(QImage &image);

struct CounterLayerArgs {
	template <typename T>
	using required = base::required<T>;

	required<int> size = 16;
	required<int> count = 1;
	required<style::color> bg;
	required<style::color> fg;
};

[[nodiscard]] QImage GenerateCounterLayer(CounterLayerArgs &&args);
[[nodiscard]] QImage WithSmallCounter(QImage image, CounterLayerArgs &&args);

class MainWindow : public Ui::RpWindow {
public:
	explicit MainWindow(not_null<Controller*> controller);
	virtual ~MainWindow();

	[[nodiscard]] Window::Controller &controller() const {
		return *_controller;
	}
	[[nodiscard]] PeerData *singlePeer() const;
	[[nodiscard]] bool isPrimary() const;
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

	void launchDrag(std::unique_ptr<QMimeData> data, Fn<void()> &&callback);

	rpl::producer<> leaveEvents() const;

	virtual void updateWindowIcon();

	void clearWidgets();

	int computeMinWidth() const;
	int computeMinHeight() const;

	void recountGeometryConstraints();
	virtual void updateControlsGeometry();

	bool minimizeToTray();
	void updateGlobalMenu() {
		updateGlobalMenuHook();
	}

	[[nodiscard]] virtual bool preventsQuit(Core::QuitReason reason) {
		return false;
	}

protected:
	void leaveEventHook(QEvent *e) override;

	void savePosition(Qt::WindowState state = Qt::WindowActive);
	void handleStateChanged(Qt::WindowState state);
	void handleActiveChanged();
	void handleVisibleChanged(bool visible);

	virtual void initHook() {
	}

	virtual void activeChangedHook() {
	}

	virtual void handleActiveChangedHook() {
	}

	virtual void handleVisibleChangedHook(bool visible) {
	}

	virtual void clearWidgetsHook() {
	}

	virtual void stateChangedHook(Qt::WindowState state) {
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

	virtual void workmodeUpdated(Core::Settings::WorkMode mode) {
	}

	virtual void createGlobalMenu() {
	}

	virtual bool initGeometryFromSystem() {
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
	void updatePalette();

	[[nodiscard]] Core::WindowPosition positionFromSettings() const;
	[[nodiscard]] QRect countInitialGeometry(Core::WindowPosition position);
	void initGeometry();

	bool computeIsActive() const;

	not_null<Window::Controller*> _controller;

	base::Timer _positionUpdatedTimer;
	bool _positionInited = false;

	object_ptr<Ui::PlainShadow> _titleShadow = { nullptr };
	object_ptr<Ui::RpWidget> _outdated;
	object_ptr<Ui::RpWidget> _body;
	object_ptr<TWidget> _rightColumn = { nullptr };

	QIcon _icon;
	bool _usingSupportIcon = false;

	bool _isActive = false;

	rpl::event_stream<> _leaveEvents;

	bool _maximizedBeforeHide = false;

	mutable QRect _monitorRect;
	mutable crl::time _monitorLastGot = 0;

};

} // namespace Window
