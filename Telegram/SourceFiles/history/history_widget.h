/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/controls/history_view_compose_media_edit_manager.h"
#include "history/view/history_view_corner_buttons.h"
#include "history/history_drag_area.h"
#include "history/history_item_helpers.h"
#include "history/history_view_highlight_manager.h"
#include "history/history_view_top_toast.h"
#include "history/history.h"
#include "chat_helpers/field_characters_count_manager.h"
#include "data/data_report.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "ui/widgets/fields/input_field.h"
#include "mtproto/sender.h"

enum class SendMediaType;
class MessageLinksParser;
struct InlineBotQuery;

namespace MTP {
class Error;
} // namespace MTP

namespace Data {
class PhotoMedia;
struct SendError;
} // namespace Data

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace Api {
struct MessageToSend;
struct SendOptions;
struct SendAction;
} // namespace Api

namespace InlineBots {
namespace Layout {
class Widget;
} // namespace Layout
struct ResultSelected;
} // namespace InlineBots

namespace Support {
class Autocomplete;
struct Contact;
} // namespace Support

namespace Ui {
class InnerDropdown;
class DropdownMenu;
class PlainShadow;
class IconButton;
class EmojiButton;
class SendButton;
class SilentToggle;
class FlatButton;
class RoundButton;
class PinnedBar;
class GroupCallBar;
class RequestsBar;
struct PreparedList;
struct PreparedBundle;
class SendFilesWay;
class SendAsButton;
class SpoilerAnimation;
class ChooseThemeController;
class ContinuousScroll;
struct ChatPaintHighlight;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Ui::Emoji {
class SuggestionsController;
} // namespace Ui::Emoji

namespace Webrtc {
enum class RecordAvailability : uchar;
} // namespace Webrtc

namespace ChatHelpers {
class TabbedPanel;
class TabbedSelector;
class FieldAutocomplete;
struct FileChosen;
} // namespace ChatHelpers

namespace HistoryView {
class StickerToast;
class PaidReactionToast;
class TopBarWidget;
class PaysStatus;
class ContactStatus;
class BusinessBotStatus;
class Element;
class PinnedTracker;
class TranslateBar;
class ComposeSearch;
class SubsectionTabs;
struct SelectedQuote;
class SuggestOptions;
enum class SuggestMode;
} // namespace HistoryView

namespace HistoryView::Controls {
class RecordLock;
class VoiceRecordBar;
class ForwardPanel;
class TTLButton;
class WebpageProcessor;
class CharactersLimitLabel;
class PhotoEditSpoilerManager;
struct VoiceToSend;
} // namespace HistoryView::Controls

class BotKeyboard;
class HistoryInner;

class HistoryWidget final
	: public Window::AbstractSectionWidget
	, private HistoryView::CornerButtonsDelegate {
public:
	using FieldHistoryAction = Ui::InputField::HistoryAction;
	using RecordLock = HistoryView::Controls::RecordLock;
	using VoiceRecordBar = HistoryView::Controls::VoiceRecordBar;
	using ForwardPanel = HistoryView::Controls::ForwardPanel;

	HistoryWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void historyLoaded();

	[[nodiscard]] bool preventsClose(Fn<void()> &&continueCallback) const;

	// When resizing the widget with top edge moved up or down and we
	// want to add this top movement to the scroll position, so inner
	// content will not move.
	void setGeometryWithTopMoved(const QRect &newGeometry, int topDelta);

	void windowShown();
	[[nodiscard]] bool markingMessagesRead() const;
	[[nodiscard]] bool markingContentsRead() const;
	bool skipItemRepaint();
	void checkActivation();

	void leaveToChildEvent(QEvent *e, QWidget *child) override;

	bool isItemCompletelyHidden(HistoryItem *item) const;
	void updateTopBarSelection();
	void updateTopBarChooseForReport();

	void loadMessages();
	void loadMessagesDown();
	void firstLoadMessages();
	void delayedShowAt(MsgId showAtMsgId, const Window::SectionShow &params);

	bool updateReplaceMediaButton();
	void updateFieldPlaceholder();

	bool confirmSendingFiles(const QStringList &files);
	bool confirmSendingFiles(not_null<const QMimeData*> data);

	void updateControlsVisibility();
	void updateControlsGeometry();

	History *history() const;
	PeerData *peer() const;
	void setMsgId(MsgId showAtMsgId, const Window::SectionShow &params = {});
	MsgId msgId() const;

	bool hasTopBarShadow() const {
		return peer() != nullptr;
	}
	void showAnimated(
		Window::SlideDirection direction,
		const Window::SectionSlideParams &params);
	void showFast();
	void finishAnimating();

	void doneShow();

	QPoint clampMousePosition(QPoint point);

	bool touchScroll(const QPoint &delta);

	void enqueueMessageHighlight(const HistoryView::SelectedQuote &quote);
	[[nodiscard]] Ui::ChatPaintHighlight itemHighlight(
		not_null<const HistoryItem*> item) const;

	MessageIdsList getSelectedItems() const;
	void itemEdited(not_null<HistoryItem*> item);

	void replyToMessage(FullReplyTo id);
	void replyToMessage(
		not_null<HistoryItem*> item,
		FullReplyTo fields = {});
	void editMessage(
		not_null<HistoryItem*> item,
		const TextSelection &selection);

	void fillSenderUserpicMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<PeerData*> peer);

