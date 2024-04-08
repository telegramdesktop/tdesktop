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
#include "boxes/premium_preview_box.h"
#include "boxes/send_files_box.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_message_reaction_id.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/controls/compose_controls_common.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "history/view/history_view_corner_buttons.h"
#include "history/view/history_view_empty_list_bubble.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_sticker_toast.h"
#include "history/history.h"
#include "history/history_item.h"
#include "info/info_wrap_widget.h"
#include "inline_bots/inline_bot_result.h"
#include "lang/lang_keys.h"
#include "lang/lang_numbers_animation.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "settings/business/settings_quick_replies.h"
#include "settings/business/settings_recipients_helper.h"
#include "storage/localimageloader.h"
#include "storage/storage_account.h"
#include "storage/storage_media_prepare.h"
#include "storage/storage_shared_media.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/jump_down_button.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/scroll_area.h"
#include "ui/painter.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"
#include "styles/style_layers.h"

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
		rpl::producer<Container> containerValue,
		BusinessShortcutId shortcutId);
	~ShortcutMessages();

	[[nodiscard]] static Type Id(BusinessShortcutId shortcutId);

	[[nodiscard]] Type id() const final override {
		return Id(_shortcutId.current());
	}

	[[nodiscard]] rpl::producer<QString> title() override;
	[[nodiscard]] rpl::producer<> sectionShowBack() override;
	void setInnerFocus() override;

	rpl::producer<Info::SelectedItems> selectedListValue() override;
	void selectionAction(Info::SelectionAction action) override;
	void fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) override;

	bool paintOuter(
		not_null<QWidget*> outer,
		int maxVisibleHeight,
		QRect clip) override;

private:
	void outerResized();
	void updateComposeControlsPosition();

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
	void processScroll();
	void updateInnerVisibleArea();

	void checkReplyReturns();
	void confirmDeleteSelected();
	void clearSelected();

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
	void refreshEmptyText();
	bool showPremiumRequired() const;

	const not_null<Window::SessionController*> _controller;
	const not_null<Main::Session*> _session;
	const not_null<Ui::ScrollArea*> _scroll;
	const not_null<History*> _history;
	rpl::variable<BusinessShortcutId> _shortcutId;
	rpl::variable<QString> _shortcut;
	rpl::variable<Container> _container;
	rpl::variable<int> _count;
	std::shared_ptr<Ui::ChatStyle> _style;
	std::shared_ptr<Ui::ChatTheme> _theme;
	QPointer<ListWidget> _inner;
	std::unique_ptr<Ui::RpWidget> _controlsWrap;
	std::unique_ptr<ComposeControls> _composeControls;
	rpl::event_stream<> _showBackRequests;
	bool _skipScrollEvent = false;

	QSize _inOuterResize;
	QSize _pendingOuterResize;

	const style::icon *_emptyIcon = nullptr;
	Ui::Text::String _emptyText;
	int _emptyTextWidth = 0;
	int _emptyTextHeight = 0;

	rpl::variable<Info::SelectedItems> _selectedItems
		= Info::SelectedItems(Storage::SharedMediaType::kCount);

	std::unique_ptr<StickerToast> _stickerToast;

	FullMsgId _lastShownAt;
	CornerButtons _cornerButtons;

	Data::MessagesSlice _lastSlice;
	bool _choosingAttach = false;

};

struct Factory final : AbstractSectionFactory {
	explicit Factory(BusinessShortcutId shortcutId)
	: shortcutId(shortcutId) {
	}

	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Container> containerValue
	) const final override {
		return object_ptr<ShortcutMessages>(
			parent,
			controller,
			scroll,
			std::move(containerValue),
			shortcutId);
	}

	const BusinessShortcutId shortcutId = {};
};

[[nodiscard]] bool IsAway(const QString &shortcut) {
	return (shortcut == u"away"_q);
}

[[nodiscard]] bool IsGreeting(const QString &shortcut) {
	return (shortcut == u"hello"_q);
}

ShortcutMessages::ShortcutMessages(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Ui::ScrollArea*> scroll,
	rpl::producer<Container> containerValue,
	BusinessShortcutId shortcutId)
: AbstractSection(parent)
, _controller(controller)
, _session(&controller->session())
, _scroll(scroll)
, _history(_session->data().history(_session->user()->id))
, _shortcutId(shortcutId)
, _shortcut(
	_session->data().shortcutMessages().lookupShortcut(shortcutId).name)
