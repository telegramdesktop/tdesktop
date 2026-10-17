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
class QImage;
enum class SendMediaType;

namespace Api {
struct SendAction;
struct SendOptions;
} // namespace Api

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace Ui {
class ElasticScroll;
class PlainShadow;
struct PreparedBundle;
struct PreparedList;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

class Element;
class TopBarWidget;
class WelcomeMessagesMemento;
class ComposeControls;
class StickerToast;

class WelcomeMessagesWidget final
	: public Window::SectionWidget
	, private WindowListDelegate
	, private CornerButtonsDelegate {
public:
	WelcomeMessagesWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<History*> history);
	~WelcomeMessagesWidget();

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
		not_null<WelcomeMessagesMemento*> memento);

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
		const std::vector<not_null<Element*>> &elements,
		bool markLastAsRead) override;
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
	bool listElementHideTopicButton(not_null<const Element*> view) override;
	History *listTranslateHistory() override;
	void listAddTranslatedItems(
		not_null<TranslateTracker*> tracker) override;
	Ui::ElasticScroll *listScrollArea() const override;
	bool listThanosEffectEnabled() const override;

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
	void saveState(not_null<WelcomeMessagesMemento*> memento);
	void restoreState(not_null<WelcomeMessagesMemento*> memento);
	void showAtPosition(
		Data::MessagePosition position,
		FullMsgId originId = {});

	void setupComposeControls();
	void refreshEmptyText();

	void confirmDeleteSelected();
	void clearSelected();

	void uploadFile(const QByteArray &fileContent, SendMediaType type);
	bool confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		std::optional<bool> overrideSendImagesAsPhotos = std::nullopt,
		const QString &insertTextOnCancel = QString());
	bool confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel = QString());
	[[nodiscard]] bool showSendingFilesError(
		const Ui::PreparedList &list) const;
	[[nodiscard]] bool showSendingFilesError(
		const Ui::PreparedBundle &bundle) const;
	void sendingFilesConfirmed(
		std::shared_ptr<Ui::PreparedBundle> bundle,
		Api::SendOptions options);
	void chooseAttach(
		std::optional<bool> overrideSendImagesAsPhotos);
	[[nodiscard]] bool checkLimit() const;
	[[nodiscard]] Api::SendAction prepareSendAction(
		Api::SendOptions options) const;
	bool sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options,
		TextWithTags caption);
	bool sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options);
	void sendInlineResult(
		std::shared_ptr<InlineBots::Result> result,
		Api::SendOptions options);
	void finishSending();
	void send();
	void edit(not_null<HistoryItem*> item);
	[[nodiscard]] not_null<HistoryItem*> noticeItem();
	void highlightSingleNewMessage(const Data::MessagesSlice &slice);
	[[nodiscard]] SendMenu::Details sendMenuDetails() const override;
	bool processChosenSticker(ChatHelpers::FileChosen &&chosen) override;

	void checkReplyReturns();

	const not_null<History*> _history;
	std::shared_ptr<Ui::ChatTheme> _theme;
	object_ptr<Ui::ElasticScroll> _scroll;
	QPointer<ListWidget> _inner;
	object_ptr<TopBarWidget> _topBar;
	object_ptr<Ui::PlainShadow> _topBarShadow;
	std::unique_ptr<ComposeControls> _composeControls;
	bool _skipScrollEvent = false;
	bool _choosingAttach = false;

	std::unique_ptr<HistoryView::StickerToast> _stickerToast;

	CornerButtons _cornerButtons;

	Data::MessagesSlice _lastSlice;
	rpl::variable<int> _count;
	HistoryItem *_notice = nullptr;

	Ui::Text::String _emptyTitle;
	Ui::Text::String _emptyAbout;
	QSize _emptyTitleSize;
	QSize _emptyAboutSize;

};

class WelcomeMessagesMemento final : public Window::SectionMemento {
public:
	explicit WelcomeMessagesMemento(not_null<History*> history);

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	[[nodiscard]] not_null<History*> getHistory() const {
		return _history;
	}

	[[nodiscard]] not_null<ListMemento*> list() {
		return &_list;
	}

private:
	const not_null<History*> _history;
	ListMemento _list;

};

} // namespace HistoryView
