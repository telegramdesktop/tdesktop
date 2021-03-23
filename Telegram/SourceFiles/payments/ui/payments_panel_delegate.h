/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QJsonDocument;
class QString;

namespace Payments::Ui {

class PanelDelegate {
public:
	virtual void panelRequestClose() = 0;
	virtual void panelCloseSure() = 0;
	virtual void panelSubmit() = 0;
	virtual void panelWebviewMessage(const QJsonDocument &message) = 0;
	virtual bool panelWebviewNavigationAttempt(const QString &uri) = 0;
};

} // namespace Payments::Ui
