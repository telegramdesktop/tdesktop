/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_context_menu.h"

#include "api/api_attached_stickers.h"
#include "api/api_editing.h"
#include "api/api_polls.h"
#include "api/api_report.h"
#include "api/api_ringtones.h"
#include "api/api_who_reacted.h"
#include "api/api_toggling_media.h" // Api::ToggleFavedSticker
#include "base/unixtime.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_message.h"
#include "history/history_item_text.h"
#include "history/view/history_view_schedule_box.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_web_page.h"
#include "history/view/reactions/message_reactions_list.h"
#include "ui/widgets/popup_menu.h"
#include "ui/image/image.h"
#include "ui/toast/toast.h"
#include "ui/controls/delete_message_context_action.h"
#include "ui/controls/who_reacted_context_action.h"
#include "ui/boxes/report_box.h"
#include "ui/ui_utility.h"
#include "menu/menu_send.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/delete_messages_box.h"
#include "boxes/report_messages_box.h"
#include "boxes/sticker_set_box.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_groups.h"
#include "data/data_channel.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "data/data_scheduled_messages.h"
#include "data/data_message_reactions.h"
#include "core/file_utilities.h"
#include "core/click_handler_types.h"
#include "base/platform/base_platform_info.h"
#include "window/window_peer_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/application.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "apiwrap.h"
#include "facades.h" // LambdaDelayed
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace HistoryView {
namespace {

constexpr auto kRescheduleLimit = 20;

bool HasEditMessageAction(
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	const auto context = list->elementContext();
	if (!item
		|| item->isSending()
		|| item->hasFailed()
		|| item->isEditingMedia()
		|| !request.selectedItems.empty()
		|| (context != Context::History && context != Context::Replies)) {
		return false;
	}
	const auto peer = item->history()->peer;
	if (const auto channel = peer->asChannel()) {
		if (!channel->isMegagroup() && !channel->canEditMessages()) {
			return false;
		}
	}
	return true;
}

void SavePhotoToFile(not_null<PhotoData*> photo) {
	const auto media = photo->activeMediaView();
	if (photo->isNull() || !media || !media->loaded()) {
		return;
	}

	const auto image = media->image(Data::PhotoSize::Large)->original();
	FileDialog::GetWritePath(
		Core::App().getFileDialogParent(),
		tr::lng_save_photo(tr::now),
		qsl("JPEG Image (*.jpg);;") + FileDialog::AllFilesFilter(),
		filedialogDefaultName(qsl("photo"), qsl(".jpg")),
		crl::guard(&photo->session(), [=](const QString &result) {
			if (!result.isEmpty()) {
				media->saveToFile(result);
			}
		}));
}

void CopyImage(not_null<PhotoData*> photo) {
	const auto media = photo->activeMediaView();
	if (photo->isNull() || !media || !media->loaded()) {
		return;
	}

	const auto image = media->image(Data::PhotoSize::Large)->original();
	QGuiApplication::clipboard()->setImage(image);
}

void ShowStickerPackInfo(
		not_null<DocumentData*> document,
		not_null<ListWidget*> list) {
	StickerSetBox::Show(list->controller(), document);
}

void ToggleFavedSticker(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document,
		FullMsgId contextId) {
	Api::ToggleFavedSticker(controller, document, contextId);
}

void AddPhotoActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<PhotoData*> photo,
		HistoryItem *item,
		not_null<ListWidget*> list) {
	const auto contextId = item ? item->fullId() : FullMsgId();
	if (!list->hasCopyRestriction(item)) {
		menu->addAction(
			tr::lng_context_save_image(tr::now),
			App::LambdaDelayed(
				st::defaultDropdownMenu.menu.ripple.hideDuration,
				&photo->session(),
				[=] { SavePhotoToFile(photo); }),
			&st::menuIconSaveImage);
		menu->addAction(tr::lng_context_copy_image(tr::now), [=] {
			const auto item = photo->owner().message(contextId);
			if (!list->showCopyRestriction(item)) {
				CopyImage(photo);
			}
		}, &st::menuIconCopy);
	}
	if (photo->hasAttachedStickers()) {
		const auto controller = list->controller();
		auto callback = [=] {
			auto &attached = photo->session().api().attachedStickers();
			attached.requestAttachedStickerSets(controller, photo);
		};
		menu->addAction(
			tr::lng_context_attached_stickers(tr::now),
			std::move(callback),
			&st::menuIconStickers);
	}
}

void SaveGif(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	if (const auto item = controller->session().data().message(itemId)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				Api::ToggleSavedGif(
					controller,
					document,
					item->fullId(),
					true);
			}
		}
	}
}

