/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Payments {
struct CreditsFormData;
} // namespace Payments

namespace Ui {

class GenericBox;

void SendCreditsBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Payments::CreditsFormData> data,
	Fn<void()> sent);

[[nodiscard]] TextWithEntities CreditsEmoji(
	not_null<Main::Session*> session);

[[nodiscard]] TextWithEntities CreditsEmojiSmall(
	not_null<Main::Session*> session);

} // namespace Ui