	[[nodiscard]] FullReplyTo replyTo() const;
	[[nodiscard]] SuggestPostOptions suggestOptions(
		bool skipNoAdminCheck = false) const;
	bool lastForceReplyReplied(const FullMsgId &replyTo) const;
	bool lastForceReplyReplied() const;
	bool cancelReplyOrSuggest(bool lastKeyboardUsed = false);
	bool cancelReply(bool lastKeyboardUsed = false);
	bool cancelSuggestPost();
	void cancelEdit();
	void updateForwarding();

	void pushReplyReturn(not_null<HistoryItem*> item);
	[[nodiscard]] QVector<FullMsgId> replyReturns() const;
	void setReplyReturns(PeerId peer, QVector<FullMsgId> replyReturns);

	void escape();

	void sendBotCommand(const Bot::SendCommandRequest &request);
	void hideSingleUseKeyboard(FullMsgId replyToId);
	bool insertBotCommand(const QString &cmd);

	bool eventFilter(QObject *obj, QEvent *e) override;

	// With force=true the markup is updated even if it is
	// already shown for the passed history item.
	void updateBotKeyboard(History *h = nullptr, bool force = false);
	void botCallbackSent(not_null<HistoryItem*> item);

	void fastShowAtEnd(not_null<History*> history);
	bool applyDraft(
		FieldHistoryAction fieldHistoryAction = FieldHistoryAction::Clear);
	void showHistory(
		PeerId peerId,
		MsgId showAtMsgId,
		const Window::SectionShow &params = {});
	void setChooseReportMessagesDetails(
		Data::ReportInput reportInput,
		Fn<void(std::vector<MsgId>)> callback);
	void clearAllLoadRequests();
	void clearSupportPreloadRequest();
	void clearDelayedShowAtRequest();
	void clearDelayedShowAt();

	void toggleChooseChatTheme(
		not_null<PeerData*> peer,
		std::optional<bool> show = std::nullopt);
	[[nodiscard]] Ui::ChatTheme *customChatTheme() const;

	void applyCloudDraft(History *history);

	void activate();
	void setInnerFocus();
	[[nodiscard]] rpl::producer<> cancelRequests() const {
		return _cancelRequests.events();
	}
	bool searchInChatEmbedded(
		QString query,
		Dialogs::Key chat,
		PeerData *searchFrom = nullptr);

	void updateNotifyControls();

