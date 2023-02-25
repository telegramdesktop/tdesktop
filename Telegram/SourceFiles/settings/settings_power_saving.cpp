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
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/power_saving.h"
#include "styles/style_menu_icons.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {

void PowerSavingBox(not_null<Ui::GenericBox*> box) {
	box->setStyle(st::layerBox);
	box->setTitle(tr::lng_settings_power_title());
	box->setWidth(st::boxWideWidth);

	const auto container = box->verticalLayout();

	// Force top shadow visibility.
	box->setPinnedToTopContent(
		object_ptr<Ui::FixedHeightWidget>(box, st::lineWidth));

	AddSubsectionTitle(
		container,
		tr::lng_settings_power_subtitle(),
		st::powerSavingSubtitlePadding);

	auto [checkboxes, getResult, changes] = CreateEditPowerSaving(
		box,
		PowerSaving::kAll & ~PowerSaving::Current());

	box->addRow(std::move(checkboxes), {});

	auto automatic = (Ui::SettingsButton*)nullptr;
	const auto hasBattery = true;
	const auto automaticEnabled = true;
	if (hasBattery) {
		AddSkip(container);
		AddDivider(container);
		AddSkip(container);
		AddButton(
			container,
			tr::lng_settings_power_auto(),
			st::powerSavingButtonNoIcon
		)->toggleOn(rpl::single(automaticEnabled));
		AddSkip(container);
		AddDividerText(container, tr::lng_settings_power_auto_about());
	}

	box->addButton(tr::lng_settings_save(), [=, collect = getResult] {
		Set(PowerSaving::kAll & ~collect());
		Core::App().saveSettingsDelayed();
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

EditFlagsDescriptor<PowerSaving::Flags> PowerSavingLabels() {
	using namespace PowerSaving;
	using Label = EditFlagsLabel<Flags>;

	auto stickers = std::vector<Label>{
		{
			kStickersPanel,
			tr::lng_settings_power_stickers_panel(tr::now),
			&st::menuIconStickers,
		},
		{ kStickersChat, tr::lng_settings_power_stickers_chat(tr::now) },
	};
	auto emoji = std::vector<Label>{
		{
			kEmojiPanel,
			tr::lng_settings_power_emoji_panel(tr::now),
			&st::menuIconEmoji,
		},
		{ kEmojiReactions, tr::lng_settings_power_emoji_reactions(tr::now) },
		{ kEmojiChat, tr::lng_settings_power_emoji_chat(tr::now) },
	};
	auto chat = std::vector<Label>{
		{
			kChatBackground,
			tr::lng_settings_power_chat_background(tr::now),
			&st::menuIconChatBubble,
		},
		{ kChatSpoiler, tr::lng_settings_power_chat_spoiler(tr::now) },
	};
	auto calls = std::vector<Label>{
		{
			kCalls,
			tr::lng_settings_power_calls(tr::now),
			&st::menuIconPhone,
		},
	};
	auto animations = std::vector<Label>{
		{
			kAnimations,
			tr::lng_settings_power_ui(tr::now),
			&st::menuIconStartStream,
		},
	};
	return { .labels = {
		{ tr::lng_settings_power_stickers(), std::move(stickers) },
		{ tr::lng_settings_power_emoji(), std::move(emoji) },
		{ tr::lng_settings_power_chat(), std::move(chat) },
		{ std::nullopt, std::move(calls) },
		{ std::nullopt, std::move(animations),  },
	}, .st = &st::powerSavingButton };
}

} // namespace Settings
