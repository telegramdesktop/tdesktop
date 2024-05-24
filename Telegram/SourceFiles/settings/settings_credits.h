/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

class PeerData;

namespace Data {
struct CreditsHistoryEntry;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class GenericBox;
class RpWidget;
} // namespace Ui

namespace Settings {

[[nodiscard]] Type CreditsId();

[[nodiscard]] not_null<Ui::RpWidget*> AddBalanceWidget(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<uint64> balanceValue,
	bool rightAlign);

void ReceiptCreditsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	PeerData *premiumBot,
	const Data::CreditsHistoryEntry &e);

} // namespace Settings

