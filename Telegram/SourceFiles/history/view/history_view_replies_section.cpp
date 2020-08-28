/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_replies_section.h"

#include "history/view/history_view_compose_controls.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_schedule_box.h"
#include "history/history.h"
#include "history/history_drag_area.h"
#include "history/history_item.h"
#include "chat_helpers/send_context_menu.h" // SendMenu::Type.
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/layers/generic_box.h"
#include "ui/text_options.h"
#include "ui/toast/toast.h"
#include "ui/special_buttons.h"
#include "ui/ui_utility.h"
#include "api/api_common.h"
#include "api/api_editing.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "boxes/confirm_box.h"
#include "boxes/edit_caption_box.h"
#include "boxes/send_files_box.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "base/event_filter.h"
#include "base/call_delayed.h"
#include "core/file_utilities.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_replies_list.h"
#include "data/data_user.h"
#include "storage/storage_media_prepare.h"
#include "storage/storage_account.h"
#include "inline_bots/inline_bot_result.h"
#include "platform/platform_specific.h"
#include "lang/lang_keys.h"
#include "facades.h"
#include "app.h"
#include "styles/style_history.h"
#include "styles/style_window.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"

#include <QtCore/QMimeData>

namespace HistoryView {
namespace {

void ShowErrorToast(const QString &text) {
	Ui::Toast::Show(Ui::Toast::Config{
		.text = { text },
		.st = &st::historyErrorToast,
		.multiline = true,
	});
}

bool CanSendFiles(not_null<const QMimeData*> data) {
	if (data->hasImage()) {
		return true;
	} else if (const auto urls = data->urls(); !urls.empty()) {
		if (ranges::all_of(urls, &QUrl::isLocalFile)) {
			return true;
		}
	}
	return false;
}

} // namespace

object_ptr<Window::SectionWidget> RepliesMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<RepliesWidget>(
		parent,
		controller,
		_history,
		_rootId);
	result->setInternalState(geometry, this);
	return result;
}

RepliesWidget::RepliesWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history,
	MsgId rootId)
: Window::SectionWidget(parent, controller)
, _history(history)
, _rootId(rootId)
, _scroll(this, st::historyScroll, false)
, _topBar(this, controller)
, _topBarShadow(this)
, _composeControls(std::make_unique<ComposeControls>(
	this,
	controller,
	ComposeControls::Mode::Normal))
, _scrollDown(_scroll, st::historyToDown) {
	_topBar->setActiveChat(_history, TopBarWidget::Section::Replies);

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();

	_topBar->sendNowSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmSendNowSelected();
	}, _topBar->lifetime());
	_topBar->deleteSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::start_with_next([=] {
		clearSelected();
	}, _topBar->lifetime());

	_topBarShadow->raise();
	updateAdaptiveLayout();
	subscribe(Adaptive::Changed(), [=] { updateAdaptiveLayout(); });

	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		controller,
		static_cast<ListDelegate*>(this)));
	_scroll->move(0, _topBar->height());
	_scroll->show();
	connect(_scroll, &Ui::ScrollArea::scrolled, [=] { onScroll(); });

	_inner->editMessageRequested(
	) | rpl::start_with_next([=](auto fullId) {
		if (const auto item = session().data().message(fullId)) {
			const auto media = item->media();
			if (media && !media->webpage()) {
				if (media->allowsEditCaption()) {
					Ui::show(Box<EditCaptionBox>(controller, item));
				}
			} else {
				_composeControls->editMessage(fullId);
			}
		}
	}, _inner->lifetime());

	setupScrollDownButton();
	setupComposeControls();
}

RepliesWidget::~RepliesWidget() = default;

