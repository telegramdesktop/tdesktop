/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/tooltip.h"
#include "mainwidget.h"
#include "chat_helpers/field_autocomplete.h"
#include "window/section_widget.h"
#include "core/single_timer.h"
#include "ui/widgets/input_fields.h"
#include "ui/rp_widget.h"
#include "base/flags.h"
#include "base/timer.h"

struct FileLoadResult;
struct FileMediaInformation;
struct SendingAlbum;
enum class SendMediaType;
enum class CompressConfirm;
class MessageLinksParser;

namespace InlineBots {
namespace Layout {
class ItemBase;
class Widget;
} // namespace Layout
class Result;
} // namespace InlineBots

namespace Data {
struct Draft;
} // namespace Data

namespace Ui {
class AbstractButton;
class InnerDropdown;
class DropdownMenu;
class PlainShadow;
class PopupMenu;
class IconButton;
class HistoryDownButton;
class EmojiButton;
class SendButton;
class SilentToggle;
class FlatButton;
class LinkButton;
class RoundButton;
namespace Emoji {
class SuggestionsController;
} // namespace Emoji
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace ChatHelpers {
class TabbedPanel;
class TabbedSection;
class TabbedSelector;
} // namespace ChatHelpers

namespace Storage {
enum class MimeDataState;
struct PreparedList;
struct UploadedPhoto;
struct UploadedDocument;
struct UploadedThumbDocument;
} // namespace Storage

namespace HistoryView {
class TopBarWidget;
} // namespace HistoryView

class DragArea;
class SendFilesBox;
class BotKeyboard;
class MessageField;
class HistoryInner;
struct HistoryMessageMarkupButton;

class ReportSpamPanel : public TWidget {
	Q_OBJECT

public:
	ReportSpamPanel(QWidget *parent);

	void setReported(bool reported, PeerData *onPeer);

signals:
	void hideClicked();
	void reportClicked();
	void clearClicked();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	object_ptr<Ui::FlatButton> _report;
	object_ptr<Ui::FlatButton> _hide;
	object_ptr<Ui::LinkButton> _clear;

};

class HistoryHider : public Ui::RpWidget, private base::Subscriber {
	Q_OBJECT

public:
	HistoryHider(MainWidget *parent, MessageIdsList &&items); // forward messages
	HistoryHider(MainWidget *parent); // send path from command line argument
	HistoryHider(MainWidget *parent, const QString &url, const QString &text); // share url
	HistoryHider(MainWidget *parent, const QString &botAndQuery); // inline switch button handler

	bool withConfirm() const;

	bool offerPeer(PeerId peer);
	QString offeredText() const;
	QString botAndQuery() const {
		return _botAndQuery;
	}

	bool wasOffered() const;

	void forwardDone();

	~HistoryHider();

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

public slots:
	void startHide();
	void forward();

signals:
	void forwarded();

private:
	void refreshLang();
	void updateControlsGeometry();
	void animationCallback();
	void init();
	MainWidget *parent();

	MessageIdsList _forwardItems;
	bool _sendPath = false;

	QString _shareUrl, _shareText;
	QString _botAndQuery;

	object_ptr<Ui::RoundButton> _send;
	object_ptr<Ui::RoundButton> _cancel;
	PeerData *_offered = nullptr;

	Animation _a_opacity;

	QRect _box;
	bool _hiding = false;

	mtpRequestId _forwardRequest = 0;

	int _chooseWidth = 0;

	Text _toText;
	int32 _toTextWidth = 0;
	QPixmap _cacheForAnim;

};

class HistoryWidget final : public Window::AbstractSectionWidget, public RPCSender {
	Q_OBJECT

public:
	using FieldHistoryAction = Ui::InputField::HistoryAction;

	HistoryWidget(QWidget *parent, not_null<Window::Controller*> controller);

	void start();

	void messagesReceived(PeerData *peer, const MTPmessages_Messages &messages, mtpRequestId requestId);
	void historyLoaded();

	void windowShown();
	bool doWeReadServerHistory() const;
	bool doWeReadMentions() const;
	bool skipItemRepaint();

	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

	bool isItemCompletelyHidden(HistoryItem *item) const;
	void updateTopBarSelection();

	void loadMessages();
	void loadMessagesDown();
	void firstLoadMessages();
	void delayedShowAt(MsgId showAtMsgId);

	void newUnreadMsg(
		not_null<History*> history,
		not_null<HistoryItem*> item);
	void historyToDown(History *history);

