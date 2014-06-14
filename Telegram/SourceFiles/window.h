/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "title.h"
#include "pspecific.h"
#include "gui/boxshadow.h"

class TitleWidget;
class IntroWidget;
class MainWidget;
class Settings;
class LayerWidget;
class BackgroundWidget;
class LayeredWidget;

class ConnectingWidget : public QWidget {
	Q_OBJECT

public:

	ConnectingWidget(QWidget *parent, const QString &text, const QString &reconnect);
	void set(const QString &text, const QString &reconnect);
	void paintEvent(QPaintEvent *e);

public slots:

	void onReconnect();

private:

	BoxShadow _shadow;
	QString _text;
	int32 _textWidth;
	LinkButton _reconnect;

};

class TempDirDeleter : public QObject {
	Q_OBJECT
public:
	TempDirDeleter(QThread *thread);

public slots:
	void onStart();

signals:
	void succeed();
	void failed();

};

class Window : public PsMainWindow {
	Q_OBJECT

public:
	Window(QWidget *parent = 0);
	~Window();

	void init();

	bool eventFilter(QObject *obj, QEvent *evt);

	void inactivePress(bool inactive);
	bool inactivePress() const;

	void wStartDrag(QMouseEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);
	void closeEvent(QCloseEvent *e);

	void paintEvent(QPaintEvent *e);

	void resizeEvent(QResizeEvent *e);

	void setupIntro(bool anim);
	void setupMain(bool anim);
	void startMain(const MTPUser &user);
	void getNotifySetting(const MTPInputNotifyPeer &peer, uint32 msWait = 0);

	void mtpStateChanged(int32 dc, int32 state);

	TitleWidget *getTitle();

	HitTestType hitTest(const QPoint &p) const;
	QRect iconRect() const;

	QRect clientRect() const;
	QRect photoRect() const;
	
	IntroWidget *introWidget();
	MainWidget *mainWidget();
	Settings *settingsWidget();

	void showConnecting(const QString &text, const QString &reconnect = QString());
	void hideConnecting();

	void hideSettings(bool fast = false);
	void showPhoto(const PhotoLink *lnk, HistoryItem *item = 0);
	void showPhoto(PhotoData *photo, HistoryItem *item = 0);
//	void showVideo(const VideoOpenLink *lnk, HistoryItem *item = 0);
	PhotoData *photoShown();
	void showLayer(LayeredWidget *w);
	void replaceLayer(LayeredWidget *w);
	void hideLayer();

	bool layerShown();

	bool historyIsActive(int state = -1) const;

	bool getPhotoCoords(PhotoData *photo, int32 &x, int32 &y, int32 &w) const;
	bool getVideoCoords(VideoData *video, int32 &x, int32 &y, int32 &w) const;

	bool minimizeToTray();

	void activate();

	void noIntro(IntroWidget *was);
	void noSettings(Settings *was);
	void noMain(MainWidget *was);
	void noLayer(LayerWidget *was);
	void noBox(BackgroundWidget *was);

	void topWidget(QWidget *w);
	void noTopWidget(QWidget *w);

	void fixOrder();

	enum TempDirState {
		TempDirRemoving,
		TempDirExists,
		TempDirEmpty,
	};
	TempDirState tempDirState();
	void tempDirDelete();

public slots:
	
	void checkHistoryActivation(int state = -1);
    
	void showSettings();
	void layerHidden();
	void updateTitleStatus();
	void quitFromTray();
	void showFromTray(QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Unknown);
	void toggleTray(QSystemTrayIcon::ActivationReason reason = QSystemTrayIcon::Unknown);

	void onInactiveTimer();

	void onTempDirCleared();
	void onTempDirClearFailed();
	
signals:

	void resized(const QSize &size);
	void tempDirCleared();
	void tempDirClearFailed();

protected:

	void setupTrayIcon();

private:
	QWidget *centralwidget;

	TitleWidget *title;
	IntroWidget *intro;
	MainWidget *main;
	Settings *settings;
	LayerWidget *layer;
	BackgroundWidget *layerBG;

	QWidget *_topWidget; // temp hack for CountrySelect
	ConnectingWidget *_connecting;

	TempDirDeleter *_tempDeleter;
	QThread *_tempDeleterThread;

	void clearWidgets();

	QIcon myIcon;

	bool dragging;
	QPoint dragStart;

	bool _inactivePress;
	QTimer _inactiveTimer;

};

#endif // MAINWINDOW_H
