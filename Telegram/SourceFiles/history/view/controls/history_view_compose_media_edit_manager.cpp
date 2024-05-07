/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_media_edit_manager.h"

#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_menu_icons.h"

namespace HistoryView {

MediaEditSpoilerManager::MediaEditSpoilerManager() = default;

void MediaEditSpoilerManager::showMenu(
		not_null<Ui::RpWidget*> parent,
		not_null<HistoryItem*> item,
		Fn<void(bool)> callback) {
	const auto media = item->media();
	const auto hasPreview = media && media->hasReplyPreview();
	const auto preview = hasPreview ? media->replyPreview() : nullptr;
	if (!preview) {
		return;
	}
	const auto spoilered = _spoilerOverride
		? (*_spoilerOverride)
		: (preview && media->hasSpoiler());
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	menu->addAction(
		spoilered
			? tr::lng_context_disable_spoiler(tr::now)
			: tr::lng_context_spoiler_effect(tr::now),
		[=] {
			_spoilerOverride = (!spoilered);
			if (callback) {
				callback(!spoilered);
			}
		},
		spoilered ? &st::menuIconSpoilerOff : &st::menuIconSpoiler);
	menu->popup(QCursor::pos());
}

[[nodiscard]] Image *MediaEditSpoilerManager::mediaPreview(
		not_null<HistoryItem*> item) {
	if (!_spoilerOverride) {
		return nullptr;
	}
	if (const auto media = item->media()) {
		if (const auto photo = media->photo()) {
			return photo->getReplyPreview(
				item->fullId(),
				item->history()->peer,
				*_spoilerOverride);
		} else if (const auto document = media->document()) {
			return document->getReplyPreview(
				item->fullId(),
				item->history()->peer,
				*_spoilerOverride);
		}
	}
	return nullptr;
}

void MediaEditSpoilerManager::setSpoilerOverride(
		std::optional<bool> spoilerOverride) {
	_spoilerOverride = spoilerOverride;
}

std::optional<bool> MediaEditSpoilerManager::spoilerOverride() const {
	return _spoilerOverride;
}

} // namespace HistoryView
