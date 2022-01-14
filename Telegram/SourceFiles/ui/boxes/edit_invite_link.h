/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

namespace Ui {

struct InviteLinkFields {
	QString link;
	QString label;
	TimeId expireDate = 0;
	int usageLimit = 0;
	bool requestApproval = false;
	bool isGroup = false;
	bool isPublic = false;
};

void EditInviteLinkBox(
	not_null<Ui::GenericBox*> box,
	const InviteLinkFields &data,
	Fn<void(InviteLinkFields)> done);

void CreateInviteLinkBox(
	not_null<Ui::GenericBox*> box,
	bool isGroup,
	bool isPublic,
	Fn<void(InviteLinkFields)> done);

} // namespace Ui
