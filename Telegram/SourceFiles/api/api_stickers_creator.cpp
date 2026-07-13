/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_stickers_creator.h"

#include "apiwrap.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_stickers_set.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_action_with_thumbnail.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/popup_menu.h"

namespace Api {
namespace {

constexpr auto kStickerSide = 512;

[[nodiscard]] int SideForType(Data::StickersType type) {
	return (type == Data::StickersType::Emoji)
		? kEmojiStickerSideMax
		: kStickerSide;
}

[[nodiscard]] MTPInputStickerSetItem InputItem(
		const MTPInputDocument &document,
		const QString &emoji) {
	return MTP_inputStickerSetItem(
		MTP_flags(0),
		document,
		MTP_string(emoji),
		MTPMaskCoords(),
		MTPstring());
}

[[nodiscard]] std::shared_ptr<FilePrepareResult> PrepareStickerWebp(
		MTP::DcId dcId,
		DocumentId id,
		const QByteArray &bytes,
		Data::StickersType type) {
	const auto side = SideForType(type);
	const auto filename = (type == Data::StickersType::Emoji)
		? u"emoji.webp"_q
		: u"sticker.webp"_q;
	auto attributes = QVector<MTPDocumentAttribute>(
		1,
		MTP_documentAttributeFilename(MTP_string(filename)));
	attributes.push_back(MTP_documentAttributeImageSize(
		MTP_int(side),
		MTP_int(side)));

	auto result = MakePreparedFile({
		.id = id,
		.type = SendMediaType::File,
	});
	result->filename = filename;
	result->filemime = u"image/webp"_q;
	result->content = bytes;
	result->filesize = bytes.size();
	result->setFileData(bytes);
	result->document = MTP_document(
		MTP_flags(0),
		MTP_long(id),
		MTP_long(0),
		MTP_bytes(),
		MTP_int(base::unixtime::now()),
		MTP_string("image/webp"),
		MTP_long(bytes.size()),
		MTP_vector<MTPPhotoSize>(),
		MTPVector<MTPVideoSize>(),
		MTP_int(dcId),
		MTP_vector<MTPDocumentAttribute>(std::move(attributes)));
	return result;
}

void FeedSetIfFull(
		not_null<Main::Session*> session,
		const MTPmessages_StickerSet &result) {
	result.match([&](const MTPDmessages_stickerSet &data) {
		session->data().stickers().feedSetFull(data);
		session->data().stickers().notifyUpdated(
			Data::StickersType::Stickers);
	}, [](const auto &) {
	});
}

template <typename Callback>
void EnumerateOwnedSets(
		not_null<Main::Session*> session,
		Data::StickersType type,
		Callback &&callback) {
	const auto &stickers = session->data().stickers();
	const auto &sets = stickers.sets();
	const auto &order = (type == Data::StickersType::Emoji)
		? stickers.emojiSetsOrder()
		: stickers.setsOrder();
	for (const auto setId : order) {
		const auto it = sets.find(setId);
		if (it == sets.end()) {
			continue;
		}
		const auto set = it->second.get();
		if (!(set->flags & Data::StickersSetFlag::AmCreator)
			|| (set->type() != type)) {
			continue;
		}
		using namespace Data;
		if constexpr (std::is_same_v<
				bool,
				std::invoke_result_t<Callback, not_null<StickersSet*>>>) {
			if (!callback(set)) {
				return;
			}
		} else {
			callback(set);
		}
	}
}

void FillChooseOwnedSetMenu(
		not_null<Ui::PopupMenu*> menu,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document,
		Data::StickersType type) {
	const auto session = &show->session();
	const auto emoji = StickerEmojiOrDefault(document);
	const auto isEmoji = (type == Data::StickersType::Emoji);
	const auto maxCount = isEmoji
		? kEmojiInOwnedSetMax
		: kStickersInOwnedSetMax;
	const auto fullMessage = isEmoji
		? tr::lng_emoji_set_is_full
		: tr::lng_stickers_set_is_full;
	const auto addedMessage = isEmoji
		? tr::lng_emoji_added
		: tr::lng_stickers_create_added;
	const auto alreadyMessage = isEmoji
		? tr::lng_emoji_already_in_set
		: tr::lng_stickers_already_in_set;
	const auto failToast = [=](QString err) {
		show->showToast(err.isEmpty()
			? tr::lng_attach_failed(tr::now)
			: err);
	};
	EnumerateOwnedSets(session, type, [&](not_null<Data::StickersSet*> set) {
		const auto identifier = set->identifier();
		const auto coverDocument = set->lookupThumbnailDocument();
		auto thumbnail = coverDocument
			? Ui::MakeDocumentThumbnailFit(
				coverDocument,
				Data::FileOriginStickerSet(set->id, set->accessHash))
			: nullptr;
		const auto targetSetId = set->id;
		const auto handler = crl::guard(session, [=] {
			const auto &map = session->data().stickers().sets();
			const auto i = map.find(targetSetId);
			if (i != map.end() && i->second->count >= maxCount) {
				show->showToast(fullMessage(tr::now));
				return;
			}
			const auto oldCount = (i != map.end())
				? i->second->count
				: 0;
			AddExistingStickerToSet(
				session,
				identifier,
				document,
				emoji,
				crl::guard(session, [=](MTPmessages_StickerSet) {
					const auto &map = session->data().stickers().sets();
					const auto i = map.find(targetSetId);
					const auto newCount = (i != map.end())
						? i->second->count
						: oldCount;
					show->showToast(newCount > oldCount
						? addedMessage(tr::now)
						: alreadyMessage(tr::now));
				}),
				crl::guard(session, failToast));
		});
		const auto rawAction = Ui::Menu::CreateAction(
			menu.get(),
			set->title,
			handler);
		auto item = base::make_unique_q<Menu::ActionWithThumbnail>(
			menu->menu(),
			menu->menu()->st(),
			rawAction,
			std::move(thumbnail),
			st::menuIconStickerAdd.width());
		menu->addAction(std::move(item));
	});
}

} // namespace

void AddExistingStickerToSet(
		not_null<Main::Session*> session,
		const StickerSetIdentifier &set,
		not_null<DocumentData*> document,
		const QString &emoji,
		Fn<void(MTPmessages_StickerSet)> done,
		Fn<void(QString)> fail) {
	session->api().request(MTPstickers_AddStickerToSet(
		Data::InputStickerSet(set),
		InputItem(document->mtpInput(), emoji))
	).done([=](const MTPmessages_StickerSet &result) {
		FeedSetIfFull(session, result);
		if (done) {
			done(result);
		}
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error.type());
		}
	}).handleFloodErrors().send();
}