void RepliesWidget::setupComposeControls() {
	_composeControls->setHistory(_history);

	_composeControls->height(
	) | rpl::start_with_next([=] {
		const auto wasMax = (_scroll->scrollTopMax() == _scroll->scrollTop());
		updateControlsGeometry();
		if (wasMax) {
			listScrollTo(_scroll->scrollTopMax());
		}
	}, lifetime());

	_composeControls->cancelRequests(
	) | rpl::start_with_next([=] {
		listCancelRequest();
	}, lifetime());

	_composeControls->sendRequests(
	) | rpl::start_with_next([=] {
		send();
	}, lifetime());

	const auto saveEditMsgRequestId = lifetime().make_state<mtpRequestId>(0);
	_composeControls->editRequests(
	) | rpl::start_with_next([=](auto data) {
		if (const auto item = session().data().message(data.fullId)) {
			edit(item, data.options, saveEditMsgRequestId);
		}
	}, lifetime());

	_composeControls->attachRequests(
	) | rpl::filter([=] {
		return !_choosingAttach;
	}) | rpl::start_with_next([=] {
		_choosingAttach = true;
		base::call_delayed(
			st::historyAttach.ripple.hideDuration,
			this,
			[=] { _choosingAttach = false; chooseAttach(); });
	}, lifetime());

	using Selector = ChatHelpers::TabbedSelector;

	_composeControls->fileChosen(
	) | rpl::start_with_next([=](Selector::FileChosen chosen) {
		sendExistingDocument(chosen.document);
	}, lifetime());

	_composeControls->photoChosen(
	) | rpl::start_with_next([=](Selector::PhotoChosen chosen) {
		sendExistingPhoto(chosen.photo);
	}, lifetime());

	_composeControls->inlineResultChosen(
	) | rpl::start_with_next([=](Selector::InlineChosen chosen) {
		sendInlineResult(chosen.result, chosen.bot);
	}, lifetime());

	_composeControls->scrollRequests(
	) | rpl::start_with_next([=](Data::MessagePosition pos) {
		showAtPosition(pos);
	}, lifetime());

	_composeControls->keyEvents(
	) | rpl::start_with_next([=](not_null<QKeyEvent*> e) {
		if (e->key() == Qt::Key_Up) {
			if (!_composeControls->isEditingMessage()) {
				// #TODO replies
				//auto &messages = session().data().scheduledMessages();
				//if (const auto item = messages.lastSentMessage(_history)) {
				//	_inner->editMessageRequestNotify(item->fullId());
				//} else {
				//	_scroll->keyPressEvent(e);
				//}
			} else {
				_scroll->keyPressEvent(e);
			}
			e->accept();
		} else if (e->key() == Qt::Key_Down) {
			_scroll->keyPressEvent(e);
			e->accept();
		}
	}, lifetime());

	_composeControls->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return CanSendFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return confirmSendingFiles(
				data,
				CompressConfirm::Auto,
				data->text());
		}
		Unexpected("action in MimeData hook.");
	});
}

void RepliesWidget::chooseAttach() {
	if (const auto error = Data::RestrictionError(
			_history->peer,
			ChatRestriction::f_send_media)) {
		ShowErrorToast(*error);
		return;
	}

	const auto filter = FileDialog::AllFilesFilter()
		+ qsl(";;Image files (*")
		+ cImgExtensions().join(qsl(" *"))
		+ qsl(")");

	FileDialog::GetOpenPaths(this, tr::lng_choose_files(tr::now), filter, crl::guard(this, [=](
			FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!result.remoteContent.isEmpty()) {
			auto animated = false;
			auto image = App::readImage(
				result.remoteContent,
				nullptr,
				false,
				&animated);
			if (!image.isNull() && !animated) {
				confirmSendingFiles(
					std::move(image),
					std::move(result.remoteContent),
					CompressConfirm::Auto);
			} else {
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			auto list = Storage::PrepareMediaList(
				result.paths,
				st::sendMediaPreviewSize);
			if (list.allFilesForCompress || list.albumIsPossible) {
				confirmSendingFiles(std::move(list), CompressConfirm::Auto);
			} else if (!showSendingFilesError(list)) {
				confirmSendingFiles(std::move(list), CompressConfirm::No);
			}
		}
	}), nullptr);
}

