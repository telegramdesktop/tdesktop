/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

namespace Main {
class Session;
} // namespace Main

void ChannelsLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);
void PublicLinksLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);
void FilterChatsLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);
void FiltersLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);
void FilterPinsLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);
void FolderPinsLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);
void PinsLimitBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);

[[nodiscard]] int AppConfigLimit(
	not_null<Main::Session*> session,
	const QString &key,
	int fallback);
[[nodiscard]] int CurrentPremiumLimit(
	not_null<Main::Session*> session,
	const QString &keyDefault,
	int limitDefault,
	const QString &keyPremium,
	int limitPremium);
