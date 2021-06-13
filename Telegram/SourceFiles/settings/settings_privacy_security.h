/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"
#include "apiwrap.h"

class EditPrivacyController;

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Settings {

int ExceptionUsersCount(const std::vector<not_null<PeerData*>> &exceptions);

bool CheckEditCloudPassword(not_null<::Main::Session*> session);
object_ptr<Ui::BoxContent> EditCloudPasswordBox(
	not_null<::Main::Session*> session);
void RemoveCloudPassword(not_null<Window::SessionController*> session);
object_ptr<Ui::BoxContent> CloudPasswordAppOutdatedBox();

void AddPrivacyButton(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> label,
	ApiWrap::Privacy::Key key,
	Fn<std::unique_ptr<EditPrivacyController>()> controllerFactory);

class PrivacySecurity : public Section {
public:
	PrivacySecurity(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
