/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class FlatLabel;
class GenericBox;
class RoundButton;
class VerticalLayout;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

void ShowGiftOnchainOfferReview(
	not_null<Window::SessionController*> controller,
	FullMsgId itemId);

[[nodiscard]] not_null<Ui::SlideWrap<Ui::FlatLabel>*> AddGiftOnchainFeeLine(
	not_null<Ui::GenericBox*> box,
	rpl::producer<int64> fee);
[[nodiscard]] auto AddGiftOnchainInsufficientBlock(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session)
-> not_null<Ui::SlideWrap<Ui::VerticalLayout>*>;
void AddGiftOnchainBusySpinner(
	not_null<Ui::RoundButton*> button,
	rpl::producer<bool> busy);