void OpenGif(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	if (const auto item = controller->session().data().message(itemId)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				controller->openDocument(document, itemId, true);
			}
		}
	}
}

void ShowInFolder(not_null<DocumentData*> document) {
	const auto filepath = document->filepath(true);
	if (!filepath.isEmpty()) {
		File::ShowInFolder(filepath);
	}
}

void AddSaveDocumentAction(
		not_null<Ui::PopupMenu*> menu,
		HistoryItem *item,
		not_null<DocumentData*> document,
		not_null<ListWidget*> list) {
	if (list->hasCopyRestriction(item)) {
		return;
	}
	const auto origin = item ? item->fullId() : FullMsgId();
	const auto save = [=] {
		DocumentSaveClickHandler::SaveAndTrack(
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
			save),
		&st::menuIconDownload);
}

void AddDocumentActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<DocumentData*> document,
		HistoryItem *item,
		not_null<ListWidget*> list) {
	if (document->loading()) {
		menu->addAction(tr::lng_context_cancel_download(tr::now), [=] {
			document->cancel();
		}, &st::menuIconCancel);
		return;
	}
	const auto controller = list->controller();
	const auto contextId = item ? item->fullId() : FullMsgId();
	const auto session = &document->session();
	if (item && document->isGifv()) {
		const auto notAutoplayedGif = !Data::AutoDownload::ShouldAutoPlay(
			document->session().settings().autoDownload(),
			item->history()->peer,
			document);
		if (notAutoplayedGif) {
			menu->addAction(tr::lng_context_open_gif(tr::now), [=] {
				OpenGif(list->controller(), contextId);
			}, &st::menuIconShowInChat);
		}
		if (!list->hasCopyRestriction(item)) {
			menu->addAction(tr::lng_context_save_gif(tr::now), [=] {
				SaveGif(list->controller(), contextId);
			}, &st::menuIconGif);
		}
	}
	if (document->sticker() && document->sticker()->set) {
		menu->addAction(
			(document->isStickerSetInstalled()
				? tr::lng_context_pack_info(tr::now)
				: tr::lng_context_pack_add(tr::now)),
			[=] { ShowStickerPackInfo(document, list); },
			&st::menuIconStickers);
		const auto isFaved = document->owner().stickers().isFaved(document);
		menu->addAction(
			(isFaved
				? tr::lng_faved_stickers_remove(tr::now)
				: tr::lng_faved_stickers_add(tr::now)),
			[=] { ToggleFavedSticker(controller, document, contextId); },
			isFaved ? &st::menuIconUnfave : &st::menuIconFave);
	}
	if (!document->filepath(true).isEmpty()) {
		menu->addAction(
			(Platform::IsMac()
				? tr::lng_context_show_in_finder(tr::now)
				: tr::lng_context_show_in_folder(tr::now)),
			[=] { ShowInFolder(document); },
			&st::menuIconShowInFolder);
	}
	if (document->hasAttachedStickers()) {
		const auto controller = list->controller();
		auto callback = [=] {
			auto &attached = session->api().attachedStickers();
			attached.requestAttachedStickerSets(controller, document);
		};
		menu->addAction(
			tr::lng_context_attached_stickers(tr::now),
			std::move(callback),
			&st::menuIconStickers);
	}
	if (item && !list->hasCopyRestriction(item)) {
		const auto controller = list->controller();
		AddSaveSoundForNotifications(menu, item, document, controller);
	}
	AddSaveDocumentAction(menu, item, document, list);
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
	const auto context = request.view
		? request.view->context()
		: Context::History;
	const auto controller = request.navigation->parentController();
	menu->addAction(
		(item->history()->peer->isMegagroup()
			? tr::lng_context_copy_message_link
			: tr::lng_context_copy_post_link)(tr::now),
		[=] { CopyPostLink(controller, itemId, context); },
		&st::menuIconLink);
}

MessageIdsList ExtractIdsList(const SelectedItems &items) {
	return ranges::views::all(
		items
	) | ranges::views::transform(
		&SelectedItem::msgId
	) | ranges::to_vector;
}

