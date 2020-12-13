/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_specific.h"
#include "platform/platform_main_window.h"
#include "base/unique_qptr.h"
#include "ui/layers/layer_widget.h"
#include "ui/effects/animation_value.h"

class MainWidget;

namespace Intro {
class Widget;
enum class EnterPoint : uchar;
} // namespace Intro

namespace Window {
class MediaPreviewWidget;
class SectionMemento;
struct SectionShow;
class PasscodeLockWidget;
namespace Theme {
struct BackgroundUpdate;
class WarningWidget;
} // namespace Theme
} // namespace Window

namespace Ui {
class LinkButton;
class BoxContent;
class LayerStackWidget;
} // namespace Ui

class MediaPreviewWidget;

class MainWindow : public Platform::MainWindow {
	Q_OBJECT

public:
	explicit MainWindow(not_null<Window::Controller*> controller);
	~MainWindow();

	void finishFirstShow();

	void preventOrInvoke(Fn<void()> callback);

	void setupPasscodeLock();
	void clearPasscodeLock();
	void setupIntro(Intro::EnterPoint point);
	void setupMain();

	MainWidget *sessionContent() const;

	[[nodiscard]] bool doWeMarkAsRead();

	void activate();

	bool takeThirdSectionFromLayer();

	void checkHistoryActivation();

	void sendPaths();

	QImage iconWithCounter(int size, int count, style::color bg, style::color fg, bool smallIcon) override;
	void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) override;

	bool contentOverlapped(const QRect &globalRect);
	bool contentOverlapped(QWidget *w, QPaintEvent *e) {
		return contentOverlapped(QRect(w->mapToGlobal(e->rect().topLeft()), e->rect().size()));
	}
	bool contentOverlapped(QWidget *w, const QRegion &r) {
		return contentOverlapped(QRect(w->mapToGlobal(r.boundingRect().topLeft()), r.boundingRect().size()));
	}

	void showMainMenu();
	void updateTrayMenu(bool force = false) override;
	void fixOrder() override;

	void showSpecialLayer(
		object_ptr<Ui::LayerWidget> layer,
		anim::type animated);
	bool showSectionInExistingLayer(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params);
	void ui_showBox(
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options,
		anim::type animated);
	void ui_hideSettingsAndLayer(anim::type animated);
	void ui_removeLayerBlackout();
	bool ui_isLayerShown();
	bool showMediaPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document);
	bool showMediaPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo);
	void hideMediaPreview();

	void showLogoutConfirmation();

	void updateControlsGeometry() override;

protected:
	bool eventFilter(QObject *o, QEvent *e) override;
	void closeEvent(QCloseEvent *e) override;

	void initHook() override;
	void updateIsActiveHook() override;
	void clearWidgetsHook() override;

public slots:
	void showSettings();
	void setInnerFocus();

	void quitFromTray();
	void showFromTray(QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Unknown);
	void toggleDisplayNotifyFromTray();

	void onShowAddContact();
	void onShowNewGroup();
	void onShowNewChannel();

private:
	[[nodiscard]] bool skipTrayClick() const;

	void createTrayIconMenu();
	void handleTrayIconActication(
		QSystemTrayIcon::ActivationReason reason) override;

	void applyInitialWorkMode();
	void ensureLayerCreated();
	void destroyLayer();

	void themeUpdated(const Window::Theme::BackgroundUpdate &data);

	QPixmap grabInner();

	QImage icon16, icon32, icon64, iconbig16, iconbig32, iconbig64;

	crl::time _lastTrayClickTime = 0;
	QPoint _lastMousePosition;

	object_ptr<Window::PasscodeLockWidget> _passcodeLock = { nullptr };
	object_ptr<Intro::Widget> _intro = { nullptr };
	object_ptr<MainWidget> _main = { nullptr };
	base::unique_qptr<Ui::LayerStackWidget> _layer;
	object_ptr<Window::MediaPreviewWidget> _mediaPreview = { nullptr };

	object_ptr<Window::Theme::WarningWidget> _testingThemeWarning = { nullptr };

};

namespace App {
MainWindow *wnd();
} // namespace App