bool RepliesWidget::confirmSendingFiles(
		not_null<const QMimeData*> data,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	const auto hasImage = data->hasImage();

	if (const auto urls = data->urls(); !urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize);
		if (list.error != Storage::PreparedList::Error::NonLocalUrl) {
			if (list.error == Storage::PreparedList::Error::None
				|| !hasImage) {
				const auto emptyTextOnCancel = QString();
				confirmSendingFiles(
					std::move(list),
					compressed,
					emptyTextOnCancel);
				return true;
			}
		}
	}

	if (hasImage) {
		auto image = Platform::GetImageFromClipboard();
		if (image.isNull()) {
			image = qvariant_cast<QImage>(data->imageData());
		}
		if (!image.isNull()) {
			confirmSendingFiles(
				std::move(image),
				QByteArray(),
				compressed,
				insertTextOnCancel);
			return true;
		}
	}
	return false;
}

bool RepliesWidget::confirmSendingFiles(
		Storage::PreparedList &&list,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	if (showSendingFilesError(list)) {
		return false;
	}

	const auto noCompressOption = (list.files.size() > 1)
		&& !list.allFilesForCompress
		&& !list.albumIsPossible;
	const auto boxCompressConfirm = noCompressOption
		? CompressConfirm::None
		: compressed;

	//const auto cursor = _field->textCursor();
	//const auto position = cursor.position();
	//const auto anchor = cursor.anchor();
	const auto text = _composeControls->getTextWithAppliedMarkdown();//_field->getTextWithTags();
	using SendLimit = SendFilesBox::SendLimit;
	auto box = Box<SendFilesBox>(
		controller(),
		std::move(list),
		text,
		boxCompressConfirm,
		_history->peer->slowmodeApplied() ? SendLimit::One : SendLimit::Many,
		CanScheduleUntilOnline(_history->peer)
			? Api::SendType::ScheduledToUser
			: Api::SendType::Scheduled,
		SendMenu::Type::Disabled);
	//_field->setTextWithTags({});

	box->setConfirmedCallback(crl::guard(this, [=](
			Storage::PreparedList &&list,
			SendFilesWay way,
			TextWithTags &&caption,
			Api::SendOptions options,
			bool ctrlShiftEnter) {
		if (showSendingFilesError(list)) {
			return;
		}
		const auto type = (way == SendFilesWay::Files)
			? SendMediaType::File
			: SendMediaType::Photo;
		const auto album = (way == SendFilesWay::Album)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		uploadFilesAfterConfirmation(
			std::move(list),
			type,
			std::move(caption),
			MsgId(0),//replyToId(),
			options,
			album);
	}));
	//box->setCancelledCallback(crl::guard(this, [=] {
	//	_field->setTextWithTags(text);
	//	auto cursor = _field->textCursor();
	//	cursor.setPosition(anchor);
	//	if (position != anchor) {
	//		cursor.setPosition(position, QTextCursor::KeepAnchor);
	//	}
	//	_field->setTextCursor(cursor);
	//	if (!insertTextOnCancel.isEmpty()) {
	//		_field->textCursor().insertText(insertTextOnCancel);
	//	}
	//}));

	//ActivateWindow(controller());
	const auto shown = Ui::show(std::move(box));
	shown->setCloseByOutsideClick(false);

	return true;
}

bool RepliesWidget::confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	if (image.isNull()) {
		return false;
	}

	auto list = Storage::PrepareMediaFromImage(
		std::move(image),
		std::move(content),
		st::sendMediaPreviewSize);
	return confirmSendingFiles(
		std::move(list),
		compressed,
		insertTextOnCancel);
}

void RepliesWidget::uploadFilesAfterConfirmation(
		Storage::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		MsgId replyTo,
		Api::SendOptions options,
		std::shared_ptr<SendingAlbum> album) {
	const auto isAlbum = (album != nullptr);
	const auto compressImages = (type == SendMediaType::Photo);
	if (_history->peer->slowmodeApplied()
		&& ((list.files.size() > 1 && !album)
			|| (!list.files.empty()
				&& !caption.text.isEmpty()
				&& !list.canAddCaption(isAlbum, compressImages)))) {
		ShowErrorToast(tr::lng_slowmode_no_many(tr::now));
		return;
	}
	auto action = Api::SendAction(_history);
	action.replyTo = replyTo;
	action.options = options;
	session().api().sendFiles(
		std::move(list),
		type,
		std::move(caption),
		album,
		action);
}

