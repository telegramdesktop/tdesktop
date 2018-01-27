/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_context_menu.h"

#include "history/view/history_view_list_widget.h"
#include "history/history_item.h"
#include "history/history_item_text.h"
#include "history/history_media_types.h"
#include "ui/widgets/popup_menu.h"
#include "chat_helpers/message_field.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "core/file_utilities.h"
#include "window/window_peer_menu.h"
#include "lang/lang_keys.h"
#include "messenger.h"
#include "mainwidget.h"
#include "auth_session.h"

namespace HistoryView {
namespace {

void AddToggleGroupingAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		const auto grouped = (channel->feed() != nullptr);
		menu->addAction(
			lang(grouped ? lng_feed_ungroup : lng_feed_group),
			[=] { Window::ToggleChannelGrouping(channel, !grouped); });
	}
}

void SavePhotoToFile(not_null<PhotoData*> photo) {
	if (!photo->date || !photo->loaded()) {
		return;
	}

	FileDialog::GetWritePath(
		lang(lng_save_photo),
		qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter(),
		filedialogDefaultName(qsl("photo"), qsl(".jpg")),
		base::lambda_guarded(&Auth(), [=](const QString &result) {
			if (!result.isEmpty()) {
				photo->full->pix().toImage().save(result, "JPG");
			}
		}));
}

void CopyImage(not_null<PhotoData*> photo) {
	if (!photo->date || !photo->loaded()) {
		return;
	}

	QApplication::clipboard()->setPixmap(photo->full->pix());
}

void AddPhotoActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<PhotoData*> photo) {
	menu->addAction(
		lang(lng_context_save_image),
		App::LambdaDelayed(
			st::defaultDropdownMenu.menu.ripple.hideDuration,
			&Auth(),
			[=] { SavePhotoToFile(photo); }));
	menu->addAction(lang(lng_context_copy_image), [=] {
		CopyImage(photo);
	});
}

void OpenGif(FullMsgId itemId) {
	if (const auto item = App::histItemById(itemId)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				Messenger::Instance().showDocument(document, item);
			}
		}
	}
}

void ShowInFolder(not_null<DocumentData*> document) {
	const auto filepath = document->filepath(
		DocumentData::FilePathResolveChecked);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
	}
}

void AddSaveDocumentAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<DocumentData*> document) {
	menu->addAction(
		lang(document->isVideoFile()
			? lng_context_save_video
			: (document->isVoiceMessage()
				? lng_context_save_audio
				: (document->isAudioFile()
					? lng_context_save_audio_file
					: (document->sticker()
						? lng_context_save_image
						: lng_context_save_file)))),
		App::LambdaDelayed(
			st::defaultDropdownMenu.menu.ripple.hideDuration,
			&Auth(),
			[=] { DocumentSaveClickHandler::doSave(document, true); }));
}

void AddDocumentActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<DocumentData*> document,
		FullMsgId contextId) {
	if (document->loading()) {
		menu->addAction(lang(lng_context_cancel_download), [=] {
			document->cancel();
		});
		return;
	}
	if (document->loaded() && document->isGifv()) {
		if (!cAutoPlayGif()) {
			menu->addAction(lang(lng_context_open_gif), [=] {
				OpenGif(contextId);
			});
		}
	}
	if (!document->filepath(
			DocumentData::FilePathResolveChecked).isEmpty()) {
		menu->addAction(
			lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld)
				? lng_context_show_in_finder
				: lng_context_show_in_folder),
			[=] { ShowInFolder(document); });
	}
	AddSaveDocumentAction(menu, document);
}

void ShowStickerPackInfo(not_null<DocumentData*> document) {
	if (const auto sticker = document->sticker()) {
		if (sticker->set.type() != mtpc_inputStickerSetEmpty) {
			App::main()->stickersBox(sticker->set);
		}
	}
}

} // namespace

base::unique_qptr<Ui::PopupMenu> FillContextMenu(
		not_null<ListWidget*> list,
		const ContextMenuRequest &request) {
	auto result = base::make_unique_q<Ui::PopupMenu>(nullptr);

	const auto link = request.link;
	const auto view = request.view;
	const auto item = view ? view->data().get() : nullptr;
	const auto itemId = item ? item->fullId() : FullMsgId();
	const auto rawLink = link.get();
	const auto linkPhoto = dynamic_cast<PhotoClickHandler*>(rawLink);
	const auto linkDocument = dynamic_cast<DocumentClickHandler*>(rawLink);
	const auto linkPeer = dynamic_cast<PeerClickHandler*>(rawLink);
	const auto photo = linkPhoto ? linkPhoto->photo().get() : nullptr;
	const auto document = linkDocument
		? linkDocument->document().get()
		: nullptr;
	const auto isVideoLink = document ? document->isVideoFile() : false;
	const auto isVoiceLink = document ? document->isVoiceMessage() : false;
	const auto isAudioLink = document ? document->isAudioFile() : false;
	const auto hasSelection = !request.selectedItems.empty()
		|| !request.selectedText.text.isEmpty();

	if (request.overSelection) {
		result->addAction(lang(lng_context_copy_selected), [=] {
			SetClipboardWithEntities(list->getSelectedText());
		});
	}

	if (linkPhoto) {
		AddPhotoActions(result, photo);
	} else if (linkDocument) {
		AddDocumentActions(result, document, itemId);
	} else if (linkPeer) {
		if (list->delegate()->listContext() == Context::Feed) {
			AddToggleGroupingAction(result, linkPeer->peer());
		}
	} else { // maybe cursor on some text history item?
		bool canDelete = item && item->canDelete() && (item->id > 0 || !item->serviceMsg());
		bool canForward = item && item->allowsForward();

		const auto msg = item->toHistoryMessage();
		if (!request.overSelection) {
			if (item && !hasSelection) {
				auto mediaHasTextForCopy = false;
				if (auto media = view->media()) {
					mediaHasTextForCopy = media->hasTextForCopy();
					if (media->type() == MediaTypeWebPage
						&& static_cast<HistoryWebPage*>(media)->attach()) {
						media = static_cast<HistoryWebPage*>(media)->attach();
					}
					if (media->type() == MediaTypeSticker) {
						if (auto document = media->getDocument()) {
							if (document->sticker() && document->sticker()->set.type() != mtpc_inputStickerSetEmpty) {
								result->addAction(lang(document->isStickerSetInstalled() ? lng_context_pack_info : lng_context_pack_add), [=] {
									ShowStickerPackInfo(document);
								});
							}
							result->addAction(
								lang(lng_context_save_image),
								App::LambdaDelayed(
									st::defaultDropdownMenu.menu.ripple.hideDuration,
									list,
									[=] { DocumentSaveClickHandler::doSave(document, true); }));
						}
					}
				}
				if (!link && (view->hasVisibleText() || mediaHasTextForCopy)) {
					result->addAction(lang(lng_context_copy_text), [=] {
						if (const auto item = App::histItemById(itemId)) {
							SetClipboardWithEntities(HistoryItemText(item));
						}
					});
				}
			}
		}

		const auto actionText = link
			? link->copyToClipboardContextItemText()
			: QString();
		if (!actionText.isEmpty()) {
			result->addAction(
				actionText,
				[text = link->copyToClipboardText()] {
					QApplication::clipboard()->setText(text);
				});
		}
	}
	return result;
}

} // namespace HistoryView