	QRect historyRect() const;
	void pushTabbedSelectorToThirdSection(
		const Window::SectionShow &params);

	void updateRecentStickers();

	void destroyData();

	void updateFieldPlaceholder();
	void updateStickersByEmoji();

	bool confirmSendingFiles(const QStringList &files);
	bool confirmSendingFiles(not_null<const QMimeData*> data);
	void sendFileConfirmed(const std::shared_ptr<FileLoadResult> &file);

	void updateControlsVisibility();
	void updateControlsGeometry();

	History *history() const;
	PeerData *peer() const;
	void setMsgId(MsgId showAtMsgId);
	MsgId msgId() const;

	bool hasTopBarShadow() const {
		return peer() != nullptr;
	}
	void showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params);
	void finishAnimating();

	void doneShow();

	QPoint clampMousePosition(QPoint point);

	void checkSelectingScroll(QPoint point);
	void noSelectingScroll();

	bool touchScroll(const QPoint &delta);

	void enqueueMessageHighlight(not_null<HistoryView::Element*> view);
	TimeMs highlightStartTime(not_null<const HistoryItem*> item) const;
	bool inSelectionMode() const;

	MessageIdsList getSelectedItems() const;
	void itemEdited(HistoryItem *item);

	void updateScrollColors();

	void replyToMessage(FullMsgId itemId);
	void replyToMessage(not_null<HistoryItem*> item);
	void editMessage(FullMsgId itemId);
	void editMessage(not_null<HistoryItem*> item);
	void pinMessage(FullMsgId itemId);
	void unpinMessage(FullMsgId itemId);
	void copyPostLink(FullMsgId itemId);

	MsgId replyToId() const;
	void messageDataReceived(ChannelData *channel, MsgId msgId);
	bool lastForceReplyReplied(const FullMsgId &replyTo) const;
	bool lastForceReplyReplied() const;
	bool cancelReply(bool lastKeyboardUsed = false);
	void cancelEdit();
	void updateForwarding();
	void updateForwardingTexts();

	void clearReplyReturns();
	void pushReplyReturn(not_null<HistoryItem*> item);
	QList<MsgId> replyReturns();
	void setReplyReturns(PeerId peer, const QList<MsgId> &replyReturns);
	void calcNextReplyReturn();

	void updatePreview();
	void previewCancel();

	void step_recording(float64 ms, bool timer);
	void stopRecording(bool send);

	void onListEscapePressed();
	void onListEnterPressed();

	void sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo);
	void hideSingleUseKeyboard(PeerData *peer, MsgId replyTo);
	bool insertBotCommand(const QString &cmd);

	bool eventFilter(QObject *obj, QEvent *e) override;

	// With force=true the markup is updated even if it is
	// already shown for the passed history item.
	void updateBotKeyboard(History *h = nullptr, bool force = false);

	void fastShowAtEnd(not_null<History*> history);
	void applyDraft(
		FieldHistoryAction fieldHistoryAction = FieldHistoryAction::Clear);
	void showHistory(const PeerId &peer, MsgId showAtMsgId, bool reload = false);
	void clearDelayedShowAt();
	void clearAllLoadRequests();
	void saveFieldToHistoryLocalDraft();

	void applyCloudDraft(History *history);

	void updateHistoryDownPosition();
	void updateHistoryDownVisibility();
	void updateUnreadMentionsPosition();
	void updateUnreadMentionsVisibility();

	void updateFieldSubmitSettings();

	void setInnerFocus();

	void updateNotifyControls();

	bool contentOverlapped(const QRect &globalRect);

	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params);

	void forwardSelected();
	void confirmDeleteSelected();
	void clearSelected();

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e) override;
	QRect rectForFloatPlayer() const override;

	void app_sendBotCallback(
		not_null<const HistoryMessageMarkupButton*> button,
		not_null<const HistoryItem*> msg,
		int row,
		int column);

	PeerData *ui_getPeerForMouseAction();

	void notify_botCommandsChanged(UserData *user);
	void notify_inlineBotRequesting(bool requesting);
	void notify_replyMarkupUpdated(const HistoryItem *item);
	void notify_inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop);
	bool notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo);
	void notify_userIsBotChanged(UserData *user);
	void notify_migrateUpdated(PeerData *peer);

	bool cmd_search();
	bool cmd_next_chat();
	bool cmd_previous_chat();

	~HistoryWidget();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

signals:
	void cancelled();

