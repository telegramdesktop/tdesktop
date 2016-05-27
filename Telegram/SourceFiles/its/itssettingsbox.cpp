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
//
#include "stdafx.h"
#include "lang.h"

#include "application.h"
#include "itssettingsbox.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "localstorage.h"

ITSSettingsBox::ITSSettingsBox() : AbstractBox(st::boxWidth),
_save(this, lang(lng_settings_save), st::defaultBoxButton),
_cancel(this, lang(lng_cancel), st::cancelBoxButton),
_getBitrix24AccessToken(this, qsl("Get access token"), st::defaultBoxButton),
_getAdminsListFromBitrix(this, qsl("Get admins list from Bitrix"), st::defaultBoxButton),

_bitrix24PortalUrl(this, st::defaultInputField, qsl("bitrix24 portal url")),
_bitrix24ClientId(this, st::defaultInputField, qsl("bitrix24 client id")),
_bitrix24ClientSecret(this, st::defaultInputField, qsl("bitrix24 client secret")),
_bitrix24CallbackPort(this, st::defaultInputField, qsl("bitrix24 callback port")),
_bitrix24DefaultGroupId(this, st::defaultInputField, qsl("bitrix24 default group id")),

_selfAdminsList(this, st::defaultInputField, qsl("admins list")),

_about(st::boxWidth - st::usernamePadding.left()){

	ITSBitrix24& bitrix24 = ITSBitrix24::Instance();
	bitrix24.loadConfigFromLocalStorage();

	_bitrix24PortalUrl.setText(bitrix24.getPortalUrl());
	_bitrix24ClientId.setText(bitrix24.getClientId());
	_bitrix24ClientSecret.setText(bitrix24.getClientSecret());

	if (bitrix24.getPortalCallbackPort() <= 0) {
		_bitrix24CallbackPort.setText("");
	}
	else {
		_bitrix24CallbackPort.setText(QString::number(bitrix24.getPortalCallbackPort()));
	}

	if (bitrix24.getDefaultGroupId() <= 0) {
		_bitrix24DefaultGroupId.setText("");
	}
	else {
		_bitrix24DefaultGroupId.setText(QString::number(bitrix24.getDefaultGroupId()));
	}
		
	setBlueTitle(true);

	textstyleSet(&st::usernameTextStyle);
	
	_about.setRichText(st::boxTextFont, lang(lng_itssettings_about));
	
	resizeMaxHeight(st::boxWidth, 
		st::boxTitleHeight + \
		st::usernamePadding.top() + \
		_bitrix24PortalUrl.height() + \
		_bitrix24ClientId.height() + \
		_bitrix24ClientSecret.height() + \
		_bitrix24CallbackPort.height() + \
		_bitrix24DefaultGroupId.height() + \
		_getBitrix24AccessToken.height() + \
		_selfAdminsList.height() + \
		st::usernameSkip + \
		3 * st::usernameTextStyle.lineHeight + \
		st::usernamePadding.bottom() + \
		st::boxButtonPadding.top() + \
		_save.height() + \
		st::boxButtonPadding.bottom());

	//resizeMaxHeight(st::boxWidth,
	//	st::boxTitleHeight + \
	//	st::usernamePadding.top() + \
	//	_bitrix24PortalUrl.height() + \
	//	_bitrix24ClientId.height() + \
	//	_bitrix24ClientSecret.height() + \
	//	_bitrix24CallbackPort.height() + \
	//	_bitrix24DefaultGroupId.height() + \
	//	_getAdminsListFromBitrix.height() + \
	//	_selfAdminsList.height() + \
	//	st::usernameSkip + \
	//	_about.countHeight(st::boxWidth - st::usernamePadding.left()) + \
	//	3 * st::usernameTextStyle.lineHeight + \
	//	st::usernamePadding.bottom() + \
	//	st::boxButtonPadding.top() + \
	//	_save.height() + \
	//	st::boxButtonPadding.bottom());

	textstyleRestore();
	
	connect(&_getBitrix24AccessToken, SIGNAL(clicked()), this, SLOT(getBitrix24AccessTokenClicked()));
	connect(&_getAdminsListFromBitrix, SIGNAL(clicked()), this, SLOT(onGetAdminsListFromBitrixClicked()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_selfAdminsList, SIGNAL(changed()), this, SLOT(onSelfAdminsListChange()));

	adminsList = cAdminsList();
	QString adminsListString;
	foreach(QString admin, adminsList) {
		adminsListString.append("@" + admin.trimmed() + ", ");
	}
	_selfAdminsList.setText(adminsListString);


	QObject::connect(&bitrix24, SIGNAL(registerBitrix24PortalFinished(bool, QString)), this, SLOT(onGetBitrix24AccessTokenFinished(bool, QString)));
	QObject::connect(&bitrix24, SIGNAL(adminsListFromBitrix24PortalLoaded(bool, QStringList)), this, SLOT(onAdminsListFromBitrix24PortalLoaded(bool, QStringList)));        

	_getAdminsListFromBitrix.hide();

	prepare();
}

void ITSSettingsBox::hideAll() {
	_bitrix24PortalUrl.hide();
	_bitrix24ClientId.hide();
	_bitrix24ClientSecret.hide();
	_bitrix24CallbackPort.hide();
	_bitrix24DefaultGroupId.hide();
	_selfAdminsList.hide();

	_save.hide();
	_cancel.hide();
	_getBitrix24AccessToken.hide();
	//_getAdminsListFromBitrix.hide();

	AbstractBox::hideAll();
}

void ITSSettingsBox::showAll() {
	_bitrix24PortalUrl.show();
	_bitrix24ClientId.show();
	_bitrix24ClientSecret.show();
	_bitrix24CallbackPort.show();
	_bitrix24DefaultGroupId.show();
	_selfAdminsList.show();

	_save.show();
	_cancel.show();
	_getBitrix24AccessToken.show();
	//_getAdminsListFromBitrix.show();

	AbstractBox::showAll();
}

void ITSSettingsBox::showDone() {
	_bitrix24PortalUrl.setFocus();
}

void ITSSettingsBox::paintEvent(QPaintEvent *e) {

	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_itssettings_title));
}



