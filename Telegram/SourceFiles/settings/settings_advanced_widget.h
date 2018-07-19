/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_block_widget.h"
#include "settings/settings_chat_settings_widget.h"

namespace Settings {

class AdvancedWidget : public BlockWidget, public RPCSender {
	Q_OBJECT

public:
	AdvancedWidget(QWidget *parent, UserData *self);

private slots:
	void onManageLocalStorage();
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	void onConnectionType();
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	void onAskQuestion();
	void onAskQuestionSure();
	void onUseDefaultTheme();
	void onToggleNightTheme();
	void onTelegramFAQ();
	void onLogOut();

private:
	void createControls();
	void checkNonDefaultTheme();
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	void connectionTypeUpdated();
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	void supportGot(const MTPhelp_Support &support);
	QString getNightThemeToggleText() const;

	Ui::LinkButton *_manageLocalStorage = nullptr;
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	LabeledLink *_connectionType = nullptr;
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	Ui::SlideWrap<Ui::LinkButton> *_useDefaultTheme = nullptr;
	Ui::LinkButton *_toggleNightTheme = nullptr;
	Ui::LinkButton *_askQuestion = nullptr;
	Ui::LinkButton *_telegramFAQ = nullptr;
	Ui::LinkButton *_logOut = nullptr;

	mtpRequestId _supportGetRequest = 0;

};

} // namespace Settings
