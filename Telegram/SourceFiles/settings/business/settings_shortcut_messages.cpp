/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_shortcut_messages.h"

#include "api/api_editing.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "boxes/delete_messages_box.h"
#include "boxes/premium_limits_box.h"
#include "boxes/send_files_box.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_message_reaction_id.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/controls/compose_controls_common.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "history/view/history_view_corner_buttons.h"
#include "history/view/history_view_empty_list_bubble.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_sticker_toast.h"
#include "history/history.h"
#include "history/history_item.h"
#include "inline_bots/inline_bot_result.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "settings/business/settings_recipients_helper.h"
#include "storage/localimageloader.h"
#include "storage/storage_account.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/jump_down_button.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/scroll_area.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"

namespace Settings {
namespace {

using namespace HistoryView;

class ShortcutMessages
	: public AbstractSection
	, private ListDelegate
	, private CornerButtonsDelegate {
public:
	ShortcutMessages(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		BusinessShortcutId shortcutId);
	~ShortcutMessages();

	[[nodiscard]] static Type Id(BusinessShortcutId shortcutId);

	[[nodiscard]] Type id() const final override {
		return Id(_shortcutId);
	}

	[[nodiscard]] rpl::producer<QString> title() override;

	bool paintOuter(
		not_null<QWidget*> outer,
		int maxVisibleHeight,
		QRect clip) override;

private:
	// ListDelegate interface.
	Context listContext() override;
	bool listScrollTo(int top, bool syntetic = true) override;
	void listCancelRequest() override;
	void listDeleteRequest() override;
	void listTryProcessKeyInput(not_null<QKeyEvent*> e) override;
	rpl::producer<Data::MessagesSlice> listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) override;
	bool listAllowsMultiSelect() override;
	bool listIsItemGoodForSelection(not_null<HistoryItem*> item) override;
	bool listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) override;
	void listSelectionChanged(SelectedItems &&items) override;
	void listMarkReadTill(not_null<HistoryItem*> item) override;
	void listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) override;
	MessagesBarData listMessagesBar(
		const std::vector<not_null<Element*>> &elements) override;
	void listContentRefreshed() override;
	void listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<Element*> view) override;
	bool listElementHideReply(not_null<const Element*> view) override;
	bool listElementShownUnread(not_null<const Element*> view) override;
	bool listIsGoodForAroundPosition(
		not_null<const Element *> view) override;
	void listSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void listSearch(
		const QString &query,
		const FullMsgId &context) override;
	void listHandleViaClick(not_null<UserData*> bot) override;
	not_null<Ui::ChatTheme*> listChatTheme() override;
	CopyRestrictionType listCopyRestrictionType(HistoryItem *item) override;
	CopyRestrictionType listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) override;
	CopyRestrictionType listSelectRestrictionType() override;
	auto listAllowedReactionsValue()
		-> rpl::producer<Data::AllowedReactions> override;
	void listShowPremiumToast(not_null<DocumentData*> document) override;
	void listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) override;
	void listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) override;
	void listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) override;
	QString listElementAuthorRank(not_null<const Element*> view) override;
	History *listTranslateHistory() override;
	void listAddTranslatedItems(
		not_null<TranslateTracker*> tracker) override;

	// CornerButtonsDelegate delegate.
	void cornerButtonsShowAtPosition(
		Data::MessagePosition position) override;
	Data::Thread *cornerButtonsThread() override;
	FullMsgId cornerButtonsCurrentId() override;
	bool cornerButtonsIgnoreVisibility() override;
	std::optional<bool> cornerButtonsDownShown() override;
	bool cornerButtonsUnreadMayBeShown() override;
	bool cornerButtonsHas(CornerButtonType type) override;

	QPointer<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;
	void setupComposeControls();


	void uploadFile(const QByteArray &fileContent, SendMediaType type);
	bool confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		std::optional<bool> overrideSendImagesAsPhotos = std::nullopt,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		const QStringList &files,
		const QString &insertTextOnCancel);
	bool confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel = QString());
	bool showSendingFilesError(const Ui::PreparedList &list) const;
	bool showSendingFilesError(
		const Ui::PreparedList &list,
		std::optional<bool> compress) const;
	void sendingFilesConfirmed(
		Ui::PreparedList &&list,
		Ui::SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter);

	void sendExistingDocument(not_null<DocumentData*> document);
	bool sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options,
		std::optional<MsgId> localId);
	void sendExistingPhoto(not_null<PhotoData*> photo);
	bool sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options);
	void sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot);
	void sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot,
		Api::SendOptions options,
		std::optional<MsgId> localMessageId);

	[[nodiscard]] Api::SendAction prepareSendAction(
		Api::SendOptions options) const;
	void send();
	void send(Api::SendOptions options);
	void sendVoice(Controls::VoiceToSend &&data);
	void edit(
		not_null<HistoryItem*> item,
		Api::SendOptions options,
		mtpRequestId *const saveEditMsgRequestId);
	void chooseAttach(std::optional<bool> overrideSendImagesAsPhotos);
	[[nodiscard]] SendMenu::Type sendMenuType() const;
	[[nodiscard]] FullReplyTo replyTo() const;
	void doSetInnerFocus();
	void showAtPosition(
		Data::MessagePosition position,
		FullMsgId originItemId = {});
	void showAtPosition(
		Data::MessagePosition position,
		FullMsgId originItemId,
		const Window::SectionShow &params);
	void showAtEnd();
	void finishSending();

	const not_null<Window::SessionController*> _controller;
	const not_null<Main::Session*> _session;
	const not_null<Ui::ScrollArea*> _scroll;
	const BusinessShortcutId _shortcutId;
	const not_null<History*> _history;
	std::shared_ptr<Ui::ChatStyle> _style;
	std::shared_ptr<Ui::ChatTheme> _theme;
	QPointer<ListWidget> _inner;
	std::unique_ptr<Ui::RpWidget> _controlsWrap;
	std::unique_ptr<ComposeControls> _composeControls;
	bool _skipScrollEvent = false;

	std::unique_ptr<StickerToast> _stickerToast;

	FullMsgId _lastShownAt;
	CornerButtons _cornerButtons;

	Data::MessagesSlice _lastSlice;
	bool _choosingAttach = false;

};