void ITSSettingsBox::resizeEvent(QResizeEvent *e) {

	int32 y = st::boxTitleHeight + st::usernamePadding.top();
	
	_bitrix24PortalUrl.resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _bitrix24PortalUrl.height());
	_bitrix24PortalUrl.moveToLeft(st::usernamePadding.left(), y);
	y += st::usernamePadding.top() + _bitrix24PortalUrl.height();

	_bitrix24ClientId.resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _bitrix24ClientId.height());
	_bitrix24ClientId.moveToLeft(st::usernamePadding.left(), y);
	y += st::usernamePadding.top() + _bitrix24ClientId.height();

	_bitrix24ClientSecret.resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _bitrix24ClientSecret.height());
	_bitrix24ClientSecret.moveToLeft(st::usernamePadding.left(), y);
	y += st::usernamePadding.top() + _bitrix24ClientSecret.height();

	_bitrix24CallbackPort.resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _bitrix24CallbackPort.height());
	_bitrix24CallbackPort.moveToLeft(st::usernamePadding.left(), y);
	y += st::usernamePadding.top() + _bitrix24CallbackPort.height();

	_bitrix24DefaultGroupId.resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _bitrix24DefaultGroupId.height());
	_bitrix24DefaultGroupId.moveToLeft(st::usernamePadding.left(), y);
	y += st::usernamePadding.top() + _bitrix24DefaultGroupId.height();

	_getBitrix24AccessToken.resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _getBitrix24AccessToken.height());
	_getBitrix24AccessToken.moveToLeft(st::usernamePadding.left(), y);
	y += st::usernamePadding.top() + _getBitrix24AccessToken.height();

	//_getAdminsListFromBitrix.resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _getAdminsListFromBitrix.height());
	//_getAdminsListFromBitrix.moveToLeft(st::usernamePadding.left(), y);
	//y += st::usernamePadding.top() + _getAdminsListFromBitrix.height();

	_selfAdminsList.resize(width() - st::usernamePadding.left() - st::usernamePadding.right(), _selfAdminsList.height());
	_selfAdminsList.moveToLeft(st::usernamePadding.left(), y);
	y += st::usernamePadding.top() + _selfAdminsList.height();

	_save.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _save.width() + st::boxButtonPadding.left(), _save.y());

	AbstractBox::resizeEvent(e);
}

void ITSSettingsBox::onSelfAdminsListChange() {	

	QString adminsListString = _selfAdminsList.text();
	adminsListString = adminsListString.remove("@");
	QStringList adminsListStringSegments = adminsListString.split(",");
	
	adminsList.clear();
	foreach(QString admin, adminsListStringSegments)
	{
		admin = admin.trimmed();
		if (!admin.isEmpty()) {			
			adminsList.push_back(admin);
			adminsListString.append("@" + admin.trimmed() + ", ");
		}	
	}
}