bool AddForwardSelectedAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!request.overSelection || request.selectedItems.empty()) {
		return false;
	}
	if (!ranges::all_of(request.selectedItems, &SelectedItem::canForward)) {
		return false;
	}

	menu->addAction(tr::lng_context_forward_selected(tr::now), [=] {
		const auto weak = Ui::MakeWeak(list);
		const auto callback = [=] {
			if (const auto strong = weak.data()) {
				strong->cancelSelection();
			}
		};
		Window::ShowForwardMessagesBox(
			request.navigation,
			ExtractIdsList(request.selectedItems),
			callback);
	}, &st::menuIconForward);
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
			if (!ranges::all_of(group->items, &HistoryItem::allowsForward)) {
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
	}, &st::menuIconForward);
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
	if (!ranges::all_of(request.selectedItems, &SelectedItem::canSendNow)) {
		return false;
	}

	const auto session = &request.navigation->session();
	auto histories = ranges::views::all(
		request.selectedItems
	) | ranges::views::transform([&](const SelectedItem &item) {
		return session->data().message(item.msgId);
	}) | ranges::views::filter([](HistoryItem *item) {
		return item != nullptr;
	}) | ranges::views::transform(
		&HistoryItem::history
	);
	if (histories.begin() == histories.end()) {
		return false;
	}
	const auto history = *histories.begin();

	menu->addAction(tr::lng_context_send_now_selected(tr::now), [=] {
		const auto weak = Ui::MakeWeak(list);
		const auto callback = [=] {
			request.navigation->showBackFromStack();
		};
		Window::ShowSendNowMessagesBox(
			request.navigation,
			history,
			ExtractIdsList(request.selectedItems),
			callback);
	}, &st::menuIconSend);
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
			if (!ranges::all_of(group->items, &HistoryItem::allowsSendNow)) {
				return false;
			}
		}
	}
	const auto itemId = item->fullId();
	menu->addAction(tr::lng_context_send_now_msg(tr::now), [=] {
		if (const auto item = owner->message(itemId)) {
			Window::ShowSendNowMessagesBox(
				request.navigation,
				item->history(),
				(asGroup
					? owner->itemOrItsGroup(item)
					: MessageIdsList{ 1, itemId }));
		}
	}, &st::menuIconSend);
	return true;
}

bool AddRescheduleAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto owner = &request.navigation->session().data();

	const auto goodSingle = HasEditMessageAction(request, list)
		&& request.item->isScheduled();
	const auto goodMany = [&] {
		if (goodSingle) {
			return false;
		}
		const auto &items = request.selectedItems;
		if (!request.overSelection || items.empty()) {
			return false;
		}
		if (items.size() > kRescheduleLimit) {
			return false;
		}
		return ranges::all_of(items, &SelectedItem::canSendNow);
	}();
	if (!goodSingle && !goodMany) {
		return false;
	}
	auto ids = goodSingle
		? MessageIdsList{ request.item->fullId() }
		: ExtractIdsList(request.selectedItems);
	ranges::sort(ids, [&](const FullMsgId &a, const FullMsgId &b) {
		const auto itemA = owner->message(a);
		const auto itemB = owner->message(b);
		return (itemA && itemB) && (itemA->position() < itemB->position());
	});

	auto text = ((ids.size() == 1)
		? tr::lng_context_reschedule
		: tr::lng_context_reschedule_selected)(tr::now);

	menu->addAction(std::move(text), [=] {
		const auto firstItem = owner->message(ids.front());
		if (!firstItem) {
			return;
		}
		const auto callback = [=](Api::SendOptions options) {
			list->cancelSelection();
			for (const auto &id : ids) {
				const auto item = owner->message(id);
				if (!item || !item->isScheduled()) {
					continue;
				}
				if (!item->media() || !item->media()->webpage()) {
					options.removeWebPageId = true;
				}
				Api::RescheduleMessage(item, options);
				// Increase the scheduled date by 1s to keep the order.
				options.scheduled += 1;
			}
		};

		const auto peer = firstItem->history()->peer;
		const auto sendMenuType = !peer
			? SendMenu::Type::Disabled
			: peer->isSelf()
			? SendMenu::Type::Reminder
			: HistoryView::CanScheduleUntilOnline(peer)
			? SendMenu::Type::ScheduledToUser
			: SendMenu::Type::Scheduled;

		using S = Data::ScheduledMessages;
		const auto itemDate = firstItem->date();
		const auto date = (itemDate == S::kScheduledUntilOnlineTimestamp)
			? HistoryView::DefaultScheduleTime()
			: itemDate + 600;

		const auto box = request.navigation->parentController()->show(
			HistoryView::PrepareScheduleBox(
				&request.navigation->session(),
				sendMenuType,
				callback,
				date),
			Ui::LayerOption::KeepOther);

		owner->itemRemoved(
		) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
			if (ranges::contains(ids, item->fullId())) {
				box->closeBox();
			}
		}, box->lifetime());
	}, &st::menuIconReschedule);
	return true;
}

