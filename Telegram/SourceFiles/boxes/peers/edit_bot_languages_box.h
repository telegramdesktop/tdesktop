/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

namespace Ui {
class GenericBox;
class InputField;
} // namespace Ui

[[nodiscard]] rpl::producer<QString> BotLanguagesStatusValue(
	not_null<UserData*> bot);

void TrackBotInfoField(
	not_null<Ui::InputField*> field,
	rpl::producer<QString> value);

void EditBotLanguagesBox(
	not_null<Ui::GenericBox*> box,
	not_null<UserData*> bot);
