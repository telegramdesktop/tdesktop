/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_welcome_messages_section.h"

#include "api/api_sending.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "boxes/delete_messages_box.h"
#include "boxes/send_files_box.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/file_utilities.h"
#include "data/components/welcome_messages.h"
#include "data/data_chat_participant_status.h"
#include "data/data_message_reactions.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_sticker_toast.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/history.h"
#include "history/history_item_helpers.h"
#include "history/history_view_swipe_back_session.h"
#include "inline_bots/inline_bot_result.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/chat_style.h"
#include "ui/image/image_prepare.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/shadow.h"
#include "ui/screen_reader_mode.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView {
namespace {

[[nodiscard]] QSize FillEmptyText(
		Ui::Text::String &text,
		const style::TextStyle &st,
		const QString &value) {
	const auto padding = st::welcomeEmptyPadding;
	const auto minWidth = st::welcomeEmptyWidth / 4;
	const auto maxWidth = std::max(
		minWidth + 1,
		st::welcomeEmptyWidth - padding.left() - padding.right());
	text = Ui::Text::String(st, value, kPlainTextOptions, minWidth);
	return Ui::Text::CountOptimalTextSize(text, minWidth, maxWidth);
}

} // namespace

WelcomeMessagesMemento::WelcomeMessagesMemento(not_null<History*> history)
: _history(history) {
	const auto list = _history->session().welcomeMessages().list(_history);
	if (!list.ids.empty()) {
		_list.setScrollTopState({ .item = { .fullId = list.ids.front() } });
	}
}

object_ptr<Window::SectionWidget> WelcomeMessagesMemento::createWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Window::Column column,
	const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<WelcomeMessagesWidget>(
		parent,
		controller,
		_history);
	result->setInternalState(geometry, this);
	return result;
}

WelcomeMessagesWidget::WelcomeMessagesWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: Window::SectionWidget(parent, controller, history->peer)
, WindowListDelegate(controller)
, _history(history)
, _scroll(
	this,
	controller->chatStyle()->value(lifetime(), st::historyScroll))
, _topBar(this, controller)
, _topBarShadow(this)
, _composeControls(std::make_unique<ComposeControls>(
	this,
	ComposeControlsDescriptor{
		.show = controller->uiShow(),
		.unavailableEmojiPasted = [=](not_null<DocumentData*> emoji) {
			listShowPremiumToast(emoji);
		},
		.mode = ComposeControls::Mode::Normal,
		.sendMenuDetails = [] { return SendMenu::Details(); },
		.regularWindow = controller,
		.stickerOrEmojiChosen = controller->stickerOrEmojiChosen(),
		.customPlaceholder = tr::lng_welcome_messages_placeholder(),
		.features = {
			.sendAs = false,
			.ttlInfo = false,
			.attachments = true,
			.botCommandSend = false,
			.silentBroadcastToggle = false,
			.attachBotsMenu = false,
			.inlineBots = false,
			.megagroupSet = false,
			.commonTabbedPanel = false,
			.recordMediaMessage = false,
			.emojiOnlyPanel = false,
		},
	}))