struct Factory : AbstractSectionFactory {
	explicit Factory(BusinessShortcutId shortcutId)
		: shortcutId(shortcutId) {
	}

	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll
	) const final override {
		return object_ptr<ShortcutMessages>(
			parent,
			controller,
			scroll,
			shortcutId);
	}

	const BusinessShortcutId shortcutId = {};
};

ShortcutMessages::ShortcutMessages(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Ui::ScrollArea*> scroll,
	BusinessShortcutId shortcutId)
: AbstractSection(parent)
, _controller(controller)
, _session(&controller->session())
, _scroll(scroll)
, _shortcutId(shortcutId)
, _history(_session->data().history(_session->user()->id))
, _cornerButtons(
		_scroll,
		controller->chatStyle(),
		static_cast<HistoryView::CornerButtonsDelegate*>(this)) {
	controller->chatStyle()->paletteChanged(
	) | rpl::start_with_next([=] {
		_scroll->updateBars();
	}, _scroll->lifetime());

	_style = std::make_shared<Ui::ChatStyle>(_session->colorIndicesValue());
	_theme = std::shared_ptr<Ui::ChatTheme>(
		Window::Theme::DefaultChatThemeOn(lifetime()));

	_inner = Ui::CreateChild<ListWidget>(
		this,
		controller,
		static_cast<ListDelegate*>(this));
	//_scroll->scrolls(
	//) | rpl::start_with_next([=] {
	//	onScroll();
	//}, lifetime());

	_inner->editMessageRequested(
	) | rpl::start_with_next([=](auto fullId) {
		if (const auto item = _session->data().message(fullId)) {
			const auto media = item->media();
			if (!media || media->webpage() || media->allowsEditCaption()) {
				_composeControls->editMessage(fullId);
			}
		}
	}, _inner->lifetime());

	{
		auto emptyInfo = base::make_unique_q<EmptyListBubbleWidget>(
			_inner,
			controller->chatStyle(),
			st::msgServicePadding);
		const auto emptyText = Ui::Text::Semibold(
			tr::lng_scheduled_messages_empty(tr::now));
		emptyInfo->setText(emptyText);
		_inner->setEmptyInfoWidget(std::move(emptyInfo));
	}

	widthValue() | rpl::start_with_next([=](int width) {
		resize(width, width);
	}, lifetime());
}