, _container(std::move(containerValue))
, _cornerButtons(
		_scroll,
		controller->chatStyle(),
		static_cast<HistoryView::CornerButtonsDelegate*>(this)) {
	const auto messages = &_session->data().shortcutMessages();

	messages->shortcutIdChanged(
	) | rpl::start_with_next([=](Data::ShortcutIdChange change) {
		if (change.oldId == _shortcutId.current()) {
			if (change.newId) {
				_shortcutId = change.newId;
			} else {
				_showBackRequests.fire({});
			}
		}
	}, lifetime());
	messages->shortcutsChanged(
	) | rpl::start_with_next([=] {
		_shortcut = messages->lookupShortcut(_shortcutId.current()).name;
	}, lifetime());

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
	_inner->overrideIsChatWide(false);

	_scroll->sizeValue() | rpl::filter([](QSize size) {
		return !size.isEmpty();
	}) | rpl::start_with_next([=] {
		outerResized();
	}, lifetime());

	_scroll->scrolls(
	) | rpl::start_with_next([=] {
		processScroll();
	}, lifetime());

	_shortcut.value() | rpl::start_with_next([=] {
		refreshEmptyText();
		_inner->update();
	}, lifetime());

	_inner->editMessageRequested(
	) | rpl::start_with_next([=](auto fullId) {
		if (const auto item = _session->data().message(fullId)) {
			const auto media = item->media();
			if (!media || media->webpage() || media->allowsEditCaption()) {
				_composeControls->editMessage(
					fullId,
					_inner->getSelectedTextRange(item));
			}
		}
	}, _inner->lifetime());

	_inner->heightValue() | rpl::start_with_next([=](int height) {
		resize(width(), height);
	}, lifetime());
}

ShortcutMessages::~ShortcutMessages() = default;

void ShortcutMessages::refreshEmptyText() {
	const auto &shortcut = _shortcut.current();
	const auto away = IsAway(shortcut);
	const auto greeting = !away && IsGreeting(shortcut);
	auto text = away
		? tr::lng_away_empty_title(
			tr::now,
			Ui::Text::Bold
		).append("\n\n").append(tr::lng_away_empty_about(tr::now))
		: greeting
		? tr::lng_greeting_empty_title(
			tr::now,
			Ui::Text::Bold
		).append("\n\n").append(tr::lng_greeting_empty_about(tr::now))
		: tr::lng_replies_empty_title(
			tr::now,
			Ui::Text::Bold
		).append("\n\n").append(tr::lng_replies_empty_about(
			tr::now,
			lt_shortcut,
			Ui::Text::Bold('/' + shortcut),
			Ui::Text::WithEntities));
	_emptyIcon = away
		? &st::awayEmptyIcon
		: greeting
		? &st::greetingEmptyIcon
		: &st::repliesEmptyIcon;
	const auto padding = st::repliesEmptyPadding;
	const auto minWidth = st::repliesEmptyWidth / 4;
	const auto maxWidth = std::max(
		minWidth + 1,
		st::repliesEmptyWidth - padding.left() - padding.right());
	_emptyText = Ui::Text::String(
		st::messageTextStyle,
		text,
		kMarkupTextOptions,
		minWidth);
	const auto countHeight = [&](int width) {
		return _emptyText.countHeight(width);
	};
	_emptyTextWidth = Ui::FindNiceTooltipWidth(
		minWidth,
		maxWidth,
		countHeight);
	_emptyTextHeight = countHeight(_emptyTextWidth);
}

Type ShortcutMessages::Id(BusinessShortcutId shortcutId) {
	return std::make_shared<Factory>(shortcutId);
}

rpl::producer<QString> ShortcutMessages::title() {
	return _shortcut.value() | rpl::map([=](const QString &shortcut) {
		return IsAway(shortcut)
			? tr::lng_away_title()
			: IsGreeting(shortcut)
			? tr::lng_greeting_title()
			: rpl::single('/' + shortcut);
	}) | rpl::flatten_latest();
}

void ShortcutMessages::processScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void ShortcutMessages::updateInnerVisibleArea() {
	if (!_inner->animatedScrolling()) {
		checkReplyReturns();
	}
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	_cornerButtons.updateJumpDownVisibility();
	_cornerButtons.updateUnreadThingsVisibility();
}

rpl::producer<> ShortcutMessages::sectionShowBack() {
	return _showBackRequests.events();
}