void ITSSettingsBox::getBitrix24AccessTokenClicked() {

	//check
	bool error = false;

	if (_bitrix24PortalUrl.text().isEmpty() || QUrl::fromUserInput(_bitrix24PortalUrl.text()) == QUrl()) {
		_bitrix24PortalUrl.showError();
		error = true;
	}
	else _bitrix24PortalUrl.showNormal();	

	if (_bitrix24DefaultGroupId.text().isEmpty()) {
		_bitrix24DefaultGroupId.showError();
		error = true;
	}
	else _bitrix24DefaultGroupId.showNormal();

	if (_bitrix24ClientSecret.text().isEmpty()) {
		_bitrix24ClientSecret.showError();
		error = true;
	}
	else _bitrix24ClientSecret.showNormal();
		
	bool convertOk = false;
	int bitrix24Port = _bitrix24CallbackPort.text().toInt(&convertOk);
	if (convertOk && bitrix24Port > 0) {
		_bitrix24CallbackPort.showNormal();
	}
	else {
		error = true;
		_bitrix24CallbackPort.showError();
	}

	convertOk = false;
	int bitrix24DefaultGroupId = _bitrix24DefaultGroupId.text().toInt(&convertOk);
	if (convertOk && bitrix24DefaultGroupId > 0) {
		_bitrix24DefaultGroupId.showNormal();
	}
	else {
		error = true;
		_bitrix24DefaultGroupId.showError();
	}		

	if (!error) {
		ITSBitrix24& bitrix24 = ITSBitrix24::Instance();
		bitrix24.setPortalCallbackPort(bitrix24Port);
		bitrix24.setDefaultGroupId(bitrix24DefaultGroupId);
		bitrix24.registerBitrix24Portal(_bitrix24PortalUrl.text(), _bitrix24ClientId.text(), _bitrix24ClientSecret.text());
	}
}

void ITSSettingsBox::onGetAdminsListFromBitrixClicked() {

	ITSBitrix24& bitrix24 = ITSBitrix24::Instance();
	bitrix24.loadAdminsListFromBitrix24Portal();

}

void ITSSettingsBox::onAdminsListFromBitrix24PortalLoaded(bool success, QStringList receivedAdminsList) {

	if (success) {		

		InformBox *box = new InformBox(QString("Get admins list success"));	
		Ui::showLayer(box, KeepOtherLayers);

		adminsList.clear();
		_selfAdminsList.setText("");
		QString adminsListString;
		foreach (QString admin, receivedAdminsList)
		{
			adminsList.push_back(admin.trimmed());
			adminsListString.append("@" + admin.trimmed() + ", ");
		}
		_selfAdminsList.setText(adminsListString);	

	}
	else {
		InformBox *box = new InformBox(QString("Get admins list failed."));
		Ui::showLayer(box, KeepOtherLayers);
	}		
	
}

void ITSSettingsBox::onSave() {

	ITSBitrix24& bitrix24 = ITSBitrix24::Instance();
	QString saveResult = bitrix24.saveConfigToLocalStorage();

	if (saveResult.isEmpty()) {
		cSetAdminsList(adminsList);
		Local::writeUserSettings();
		emit closed();
	}
	else {
		InformBox *box = new InformBox(QString("Save failed. Error: %1").arg(saveResult));
		Ui::showLayer(box, KeepOtherLayers);
	}		
	
}

void ITSSettingsBox::onGetBitrix24AccessTokenFinished(bool success, QString errorDescription) {

	qDebug() << "Register bitrix24 portal " + ((success) ? "success" : "failed. Error: " + errorDescription);

	if (success) {

		ITSBitrix24& bitrix24 = ITSBitrix24::Instance();
		_bitrix24PortalUrl.setText(bitrix24.getPortalUrl());

		InformBox *box = new InformBox(QString("Get token success."));
		Ui::showLayer(box, KeepOtherLayers);
	}
	else {
		InformBox *box = new InformBox(QString("Get token failed."));
		Ui::showLayer(box, KeepOtherLayers);
	}

}

void ITSSettingsBox::onClose() {

	ITSBitrix24& bitrix24 = ITSBitrix24::Instance();
	QObject::disconnect(&bitrix24, SIGNAL(registerBitrix24PortalFinished(bool, QString)), this, SLOT(onGetBitrix24AccessTokenFinished(bool, QString)));
    QObject::disconnect(&bitrix24, SIGNAL(adminsListFromBitrix24PortalLoaded(bool, QStringList)), this, SLOT(onAdminsListFromBitrix24PortalLoaded(bool, QString)));
	emit closed();
}
