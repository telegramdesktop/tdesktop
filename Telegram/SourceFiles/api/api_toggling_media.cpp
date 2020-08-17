/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_toggling_media.h"

#include "apiwrap.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h" // Stickers::addSavedGif
#include "main/main_session.h"

namespace Api {
namespace {

template <typename MTPToggleRequest, typename DoneCallback>
void ToggleExistingMedia(
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		bool saved,
		DoneCallback &&done) {
	const auto api = &document->owner().session().api();

	auto performRequest = [=](const auto &repeatRequest) -> void {
		const auto usedFileReference = document->fileReference();
		api->request(MTPToggleRequest(
			document->mtpInput(),
			MTP_bool(!saved)
		)).done([=](const MTPBool &result) {
			if (mtpIsTrue(result)) {
				done();
			}
		}).fail([=](const RPCError &error) {
			if (error.code() == 400
				&& error.type().startsWith(u"FILE_REFERENCE_"_q)) {
				auto refreshed = [=](const Data::UpdatedFileReferences &d) {
					if (document->fileReference() != usedFileReference) {
						repeatRequest(repeatRequest);
					}
				};
				api->refreshFileReference(origin, std::move(refreshed));
			}
		}).send();
	};
	performRequest(performRequest);
}

} // namespace

void ToggleFavedSticker(
		not_null<DocumentData*> document,
		Data::FileOrigin origin) {
	ToggleFavedSticker(
		document,
		std::move(origin),
		!document->owner().stickers().isFaved(document));
}

void ToggleFavedSticker(
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		bool faved) {
	if (faved && !document->sticker()) {
		return;
	}
	ToggleExistingMedia<MTPmessages_FaveSticker>(
		document,
		std::move(origin),
		faved,
		[=] { document->owner().stickers().setFaved(document, faved); });
}

void ToggleSavedGif(
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		bool saved) {
	if (saved && !document->isGifv()) {
		return;
	}
	auto done = [=] {
		if (saved) {
			document->owner().stickers().addSavedGif(document);
		}
	};
	ToggleExistingMedia<MTPmessages_SaveGif>(
		document,
		std::move(origin),
		saved,
		std::move(done));
}

} // namespace Api