	bool contentOverlapped(const QRect &globalRect);

	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params);

	void forwardSelected();
	void confirmDeleteSelected();
	void clearSelected();

	[[nodiscard]] SendMenu::Details sendMenuDetails() const;
	[[nodiscard]] SendMenu::Details saveMenuDetails() const;
	bool sendExistingDocument(
		not_null<DocumentData*> document,
		Api::MessageToSend messageToSend,
		std::optional<MsgId> localId = std::nullopt);
	bool sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options);

	void showInfoTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback);
	void showPremiumStickerTooltip(
		not_null<const HistoryView::Element*> view);
	void showPremiumToast(not_null<DocumentData*> document);

	// Tabbed selector management.
	bool pushTabbedSelectorToThirdSection(
		not_null<Data::Thread*> thread,
		const Window::SectionShow &params) override;
	bool returnTabbedSelector() override;

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

	bool notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo);

	void tryProcessKeyInput(not_null<QKeyEvent*> e);

	~HistoryWidget();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	using TabbedPanel = ChatHelpers::TabbedPanel;
	using TabbedSelector = ChatHelpers::TabbedSelector;
	using VoiceToSend = HistoryView::Controls::VoiceToSend;
	enum ScrollChangeType {
		ScrollChangeNone,

		// When we toggle a pinned message.
		ScrollChangeAdd,

		// When loading a history part while scrolling down.
		ScrollChangeNoJumpToBottom,
	};
	struct ScrollChange {
		ScrollChangeType type;
		int value;
	};
	struct ChooseMessagesForReport {
		Data::ReportInput reportInput;
		Fn<void(std::vector<MsgId>)> callback;
		bool active = false;
	};
	struct ItemRevealAnimation {
		Ui::Animations::Simple animation;
		int startHeight = 0;
	};
	enum class TextUpdateEvent {
		SaveDraft = (1 << 0),
		SendTyping = (1 << 1),
	};
	using TextUpdateEvents = base::flags<TextUpdateEvent>;
	friend inline constexpr bool is_flag_type(TextUpdateEvent) { return true; };

	void cornerButtonsShowAtPosition(
		Data::MessagePosition position) override;
	Data::Thread *cornerButtonsThread() override;
	FullMsgId cornerButtonsCurrentId() override;
	bool cornerButtonsIgnoreVisibility() override;
	std::optional<bool> cornerButtonsDownShown() override;
	bool cornerButtonsUnreadMayBeShown() override;
	bool cornerButtonsHas(HistoryView::CornerButtonType type) override;

	[[nodiscard]] bool checkSendPayment(
		int messagesCount,
		Api::SendOptions options,
		Fn<void(int)> withPaymentApproved);

	void checkSuggestToGigagroup();
	void processReply();
	void setReplyFieldsFromProcessing();

	void initTabbedSelector();
	void initVoiceRecordBar();
	void refreshTabbedPanel();
	void createTabbedPanel();
	void setTabbedPanel(std::unique_ptr<TabbedPanel> panel);
	void updateField();
	void fieldChanged();
	void fieldTabbed();
	void fieldFocused();
	void fieldResized();

	void initFieldAutocomplete();
	void cancelInlineBot();
	void saveDraft(bool delayed = false);
	void saveCloudDraft();
	void saveDraftDelayed();
	void saveDraftWithTextNow();
	void showMembersDropdown();
	void windowIsVisibleChanged();
	void saveFieldToHistoryLocalDraft();
	void fileChosen(ChatHelpers::FileChosen &&data);

	void updateFieldSubmitSettings();
	bool clearMaybeSendStart();

	// Checks if we are too close to the top or to the bottom
	// in the scroll area and preloads history if needed.
	void preloadHistoryIfNeeded();

	void handleScroll();
	void updateHistoryItemsByTimer();

	[[nodiscard]] Dialogs::EntryState computeDialogsEntryState() const;
	void refreshTopBarActiveChat();

	void refreshJoinChannelText();
	void refreshGiftToChannelShown();
	void refreshDirectMessageShown();
	void requestMessageData(MsgId msgId);
	void messageDataReceived(not_null<PeerData*> peer, MsgId msgId);

	[[nodiscard]] Api::SendAction prepareSendAction(
		Api::SendOptions options) const;
	void sendVoice(const VoiceToSend &data);
	void send(Api::SendOptions options);
	void sendWithModifiers(Qt::KeyboardModifiers modifiers);
	void sendScheduled(Api::SendOptions initialOptions);
	[[nodiscard]] SendMenu::Details sendButtonMenuDetails() const;
	[[nodiscard]] SendMenu::Details sendButtonDefaultDetails() const;
	void handlePendingHistoryUpdate();
	void fullInfoUpdated();
	void toggleTabbedSelectorMode();
	void recountChatWidth();
	void handlePeerUpdate();
	bool updateCanSendMessage();
	void setMembersShowAreaActive(bool active);
	void handleHistoryChange(not_null<const History*> history);
	void showAboutTopPromotion();
	void hideFieldIfVisible();
	void unreadCountUpdated();
	void closeCurrent();

	[[nodiscard]] int computeMaxFieldHeight() const;
	void toggleMuteUnmute();
	void reportSelectedMessages();
	void showKeyboardHideButton();
	void toggleKeyboard(bool manual = true);
	void startBotCommand();
	void hidePinnedMessage();
	void cancelFieldAreaState();
	void unblockUser();
	void sendBotStartCommand();
	void joinChannel();

	void supportInitAutocomplete();
	void supportInsertText(const QString &text);
	void supportShareContact(Support::Contact contact);

	auto computeSendButtonType() const;

	void showFinished();
	void updateOverStates(QPoint pos);
	void chooseAttach(std::optional<bool> overrideSendImagesAsPhotos = {});
	void sendButtonClicked();
	void newItemAdded(not_null<HistoryItem*> item);
	void maybeMarkReactionsRead(not_null<HistoryItem*> item);

	bool canSendFiles(not_null<const QMimeData*> data) const;
	bool confirmSendingFiles(
		const QStringList &files,
		const QString &insertTextOnCancel);
	bool confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		std::optional<bool> overrideSendImagesAsPhotos = std::nullopt,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel = QString());
	bool showSendingFilesError(const Ui::PreparedList &list) const;
	bool showSendingFilesError(
		const Ui::PreparedList &list,
		std::optional<bool> compress) const;
	bool showSendMessageError(
		const TextWithTags &textWithTags,
		bool ignoreSlowmodeCountdown,
		Fn<void(int starsApproved)> withPaymentApproved = nullptr,
		Api::SendOptions options = {});

	void sendingFilesConfirmed(
		Ui::PreparedList &&list,
		Ui::SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter);
	void sendingFilesConfirmed(
		std::shared_ptr<Ui::PreparedBundle> bundle,
		Api::SendOptions options);

	void sendBotCommand(
		const Bot::SendCommandRequest &request,
		Api::SendOptions options);

	void uploadFile(const QByteArray &fileContent, SendMediaType type);
	void itemRemoved(not_null<const HistoryItem*> item);

	// Updates position of controls around the message field,
	// like send button, emoji button and others.
	void moveFieldControls();
	void updateFieldSize();

	[[nodiscard]] MsgId resolveReplyToTopicRootId();
	[[nodiscard]] Data::ForumTopic *resolveReplyToTopic();
	[[nodiscard]] bool canWriteMessage() const;
	void orderWidgets();

	[[nodiscard]] InlineBotQuery parseInlineBotQuery() const;

	void clearInlineBot();
	void inlineBotChanged();

	// Look in the _field for the inline bot and query string.
	void updateInlineBotQuery();

	// Request to show results in the emoji panel.
	void applyInlineBotQuery(UserData *bot, const QString &query);

	void cancelReplyAfterMediaSend(bool lastKeyboardUsed);
	bool replyToPreviousMessage();
	bool replyToNextMessage();
	[[nodiscard]] bool showSlowmodeError();

	void hideChildWidgets();
	void hideSelectorControlsAnimated();
	int countMembersDropdownHeightMax() const;

	void updateReplyToName();
	[[nodiscard]] bool editingMessage() const {
		return _editMsgId != 0;
	}

	void setupShortcuts();
	void setupGiftToChannelButton();
	void setupDirectMessageButton();

	void handlePeerMigration();

	void updateReplyEditTexts(bool force = false);
	void updateReplyEditText(not_null<HistoryItem*> item);

	void updatePinnedViewer();
	void setupTranslateBar();
	void setupPinnedTracker();
	void checkPinnedBarState();
	void clearHidingPinnedBar();
	void refreshPinnedBarButton(bool many, HistoryItem *item);
	void checkLastPinnedClickedIdReset(
		int wasScrollTop,
		int nowScrollTop);

	void checkMessagesTTL();
	void setupGroupCallBar();
	void setupRequestsBar();

	void checkSponsoredMessageBar();
	[[nodiscard]] bool checkSponsoredMessageBarVisibility() const;
	void requestSponsoredMessageBar();
	void createSponsoredMessageBar();

	void sendInlineResult(InlineBots::ResultSelected result);

	void drawField(Painter &p, const QRect &rect);
	void paintEditHeader(
		Painter &p,
		const QRect &rect,
		int left,
		int top) const;
	bool paintShowAnimationFrame();

	void updateMouseTracking();

	// destroys _history and _migrated unread bars
	void destroyUnreadBar();
	void destroyUnreadBarOnClose();
	void createUnreadBarIfBelowVisibleArea(int withScrollTop);
	[[nodiscard]] bool insideJumpToEndInsteadOfToUnread() const;
	void createUnreadBarAndResize();

	[[nodiscard]] TextWithEntities prepareTextForEditMsg() const;
	void saveEditMessage(Api::SendOptions options = {});

	void setupPreview();
	void editDraftOptions();
	void jumpToReply(FullReplyTo to);

	void messagesReceived(not_null<PeerData*> peer, const MTPmessages_Messages &messages, int requestId);
	void messagesFailed(const MTP::Error &error, int requestId);
	void addMessagesToFront(not_null<PeerData*> peer, const QVector<MTPMessage> &messages);
	void addMessagesToBack(not_null<PeerData*> peer, const QVector<MTPMessage> &messages);

	void updateSendRestriction();
	[[nodiscard]] Data::SendError computeSendRestriction() const;
	void updateHistoryGeometry(bool initial = false, bool loadedDown = false, const ScrollChange &change = { ScrollChangeNone, 0 });
	void updateListSize();
	void startItemRevealAnimations();
	void revealItemsCallback();

	void startMessageSendingAnimation(not_null<HistoryItem*> item);

	// Does any of the shown histories has this flag set.
	bool hasPendingResizedItems() const;

	// Counts scrollTop for placing the scroll right at the unread
	// messages bar, choosing from _history and _migrated unreadBar.
	std::optional<int> unreadBarTop() const;
	int itemTopForHighlight(not_null<HistoryView::Element*> view) const;
	void scrollToCurrentVoiceMessage(FullMsgId fromId, FullMsgId toId);

	// Scroll to current y without updating the _lastUserScrolled time.
	// Used to distinguish between user scrolls and syntetic scrolls.
	// This one is syntetic.
	void synteticScrollToY(int y);

	void writeDrafts();
	void writeDraftTexts();
	void writeDraftCursors();
	void setFieldText(
		const TextWithTags &textWithTags,
		TextUpdateEvents events = 0,
		FieldHistoryAction fieldHistoryAction = FieldHistoryAction::Clear);
	void clearFieldText(
		TextUpdateEvents events = 0,
		FieldHistoryAction fieldHistoryAction = FieldHistoryAction::Clear);
	[[nodiscard]] int fieldHeight() const;
	[[nodiscard]] bool fieldOrDisabledShown() const;

	void unregisterDraftSources();
	void registerDraftSource();
	void setHistory(History *history);
	void setEditMsgId(MsgId msgId);

	HistoryItem *getItemFromHistoryOrMigrated(MsgId genericMsgId) const;
	void animatedScrollToItem(MsgId msgId);
	void animatedScrollToY(int scrollTo, HistoryItem *attachTo = nullptr);

	// when scroll position or scroll area size changed this method
	// updates the boundings of the visible area in HistoryInner
	[[nodiscard]] bool hasSavedScroll() const;
	void visibleAreaUpdated();
	int countInitialScrollTop();
	int countAutomaticScrollTop();
	void preloadHistoryByScroll();
	void checkReplyReturns();
	void scrollToAnimationCallback(FullMsgId attachToId, int relativeTo);

	[[nodiscard]] bool readyToForward() const;
	[[nodiscard]] bool hasSilentToggle() const;

	void checkSupportPreload(bool force = false);
	void handleSupportSwitch(not_null<History*> updated);

	[[nodiscard]] bool isRecording() const;
	[[nodiscard]] bool isSearching() const;

	[[nodiscard]] bool isBotStart() const;
	[[nodiscard]] bool isBlocked() const;
	[[nodiscard]] bool isJoinChannel() const;
	[[nodiscard]] bool isMuteUnmute() const;
	[[nodiscard]] bool isReportMessages() const;
	bool updateCmdStartShown();
	void updateSendButtonType();
	[[nodiscard]] bool showRecordButton() const;
	[[nodiscard]] bool showInlineBotCancel() const;
	void refreshSilentToggle();
	void setupFastButtonMode();

	[[nodiscard]] bool isChoosingTheme() const;

	void setupScheduledToggle();
	void refreshScheduledToggle();
	void refreshSendGiftToggle();
	void refreshSuggestPostToggle();
	void applySuggestOptions(
		SuggestPostOptions suggest,
		HistoryView::SuggestMode mode);
	void setupSendAsToggle();
	void refreshSendAsToggle();
	void refreshAttachBotsMenu();

	void injectSponsoredMessages() const;

	bool kbWasHidden() const;

	void switchToSearch(QString query);

	void validateSubsectionTabs();

	void checkCharsCount();
	void checkCharsLimitation();

	MTP::Sender _api;
	FullReplyTo _replyTo;
	Ui::Text::String _replyToName;

	FullReplyTo _processingReplyTo;
	HistoryItem *_processingReplyItem = nullptr;

	MsgId _editMsgId = 0;
	std::shared_ptr<Data::PhotoMedia> _photoEditMedia;
	bool _canReplaceMedia = false;
	bool _canAddMedia = false;
	HistoryView::MediaEditManager _mediaEditManager;

	HistoryItem *_replyEditMsg = nullptr;
	Ui::Text::String _replyEditMsgText;
	std::unique_ptr<Ui::SpoilerAnimation> _replySpoiler;
	mutable base::Timer _updateEditTimeLeftDisplay;

	std::unique_ptr<HistoryView::SuggestOptions> _suggestOptions;

	object_ptr<Ui::IconButton> _fieldBarCancel;

	std::unique_ptr<Ui::RpWidget> _topBars;

	std::unique_ptr<HistoryView::TranslateBar> _translateBar;
	int _translateBarHeight = 0;

	std::unique_ptr<HistoryView::PinnedTracker> _pinnedTracker;
	std::unique_ptr<Ui::PinnedBar> _pinnedBar;
	std::unique_ptr<Ui::PinnedBar> _hidingPinnedBar;
	int _pinnedBarHeight = 0;
	FullMsgId _pinnedClickedId;
	std::optional<FullMsgId> _minPinnedId;

	std::unique_ptr<Ui::GroupCallBar> _groupCallBar;
	int _groupCallBarHeight = 0;
	std::unique_ptr<Ui::RequestsBar> _requestsBar;
	int _requestsBarHeight = 0;

	base::unique_qptr<Ui::SlideWrap<Ui::RpWidget>> _sponsoredMessageBar;
	int _sponsoredMessageBarHeight = 0;

	bool _preserveScrollTop = false;
	bool _repaintFieldScheduled = false;
	bool _sentFromScheduledTip = false;

	mtpRequestId _saveEditMsgRequestId = 0;

	std::unique_ptr<HistoryView::Controls::WebpageProcessor> _preview;
	Fn<bool(QPainter &p, QRect to)> _previewDrawPreview;
	Ui::Text::String _previewTitle;
	Ui::Text::String _previewDescription;

	PeerData *_peer = nullptr;

	bool _canSendMessages = false;
	bool _canSendTexts = false;
	MsgId _showAtMsgId = ShowAtUnreadMsgId;
	base::flat_set<MsgId> _topicsRequested;
	Window::SectionShow _showAtMsgParams;
	bool _showAndMaybeSendStart = false;

	int _firstLoadRequest = 0; // Not real mtpRequestId.
	int _preloadRequest = 0; // Not real mtpRequestId.
	int _preloadDownRequest = 0; // Not real mtpRequestId.

	MsgId _delayedShowAtMsgId = -1;
	Window::SectionShow _delayedShowAtMsgParams;
	int _delayedShowAtRequest = 0; // Not real mtpRequestId.

	History *_supportPreloadHistory = nullptr;
	int _supportPreloadRequest = 0; // Not real mtpRequestId.

	object_ptr<HistoryView::TopBarWidget> _topBar;
	object_ptr<Ui::ContinuousScroll> _scroll;
	QPointer<HistoryInner> _list;
	History *_migrated = nullptr;
	History *_history = nullptr;
	rpl::lifetime _historySponsoredPreloading;

	// Initial updateHistoryGeometry() was called.
	bool _historyInited = false;
	// If updateListSize() was called without updateHistoryGeometry().
	bool _updateHistoryGeometryRequired = false;

	int _lastScrollTop = 0; // gifs optimization
	crl::time _lastScrolled = 0;
	base::Timer _updateHistoryItems;

	crl::time _lastUserScrolled = 0;
	bool _synteticScrollEvent = false;
	Ui::Animations::Simple _scrollToAnimation;

	HistoryView::CornerButtons _cornerButtons;

	std::unique_ptr<ChatHelpers::FieldAutocomplete> _autocomplete;
	std::unique_ptr<Ui::Emoji::SuggestionsController> _emojiSuggestions;
	object_ptr<Support::Autocomplete> _supportAutocomplete;

	UserData *_inlineBot = nullptr;
	QString _inlineBotUsername;
	bool _inlineLookingUpBot = false;
	mtpRequestId _inlineBotResolveRequestId = 0;
	bool _isInlineBot = false;

	Webrtc::RecordAvailability _recordAvailability = {};

	std::unique_ptr<HistoryView::PaysStatus> _paysStatus;
	std::unique_ptr<HistoryView::ContactStatus> _contactStatus;
	std::unique_ptr<HistoryView::BusinessBotStatus> _businessBotStatus;

	const std::shared_ptr<Ui::SendButton> _send;
	object_ptr<Ui::FlatButton> _unblock;
	object_ptr<Ui::FlatButton> _botStart;
	object_ptr<Ui::FlatButton> _joinChannel;
	object_ptr<Ui::FlatButton> _muteUnmute;
	QPointer<Ui::IconButton> _giftToChannel;
	QPointer<Ui::IconButton> _directMessage;
	rpl::lifetime _directMessageLifetime;
	object_ptr<Ui::FlatButton> _reportMessages;
	struct {
		object_ptr<Ui::RoundButton> button = { nullptr };
		QString text;
		bool small = false;
	} _botMenu;
	object_ptr<Ui::IconButton> _attachToggle;
	object_ptr<Ui::IconButton> _replaceMedia = { nullptr };
	object_ptr<Ui::SendAsButton> _sendAs = { nullptr };
	object_ptr<Ui::EmojiButton> _tabbedSelectorToggle;
	object_ptr<Ui::IconButton> _botKeyboardShow;
	object_ptr<Ui::IconButton> _botKeyboardHide;
	object_ptr<Ui::IconButton> _botCommandStart;
	object_ptr<Ui::IconButton> _toggleSuggestPost = { nullptr };
	object_ptr<Ui::IconButton> _giftToUser = { nullptr };
	object_ptr<Ui::SilentToggle> _silent = { nullptr };
	object_ptr<Ui::IconButton> _scheduled = { nullptr };
	std::unique_ptr<HistoryView::Controls::TTLButton> _ttlInfo;
	const std::unique_ptr<VoiceRecordBar> _voiceRecordBar;
	const std::unique_ptr<ForwardPanel> _forwardPanel;
	std::unique_ptr<HistoryView::ComposeSearch> _composeSearch;
	std::unique_ptr<HistoryView::SubsectionTabs> _subsectionTabs;
	rpl::lifetime _subsectionTabsLifetime;
	rpl::lifetime _subsectionCheckLifetime;
	bool _cmdStartShown = false;
	object_ptr<Ui::InputField> _field;
	base::unique_qptr<Ui::RpWidget> _fieldDisabled;
	std::unique_ptr<Ui::RpWidget> _sendRestriction;
	using CharactersLimitLabel = HistoryView::Controls::CharactersLimitLabel;
	base::unique_qptr<CharactersLimitLabel> _charsLimitation;
	QString _sendRestrictionKey;
	Ui::Animations::Simple _inPhotoEditOver;
	bool _inDetails = false;
	bool _inPhotoEdit = false;
	bool _inClickable = false;

	bool _kbShown = false;
	bool _fieldIsEmpty = true;
	HistoryItem *_kbReplyTo = nullptr;
	object_ptr<Ui::ScrollArea> _kbScroll;
	const not_null<BotKeyboard*> _keyboard;

	FieldCharsCountManager _fieldCharsCountManager;

	std::unique_ptr<Ui::ChooseThemeController> _chooseTheme;

	object_ptr<Ui::InnerDropdown> _membersDropdown = { nullptr };
	base::Timer _membersDropdownShowTimer;

	object_ptr<InlineBots::Layout::Widget> _inlineResults = { nullptr };
	std::unique_ptr<TabbedPanel> _tabbedPanel;
	std::unique_ptr<Ui::DropdownMenu> _attachBotsMenu;

	DragArea::Areas _attachDragAreas;

	bool _nonEmptySelection = false;

	TextUpdateEvents _textUpdateEvents = (TextUpdateEvents() | TextUpdateEvent::SaveDraft | TextUpdateEvent::SendTyping);

	QString _confirmSource;

	std::unique_ptr<Window::SlideAnimation> _showAnimation;

	HistoryView::ElementHighlighter _highlighter;

	crl::time _saveDraftStart = 0;
	bool _saveDraftText = false;
	base::Timer _saveDraftTimer;
	base::Timer _saveCloudDraftTimer;

	HistoryView::InfoTooltip _topToast;
	std::unique_ptr<HistoryView::StickerToast> _stickerToast;
	std::unique_ptr<ChooseMessagesForReport> _chooseForReport;

	std::unique_ptr<HistoryView::PaidReactionToast> _paidReactionToast;

	base::flat_set<not_null<HistoryItem*>> _itemRevealPending;
	base::flat_map<
		not_null<HistoryItem*>,
		ItemRevealAnimation> _itemRevealAnimations;
	int _itemsRevealHeight = 0;

	bool _sponsoredMessagesStateKnown = false;
	bool _justMarkingAsRead = false;

	object_ptr<Ui::PlainShadow> _topShadow;
	bool _inGrab = false;

	int _topDelta = 0;

	SendPaymentHelper _sendPayment;

	rpl::event_stream<> _cancelRequests;

};
