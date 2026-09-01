/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/section_widget.h"
#include "window/section_memento.h"
#include "history/view/history_view_corner_buttons.h"
#include "history/view/history_view_list_widget.h"
#include "history/history_item_helpers.h"
#include "history/history_view_top_toast.h"
#include "data/data_messages.h"
#include "data/data_report.h"
#include "ui/controls/swipe_handler_data.h"
#include "base/timer.h"

class History;
class BotKeyboard;
enum class SendMediaType;
struct SendingAlbum;

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace Api {
struct MessageToSend;
struct SendOptions;
struct SendAction;
struct VideoCoverEdit;
} // namespace Api

namespace Storage {
} // namespace Storage

namespace Ui {
class ElasticScroll;
class InnerDropdown;
class PlainShadow;
class ScrollArea;
struct PreparedList;
struct PreparedBundle;
class SendFilesWay;
class ChooseThemeController;
} // namespace Ui

namespace Profile {
class BackButton;
} // namespace Profile

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace Iv {
struct RichPage;
} // namespace Iv

namespace Data {
class RepliesList;
class ForumTopic;
struct DrawToReplyRequest;
} // namespace Data

namespace Support {
class Autocomplete;
struct Contact;
} // namespace Support

namespace HistoryView {

namespace Controls {
struct VoiceToSend;
} // namespace Controls

class Element;
class TopBarWidget;
class ChatMemento;
class ComposeControls;
class ComposeSearch;
class TopControls;
class SendActionPainter;
class StickerToast;
class EmptyPainter;
class PullToNextChannel;
class SubsectionTabs;
class SelfForwardsTagger;
class SuggestOptionsBar;
class AboutView;
class BottomControls;
class PaidReactionToast;
class PullToNextChannel;
enum class SuggestMode;

struct ChatViewId {
	not_null<History*> history;
	MsgId repliesRootId;
	Data::SavedSublist *sublist = nullptr;

	friend inline bool operator==(ChatViewId, ChatViewId) = default;
};

class ChatWidget final
	: public Window::SectionWidget
	, private WindowListDelegate
	, private CornerButtonsDelegate {
public:
	ChatWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		ChatViewId id);
	~ChatWidget();

	[[nodiscard]] ChatViewId id() const {
		return _id;
	}
	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}
	Dialogs::RowDescriptor activeChat() const override;
	bool preventsClose(Fn<void()> &&continueCallback) const override;

	bool hasTopBarShadow() const override {
		return true;
	}

	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	bool sameTypeAs(not_null<Window::SectionMemento*> memento) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	bool showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) override;

	Window::SectionActionResult sendBotCommand(
		Bot::SendCommandRequest request) override;
	Window::SectionActionResult hideSingleUseKeyboard(
		FullMsgId replyToId) override;

	bool searchInChatEmbedded(
		QString query,
		Dialogs::Key chat,
		PeerData *searchFrom = nullptr) override;

	bool confirmSendingFiles(const QStringList &files) override;
	bool confirmSendingFiles(not_null<const QMimeData*> data) override;
	bool notify_switchInlineBotButtonReceived(
		const QString &query,
		UserData *samePeerBot,
		MsgId samePeerReplyTo) override;

	bool showChooseReportMessages(
		not_null<PeerData*> peer,
		Data::ReportInput &&reportInput,
		Fn<void(std::vector<MsgId>)> &&done) override;
	bool clearChooseReportMessages() override;
	bool toggleChooseChatTheme(
		not_null<PeerData*> peer,
		std::optional<bool> show = std::nullopt) override;
	[[nodiscard]] Ui::ChatTheme *customChatTheme() const override;

	void setInternalState(
		const QRect &geometry,
		not_null<ChatMemento*> memento);

	// Tabbed selector management.
	bool pushTabbedSelectorToThirdSection(
		not_null<Data::Thread*> thread,
		const Window::SectionShow &params) override;
	bool returnTabbedSelector() override;

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

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
	void listItemsAddedToEnd(
		const std::vector<not_null<Element*>> &items,
		int addedCount) override;
	void listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) override;
	bool listAllowsReadEffect(not_null<const Element*> view) override;
	MessagesBarData listMessagesBar(
		const std::vector<not_null<Element*>> &elements,
		bool markLastAsRead) override;
	void listContentRefreshed() override;
	void listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<Element*> view) override;
	bool listElementHideReply(not_null<const Element*> view) override;
	bool listElementShownUnread(not_null<const Element*> view) override;
	bool listIsGoodForAroundPosition(not_null<const Element*> view) override;
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
		->rpl::producer<Data::AllowedReactions> override;
	void listShowPremiumToast(not_null<DocumentData*> document) override;
	bool handleDrawToReplyRequest(Data::DrawToReplyRequest request);
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
	bool listElementHideTopicButton(not_null<const Element*> view) override;
	History *listTranslateHistory() override;
	void listAddTranslatedItems(
		not_null<TranslateTracker*> tracker) override;
	Ui::ChatPaintContext listPreparePaintContext(
		Ui::ChatPaintContextArgs &&args) override;
	base::unique_qptr<Ui::PopupMenu> listFillSenderUserpicMenu(
		PeerId userpicPeerId) override;
	Ui::ElasticScroll *listScrollArea() const override;
	bool listShowForumThreadBars() const override;

	// CornerButtonsDelegate delegate.
	void cornerButtonsShowAtPosition(
		Data::MessagePosition position) override;
	Data::Thread *cornerButtonsThread() override;
	FullMsgId cornerButtonsCurrentId() override;
	bool cornerButtonsIgnoreVisibility() override;
	std::optional<bool> cornerButtonsDownShown() override;
	bool cornerButtonsUnreadMayBeShown() override;
	bool cornerButtonsHas(CornerButtonType type) override;