bool AddReplyToMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto context = list->elementContext();
	const auto item = request.item;
	if (!item
		|| !item->isRegular()
		|| !item->history()->peer->canWrite()
		|| (context != Context::History && context != Context::Replies)) {
		return false;
	}
	const auto owner = &item->history()->owner();
	const auto itemId = item->fullId();
	menu->addAction(tr::lng_context_reply_msg(tr::now), [=] {
		const auto item = owner->message(itemId);
		if (!item) {
			return;
		}
		list->replyToMessageRequestNotify(item->fullId());
	}, &st::menuIconReply);
	return true;
}

bool AddViewRepliesAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto context = list->elementContext();
	const auto item = request.item;
	if (!item
		|| !item->isRegular()
		|| (context != Context::History && context != Context::Pinned)) {
		return false;
	}
	const auto repliesCount = item->repliesCount();
	const auto withReplies = (repliesCount > 0);
	if (!withReplies || !item->history()->peer->isMegagroup()) {
		return false;
	}
	const auto rootId = repliesCount ? item->id : item->replyToTop();
	const auto phrase = (repliesCount > 0)
		? tr::lng_replies_view(
			tr::now,
			lt_count,
			repliesCount)
		: tr::lng_replies_view_thread(tr::now);
	const auto controller = list->controller();
	const auto history = item->history();
	menu->addAction(phrase, crl::guard(controller, [=] {
		controller->showRepliesForMessage(history, rootId);
	}), &st::menuIconViewReplies);
	return true;
}

bool AddEditMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!HasEditMessageAction(request, list)) {
		return false;
	}
	const auto item = request.item;
	if (!item->allowsEdit(base::unixtime::now())) {
		return false;
	}
	const auto owner = &item->history()->owner();
	const auto itemId = item->fullId();
	menu->addAction(tr::lng_context_edit_msg(tr::now), [=] {
		const auto item = owner->message(itemId);
		if (!item) {
			return;
		}
		list->editMessageRequestNotify(item->fullId());
	}, &st::menuIconEdit);
	return true;
}

bool AddPinMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto context = list->elementContext();
	const auto item = request.item;
	if (!item
		|| !item->isRegular()
		|| (context != Context::History && context != Context::Pinned)) {
		return false;
	}
	const auto group = item->history()->owner().groups().find(item);
	const auto pinItem = ((item->canPin() && item->isPinned()) || !group)
		? item
		: group->items.front().get();
	if (!pinItem->canPin()) {
		return false;
	}
	const auto pinItemId = pinItem->fullId();
	const auto isPinned = pinItem->isPinned();
	const auto controller = list->controller();
	menu->addAction(isPinned ? tr::lng_context_unpin_msg(tr::now) : tr::lng_context_pin_msg(tr::now), crl::guard(controller, [=] {
		Window::ToggleMessagePinned(controller, pinItemId, !isPinned);
	}), isPinned ? &st::menuIconUnpin : &st::menuIconPin);
	return true;
}

