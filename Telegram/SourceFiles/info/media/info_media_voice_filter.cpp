/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_voice_filter.h"

#include "lang/lang_keys.h"
#include "menu/menu_check_item.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_widgets.h"

namespace Info::Media {
namespace {

using Type = Storage::SharedMediaType;

enum class VoiceFilterFlag : uchar {
	Voice = (1 << 0),
	Round = (1 << 1),
};
inline constexpr bool is_flag_type(VoiceFilterFlag) { return true; }
using VoiceFilterFlags = base::flags<VoiceFilterFlag>;

[[nodiscard]] VoiceFilterFlags ToFlags(Type type) {
	using Flag = VoiceFilterFlag;
	switch (type) {
	case Type::VoiceFile: return Flag::Voice;
	case Type::RoundFile: return Flag::Round;
	}
	return Flag::Voice | Flag::Round;
}

[[nodiscard]] Type FromFlags(VoiceFilterFlags flags) {
	using Flag = VoiceFilterFlag;
	return (flags == Flag::Voice)
		? Type::VoiceFile
		: (flags == Flag::Round)
		? Type::RoundFile
		: Type::RoundVoiceFile;
}

} // namespace

bool HasVoiceFilter(Type sectionType) {
	return (sectionType == Type::RoundVoiceFile);
}

void FillVoiceFilterMenu(
		const Ui::Menu::MenuCallback &addAction,
		Type sourceType,
		Fn<void(Type)> apply) {
	const auto flags = std::make_shared<VoiceFilterFlags>(
		ToFlags(sourceType));
	const auto add = [=](const QString &text, VoiceFilterFlag flag) {
		addAction({
			.make = [=](not_null<Ui::PopupMenu*> menu) {
				auto item = base::make_unique_q<Menu::ItemWithCheck>(
					menu->menu(),
					menu->st().menu,
					Ui::Menu::CreateAction(menu->menu().get(), text, [] {}),
					nullptr,
					nullptr);
				item->init((*flags & flag) != 0);
				item->setChangeAllowed([=](bool checked) {
					return checked || ((*flags & ~flag) != 0);
				});
				item->checkView()->checkedChanges(
				) | rpl::on_next([=](bool checked) {
					const auto changed = checked
						? (*flags | flag)
						: (*flags & ~flag);
					if (changed != *flags) {
						*flags = changed;
						apply(FromFlags(changed));
					}
				}, item->lifetime());
				return item;
			}
		});
	};
	add(
		tr::lng_send_text_type_voice_messages(tr::now),
		VoiceFilterFlag::Voice);
	add(
		tr::lng_send_text_type_video_messages(tr::now),
		VoiceFilterFlag::Round);
}

} // namespace Info::Media