, _cornerButtons(
	_scroll.data(),
	controller->chatStyle(),
	static_cast<HistoryView::CornerButtonsDelegate*>(this)) {
	refreshEmptyText();

	controller->chatStyle()->paletteChanged(
	) | rpl::on_next([=] {
		_scroll->updateBars();
	}, _scroll->lifetime());

	Window::ChatThemeValueFromPeer(
		controller,
		history->peer
	) | rpl::on_next([=](std::shared_ptr<Ui::ChatTheme> &&theme) {
		_theme = std::move(theme);
		controller->setChatStyleTheme(_theme);
	}, lifetime());

	const auto state = Dialogs::EntryState{
		.key = _history,
		.section = Dialogs::EntryState::Section::WelcomeMessages,
	};
	_topBar->setActiveChat(state, nullptr);
	_composeControls->setCurrentDialogsEntryState(state);
	controller->setDialogsEntryState(state);

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();

	_topBar->deleteSelectionRequest(
	) | rpl::on_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::on_next([=] {
		clearSelected();
	}, _topBar->lifetime());

	_topBarShadow->raise();
	controller->adaptive().value(
	) | rpl::on_next([=] {
		updateAdaptiveLayout();
	}, lifetime());

	_scroll->setHandleTouch(false);
	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		&controller->session(),
		static_cast<ListDelegate*>(this)));
	_inner->lower();
	_scroll->move(0, _topBar->height());
	_scroll->show();
	_scroll->setOverscrollBg(QColor(0, 0, 0, 0));
	_scroll->setOverscrollEdges([=] {
		return _inner->loadedAtTopKnown() && _inner->loadedAtTop();
	}, [=] {
		return _inner->loadedAtBottomKnown() && _inner->loadedAtBottom();
	});
	_scroll->scrolls(
	) | rpl::on_next([=] {
		onScroll();
	}, lifetime());

	_inner->editMessageRequested(
	) | rpl::on_next([=](auto fullId) {
		if (const auto item = session().data().message(fullId)) {
			const auto media = item->media();
			if (!media || media->webpage() || media->allowsEditCaption()) {
				_composeControls->editMessage(
					fullId,
					_inner->getSelectedTextRange(item));
				if (_composeControls->isEditingMessage()) {
					doSetInnerFocus();
				}
			}
		}
	}, _inner->lifetime());

	setupComposeControls();
	Window::SetupSwipeBackSection(this, _scroll, _inner);
}

WelcomeMessagesWidget::~WelcomeMessagesWidget() {
	if (_notice) {
		_notice->destroy();
	}
}

void WelcomeMessagesWidget::setupComposeControls() {
	auto writeRestriction = rpl::combine(
		_count.value(),
		Data::WelcomeMessagesLimitValue(&session()),
		_composeControls->editMsgIdValue()
	) | rpl::map([=](int count, int limit, FullMsgId editing) {
		return (count >= limit && !editing)
			? Controls::WriteRestriction{
				.text = tr::lng_business_limit_reached(
					tr::now,
					lt_count,
					limit),
				.type = Controls::WriteRestrictionType::Rights,
			} : Controls::WriteRestriction();
	});
	rpl::duplicate(
		writeRestriction
	) | rpl::map([](const Controls::WriteRestriction &restriction) {
		return !restriction.empty();
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](bool restricted) {
		if (restricted) {
			_inner->setFocus();
		} else {
			crl::on_main(this, [=] {
				doSetInnerFocus();
			});
		}
	}, lifetime());
	_composeControls->setPasteToastParent(_scroll.data());
	_composeControls->setHistory({
		.history = _history.get(),
		.sendActionFactory = [=] { return Api::SendAction(_history); },
		.writeRestriction = std::move(writeRestriction),
	});

	_composeControls->height(
	) | rpl::on_next([=] {
		const auto wasMax = (_scroll->scrollTopMax() == _scroll->scrollTop());
		updateControlsGeometry();
		if (wasMax) {
			listScrollTo(_scroll->scrollTopMax());
		}
	}, lifetime());

	_composeControls->cancelRequests(
	) | rpl::on_next([=] {
		listCancelRequest();
	}, lifetime());

	_composeControls->sendRequests(
	) | rpl::on_next([=] {
		send();
	}, lifetime());

	_composeControls->attachRequests(
	) | rpl::filter([=] {
		return !_choosingAttach;
	}) | rpl::on_next([=](std::optional<bool> overrideCompress) {
		if (!checkLimit()) {
			return;
		}
		_choosingAttach = true;
		base::call_delayed(st::historyAttach.ripple.hideDuration, this, [=] {
			_choosingAttach = false;
			chooseAttach(overrideCompress);
		});
	}, lifetime());

	_composeControls->setSendAsFileConfirmed(crl::guard(this, [=](
			std::shared_ptr<Ui::PreparedBundle> bundle,
			Api::SendOptions options) {
		sendingFilesConfirmed(std::move(bundle), options);
	}));

	_composeControls->fileChosen(
	) | rpl::on_next([=](ChatHelpers::FileChosen chosen) {
		sendExistingDocument(
			chosen.document,
			chosen.options,
			std::move(chosen.caption));
	}, lifetime());

	_composeControls->photoChosen(
	) | rpl::on_next([=](ChatHelpers::PhotoChosen chosen) {
		sendExistingPhoto(chosen.photo, chosen.options);
	}, lifetime());

	_composeControls->inlineResultChosen(
	) | rpl::on_next([=](ChatHelpers::InlineChosen chosen) {
		sendInlineResult(std::move(chosen.result), chosen.options);
	}, lifetime());

	_composeControls->editRequests(
	) | rpl::on_next([=](auto data) {
		if (const auto item = session().data().message(data.fullId)) {
			if (item->isWelcomeTemplate()) {
				edit(item);
			}
		}
	}, lifetime());

	_composeControls->jumpToItemRequests(
	) | rpl::on_next([=](FullReplyTo to) {
		if (const auto item = session().data().message(to.messageId)) {
			if (item->isWelcomeTemplate() && item->history() == _history) {
				showAtPosition(item->position());
			}
		}
	}, lifetime());

	rpl::merge(
		_composeControls->scrollKeyEvents(),
		_inner->scrollKeyEvents()
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		_scroll->keyPressEvent(e);
	}, lifetime());

	_composeControls->editLastMessageRequests(
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		if (!_inner->lastMessageEditRequestNotify()) {
			_scroll->keyPressEvent(e);
		}
	}, lifetime());

	_composeControls->lockShowStarts(
	) | rpl::on_next([=] {
		_cornerButtons.updateJumpDownVisibility();
		_cornerButtons.updateUnreadThingsVisibility();
	}, lifetime());

	_composeControls->viewportEvents(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		_scroll->viewportEvent(e);
	}, lifetime());
}

