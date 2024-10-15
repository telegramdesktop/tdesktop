/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_context_menu.h"

#include "api/api_attached_stickers.h"
#include "api/api_editing.h"
#include "api/api_global_privacy.h"
#include "api/api_polls.h"
#include "api/api_report.h"
#include "api/api_ringtones.h"
#include "api/api_transcribes.h"
#include "api/api_who_reacted.h"
#include "api/api_toggling_media.h" // Api::ToggleFavedSticker
#include "base/qt/qt_key_modifiers.h"
#include "base/unixtime.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_text.h"
#include "history/view/history_view_schedule_box.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/history_view_web_page.h"
#include "history/view/reactions/history_view_reactions_list.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_widget.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/menu/menu_multiline_action.h"
#include "ui/image/image.h"
#include "ui/toast/toast.h"
#include "ui/text/format_song_document_name.h"
#include "ui/text/text_utilities.h"
#include "ui/controls/delete_message_context_action.h"
#include "ui/controls/who_reacted_context_action.h"
#include "ui/boxes/edit_factcheck_box.h"
#include "ui/boxes/report_box_graphics.h"
#include "ui/ui_utility.h"
#include "menu/menu_item_download_files.h"
#include "menu/menu_send.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/show_or_premium_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/power_saving.h"
#include "boxes/delete_messages_box.h"
#include "boxes/moderate_messages_box.h"
#include "boxes/report_messages_box.h"
#include "boxes/sticker_set_box.h"
#include "boxes/stickers_box.h"
#include "boxes/translate_box.h"
#include "data/components/factchecks.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_groups.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "data/data_message_reactions.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "chat_helpers/message_field.h" // FactcheckFieldIniter.
#include "core/file_utilities.h"
#include "core/click_handler_types.h"
#include "base/platform/base_platform_info.h"
#include "base/call_delayed.h"
#include "settings/settings_premium.h"
#include "window/window_peer_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/application.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "spellcheck/spellcheck_types.h"
#include "apiwrap.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace HistoryView {
namespace {

constexpr auto kRescheduleLimit = 20;
constexpr auto kTagNameLimit = 12;

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
		|| (context != Context::History
			&& context != Context::Replies
			&& context != Context::ShortcutMessages
			&& context != Context::ScheduledTopic)) {
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

	const auto image = media->image(Data::PhotoSize::Large)->original(); // clazy:exclude=unused-non-trivial-variable
	FileDialog::GetWritePath(
		Core::App().getFileDialogParent(),
		tr::lng_save_photo(tr::now),
		u"JPEG Image (*.jpg);;"_q + FileDialog::AllFilesFilter(),
		filedialogDefaultName(u"photo"_q, u".jpg"_q),
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
	media->setToClipboard();
}

void ShowStickerPackInfo(
		not_null<DocumentData*> document,
		not_null<ListWidget*> list) {
	StickerSetBox::Show(list->controller()->uiShow(), document);
}

void ToggleFavedSticker(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document,
		FullMsgId contextId) {
	Api::ToggleFavedSticker(controller->uiShow(), document, contextId);
}

void AddPhotoActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<PhotoData*> photo,
		HistoryItem *item,
		not_null<ListWidget*> list) {
	const auto contextId = item ? item->fullId() : FullMsgId();
	if (!list->hasCopyMediaRestriction(item)) {
		menu->addAction(
			tr::lng_context_save_image(tr::now),
			base::fn_delayed(
				st::defaultDropdownMenu.menu.ripple.hideDuration,
				&photo->session(),
				[=] { SavePhotoToFile(photo); }),
			&st::menuIconSaveImage);
		menu->addAction(tr::lng_context_copy_image(tr::now), [=] {
			const auto item = photo->owner().message(contextId);
			if (!list->showCopyMediaRestriction(item)) {
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
					controller->uiShow(),
					document,
					item->fullId(),
					true);
			}
		}
	}
}

