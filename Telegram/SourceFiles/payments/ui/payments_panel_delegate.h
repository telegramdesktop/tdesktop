/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

class QJsonDocument;
class QString;

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Payments::Ui {

using namespace ::Ui;

struct RequestedInformation;
struct UncheckedCardDetails;

class PanelDelegate {
public:
	virtual void panelRequestClose() = 0;
	virtual void panelCloseSure() = 0;
	virtual void panelSubmit() = 0;
	virtual void panelTrustAndSubmit() = 0;
	virtual void panelWebviewMessage(
		const QJsonDocument &message,
		bool saveInformation) = 0;
	virtual bool panelWebviewNavigationAttempt(const QString &uri) = 0;
	virtual void panelSetPassword() = 0;
	virtual void panelOpenUrl(const QString &url) = 0;

	virtual void panelCancelEdit() = 0;
	virtual void panelEditPaymentMethod() = 0;
	virtual void panelEditShippingInformation() = 0;
	virtual void panelEditName() = 0;
	virtual void panelEditEmail() = 0;
	virtual void panelEditPhone() = 0;
	virtual void panelChooseShippingOption() = 0;
	virtual void panelChangeShippingOption(const QString &id) = 0;
	virtual void panelChooseTips() = 0;
	virtual void panelChangeTips(int64 value) = 0;

	virtual void panelValidateInformation(RequestedInformation data) = 0;
	virtual void panelValidateCard(
		Ui::UncheckedCardDetails data,
		bool saveInformation) = 0;
	virtual void panelShowBox(object_ptr<BoxContent> box) = 0;

	virtual QString panelWebviewDataPath() = 0;
};

} // namespace Payments::Ui