void WelcomeMessagesWidget::refreshEmptyText() {
	_emptyTitleSize = FillEmptyText(
		_emptyTitle,
		st::welcomeEmptyTitle,
		tr::lng_welcome_messages_empty_title(tr::now));
	_emptyAboutSize = FillEmptyText(
		_emptyAbout,
		st::welcomeEmptyAbout,
		tr::lng_welcome_messages_empty_about(tr::now));
}

void WelcomeMessagesWidget::checkReplyReturns() {
	const auto currentTop = _scroll->scrollTop();
	while (const auto replyReturn = _cornerButtons.replyReturn()) {
		const auto position = replyReturn->position();
		const auto scrollTop = _inner->scrollTopForPosition(position);
		const auto below = scrollTop
			? (currentTop >= std::min(*scrollTop, _scroll->scrollTopMax()))
			: _inner->isBelowPosition(position);
		if (below) {
			_cornerButtons.calculateNextReplyReturn();
		} else {
			break;
		}
	}
}

void WelcomeMessagesWidget::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	if (!checkLimit()) {
		return;
	}
	session().api().sendFile(
		fileContent,
		type,
		prepareSendAction({}));
}

bool WelcomeMessagesWidget::showSendingFilesError(
		const Ui::PreparedList &list) const {
	return Data::ShowSendError(
		controller()->uiShow(),
		_history->peer,
		list,
		std::nullopt,
		true);
}

bool WelcomeMessagesWidget::showSendingFilesError(
		const Ui::PreparedBundle &bundle) const {
	return Data::ShowSendError(
		controller()->uiShow(),
		_history->peer,
		bundle,
		true);
}

bool WelcomeMessagesWidget::confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel) {
	if (!_history->peer->canManageWelcomeMessages()
		|| !checkLimit()
		|| showSendingFilesError(list)) {
		return false;
	}

	const auto show = controller()->uiShow();
	auto box = Box<SendFilesBox>(SendFilesBoxDescriptor{
		.show = show,
		.list = std::move(list),
		.caption = _composeControls->getTextWithAppliedMarkdown(),
		.toPeer = _history->peer,
		.limits = SendFilesAllow::OnlyOne
			| SendFilesAllow::Photos
			| SendFilesAllow::Videos
			| SendFilesAllow::Files,
		.check = DefaultCheckForPeer(show, _history->peer),
		.sendType = Api::SendType::Normal,
		.sendMenuDetails = [=] { return sendMenuDetails(); },
		.confirmed = crl::guard(this, [=](
				std::shared_ptr<Ui::PreparedBundle> bundle,
				Api::SendOptions options,
				FullReplyTo) {
			sendingFilesConfirmed(std::move(bundle), options);
		}),
		.cancelled = _composeControls->restoreTextCallback(
			insertTextOnCancel),
	});
	box->takeTextWithTagsRequests(
	) | rpl::on_next([=](TextWithTags &&text) {
		_composeControls->setText(std::move(text));
	}, box->lifetime());
	show->show(std::move(box));
	return true;
}