void RepliesWidget::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	const auto callback = [=](Api::SendOptions options) {
		auto action = Api::SendAction(_history);
		//action.replyTo = replyToId();
		action.options = options;
		session().api().sendFile(fileContent, type, action);
	};
	Ui::show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

bool RepliesWidget::showSendingFilesError(
		const Storage::PreparedList &list) const {
	const auto text = [&] {
		const auto error = Data::RestrictionError(
			_history->peer,
			ChatRestriction::f_send_media);
		if (error) {
			return *error;
		}
		using Error = Storage::PreparedList::Error;
		switch (list.error) {
		case Error::None: return QString();
		case Error::EmptyFile:
		case Error::Directory:
		case Error::NonLocalUrl: return tr::lng_send_image_empty(
			tr::now,
			lt_name,
			list.errorData);
		case Error::TooLargeFile: return tr::lng_send_image_too_large(
			tr::now,
			lt_name,
			list.errorData);
		}
		return tr::lng_forward_send_files_cant(tr::now);
	}();
	if (text.isEmpty()) {
		return false;
	}

	ShowErrorToast(text);
	return true;
}

void RepliesWidget::send() {
	if (_composeControls->getTextWithAppliedMarkdown().text.isEmpty()) {
		return;
	}
	const auto callback = [=](Api::SendOptions options) { send(options); };
	Ui::show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

void RepliesWidget::send(Api::SendOptions options) {
	const auto webPageId = _composeControls->webPageId();/* _previewCancelled
		? CancelledWebPageId
		: ((_previewData && _previewData->pendingTill >= 0)
			? _previewData->id
			: WebPageId(0));*/

	auto message = ApiWrap::MessageToSend(_history);
	message.textWithTags = _composeControls->getTextWithAppliedMarkdown();
	message.action.options = options;
	//message.action.replyTo = replyToId();
	message.webPageId = webPageId;

	//const auto error = GetErrorTextForSending(
	//	_peer,
	//	_toForward,
	//	message.textWithTags);
	//if (!error.isEmpty()) {
	//	ShowErrorToast(error);
	//	return;
	//}

	session().api().sendMessage(std::move(message));

	_composeControls->clear();
	//_saveDraftText = true;
	//_saveDraftStart = crl::now();
	//onDraftSave();

	_composeControls->hidePanelsAnimated();

	//if (_previewData && _previewData->pendingTill) previewCancel();
	_composeControls->focus();
}

void RepliesWidget::edit(
		not_null<HistoryItem*> item,
		Api::SendOptions options,
		mtpRequestId *const saveEditMsgRequestId) {
	if (*saveEditMsgRequestId) {
		return;
	}
	const auto textWithTags = _composeControls->getTextWithAppliedMarkdown();
	const auto prepareFlags = Ui::ItemTextOptions(
		_history,
		session().user()).flags;
	auto sending = TextWithEntities();
	auto left = TextWithEntities {
		textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(textWithTags.tags) };
	TextUtilities::PrepareForSending(left, prepareFlags);

	if (!TextUtilities::CutPart(sending, left, MaxMessageSize)) {
		if (item) {
			Ui::show(Box<DeleteMessagesBox>(item, false));
		} else {
			_composeControls->focus();
		}
		return;
	} else if (!left.text.isEmpty()) {
		Ui::show(Box<InformBox>(tr::lng_edit_too_long(tr::now)));
		return;
	}

	lifetime().add([=] {
		if (!*saveEditMsgRequestId) {
			return;
		}
		session().api().request(base::take(*saveEditMsgRequestId)).cancel();
	});

	const auto done = [=](const MTPUpdates &result, mtpRequestId requestId) {
		if (requestId == *saveEditMsgRequestId) {
			*saveEditMsgRequestId = 0;
			_composeControls->cancelEditMessage();
		}
	};

	const auto fail = [=](const RPCError &error, mtpRequestId requestId) {
		if (requestId == *saveEditMsgRequestId) {
			*saveEditMsgRequestId = 0;
		}

		const auto &err = error.type();
		if (ranges::contains(Api::kDefaultEditMessagesErrors, err)) {
			Ui::show(Box<InformBox>(tr::lng_edit_error(tr::now)));
		} else if (err == u"MESSAGE_NOT_MODIFIED"_q) {
			_composeControls->cancelEditMessage();
		} else if (err == u"MESSAGE_EMPTY"_q) {
			_composeControls->focus();
		} else {
			Ui::show(Box<InformBox>(tr::lng_edit_error(tr::now)));
		}
		update();
		return true;
	};

	*saveEditMsgRequestId = Api::EditTextMessage(
		item,
		sending,
		options,
		crl::guard(this, done),
		crl::guard(this, fail));

	_composeControls->hidePanelsAnimated();
	_composeControls->focus();
}

void RepliesWidget::sendExistingDocument(
		not_null<DocumentData*> document) {
	const auto callback = [=](Api::SendOptions options) {
		sendExistingDocument(document, options);
	};
	Ui::show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

bool RepliesWidget::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options) {
	const auto error = Data::RestrictionError(
		_history->peer,
		ChatRestriction::f_send_stickers);
	if (error) {
		Ui::show(Box<InformBox>(*error), Ui::LayerOption::KeepOther);
		return false;
	}

	auto message = Api::MessageToSend(_history);
	//message.action.replyTo = replyToId();
	message.action.options = options;
	Api::SendExistingDocument(std::move(message), document);

	//if (_fieldAutocomplete->stickersShown()) {
	//	clearFieldText();
	//	//_saveDraftText = true;
	//	//_saveDraftStart = crl::now();
	//	//onDraftSave();
	//	onCloudDraftSave(); // won't be needed if SendInlineBotResult will clear the cloud draft
	//}

	_composeControls->hidePanelsAnimated();
	_composeControls->focus();
	return true;
}

void RepliesWidget::sendExistingPhoto(not_null<PhotoData*> photo) {
	const auto callback = [=](Api::SendOptions options) {
		sendExistingPhoto(photo, options);
	};
	Ui::show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

bool RepliesWidget::sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options) {
	const auto error = Data::RestrictionError(
		_history->peer,
		ChatRestriction::f_send_media);
	if (error) {
		Ui::show(Box<InformBox>(*error), Ui::LayerOption::KeepOther);
		return false;
	}

	auto message = Api::MessageToSend(_history);
	//message.action.replyTo = replyToId();
	message.action.options = options;
	Api::SendExistingPhoto(std::move(message), photo);

	_composeControls->hidePanelsAnimated();
	_composeControls->focus();
	return true;
}

void RepliesWidget::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot) {
	const auto errorText = result->getErrorOnSend(_history);
	if (!errorText.isEmpty()) {
		Ui::show(Box<InformBox>(errorText));
		return;
	}
	const auto callback = [=](Api::SendOptions options) {
		sendInlineResult(result, bot, options);
	};
	Ui::show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		Ui::LayerOption::KeepOther);
}

