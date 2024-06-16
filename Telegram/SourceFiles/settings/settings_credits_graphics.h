/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

template <typename Object>
class object_ptr;

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
class VerticalLayout;
} // namespace Ui

namespace Settings {

[[nodiscard]] QImage GenerateStars(int height, int count);

void FillCreditOptions(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container,
	int minCredits,
	Fn<void()> paid);

[[nodiscard]] not_null<Ui::RpWidget*> AddBalanceWidget(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<uint64> balanceValue,
	bool rightAlign);

void ReceiptCreditsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	PeerData *premiumBot,
	const Data::CreditsHistoryEntry &e);

[[nodiscard]] object_ptr<Ui::RpWidget> HistoryEntryPhoto(
	not_null<Ui::RpWidget*> parent,
	not_null<PhotoData*> photo,
	int photoSize);

void SmallBalanceBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	int creditsNeeded,
	UserId botId,
	Fn<void()> paid);

} // namespace Settings

