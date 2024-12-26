/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace style {
struct FlatLabel;
} // namespace style

namespace Main {
class Session;
} // namespace Main

namespace Payments {
struct CreditsFormData;
} // namespace Payments

namespace Ui {

class RpWidget;
class GenericBox;
class FlatLabel;

void SendCreditsBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Payments::CreditsFormData> data,
	Fn<void()> sent);

[[nodiscard]] TextWithEntities CreditsEmoji(
	not_null<Main::Session*> session);

[[nodiscard]] TextWithEntities CreditsEmojiSmall(
	not_null<Main::Session*> session);

not_null<FlatLabel*> SetButtonMarkedLabel(
	not_null<RpWidget*> button,
	rpl::producer<TextWithEntities> text,
	Fn<std::any(Fn<void()> update)> context,
	const style::FlatLabel &st,
	const style::color *textFg = nullptr);

not_null<FlatLabel*> SetButtonMarkedLabel(
	not_null<RpWidget*> button,
	rpl::producer<TextWithEntities> text,
	not_null<Main::Session*> session,
	const style::FlatLabel &st,
	const style::color *textFg = nullptr);

void SendStarGift(
	not_null<Main::Session*> session,
	std::shared_ptr<Payments::CreditsFormData> data,
	Fn<void(std::optional<QString>)> done);

} // namespace Ui
