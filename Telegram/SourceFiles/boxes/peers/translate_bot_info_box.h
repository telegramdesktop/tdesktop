/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
class BotInfoLocalization;
} // namespace Data

namespace Ui {
class GenericBox;
} // namespace Ui

void TranslateBotInfoBox(
	not_null<Ui::GenericBox*> box,
	not_null<Data::BotInfoLocalization*> localization,
	std::vector<QString> codes,
	Fn<void(int translated)> done);