bool WelcomeMessagesWidget::confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	if (image.isNull()) {
		return false;
	}
	auto list = Storage::PrepareMediaFromImage(
		std::move(image),
		std::move(content),
		st::sendMediaPreviewSize);
	list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
	return confirmSendingFiles(std::move(list), insertTextOnCancel);
}

void WelcomeMessagesWidget::sendingFilesConfirmed(
		std::shared_ptr<Ui::PreparedBundle> bundle,
		Api::SendOptions options) {
	if (!_history->peer->canManageWelcomeMessages()
		|| !checkLimit()
		|| showSendingFilesError(*bundle)) {
		return;
	}
	if (bundle->totalCount != 1
		|| bundle->groups.size() != 1
		|| bundle->groups.front().list.files.size() != 1) {
		controller()->showToast(tr::lng_send_media_invalid_files(tr::now));
		return;
	}
	const auto type = bundle->way.sendImagesAsPhotos()
		? SendMediaType::Photo
		: SendMediaType::File;
	auto action = prepareSendAction(options);
	action.clearDraft = false;
	session().api().sendFiles(
		std::move(bundle->groups.front().list),
		type,
		nullptr,
		action);
	finishSending();
}

void WelcomeMessagesWidget::chooseAttach(
		std::optional<bool> overrideSendImagesAsPhotos) {
	if (!_history->peer->canManageWelcomeMessages() || !checkLimit()) {
		return;
	}
	const auto filter = (overrideSendImagesAsPhotos == true)
		? FileDialog::PhotoVideoFilesFilter()
		: FileDialog::AllOrImagesFilter();
	FileDialog::GetOpenPaths(
		this,
		tr::lng_choose_files(tr::now),
		filter,
		crl::guard(this, [=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty()
				&& result.remoteContent.isEmpty()) {
				return;
			}
			if (!checkLimit()) {
				return;
			}
			if (!result.remoteContent.isEmpty()) {
				auto read = Images::Read({
					.content = result.remoteContent,
				});
				if (!read.image.isNull() && !read.animated) {
					confirmSendingFiles(
						std::move(read.image),
						std::move(result.remoteContent),
						overrideSendImagesAsPhotos);
				} else {
					uploadFile(
						result.remoteContent,
						SendMediaType::File);
				}
			} else {
				auto list = Storage::PrepareMediaList(
					result.paths,
					st::sendMediaPreviewSize,
					session().user()->isPremium());
				list.overrideSendImagesAsPhotos
					= overrideSendImagesAsPhotos;
				confirmSendingFiles(std::move(list));
			}
		}));
}

bool WelcomeMessagesWidget::checkLimit() const {
	const auto limit = Data::WelcomeMessagesLimit(&session());
	if (session().welcomeMessages().count(_history) < limit) {
		return true;
	}
	controller()->showToast(tr::lng_business_limit_reached(
		tr::now,
		lt_count,
		limit));
	return false;
}

Api::SendAction WelcomeMessagesWidget::prepareSendAction(
		Api::SendOptions options) const {
	auto result = Api::SendAction(_history, options);
	result.options.welcomeTemplate = true;
	return result;
}

bool WelcomeMessagesWidget::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options,
		TextWithTags caption) {
	if (!_history->peer->canManageWelcomeMessages() || !checkLimit()) {
		return false;
	}
	auto message = Api::MessageToSend(prepareSendAction(options));
	message.textWithTags = std::move(caption);
	Api::SendExistingDocument(std::move(message), document);
	_composeControls->clearFieldAfterStickerSend();
	finishSending();
	return true;
}

bool WelcomeMessagesWidget::sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options) {
	if (!_history->peer->canManageWelcomeMessages() || !checkLimit()) {
		return false;
	}
	Api::SendExistingPhoto(
		Api::MessageToSend(prepareSendAction(options)),
		photo);
	finishSending();
	return true;
}

void WelcomeMessagesWidget::sendInlineResult(
		std::shared_ptr<InlineBots::Result> result,
		Api::SendOptions options) {
	if (const auto error = result->getErrorOnSend(_history)) {
		Data::ShowSendErrorToast(controller(), _history->peer, error);
		return;
	}
	const auto request = result->openRequest();
	if (const auto document = request.document()) {
		sendExistingDocument(document, options, {});
	} else if (const auto photo = request.photo()) {
		sendExistingPhoto(photo, options);
	}
}