private:
	enum class Mode {
		Sublist,
		Replies,
		History,
	};
	[[nodiscard]] Mode mode() const;
	[[nodiscard]] bool unreadMessagesBelowBottom() const;

	void setChooseReportMessagesDetails(
		Data::ReportInput reportInput,
		Fn<void(std::vector<MsgId>)> callback);
	void activateChooseForReport();

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	[[nodiscard]] bool contentOverlapped(const QRect &globalRect) override;

	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;
	void showFinishedHook() override;
	void checkActivation() override;
	void doSetInnerFocus() override;

	[[nodiscard]] bool checkSendPayment(
		int messagesCount,
		Api::SendOptions options,
		Fn<void(int)> withPaymentApproved);

	void markLoaded();
	void requestSponsoredMessages();
	void injectSponsoredMessages();
	[[nodiscard]] bool injectNextSponsoredMessage();
	[[nodiscard]] bool appendSponsoredMessages();
	[[nodiscard]] bool showAppendedSponsored();
	void requestMessageData(MsgId msgId);
	void messageDataReceived(not_null<PeerData*> peer, MsgId msgId);
	void clearSupportPreloadRequest();
	void checkSupportPreload(bool force = false);
	void handleSupportSwitch(not_null<History*> updated);
	void supportInitAutocomplete();
	void supportInsertText(const QString &text);
	void supportShareContact(Support::Contact contact);
	[[nodiscard]] rpl::producer<Data::MessagesSlice> repliesSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter);
	[[nodiscard]] rpl::producer<Data::MessagesSlice> sublistSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter);
	[[nodiscard]] rpl::producer<Data::MessagesSlice> historySource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter);

	void onScroll();
	void scrollToCurrentVoiceMessage(FullMsgId fromId, FullMsgId toId);
	void closeCurrent();
	void unreadCountUpdated();
	void updateInnerVisibleArea();
	void updateControlsGeometry();
	void updateAdaptiveLayout();
	void saveHistoryScrollState(const ListMemento &state);
	void saveState(not_null<ChatMemento*> memento);
	void restoreState(not_null<ChatMemento*> memento);
	void setReplies(std::shared_ptr<Data::RepliesList> replies);
	void refreshReplies();
	void showAtStart();
	void showAtEnd();
	void showAtPosition(
		Data::MessagePosition position,
		FullMsgId originItemId = {});
	void showAtPosition(
		Data::MessagePosition position,
		FullMsgId originItemId,
		const Window::SectionShow &params);
	void finishSending();

	void setupComposeControls();
	void setupSwipeReplyAndBack();

	void setupRoot();
	void setupTopicViewer();
	void subscribeToTopic();
	void subscribeToSublist();
	void setTopic(Data::ForumTopic *topic);

	void setupDragArea();
	void setupShortcuts();

	void searchRequested();
	void searchInTopic();
	void updatePinnedVisibility();

	void confirmDeleteSelected();
	void confirmForwardSelected();
	void clearSelected();
	void setPinnedVisibility(bool shown);

	[[nodiscard]] Api::SendAction prepareSendAction(
		Api::SendOptions options) const;
	void sendTextWithTags(
		TextWithTags textWithTags,
		bool useCurrentWebPageDraft,
		Api::SendOptions options,
		Fn<void()> done);
	void sendRichDraft(
		std::shared_ptr<const Iv::RichPage> page,
		Api::SendOptions options);
	void sendRichDraftWithoutFormatting(
		std::shared_ptr<const Iv::RichPage> page,
		Api::SendOptions options);
	void sendWithTextOverride(
		TextWithEntities text,
		Api::SendOptions options,
		Fn<void()> done);
	void send();
	void send(Api::SendOptions options);
	void sendVoice(const Controls::VoiceToSend &data);
	void edit(
		not_null<HistoryItem*> item,
		Api::SendOptions options,
		mtpRequestId *const saveEditMsgRequestId,
		bool spoilered,
		Api::VideoCoverEdit videoCover);
	void chooseAttach(std::optional<bool> overrideSendImagesAsPhotos);
	[[nodiscard]] SendMenu::Details sendMenuDetails() const override;
	bool processChosenSticker(ChatHelpers::FileChosen &&chosen) override;
	[[nodiscard]] FullReplyTo replyTo() const;
	[[nodiscard]] FullReplyTo keyboardReplyTo() const;
	[[nodiscard]] MsgId resolveTopicRootId(const FullReplyTo &replyTo) const;
	[[nodiscard]] MsgId resolvedTopicRootId() const;
	[[nodiscard]] Data::ForumTopic *resolvedTopic();
	void refreshCanSendMessages();
	void refreshResolvedTopicRootState();
	void setKeyboardReplyTo(HistoryItem *item);
	[[nodiscard]] SuggestOptions suggestOptions(
		bool skipNoAdminCheck = false) const;
	void applySuggestOptions(
		SuggestOptions suggest,
		SuggestMode suggestMode);
	bool cancelSuggestPost();
	[[nodiscard]] bool realReplyOrEditActive() const;
	[[nodiscard]] FullMsgId keyboardSourceId() const;
	[[nodiscard]] HistoryItem *keyboardSourceItem() const;
	[[nodiscard]] bool keyboardRowsVisible() const;
	[[nodiscard]] FullMsgId keyboardSourceIdForHiddenState() const;
	[[nodiscard]] bool keyboardUiSuppressedByReplyOrEdit() const;
	[[nodiscard]] int computeMaxFieldHeightForKeyboard(
		int contentTop,
		int bottom) const;
	[[nodiscard]] bool itemBelongsToKeyboardView(
		not_null<const HistoryItem*> item) const;
	[[nodiscard]] MsgId keyboardHiddenId() const;
	void setKeyboardHiddenId(MsgId id);
	void clearKeyboardHiddenId();
	[[nodiscard]] bool keyboardUsed() const;
	void markKeyboardUsed();
	void showKeyboardReplyToExternal();
	void hideKeyboardReplyToExternal();
	void updateKeyboardUiState(bool hasMarkup, bool suppress);
	void resetRepliesKeyboardState();
	void clearRepliesKeyboardState();
	void setRepliesKeyboardState(MsgId id);
	void updateBotKeyboard(History *h = nullptr, bool force = false);
	void toggleBotKeyboard(bool manual = true);
	void maybeUpdateLastKeyboardFromSlice(
		const Data::MessagesSlice &slice,
		bool force = false);
	void botCallbackSent(not_null<HistoryItem*> item);
	[[nodiscard]] bool kbWasHidden() const;
	[[nodiscard]] bool lastForceReplyReplied(const FullMsgId &replyTo) const;
	[[nodiscard]] bool lastForceReplyReplied() const;
	bool cancelReply(bool lastKeyboardUsed = false);
	void sendBotCommand(
		Bot::SendCommandRequest request,
		Api::SendOptions options);
	void refreshSuggestPostToggle();
	void refreshSuggestFromDraft();
	[[nodiscard]] HistoryItem *lookupRepliesRoot() const;
	[[nodiscard]] Data::ForumTopic *lookupTopic();
	[[nodiscard]] bool computeAreComments() const;
	void setMembersShowAreaActive(bool active);
	void showMembersDropdown();
	[[nodiscard]] int countMembersDropdownHeightMax() const;
	void orderWidgets();

	void pushReplyReturn(not_null<HistoryItem*> item);
	void checkReplyReturns();
	void recountChatWidth();
	void replyToMessage(FullReplyTo id);
	void refreshTopBarActiveChat();
	void handlePeerMigration();
	void refreshUnreadCountBadge(std::optional<int> count);

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
	bool showSendingFilesError(const Ui::PreparedBundle &bundle) const;

	void sendingFilesConfirmed(
		std::shared_ptr<Ui::PreparedBundle> bundle,
		Api::SendOptions options);

	bool sendExistingDocument(
		not_null<DocumentData*> document,
		Api::MessageToSend messageToSend,
		std::optional<MsgId> localId);
	void sendExistingPhoto(not_null<PhotoData*> photo);
	bool sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options);
	void sendInlineResult(
		std::shared_ptr<InlineBots::Result> result,
		not_null<UserData*> bot);
	void sendInlineResult(
		std::shared_ptr<InlineBots::Result> result,
		not_null<UserData*> bot,
		Api::SendOptions options,
		std::optional<MsgId> localMessageId);

	void validateSubsectionTabs() override;
	void updateSubsectionTabsGeometry();
	void setupEmptyPainter();
	void refreshAboutView(bool force = false);
	void checkSuggestToGigagroup();
	void showAboutTopPromotion();
	void showInfoTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback);
	void updateControlsVisibility();
	void unblockUser();
	void sendBotStartCommand();
	bool clearMaybeSendStart();
	void checkMaybeSendBotStart();
	void joinChannelAction();
	void joinGroupAction();
	void toggleMuteUnmute();
	void reportSelectedMessages();
	void updateTopBarChooseForReport();
	[[nodiscard]] bool isChoosingTheme() const;
	[[nodiscard]] bool emptyShown() const;
	[[nodiscard]] bool showSlowmodeError();
	[[nodiscard]] bool showScheduleSendError();

	const not_null<History*> _history;
	const not_null<PeerData*> _peer;
	ChatViewId _id;

	MsgId _repliesRootId = 0;
	MsgId _repliesKeyboardRootId = 0;
	MsgId _repliesKeyboardId = 0;
	MsgId _repliesKeyboardHiddenId = 0;
	HistoryItem *_repliesRoot = nullptr;
	Data::ForumTopic *_topic = nullptr;
	mutable Data::ForumTopic *_creatingBotTopic = nullptr;
	mutable bool _newTopicDiscarded = false;
	std::shared_ptr<Data::RepliesList> _replies;
	rpl::lifetime _repliesLifetime;
	rpl::variable<bool> _areComments = false;
	bool _repliesKeyboardInited = false;
	bool _repliesKeyboardUsed = false;

	struct ChooseMessagesForReport {
		Data::ReportInput reportInput;
		Fn<void(std::vector<MsgId>)> callback;
		bool active = false;
	};
	std::unique_ptr<ChooseMessagesForReport> _chooseForReport;
	std::unique_ptr<Ui::ChooseThemeController> _chooseTheme;

	Data::SavedSublist *_sublist = nullptr;
	PeerId _monoforumPeerId;

	std::shared_ptr<SendActionPainter> _sendAction;
	std::shared_ptr<Ui::ChatTheme> _theme;
	QPointer<ListWidget> _inner;
	object_ptr<TopBarWidget> _topBar;
	object_ptr<Ui::PlainShadow> _topBarShadow;
	object_ptr<Ui::InnerDropdown> _membersDropdown = { nullptr };
	base::Timer _membersDropdownShowTimer;
	std::unique_ptr<HistoryView::PaidReactionToast> _paidReactionToast;
	std::unique_ptr<HistoryView::TopControls> _topControls;
	rpl::variable<bool> _suggestPostToggleShown = false;
	rpl::variable<bool> _suggestPostToggleActive = false;
	rpl::variable<bool> _botKeyboardShownToggleShown = false;
	rpl::variable<bool> _botKeyboardHideToggleShown = false;
	rpl::variable<bool> _botCommandStartExtraGuard = true;
	rpl::variable<QString> _botKeyboardPlaceholder;
	std::unique_ptr<ComposeControls> _composeControls;
	std::unique_ptr<SuggestOptionsBar> _suggestOptions;
	std::unique_ptr<ComposeSearch> _composeSearch;
	std::unique_ptr<HistoryView::BottomControls> _bottom;
	std::unique_ptr<HistoryView::AboutView> _aboutView;
	std::unique_ptr<EmptyPainter> _emptyPainter;
	std::unique_ptr<SubsectionTabs> _subsectionTabs;
	rpl::lifetime _subsectionTabsLifetime;
	rpl::lifetime _subsectionCheckLifetime;
	rpl::lifetime _subsectionTopicsLifetime;
	bool _canSendMessages = false;
	rpl::lifetime _canSendMessagesLifetime;
	bool _skipScrollEvent = false;
	bool _synteticScrollEvent = false;

	std::unique_ptr<Ui::ElasticScroll> _scroll;
	std::unique_ptr<PullToNextChannel> _pullToNext;
	base::unique_qptr<Ui::ScrollArea> _kbScroll;
	BotKeyboard *_keyboard = nullptr;
	HistoryItem *_kbReplyTo = nullptr;
	rpl::event_stream<> _kbReplyToChanges;
	bool _keyboardReplyExternalVisible = false;
	std::unique_ptr<Data::MessagesSlice> _repliesLastSlice;
	bool _ignoreReplyCancelledExternal = false;
	bool _kbShown = false;
	bool _fieldHasSendText = false;
	std::unique_ptr<HistoryView::StickerToast> _stickerToast;
	InfoTooltip _topToast;

	FullMsgId _lastShownAt;
	HistoryView::CornerButtons _cornerButtons;
	std::unique_ptr<Support::Autocomplete> _supportAutocomplete;
	rpl::lifetime _topicLifetime;
	rpl::lifetime _historySponsoredPreloading;
	bool _injectingSponsored = false;

	Ui::Controls::SwipeContextData _gestureHorizontal;
	Ui::Controls::SwipeBackResult _swipeBackData;

	SendPaymentHelper _sendPayment;

	int _lastScrollTop = 0;
	int _scrollTopDelta = 0;
	int _composeControlsTop = 0;
	crl::time _lastUserScrolled = 0;

	bool _choosingAttach = false;
	bool _justMarkingAsRead = false;

	bool _loaded = false;
	bool _maybeSendStart = false;
	bool _sentFromScheduledTip = false;
	History *_supportPreloadHistory = nullptr;
	int _supportPreloadRequest = 0;

	std::unique_ptr<HistoryView::SelfForwardsTagger> _selfForwardsTagger;

};