public slots:
	void onCancel();
	void onPinnedHide();
	void onFieldBarCancel();

	void onReportSpamClicked();
	void onReportSpamHide();
	void onReportSpamClear();

	void onScroll();

	void onUnblock();
	void onBotStart();
	void onJoinChannel();
	void onMuteUnmute();
	void onBroadcastSilentChange();

	void onKbToggle(bool manual = true);
	void onCmdStart();

	void activate();
	void onTextChange();

	void onFieldTabbed();
	bool onStickerOrGifSend(not_null<DocumentData*> document);
	void onPhotoSend(not_null<PhotoData*> photo);
	void onInlineResultSend(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot);

	void onWindowVisibleChanged();

	void onFieldFocused();
	void onFieldResize();
	void onCheckFieldAutocomplete();
	void onScrollTimer();

	void onDraftSaveDelayed();
	void onDraftSave(bool delayed = false);
	void onCloudDraftSave();

	void onRecordError();
	void onRecordDone(QByteArray result, VoiceWaveform waveform, qint32 samples);
	void onRecordUpdate(quint16 level, qint32 samples);

	void onUpdateHistoryItems();

	// checks if we are too close to the top or to the bottom
	// in the scroll area and preloads history if needed
	void preloadHistoryIfNeeded();

private slots:
	void onHashtagOrBotCommandInsert(QString str, FieldAutocomplete::ChooseMethod method);
	void onMentionInsert(UserData *user);
	void onInlineBotCancel();
	void onMembersDropdownShow();

	void onModerateKeyActivate(int index, bool *outHandled);

	void updateField();