void WelcomeMessagesWidget::finishSending() {
	_composeControls->hidePanelsAnimated();
	doSetInnerFocus();
}

void WelcomeMessagesWidget::send() {
	const auto textWithTags
		= _composeControls->getTextWithAppliedMarkdown();
	auto text = TextWithEntities{
		textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(textWithTags.tags),
	};
	TextUtilities::Trim(text);
	if (text.text.isEmpty()) {
		return;
	}
	if (!_history->peer->canManageWelcomeMessages() || !checkLimit()) {
		return;
	}
	session().welcomeMessages().send(_history, std::move(text));
	_composeControls->clear();
	finishSending();
}

void WelcomeMessagesWidget::edit(not_null<HistoryItem*> item) {
	if (!_history->peer->canManageWelcomeMessages()) {
		return;
	}
	auto sending = _composeControls->prepareTextForEditMsg();
	TextUtilities::Trim(sending);
	const auto hasMediaWithCaption = item->media()
		&& item->media()->allowsEditCaption();
	if (sending.text.isEmpty() && !hasMediaWithCaption) {
		controller()->show(Box<DeleteMessagesBox>(item));
		return;
	}
	const auto limits = Data::PremiumLimits(&session());
	const auto maxTextSize = hasMediaWithCaption
		? limits.captionLengthCurrent()
		: limits.messageLengthCurrent();
	const auto remove = int(sending.text.size()) - maxTextSize;
	if (remove > 0) {
		controller()->showToast(
			tr::lng_edit_limit_reached(tr::now, lt_count, remove));
		return;
	}
	const auto id = session().welcomeMessages().lookupId(item);
	session().welcomeMessages().edit(
		_history,
		id,
		std::move(sending),
		crl::guard(this, [=] {
			_composeControls->cancelEditMessage();
		}),
		crl::guard(this, [=](const QString &error) {
			if (error == u"MESSAGE_NOT_MODIFIED"_q) {
				_composeControls->cancelEditMessage();
			} else {
				controller()->showToast(tr::lng_edit_error(tr::now));
			}
		}));
	_composeControls->hidePanelsAnimated();
	doSetInnerFocus();
}

SendMenu::Details WelcomeMessagesWidget::sendMenuDetails() const {
	return SendMenu::Details();
}

bool WelcomeMessagesWidget::processChosenSticker(
		ChatHelpers::FileChosen &&chosen) {
	_composeControls->processChosenSticker(std::move(chosen));
	return true;
}

void WelcomeMessagesWidget::cornerButtonsShowAtPosition(
		Data::MessagePosition position) {
	showAtPosition(position);
}

Data::Thread *WelcomeMessagesWidget::cornerButtonsThread() {
	return _history;
}

FullMsgId WelcomeMessagesWidget::cornerButtonsCurrentId() {
	return {};
}

bool WelcomeMessagesWidget::cornerButtonsIgnoreVisibility() {
	return animatingShow();
}

std::optional<bool> WelcomeMessagesWidget::cornerButtonsDownShown() {
	if (_composeControls->isLockPresent()
		|| _composeControls->isTTLButtonShown()) {
		return false;
	}
	const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
	if (top < _scroll->scrollTopMax() || _cornerButtons.replyReturn()) {
		return true;
	} else if (_inner->loadedAtBottomKnown()) {
		return !_inner->loadedAtBottom();
	}
	return std::nullopt;
}

bool WelcomeMessagesWidget::cornerButtonsUnreadMayBeShown() {
	return _inner->loadedAtBottomKnown()
		&& !_composeControls->isLockPresent()
		&& !_composeControls->isTTLButtonShown();
}

bool WelcomeMessagesWidget::cornerButtonsHas(CornerButtonType type) {
	return (type == CornerButtonType::Down);
}

void WelcomeMessagesWidget::showAtPosition(
		Data::MessagePosition position,
		FullMsgId originId) {
	_inner->showAtPosition(
		position,
		{},
		_cornerButtons.doneJumpFrom(position.fullId, originId));
}

