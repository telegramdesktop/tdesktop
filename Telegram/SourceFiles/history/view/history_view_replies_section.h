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
#include "history/history_view_swipe_data.h"
#include "data/data_messages.h"
#include "base/timer.h"

class History;
enum class SendMediaType;
struct SendingAlbum;

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace Api {
struct MessageToSend;
struct SendOptions;
struct SendAction;
} // namespace Api

namespace Storage {
} // namespace Storage

namespace Ui {
class ScrollArea;
class PlainShadow;
class FlatButton;
class PinnedBar;
struct PreparedList;
class SendFilesWay;
} // namespace Ui

namespace Profile {
class BackButton;
} // namespace Profile

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace Data {
class RepliesList;
class ForumTopic;
} // namespace Data

namespace HistoryView {

namespace Controls {
struct VoiceToSend;
} // namespace Controls

class Element;
class TopBarWidget;
class RepliesMemento;
class ComposeControls;
class ComposeSearch;
class SendActionPainter;
class StickerToast;
class TopicReopenBar;
class EmptyPainter;
class PinnedTracker;
class TranslateBar;

class RepliesWidget final
	: public Window::SectionWidget
	, private WindowListDelegate
	, private CornerButtonsDelegate {
public:
	RepliesWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<History*> history,
		MsgId rootId);
	~RepliesWidget();

	[[nodiscard]] not_null<History*> history() const;
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
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	bool showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) override;

	Window::SectionActionResult sendBotCommand(
		Bot::SendCommandRequest request) override;

	bool confirmSendingFiles(const QStringList &files) override;
	bool confirmSendingFiles(not_null<const QMimeData*> data) override;

	void setInternalState(
		const QRect &geometry,
		not_null<RepliesMemento*> memento);

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
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;
	void showFinishedHook() override;
	void checkActivation() override;
	void doSetInnerFocus() override;

	void onScroll();
	void updateInnerVisibleArea();
	void updateControlsGeometry();
	void updateAdaptiveLayout();
	void saveState(not_null<RepliesMemento*> memento);
	void restoreState(not_null<RepliesMemento*> memento);
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
	void setupSwipeReply();

	void setupRoot();
	void setupRootView();
	void setupTopicViewer();
	void subscribeToTopic();
	void subscribeToPinnedMessages();
	void setTopic(Data::ForumTopic *topic);
	void setupDragArea();
	void setupShortcuts();
	void setupTranslateBar();

	void searchInTopic();
	void updatePinnedVisibility();

	void confirmDeleteSelected();
	void confirmForwardSelected();
	void clearSelected();
	void setPinnedVisibility(bool shown);

	[[nodiscard]] Api::SendAction prepareSendAction(
		Api::SendOptions options) const;
	void send();
	void send(Api::SendOptions options);
	void sendVoice(Controls::VoiceToSend &&data);
	void edit(
		not_null<HistoryItem*> item,
		Api::SendOptions options,
		mtpRequestId *const saveEditMsgRequestId,
		bool spoilered);
	void chooseAttach(std::optional<bool> overrideSendImagesAsPhotos);
	[[nodiscard]] SendMenu::Details sendMenuDetails() const;
	[[nodiscard]] FullReplyTo replyTo() const;
	[[nodiscard]] HistoryItem *lookupRoot() const;
	[[nodiscard]] Data::ForumTopic *lookupTopic();
	[[nodiscard]] bool computeAreComments() const;
	void orderWidgets();

	void pushReplyReturn(not_null<HistoryItem*> item);
	void checkReplyReturns();
	void recountChatWidth();
	void replyToMessage(FullReplyTo id);
	void refreshTopBarActiveChat();
	void refreshUnreadCountBadge(std::optional<int> count);

	void hidePinnedMessage();
	void updatePinnedViewer();
	void setupPinnedTracker();
	void checkPinnedBarState();
	void clearHidingPinnedBar();
	void refreshPinnedBarButton(bool many, HistoryItem *item);
	void checkLastPinnedClickedIdReset(
		int wasScrollTop,
		int nowScrollTop);

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

	bool sendExistingDocument(
		not_null<DocumentData*> document,
		Api::MessageToSend messageToSend,
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

	void setupEmptyPainter();
	void refreshJoinGroupButton();
	[[nodiscard]] bool emptyShown() const;
	[[nodiscard]] bool showSlowmodeError();

	const not_null<History*> _history;
	MsgId _rootId = 0;
	std::shared_ptr<Ui::ChatTheme> _theme;
	HistoryItem *_root = nullptr;
	Data::ForumTopic *_topic = nullptr;
	mutable bool _newTopicDiscarded = false;

	std::shared_ptr<Data::RepliesList> _replies;
	rpl::lifetime _repliesLifetime;
	rpl::variable<bool> _areComments = false;
	std::shared_ptr<SendActionPainter> _sendAction;
	QPointer<ListWidget> _inner;
	object_ptr<TopBarWidget> _topBar;
	object_ptr<Ui::PlainShadow> _topBarShadow;
	std::unique_ptr<ComposeControls> _composeControls;
	std::unique_ptr<ComposeSearch> _composeSearch;
	std::unique_ptr<Ui::FlatButton> _joinGroup;
	std::unique_ptr<TopicReopenBar> _topicReopenBar;
	std::unique_ptr<EmptyPainter> _emptyPainter;
	bool _skipScrollEvent = false;
	bool _synteticScrollEvent = false;

	std::unique_ptr<TranslateBar> _translateBar;
	int _translateBarHeight = 0;

	std::unique_ptr<PinnedTracker> _pinnedTracker;
	std::unique_ptr<Ui::PinnedBar> _pinnedBar;
	std::unique_ptr<Ui::PinnedBar> _hidingPinnedBar;
	int _pinnedBarHeight = 0;
	FullMsgId _pinnedClickedId;
	std::optional<FullMsgId> _minPinnedId;
	HistoryItem *_shownPinnedItem = nullptr;

	std::unique_ptr<Ui::PinnedBar> _rootView;
	int _rootViewHeight = 0;
	bool _rootViewInited = false;
	bool _rootViewInitScheduled = false;
	rpl::variable<bool> _rootVisible = false;

	std::unique_ptr<Ui::ScrollArea> _scroll;
	std::unique_ptr<HistoryView::StickerToast> _stickerToast;

	FullMsgId _lastShownAt;
	HistoryView::CornerButtons _cornerButtons;
	rpl::lifetime _topicLifetime;

	HistoryView::ChatPaintGestureHorizontalData _gestureHorizontal;

	int _lastScrollTop = 0;
	int _topicReopenBarHeight = 0;
	int _scrollTopDelta = 0;

	bool _choosingAttach = false;

	bool _loaded = false;

};

