/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_media_edit_manager.h"

#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView {

MediaEditManager::MediaEditManager()
: _coverUploader([=] { _updateRequests.fire({}); }) {
}

void MediaEditManager::start(
		not_null<HistoryItem*> item,
		std::optional<bool> spoilered,
		std::optional<bool> invertCaption) {
	const auto media = item->media();
	if (!media) {
		cancel();
		return;
	}
	resetCover();
	_item = item;
	_spoilered = spoilered.value_or(media->hasSpoiler());
	_invertCaption = invertCaption.value_or(item->invertMedia());
	_lifetime = item->history()->owner().itemRemoved(
	) | rpl::on_next([=](not_null<const HistoryItem*> removed) {
		if (removed == _item) {
			cancel();
		}
	});
	if (CanEditVideoCover(item) && media->videoCover()) {
		// Prefetch the video's own thumbnail, it was never loaded
		// if the video was always displayed with a cover, and it
		// becomes visible as soon as the cover is removed.
		media->document()->loadThumbnail(item->fullId());
	}
}

void MediaEditManager::apply(
		SendMenu::Action action,
		std::shared_ptr<ChatHelpers::Show> show) {
	using Type = SendMenu::Action::Type;
	if (action.type == Type::CaptionUp) {
		_invertCaption = true;
	} else if (action.type == Type::CaptionDown) {
		_invertCaption = false;
	} else if (action.type == Type::SpoilerOn) {
		_spoilered = true;
	} else if (action.type == Type::SpoilerOff) {
		_spoilered = false;
	} else if (action.type == Type::EditCover) {
		editVideoCover(std::move(show));
		return;
	} else if (action.type == Type::RemoveCover) {
		removeVideoCover();
		return;
	}
	_updateRequests.fire({});
}

void MediaEditManager::cancel() {
	resetCover();
	_menu = nullptr;
	_item = nullptr;
	_spoilered = false;
	_invertCaption = false;
	_lifetime.destroy();
}

void MediaEditManager::showMenu(
		not_null<Ui::RpWidget*> parent,
		Fn<void()> finished,
		bool hasCaptionText,
		std::shared_ptr<ChatHelpers::Show> show) {
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
		apply(action, show);
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
	if (const auto picked = _coverUploader.preview()) {
		return picked;
	}
	if (const auto media = _item ? _item->media() : nullptr) {
		if (const auto photo = media->photo()) {
			return photo->getReplyPreview(
				_item->fullId(),
				_item->history()->peer,
				_spoilered);
		} else if (const auto document = media->document()) {
			const auto cover = _coverCleared
				? nullptr
				: media->videoCover();
			if (cover) {
				return cover->getReplyPreview(
					_item->fullId(),
					_item->history()->peer,
					_spoilered);
			}
			const auto result = document->getReplyPreview(
				_item->fullId(),
				_item->history()->peer,
				_spoilered,
				_coverCleared);
			if (_coverCleared
				&& document->replyPreviewLoaded(_spoilered)) {
				_coverClearedLifetime.destroy();
			}
			return result;
		}
	}
	return nullptr;
}

void MediaEditManager::paintCoverUpload(Painter &p, QRect to) {
	_coverUploader.paintUploading(p, to);
}

bool MediaEditManager::spoilered() const {
	return _spoilered;
}

bool MediaEditManager::invertCaption() const {
	return _invertCaption;
}

Api::VideoCoverEdit MediaEditManager::videoCover() {
	const auto photo = _coverUploader.photo();
	const auto weak = base::make_weak(&_coverUploader);
	return {
		.photo = photo,
		.cleared = _coverCleared && !photo,
		.refresh = [weak](Fn<void(PhotoData*)> done) {
			if (const auto strong = weak.get()) {
				strong->reupload(std::move(done));
			} else {
				done(nullptr);
			}
		},
	};
}

bool MediaEditManager::videoCoverUploading() const {
	return _coverUploader.uploading();
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
	const auto canEditCover = CanEditVideoCover(_item);
	const auto hasCover = (_coverUploader.photo() != nullptr)
		|| _coverUploader.uploading()
		|| (media->videoCover() && !_coverCleared);
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
		.cover = (!canEditCover
			? SendMenu::CoverState::None
			: hasCover
			? SendMenu::CoverState::Has
			: SendMenu::CoverState::Add),
	};
}

rpl::producer<> MediaEditManager::updateRequests() const {
	return _updateRequests.events();
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

bool MediaEditManager::CanEditVideoCover(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = (media && media->allowsEditMedia())
		? media->document()
		: nullptr;
	const auto peer = item->history()->peer;
	return document
		&& document->isVideoFile()
		&& !document->dimensions.isEmpty()
		&& (peer->isBroadcast() || peer->isSelf() || peer->isBot());
}

void MediaEditManager::editVideoCover(
		std::shared_ptr<ChatHelpers::Show> show) {
	if (!_item || !show || !CanEditVideoCover(_item)) {
		return;
	}
	_coverUploader.choose(_item, std::move(show));
}

void MediaEditManager::removeVideoCover() {
	_coverUploader.reset();
	const auto item = _item;
	const auto media = item ? item->media() : nullptr;
	_coverCleared = media && (media->videoCover() != nullptr);
	if (_coverCleared) {
		media->document()->loadThumbnail(item->fullId());
		_coverClearedLifetime.destroy();
		item->history()->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			_updateRequests.fire({});
		}, _coverClearedLifetime);
	}
	_updateRequests.fire({});
}

void MediaEditManager::resetCover() {
	_coverUploader.reset();
	_coverCleared = false;
	_coverClearedLifetime.destroy();
}

} // namespace HistoryView