private:
	using TabbedPanel = ChatHelpers::TabbedPanel;
	using TabbedSelector = ChatHelpers::TabbedSelector;
	using DragState = Storage::MimeDataState;

	void send();
	void handlePendingHistoryUpdate();
	void fullPeerUpdated(PeerData *peer);
	void toggleTabbedSelectorMode();
	void returnTabbedSelector(object_ptr<TabbedSelector> selector);
	void recountChatWidth();
	void setReportSpamStatus(DBIPeerReportSpamStatus status);
	void historyDownClicked();
	void showNextUnreadMention();
	void handlePeerUpdate();
	void setMembersShowAreaActive(bool active);
	void forwardItems(MessageIdsList &&items);
	void handleHistoryChange(not_null<const History*> history);
	void refreshAboutProxyPromotion();
	void unreadCountUpdated();

	void highlightMessage(MsgId universalMessageId);
	void adjustHighlightedMessageToMigrated();
	void checkNextHighlight();
	void updateHighlightedMessage();
	void clearHighlightMessages();
	void stopMessageHighlight();

	void updateSendAction(
		not_null<History*> history,
		SendAction::Type type,
		int32 progress = 0);
	void cancelSendAction(
		not_null<History*> history,
		SendAction::Type type);
	void cancelTypingAction();
	void sendActionDone(const MTPBool &result, mtpRequestId req);

	void animationCallback();
	void updateOverStates(QPoint pos);
	void recordStartCallback();
	void recordStopCallback(bool active);
	void recordUpdateCallback(QPoint globalPos);
	void chooseAttach();
	void historyDownAnimationFinish();
	void unreadMentionsAnimationFinish();
	void sendButtonClicked();

	bool canSendFiles(not_null<const QMimeData*> data) const;
	bool confirmSendingFiles(
		const QStringList &files,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		not_null<const QMimeData*> data,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		Storage::PreparedList &&list,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool showSendingFilesError(const Storage::PreparedList &list) const;

	void uploadFiles(Storage::PreparedList &&list, SendMediaType type);
	void uploadFile(const QByteArray &fileContent, SendMediaType type);

	void uploadFilesAfterConfirmation(
		Storage::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		MsgId replyTo,
		std::shared_ptr<SendingAlbum> album = nullptr);

	void subscribeToUploader();

	void photoUploaded(
		const FullMsgId &msgId,
		bool silent,
		const MTPInputFile &file);
	void photoProgress(const FullMsgId &msgId);
	void photoFailed(const FullMsgId &msgId);
	void documentUploaded(
		const FullMsgId &msgId,
		bool silent,
		const MTPInputFile &file);
	void thumbDocumentUploaded(
		const FullMsgId &msgId,
		bool silent,
		const MTPInputFile &file,
		const MTPInputFile &thumb);
	void documentProgress(const FullMsgId &msgId);
	void documentFailed(const FullMsgId &msgId);

	void itemRemoved(not_null<const HistoryItem*> item);

	// Updates position of controls around the message field,
	// like send button, emoji button and others.
	void moveFieldControls();
	void updateFieldSize();
	void updateTabbedSelectorToggleTooltipGeometry();
	void checkTabbedSelectorToggleTooltip();

	bool canWriteMessage() const;
	bool isRestrictedWrite() const;
	void orderWidgets();

	void clearInlineBot();
	void inlineBotChanged();

	// Look in the _field for the inline bot and query string.
	void updateInlineBotQuery();

	// Request to show results in the emoji panel.
	void applyInlineBotQuery(UserData *bot, const QString &query);

	void cancelReplyAfterMediaSend(bool lastKeyboardUsed);
	void replyToPreviousMessage();
	void replyToNextMessage();

	void hideSelectorControlsAnimated();
	int countMembersDropdownHeightMax() const;

	void updateReplyToName();
	void checkForwardingInfo();
	bool editingMessage() const {
		return _editMsgId != 0;
	}

	MsgId _replyToId = 0;
	Text _replyToName;
	int _replyToNameVersion = 0;

	HistoryItemsList _toForward;
	Text _toForwardFrom, _toForwardText;
	int _toForwardNameVersion = 0;

	MsgId _editMsgId = 0;

	HistoryItem *_replyEditMsg = nullptr;
	Text _replyEditMsgText;
	mutable SingleTimer _updateEditTimeLeftDisplay;

	object_ptr<Ui::IconButton> _fieldBarCancel;
	void updateReplyEditTexts(bool force = false);
	void updateReplyEditText(not_null<HistoryItem*> item);

	struct PinnedBar {
		PinnedBar(MsgId msgId, HistoryWidget *parent);
		~PinnedBar();

		MsgId msgId = 0;
		HistoryItem *msg = nullptr;
		Text text;
		object_ptr<Ui::IconButton> cancel;
		object_ptr<Ui::PlainShadow> shadow;
	};
	std::unique_ptr<PinnedBar> _pinnedBar;
	void updatePinnedBar(bool force = false);
	bool pinnedMsgVisibilityUpdated();
	void destroyPinnedBar();
	void unpinDone(const MTPUpdates &updates);

	bool sendExistingDocument(
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		TextWithEntities caption);
	void sendExistingPhoto(
		not_null<PhotoData*> photo,
		TextWithEntities caption);

	void drawField(Painter &p, const QRect &rect);
	void paintEditHeader(Painter &p, const QRect &rect, int left, int top) const;
	void drawRecording(Painter &p, float64 recordActive);
	void drawPinnedBar(Painter &p);
	void drawRestrictedWrite(Painter &p);
	bool paintShowAnimationFrame(TimeMs ms);

	void updateMouseTracking();

	// destroys _history and _migrated unread bars
	void destroyUnreadBar();

	mtpRequestId _saveEditMsgRequestId = 0;
	void saveEditMsg();
	void saveEditMsgDone(History *history, const MTPUpdates &updates, mtpRequestId req);
	bool saveEditMsgFail(History *history, const RPCError &error, mtpRequestId req);

	void updateReportSpamStatus();
	void requestReportSpamSetting();
	void reportSpamSettingDone(const MTPPeerSettings &result, mtpRequestId req);
	bool reportSpamSettingFail(const RPCError &error, mtpRequestId req);

	void checkPreview();
	void requestPreview();
	void gotPreview(QString links, const MTPMessageMedia &media, mtpRequestId req);

	static const mtpRequestId ReportSpamRequestNeeded = -1;
	DBIPeerReportSpamStatus _reportSpamStatus = dbiprsUnknown;
	mtpRequestId _reportSpamSettingRequestId = ReportSpamRequestNeeded;

	QStringList _parsedLinks;
	QString _previewLinks;
	WebPageData *_previewData = nullptr;
	typedef QMap<QString, WebPageId> PreviewCache;
	PreviewCache _previewCache;
	mtpRequestId _previewRequest = 0;
	Text _previewTitle;
	Text _previewDescription;
	base::Timer _previewTimer;
	bool _previewCancelled = false;

	bool _replyForwardPressed = false;

	HistoryItem *_replyReturn = nullptr;
	QList<MsgId> _replyReturns;

	bool messagesFailed(const RPCError &error, mtpRequestId requestId);
	void addMessagesToFront(PeerData *peer, const QVector<MTPMessage> &messages);
	void addMessagesToBack(PeerData *peer, const QVector<MTPMessage> &messages);

	struct BotCallbackInfo {
		UserData *bot;
		FullMsgId msgId;
		int row, col;
		bool game;
	};
	void botCallbackDone(BotCallbackInfo info, const MTPmessages_BotCallbackAnswer &answer, mtpRequestId req);
	bool botCallbackFail(BotCallbackInfo info, const RPCError &error, mtpRequestId req);

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
	void updateHistoryGeometry(bool initial = false, bool loadedDown = false, const ScrollChange &change = { ScrollChangeNone, 0 });
	void updateListSize();

	// Does any of the shown histories has this flag set.
	bool hasPendingResizedItems() const;

	// Counts scrollTop for placing the scroll right at the unread
	// messages bar, choosing from _history and _migrated unreadBar.
	std::optional<int> unreadBarTop() const;
	int itemTopForHighlight(not_null<HistoryView::Element*> view) const;
	void scrollToCurrentVoiceMessage(FullMsgId fromId, FullMsgId toId);
	HistoryView::Element *firstUnreadMessage() const;

	// Scroll to current y without updating the _lastUserScrolled time.
	// Used to distinguish between user scrolls and syntetic scrolls.
	// This one is syntetic.
	void synteticScrollToY(int y);

	void reportSpamDone(PeerData *peer, const MTPBool &result, mtpRequestId request);
	bool reportSpamFail(const RPCError &error, mtpRequestId request);

	void unblockDone(PeerData *peer, const MTPBool &result, mtpRequestId req);
	bool unblockFail(const RPCError &error, mtpRequestId req);
	void blockDone(PeerData *peer, const MTPBool &result);

	void countHistoryShowFrom();

	enum class TextUpdateEvent {
		SaveDraft  = (1 << 0),
		SendTyping = (1 << 1),
	};
	using TextUpdateEvents = base::flags<TextUpdateEvent>;
	friend inline constexpr bool is_flag_type(TextUpdateEvent) { return true; };

	void writeDrafts(Data::Draft **localDraft, Data::Draft **editDraft);
	void writeDrafts(History *history);
	void setFieldText(
		const TextWithTags &textWithTags,
		TextUpdateEvents events = 0,
		FieldHistoryAction fieldHistoryAction = FieldHistoryAction::Clear);
	void clearFieldText(
		TextUpdateEvents events = 0,
		FieldHistoryAction fieldHistoryAction = FieldHistoryAction::Clear);

	HistoryItem *getItemFromHistoryOrMigrated(MsgId genericMsgId) const;
	void animatedScrollToItem(MsgId msgId);
	void animatedScrollToY(int scrollTo, HistoryItem *attachTo = nullptr);

	void updateDragAreas();

	// when scroll position or scroll area size changed this method
	// updates the boundings of the visible area in HistoryInner
	void visibleAreaUpdated();
	int countInitialScrollTop();
	int countAutomaticScrollTop();
	void preloadHistoryByScroll();
	void checkReplyReturns();
	void scrollToAnimationCallback(FullMsgId attachToId);

	bool readyToForward() const;
	bool hasSilentToggle() const;

	PeerData *_peer = nullptr;

	ChannelId _channel = NoChannel;
	bool _canSendMessages = false;
	MsgId _showAtMsgId = ShowAtUnreadMsgId;

	mtpRequestId _firstLoadRequest = 0;
	mtpRequestId _preloadRequest = 0;
	mtpRequestId _preloadDownRequest = 0;

	MsgId _delayedShowAtMsgId = -1;
	mtpRequestId _delayedShowAtRequest = 0;

	object_ptr<HistoryView::TopBarWidget> _topBar;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<HistoryInner> _list;
	History *_migrated = nullptr;
	History *_history = nullptr;
	// Initial updateHistoryGeometry() was called.
	bool _historyInited = false;
	// If updateListSize() was called without updateHistoryGeometry().
	bool _updateHistoryGeometryRequired = false;
	int _addToScroll = 0;

	int _lastScrollTop = 0; // gifs optimization
	TimeMs _lastScrolled = 0;
	QTimer _updateHistoryItems;

	TimeMs _lastUserScrolled = 0;
	bool _synteticScrollEvent = false;
	Animation _scrollToAnimation;

	Animation _historyDownShown;
	bool _historyDownIsShown = false;
	object_ptr<Ui::HistoryDownButton> _historyDown;

	Animation _unreadMentionsShown;
	bool _unreadMentionsIsShown = false;
	object_ptr<Ui::HistoryDownButton> _unreadMentions;

	object_ptr<FieldAutocomplete> _fieldAutocomplete;
	std::unique_ptr<MessageLinksParser> _fieldLinksParser;

	UserData *_inlineBot = nullptr;
	QString _inlineBotUsername;
	bool _inlineLookingUpBot = false;
	mtpRequestId _inlineBotResolveRequestId = 0;
	bool _isInlineBot = false;
	void inlineBotResolveDone(const MTPcontacts_ResolvedPeer &result);
	bool inlineBotResolveFail(QString name, const RPCError &error);

	bool isBotStart() const;
	bool isBlocked() const;
	bool isJoinChannel() const;
	bool isMuteUnmute() const;
	bool updateCmdStartShown();
	void updateSendButtonType();
	bool showRecordButton() const;
	bool showInlineBotCancel() const;
	void refreshSilentToggle();

	object_ptr<ReportSpamPanel> _reportSpamPanel = { nullptr };

	object_ptr<Ui::SendButton> _send;
	object_ptr<Ui::FlatButton> _unblock;
	object_ptr<Ui::FlatButton> _botStart;
	object_ptr<Ui::FlatButton> _joinChannel;
	object_ptr<Ui::FlatButton> _muteUnmute;
	object_ptr<Ui::RpWidget> _aboutProxyPromotion = { nullptr };
	mtpRequestId _unblockRequest = 0;
	mtpRequestId _reportSpamRequest = 0;
	object_ptr<Ui::IconButton> _attachToggle;
	object_ptr<Ui::EmojiButton> _tabbedSelectorToggle;
	object_ptr<Ui::ImportantTooltip> _tabbedSelectorToggleTooltip = { nullptr };
	bool _tabbedSelectorToggleTooltipShown = false;
	object_ptr<Ui::IconButton> _botKeyboardShow;
	object_ptr<Ui::IconButton> _botKeyboardHide;
	object_ptr<Ui::IconButton> _botCommandStart;
	object_ptr<Ui::SilentToggle> _silent = { nullptr };
	bool _cmdStartShown = false;
	object_ptr<Ui::InputField> _field;
	bool _recording = false;
	bool _inField = false;
	bool _inReplyEditForward = false;
	bool _inPinnedMsg = false;
	bool _inClickable = false;
	int _recordingSamples = 0;
	int _recordCancelWidth;

	rpl::lifetime _uploaderSubscriptions;

	// This can animate for a very long time (like in music playing),
	// so it should be a BasicAnimation, not an Animation.
	BasicAnimation _a_recording;
	anim::value a_recordingLevel;

	bool kbWasHidden() const;

	bool _kbShown = false;
	HistoryItem *_kbReplyTo = nullptr;
	object_ptr<Ui::ScrollArea> _kbScroll;
	QPointer<BotKeyboard> _keyboard;

	object_ptr<Ui::InnerDropdown> _membersDropdown = { nullptr };
	QTimer _membersDropdownShowTimer;

	object_ptr<InlineBots::Layout::Widget> _inlineResults = { nullptr };
	object_ptr<TabbedPanel> _tabbedPanel;
	QPointer<TabbedSelector> _tabbedSelector;
	DragState _attachDragState;
	object_ptr<DragArea> _attachDragDocument, _attachDragPhoto;

	object_ptr<Ui::Emoji::SuggestionsController> _emojiSuggestions = { nullptr };

	bool _nonEmptySelection = false;

	TextUpdateEvents _textUpdateEvents = (TextUpdateEvents() | TextUpdateEvent::SaveDraft | TextUpdateEvent::SendTyping);

	int64 _serviceImageCacheSize = 0;
	QString _confirmSource;

	Animation _a_show;
	Window::SlideDirection _showDirection;
	QPixmap _cacheUnder, _cacheOver;

	QTimer _scrollTimer;
	int32 _scrollDelta = 0;

	MsgId _highlightedMessageId = 0;
	std::deque<MsgId> _highlightQueue;
	base::Timer _highlightTimer;
	TimeMs _highlightStart = 0;

	QMap<QPair<not_null<History*>, SendAction::Type>, mtpRequestId> _sendActionRequests;
	base::Timer _sendActionStopTimer;

	TimeMs _saveDraftStart = 0;
	bool _saveDraftText = false;
	QTimer _saveDraftTimer, _saveCloudDraftTimer;

	object_ptr<Ui::PlainShadow> _topShadow;
	bool _inGrab = false;

};
