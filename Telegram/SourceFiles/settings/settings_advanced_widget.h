/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
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
#endif // TDESKTOP_DISABLE_NETWORK_PROXY
	void onAskQuestion();
	void onAskQuestionSure();
	void onTelegramFAQ();
	void onLogOut();

private:
	void createControls();
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	void connectionTypeUpdated();
#endif // TDESKTOP_DISABLE_NETWORK_PROXY
	void supportGot(const MTPhelp_Support &support);

	ChildWidget<LinkButton> _manageLocalStorage = { nullptr };
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
	ChildWidget<LabeledLink> _connectionType = { nullptr };
#endif // TDESKTOP_DISABLE_NETWORK_PROXY
	ChildWidget<LinkButton> _askQuestion = { nullptr };
	ChildWidget<LinkButton> _telegramFAQ = { nullptr };
	ChildWidget<LinkButton> _logOut = { nullptr };

	mtpRequestId _supportGetRequest = 0;

};

} // namespace Settings
