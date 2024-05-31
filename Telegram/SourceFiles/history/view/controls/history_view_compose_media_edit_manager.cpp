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
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "menu/menu_send.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

namespace HistoryView {

MediaEditManager::MediaEditManager() = default;

void MediaEditManager::start(
		not_null<HistoryItem*> item,
		std::optional<bool> spoilered,
		std::optional<bool> invertCaption) {
	const auto media = item->media();
	if (!media) {
		return;
	}
	_item = item;
	_spoilered = spoilered.value_or(media->hasSpoiler());
	_invertCaption = invertCaption.value_or(item->invertMedia());
	_lifetime = item->history()->owner().itemRemoved(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> removed) {
		if (removed == _item) {
			cancel();
		}
	});
}

void MediaEditManager::apply(SendMenu::Action action) {
	using Type = SendMenu::Action::Type;
	if (action.type == Type::CaptionUp) {
		_invertCaption = true;
	} else if (action.type == Type::CaptionDown) {
		_invertCaption = false;
	} else if (action.type == Type::SpoilerOn) {
		_spoilered = true;
	} else if (action.type == Type::SpoilerOff) {
		_spoilered = false;
	}
}

void MediaEditManager::cancel() {
	_menu = nullptr;
	_item = nullptr;
	_lifetime.destroy();
}

void MediaEditManager::showMenu(
		not_null<Ui::RpWidget*> parent,
		Fn<void()> finished,
		bool hasCaptionText) {
	if (!_item) {
		return;
	}
	const auto media = _item->media();
	const auto hasPreview = media && media->hasReplyPreview();
	const auto preview = hasPreview ? media->replyPreview() : nullptr;
	if (!preview || (media && media->webpage())) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto callback = [=](SendMenu::Action action, const auto &) {
		apply(action);
	};
	const auto position = QCursor::pos();
	SendMenu::FillSendMenu(
		_menu.get(),
		nullptr,
		sendMenuDetails(hasCaptionText),
		callback,
		&st::defaultComposeIcons,
		position);
	_menu->popup(position);
}

Image *MediaEditManager::mediaPreview() {
	if (const auto media = _item ? _item->media() : nullptr) {
		if (const auto photo = media->photo()) {
			return photo->getReplyPreview(
				_item->fullId(),
				_item->history()->peer,
				_spoilered);
		} else if (const auto document = media->document()) {
			return document->getReplyPreview(
				_item->fullId(),
				_item->history()->peer,
				_spoilered);
		}
	}
	return nullptr;
}

bool MediaEditManager::spoilered() const {
	return _spoilered;
}

bool MediaEditManager::invertCaption() const {
	return _invertCaption;
}

SendMenu::Details MediaEditManager::sendMenuDetails(
		bool hasCaptionText) const {
	const auto media = _item ? _item->media() : nullptr;
	if (!media) {
		return {};
	}
	const auto editingMedia = media->allowsEditMedia();
	const auto editPhoto = editingMedia ? media->photo() : nullptr;
	const auto editDocument = editingMedia ? media->document() : nullptr;
	const auto canSaveSpoiler = CanBeSpoilered(_item);
	const auto canMoveCaption = media->allowsEditCaption()
		&& hasCaptionText
		&& (editPhoto
			|| (editDocument
				&& (editDocument->isVideoFile() || editDocument->isGifv())));
	return {
		.spoiler = (!canSaveSpoiler
			? SendMenu::SpoilerState::None
			: _spoilered
			? SendMenu::SpoilerState::Enabled
			: SendMenu::SpoilerState::Possible),
		.caption = (!canMoveCaption
			? SendMenu::CaptionState::None
			: _invertCaption
			? SendMenu::CaptionState::Above
			: SendMenu::CaptionState::Below),
	};
}

bool MediaEditManager::CanBeSpoilered(not_null<HistoryItem*> item) {
	const auto media = item ? item->media() : nullptr;
	const auto editingMedia = media && media->allowsEditMedia();
	const auto editPhoto = editingMedia ? media->photo() : nullptr;
	const auto editDocument = editingMedia ? media->document() : nullptr;
	return (editPhoto && !editPhoto->isNull())
		|| (editDocument
			&& (editDocument->isVideoFile() || editDocument->isGifv()));
}

} // namespace HistoryView