void OpenGif(not_null<ListWidget*> list, FullMsgId itemId) {
	const auto controller = list->controller();
	if (const auto item = controller->session().data().message(itemId)) {
		if (const auto media = item->media()) {
			if (const auto document = media->document()) {
				list->elementOpenDocument(document, itemId, true);
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
	if (list->hasCopyMediaRestriction(item) || ItemHasTtl(item)) {
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
		base::fn_delayed(
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
			const auto weak = Ui::MakeWeak(list.get());
			menu->addAction(tr::lng_context_open_gif(tr::now), [=] {
				if (const auto strong = weak.data()) {
					OpenGif(strong, contextId);
				}
			}, &st::menuIconShowInChat);
		}
		if (!list->hasCopyMediaRestriction(item)) {
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
	if (item && !list->hasCopyMediaRestriction(item)) {
		const auto controller = list->controller();
		AddSaveSoundForNotifications(menu, item, document, controller);
	}
	AddSaveDocumentAction(menu, item, document, list);
	AddCopyFilename(
		menu,
		document,
		[=] { return list->showCopyRestrictionForSelected(); });
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
			auto groupedIds = std::vector<MessageGroupId>();
			for (const auto &id : ids) {
				const auto item = owner->message(id);
				if (!item || !item->isScheduled()) {
					continue;
				}
				if (const auto groupId = item->groupId()) {
					if (ranges::contains(groupedIds, groupId)) {
						continue;
					}
					groupedIds.push_back(groupId);
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
			: SendMenu::Type::Disabled;

		const auto itemDate = firstItem->date();
		const auto date = (itemDate == Api::kScheduledUntilOnlineTimestamp)
			? HistoryView::DefaultScheduleTime()
			: itemDate + (firstItem->isScheduled() ? 0 : crl::time(600));

		const auto box = request.navigation->parentController()->show(
			HistoryView::PrepareScheduleBox(
				&request.navigation->session(),
				request.navigation->uiShow(),
				{ .type = sendMenuType, .effectAllowed = false },
				callback,
				{}, // initial options
				date));

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
	const auto item = request.quote.item
		? request.quote.item
		: request.item;
	const auto topic = item ? item->topic() : nullptr;
	const auto peer = item ? item->history()->peer.get() : nullptr;
	if (!item
		|| !item->isRegular()
		|| (context != Context::History && context != Context::Replies)) {
		return false;
	}
	const auto canSendReply = topic
		? Data::CanSendAnything(topic)
		: Data::CanSendAnything(peer);
	const auto canReply = canSendReply || item->allowsForward();
	if (!canReply) {
		return false;
	}

	const auto &quote = request.quote;
	auto text = quote.text.empty()
		? tr::lng_context_reply_msg(tr::now)
		: tr::lng_context_quote_and_reply(tr::now);
	text.replace('&', u"&&"_q);
	const auto itemId = item->fullId();
	menu->addAction(text, [=] {
		list->replyToMessageRequestNotify({
			.messageId = itemId,
			.quote = quote.text,
			.quoteOffset = quote.offset,
		}, base::IsCtrlPressed());
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
	const auto topicRootId = item->history()->isForum()
		? item->topicRootId()
		: 0;
	const auto repliesCount = item->repliesCount();
	const auto withReplies = (repliesCount > 0);
	if (!withReplies || !item->history()->peer->isMegagroup()) {
		if (!topicRootId) {
			return false;
		}
	}
	const auto rootId = topicRootId
		? topicRootId
		: repliesCount
		? item->id
		: item->replyToTop();
	const auto highlightId = topicRootId ? item->id : 0;
	const auto phrase = topicRootId
		? tr::lng_replies_view_topic(tr::now)
		: (repliesCount > 0)
		? tr::lng_replies_view(
			tr::now,
			lt_count,
			repliesCount)
		: tr::lng_replies_view_thread(tr::now);
	const auto controller = list->controller();
	const auto history = item->history();
	menu->addAction(phrase, crl::guard(controller, [=] {
		controller->showRepliesForMessage(
			history,
			rootId,
			highlightId);
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

void AddFactcheckAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto item = request.item;
	if (!item || !item->history()->session().factchecks().canEdit(item)) {
		return;
	}
	const auto itemId = item->fullId();
	const auto text = item->factcheckText();
	const auto session = &item->history()->session();
	const auto phrase = text.empty()
		? tr::lng_context_add_factcheck(tr::now)
		: tr::lng_context_edit_factcheck(tr::now);
	menu->addAction(phrase, [=] {
		const auto limit = session->factchecks().lengthLimit();
		const auto controller = request.navigation->parentController();
		controller->show(Box(EditFactcheckBox, text, limit, [=](
				TextWithEntities result) {
			const auto show = controller->uiShow();
			session->factchecks().save(itemId, text, result, show);
		}, FactcheckFieldIniter(controller->uiShow())));
	}, &st::menuIconFactcheck);
}

bool AddPinMessageAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	const auto context = list->elementContext();
	const auto item = request.item;
	if (!item || !item->isRegular()) {
		return false;
	}
	const auto topic = item->topic();
	if (context != Context::History && context != Context::Pinned) {
		if (context != Context::Replies || !topic) {
			return false;
		}
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
		if (const auto item = controller->session().data().message(itemId)) {
			controller->showMessage(item);
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
			const auto list = HistoryItemsList{ item };
			if (CanCreateModerateMessagesBox(list)) {
				controller->show(
					Box(CreateModerateMessagesBox, list, nullptr));
			} else {
				const auto suggestModerateActions = false;
				controller->show(
					Box<DeleteMessagesBox>(item, suggestModerateActions));
			}
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

void AddDownloadFilesAction(
		not_null<Ui::PopupMenu*> menu,
		const ContextMenuRequest &request,
		not_null<ListWidget*> list) {
	if (!request.overSelection
		|| request.selectedItems.empty()
		|| list->hasCopyRestrictionForSelected()) {
		return;
	}
	Menu::AddDownloadFilesAction(
		menu,
		request.navigation->parentController(),
		request.selectedItems,
		list);
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
			const auto ids = group
				? (ranges::views::all(
					group->items
				) | ranges::views::transform([](const auto &i) {
					return i->fullId().msg;
				}) | ranges::to_vector)
				: std::vector<MsgId>{ 1, itemId.msg };
			const auto peer = item->history()->peer;
			ShowReportMessageBox(controller->uiShow(), peer, ids, {});
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
	AddGoToMessageAction(menu, request, list);
	AddViewRepliesAction(menu, request, list);
	AddEditMessageAction(menu, request, list);
	AddFactcheckAction(menu, request, list);
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
	AddDownloadFilesAction(menu, request, list);
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

void EditTagBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const Data::ReactionId &id) {
	const auto owner = &controller->session().data();
	const auto title = owner->reactions().myTagTitle(id);
	box->setTitle(title.isEmpty()
		? tr::lng_context_tag_add_name()
		: tr::lng_context_tag_edit_name());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_edit_tag_about(),
		st::editTagAbout));
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::editTagField,
		tr::lng_edit_tag_name(),
		title));
	field->setMaxLength(kTagNameLimit * 2);
	box->setFocusCallback([=] {
		field->setFocusFast();
	});

	struct State {
		std::unique_ptr<Ui::Text::CustomEmoji> custom;
		QImage image;
	};
	const auto state = field->lifetime().make_state<State>();

	if (const auto customId = id.custom()) {
		state->custom = owner->customEmojiManager().create(
			customId,
			[=] { field->update(); });
	} else {
		owner->reactions().preloadReactionImageFor(id);
	}
	field->paintRequest() | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(field);
		const auto top = st::editTagField.textMargins.top();
		if (const auto custom = state->custom.get()) {
			const auto inactive = !field->window()->isActiveWindow();
			custom->paint(p, {
				.textColor = st::windowFg->c,
				.now = crl::now(),
				.position = QPoint(0, top),
				.paused = inactive || On(PowerSaving::kEmojiChat),
			});
		} else {
			if (state->image.isNull()) {
				state->image = owner->reactions().resolveReactionImageFor(
					id);
			}
			if (!state->image.isNull()) {
				const auto size = st::reactionInlineSize;
				const auto skip = (size - st::reactionInlineImage) / 2;
				p.drawImage(skip, top + skip, state->image);
			}
		}
	}, field->lifetime());

	Ui::AddLengthLimitLabel(field, kTagNameLimit);

	const auto save = [=] {
		const auto text = field->getLastText();
		if (text.size() > kTagNameLimit) {
			field->showError();
			return;
		}
		const auto weak = Ui::MakeWeak(box);
		controller->session().data().reactions().renameTag(id, text);
		if (const auto strong = weak.data()) {
			strong->closeBox();
		}
	};

	field->submits(
	) | rpl::start_with_next(save, field->lifetime());

	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void ShowWhoReadInfo(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId,
		Ui::WhoReadParticipant who) {
	const auto peer = controller->session().data().peer(itemId.peer);
	const auto participant = peer->owner().peer(PeerId(who.id));
	const auto migrated = participant->migrateFrom();
	const auto origin = who.dateReacted
		? Info::Profile::Origin{
			Info::Profile::GroupReactionOrigin{ peer, itemId.msg },
		}
		: Info::Profile::Origin();
	auto memento = std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<Info::ContentMemento>>{
		std::make_shared<Info::Profile::Memento>(
			participant,
			migrated ? migrated->id : PeerId(),
			origin),
	});
	controller->showSection(std::move(memento));
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
	const auto hasWhoReactedItem = item
		&& Api::WhoReactedExists(item, Api::WhoReactedList::All);

	auto result = base::make_unique_q<Ui::PopupMenu>(
		list,
		st::popupMenuWithIcons);

	AddReplyToMessageAction(result, request, list);

	if (request.overSelection
		&& !list->hasCopyRestrictionForSelected()
		&& !list->getSelectedText().empty()) {
		const auto text = request.selectedItems.empty()
			? tr::lng_context_copy_selected(tr::now)
			: tr::lng_context_copy_selected_items(tr::now);
		result->addAction(text, [=] {
			if (!list->showCopyRestrictionForSelected()) {
				TextUtilities::SetClipboardText(list->getSelectedText());
			}
		}, &st::menuIconCopy);
	}
	if (request.overSelection
		&& !Ui::SkipTranslate(list->getSelectedText().rich)) {
		const auto owner = &view->history()->owner();
		result->addAction(tr::lng_context_translate_selected(tr::now), [=] {
			if (const auto item = owner->message(itemId)) {
				list->controller()->show(Box(
					Ui::TranslateBox,
					item->history()->peer,
					MsgId(),
					list->getSelectedText().rich,
					list->hasCopyRestrictionForSelected()));
			}
		}, &st::menuIconTranslate);
	}

	AddTopMessageActions(result, request, list);
	if (lnkPhoto && request.selectedItems.empty()) {
		AddPhotoActions(result, lnkPhoto, item, list);
	} else if (lnkDocument) {
		AddDocumentActions(result, lnkDocument, item, list);
	} else if (poll) {
		const auto context = list->elementContext();
		AddPollActions(result, poll, item, context, list->controller());
	} else if (!request.overSelection && view && !hasSelection) {
		const auto owner = &view->history()->owner();
		const auto media = view->media();
		const auto mediaHasTextForCopy = media && media->hasTextForCopy();
		if (const auto document = media ? media->getDocument() : nullptr) {
			AddDocumentActions(result, document, view->data(), list);
		}
		if (!link && (view->hasVisibleText() || mediaHasTextForCopy)) {
			if (!list->hasCopyRestriction(view->data())) {
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

			const auto translate = mediaHasTextForCopy
				? (HistoryView::TransribedText(item)
					.append('\n')
					.append(item->originalText()))
				: item->originalText();
			if ((!item->translation() || !item->history()->translatedTo())
				&& !translate.text.isEmpty()
				&& !Ui::SkipTranslate(translate)) {
				result->addAction(tr::lng_context_translate(tr::now), [=] {
					if (const auto item = owner->message(itemId)) {
						list->controller()->show(Box(
							Ui::TranslateBox,
							item->history()->peer,
							mediaHasTextForCopy
								? MsgId()
								: item->fullId().msg,
							translate,
							list->hasCopyRestriction(view->data())));
					}
				}, &st::menuIconTranslate);
			}
		}
	}

	AddCopyLinkAction(result, link);
	AddMessageActions(result, request, list);

	const auto wasAmount = result->actions().size();
	if (const auto textItem = view ? view->textItem() : item) {
		AddEmojiPacksAction(
			result,
			textItem,
			HistoryView::EmojiPacksSource::Message,
			list->controller());
	}
	{
		const auto added = (result->actions().size() > wasAmount);
		if (!added) {
			result->addSeparator();
		}
		AddSelectRestrictionAction(result, item, !added);
	}
	if (hasWhoReactedItem) {
		AddWhoReactedAction(result, list, item, list->controller());
	}

	return result;
}

void CopyPostLink(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId,
		Context context) {
	CopyPostLink(controller->uiShow(), itemId, context);
}

void CopyPostLink(
		std::shared_ptr<Main::SessionShow> show,
		FullMsgId itemId,
		Context context) {
	const auto item = show->session().data().message(itemId);
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

	show->showToast(isPublicLink
		? tr::lng_channel_public_link_copied(tr::now)
		: tr::lng_context_about_private_link(tr::now));
}

void CopyStoryLink(
		std::shared_ptr<Main::SessionShow> show,
		FullStoryId storyId) {
	const auto session = &show->session();
	const auto maybeStory = session->data().stories().lookup(storyId);
	if (!maybeStory) {
		return;
	}
	const auto story = *maybeStory;
	QGuiApplication::clipboard()->setText(
		session->api().exportDirectStoryLink(story));
	show->showToast(tr::lng_channel_public_link_copied(tr::now));
}

void AddPollActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<PollData*> poll,
		not_null<HistoryItem*> item,
		Context context,
		not_null<Window::SessionController*> controller) {
	{
		constexpr auto kRadio = "\xf0\x9f\x94\x98";
		const auto radio = QString::fromUtf8(kRadio);
		auto text = poll->question;
		for (const auto &answer : poll->answers) {
			text.append('\n').append(radio).append(answer.text);
		}
		if (!Ui::SkipTranslate(text)) {
			menu->addAction(tr::lng_context_translate(tr::now), [=] {
				controller->show(Box(
					Ui::TranslateBox,
					item->history()->peer,
					MsgId(),
					std::move(text),
					item->forbidsForward()));
			}, &st::menuIconTranslate);
		}
	}
	if ((context != Context::History)
		&& (context != Context::Replies)
		&& (context != Context::Pinned)
		&& (context != Context::ScheduledTopic)) {
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
		}, &st::menuIconRemove);
	}
}

void AddSaveSoundForNotifications(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document,
		not_null<Window::SessionController*> controller) {
	if (ItemHasTtl(item)) {
		return;
	}
	const auto &ringtones = document->session().api().ringtones();
	if (document->size > ringtones.maxSize()) {
		return;
	} else if (ranges::contains(ringtones.list(), document->id)) {
		return;
	} else if (int(ringtones.list().size()) >= ringtones.maxSavedCount()) {
		return;
	} else if (const auto song = document->song()) {
		if (document->duration() > ringtones.maxDuration()) {
			return;
		}
	} else if (const auto voice = document->voice()) {
		if (document->duration() > ringtones.maxDuration()) {
			return;
		}
	} else {
		return;
	}
	const auto show = controller->uiShow();
	menu->addAction(tr::lng_context_save_custom_sound(tr::now), [=] {
		Api::ToggleSavedRingtone(
			document,
			item->fullId(),
			[=] { show->showToast(
				tr::lng_ringtones_toast_added(tr::now)); },
			true);
	}, &st::menuIconSoundAdd);
}

void AddWhoReactedAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<QWidget*> context,
		not_null<HistoryItem*> item,
		not_null<Window::SessionController*> controller) {
	const auto whoReadIds = std::make_shared<Api::WhoReadList>();
	const auto weak = Ui::MakeWeak(menu.get());
	const auto user = item->history()->peer;
	const auto showOrPremium = [=] {
		if (const auto strong = weak.data()) {
			strong->hideMenu();
		}
		const auto type = Ui::ShowOrPremium::ReadTime;
		const auto name = user->shortName();
		auto box = Box(Ui::ShowOrPremiumBox, type, name, [=] {
			const auto api = &controller->session().api();
			api->globalPrivacy().updateHideReadTime({});
		}, [=] {
			Settings::ShowPremium(controller, u"revtime_hidden"_q);
		});
		controller->show(std::move(box));
	};
	const auto itemId = item->fullId();
	const auto participantChosen = [=](Ui::WhoReadParticipant who) {
		if (const auto strong = weak.data()) {
			strong->hideMenu();
		}
		ShowWhoReadInfo(controller, itemId, who);
	};
	const auto showAllChosen = [=, itemId = item->fullId()]{
		// Pressing on an item that has a submenu doesn't hide it :(
		if (const auto strong = weak.data()) {
			strong->hideMenu();
		}
		if (const auto item = controller->session().data().message(itemId)) {
			controller->window().show(Reactions::FullListBox(
				controller,
				item,
				{},
				whoReadIds));
		}
	};
	if (!menu->empty()) {
		menu->addSeparator(&st::expandedMenuSeparator);
	}
	if (item->history()->peer->isUser()) {
		menu->addAction(Ui::WhenReadContextAction(
			menu.get(),
			Api::WhoReacted(item, context, st::defaultWhoRead, whoReadIds),
			showOrPremium));
	} else {
		menu->addAction(Ui::WhoReactedContextAction(
			menu.get(),
			Api::WhoReacted(item, context, st::defaultWhoRead, whoReadIds),
			Data::ReactedMenuFactory(&controller->session()),
			participantChosen,
			showAllChosen));
	}
}

void AddEditTagAction(
		not_null<Ui::PopupMenu*> menu,
		const Data::ReactionId &id,
		not_null<Window::SessionController*> controller) {
	const auto owner = &controller->session().data();
	const auto editLabel = owner->reactions().myTagTitle(id).isEmpty()
		? tr::lng_context_tag_add_name(tr::now)
		: tr::lng_context_tag_edit_name(tr::now);
	menu->addAction(editLabel, [=] {
		controller->show(Box(EditTagBox, controller, id));
	}, &st::menuIconTagRename);
}

void AddTagPackAction(
		not_null<Ui::PopupMenu*> menu,
		const Data::ReactionId &id,
		not_null<Window::SessionController*> controller) {
	if (const auto custom = id.custom()) {
		const auto owner = &controller->session().data();
		if (const auto set = owner->document(custom)->sticker()) {
			if (set->set.id) {
				AddEmojiPacksAction(
					menu,
					{ set->set },
					EmojiPacksSource::Tag,
					controller);
			}
		}
	}
}

void ShowTagMenu(
		not_null<base::unique_qptr<Ui::PopupMenu>*> menu,
		QPoint position,
		not_null<QWidget*> context,
		not_null<HistoryItem*> item,
		const Data::ReactionId &id,
		not_null<Window::SessionController*> controller) {
	using namespace Data;
	const auto itemId = item->fullId();
	const auto owner = &controller->session().data();
	*menu = base::make_unique_q<Ui::PopupMenu>(
		context,
		st::popupMenuExpandedSeparator);
	(*menu)->addAction(tr::lng_context_filter_by_tag(tr::now), [=] {
		HashtagClickHandler(SearchTagToQuery(id)).onClick({
			.button = Qt::LeftButton,
			.other = QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = controller,
			}),
		});
	}, &st::menuIconTagFilter);

	AddEditTagAction(menu->get(), id, controller);

	const auto removeTag = [=] {
		if (const auto item = owner->message(itemId)) {
			const auto &list = item->reactions();
			if (ranges::contains(list, id, &MessageReaction::id)) {
				item->toggleReaction(id, HistoryReactionSource::Quick);
			}
		}
	};
	(*menu)->addAction(base::make_unique_q<Ui::Menu::Action>(
		(*menu)->menu(),
		st::menuWithIconsAttention,
		Ui::Menu::CreateAction(
			(*menu)->menu(),
			tr::lng_context_remove_tag(tr::now),
			removeTag),
		&st::menuIconTagRemoveAttention,
		&st::menuIconTagRemoveAttention));

	AddTagPackAction(menu->get(), id, controller);

	(*menu)->popup(position);
}

void ShowTagInListMenu(
		not_null<base::unique_qptr<Ui::PopupMenu>*> menu,
		QPoint position,
		not_null<QWidget*> context,
		const Data::ReactionId &id,
		not_null<Window::SessionController*> controller) {
	*menu = base::make_unique_q<Ui::PopupMenu>(
		context,
		st::popupMenuExpandedSeparator);

	AddEditTagAction(menu->get(), id, controller);
	AddTagPackAction(menu->get(), id, controller);

	(*menu)->popup(position);
}

void AddCopyFilename(
		not_null<Ui::PopupMenu*> menu,
		not_null<DocumentData*> document,
		Fn<bool()> showCopyRestrictionForSelected) {
	const auto filenameToCopy = [&] {
		if (document->isAudioFile()) {
			return TextForMimeData().append(
				Ui::Text::FormatSongNameFor(document).string());
		} else if (document->sticker()
			|| document->isAnimation()
			|| document->isVideoMessage()
			|| document->isVideoFile()
			|| document->isVoiceMessage()) {
			return TextForMimeData();
		} else {
			return TextForMimeData().append(document->filename());
		}
	}();
	if (!filenameToCopy.empty()) {
		menu->addAction(tr::lng_context_copy_filename(tr::now), [=] {
			if (!showCopyRestrictionForSelected()) {
				TextUtilities::SetClipboardText(filenameToCopy);
			}
		}, &st::menuIconCopy);
	}
}

void ShowWhoReactedMenu(
		not_null<base::unique_qptr<Ui::PopupMenu>*> menu,
		QPoint position,
		not_null<QWidget*> context,
		not_null<HistoryItem*> item,
		const Data::ReactionId &id,
		not_null<Window::SessionController*> controller,
		rpl::lifetime &lifetime) {
	if (item->reactionsAreTags()) {
		ShowTagMenu(menu, position, context, item, id, controller);
		return;
	}

	struct State {
		int addedToBottom = 0;
	};
	const auto itemId = item->fullId();
	const auto participantChosen = [=](Ui::WhoReadParticipant who) {
		ShowWhoReadInfo(controller, itemId, who);
	};
	const auto showAllChosen = [=, itemId = item->fullId()]{
		if (const auto item = controller->session().data().message(itemId)) {
			controller->window().show(Reactions::FullListBox(
				controller,
				item,
				id));
		}
	};
	const auto owner = &controller->session().data();
	const auto reactions = &owner->reactions();
	const auto &list = reactions->list(
		Data::Reactions::Type::Active);
	const auto activeNonQuick = !id.paid()
		&& (id != reactions->favoriteId())
		&& (ranges::contains(list, id, &Data::Reaction::id)
			|| (controller->session().premium() && id.custom()));
	const auto filler = lifetime.make_state<Ui::WhoReactedListMenu>(
		Data::ReactedMenuFactory(&controller->session()),
		participantChosen,
		showAllChosen);
	const auto state = lifetime.make_state<State>();
	Api::WhoReacted(
		item,
		id,
		context,
		st::defaultWhoRead
	) | rpl::filter([=](const Ui::WhoReadContent &content) {
		return content.state != Ui::WhoReadState::Unknown;
	}) | rpl::start_with_next([=, &lifetime](Ui::WhoReadContent &&content) {
		const auto creating = !*menu;
		const auto refillTop = [=] {
			if (activeNonQuick) {
				(*menu)->addAction(tr::lng_context_set_as_quick(tr::now), [=] {
					reactions->setFavorite(id);
				}, &st::menuIconFave);
				(*menu)->addSeparator();
			}
		};
		const auto appendBottom = [=] {
			state->addedToBottom = 0;
			if (const auto custom = id.custom()) {
				if (const auto set = owner->document(custom)->sticker()) {
					if (set->set.id) {
						state->addedToBottom = 2;
						AddEmojiPacksAction(
							menu->get(),
							{ set->set },
							EmojiPacksSource::Reaction,
							controller);
					}
				}
			}
		};
		if (creating) {
			*menu = base::make_unique_q<Ui::PopupMenu>(
				context,
				st::whoReadMenu);
			(*menu)->lifetime().add(base::take(lifetime));
			refillTop();
		}
		filler->populate(
			menu->get(),
			content,
			refillTop,
			state->addedToBottom,
			appendBottom);
		if (creating) {
			(*menu)->popup(position);
		}
	}, lifetime);
}

std::vector<StickerSetIdentifier> CollectEmojiPacks(
		not_null<HistoryItem*> item,
		EmojiPacksSource source) {
	auto result = std::vector<StickerSetIdentifier>();
	const auto owner = &item->history()->owner();
	const auto push = [&](DocumentId id) {
		if (const auto set = owner->document(id)->sticker()) {
			if (set->set.id
				&& !ranges::contains(
					result,
					set->set.id,
					&StickerSetIdentifier::id)) {
				result.push_back(set->set);
			}
		}
	};
	switch (source) {
	case EmojiPacksSource::Message:
		for (const auto &entity : item->originalText().entities) {
			if (entity.type() == EntityType::CustomEmoji) {
				const auto data = Data::ParseCustomEmojiData(entity.data());
				push(data);
			}
		}
		break;
	case EmojiPacksSource::Reactions:
		for (const auto &reaction : item->reactions()) {
			if (const auto customId = reaction.id.custom()) {
				push(customId);
			}
		}
		break;
	default: Unexpected("Source in CollectEmojiPacks.");
	}
	return result;
}

void AddEmojiPacksAction(
		not_null<Ui::PopupMenu*> menu,
		std::vector<StickerSetIdentifier> packIds,
		EmojiPacksSource source,
		not_null<Window::SessionController*> controller) {
	if (packIds.empty()) {
		return;
	}

	const auto count = int(packIds.size());
	const auto manager = &controller->session().data().customEmojiManager();
	const auto name = (count == 1)
		? TextWithEntities{ manager->lookupSetName(packIds[0].id) }
		: TextWithEntities();
	if (!menu->empty()) {
		menu->addSeparator();
	}
	auto text = [&] {
		switch (source) {
		case EmojiPacksSource::Message:
			return name.text.isEmpty()
				? tr::lng_context_animated_emoji_many(
					tr::now,
					lt_count,
					count,
					Ui::Text::RichLangValue)
				: tr::lng_context_animated_emoji(
					tr::now,
					lt_name,
					TextWithEntities{ name },
					Ui::Text::RichLangValue);
		case EmojiPacksSource::Tag:
			return tr::lng_context_animated_tag(
				tr::now,
				lt_name,
				TextWithEntities{ name },
				Ui::Text::RichLangValue);
		case EmojiPacksSource::Reaction:
			if (!name.text.isEmpty()) {
				return tr::lng_context_animated_reaction(
					tr::now,
					lt_name,
					TextWithEntities{ name },
					Ui::Text::RichLangValue);
			}
			[[fallthrough]];
		case EmojiPacksSource::Reactions:
			return name.text.isEmpty()
				? tr::lng_context_animated_reactions_many(
					tr::now,
					lt_count,
					count,
					Ui::Text::RichLangValue)
				: tr::lng_context_animated_reactions(
					tr::now,
					lt_name,
					TextWithEntities{ name },
					Ui::Text::RichLangValue);
		}
		Unexpected("Source in AddEmojiPacksAction.");
	}();
	auto button = base::make_unique_q<Ui::Menu::MultilineAction>(
		menu->menu(),
		menu->st().menu,
		st::historyHasCustomEmoji,
		st::historyHasCustomEmojiPosition,
		std::move(text));
	const auto weak = base::make_weak(controller);
	button->setClickedCallback([=] {
		const auto strong = weak.get();
		if (!strong) {
			return;
		} else if (packIds.size() > 1) {
			strong->show(Box<StickersBox>(strong->uiShow(), packIds));
			return;
		}
		// Single used emoji pack.
		strong->show(Box<StickerSetBox>(
			strong->uiShow(),
			packIds.front(),
			Data::StickersType::Emoji));
	});
	menu->addAction(std::move(button));
}

void AddEmojiPacksAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		EmojiPacksSource source,
		not_null<Window::SessionController*> controller) {
	AddEmojiPacksAction(
		menu,
		CollectEmojiPacks(item, source),
		source,
		controller);
}

void AddSelectRestrictionAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		bool addIcon) {
	const auto peer = item->history()->peer;
	if ((peer->allowsForwarding() && !item->forbidsForward())
		|| item->isSponsored()) {
		return;
	}
	auto button = base::make_unique_q<Ui::Menu::MultilineAction>(
		menu->menu(),
		menu->st().menu,
		st::historyHasCustomEmoji,
		addIcon
			? st::historySponsoredAboutMenuLabelPosition
			: st::historyHasCustomEmojiPosition,
		(peer->isMegagroup()
			? tr::lng_context_noforwards_info_group
			: (peer->isChannel())
			? tr::lng_context_noforwards_info_channel
			: (peer->isUser() && peer->asUser()->isBot())
			? tr::lng_context_noforwards_info_channel
			: tr::lng_context_noforwards_info_bot)(
			tr::now,
			Ui::Text::RichLangValue),
		addIcon ? &st::menuIconCopyright : nullptr);
	button->setAttribute(Qt::WA_TransparentForMouseEvents);
	menu->addAction(std::move(button));
}

TextWithEntities TransribedText(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	if (!document || !document->isVoiceMessage()) {
		return {};
	}
	const auto &entry = document->session().api().transcribes().entry(item);
	if (!entry.requestId
		&& entry.shown
		&& !entry.toolong
		&& !entry.failed
		&& !entry.pending
		&& !entry.result.isEmpty()) {
		return { entry.result };
	}
	return {};
}

bool ItemHasTtl(HistoryItem *item) {
	return (item && item->media())
		? (item->media()->ttlSeconds() > 0)
		: false;
}

} // namespace HistoryView