QString StickerEmojiOrDefault(not_null<DocumentData*> document) {
	if (const auto sticker = document->sticker()) {
		if (!sticker->alt.isEmpty()) {
			return sticker->alt;
		}
	}
	return QString::fromUtf8("\xF0\x9F\x99\x82");
}

bool HasOwnedStickerSets(not_null<Main::Session*> session) {
	auto found = false;
	EnumerateOwnedSets(
		session,
		Data::StickersType::Stickers,
		[&](not_null<Data::StickersSet*>) {
			found = true;
			return false;
		});
	return found;
}

bool HasOwnedEmojiSets(not_null<Main::Session*> session) {
	auto found = false;
	EnumerateOwnedSets(
		session,
		Data::StickersType::Emoji,
		[&](not_null<Data::StickersSet*>) {
			found = true;
			return false;
		});
	return found;
}

void FillChooseStickerSetMenu(
		not_null<Ui::PopupMenu*> menu,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document) {
	using namespace Data;
	FillChooseOwnedSetMenu(menu, show, document, StickersType::Stickers);
}

void FillChooseEmojiSetMenu(
		not_null<Ui::PopupMenu*> menu,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document) {
	FillChooseOwnedSetMenu(menu, show, document, Data::StickersType::Emoji);
}

void AddAddToStickerSetAction(
		const Ui::Menu::MenuCallback &addAction,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document) {
	const auto session = &show->session();
	if (!HasOwnedStickerSets(session)) {
		return;
	}
	addAction({
		.text = tr::lng_stickers_add_to_set(tr::now),
		.icon = &st::menuIconStickerAdd,
		.fillSubmenu = [show, document](not_null<Ui::PopupMenu*> submenu) {
			FillChooseStickerSetMenu(submenu, show, document);
		},
		.submenuSt = &st::popupMenuWithIcons,
	});
}