ShortcutMessages::~ShortcutMessages() = default;

Type ShortcutMessages::Id(BusinessShortcutId shortcutId) {
	return std::make_shared<Factory>(shortcutId);
}

rpl::producer<QString> ShortcutMessages::title() {
	return rpl::single(u"Editing messages list"_q);
}

bool ShortcutMessages::paintOuter(
		not_null<QWidget*> outer,
		int maxVisibleHeight,
		QRect clip) {
	const auto window = outer->window()->height();
	Window::SectionWidget::PaintBackground(
		_theme.get(),
		outer,
		std::max(outer->height(), maxVisibleHeight),
		0,
		clip);
	return true;
}

void ShortcutMessages::setupComposeControls() {
	_composeControls->setHistory({
		.history = _history.get(),
		.writeRestriction = rpl::single(Controls::WriteRestriction()),
	});

	_composeControls->height(
	) | rpl::start_with_next([=](int height) {
		const auto wasMax = (_scroll->scrollTopMax() == _scroll->scrollTop());
		_controlsWrap->resize(width(), height);
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

	_composeControls->sendVoiceRequests(
	) | rpl::start_with_next([=](ComposeControls::VoiceToSend &&data) {
		sendVoice(std::move(data));
	}, lifetime());

	_composeControls->sendCommandRequests(
	) | rpl::start_with_next([=](const QString &command) {
		listSendBotCommand(command, FullMsgId());
	}, lifetime());

	const auto saveEditMsgRequestId = lifetime().make_state<mtpRequestId>(0);
	_composeControls->editRequests(
	) | rpl::start_with_next([=](auto data) {
		if (const auto item = _session->data().message(data.fullId)) {
			if (item->isBusinessShortcut()) {
				edit(item, data.options, saveEditMsgRequestId);
			}
		}
	}, lifetime());

	_composeControls->attachRequests(
	) | rpl::filter([=] {
		return !_choosingAttach;
	}) | rpl::start_with_next([=](std::optional<bool> overrideCompress) {
		_choosingAttach = true;
		base::call_delayed(st::historyAttach.ripple.hideDuration, this, [=] {
			_choosingAttach = false;
			chooseAttach(overrideCompress);
		});
	}, lifetime());

	_composeControls->fileChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		_controller->hideLayer(anim::type::normal);
		sendExistingDocument(data.document);
	}, lifetime());

	_composeControls->photoChosen(
	) | rpl::start_with_next([=](ChatHelpers::PhotoChosen chosen) {
		sendExistingPhoto(chosen.photo);
	}, lifetime());

	_composeControls->inlineResultChosen(
	) | rpl::start_with_next([=](ChatHelpers::InlineChosen chosen) {
		sendInlineResult(chosen.result, chosen.bot);
	}, lifetime());

	_composeControls->jumpToItemRequests(
	) | rpl::start_with_next([=](FullReplyTo to) {
		if (const auto item = _session->data().message(to.messageId)) {
			showAtPosition(item->position());
		}
	}, lifetime());

	_composeControls->scrollKeyEvents(
	) | rpl::start_with_next([=](not_null<QKeyEvent*> e) {
		_scroll->keyPressEvent(e);
	}, lifetime());

	_composeControls->editLastMessageRequests(
	) | rpl::start_with_next([=](not_null<QKeyEvent*> e) {
		if (!_inner->lastMessageEditRequestNotify()) {
			_scroll->keyPressEvent(e);
		}
	}, lifetime());

	_composeControls->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return Core::CanSendFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			//return confirmSendingFiles(
			//	data,
			//	std::nullopt,
			//	Core::ReadMimeText(data));#TODO
		}
		Unexpected("action in MimeData hook.");
	});

	_composeControls->lockShowStarts(
	) | rpl::start_with_next([=] {
		_cornerButtons.updateJumpDownVisibility();
		_cornerButtons.updateUnreadThingsVisibility();
	}, lifetime());

	_composeControls->viewportEvents(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		_scroll->viewportEvent(e);
	}, lifetime());

	_controlsWrap->widthValue() | rpl::start_with_next([=](int width) {
		_composeControls->resizeToWidth(width);
	}, _controlsWrap->lifetime());
	_composeControls->height() | rpl::start_with_next([=](int height) {
		_controlsWrap->resize(_controlsWrap->width(), height);
	}, _controlsWrap->lifetime());
}