void WelcomeMessagesWidget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		controller()->adaptive().isOneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

not_null<History*> WelcomeMessagesWidget::history() const {
	return _history;
}

Dialogs::RowDescriptor WelcomeMessagesWidget::activeChat() const {
	return {
		_history,
		FullMsgId(_history->peer->id, ShowAtUnreadMsgId)
	};
}

bool WelcomeMessagesWidget::preventsClose(
		Fn<void()> &&continueCallback) const {
	return _composeControls->preventsClose(std::move(continueCallback));
}

QPixmap WelcomeMessagesWidget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	_topBar->updateControlsVisibility();
	if (params.withTopBarShadow) {
		_topBarShadow->hide();
	}
	_composeControls->showForGrab();
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
	return result;
}

void WelcomeMessagesWidget::checkActivation() {
	_inner->checkActivation();
}

void WelcomeMessagesWidget::doSetInnerFocus() {
	if (!_composeControls->focus()) {
		_inner->setFocus();
	}
}

bool WelcomeMessagesWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	const auto welcomeMemento = dynamic_cast<WelcomeMessagesMemento*>(
		memento.get());
	if (welcomeMemento && (welcomeMemento->getHistory() == history())) {
		restoreState(welcomeMemento);
		if (params.reapplyLocalDraft) {
			_composeControls->applyDraft(
				ComposeControls::FieldHistoryAction::NewEntry);
		}
		return true;
	}
	return false;
}

void WelcomeMessagesWidget::setInternalState(
		const QRect &geometry,
		not_null<WelcomeMessagesMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

bool WelcomeMessagesWidget::pushTabbedSelectorToThirdSection(
		not_null<Data::Thread*> thread,
		const Window::SectionShow &params) {
	return _composeControls->pushTabbedSelectorToThirdSection(
		thread,
		params);
}

bool WelcomeMessagesWidget::returnTabbedSelector() {
	return _composeControls->returnTabbedSelector();
}

auto WelcomeMessagesWidget::createMemento()
-> std::shared_ptr<Window::SectionMemento> {
	auto result = std::make_shared<WelcomeMessagesMemento>(history());
	saveState(result.get());
	return result;
}

void WelcomeMessagesWidget::saveState(
		not_null<WelcomeMessagesMemento*> memento) {
	_inner->saveState(memento->list());
}

void WelcomeMessagesWidget::restoreState(
		not_null<WelcomeMessagesMemento*> memento) {
	_inner->restoreState(memento->list());
}

void WelcomeMessagesWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	_composeControls->resizeToWidth(width());
	updateControlsGeometry();
}

void WelcomeMessagesWidget::updateControlsGeometry() {
	const auto contentWidth = width();

	const auto newScrollTop = _scroll->isHidden()
		? std::nullopt
		: base::make_optional(_scroll->scrollTop() + takeTopDelta());
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
	_composeControls->setAutocompleteBoundingRect(_scroll->geometry());

	_cornerButtons.updatePositions();
}

void WelcomeMessagesWidget::paintEvent(QPaintEvent *e) {
	if (animatingShow()) {
		SectionWidget::paintEvent(e);
		return;
	} else if (controller()->contentOverlapped(this, e)) {
		return;
	}
	const auto clip = e->rect();
	SectionWidget::PaintBackground(controller(), _theme.get(), this, clip);
}

void WelcomeMessagesWidget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void WelcomeMessagesWidget::updateInnerVisibleArea() {
	if (!_inner->animatedScrolling()) {
		checkReplyReturns();
	}
	const auto scrollTop = _scroll->scrollTop();
	const auto scrollBottom = scrollTop + _scroll->height();
	_inner->setVisibleTopBottom(scrollTop, scrollBottom);
	_cornerButtons.updateJumpDownVisibility();
	_cornerButtons.updateUnreadThingsVisibility();
}

void WelcomeMessagesWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
	_composeControls->showStarted();
}

void WelcomeMessagesWidget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
	_composeControls->showFinished();
	_inner->showFinished();
}

bool WelcomeMessagesWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect WelcomeMessagesWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

Context WelcomeMessagesWidget::listContext() {
	return Context::WelcomeMessages;
}

bool WelcomeMessagesWidget::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	if (_scroll->scrollTop() == top) {
		updateInnerVisibleArea();
		return false;
	}
	_scroll->scrollToY(top);
	return true;
}