void AddAddToEmojiSetAction(
		const Ui::Menu::MenuCallback &addAction,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document) {
	const auto session = &show->session();
	if (!HasOwnedEmojiSets(session)) {
		return;
	}
	addAction({
		.text = tr::lng_emoji_add_to_set(tr::now),
		.icon = &st::menuIconEmoji,
		.fillSubmenu = [show, document](not_null<Ui::PopupMenu*> submenu) {
			FillChooseEmojiSetMenu(submenu, show, document);
		},
		.submenuSt = &st::popupMenuWithIcons,
	});
}

void AddAddToOwnedSetAction(
		const Ui::Menu::MenuCallback &addAction,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document) {
	const auto sticker = document->sticker();
	const auto isLottie = sticker && sticker->isLottie();
	const auto size = document->dimensions;
	const auto fitsEmoji = !size.isEmpty()
		&& (size.width() <= kEmojiStickerSideMax)
		&& (size.height() <= kEmojiStickerSideMax);
	if (isLottie || !fitsEmoji) {
		AddAddToStickerSetAction(addAction, show, document);
	}
	if (isLottie || fitsEmoji) {
		AddAddToEmojiSetAction(addAction, std::move(show), document);
	}
}

void DeleteStickerSet(
		not_null<Main::Session*> session,
		const StickerSetIdentifier &set,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	session->api().request(MTPstickers_DeleteStickerSet(
		Data::InputStickerSet(set))
	).done([=] {
		session->data().stickers().notifyUpdated(
			Data::StickersType::Stickers);
		if (done) {
			done();
		}
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error.type());
		}
	}).send();
}

StickerUpload::StickerUpload(
	not_null<Main::Session*> session,
	StickerSetIdentifier set,
	QByteArray webpBytes,
	QString emoji,
	Data::StickersType type)
: _session(session)
, _set(std::move(set))
, _bytes(std::move(webpBytes))
, _emoji(std::move(emoji))
, _type(type)
, _api(&session->mtp()) {
}

StickerUpload::~StickerUpload() {
	cancel();
}

void StickerUpload::start(
		Fn<void(MTPmessages_StickerSet)> done,
		Fn<void(QString)> fail,
		Fn<void(int)> progress) {
	Expects(!_uploadId);

	_done = std::move(done);
	_fail = std::move(fail);
	_progress = std::move(progress);

	_documentId = base::RandomValue<DocumentId>();
	auto ready = PrepareStickerWebp(
		_session->mtp().mainDcId(),
		_documentId,
		_bytes,
		_type);
	_uploadId = FullMsgId(
		_session->userPeerId(),
		_session->data().nextLocalMessageId());

	const auto document = _session->data().document(_documentId);
	document->uploadingData = std::make_unique<Data::UploadState>(
		document->size > 0 ? document->size : int64(_bytes.size()));

	_session->uploader().documentReady(
	) | rpl::filter([=](const Storage::UploadedMedia &data) {
		return data.fullId == _uploadId;
	}) | rpl::on_next([=](const Storage::UploadedMedia &data) {
		uploadReady(data.info.file);
	}, _uploadLifetime);

	_session->uploader().documentFailed(
	) | rpl::filter([=](const FullMsgId &id) {
		return id == _uploadId;
	}) | rpl::on_next([=] {
		uploadFailed();
	}, _uploadLifetime);

	if (_progress) {
		_session->uploader().documentProgress(
		) | rpl::filter([=](const FullMsgId &id) {
			return id == _uploadId;
		}) | rpl::on_next([=] {
			uploadProgressed();
		}, _uploadLifetime);
	}

	_session->uploader().upload(_uploadId, ready);
}

