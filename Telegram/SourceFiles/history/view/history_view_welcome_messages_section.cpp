/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_welcome_messages_section.h"

#include "boxes/delete_messages_box.h"
#include "chat_helpers/tabbed_selector.h"
#include "data/components/welcome_messages.h"
#include "data/data_message_reactions.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "history/view/history_view_empty_list_bubble.h"
#include "history/view/history_view_sticker_toast.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/history.h"
#include "history/history_view_swipe_back_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "ui/chat/chat_style.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/shadow.h"
#include "ui/screen_reader_mode.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"

#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView {

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
			.attachments = false,
			.botCommandSend = false,
			.silentBroadcastToggle = false,
			.attachBotsMenu = false,
			.inlineBots = false,
			.megagroupSet = false,
			.recordMediaMessage = false,
			.emojiOnlyPanel = true,
		},
	}))
, _cornerButtons(
	_scroll.data(),
	controller->chatStyle(),
	static_cast<HistoryView::CornerButtonsDelegate*>(this)) {
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
			}
		}
	}, _inner->lifetime());

	{
		auto emptyInfo = base::make_unique_q<EmptyListBubbleWidget>(
			_inner,
			controller->chatStyle(),
			st::msgServicePadding);
		const auto emptyText = tr::semibold(
			tr::lng_welcome_messages_empty(tr::now));
		emptyInfo->setText(emptyText);
		_inner->setEmptyInfoWidget(std::move(emptyInfo));
	}
	setupComposeControls();
	Window::SetupSwipeBackSection(this, _scroll, _inner);
}

WelcomeMessagesWidget::~WelcomeMessagesWidget() = default;

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
	const auto limit = Data::WelcomeMessagesLimit(&session());
	if (session().welcomeMessages().count(_history) >= limit) {
		controller()->showToast(tr::lng_business_limit_reached(
			tr::now,
			lt_count,
			limit));
		return;
	}
	session().welcomeMessages().send(_history, std::move(text));
	_composeControls->clear();
	_composeControls->hidePanelsAnimated();
	_composeControls->focus();
}

void WelcomeMessagesWidget::edit(not_null<HistoryItem*> item) {
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
	_composeControls->focus();
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
	_composeControls->focus();
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
	return Context::ShortcutMessages;
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

rpl::producer<Data::MessagesSlice> WelcomeMessagesWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto session = &controller()->session();
	return rpl::single(rpl::empty) | rpl::then(
		session->welcomeMessages().updates(_history)
	) | rpl::map([=] {
		return session->welcomeMessages().list(_history);
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
	return !item->isSending() && !item->hasFailed();
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
