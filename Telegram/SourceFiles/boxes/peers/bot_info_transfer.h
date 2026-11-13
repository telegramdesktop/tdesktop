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
} // namespace Ui

void ImportBotInfoFromFile(
	not_null<Ui::GenericBox*> box,
	not_null<UserData*> bot);
void ImportBotInfoFromClipboard(
	not_null<Ui::GenericBox*> box,
	not_null<UserData*> bot);
void ExportBotInfoToFile(
	not_null<Ui::GenericBox*> box,
	not_null<UserData*> bot);
void AcceptBotInfoFileDrops(
	not_null<Ui::GenericBox*> box,
	not_null<UserData*> bot);