void ShortcutMessages::setInnerFocus() {
	_composeControls->focus();
}

rpl::producer<Info::SelectedItems> ShortcutMessages::selectedListValue() {
	return _selectedItems.value();
}

void ShortcutMessages::selectionAction(Info::SelectionAction action) {
	switch (action) {
	case Info::SelectionAction::Clear: clearSelected(); return;
	case Info::SelectionAction::Delete: confirmDeleteSelected(); return;
	}
	Unexpected("Action in ShortcutMessages::selectionAction.");
}

void ShortcutMessages::fillTopBarMenu(
		const Ui::Menu::MenuCallback &addAction) {
	const auto owner = &_controller->session().data();
	const auto messages = &owner->shortcutMessages();

	addAction(tr::lng_context_edit_shortcut(tr::now), [=] {
		if (!_controller->session().premium()) {
			ShowPremiumPreviewToBuy(
				_controller,
				PremiumFeature::QuickReplies);
			return;
		}
		const auto submit = [=](QString name, Fn<void()> close) {
			const auto id = _shortcutId.current();
			const auto error = [=](QString text) {
				if (!text.isEmpty()) {
					_controller->showToast((text == u"SHORTCUT_OCCUPIED"_q)
						? tr::lng_replies_error_occupied(tr::now)
						: text);
				}
			};
			messages->editShortcut(id, name, close, crl::guard(this, error));
		};
		const auto name = _shortcut.current();
		_controller->show(
			Box(EditShortcutNameBox, name, crl::guard(this, submit)));
	}, &st::menuIconEdit);

	const auto justDelete = crl::guard(this, [=] {
		messages->removeShortcut(_shortcutId.current());
	});
	const auto confirmDeleteShortcut = [=] {
		const auto slice = messages->list(_shortcutId.current());
		if (slice.fullCount == 0) {
			justDelete();
		} else {
			const auto confirmed = [=](Fn<void()> close) {
				justDelete();
				close();
			};
			_controller->show(Ui::MakeConfirmBox({
				.text = { tr::lng_replies_delete_sure() },
				.confirmed = confirmed,
				.confirmText = tr::lng_box_delete(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		}
	};
	addAction({
		.text = tr::lng_context_delete_shortcut(tr::now),
		.handler = crl::guard(this, confirmDeleteShortcut),
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
}

bool ShortcutMessages::paintOuter(
		not_null<QWidget*> outer,
		int maxVisibleHeight,
		QRect clip) {
	Window::SectionWidget::PaintBackground(
		_theme.get(),
		outer,
		std::max(outer->height(), maxVisibleHeight),
		0,
		clip);
	return true;
}

void ShortcutMessages::outerResized() {
	const auto outer = _scroll->size();
	if (!_inOuterResize.isEmpty()) {
		_pendingOuterResize = (_inOuterResize != outer)
			? outer
			: QSize();
		return;
	}
	_inOuterResize = outer;

	do {
		const auto newScrollTop = _scroll->isHidden()
			? std::nullopt
			: _scroll->scrollTop()
			? base::make_optional(_scroll->scrollTop())
			: 0;
		_skipScrollEvent = true;
		const auto minHeight = (_container.current() == Container::Layer)
			? st::boxWidth
			: _inOuterResize.height();
		_inner->resizeToWidth(_inOuterResize.width(), minHeight);
		_skipScrollEvent = false;

		if (!_scroll->isHidden() && newScrollTop) {
			_scroll->scrollToY(*newScrollTop);
		}
		_inOuterResize = base::take(_pendingOuterResize);
	} while (!_inOuterResize.isEmpty());

	if (!_scroll->isHidden()) {
		updateInnerVisibleArea();
	}
	updateComposeControlsPosition();
	_cornerButtons.updatePositions();
}

void ShortcutMessages::updateComposeControlsPosition() {
	const auto bottom = _scroll->parentWidget()->height();
	const auto controlsHeight = _composeControls->heightCurrent();
	_composeControls->move(0, bottom - controlsHeight + st::boxRadius);
	_composeControls->setAutocompleteBoundingRect(_scroll->geometry());
}

void ShortcutMessages::setupComposeControls() {
	_shortcutId.value() | rpl::start_with_next([=](BusinessShortcutId id) {
		_composeControls->updateShortcutId(id);
	}, lifetime());

	const auto state = Dialogs::EntryState{
		.key = Dialogs::Key{ _history },
		.section = Dialogs::EntryState::Section::ShortcutMessages,
		.currentReplyTo = replyTo(),
	};
	_composeControls->setCurrentDialogsEntryState(state);

	auto writeRestriction = rpl::combine(
		_count.value(),
		ShortcutMessagesLimitValue(_session)
	) | rpl::map([=](int count, int limit) {
		return (count >= limit)
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
		.writeRestriction = std::move(writeRestriction),
	});

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
			return confirmSendingFiles(
				data,
				std::nullopt,
				Core::ReadMimeText(data));
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

	_composeControls->height(
	) | rpl::start_with_next([=](int height) {
		const auto wasMax = (_scroll->scrollTopMax() == _scroll->scrollTop());
		_controlsWrap->resize(width(), height - st::boxRadius);
		updateComposeControlsPosition();
		if (wasMax) {
			listScrollTo(_scroll->scrollTopMax());
		}
	}, lifetime());
}

QPointer<Ui::RpWidget> ShortcutMessages::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	auto placeholder = rpl::deferred([=] {
		return _shortcutId.value();
	}) | rpl::map([=](BusinessShortcutId id) {
		return _session->data().shortcutMessages().lookupShortcut(id).name;
	}) | rpl::map([=](const QString &shortcut) {
		return (shortcut == u"away"_q)
			? tr::lng_away_message_placeholder()
			: (shortcut == u"hello"_q)
			? tr::lng_greeting_message_placeholder()
			: tr::lng_replies_message_placeholder();
	}) | rpl::flatten_latest();

	_controlsWrap = std::make_unique<Ui::RpWidget>(parent);
	_composeControls = std::make_unique<ComposeControls>(
		dynamic_cast<Ui::RpWidget*>(_scroll->parentWidget()),
		ComposeControlsDescriptor{
			.stOverride = &st::repliesComposeControls,
			.show = _controller->uiShow(),
			.unavailableEmojiPasted = [=](not_null<DocumentData*> emoji) {
				listShowPremiumToast(emoji);
			},
			.mode = HistoryView::ComposeControlsMode::Normal,
			.sendMenuType = SendMenu::Type::Disabled,
			.regularWindow = _controller,
			.stickerOrEmojiChosen = _controller->stickerOrEmojiChosen(),
			.customPlaceholder = std::move(placeholder),
			.panelsLevel = Window::GifPauseReason::Layer,
			.voiceCustomCancelText = tr::lng_record_cancel_stories(tr::now),
			.voiceLockFromBottom = true,
			.features = {
				.sendAs = false,
				.ttlInfo = false,
				.botCommandSend = false,
				.silentBroadcastToggle = false,
				.attachBotsMenu = false,
				.megagroupSet = false,
				.commonTabbedPanel = false,
			},
		});

	setupComposeControls();

	showAtEnd();

	return _controlsWrap.get();
}

Context ShortcutMessages::listContext() {
	return Context::ShortcutMessages;
}

bool ShortcutMessages::listScrollTo(int top, bool syntetic) {
	top = std::clamp(top, 0, _scroll->scrollTopMax());
	if (_scroll->scrollTop() == top) {
		updateInnerVisibleArea();
		return false;
	}
	_scroll->scrollToY(top);
	return true;
}

void ShortcutMessages::listCancelRequest() {
	if (_inner && !_inner->getSelectedItems().empty()) {
		clearSelected();
		return;
	} else if (_composeControls->handleCancelRequest()) {
		return;
	}
	_showBackRequests.fire({});
}

void ShortcutMessages::listDeleteRequest() {
	confirmDeleteSelected();
}

void ShortcutMessages::listTryProcessKeyInput(not_null<QKeyEvent*> e) {
	_composeControls->tryProcessKeyInput(e);
}

rpl::producer<Data::MessagesSlice> ShortcutMessages::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto messages = &_session->data().shortcutMessages();
	return _shortcutId.value(
	) | rpl::map([=](BusinessShortcutId shortcutId) {
		return rpl::single(rpl::empty) | rpl::then(
			messages->updates(shortcutId)
		) | rpl::map([=] {
			return messages->list(shortcutId);
		});
	}) | rpl::flatten_latest(
	) | rpl::after_next([=](const Data::MessagesSlice &slice) {
		_count = slice.fullCount.value_or(
			messages->count(_shortcutId.current()));
	});
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
	auto value = Info::SelectedItems();
	value.title = [](int count) {
		return tr::lng_forum_messages(
			tr::now,
			lt_count,
			count,
			Ui::StringWithNumbers::FromString);
	};
	value.list = items | ranges::views::transform([](SelectedItem item) {
		auto result = Info::SelectedItem(GlobalMsgId{ item.msgId });
		result.canDelete = item.canDelete;
		return result;
	}) | ranges::to_vector;
	_selectedItems = std::move(value);

	if (items.empty()) {
		doSetInnerFocus();
	}
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
	Expects(_emptyIcon != nullptr);

	const auto width = st::repliesEmptyWidth;
	const auto padding = st::repliesEmptyPadding;
	const auto height = padding.top()
		+ _emptyIcon->height()
		+ st::repliesEmptySkip
		+ _emptyTextHeight
		+ padding.bottom();
	const auto r = QRect(
		(this->width() - width) / 2,
		(this->height() - height) / 3,
		width,
		height);
	HistoryView::ServiceMessagePainter::PaintBubble(p, context.st, r);

	_emptyIcon->paint(
		p,
		r.x() + (r.width() - _emptyIcon->width()) / 2,
		r.y() + padding.top(),
		this->width());
	p.setPen(st::msgServiceFg);
	_emptyText.draw(
		p,
		r.x() + (r.width() - _emptyTextWidth) / 2,
		r.y() + padding.top() + _emptyIcon->height() + st::repliesEmptySkip,
		_emptyTextWidth,
		style::al_top);
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
	showAtPosition(position);
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
	const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
	if (top < _scroll->scrollTopMax() || _cornerButtons.replyReturn()) {
		return true;
	} else if (_inner->loadedAtBottomKnown()) {
		return !_inner->loadedAtBottom();
	}
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

void ShortcutMessages::checkReplyReturns() {
	const auto currentTop = _scroll->scrollTop();
	const auto shortcutId = _shortcutId.current();
	while (const auto replyReturn = _cornerButtons.replyReturn()) {
		const auto position = replyReturn->position();
		const auto scrollTop = _inner->scrollTopForPosition(position);
		const auto below = scrollTop
			? (currentTop >= std::min(*scrollTop, _scroll->scrollTopMax()))
			: _inner->isBelowPosition(position);
		if (replyReturn->shortcutId() != shortcutId || below) {
			_cornerButtons.calculateNextReplyReturn();
		} else {
			break;
		}
	}
}

void ShortcutMessages::confirmDeleteSelected() {
	ConfirmDeleteSelectedItems(_inner);
}

void ShortcutMessages::clearSelected() {
	_inner->cancelSelection();
}

void ShortcutMessages::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	_session->api().sendFile(fileContent, type, prepareSendAction({}));
}

bool ShortcutMessages::showSendingFilesError(
		const Ui::PreparedList &list) const {
	return showSendingFilesError(list, std::nullopt);
}

bool ShortcutMessages::showSendingFilesError(
		const Ui::PreparedList &list,
		std::optional<bool> compress) const {
	if (showPremiumRequired()) {
		return true;
	}
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
	result.options.shortcutId = _shortcutId.current();
	result.options.sendAs = _composeControls->sendAsPeer();
	return result;
}

void ShortcutMessages::send() {
	if (_composeControls->getTextWithAppliedMarkdown().text.isEmpty()) {
		return;
	}
	send({});
}

void ShortcutMessages::sendVoice(ComposeControls::VoiceToSend &&data) {
	if (showPremiumRequired()) {
		return;
	}
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
	if (showPremiumRequired()) {
		return;
	}
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
		SendMenu::Type::Disabled);

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
	if (showPremiumRequired()) {
		return;
	}
	_choosingAttach = false;

	const auto filter = (overrideSendImagesAsPhotos == true)
		? FileDialog::PhotoVideoFilesFilter()
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
	if (showPremiumRequired()) {
		return false;
	}

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
	if (showPremiumRequired()) {
		return false;
	}
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
	if (showPremiumRequired()) {
		return;
	}
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
	if (showPremiumRequired()) {
		return;
	}
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

bool ShortcutMessages::showPremiumRequired() const {
	if (!_controller->session().premium()) {
		ShowPremiumPreviewToBuy(_controller, PremiumFeature::QuickReplies);
		return true;
	}
	return false;
}

} // namespace

Type ShortcutMessagesId(int shortcutId) {
	return ShortcutMessages::Id(shortcutId);
}

} // namespace Settings
