/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_save_audio_action.h"

#include "base/call_delayed.h"
#include "data/data_document.h"
#include "data/data_file_click_handler.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_multiline_action.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace HistoryView {

void AddSaveAudioAction(
		const Ui::Menu::MenuCallback &addAction,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document,
		not_null<Window::SessionController*> controller) {
	const auto contextId = item->fullId();
	const auto fromSaved = item->history()->peer->isSelf();
	const auto savedMusic = &document->owner().savedMusic();
	const auto show = controller->uiShow();
	const auto inProfile = savedMusic->has(document);
	const auto &ripple = st::defaultDropdownMenu.menu.ripple;
	const auto duration = ripple.hideDuration;
	const auto saveAs = base::fn_delayed(duration, controller, [=] {
		DocumentSaveClickHandler::SaveAndTrack(
			contextId,
			document,
			DocumentSaveClickHandler::Mode::ToNewFile);
	});
	if (!document->isMusicForProfile() || (fromSaved && inProfile)) {
		addAction(
			tr::lng_context_save_audio_file(tr::now),
			saveAs,
			&st::menuIconDownload);
		return;
	}
	const auto fill = [&](not_null<Ui::PopupMenu*> menu) {
		if (!inProfile) {
			const auto saved = [=] {
				savedMusic->save(document);
				show->showToast(tr::lng_saved_music_added(tr::now));
			};
			menu->addAction(
				tr::lng_context_save_music_profile(tr::now),
				saved,
				&st::menuIconProfile);
		}
		if (!fromSaved) {
			menu->addAction(
				tr::lng_context_save_music_saved(tr::now),
				[=] { Window::ForwardToSelf(show, { { contextId } }); },
				&st::menuIconSavedMessages);
		}
		menu->addAction(
			tr::lng_context_save_music_folder(tr::now),
			saveAs,
			&st::menuIconDownload);

		menu->addSeparator(&st::expandedMenuSeparator);

		auto item = base::make_unique_q<Ui::Menu::MultilineAction>(
			menu,
			st::saveMusicInfoMenu,
			st::historyHasCustomEmoji,
			QPoint(
				st::saveMusicInfoMenu.itemPadding.left(),
				st::saveMusicInfoMenu.itemPadding.top()),
			TextWithEntities{ tr::lng_context_save_music_about(tr::now) });
		item->setAttribute(Qt::WA_TransparentForMouseEvents);

		item->setPointerCursor(false);
		menu->addAction(std::move(item));
	};
	addAction(Ui::Menu::MenuCallback::Args{
		.text = tr::lng_context_save_music_to(tr::now),
		.handler = nullptr,
		.icon = &st::menuIconSoundAdd,
		.fillSubmenu = fill,
		.submenuSt = &st::popupMenuWithIcons,
	});
}

} // namespace HistoryView