void StickerUpload::cancel() {
	if (_uploadId) {
		_session->uploader().cancel(_uploadId);
		_uploadId = FullMsgId();
	}
	if (_addRequestId) {
		_api.request(_addRequestId).cancel();
		_addRequestId = 0;
	}
	_uploadLifetime.destroy();
	_done = nullptr;
	_fail = nullptr;
	_progress = nullptr;
}

void StickerUpload::uploadProgressed() {
	if (!_progress) {
		return;
	}
	const auto document = _session->data().document(_documentId);
	if (!document->uploading() || !document->uploadingData) {
		return;
	}
	const auto size = document->uploadingData->size;
	if (size <= 0) {
		return;
	}
	const auto percent = int(
		(document->uploadingData->offset * 100) / size);
	if (percent != _lastReportedPercent) {
		_lastReportedPercent = percent;
		_progress(percent);
	}
}

void StickerUpload::uploadFailed() {
	const auto fail = std::move(_fail);
	cancel();
	if (fail) {
		fail(QString());
	}
}

void StickerUpload::uploadReady(const MTPInputFile &file) {
	_uploadLifetime.destroy();
	_uploadId = FullMsgId();

	const auto side = SideForType(_type);
	auto attributes = QVector<MTPDocumentAttribute>();
	attributes.push_back(MTP_documentAttributeSticker(
		MTP_flags(0),
		MTP_string(_emoji),
		MTP_inputStickerSetEmpty(),
		MTPMaskCoords()));
	attributes.push_back(MTP_documentAttributeImageSize(
		MTP_int(side),
		MTP_int(side)));

	const auto media = MTP_inputMediaUploadedDocument(
		MTP_flags(0),
		file,
		MTPInputFile(),
		MTP_string("image/webp"),
		MTP_vector<MTPDocumentAttribute>(std::move(attributes)),
		MTP_vector<MTPInputDocument>(),
		MTPInputPhoto(),
		MTP_int(0),
		MTP_int(0));

	_addRequestId = _api.request(MTPmessages_UploadMedia(
		MTP_flags(0),
		MTPstring(),
		MTP_inputPeerSelf(),
		media
	)).done(crl::guard(this, [=](const MTPMessageMedia &result) {
		_addRequestId = 0;
		auto inputDoc = (MTPInputDocument*)(nullptr);
		auto storage = MTPInputDocument();
		result.match([&](const MTPDmessageMediaDocument &data) {
			if (const auto doc = data.vdocument()) {
				doc->match([&](const MTPDdocument &d) {
					storage = MTP_inputDocument(
						d.vid(),
						d.vaccess_hash(),
						d.vfile_reference());
					inputDoc = &storage;
				}, [](const auto &) {
				});
			}
		}, [](const auto &) {
		});
		if (inputDoc) {
			requestAddSticker(*inputDoc);
		} else if (const auto fail = std::move(_fail)) {
			cancel();
			fail(QString());
		}
	})).fail(crl::guard(this, [=](const MTP::Error &error) {
		_addRequestId = 0;
		const auto fail = std::move(_fail);
		const auto type = error.type();
		cancel();
		if (fail) {
			fail(type);
		}
	})).handleFloodErrors().send();
}

void StickerUpload::requestAddSticker(const MTPInputDocument &document) {
	_addRequestId = _api.request(MTPstickers_AddStickerToSet(
		Data::InputStickerSet(_set),
		InputItem(document, _emoji))
	).done(crl::guard(this, [=](const MTPmessages_StickerSet &result) {
		_addRequestId = 0;
		FeedSetIfFull(_session, result);
		const auto done = std::move(_done);
		cancel();
		if (done) {
			done(result);
		}
	})).fail(crl::guard(this, [=](const MTP::Error &error) {
		_addRequestId = 0;
		const auto fail = std::move(_fail);
		const auto type = error.type();
		cancel();
		if (fail) {
			fail(type);
		}
	})).handleFloodErrors().send();
}

} // namespace Api
