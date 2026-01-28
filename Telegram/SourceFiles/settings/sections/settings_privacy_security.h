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
class GenericBox;
} // namespace Ui

namespace Settings {

[[nodiscard]] Type PrivacySecurityId();

void SetupSensitiveContent(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<> updateTrigger);

int ExceptionUsersCount(const std::vector<not_null<PeerData*>> &exceptions);

bool CheckEditCloudPassword(not_null<::Main::Session*> session);
object_ptr<Ui::BoxContent> EditCloudPasswordBox(
	not_null<::Main::Session*> session);
object_ptr<Ui::BoxContent> ClearPaymentInfoBox(
	not_null<::Main::Session*> session);
void OpenFileConfirmationsBox(not_null<Ui::GenericBox*> box);
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

[[nodiscard]] rpl::producer<QString> PrivacyButtonLabel(
	not_null<::Main::Session*> session,
	Api::UserPrivacy::Key key);

void AddPrivacyPremiumStar(
	not_null<Ui::SettingsButton*> button,
	not_null<::Main::Session*> session,
	rpl::producer<QString> label,
	const QMargins &padding);

void SetupArchiveAndMute(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	HighlightRegistry *highlights = nullptr);

void SetupSecurity(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<> updateTrigger,
	Fn<void(Type)> showOther);

void SetupPrivacy(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<> updateTrigger);

void SetupBotsAndWebsites(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	HighlightRegistry *highlights = nullptr);

void SetupConfirmationExtensions(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);

void SetupTopPeers(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);

void SetupSelfDestruction(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<> updateTrigger);

} // namespace Settings
