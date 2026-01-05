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

namespace Ui::Toast {
struct Config;
} // namespace Ui::Toast

namespace Ui {

class Show;
class InputField;
class NumberInput;
class VerticalLayout;

struct StarsInputFieldArgs {
	std::optional<int64> value;
	int64 max = 0;
};
[[nodiscard]] not_null<NumberInput*> AddStarsInputField(
	not_null<VerticalLayout*> container,
	StarsInputFieldArgs &&args);

struct TonInputFieldArgs {
	int64 value = 0;
};
[[nodiscard]] not_null<InputField*> AddTonInputField(
	not_null<VerticalLayout*> container,
	TonInputFieldArgs &&args);

void AddApproximateUsd(
	not_null<QWidget*> field,
	not_null<Main::Session*> session,
	rpl::producer<CreditsAmount> price);

void InsufficientTonBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	CreditsAmount required);

struct EmojiGameStakeArgs {
	not_null<Main::Session*> session;
	int64 currentStake = 0;
	std::array<int, 6> milliRewards;
	int jackpotMilliReward = 0;
	Fn<void(int64)> submit;
};
void EmojiGameStakeBox(not_null<GenericBox*> box, EmojiGameStakeArgs &&args);
[[nodiscard]] inline object_ptr<GenericBox> MakeEmojiGameStakeBox(
		EmojiGameStakeArgs &&args) {
	return Box(EmojiGameStakeBox, std::move(args));
}

[[nodiscard]] Toast::Config MakeEmojiGameStakeToast(
	std::shared_ptr<Show> show,
	EmojiGameStakeArgs &&args);

} // namespace Ui
