/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_context_menu.h"

#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_message.h"
#include "history/history_item_text.h"
#include "history/history_media_types.h"
#include "ui/widgets/popup_menu.h"
#include "chat_helpers/message_field.h"
#include "boxes/confirm_box.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_groups.h"
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
		//menu->addAction( // #feed
		//	lang(grouped ? lng_feed_ungroup : lng_feed_group),
		//	[=] { Window::ToggleChannelGrouping(channel, !grouped); });
	}
}

void SavePhotoToFile(not_null<PhotoData*> photo) {
	if (!photo->date || !photo->loaded()) {
		return;
	}

	FileDialog::GetWritePath(
		Messenger::Instance().getFileDialogParent(),
		lang(lng_save_photo),
		qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter(),
		filedialogDefaultName(qsl("photo"), qsl(".jpg")),
		crl::guard(&Auth(), [=](const QString &result) {
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

void ShowStickerPackInfo(not_null<DocumentData*> document) {
	if (const auto sticker = document->sticker()) {
		if (sticker->set.type() != mtpc_inputStickerSetEmpty) {
			App::main()->stickersBox(sticker->set);
		}
	}
}

void ToggleFavedSticker(not_null<DocumentData*> document) {
	const auto unfave = Stickers::IsFaved(document);
	MTP::send(
		MTPmessages_FaveSticker(
			document->mtpInput(),
			MTP_bool(unfave)),
		rpcDone([=](const MTPBool &result) {
		Stickers::SetFaved(document, !unfave);
	}));
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
	if (document->sticker()
		&& document->sticker()->set.type() != mtpc_inputStickerSetEmpty) {
		menu->addAction(
			lang(document->isStickerSetInstalled()
				? lng_context_pack_info
				: lng_context_pack_add),
			[=] { ShowStickerPackInfo(document); });
		menu->addAction(
			lang(Stickers::IsFaved(document)
				? lng_faved_stickers_remove
				: lng_faved_stickers_add),
			[=] { ToggleFavedSticker(document); });
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

void CopyPostLink(FullMsgId itemId) {
	if (const auto item = App::histItemById(itemId)) {
		if (item->hasDirectLink()) {
			QApplication::clipboard()->setText(item->directLink());
		}
	}
}

void AddPostLinkAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request) {
	const auto item = request.item;
	if (!item
		|| !item->hasDirectLink()
		|| request.pointState == PointState::Outside) {
		return;
	} else if (request.link
		&& !request.link->copyToClipboardContextItemText().isEmpty()) {
		return;
	}
	const auto itemId = item->fullId();
	menu->addAction(
		lang(item->history()->peer->isMegagroup()
			? lng_context_copy_link
			: lng_context_copy_post_link),
		[=] { CopyPostLink(itemId); });
}

MessageIdsList ExtractIdsList(const SelectedItems &items) {
	return ranges::view::all(
		items
	) | ranges::view::transform([](const auto &item) {
		return item.msgId;
	}) | ranges::to_vector;
}

bool AddForwardSelectedAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!request.overSelection || request.selectedItems.empty()) {
		return false;
	}
	if (ranges::find_if(request.selectedItems, [](const auto &item) {
		return !item.canForward;
	}) != end(request.selectedItems)) {
		return false;
	}

	menu->addAction(lang(lng_context_forward_selected), [=] {
		const auto weak = make_weak(list);
		auto items = ExtractIdsList(request.selectedItems);
		Window::ShowForwardMessagesBox(std::move(items), [=] {
			if (const auto strong = weak.data()) {
				strong->cancelSelection();
			}
		});
	});
	return true;
}

bool AddForwardMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	if (!request.selectedItems.empty()) {
		return false;
	} else if (!item || !item->allowsForward()) {
		return false;
	}
	const auto asGroup = (request.pointState != PointState::GroupPart);
	if (asGroup) {
		if (const auto group = Auth().data().groups().find(item)) {
			if (ranges::find_if(group->items, [](auto item) {
				return !item->allowsForward();
			}) != end(group->items)) {
				return false;
			}
		}
	}
	const auto itemId = item->fullId();
	menu->addAction(lang(lng_context_forward_msg), [=] {
		if (const auto item = App::histItemById(itemId)) {
			Window::ShowForwardMessagesBox(asGroup
				? Auth().data().itemOrItsGroup(item)
				: MessageIdsList{ 1, itemId });
		}
	});
	return true;
}

void AddForwardAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	AddForwardSelectedAction(menu, request, list);
	AddForwardMessageAction(menu, request, list);
}

bool AddDeleteSelectedAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!request.overSelection || request.selectedItems.empty()) {
		return false;
	}
	if (ranges::find_if(request.selectedItems, [](const auto &item) {
		return !item.canDelete;
	}) != end(request.selectedItems)) {
		return false;
	}

	menu->addAction(lang(lng_context_delete_selected), [=] {
		const auto weak = make_weak(list);
		auto items = ExtractIdsList(request.selectedItems);
		const auto box = Ui::show(Box<DeleteMessagesBox>(std::move(items)));
		box->setDeleteConfirmedCallback([=] {
			if (const auto strong = weak.data()) {
				strong->cancelSelection();
			}
		});
	});
	return true;
}

