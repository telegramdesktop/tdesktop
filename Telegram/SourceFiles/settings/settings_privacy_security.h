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

namespace Settings {

int ExceptionUsersCount(const std::vector<not_null<PeerData*>> &exceptions);

bool CheckEditCloudPassword();
object_ptr<BoxContent> EditCloudPasswordBox(not_null<AuthSession*> session);
void RemoveCloudPassword();
object_ptr<BoxContent> CloudPasswordAppOutdatedBox();

void AddPrivacyButton(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> label,
	ApiWrap::Privacy::Key key,
	Fn<std::unique_ptr<EditPrivacyController>()> controller);

class PrivacySecurity : public Section {
public:
	PrivacySecurity(QWidget *parent, not_null<UserData*> self);

private:
	void setupContent();

	not_null<UserData*> _self;

};

} // namespace Settings