void WelcomeMessagesWidget::listCancelRequest() {
	if (_inner && !_inner->getSelectedItems().empty()) {
		clearSelected();
		return;
	} else if (_composeControls->handleCancelRequest()) {
		return;
	}
	controller()->showBackFromStack();
}

void WelcomeMessagesWidget::listDeleteRequest() {
	confirmDeleteSelected();
}

void WelcomeMessagesWidget::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
	_composeControls->tryProcessKeyInput(e);
}

not_null<HistoryItem*> WelcomeMessagesWidget::noticeItem() {
	if (!_notice) {
		_notice = _history->makeMessage({
			.id = _history->nextNonHistoryEntryId(),
			.flags = MessageFlag::FakeHistoryItem,
		}, PreparedServiceText{
			tr::lng_welcome_messages_preview_about(tr::now, tr::marked),
		});
	}
	return _notice;
}

rpl::producer<Data::MessagesSlice> WelcomeMessagesWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto session = &controller()->session();
	return rpl::single(rpl::empty) | rpl::then(
		session->welcomeMessages().updates(_history)
	) | rpl::map([=] {
		auto result = session->welcomeMessages().list(_history);
		if (!result.ids.empty()) {
			result.ids.insert(begin(result.ids), noticeItem()->fullId());
		}
		return result;
	}) | rpl::after_next([=](const Data::MessagesSlice &slice) {
		_count = slice.fullCount.value_or(0);
		highlightSingleNewMessage(slice);
	});
}

void WelcomeMessagesWidget::highlightSingleNewMessage(
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
		showAtPosition(item->position());
	}
}

bool WelcomeMessagesWidget::listAllowsMultiSelect() {
	return true;
}

bool WelcomeMessagesWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return !item->isService()
		&& !item->isSending()
		&& !item->hasFailed();
}

bool WelcomeMessagesWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void WelcomeMessagesWidget::listSelectionChanged(SelectedItems &&items) {
	HistoryView::TopBarWidget::SelectedState state;
	state.count = items.size();
	for (const auto &item : items) {
		if (item.canDelete) {
			++state.canDeleteCount;
		}
	}
	_topBar->showSelected(state);
	if (items.empty()
		&& !(_inner->hasFocus() && Ui::ScreenReaderModeActive())) {
		doSetInnerFocus();
	}
}

void WelcomeMessagesWidget::listMarkReadTill(not_null<HistoryItem*> item) {
}

void WelcomeMessagesWidget::listMarkContentsRead(
	const base::flat_set<not_null<HistoryItem*>> &items) {
}

MessagesBarData WelcomeMessagesWidget::listMessagesBar(
		const std::vector<not_null<Element*>> &elements,
		bool markLastAsRead) {
	return {};
}

void WelcomeMessagesWidget::listContentRefreshed() {
}

void WelcomeMessagesWidget::listUpdateDateLink(
	ClickHandlerPtr &link,
	not_null<Element*> view) {
}

bool WelcomeMessagesWidget::listElementHideReply(
		not_null<const Element*> view) {
	return false;
}

bool WelcomeMessagesWidget::listElementShownUnread(
		not_null<const Element*> view) {
	return true;
}

bool WelcomeMessagesWidget::listIsGoodForAroundPosition(
		not_null<const Element*> view) {
	return true;
}

bool WelcomeMessagesWidget::showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) {
	if (peerId != _history->peer->id) {
		return false;
	}
	const auto id = FullMsgId(_history->peer->id, messageId);
	const auto message = _history->owner().message(id);
	if (!message || !_inner->viewByPosition(message->position())) {
		return false;
	}

	const auto originItem = [&]() -> HistoryItem* {
		using OriginMessage = Window::SectionShow::OriginMessage;
		if (const auto origin = std::get_if<OriginMessage>(&params.origin)) {
			if (const auto returnTo = session().data().message(origin->id)) {
				if (_inner->viewByPosition(returnTo->position())
					&& _cornerButtons.replyReturn() != returnTo) {
					return returnTo;
				}
			}
		}
		return nullptr;
	}();
	showAtPosition(
		message->position(),
		originItem ? originItem->fullId() : FullMsgId());
	return true;
}