void RepliesWidget::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot,
		Api::SendOptions options) {
	auto action = Api::SendAction(_history);
	//action.replyTo = replyToId();
	action.options = options;
	action.generateLocal = true;
	session().api().sendInlineResult(bot, result, action);

	_composeControls->clear();
	//_saveDraftText = true;
	//_saveDraftStart = crl::now();
	//onDraftSave();

	auto &bots = cRefRecentInlineBots();
	const auto index = bots.indexOf(bot);
	if (index) {
		if (index > 0) {
			bots.removeAt(index);
		} else if (bots.size() >= RecentInlineBotsLimit) {
			bots.resize(RecentInlineBotsLimit - 1);
		}
		bots.push_front(bot);
		bot->session().local().writeRecentHashtagsAndBots();
	}

	_composeControls->hidePanelsAnimated();
	_composeControls->focus();
}

SendMenu::Type RepliesWidget::sendMenuType() const {
	return _history->peer->isSelf()
		? SendMenu::Type::Reminder
		: HistoryView::CanScheduleUntilOnline(_history->peer)
		? SendMenu::Type::ScheduledToUser
		: SendMenu::Type::Scheduled;
}

void RepliesWidget::setupScrollDownButton() {
	_scrollDown->setClickedCallback([=] {
		scrollDownClicked();
	});
	base::install_event_filter(_scrollDown, [=](not_null<QEvent*> event) {
		if (event->type() != QEvent::Wheel) {
			return base::EventFilterResult::Continue;
		}
		return _scroll->viewportEvent(event)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
	updateScrollDownVisibility();
}

void RepliesWidget::scrollDownClicked() {
	showAtPosition(Data::MaxMessagePosition);
}

void RepliesWidget::showAtPosition(Data::MessagePosition position) {
	if (showAtPositionNow(position)) {
		if (const auto highlight = base::take(_highlightMessageId)) {
			_inner->highlightMessage(highlight);
		}
	} else {
		_nextAnimatedScrollPosition = position;
		_nextAnimatedScrollDelta = _inner->isBelowPosition(position)
			? -_scroll->height()
			: _inner->isAbovePosition(position)
			? _scroll->height()
			: 0;
		auto memento = HistoryView::ListMemento(position);
		_inner->restoreState(&memento);
	}
}

bool RepliesWidget::showAtPositionNow(Data::MessagePosition position) {
	if (const auto scrollTop = _inner->scrollTopForPosition(position)) {
		const auto currentScrollTop = _scroll->scrollTop();
		const auto wanted = snap(*scrollTop, 0, _scroll->scrollTopMax());
		const auto fullDelta = (wanted - currentScrollTop);
		const auto limit = _scroll->height();
		const auto scrollDelta = snap(fullDelta, -limit, limit);
		_inner->animatedScrollTo(
			wanted,
			position,
			scrollDelta,
			(std::abs(fullDelta) > limit
				? HistoryView::ListWidget::AnimatedScroll::Part
				: HistoryView::ListWidget::AnimatedScroll::Full));
		return true;
	}
	return false;
}

void RepliesWidget::updateScrollDownVisibility() {
	if (animating()) {
		return;
	}

	const auto scrollDownIsVisible = [&]() -> std::optional<bool> {
		const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
		if (top < _scroll->scrollTopMax()) {
			return true;
		}
		if (_inner->loadedAtBottomKnown()) {
			return !_inner->loadedAtBottom();
		}
		return std::nullopt;
	};
	const auto scrollDownIsShown = scrollDownIsVisible();
	if (!scrollDownIsShown) {
		return;
	}
	if (_scrollDownIsShown != *scrollDownIsShown) {
		_scrollDownIsShown = *scrollDownIsShown;
		_scrollDownShown.start(
			[=] { updateScrollDownPosition(); },
			_scrollDownIsShown ? 0. : 1.,
			_scrollDownIsShown ? 1. : 0.,
			st::historyToDownDuration);
	}
}

void RepliesWidget::updateScrollDownPosition() {
	// _scrollDown is a child widget of _scroll, not me.
	auto top = anim::interpolate(
		0,
		_scrollDown->height() + st::historyToDownPosition.y(),
		_scrollDownShown.value(_scrollDownIsShown ? 1. : 0.));
	_scrollDown->moveToRight(
		st::historyToDownPosition.x(),
		_scroll->height() - top);
	auto shouldBeHidden = !_scrollDownIsShown && !_scrollDownShown.animating();
	if (shouldBeHidden != _scrollDown->isHidden()) {
		_scrollDown->setVisible(!shouldBeHidden);
	}
}

void RepliesWidget::scrollDownAnimationFinish() {
	_scrollDownShown.stop();
	updateScrollDownPosition();
}

void RepliesWidget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		Adaptive::OneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

not_null<History*> RepliesWidget::history() const {
	return _history;
}

Dialogs::RowDescriptor RepliesWidget::activeChat() const {
	return {
		_history,
		FullMsgId(_history->channelId(), ShowAtUnreadMsgId)
	};
}

QPixmap RepliesWidget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	_topBar->updateControlsVisibility();
	if (params.withTopBarShadow) _topBarShadow->hide();
	_composeControls->showForGrab();
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) _topBarShadow->show();
	return result;
}

