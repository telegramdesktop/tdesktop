/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common_session.h"
#include "api/api_user_privacy.h"

class EditPrivacyController;

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Settings {

void SetupSensitiveContent(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<> updateTrigger);

int ExceptionUsersCount(const std::vector<not_null<PeerData*>> &exceptions);

bool CheckEditCloudPassword(not_null<::Main::Session*> session);
object_ptr<Ui::BoxContent> EditCloudPasswordBox(
	not_null<::Main::Session*> session);
void RemoveCloudPassword(not_null<Window::SessionController*> session);
object_ptr<Ui::BoxContent> CloudPasswordAppOutdatedBox();

not_null<Ui::SettingsButton*> AddPrivacyButton(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> label,
	IconDescriptor &&descriptor,
	Api::UserPrivacy::Key key,
	Fn<std::unique_ptr<EditPrivacyController>()> controllerFactory,
	const style::SettingsButton *stOverride = nullptr);

void SetupArchiveAndMute(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);

class PrivacySecurity : public Section<PrivacySecurity> {
public:
	PrivacySecurity(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

};

} // namespace Settings