bool AddGoToMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto context = list->elementContext();
	const auto view = request.view;
	if (!view
		|| !view->data()->isRegular()
		|| context != Context::Pinned
		|| !view->hasOutLayout()) {
		return false;
	}
	const auto itemId = view->data()->fullId();
	const auto controller = list->controller();
	menu->addAction(tr::lng_context_to_msg(tr::now), crl::guard(controller, [=] {
		const auto item = controller->session().data().message(itemId);
		if (item) {
			goToMessageClickHandler(item)->onClick(ClickContext{});
		}
	}), &st::menuIconShowInChat);
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
	if (!ranges::all_of(request.selectedItems, &SelectedItem::canDelete)) {
		return false;
	}

	menu->addAction(tr::lng_context_delete_selected(tr::now), [=] {
		auto items = ExtractIdsList(request.selectedItems);
		auto box = Box<DeleteMessagesBox>(
			&request.navigation->session(),
			std::move(items));
		box->setDeleteConfirmedCallback(crl::guard(list, [=] {
			list->cancelSelection();
		}));
		request.navigation->parentController()->show(std::move(box));
	}, &st::menuIconDelete);
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
			if (ranges::any_of(group->items, [](auto item) {
				return item->isLocal() || !item->canDelete();
			})) {
				return false;
			}
		}
	}
	const auto controller = list->controller();
	const auto itemId = item->fullId();
	const auto callback = crl::guard(controller, [=] {
		if (const auto item = owner->message(itemId)) {
			if (asGroup) {
				if (const auto group = owner->groups().find(item)) {
					controller->show(Box<DeleteMessagesBox>(
						&owner->session(),
						owner->itemsToIds(group->items)));
					return;
				}
			}
			if (item->isUploading()) {
				controller->cancelUploadLayer(item);
				return;
			}
			const auto suggestModerateActions = true;
			controller->show(
				Box<DeleteMessagesBox>(item, suggestModerateActions));
		}
	});
	if (item->isUploading()) {
		menu->addAction(
			tr::lng_context_cancel_upload(tr::now),
			callback,
			&st::menuIconCancel);
		return true;
	}
	menu->addAction(Ui::DeleteMessageContextAction(
		menu->menu(),
		callback,
		item->ttlDestroyAt(),
		[=] { delete menu; }));
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

void AddReportAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	if (!request.selectedItems.empty()) {
		return;
	} else if (!item || !item->suggestReport()) {
		return;
	}
	const auto owner = &item->history()->owner();
	const auto controller = list->controller();
	const auto itemId = item->fullId();
	const auto callback = crl::guard(controller, [=] {
		if (const auto item = owner->message(itemId)) {
			const auto group = owner->groups().find(item);
			controller->show(ReportItemsBox(
				item->history()->peer,
				(group
					? owner->itemsToIds(group->items)
					: MessageIdsList{ 1, itemId })));
		}
	});
	menu->addAction(
		tr::lng_context_report_msg(tr::now),
		callback,
		&st::menuIconReport);
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
	}, &st::menuIconSelect);
	return true;
}

bool AddSelectMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	if (request.overSelection && !request.selectedItems.empty()) {
		return false;
	} else if (!item
		|| item->isLocal()
		|| item->isService()
		|| list->hasSelectRestriction()) {
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
	}, &st::menuIconSelect);
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

void AddTopMessageActions(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	AddReplyToMessageAction(menu, request, list);
	AddGoToMessageAction(menu, request, list);
	AddViewRepliesAction(menu, request, list);
	AddEditMessageAction(menu, request, list);
	AddPinMessageAction(menu, request, list);
}

void AddMessageActions(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	AddPostLinkAction(menu, request);
	AddForwardAction(menu, request, list);
	AddSendNowAction(menu, request, list);
	AddDeleteAction(menu, request, list);
	AddReportAction(menu, request, list);
	AddSelectionAction(menu, request, list);
	AddRescheduleAction(menu, request, list);
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
		[=] { QGuiApplication::clipboard()->setText(text); },
		&st::menuIconCopy);
}

} // namespace

ContextMenuRequest::ContextMenuRequest(
	not_null<Window::SessionNavigation*> navigation)
: navigation(navigation) {
}