void RepliesWidget::doSetInnerFocus() {
	_composeControls->focus();
}

bool RepliesWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto logMemento = dynamic_cast<RepliesMemento*>(memento.get())) {
		if (logMemento->getHistory() == history()) {
			restoreState(logMemento);
			return true;
		}
	}
	return false;
}

void RepliesWidget::setInternalState(
		const QRect &geometry,
		not_null<RepliesMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

bool RepliesWidget::pushTabbedSelectorToThirdSection(
		not_null<PeerData*> peer,
		const Window::SectionShow &params) {
	return _composeControls->pushTabbedSelectorToThirdSection(peer, params);
}

bool RepliesWidget::returnTabbedSelector() {
	return _composeControls->returnTabbedSelector();
}

std::unique_ptr<Window::SectionMemento> RepliesWidget::createMemento() {
	auto result = std::make_unique<RepliesMemento>(history(), _rootId);
	saveState(result.get());
	return result;
}

void RepliesWidget::saveState(not_null<RepliesMemento*> memento) {
	memento->setReplies(_replies);
	_inner->saveState(memento->list());
}

void RepliesWidget::restoreState(not_null<RepliesMemento*> memento) {
	const auto setReplies = [&](std::shared_ptr<Data::RepliesList> replies) {
		_replies = std::move(replies);

		rpl::single(
			tr::lng_contacts_loading()
		) | rpl::then(_replies->fullCount(
		) | rpl::map([=](int count) {
			return count
				? tr::lng_replies_header(
					lt_count,
					rpl::single(count) | tr::to_count())
				: tr::lng_replies_header_none();
		})) | rpl::flatten_latest(
		) | rpl::start_with_next([=](const QString &text) {
			_topBar->setCustomTitle(text);
		}, lifetime());
	};
	if (auto replies = memento->getReplies()) {
		setReplies(std::move(replies));
	} else if (!_replies) {
		setReplies(std::make_shared<Data::RepliesList>(_history, _rootId));
	}
	_inner->restoreState(memento->list());
}

void RepliesWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	_composeControls->resizeToWidth(width());
	updateControlsGeometry();
}

