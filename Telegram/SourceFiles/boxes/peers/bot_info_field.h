/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_bot_info_draft.h"

namespace Ui {
class InputField;
class RpWidget;
class VerticalLayout;
} // namespace Ui

[[nodiscard]] rpl::producer<QString> BotInfoFieldTitle(
	Data::BotInfoField field);
[[nodiscard]] not_null<Ui::InputField*> AddBotInfoField(
	not_null<Ui::VerticalLayout*> container,
	Data::BotInfoField field,
	rpl::producer<QString> placeholder);
void PlaceBotInfoFieldButton(
	not_null<Ui::InputField*> field,
	not_null<Ui::RpWidget*> button,
	int index);
void SetBotInfoFieldButtonsCount(not_null<Ui::InputField*> field, int count);
void TrackBotInfoField(
	not_null<Ui::InputField*> field,
	rpl::producer<QString> value);