base::unique_qptr<Ui::PopupMenu> FillContextMenu(
		not_null<ListWidget*> list,
		const ContextMenuRequest &request) {
	const auto link = request.link;
	const auto view = request.view;
	const auto item = request.item;
	const auto itemId = item ? item->fullId() : FullMsgId();
	const auto lnkPhoto = link
		? reinterpret_cast<PhotoData*>(
			link->property(kPhotoLinkMediaProperty).toULongLong())
		: nullptr;
	const auto lnkDocument = link
		? reinterpret_cast<DocumentData*>(
			link->property(kDocumentLinkMediaProperty).toULongLong())
		: nullptr;
	const auto poll = item
		? (item->media() ? item->media()->poll() : nullptr)
		: nullptr;
	const auto hasSelection = !request.selectedItems.empty()
		|| !request.selectedText.empty();
	const auto hasWhoReactedItem = item && Api::WhoReactedExists(item);

	auto result = base::make_unique_q<Ui::PopupMenu>(
		list,
		hasWhoReactedItem ? st::whoReadMenu : st::popupMenuWithIcons);

	if (request.overSelection && !list->hasCopyRestrictionForSelected()) {
		const auto text = request.selectedItems.empty()
			? tr::lng_context_copy_selected(tr::now)
			: tr::lng_context_copy_selected_items(tr::now);
		result->addAction(text, [=] {
			if (!list->showCopyRestrictionForSelected()) {
				TextUtilities::SetClipboardText(list->getSelectedText());
			}
		}, &st::menuIconCopy);
	}

	AddTopMessageActions(result, request, list);
	if (lnkPhoto) {
		AddPhotoActions(result, lnkPhoto, item, list);
	} else if (lnkDocument) {
		AddDocumentActions(result, lnkDocument, item, list);
	} else if (poll) {
		const auto context = list->elementContext();
		AddPollActions(result, poll, item, context, list->controller());
	} else if (!request.overSelection && view && !hasSelection) {
		const auto owner = &view->data()->history()->owner();
		const auto media = view->media();
		const auto mediaHasTextForCopy = media && media->hasTextForCopy();
		if (const auto document = media ? media->getDocument() : nullptr) {
			AddDocumentActions(result, document, view->data(), list);
		}
		if (!link
			&& (view->hasVisibleText() || mediaHasTextForCopy)
			&& !list->hasCopyRestriction(view->data())) {
			const auto asGroup = (request.pointState != PointState::GroupPart);
			result->addAction(tr::lng_context_copy_text(tr::now), [=] {
				if (const auto item = owner->message(itemId)) {
					if (!list->showCopyRestriction(item)) {
						if (asGroup) {
							if (const auto group = owner->groups().find(item)) {
								TextUtilities::SetClipboardText(HistoryGroupText(group));
								return;
							}
						}
						TextUtilities::SetClipboardText(HistoryItemText(item));
					}
				}
			}, &st::menuIconCopy);
		}
	}

	AddCopyLinkAction(result, link);
	AddMessageActions(result, request, list);

	if (hasWhoReactedItem) {
		AddWhoReactedAction(result, list, item, list->controller());
	}

	return result;
}

void CopyPostLink(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId,
		Context context) {
	const auto item = controller->session().data().message(itemId);
	if (!item || !item->hasDirectLink()) {
		return;
	}
	const auto inRepliesContext = (context == Context::Replies);
	QGuiApplication::clipboard()->setText(
		item->history()->session().api().exportDirectMessageLink(
			item,
			inRepliesContext));

	const auto isPublicLink = [&] {
		const auto channel = item->history()->peer->asChannel();
		Assert(channel != nullptr);
		if (const auto rootId = item->replyToTop()) {
			const auto root = item->history()->owner().message(
				channel->id,
				rootId);
			const auto sender = root
				? root->discussionPostOriginalSender()
				: nullptr;
			if (sender && sender->hasUsername()) {
				return true;
			}
		}
		return channel->hasUsername();
	}();

	Ui::Toast::Show(
		Window::Show(controller).toastParent(),
		isPublicLink
			? tr::lng_channel_public_link_copied(tr::now)
			: tr::lng_context_about_private_link(tr::now));
}

void AddPollActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<PollData*> poll,
		not_null<HistoryItem*> item,
		Context context,
		not_null<Window::SessionController*> controller) {
	if ((context != Context::History)
		&& (context != Context::Replies)
		&& (context != Context::Pinned)) {
		return;
	}
	if (poll->closed()) {
		return;
	}
	const auto itemId = item->fullId();
	if (poll->voted() && !poll->quiz()) {
		menu->addAction(tr::lng_polls_retract(tr::now), [=] {
			poll->session().api().polls().sendVotes(itemId, {});
		}, &st::menuIconRetractVote);
	}
	if (item->canStopPoll()) {
		menu->addAction(tr::lng_polls_stop(tr::now), [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_polls_stop_warning(),
				.confirmed = [=](Fn<void()> &&close) {
					close();
					if (const auto item = poll->owner().message(itemId)) {
						controller->session().api().polls().close(item);
					}
				},
				.confirmText = tr::lng_polls_stop_sure(),
				.cancelText = tr::lng_cancel(),
			}));
		}, &st::menuIconStopPoll);
	}
}