void RepliesWidget::updateControlsGeometry() {
	const auto contentWidth = width();

	const auto newScrollTop = _scroll->isHidden()
		? std::nullopt
		: base::make_optional(_scroll->scrollTop() + topDelta());
	_topBar->resizeToWidth(contentWidth);
	_topBarShadow->resize(contentWidth, st::lineWidth);

	const auto bottom = height();
	const auto controlsHeight = _composeControls->heightCurrent();
	const auto scrollHeight = bottom - _topBar->height() - controlsHeight;
	const auto scrollSize = QSize(contentWidth, scrollHeight);
	if (_scroll->size() != scrollSize) {
		_skipScrollEvent = true;
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
		_skipScrollEvent = false;
	}
	if (!_scroll->isHidden()) {
		if (newScrollTop) {
			_scroll->scrollToY(*newScrollTop);
		}
		updateInnerVisibleArea();
	}
	_composeControls->move(0, bottom - controlsHeight);

	updateScrollDownPosition();
}

void RepliesWidget::paintEvent(QPaintEvent *e) {
	if (animating()) {
		SectionWidget::paintEvent(e);
		return;
	}
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}
	//if (hasPendingResizedItems()) {
	//	updateListSize();
	//}

	//auto ms = crl::now();
	//_historyDownShown.step(ms);

	SectionWidget::PaintBackground(controller(), this, e->rect());
}

void RepliesWidget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void RepliesWidget::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	updateScrollDownVisibility();
}

void RepliesWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
	_composeControls->showStarted();
}

void RepliesWidget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
	_composeControls->showFinished();

	// We should setup the drag area only after
	// the section animation is finished,
	// because after that the method showChildren() is called.
	setupDragArea();
}

bool RepliesWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect RepliesWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

Context RepliesWidget::listContext() {
	return Context::History;
}

void RepliesWidget::listScrollTo(int top) {
	if (_scroll->scrollTop() != top) {
		_scroll->scrollToY(top);
	} else {
		updateInnerVisibleArea();
	}
}