class ChatMemento final : public Window::SectionMemento {
public:
	explicit ChatMemento(
		ChatViewId id,
		MsgId highlightId = 0,
		MessageHighlightId highlight = {});

	struct Comments {
	};
	explicit ChatMemento(
		Comments,
		not_null<HistoryItem*> commentsItem,
		MsgId commentId = 0);

	void setReadInformation(
		MsgId inboxReadTillId,
		int unreadCount,
		MsgId outboxReadTillId);

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	[[nodiscard]] ChatViewId id() const {
		return _id;
	}

	void setReplies(std::shared_ptr<Data::RepliesList> replies) {
		_replies = std::move(replies);
	}
	[[nodiscard]] std::shared_ptr<Data::RepliesList> getReplies() const {
		return _replies;
	}

	void setFromTopic(not_null<Data::ForumTopic*> topic);
	void setFromHistory(not_null<History*> history);

	void setReplyReturns(const QVector<FullMsgId> &list) {
		_replyReturns = list;
	}
	const QVector<FullMsgId> &replyReturns() const {
		return _replyReturns;
	}

	void setOriginId(FullMsgId id) {
		_originId = id;
	}
	[[nodiscard]] FullMsgId originId() const {
		return _originId;
	}

	Data::ForumTopic *topicForRemoveRequests() const override;
	Data::SavedSublist *sublistForRemoveRequests() const override;

	[[nodiscard]] not_null<ListMemento*> list() {
		return &_list;
	}
	[[nodiscard]] MsgId highlightId() const {
		return _highlightId;
	}
	[[nodiscard]] const MessageHighlightId &highlight() const {
		return _highlight;
	}
	[[nodiscard]] bool activateChooseForReport() const {
		return _activateChooseForReport;
	}
	[[nodiscard]] bool sendBotStart() const {
		return _sendBotStart;
	}
	[[nodiscard]] bool maybeSendBotStart() const {
		return _maybeSendBotStart;
	}

private:
	void setupTopicViewer();

	ChatViewId _id;
	const MsgId _highlightId = 0;
	const MessageHighlightId _highlight;
	ListMemento _list;
	std::shared_ptr<Data::RepliesList> _replies;
	QVector<FullMsgId> _replyReturns;
	FullMsgId _originId;
	bool _activateChooseForReport = false;
	bool _sendBotStart = false;
	bool _maybeSendBotStart = false;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
