/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"
#include "api/api_user_privacy.h"

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
	IconDescriptor &&descriptor,
	Api::UserPrivacy::Key key,
	Fn<std::unique_ptr<EditPrivacyController>()> controllerFactory);

class PrivacySecurity : public Section<PrivacySecurity> {
public:
	PrivacySecurity(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] static rpl::producer<QString> Title();

	rpl::producer<Type> sectionShowOther() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

	rpl::event_stream<Type> _showOther;

};

} // namespace Settings