void AddSaveSoundForNotifications(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document,
		not_null<Window::SessionController*> controller) {
	const auto &ringtones = document->session().api().ringtones();
	if (document->size > ringtones.maxSize()) {
		return;
	} else if (ranges::contains(ringtones.list(), document->id)) {
		return;
	} else if (int(ringtones.list().size()) >= ringtones.maxSavedCount()) {
		return;
	} else if (const auto song = document->song()) {
		if (song->duration > ringtones.maxDuration()) {
			return;
		}
	} else if (const auto voice = document->voice()) {
		if (voice->duration > ringtones.maxDuration()) {
			return;
		}
	} else {
		return;
	}
	const auto toastParent = Window::Show(controller).toastParent();
	menu->addAction(tr::lng_context_save_custom_sound(tr::now), [=] {
		Api::ToggleSavedRingtone(
			document,
			item->fullId(),
			[=] {
				Ui::Toast::Show(
					toastParent,
					tr::lng_ringtones_toast_added(tr::now));
			},
			true);
	}, &st::menuIconSoundAdd);
}

void AddWhoReactedAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<QWidget*> context,
		not_null<HistoryItem*> item,
		not_null<Window::SessionController*> controller) {
	const auto whoReadIds = std::make_shared<Api::WhoReadList>();
	const auto participantChosen = [=](uint64 id) {
		controller->showPeerInfo(PeerId(id));
	};
	const auto weak = Ui::MakeWeak(menu.get());
	const auto showAllChosen = [=, itemId = item->fullId()]{
		// Pressing on an item that has a submenu doesn't hide it :(
		if (const auto strong = weak.data()) {
			strong->hideMenu();
		}
		if (const auto item = controller->session().data().message(itemId)) {
			controller->window().show(ReactionsListBox(
				controller,
				item,
				QString(),
				whoReadIds));
		}
	};
	if (!menu->empty()) {
		menu->addSeparator();
	}
	menu->addAction(Ui::WhoReactedContextAction(
		menu.get(),
		Api::WhoReacted(item, context, st::defaultWhoRead, whoReadIds),
		participantChosen,
		showAllChosen));
}

void ShowWhoReactedMenu(
		not_null<base::unique_qptr<Ui::PopupMenu>*> menu,
		QPoint position,
		not_null<QWidget*> context,
		not_null<HistoryItem*> item,
		const QString &emoji,
		not_null<Window::SessionController*> controller,
		rpl::lifetime &lifetime) {
	const auto participantChosen = [=](uint64 id) {
		controller->showPeerInfo(PeerId(id));
	};
	const auto showAllChosen = [=, itemId = item->fullId()]{
		if (const auto item = controller->session().data().message(itemId)) {
			controller->window().show(ReactionsListBox(
				controller,
				item,
				emoji));
		}
	};
	const auto reactions = &controller->session().data().reactions();
	const auto &list = reactions->list(
		Data::Reactions::Type::Active);
	const auto activeNonQuick = (emoji != reactions->favorite())
		&& ranges::contains(list, emoji, &Data::Reaction::emoji);
	const auto filler = lifetime.make_state<Ui::WhoReactedListMenu>(
		participantChosen,
		showAllChosen);
	Api::WhoReacted(
		item,
		emoji,
		context,
		st::defaultWhoRead
	) | rpl::filter([=](const Ui::WhoReadContent &content) {
		return !content.unknown;
	}) | rpl::start_with_next([=, &lifetime](Ui::WhoReadContent &&content) {
		const auto creating = !*menu;
		const auto refill = [=] {
			if (activeNonQuick) {
				(*menu)->addAction(tr::lng_context_set_as_quick(tr::now), [=] {
					reactions->setFavorite(emoji);
				}, &st::menuIconFave);
				(*menu)->addSeparator();
			}
		};
		if (creating) {
			*menu = base::make_unique_q<Ui::PopupMenu>(
				context,
				st::whoReadMenu);
			(*menu)->lifetime().add(base::take(lifetime));
			refill();
		}
		filler->populate(menu->get(), content);

		if (creating) {
			(*menu)->popup(position);
		}
	}, lifetime);
}

} // namespace HistoryView