QPointer<Ui::RpWidget> ShortcutMessages::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	_controlsWrap = std::make_unique<Ui::RpWidget>(parent);
	_composeControls = std::make_unique<ComposeControls>(
		_controlsWrap.get(),
		_controller,
		[=](not_null<DocumentData*> emoji) { listShowPremiumToast(emoji); },
		ComposeControls::Mode::Scheduled,
		SendMenu::Type::Disabled);

	setupComposeControls();

	return _controlsWrap.get();
}

Context ShortcutMessages::listContext() {
	return Context::History;
}

bool ShortcutMessages::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	if (_scroll->scrollTop() == top) {
		//updateInnerVisibleArea();
		return false;
	}
	_scroll->scrollToY(top);
	return true;
}

void ShortcutMessages::listCancelRequest() {
	if (_inner && !_inner->getSelectedItems().empty()) {
		//clearSelected();
		return;
	} else if (_composeControls->handleCancelRequest()) {
		return;
	}
	_controller->showBackFromStack();
}

void ShortcutMessages::listDeleteRequest() {
	//confirmDeleteSelected();
}

void ShortcutMessages::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
	_composeControls->tryProcessKeyInput(e);
}

rpl::producer<Data::MessagesSlice> ShortcutMessages::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto data = &_controller->session().data();
	//return rpl::single(rpl::empty) | rpl::then(
	//	data->scheduledMessages().updates(_history)
	//) | rpl::map([=] {
	//	return data->scheduledMessages().list(_history);
	//}) | rpl::after_next([=](const Data::MessagesSlice &slice) {
	//	highlightSingleNewMessage(slice);
	//});
	return rpl::never<Data::MessagesSlice>();
}

bool ShortcutMessages::listAllowsMultiSelect() {
	return true;
}

bool ShortcutMessages::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return !item->isSending() && !item->hasFailed();
}

bool ShortcutMessages::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void ShortcutMessages::listSelectionChanged(SelectedItems &&items) {
}

void ShortcutMessages::listMarkReadTill(not_null<HistoryItem*> item) {
}

void ShortcutMessages::listMarkContentsRead(
	const base::flat_set<not_null<HistoryItem*>> &items) {
}

MessagesBarData ShortcutMessages::listMessagesBar(
		const std::vector<not_null<Element*>> &elements) {
	return {};
}

void ShortcutMessages::listContentRefreshed() {
}

void ShortcutMessages::listUpdateDateLink(
	ClickHandlerPtr &link,
	not_null<Element*> view) {
}

bool ShortcutMessages::listElementHideReply(not_null<const Element*> view) {
	return false;
}

bool ShortcutMessages::listElementShownUnread(not_null<const Element*> view) {
	return true;
}

