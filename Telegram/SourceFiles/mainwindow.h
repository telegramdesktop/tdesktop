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

class PasscodeWidget;
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
namespace Theme {
struct BackgroundUpdate;
class WarningWidget;
} // namespace Theme
} // namespace Window

namespace Ui {
class LinkButton;
} // namespace Ui

class ConnectingWidget : public TWidget {
	Q_OBJECT

public:
	ConnectingWidget(QWidget *parent, const QString &text, const QString &reconnect);
	void set(const QString &text, const QString &reconnect);

protected:
	void paintEvent(QPaintEvent *e) override;

public slots:
	void onReconnect();

private:
	QString _text;
	int _textWidth = 0;
	object_ptr<Ui::LinkButton> _reconnect;

};

class MediaPreviewWidget;

class MainWindow : public Platform::MainWindow {
	Q_OBJECT

public:
	MainWindow();
	~MainWindow();

	void firstShow();

	void setupPasscode();
	void clearPasscode();
	void setupIntro();
	void setupMain(const MTPUser *user = nullptr);
	void serviceNotification(const TextWithEntities &message, const MTPMessageMedia &media = MTP_messageMediaEmpty(), int32 date = 0, bool force = false);
	void sendServiceHistoryRequest();
	void showDelayedServiceMsgs();

	void mtpStateChanged(int32 dc, int32 state);

	MainWidget *chatsWidget() {
		return mainWidget();
	}

	MainWidget *mainWidget();
	PasscodeWidget *passcodeWidget();

	bool doWeReadServerHistory();
	bool doWeReadMentions();

	void activate();

	void noIntro(Intro::Widget *was);
	void noLayerStack(Window::LayerStackWidget *was);
	void layerFinishedHide(Window::LayerStackWidget *was);
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
	bool ui_isLayerShown();
	void ui_showMediaPreview(DocumentData *document);
	void ui_showMediaPreview(PhotoData *photo);
	void ui_hideMediaPreview();

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
	void updateConnectingStatus();

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

	void app_activateClickHandler(ClickHandlerPtr handler, Qt::MouseButton button);

signals:
	void tempDirCleared(int task);
	void tempDirClearFailed(int task);
	void checkNewAuthorization();

private:
	void showConnecting(const QString &text, const QString &reconnect = QString());
	void hideConnecting();

	void ensureLayerCreated();
	void destroyLayerDelayed();

	void themeUpdated(const Window::Theme::BackgroundUpdate &data);

	QPixmap grabInner();

	void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) override;
	QImage icon16, icon32, icon64, iconbig16, iconbig32, iconbig64;

	struct DelayedServiceMsg {
		DelayedServiceMsg(const TextWithEntities &message, const MTPMessageMedia &media, int32 date) : message(message), media(media), date(date) {
		}
		TextWithEntities message;
		MTPMessageMedia media;
		int32 date;
	};
	QList<DelayedServiceMsg> _delayedServiceMsgs;
	mtpRequestId _serviceHistoryRequest = 0;

	object_ptr<PasscodeWidget> _passcode = { nullptr };
	object_ptr<Intro::Widget> _intro = { nullptr };
	object_ptr<MainWidget> _main = { nullptr };
	object_ptr<Window::LayerStackWidget> _layerBg = { nullptr };
	object_ptr<MediaPreviewWidget> _mediaPreview = { nullptr };

	object_ptr<ConnectingWidget> _connecting = { nullptr };
	object_ptr<Window::Theme::WarningWidget> _testingThemeWarning = { nullptr };

	Local::ClearManager *_clearManager = nullptr;

};