class RepliesMemento final : public Window::SectionMemento {
public:
	RepliesMemento(
		not_null<History*> history,
		MsgId rootId,
		MsgId highlightId = 0,
		const TextWithEntities &highlightPart = {},
		int highlightPartOffsetHint = 0);
	explicit RepliesMemento(
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

	[[nodiscard]] not_null<History*> getHistory() const {
		return _history;
	}
	[[nodiscard]] MsgId getRootId() const {
		return _rootId;
	}

	void setReplies(std::shared_ptr<Data::RepliesList> replies) {
		_replies = std::move(replies);
	}
	[[nodiscard]] std::shared_ptr<Data::RepliesList> getReplies() const {
		return _replies;
	}

	void setFromTopic(not_null<Data::ForumTopic*> topic);

	void setReplyReturns(const QVector<FullMsgId> &list) {
		_replyReturns = list;
	}
	const QVector<FullMsgId> &replyReturns() const {
		return _replyReturns;
	}

	Data::ForumTopic *topicForRemoveRequests() const override;

	[[nodiscard]] not_null<ListMemento*> list() {
		return &_list;
	}
	[[nodiscard]] MsgId highlightId() const {
		return _highlightId;
	}
	[[nodiscard]] const TextWithEntities &highlightPart() const {
		return _highlightPart;
	}
	[[nodiscard]] int highlightPartOffsetHint() const {
		return _highlightPartOffsetHint;
	}

private:
	void setupTopicViewer();

	const not_null<History*> _history;
	MsgId _rootId = 0;
	const TextWithEntities _highlightPart;
	const int _highlightPartOffsetHint = 0;
	const MsgId _highlightId = 0;
	ListMemento _list;
	std::shared_ptr<Data::RepliesList> _replies;
	QVector<FullMsgId> _replyReturns;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
