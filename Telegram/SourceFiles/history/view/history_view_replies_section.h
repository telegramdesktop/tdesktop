/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/section_widget.h"
#include "window/section_memento.h"
#include "history/view/history_view_list_widget.h"
#include "data/data_messages.h"
#include "base/timer.h"

class History;
enum class CompressConfirm;
enum class SendMediaType;
struct SendingAlbum;

namespace SendMenu {
enum class Type;
} // namespace SendMenu

namespace Api {
struct SendOptions;
} // namespace Api

namespace Storage {
struct PreparedList;
} // namespace Storage

namespace Ui {
class ScrollArea;
class PlainShadow;
class FlatButton;
class HistoryDownButton;
} // namespace Ui

namespace Profile {
class BackButton;
} // namespace Profile

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace Data {
class RepliesList;
} // namespace Data

namespace HistoryView {

class Element;
class TopBarWidget;
class RepliesMemento;
class ComposeControls;

class RepliesWidget final
	: public Window::SectionWidget
	, private ListDelegate {
public:
	RepliesWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<History*> history,
		MsgId rootId);
	~RepliesWidget();

	[[nodiscard]] not_null<History*> history() const;
	Dialogs::RowDescriptor activeChat() const override;

	bool hasTopBarShadow() const override {
		return true;
	}

	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::unique_ptr<Window::SectionMemento> createMemento() override;
	bool showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) override;

	void setInternalState(
		const QRect &geometry,
		not_null<RepliesMemento*> memento);

	// Tabbed selector management.
	bool pushTabbedSelectorToThirdSection(
		not_null<PeerData*> peer,
		const Window::SectionShow &params) override;
	bool returnTabbedSelector() override;

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

	// ListDelegate interface.
	Context listContext() override;
	void listScrollTo(int top) override;
	void listCancelRequest() override;
	void listDeleteRequest() override;
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
	void listVisibleItemsChanged(HistoryItemsList &&items) override;
	std::optional<int> listUnreadBarView(
		const std::vector<not_null<Element*>> &elements) override;
	void listContentRefreshed() override;
	ClickHandlerPtr listDateLink(not_null<Element*> view) override;
	bool listElementHideReply(not_null<const Element*> view) override;
	bool listIsGoodForAroundPosition(not_null<const Element*> view) override;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;

private:
	void onScroll();
	void updateInnerVisibleArea();
	void updateControlsGeometry();
	void updateAdaptiveLayout();
	void saveState(not_null<RepliesMemento*> memento);
	void restoreState(not_null<RepliesMemento*> memento);
	void showAtEnd();
	void showAtPosition(
		Data::MessagePosition position,
		HistoryItem *originItem = nullptr);
	bool showAtPositionNow(
		Data::MessagePosition position,
		HistoryItem *originItem);
	void finishSending();

	void setupComposeControls();

	void setupRoot();
	void setupCommentsRoot();
	void refreshRootView();
	void setupDragArea();
	void sendReadTillRequest();
	void readTill(MsgId id);

	void setupScrollDownButton();
	void scrollDownClicked();
	void scrollDownAnimationFinish();
	void updateScrollDownVisibility();
	void updateScrollDownPosition();

	void confirmSendNowSelected();
	void confirmDeleteSelected();
	void confirmForwardSelected();
	void clearSelected();

	void send();
	void send(Api::SendOptions options);
	void edit(
		not_null<HistoryItem*> item,
		Api::SendOptions options,
		mtpRequestId *const saveEditMsgRequestId);
	void chooseAttach();
	[[nodiscard]] SendMenu::Type sendMenuType() const;
	[[nodiscard]] MsgId replyToId() const;
	[[nodiscard]] HistoryItem *lookupRoot() const;
	[[nodiscard]] HistoryItem *lookupCommentsRoot() const;
	[[nodiscard]] bool computeAreComments() const;

	void pushReplyReturn(not_null<HistoryItem*> item);
	void computeCurrentReplyReturn();
	void calculateNextReplyReturn();
	void restoreReplyReturns(const std::vector<MsgId> &list);
	void checkReplyReturns();

	void uploadFile(const QByteArray &fileContent, SendMediaType type);
	bool confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		Storage::PreparedList &&list,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		not_null<const QMimeData*> data,
		CompressConfirm compressed,
		const QString &insertTextOnCancel = QString());
	bool showSendingFilesError(const Storage::PreparedList &list) const;
	void uploadFilesAfterConfirmation(
		Storage::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		MsgId replyTo,
		Api::SendOptions options,
		std::shared_ptr<SendingAlbum> album);

	void sendExistingDocument(not_null<DocumentData*> document);
	bool sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options);
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
		Api::SendOptions options);

	void paintRoot(Painter &p);

	const not_null<History*> _history;
	const MsgId _rootId = 0;
	HistoryItem *_root = nullptr;
	HistoryItem *_commentsRoot = nullptr;
	std::shared_ptr<Data::RepliesList> _replies;
	rpl::variable<bool> _areComments = false;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<ListWidget> _inner;
	object_ptr<TopBarWidget> _topBar;
	object_ptr<Ui::PlainShadow> _topBarShadow;
	std::unique_ptr<ComposeControls> _composeControls;
	bool _skipScrollEvent = false;

	Ui::Text::String _rootTitle;
	Ui::Text::String _rootMessage;
	object_ptr<Ui::PlainShadow> _rootShadow;
	int _rootHeight = 0;

	std::vector<MsgId> _replyReturns;
	HistoryItem *_replyReturn = nullptr;

	Ui::Animations::Simple _scrollDownShown;
	bool _scrollDownIsShown = false;
	object_ptr<Ui::HistoryDownButton> _scrollDown;

	Data::MessagesSlice _lastSlice;
	bool _choosingAttach = false;

	base::Timer _readRequestTimer;
	mtpRequestId _readRequestId = 0;

};


class RepliesMemento : public Window::SectionMemento {
public:
	RepliesMemento(not_null<History*> history, MsgId rootId)
	: _history(history)
	, _rootId(rootId) {
	}
	explicit RepliesMemento(not_null<HistoryItem*> commentsItem);

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

	void setReplyReturns(const std::vector<MsgId> &list) {
		_replyReturns = list;
	}
	const std::vector<MsgId> &replyReturns() const {
		return _replyReturns;
	}

	[[nodiscard]] not_null<ListMemento*> list() {
		return &_list;
	}

private:
	const not_null<History*> _history;
	const MsgId _rootId = 0;
	ListMemento _list;
	std::shared_ptr<Data::RepliesList> _replies;
	std::vector<MsgId> _replyReturns;

};

} // namespace HistoryView
