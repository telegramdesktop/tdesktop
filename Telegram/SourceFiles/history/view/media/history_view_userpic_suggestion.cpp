/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_userpic_suggestion.h"

#include "core/click_handler_types.h" // ClickHandlerContext
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_user.h"
#include "data/data_photo_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_session.h"
#include "editor/photo_editor_layer_widget.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/media/history_view_sticker_player_abstract.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "ui/painter.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "settings/settings_information.h" // UpdatePhotoLocally
#include "styles/style_chat.h"

namespace HistoryView {

UserpicSuggestion::UserpicSuggestion(
	not_null<Element*> parent,
	not_null<PeerData*> chat,
	not_null<PhotoData*> photo,
	int width)
: _photo(parent, chat, photo, width) {
	_photo.initDimensions();
	_photo.resizeGetHeight(_photo.maxWidth());
}

UserpicSuggestion::~UserpicSuggestion() = default;

int UserpicSuggestion::top() {
	return st::msgServiceGiftBoxButtonMargins.top();
}

QSize UserpicSuggestion::size() {
	return { _photo.maxWidth(), _photo.minHeight() };
}

QString UserpicSuggestion::title() {
	return tr::lng_action_suggested_photo_title(tr::now);
}

QString UserpicSuggestion::subtitle() {
	return _photo.parent()->data()->notificationText().text;
}

ClickHandlerPtr UserpicSuggestion::createViewLink() {
	const auto out = _photo.parent()->data()->out();
	const auto photo = _photo.getPhoto();
	const auto itemId = _photo.parent()->data()->fullId();
	const auto show = crl::guard(&_photo, [=](FullMsgId id) {
		_photo.showPhoto(id);
	});
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			const auto media = photo->activeMediaView();
			if (media->loaded()) {
				if (out) {
					PhotoOpenClickHandler(photo, show, itemId).onClick(context);
				} else {
					const auto original = std::make_shared<QImage>(
						media->image(Data::PhotoSize::Large)->original());
					const auto callback = [=](QImage &&image) {
						using namespace Settings;
						const auto session = &photo->session();
						const auto user = session->user();
						UpdatePhotoLocally(user, image);
						auto &peerPhotos = session->api().peerPhoto();
						if (original->size() == image.size()
							&& original->constBits() == image.constBits()) {
							peerPhotos.updateSelf(photo);
						} else {
							peerPhotos.upload(user, std::move(image));
						}
						controller->showSettings(Information::Id());
					};
					Editor::PrepareProfilePhoto(
						controller->content(),
						&controller->window(),
						ImageRoundRadius::Ellipse,
						callback,
						base::duplicate(*original));
				}
			} else if (!photo->loading()) {
				PhotoSaveClickHandler(photo, itemId).onClick(context);
			}
		}
	});
}

void UserpicSuggestion::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) {
	p.translate(geometry.topLeft());
	_photo.draw(p, context);
	p.translate(-geometry.topLeft());
}

void UserpicSuggestion::stickerClearLoopPlayed() {
}

std::unique_ptr<StickerPlayer> UserpicSuggestion::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return nullptr;
}

bool UserpicSuggestion::hasHeavyPart() {
	return _photo.hasHeavyPart();
}

void UserpicSuggestion::unloadHeavyPart() {
	_photo.unloadHeavyPart();
}

} // namespace HistoryView
