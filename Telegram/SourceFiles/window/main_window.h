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
void OverrideApplicationIcon(QImage image);
[[nodiscard]] QIcon CreateIcon(
	Main::Session *session = nullptr,
	bool returnNullIfDefault = false);
void ConvertIconToBlack(QImage &image);

struct CounterLayerArgs {
	template <typename T>
	using required = base::required<T>;

	required<int> size = 16;
	double devicePixelRatio = 1.;
	required<int> count = 1;
	required<style::color> bg;
	required<style::color> fg;
};

extern const char kOptionNewWindowsSizeAsFirst[];

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

	[[nodiscard]] QRect desktopRect() const;
	[[nodiscard]] Core::WindowPosition withScreenInPosition(
		Core::WindowPosition position) const;

	void init();

	void updateIsActive();

	[[nodiscard]] bool isActive() const {
		return !isHidden() && _isActive;
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

	virtual void fixOrder() {
	}
	virtual void setInnerFocus() {
		setFocus();
	}

	Ui::RpWidget *bodyWidget() {
		return _body.data();
	}

	void launchDrag(std::unique_ptr<QMimeData> data, Fn<void()> &&callback);

	[[nodiscard]] rpl::producer<> leaveEvents() const;
	[[nodiscard]] rpl::producer<> imeCompositionStarts() const;

	virtual void updateWindowIcon() = 0;
	void updateTitle();

	void clearWidgets();

	int computeMinWidth() const;
	int computeMinHeight() const;

	void recountGeometryConstraints();
	virtual void updateControlsGeometry();

	void firstShow();
	bool minimizeToTray();
	void updateGlobalMenu() {
		updateGlobalMenuHook();
	}

	[[nodiscard]] QRect countInitialGeometry(
		Core::WindowPosition position,
		Core::WindowPosition initial,
		QSize minSize) const;

	[[nodiscard]] virtual rpl::producer<QPoint> globalForceClicks() {
		return rpl::never<QPoint>();
	}

protected:
	void leaveEventHook(QEvent *e) override;

	void savePosition(Qt::WindowState state = Qt::WindowActive);
	void handleStateChanged(Qt::WindowState state);
	void handleActiveChanged();
	void handleVisibleChanged(bool visible);

	virtual void checkActivation() {
	}
	virtual void initHook() {
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

	virtual void workmodeUpdated(Core::Settings::WorkMode mode) {
	}

	virtual void createGlobalMenu() {
	}

	virtual bool initGeometryFromSystem() {
		return false;
	}

	void imeCompositionStartReceived();
	void setPositionInited();

	virtual QRect computeDesktopRect() const;

private:
	void refreshTitleWidget();
	void updateMinimumSize();
	void updatePalette();

	[[nodiscard]] Core::WindowPosition initialPosition() const;
	[[nodiscard]] Core::WindowPosition nextInitialChildPosition(
		bool primary);
	[[nodiscard]] QRect countInitialGeometry(Core::WindowPosition position);

	bool computeIsActive() const;

	not_null<Window::Controller*> _controller;

	base::Timer _positionUpdatedTimer;
	bool _positionInited = false;

	object_ptr<Ui::PlainShadow> _titleShadow = { nullptr };
	object_ptr<Ui::RpWidget> _outdated;
	object_ptr<Ui::RpWidget> _body;
	object_ptr<TWidget> _rightColumn = { nullptr };

	bool _isActive = false;

	rpl::event_stream<> _leaveEvents;
	rpl::event_stream<> _imeCompositionStartReceived;

	bool _maximizedBeforeHide = false;

	QPoint _lastMyChildCreatePosition;
	int _lastChildIndex = 0;

	mutable QRect _monitorRect;
	mutable crl::time _monitorLastGot = 0;

};

[[nodiscard]] int32 DefaultScreenNameChecksum(const QString &name);

[[nodiscard]] Core::WindowPosition PositionWithScreen(
	Core::WindowPosition position,
	const QScreen *chosen,
	QSize minimal);
[[nodiscard]] Core::WindowPosition PositionWithScreen(
	Core::WindowPosition position,
	not_null<const QWidget*> widget,
	QSize minimal);

} // namespace Window
