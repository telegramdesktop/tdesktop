/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_bot_info_draft.h"

class UserData;

namespace Ui {
class GenericBox;
} // namespace Ui

struct EditBotInfoBoxArgs {
	not_null<UserData*> bot;
	QString langCode;
	bool onlyIntro = false;
	bool applyOnSave = false;
};

[[nodiscard]] QString BotInfoLanguageName(const QString &langCode);

void EditBotInfoBox(not_null<Ui::GenericBox*> box, EditBotInfoBoxArgs &&args);
