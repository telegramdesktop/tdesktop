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
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_web_page.h"
#include "ui/widgets/popup_menu.h"
#include "ui/image/image.h"
#include "ui/toast/toast.h"
#include "chat_helpers/message_field.h"
#include "boxes/confirm_box.h"
#include "boxes/sticker_set_box.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_groups.h"
#include "data/data_channel.h"
#include "core/file_utilities.h"
#include "platform/platform_info.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/application.h"
#include "mainwidget.h"
#include "mainwindow.h" // App::wnd()->sessionController
#include "main/main_session.h"
#include "apiwrap.h"

namespace HistoryView {
namespace {

// If we can't cloud-export link for such time we export it locally.
constexpr auto kExportLocalTimeout = crl::time(1000);

//void AddToggleGroupingAction( // #feed
//		not_null<Ui::PopupMenu*> menu,
//		not_null<PeerData*> peer) {
//	if (const auto channel = peer->asChannel()) {
//		const auto grouped = (channel->feed() != nullptr);
//		menu->addAction( // #feed
//			grouped ? tr::lng_feed_ungroup(tr::now) : tr::lng_feed_group(tr::now),
//			[=] { Window::ToggleChannelGrouping(channel, !grouped); });
//	}
//}

void SavePhotoToFile(not_null<PhotoData*> photo) {
	if (photo->isNull() || !photo->loaded()) {
		return;
	}

	FileDialog::GetWritePath(
		Core::App().getFileDialogParent(),
		tr::lng_save_photo(tr::now),
		qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter(),
		filedialogDefaultName(qsl("photo"), qsl(".jpg")),
		crl::guard(&photo->session(), [=](const QString &result) {
			if (!result.isEmpty()) {
				photo->large()->original().save(result, "JPG");
			}
		}));
}

void CopyImage(not_null<PhotoData*> photo) {
	if (photo->isNull() || !photo->loaded()) {
		return;
	}

	QApplication::clipboard()->setImage(photo->large()->original());
}

void ShowStickerPackInfo(not_null<DocumentData*> document) {
	StickerSetBox::Show(App::wnd()->sessionController(), document);
}

void ToggleFavedSticker(
		not_null<DocumentData*> document,
		FullMsgId contextId) {
	document->session().api().toggleFavedSticker(
		document,
		contextId,
		!Stickers::IsFaved(document));
}

void AddPhotoActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<PhotoData*> photo) {
	menu->addAction(
		tr::lng_context_save_image(tr::now),
		App::LambdaDelayed(
			st::defaultDropdownMenu.menu.ripple.hideDuration,
			&photo->session(),
			[=] { SavePhotoToFile(photo); }));
	menu->addAction(tr::lng_context_copy_image(tr::now), [=] {
		CopyImage(photo);
	});
}

void OpenGif(FullMsgId itemId) {
	if (const auto item = Auth().data().message(itemId)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				Core::App().showDocument(document, item);
			}
		}
	}
}

void ShowInFolder(not_null<DocumentData*> document) {
	const auto filepath = document->filepath(
		DocumentData::FilePathResolve::Checked);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
	}
}

void AddSaveDocumentAction(
		not_null<Ui::PopupMenu*> menu,
		Data::FileOrigin origin,
		not_null<DocumentData*> document) {
	const auto save = [=] {
		DocumentSaveClickHandler::Save(
			origin,
			document,
			DocumentSaveClickHandler::Mode::ToNewFile);
	};

	menu->addAction(
		(document->isVideoFile()
			? tr::lng_context_save_video(tr::now)
			: (document->isVoiceMessage()
				? tr::lng_context_save_audio(tr::now)
				: (document->isAudioFile()
					? tr::lng_context_save_audio_file(tr::now)
					: (document->sticker()
						? tr::lng_context_save_image(tr::now)
						: tr::lng_context_save_file(tr::now))))),
		App::LambdaDelayed(
			st::defaultDropdownMenu.menu.ripple.hideDuration,
			&document->session(),
			save));
}

void AddDocumentActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<DocumentData*> document,
		FullMsgId contextId) {
	if (document->loading()) {
		menu->addAction(tr::lng_context_cancel_download(tr::now), [=] {
			document->cancel();
		});
		return;
	}
	if (document->loaded()
		&& document->isGifv()
		&& !document->session().settings().autoplayGifs()) {
		menu->addAction(tr::lng_context_open_gif(tr::now), [=] {
			OpenGif(contextId);
		});
	}
	if (document->sticker()
		&& document->sticker()->set.type() != mtpc_inputStickerSetEmpty) {
		menu->addAction(
			(document->isStickerSetInstalled()
				? tr::lng_context_pack_info(tr::now)
				: tr::lng_context_pack_add(tr::now)),
			[=] { ShowStickerPackInfo(document); });
		menu->addAction(
			(Stickers::IsFaved(document)
				? tr::lng_faved_stickers_remove(tr::now)
				: tr::lng_faved_stickers_add(tr::now)),
			[=] { ToggleFavedSticker(document, contextId); });
	}
	if (!document->filepath(
			DocumentData::FilePathResolve::Checked).isEmpty()) {
		menu->addAction(
			(Platform::IsMac()
				? tr::lng_context_show_in_finder(tr::now)
				: tr::lng_context_show_in_folder(tr::now)),
			[=] { ShowInFolder(document); });
	}
	AddSaveDocumentAction(menu, contextId, document);
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
		(item->history()->peer->isMegagroup()
			? tr::lng_context_copy_link
			: tr::lng_context_copy_post_link)(tr::now),
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

	menu->addAction(tr::lng_context_forward_selected(tr::now), [=] {
		const auto weak = make_weak(list);
		const auto callback = [=] {
			if (const auto strong = weak.data()) {
				strong->cancelSelection();
			}
		};
		Window::ShowForwardMessagesBox(
			request.navigation,
			ExtractIdsList(request.selectedItems),
			callback);
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
	const auto owner = &item->history()->owner();
	const auto asGroup = (request.pointState != PointState::GroupPart);
	if (asGroup) {
		if (const auto group = owner->groups().find(item)) {
			if (ranges::find_if(group->items, [](auto item) {
				return !item->allowsForward();
			}) != end(group->items)) {
				return false;
			}
		}
	}
	const auto itemId = item->fullId();
	menu->addAction(tr::lng_context_forward_msg(tr::now), [=] {
		if (const auto item = owner->message(itemId)) {
			Window::ShowForwardMessagesBox(
				request.navigation,
				(asGroup
					? owner->itemOrItsGroup(item)
					: MessageIdsList{ 1, itemId }));
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

bool AddSendNowSelectedAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!request.overSelection || request.selectedItems.empty()) {
		return false;
	}
	if (ranges::find_if(request.selectedItems, [](const auto &item) {
		return !item.canSendNow;
	}) != end(request.selectedItems)) {
		return false;
	}

	const auto session = &request.navigation->session();
	auto histories = ranges::view::all(
		request.selectedItems
	) | ranges::view::transform([&](const SelectedItem &item) {
		return session->data().message(item.msgId);
	}) | ranges::view::filter([](HistoryItem *item) {
		return item != nullptr;
	}) | ranges::view::transform([](not_null<HistoryItem*> item) {
		return item->history();
	});
	if (histories.begin() == histories.end()) {
		return false;
	}
	const auto history = *histories.begin();

	menu->addAction(tr::lng_context_send_now_selected(tr::now), [=] {
		const auto weak = make_weak(list);
		const auto callback = [=] {
			request.navigation->showBackFromStack();
		};
		Window::ShowSendNowMessagesBox(
			request.navigation,
			history,
			ExtractIdsList(request.selectedItems),
			callback);
	});
	return true;
}

bool AddSendNowMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	if (!request.selectedItems.empty()) {
		return false;
	} else if (!item || !item->allowsSendNow()) {
		return false;
	}
	const auto owner = &item->history()->owner();
	const auto asGroup = (request.pointState != PointState::GroupPart);
	if (asGroup) {
		if (const auto group = owner->groups().find(item)) {
			if (ranges::find_if(group->items, [](auto item) {
				return !item->allowsSendNow();
			}) != end(group->items)) {
				return false;
			}
		}
	}
	const auto itemId = item->fullId();
	menu->addAction(tr::lng_context_send_now_msg(tr::now), [=] {
		if (const auto item = owner->message(itemId)) {
			const auto callback = [=] {
				request.navigation->showBackFromStack();
			};
			Window::ShowSendNowMessagesBox(
				request.navigation,
				item->history(),
				(asGroup
					? owner->itemOrItsGroup(item)
					: MessageIdsList{ 1, itemId }),
				callback);
		}
	});
	return true;
}

void AddSendNowAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	AddSendNowSelectedAction(menu, request, list);
	AddSendNowMessageAction(menu, request, list);
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

	menu->addAction(tr::lng_context_delete_selected(tr::now), [=] {
		const auto weak = make_weak(list);
		auto items = ExtractIdsList(request.selectedItems);
		const auto box = Ui::show(Box<DeleteMessagesBox>(
			&request.navigation->session(),
			std::move(items)));
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
	const auto owner = &item->history()->owner();
	const auto asGroup = (request.pointState != PointState::GroupPart);
	if (asGroup) {
		if (const auto group = owner->groups().find(item)) {
			if (ranges::find_if(group->items, [](auto item) {
				return !IsServerMsgId(item->id) || !item->canDelete();
			}) != end(group->items)) {
				return false;
			}
		}
	}
	const auto itemId = item->fullId();
	menu->addAction(tr::lng_context_delete_msg(tr::now), [=] {
		if (const auto item = owner->message(itemId)) {
			if (asGroup) {
				if (const auto group = owner->groups().find(item)) {
					Ui::show(Box<DeleteMessagesBox>(
						&owner->session(),
						owner->itemsToIds(group->items)));
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
	menu->addAction(tr::lng_context_clear_selection(tr::now), [=] {
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
	const auto owner = &item->history()->owner();
	const auto itemId = item->fullId();
	const auto asGroup = (request.pointState != PointState::GroupPart);
	menu->addAction(tr::lng_context_select_msg(tr::now), [=] {
		if (const auto item = owner->message(itemId)) {
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
	AddSendNowAction(menu, request, list);
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

ContextMenuRequest::ContextMenuRequest(
	not_null<Window::SessionNavigation*> navigation)
: navigation(navigation) {
}

base::unique_qptr<Ui::PopupMenu> FillContextMenu(
		not_null<ListWidget*> list,
		const ContextMenuRequest &request) {
	auto result = base::make_unique_q<Ui::PopupMenu>(list);

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
		|| !request.selectedText.empty();

	if (request.overSelection) {
		const auto text = request.selectedItems.empty()
			? tr::lng_context_copy_selected(tr::now)
			: tr::lng_context_copy_selected_items(tr::now);
		result->addAction(text, [=] {
			SetClipboardText(list->getSelectedText());
		});
	}

	if (linkPhoto) {
		AddPhotoActions(result, photo);
	} else if (linkDocument) {
		AddDocumentActions(result, document, itemId);
	//} else if (linkPeer) { // #feed
	//	const auto peer = linkPeer->peer();
	//	if (peer->isChannel()
	//		&& peer->asChannel()->feed() != nullptr
	//		&& (list->delegate()->listContext() == Context::Feed)) {
	//		Window::PeerMenuAddMuteAction(peer, [&](
	//				const QString &text,
	//				Fn<void()> handler) {
	//			return result->addAction(text, handler);
	//		});
	//		AddToggleGroupingAction(result, linkPeer->peer());
	//	}
	} else if (!request.overSelection && view && !hasSelection) {
		const auto owner = &view->data()->history()->owner();
		const auto media = view->media();
		const auto mediaHasTextForCopy = media && media->hasTextForCopy();
		if (const auto document = media ? media->getDocument() : nullptr) {
			AddDocumentActions(result, document, view->data()->fullId());
		}
		if (!link && (view->hasVisibleText() || mediaHasTextForCopy)) {
			const auto asGroup = (request.pointState != PointState::GroupPart);
			result->addAction(tr::lng_context_copy_text(tr::now), [=] {
				if (const auto item = owner->message(itemId)) {
					if (asGroup) {
						if (const auto group = owner->groups().find(item)) {
							SetClipboardText(HistoryGroupText(group));
							return;
						}
					}
					SetClipboardText(HistoryItemText(item));
				}
			});
		}
	}

	AddCopyLinkAction(result, link);
	AddMessageActions(result, request, list);
	return result;
}

void CopyPostLink(FullMsgId itemId) {
	const auto item = Auth().data().message(itemId);
	if (!item || !item->hasDirectLink()) {
		return;
	}
	QApplication::clipboard()->setText(
		item->history()->session().api().exportDirectMessageLink(item));

	const auto channel = item->history()->peer->asChannel();
	Assert(channel != nullptr);

	Ui::Toast::Show(channel->hasUsername()
		? tr::lng_channel_public_link_copied(tr::now)
		: tr::lng_context_about_private_link(tr::now));
}

void StopPoll(FullMsgId itemId) {
	const auto stop = [=] {
		Ui::hideLayer();
		if (const auto item = Auth().data().message(itemId)) {
			item->history()->session().api().closePoll(item);
		}
	};
	Ui::show(Box<ConfirmBox>(
		tr::lng_polls_stop_warning(tr::now),
		tr::lng_polls_stop_sure(tr::now),
		tr::lng_cancel(tr::now),
		stop));
}

} // namespace HistoryView
