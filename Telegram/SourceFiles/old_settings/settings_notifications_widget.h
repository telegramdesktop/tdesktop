/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "old_settings/settings_block_widget.h"

namespace OldSettings {

class NotificationsWidget : public BlockWidget {
	Q_OBJECT

public:
	NotificationsWidget(QWidget *parent, UserData *self);

private slots:
	void onDesktopNotifications();
	void onShowSenderName();
	void onShowMessagePreview();
	void onNativeNotifications();
	void onPlaySound();
	void onIncludeMuted();
	void onAdvanced();

private:
	void createControls();
	void createNotificationsControls();
	void desktopEnabledUpdated();
	void viewParamUpdated();

	Ui::Checkbox *_desktopNotifications = nullptr;
	Ui::SlideWrap<Ui::Checkbox> *_showSenderName = nullptr;
	Ui::SlideWrap<Ui::Checkbox> *_showMessagePreview = nullptr;
	Ui::Checkbox *_nativeNotifications = nullptr;
	Ui::Checkbox *_playSound = nullptr;
	Ui::Checkbox *_includeMuted = nullptr;
	Ui::SlideWrap<Ui::LinkButton> *_advanced = nullptr;

};

} // namespace Settings