bool ShortcutMessages::listIsGoodForAroundPosition(
		not_null<const Element*> view) {
	return true;
}

void ShortcutMessages::listSendBotCommand(
	const QString &command,
	const FullMsgId &context) {
}

void ShortcutMessages::listSearch(
		const QString &query,
		const FullMsgId &context) {
	const auto inChat = _history->peer->isUser()
		? Dialogs::Key()
		: Dialogs::Key(_history);
	_controller->searchMessages(query, inChat);
}

void ShortcutMessages::listHandleViaClick(not_null<UserData*> bot) {
	_composeControls->setText({ '@' + bot->username() + ' ' });
}

not_null<Ui::ChatTheme*> ShortcutMessages::listChatTheme() {
	return _theme.get();
}

CopyRestrictionType ShortcutMessages::listCopyRestrictionType(
		HistoryItem *item) {
	return CopyRestrictionType::None;
}

CopyRestrictionType ShortcutMessages::listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) {
	if (const auto media = item->media()) {
		if (const auto invoice = media->invoice()) {
			if (invoice->extendedMedia) {
				return CopyMediaRestrictionTypeFor(_history->peer, item);
			}
		}
	}
	return CopyRestrictionType::None;
}

CopyRestrictionType ShortcutMessages::listSelectRestrictionType() {
	return CopyRestrictionType::None;
}

auto ShortcutMessages::listAllowedReactionsValue()
-> rpl::producer<Data::AllowedReactions> {
	return rpl::single(Data::AllowedReactions());
}

void ShortcutMessages::listShowPremiumToast(
		not_null<DocumentData*> document) {
	if (!_stickerToast) {
		_stickerToast = std::make_unique<HistoryView::StickerToast>(
			_controller,
			this,
			[=] { _stickerToast = nullptr; });
	}
	_stickerToast->showFor(document);
}

void ShortcutMessages::listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) {
	_controller->openPhoto(photo, { context });
}

void ShortcutMessages::listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) {
	_controller->openDocument(document, showInMediaView, { context });
}

void ShortcutMessages::listPaintEmpty(
	Painter &p,
	const Ui::ChatPaintContext &context) {
}

QString ShortcutMessages::listElementAuthorRank(
		not_null<const Element*> view) {
	return {};
}

History *ShortcutMessages::listTranslateHistory() {
	return nullptr;
}

void ShortcutMessages::listAddTranslatedItems(
	not_null<TranslateTracker*> tracker) {
}

void ShortcutMessages::cornerButtonsShowAtPosition(
		Data::MessagePosition position) {
	//showAtPosition(position);
}

Data::Thread *ShortcutMessages::cornerButtonsThread() {
	return _history;
}

FullMsgId ShortcutMessages::cornerButtonsCurrentId() {
	return _lastShownAt;
}

bool ShortcutMessages::cornerButtonsIgnoreVisibility() {
	return false;// animatingShow();
}

std::optional<bool> ShortcutMessages::cornerButtonsDownShown() {
	if (_composeControls->isLockPresent()
		|| _composeControls->isTTLButtonShown()) {
		return false;
	}
	//const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
	//if (top < _scroll->scrollTopMax() || _cornerButtons.replyReturn()) {
	//	return true;
	//} else if (_inner->loadedAtBottomKnown()) {
	//	return !_inner->loadedAtBottom();
	//}
	return std::nullopt;
}

bool ShortcutMessages::cornerButtonsUnreadMayBeShown() {
	return _inner->loadedAtBottomKnown()
		&& !_composeControls->isLockPresent()
		&& !_composeControls->isTTLButtonShown();
}

bool ShortcutMessages::cornerButtonsHas(CornerButtonType type) {
	return (type == CornerButtonType::Down);
}

void ShortcutMessages::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	// #TODO replies schedule
	_session->api().sendFile(fileContent, type, prepareSendAction({}));
}

