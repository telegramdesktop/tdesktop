/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_specific.h"
#include "platform/platform_main_window.h"
#include "core/single_timer.h"
#include "base/unique_qptr.h"

class MainWidget;
class BoxContent;

namespace Intro {
class Widget;
} // namespace Intro

namespace Local {
class ClearManager;
} // namespace Local

namespace Window {
class LayerWidget;
class LayerStackWidget;
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
} // namespace Ui

class MediaPreviewWidget;

class MainWindow : public Platform::MainWindow {
	Q_OBJECT

public:
	MainWindow();
	~MainWindow();

	void firstShow();

	void setupPasscodeLock();
	void clearPasscodeLock();
	void setupIntro();
	void setupMain();

	MainWidget *chatsWidget() {
		return mainWidget();
	}

	MainWidget *mainWidget();

	bool doWeReadServerHistory();
	bool doWeReadMentions();

	void activate();

	void noIntro(Intro::Widget *was);
	bool takeThirdSectionFromLayer();

	void checkHistoryActivation();

	void fixOrder();

	enum TempDirState {
		TempDirRemoving,
		TempDirExists,
		TempDirEmpty,
	};
	TempDirState tempDirState();
	TempDirState localStorageState();
	void tempDirDelete(int task);

	void sendPaths();

	QImage iconWithCounter(int size, int count, style::color bg, style::color fg, bool smallIcon) override;

	bool contentOverlapped(const QRect &globalRect);
	bool contentOverlapped(QWidget *w, QPaintEvent *e) {
		return contentOverlapped(QRect(w->mapToGlobal(e->rect().topLeft()), e->rect().size()));
	}
	bool contentOverlapped(QWidget *w, const QRegion &r) {
		return contentOverlapped(QRect(w->mapToGlobal(r.boundingRect().topLeft()), r.boundingRect().size()));
	}

	void showMainMenu();
	void updateTrayMenu(bool force = false) override;

	void showSpecialLayer(
		object_ptr<Window::LayerWidget> layer,
		anim::type animated);
	bool showSectionInExistingLayer(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params);
	void ui_showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated);
	void ui_hideSettingsAndLayer(anim::type animated);
	void ui_removeLayerBlackout();
	bool ui_isLayerShown();
	void ui_showMediaPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document);
	void ui_showMediaPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo);

protected:
	bool eventFilter(QObject *o, QEvent *e) override;
	void closeEvent(QCloseEvent *e) override;

	void initHook() override;
	void updateIsActiveHook() override;
	void clearWidgetsHook() override;

	void updateControlsGeometry() override;

public slots:
	void showSettings();
	void setInnerFocus();

	void quitFromTray();
	void showFromTray(QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Unknown);
	void toggleTray(QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Unknown);
	void toggleDisplayNotifyFromTray();

	void onClearFinished(int task, void *manager);
	void onClearFailed(int task, void *manager);

	void onShowAddContact();
	void onShowNewGroup();
	void onShowNewChannel();
	void onLogout();

signals:
	void tempDirCleared(int task);
	void tempDirClearFailed(int task);

private:
	[[nodiscard]] bool skipTrayClick() const;

	void hideMediaPreview();
	void ensureLayerCreated();
	void destroyLayer();

	void themeUpdated(const Window::Theme::BackgroundUpdate &data);

	QPixmap grabInner();

	void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) override;
	QImage icon16, icon32, icon64, iconbig16, iconbig32, iconbig64;

	TimeMs _lastTrayClickTime = 0;

	object_ptr<Window::PasscodeLockWidget> _passcodeLock = { nullptr };
	object_ptr<Intro::Widget> _intro = { nullptr };
	object_ptr<MainWidget> _main = { nullptr };
	base::unique_qptr<Window::LayerStackWidget> _layer;
	object_ptr<MediaPreviewWidget> _mediaPreview = { nullptr };

	object_ptr<Window::Theme::WarningWidget> _testingThemeWarning = { nullptr };

	Local::ClearManager *_clearManager = nullptr;

};