void RepliesWidget::listCancelRequest() {
	if (_inner && !_inner->getSelectedItems().empty()) {
		clearSelected();
		return;
	}
	if (_composeControls->isEditingMessage()) {
		_composeControls->cancelEditMessage();
		return;
	}
	controller()->showBackFromStack();
}

void RepliesWidget::listDeleteRequest() {
	confirmDeleteSelected();
}

rpl::producer<Data::MessagesSlice> RepliesWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	return _replies->source(aroundId, limitBefore, limitAfter);
}

void RepliesWidget::highlightSingleNewMessage(
		const Data::MessagesSlice &slice) {
	const auto guard = gsl::finally([&] { _lastSlice = slice; });
	if (_lastSlice.ids.empty()
		|| (slice.ids.size() != _lastSlice.ids.size() + 1)) {
		return;
	}
	auto firstDifferent = 0;
	while (firstDifferent != _lastSlice.ids.size()) {
		if (slice.ids[firstDifferent] != _lastSlice.ids[firstDifferent]) {
			break;
		}
		++firstDifferent;
	}
	auto lastDifferent = slice.ids.size() - 1;
	while (lastDifferent != firstDifferent) {
		if (slice.ids[lastDifferent] != _lastSlice.ids[lastDifferent - 1]) {
			break;
		}
		--lastDifferent;
	}
	if (firstDifferent != lastDifferent) {
		return;
	}
	const auto newId = slice.ids[firstDifferent];
	if (const auto item = session().data().message(newId)) {
	//	_highlightMessageId = newId;
		showAtPosition(item->position());
	}
}

bool RepliesWidget::listAllowsMultiSelect() {
	return true;
}

bool RepliesWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return !item->isSending() && !item->hasFailed();
}

bool RepliesWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void RepliesWidget::listSelectionChanged(SelectedItems &&items) {
	HistoryView::TopBarWidget::SelectedState state;
	state.count = items.size();
	for (const auto item : items) {
		if (item.canDelete) {
			++state.canDeleteCount;
		}
		if (item.canSendNow) {
			++state.canSendNowCount;
		}
	}
	_topBar->showSelected(state);
}

void RepliesWidget::listVisibleItemsChanged(HistoryItemsList &&items) {
}

std::optional<int> RepliesWidget::listUnreadBarView(
		const std::vector<not_null<Element*>> &elements) {
	return std::nullopt;
}

void RepliesWidget::listContentRefreshed() {
}

ClickHandlerPtr RepliesWidget::listDateLink(not_null<Element*> view) {
	return nullptr;
}

void RepliesWidget::confirmSendNowSelected() {
	auto items = _inner->getSelectedItems();
	if (items.empty()) {
		return;
	}
	const auto navigation = controller();
	Window::ShowSendNowMessagesBox(
		navigation,
		_history,
		std::move(items),
		[=] { navigation->showBackFromStack(); });
}

void RepliesWidget::confirmDeleteSelected() {
	auto items = _inner->getSelectedItems();
	if (items.empty()) {
		return;
	}
	const auto weak = Ui::MakeWeak(this);
	const auto box = Ui::show(Box<DeleteMessagesBox>(
		&_history->session(),
		std::move(items)));
	box->setDeleteConfirmedCallback([=] {
		if (const auto strong = weak.data()) {
			strong->clearSelected();
		}
	});
}

void RepliesWidget::clearSelected() {
	_inner->cancelSelection();
}

void RepliesWidget::setupDragArea() {
	const auto areas = DragArea::SetupDragAreaToContainer(
		this,
		[=](not_null<const QMimeData*> d) { return _history; },
		nullptr,
		[=] { updateControlsGeometry(); });

	const auto droppedCallback = [=](CompressConfirm compressed) {
		return [=](const QMimeData *data) {
			confirmSendingFiles(data, compressed);
			Window::ActivateWindow(controller());
		};
	};
	areas.document->setDroppedCallback(droppedCallback(CompressConfirm::No));
	areas.photo->setDroppedCallback(droppedCallback(CompressConfirm::Yes));
}

} // namespace HistoryView