bool ShortcutMessages::showSendingFilesError(
		const Ui::PreparedList &list) const {
	return showSendingFilesError(list, std::nullopt);
}

bool ShortcutMessages::showSendingFilesError(
		const Ui::PreparedList &list,
		std::optional<bool> compress) const {
	const auto text = [&] {
		using Error = Ui::PreparedList::Error;
		switch (list.error) {
		case Error::None: return QString();
		case Error::EmptyFile:
		case Error::Directory:
		case Error::NonLocalUrl: return tr::lng_send_image_empty(
			tr::now,
			lt_name,
			list.errorData);
		case Error::TooLargeFile: return u"(toolarge)"_q;
		}
		return tr::lng_forward_send_files_cant(tr::now);
	}();
	if (text.isEmpty()) {
		return false;
	} else if (text == u"(toolarge)"_q) {
		const auto fileSize = list.files.back().size;
		_controller->show(
			Box(FileSizeLimitBox, _session, fileSize, nullptr));
		return true;
	}

	_controller->showToast(text);
	return true;
}

Api::SendAction ShortcutMessages::prepareSendAction(
		Api::SendOptions options) const {
	auto result = Api::SendAction(_history, options);
	result.replyTo = replyTo();
	result.options.shortcutId = _shortcutId;
	result.options.sendAs = _composeControls->sendAsPeer();
	return result;
}

void ShortcutMessages::send() {
	if (_composeControls->getTextWithAppliedMarkdown().text.isEmpty()) {
		return;
	}
	send({});
	// #TODO replies schedule
	//const auto callback = [=](Api::SendOptions options) { send(options); };
	//Ui::show(
	//	PrepareScheduleBox(this, sendMenuType(), callback),
	//	Ui::LayerOption::KeepOther);
}

void ShortcutMessages::sendVoice(ComposeControls::VoiceToSend &&data) {
	auto action = prepareSendAction(data.options);
	_session->api().sendVoiceMessage(
		data.bytes,
		data.waveform,
		data.duration,
		std::move(action));

	_composeControls->cancelReplyMessage();
	_composeControls->clearListenState();
	finishSending();
}

void ShortcutMessages::send(Api::SendOptions options) {
	_cornerButtons.clearReplyReturns();

	auto message = Api::MessageToSend(prepareSendAction(options));
	message.textWithTags = _composeControls->getTextWithAppliedMarkdown();
	message.webPage = _composeControls->webPageDraft();

	_session->api().sendMessage(std::move(message));

	_composeControls->clear();

	finishSending();
}

void ShortcutMessages::edit(
		not_null<HistoryItem*> item,
		Api::SendOptions options,
		mtpRequestId *const saveEditMsgRequestId) {
	if (*saveEditMsgRequestId) {
		return;
	}
	const auto webpage = _composeControls->webPageDraft();
	auto sending = TextWithEntities();
	auto left = _composeControls->prepareTextForEditMsg();

	const auto originalLeftSize = left.text.size();
	const auto hasMediaWithCaption = item
		&& item->media()
		&& item->media()->allowsEditCaption();
	const auto maxCaptionSize = !hasMediaWithCaption
		? MaxMessageSize
		: Data::PremiumLimits(_session).captionLengthCurrent();
	if (!TextUtilities::CutPart(sending, left, maxCaptionSize)
		&& !hasMediaWithCaption) {
		if (item) {
			_controller->show(Box<DeleteMessagesBox>(item, false));
		} else {
			doSetInnerFocus();
		}
		return;
	} else if (!left.text.isEmpty()) {
		const auto remove = originalLeftSize - maxCaptionSize;
		_controller->showToast(
			tr::lng_edit_limit_reached(tr::now, lt_count, remove));
		return;
	}

	lifetime().add([=] {
		if (!*saveEditMsgRequestId) {
			return;
		}
		_session->api().request(base::take(*saveEditMsgRequestId)).cancel();
	});

	const auto done = [=](mtpRequestId requestId) {
		if (requestId == *saveEditMsgRequestId) {
			*saveEditMsgRequestId = 0;
			_composeControls->cancelEditMessage();
		}
	};

	const auto fail = [=](const QString &error, mtpRequestId requestId) {
		if (requestId == *saveEditMsgRequestId) {
			*saveEditMsgRequestId = 0;
		}

		if (ranges::contains(Api::kDefaultEditMessagesErrors, error)) {
			_controller->showToast(tr::lng_edit_error(tr::now));
		} else if (error == u"MESSAGE_NOT_MODIFIED"_q) {
			_composeControls->cancelEditMessage();
		} else if (error == u"MESSAGE_EMPTY"_q) {
			doSetInnerFocus();
		} else {
			_controller->showToast(tr::lng_edit_error(tr::now));
		}
		update();
		return true;
	};

	*saveEditMsgRequestId = Api::EditTextMessage(
		item,
		sending,
		webpage,
		options,
		crl::guard(this, done),
		crl::guard(this, fail));

	_composeControls->hidePanelsAnimated();
	doSetInnerFocus();
}

