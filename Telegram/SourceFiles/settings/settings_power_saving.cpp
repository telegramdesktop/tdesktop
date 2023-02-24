/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_power_saving.h"

#include "boxes/peers/edit_peer_permissions_box.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/power_saving.h"

namespace Settings {

void PowerSavingBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::lng_settings_power_title());

	auto [checkboxes, getResult, changes] = CreateEditPowerSaving(
		box,
		PowerSaving::kAll & ~PowerSaving::Current());

	box->addRow(std::move(checkboxes), {});

	box->addButton(tr::lng_settings_save(), [=, collect = getResult] {
		Set(PowerSaving::kAll & ~collect());
		Core::App().saveSettingsDelayed();
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

std::vector<NestedPowerSavingLabels> PowerSavingLabelsList() {
	using namespace PowerSaving;
	using Label = PowerSavingLabel;
	auto stickers = std::vector<Label>{
		{ kStickersPanel, tr::lng_settings_power_stickers_panel(tr::now) },
		{ kStickersChat, tr::lng_settings_power_stickers_chat(tr::now) },
	};
	auto emoji = std::vector<Label>{
		{ kEmojiPanel, tr::lng_settings_power_emoji_panel(tr::now) },
		{ kEmojiReactions, tr::lng_settings_power_emoji_reactions(tr::now) },
		{ kEmojiChat, tr::lng_settings_power_emoji_chat(tr::now) },
	};
	auto chat = std::vector<Label>{
		{ kChatBackground, tr::lng_settings_power_chat_background(tr::now) },
		{ kChatSpoiler, tr::lng_settings_power_chat_spoiler(tr::now) },
	};
	auto calls = std::vector<Label>{
		{ kCalls, tr::lng_settings_power_calls(tr::now) },
	};
	auto animations = std::vector<Label>{
		{ kAnimations, tr::lng_settings_power_ui(tr::now) },
	};
	return {
		{ tr::lng_settings_power_stickers(), std::move(stickers) },
		{ tr::lng_settings_power_emoji(), std::move(emoji) },
		{ tr::lng_settings_power_chat(), std::move(chat) },
		{ std::nullopt, std::move(calls) },
		{ std::nullopt, std::move(animations) },
	};
}

} // namespace Settings
