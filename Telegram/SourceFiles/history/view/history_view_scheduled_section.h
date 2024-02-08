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
#include "history/view/history_view_corner_buttons.h"
#include "data/data_messages.h"

class History;
enum class SendMediaType;
struct SendingAlbum;

namespace SendMenu {
enum class Type;
} // namespace SendMenu

namespace Api {
struct SendOptions;
struct SendAction;
} // namespace Api

namespace Ui {
class ScrollArea;
class PlainShadow;
class FlatButton;
struct PreparedList;
class SendFilesWay;
} // namespace Ui

namespace Profile {
class BackButton;
} // namespace Profile

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace HistoryView {

class Element;
class TopBarWidget;
class ScheduledMemento;
class ComposeControls;
class StickerToast;

class ScheduledWidget final
	: public Window::SectionWidget
	, private ListDelegate
	, private CornerButtonsDelegate {
public:
	ScheduledWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<History*> history);
	~ScheduledWidget();

	not_null<History*> history() const;
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
	using SectionWidget::confirmSendingFiles;

	void setInternalState(
		const QRect &geometry,
		not_null<ScheduledMemento*> memento);

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

private:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;
	void checkActivation() override;

	void onScroll();
	void updateInnerVisibleArea();
	void updateControlsGeometry();
	void updateAdaptiveLayout();
	void saveState(not_null<ScheduledMemento*> memento);
	void restoreState(not_null<ScheduledMemento*> memento);
	void showAtPosition(
		Data::MessagePosition position,
		FullMsgId originId = {});

	void setupComposeControls();

	void setupDragArea();

	void confirmSendNowSelected();
	void confirmDeleteSelected();
	void clearSelected();

	[[nodiscard]] Api::SendAction prepareSendAction(
		Api::SendOptions options) const;
	void send();
	void send(Api::SendOptions options);
	void sendVoice(
		QByteArray bytes,
		VoiceWaveform waveform,
		crl::time duration);
	void sendVoice(
		QByteArray bytes,
		VoiceWaveform waveform,
		crl::time duration,
		Api::SendOptions options);
	void edit(
		not_null<HistoryItem*> item,
		Api::SendOptions options,
		mtpRequestId *const saveEditMsgRequestId);
	void highlightSingleNewMessage(const Data::MessagesSlice &slice);
	void chooseAttach();
	[[nodiscard]] SendMenu::Type sendMenuType() const;

	void pushReplyReturn(not_null<HistoryItem*> item);
	void checkReplyReturns();

	void uploadFile(const QByteArray &fileContent, SendMediaType type);
	bool confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		std::optional<bool> overrideSendImagesAsPhotos = std::nullopt,
		const QString &insertTextOnCancel = QString());
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

	const not_null<History*> _history;
	std::shared_ptr<Ui::ChatTheme> _theme;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<ListWidget> _inner;
	object_ptr<TopBarWidget> _topBar;
	object_ptr<Ui::PlainShadow> _topBarShadow;
	std::unique_ptr<ComposeControls> _composeControls;
	bool _skipScrollEvent = false;

	std::unique_ptr<HistoryView::StickerToast> _stickerToast;

	CornerButtons _cornerButtons;

	Data::MessagesSlice _lastSlice;
	bool _choosingAttach = false;

};

class ScheduledMemento : public Window::SectionMemento {
public:
	ScheduledMemento(not_null<History*> history)
	: _history(history) {
	}

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	not_null<History*> getHistory() const {
		return _history;
	}

	not_null<ListMemento*> list() {
		return &_list;
	}

private:
	const not_null<History*> _history;
	ListMemento _list;

};

} // namespace HistoryView