bool AddDeleteMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	if (!request.selectedItems.empty()) {
		return false;
	} else if (!item || !item->canDelete()) {
		return false;
	}
	const auto asGroup = (request.pointState != PointState::GroupPart);
	if (asGroup) {
		if (const auto group = Auth().data().groups().find(item)) {
			if (ranges::find_if(group->items, [](auto item) {
				return !IsServerMsgId(item->id) || !item->canDelete();
			}) != end(group->items)) {
				return false;
			}
		}
	}
	const auto itemId = item->fullId();
	menu->addAction(lang(lng_context_delete_msg), [=] {
		if (const auto item = App::histItemById(itemId)) {
			if (asGroup) {
				if (const auto group = Auth().data().groups().find(item)) {
					Ui::show(Box<DeleteMessagesBox>(
						Auth().data().itemsToIds(group->items)));
					return;
				}
			}
			if (const auto message = item->toHistoryMessage()) {
				if (message->uploading()) {
					App::main()->cancelUploadLayer(item);
					return;
				}
			}
			const auto suggestModerateActions = true;
			Ui::show(Box<DeleteMessagesBox>(item, suggestModerateActions));
		}
	});
	return true;
}

void AddDeleteAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!AddDeleteSelectedAction(menu, request, list)) {
		AddDeleteMessageAction(menu, request, list);
	}
}

bool AddClearSelectionAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!request.overSelection || request.selectedItems.empty()) {
		return false;
	}
	menu->addAction(lang(lng_context_clear_selection), [=] {
		list->cancelSelection();
	});
	return true;
}

bool AddSelectMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	if (request.overSelection && !request.selectedItems.empty()) {
		return false;
	} else if (!item || !IsServerMsgId(item->id) || item->serviceMsg()) {
		return false;
	}
	const auto itemId = item->fullId();
	const auto asGroup = (request.pointState != PointState::GroupPart);
	menu->addAction(lang(lng_context_select_msg), [=] {
		if (const auto item = App::histItemById(itemId)) {
			if (asGroup) {
				list->selectItemAsGroup(item);
			} else {
				list->selectItem(item);
			}
		}
	});
	return true;
}

void AddSelectionAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!AddClearSelectionAction(menu, request, list)) {
		AddSelectMessageAction(menu, request, list);
	}
}

void AddMessageActions(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	AddPostLinkAction(menu, request);
	AddForwardAction(menu, request, list);
	AddDeleteAction(menu, request, list);
	AddSelectionAction(menu, request, list);
}

void AddCopyLinkAction(
		not_null<Ui::PopupMenu*> menu,
		const ClickHandlerPtr &link) {
	if (!link) {
		return;
	}
	const auto action = link->copyToClipboardContextItemText();
	if (action.isEmpty()) {
		return;
	}
	const auto text = link->copyToClipboardText();
	menu->addAction(
		action,
		[=] { QApplication::clipboard()->setText(text); });
}

} // namespace

base::unique_qptr<Ui::PopupMenu> FillContextMenu(
		not_null<ListWidget*> list,
		const ContextMenuRequest &request) {
	auto result = base::make_unique_q<Ui::PopupMenu>(nullptr);

	const auto link = request.link;
	const auto view = request.view;
	const auto item = request.item;
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
		const auto text = lang(request.selectedItems.empty()
			? lng_context_copy_selected
			: lng_context_copy_selected_items);
		result->addAction(text, [=] {
			SetClipboardWithEntities(list->getSelectedText());
		});
	}

	if (linkPhoto) {
		AddPhotoActions(result, photo);
	} else if (linkDocument) {
		AddDocumentActions(result, document, itemId);
	} else if (linkPeer) {
		const auto peer = linkPeer->peer();
		if (peer->isChannel()
			&& peer->asChannel()->feed() != nullptr
			&& (list->delegate()->listContext() == Context::Feed)) {
			Window::PeerMenuAddMuteAction(peer, [&](
					const QString &text,
					Fn<void()> handler) {
				return result->addAction(text, handler);
			});
			AddToggleGroupingAction(result, linkPeer->peer());
		}
	} else if (!request.overSelection && view && !hasSelection) {
		auto media = view->media();
		const auto mediaHasTextForCopy = media && media->hasTextForCopy();
		if (media) {
			if (media->type() == MediaTypeWebPage
				&& static_cast<HistoryWebPage*>(media)->attach()) {
				media = static_cast<HistoryWebPage*>(media)->attach();
			}
			if (media->type() == MediaTypeSticker) {
				if (const auto document = media->getDocument()) {
					AddDocumentActions(result, document, view->data()->fullId());
				}
			}
		}
		if (!link && (view->hasVisibleText() || mediaHasTextForCopy)) {
			const auto asGroup = (request.pointState != PointState::GroupPart);
			result->addAction(lang(lng_context_copy_text), [=] {
				if (const auto item = App::histItemById(itemId)) {
					if (asGroup) {
						if (const auto group = Auth().data().groups().find(item)) {
							SetClipboardWithEntities(HistoryGroupText(group));
							return;
						}
					}
					SetClipboardWithEntities(HistoryItemText(item));
				}
			});
		}
	}

	AddCopyLinkAction(result, link);
	AddMessageActions(result, request, list);
	return result;
}

} // namespace HistoryView
