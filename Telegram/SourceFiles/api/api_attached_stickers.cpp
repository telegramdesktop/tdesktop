/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_attached_stickers.h"

#include "apiwrap.h"
#include "boxes/confirm_box.h"
#include "boxes/sticker_set_box.h"
#include "boxes/stickers_box.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "lang/lang_keys.h"
#include "window/window_session_controller.h"

namespace Api {

AttachedStickers::AttachedStickers(not_null<ApiWrap*> api)
: _api(&api->instance()) {
}

void AttachedStickers::request(
		not_null<Window::SessionController*> controller,
		MTPmessages_GetAttachedStickers &&mtpRequest) {
	const auto weak = base::make_weak(controller.get());
	_api.request(_requestId).cancel();
	_requestId = _api.request(
		std::move(mtpRequest)
	).done([=](const MTPVector<MTPStickerSetCovered> &result) {
		_requestId = 0;
		const auto strongController = weak.get();
		if (!strongController) {
			return;
		}
		if (result.v.isEmpty()) {
			strongController->show(
				Box<InformBox>(tr::lng_stickers_not_found(tr::now)));
			return;
		} else if (result.v.size() > 1) {
			strongController->show(
				Box<StickersBox>(strongController, result));
			return;
		}
		// Single attached sticker pack.
		const auto setData = result.v.front().match([&](const auto &data) {
			return data.vset().match([&](const MTPDstickerSet &data) {
				return &data;
			});
		});

		const auto setId = (setData->vid().v && setData->vaccess_hash().v)
			? StickerSetIdentifier{
				.id = setData->vid().v,
				.accessHash = setData->vaccess_hash().v }
			: StickerSetIdentifier{ .shortName = qs(setData->vshort_name()) };
		strongController->show(
			Box<StickerSetBox>(strongController, setId),
			Ui::LayerOption::KeepOther);
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		if (const auto strongController = weak.get()) {
			strongController->show(
				Box<InformBox>(tr::lng_stickers_not_found(tr::now)));
		}
	}).send();
}

void AttachedStickers::requestAttachedStickerSets(
		not_null<Window::SessionController*> controller,
		not_null<PhotoData*> photo) {
	request(
		controller,
		MTPmessages_GetAttachedStickers(
			MTP_inputStickeredMediaPhoto(photo->mtpInput())));
}

void AttachedStickers::requestAttachedStickerSets(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document) {
	request(
		controller,
		MTPmessages_GetAttachedStickers(
			MTP_inputStickeredMediaDocument(document->mtpInput())));
}

} // namespace Api