bool ShortcutMessages::confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	const auto hasImage = data->hasImage();
	const auto premium = _controller->session().user()->isPremium();

	if (const auto urls = Core::ReadMimeUrls(data); !urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize,
			premium);
		if (list.error != Ui::PreparedList::Error::NonLocalUrl) {
			if (list.error == Ui::PreparedList::Error::None
				|| !hasImage) {
				const auto emptyTextOnCancel = QString();
				list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
				confirmSendingFiles(std::move(list), emptyTextOnCancel);
				return true;
			}
		}
	}

	if (auto read = Core::ReadMimeImage(data)) {
		confirmSendingFiles(
			std::move(read.image),
			std::move(read.content),
			overrideSendImagesAsPhotos,
			insertTextOnCancel);
		return true;
	}
	return false;
}

bool ShortcutMessages::confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel) {
	if (_composeControls->confirmMediaEdit(list)) {
		return true;
	} else if (showSendingFilesError(list)) {
		return false;
	}

	auto box = Box<SendFilesBox>(
		_controller,
		std::move(list),
		_composeControls->getTextWithAppliedMarkdown(),
		_history->peer,
		Api::SendType::Normal,
		SendMenu::Type::SilentOnly); // #TODO replies schedule

	box->setConfirmedCallback(crl::guard(this, [=](
			Ui::PreparedList &&list,
			Ui::SendFilesWay way,
			TextWithTags &&caption,
			Api::SendOptions options,
			bool ctrlShiftEnter) {
		sendingFilesConfirmed(
			std::move(list),
			way,
			std::move(caption),
			options,
			ctrlShiftEnter);
	}));
	box->setCancelledCallback(_composeControls->restoreTextCallback(
		insertTextOnCancel));

	//ActivateWindow(_controller);
	_controller->show(std::move(box));

	return true;
}

bool ShortcutMessages::confirmSendingFiles(
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

void ShortcutMessages::sendingFilesConfirmed(
		Ui::PreparedList &&list,
		Ui::SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter) {
	Expects(list.filesToProcess.empty());

	if (showSendingFilesError(list, way.sendImagesAsPhotos())) {
		return;
	}
	auto groups = DivideByGroups(
		std::move(list),
		way,
		_history->peer->slowmodeApplied());
	const auto type = way.sendImagesAsPhotos()
		? SendMediaType::Photo
		: SendMediaType::File;
	auto action = prepareSendAction(options);
	action.clearDraft = false;
	if ((groups.size() != 1 || !groups.front().sentWithCaption())
		&& !caption.text.isEmpty()) {
		auto message = Api::MessageToSend(action);
		message.textWithTags = base::take(caption);
		_session->api().sendMessage(std::move(message));
	}
	for (auto &group : groups) {
		const auto album = (group.type != Ui::AlbumType::None)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		_session->api().sendFiles(
			std::move(group.list),
			type,
			base::take(caption),
			album,
			action);
	}
	if (_composeControls->replyingToMessage() == action.replyTo) {
		_composeControls->cancelReplyMessage();
	}
	finishSending();
}

void ShortcutMessages::chooseAttach(
		std::optional<bool> overrideSendImagesAsPhotos) {
	_choosingAttach = false;

	const auto filter = (overrideSendImagesAsPhotos == true)
		? FileDialog::ImagesOrAllFilter()
		: FileDialog::AllOrImagesFilter();
	FileDialog::GetOpenPaths(this, tr::lng_choose_files(tr::now), filter, crl::guard(this, [=](
			FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
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
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			const auto premium = _controller->session().user()->isPremium();
			auto list = Storage::PrepareMediaList(
				result.paths,
				st::sendMediaPreviewSize,
				premium);
			list.overrideSendImagesAsPhotos = overrideSendImagesAsPhotos;
			confirmSendingFiles(std::move(list));
		}
	}), nullptr);
}

