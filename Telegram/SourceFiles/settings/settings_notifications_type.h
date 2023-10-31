/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"
#include "data/notify/data_notify_settings.h"

namespace Data {
enum class DefaultNotify;
} // namespace Data

namespace Settings {

class NotificationsType : public AbstractSection {
public:
	NotificationsType(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Data::DefaultNotify type);

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] static Type Id(Data::DefaultNotify type);

	[[nodiscard]] Type id() const final override {
		return Id(_type);
	}

private:
	void setupContent(not_null<Window::SessionController*> controller);

	const Data::DefaultNotify _type;

};

[[nodiscard]] bool NotificationsEnabledForType(
	not_null<Main::Session*> session,
	Data::DefaultNotify type);

[[nodiscard]] rpl::producer<bool> NotificationsEnabledForTypeValue(
	not_null<Main::Session*> session,
	Data::DefaultNotify type);

} // namespace Settings
