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

#include "boxes/abstractbox.h"
#include "boxes/confirmbox.h"
#include "its/itsbitrix24.h"

class ITSSettingsBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:

	ITSSettingsBox();
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

public slots:

	void onSave();	
	void onClose();
	void getBitrix24AccessTokenClicked();
	void onGetBitrix24AccessTokenFinished(bool success, QString errorDescription);
	void onAdminsListFromBitrix24PortalLoaded(bool success, QStringList receivedAdminsList);
	void onGetAdminsListFromBitrixClicked();
	void onSelfAdminsListChange();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	BoxButton
		_save,
		_cancel,
		_getBitrix24AccessToken,
		_getAdminsListFromBitrix;
	
	MaskedInputField
		_bitrix24PortalUrl,
		_bitrix24ClientId,
		_bitrix24ClientSecret,
		_bitrix24CallbackPort,
		_bitrix24DefaultGroupId,
		_selfAdminsList;

	Text _about;		

	QStringList adminsList;
};