Window::SectionActionResult WelcomeMessagesWidget::sendBotCommand(
		Bot::SendCommandRequest request) {
	if (request.peer != _history->peer) {
		return Window::SectionActionResult::Ignore;
	}
	return Window::SectionActionResult::Handle;
}

void WelcomeMessagesWidget::listSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void WelcomeMessagesWidget::listSearch(
		const QString &query,
		const FullMsgId &context) {
	const auto inChat = _history->peer->isUser()
		? Dialogs::Key()
		: Dialogs::Key(_history);
	controller()->searchMessages(query, inChat);
}

void WelcomeMessagesWidget::listHandleViaClick(not_null<UserData*> bot) {
}

not_null<Ui::ChatTheme*> WelcomeMessagesWidget::listChatTheme() {
	return _theme.get();
}

CopyRestrictionType WelcomeMessagesWidget::listCopyRestrictionType(
		HistoryItem *item) {
	return CopyRestrictionType::None;
}

CopyRestrictionType WelcomeMessagesWidget::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	return CopyRestrictionType::None;
}

CopyRestrictionType WelcomeMessagesWidget::listSelectRestrictionType() {
	return CopyRestrictionType::None;
}

auto WelcomeMessagesWidget::listAllowedReactionsValue()
-> rpl::producer<Data::AllowedReactions> {
	return rpl::single(Data::AllowedReactions());
}

void WelcomeMessagesWidget::listShowPremiumToast(
		not_null<DocumentData*> document) {
	if (!_stickerToast) {
		_stickerToast = std::make_unique<HistoryView::StickerToast>(
			controller(),
			this,
			[=] { _stickerToast = nullptr; });
	}
	_stickerToast->showFor(document);
}

void WelcomeMessagesWidget::listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	controller()->openPhoto(photo, { .id = context });
}

void WelcomeMessagesWidget::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	controller()->openDocument(document, showInMediaView, { .id = context });
}

void WelcomeMessagesWidget::listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) {
	const auto &icon = st::welcomeEmptyIcon;
	const auto fg = context.st->msgServiceFg();
	const auto outer = _inner->size();
	const auto width = st::welcomeEmptyWidth;
	const auto padding = st::welcomeEmptyPadding;
	const auto height = padding.top()
		+ icon.height()
		+ st::welcomeEmptyIconSkip
		+ _emptyTitleSize.height()
		+ st::welcomeEmptyTextSkip
		+ _emptyAboutSize.height()
		+ padding.bottom();
	const auto r = QRect(
		(outer.width() - width) / 2,
		(outer.height() - height) / 2,
		width,
		height);
	ServiceMessagePainter::PaintBubble(p, context.st, r);

	auto top = r.y() + padding.top();
	icon.paint(
		p,
		r.x() + (r.width() - icon.width()) / 2,
		top,
		outer.width(),
		fg->c);
	top += icon.height() + st::welcomeEmptyIconSkip;

	p.setPen(fg);
	_emptyTitle.draw(
		p,
		r.x() + (r.width() - _emptyTitleSize.width()) / 2,
		top,
		_emptyTitleSize.width(),
		style::al_top);
	top += _emptyTitleSize.height() + st::welcomeEmptyTextSkip;

	_emptyAbout.draw(
		p,
		r.x() + (r.width() - _emptyAboutSize.width()) / 2,
		top,
		_emptyAboutSize.width(),
		style::al_top);
}

QString WelcomeMessagesWidget::listElementAuthorRank(
		not_null<const Element*> view) {
	return {};
}

bool WelcomeMessagesWidget::listElementHideTopicButton(
		not_null<const Element*> view) {
	return true;
}

History *WelcomeMessagesWidget::listTranslateHistory() {
	return nullptr;
}

void WelcomeMessagesWidget::listAddTranslatedItems(
	not_null<TranslateTracker*> tracker) {
}

Ui::ElasticScroll *WelcomeMessagesWidget::listScrollArea() const {
	return _scroll.data();
}

bool WelcomeMessagesWidget::listThanosEffectEnabled() const {
	return false;
}

void WelcomeMessagesWidget::confirmDeleteSelected() {
	ConfirmDeleteSelectedItems(_inner);
}

void WelcomeMessagesWidget::clearSelected() {
	_inner->cancelSelection();
}

} // namespace HistoryView