void ShortcutMessages::finishSending() {
	_composeControls->hidePanelsAnimated();
	//if (_previewData && _previewData->pendingTill) previewCancel();
	doSetInnerFocus();
	showAtEnd();
}

void ShortcutMessages::showAtEnd() {
	showAtPosition(Data::MaxMessagePosition);
}

void ShortcutMessages::doSetInnerFocus() {
	if (!_inner->getSelectedText().rich.text.isEmpty()
		|| !_inner->getSelectedItems().empty()
		|| !_composeControls->focus()) {
		_inner->setFocus();
	}
}

void ShortcutMessages::sendExistingDocument(
		not_null<DocumentData*> document) {
	sendExistingDocument(document, {}, std::nullopt);
}

bool ShortcutMessages::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options,
		std::optional<MsgId> localId) {
	Api::SendExistingDocument(
		Api::MessageToSend(prepareSendAction(options)),
		document,
		localId);

	_composeControls->cancelReplyMessage();
	finishSending();
	return true;
}

void ShortcutMessages::sendExistingPhoto(not_null<PhotoData*> photo) {
	sendExistingPhoto(photo, {});
}

bool ShortcutMessages::sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options) {
	Api::SendExistingPhoto(
		Api::MessageToSend(prepareSendAction(options)),
		photo);

	_composeControls->cancelReplyMessage();
	finishSending();
	return true;
}

void ShortcutMessages::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot) {
	const auto errorText = result->getErrorOnSend(_history);
	if (!errorText.isEmpty()) {
		_controller->showToast(errorText);
		return;
	}
	sendInlineResult(result, bot, {}, std::nullopt);
	//const auto callback = [=](Api::SendOptions options) {
	//	sendInlineResult(result, bot, options);
	//};
	//Ui::show(
	//	PrepareScheduleBox(this, sendMenuType(), callback),
	//	Ui::LayerOption::KeepOther);
}

void ShortcutMessages::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot,
		Api::SendOptions options,
		std::optional<MsgId> localMessageId) {
	auto action = prepareSendAction(options);
	action.generateLocal = true;
	_session->api().sendInlineResult(bot, result, action, localMessageId);

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
	finishSending();
}

void ShortcutMessages::showAtPosition(
		Data::MessagePosition position,
		FullMsgId originItemId) {
	showAtPosition(position, originItemId, {});
}

void ShortcutMessages::showAtPosition(
		Data::MessagePosition position,
		FullMsgId originItemId,
		const Window::SectionShow &params) {
	_lastShownAt = position.fullId;
	_inner->showAtPosition(
		position,
		params,
		_cornerButtons.doneJumpFrom(position.fullId, originItemId, true));
}

FullReplyTo ShortcutMessages::replyTo() const {
	return _composeControls->replyingToMessage();
}

} // namespace

Type ShortcutMessagesId(int shortcutId) {
	return ShortcutMessages::Id(shortcutId);
}

} // namespace Settings
