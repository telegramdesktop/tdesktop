/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_scheduled_section.h"

#include "history/view/history_view_compose_controls.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_schedule_box.h"
#include "history/history.h"
#include "history/history_item.h"
#include "chat_helpers/message_field.h" // SendMenuType.
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/toast/toast.h"
#include "ui/special_buttons.h"
#include "api/api_common.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "boxes/confirm_box.h"
#include "boxes/send_files_box.h"
#include "boxes/generic_box.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "core/event_filter.h"
#include "core/file_utilities.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_scheduled_messages.h"
#include "storage/storage_media_prepare.h"
#include "storage/localstorage.h"
#include "inline_bots/inline_bot_result.h"
#include "lang/lang_keys.h"
#include "styles/style_history.h"
#include "styles/style_window.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"

namespace HistoryView {
namespace {

void ShowErrorToast(const QString &text) {
	auto config = Ui::Toast::Config();
	config.multiline = true;
	config.minWidth = st::msgMinWidth;
	config.text = text;
	Ui::Toast::Show(config);
}

} // namespace

object_ptr<Window::SectionWidget> ScheduledMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<ScheduledWidget>(parent, controller, _history);
	result->setInternalState(geometry, this);
	return std::move(result);
}

ScheduledWidget::ScheduledWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: Window::SectionWidget(parent, controller)
, _history(history)
, _scroll(this, st::historyScroll, false)
, _topBar(this, controller)
, _topBarShadow(this)
, _composeControls(std::make_unique<ComposeControls>(
	this,
	controller,
	ComposeControls::Mode::Scheduled))
, _scrollDown(_scroll, st::historyToDown) {
	_topBar->setActiveChat(_history, TopBarWidget::Section::Scheduled);

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();

	_topBar->sendNowSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmSendNowSelected();
	}, _topBar->lifetime());
	_topBar->deleteSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::start_with_next([=] {
		clearSelected();
	}, _topBar->lifetime());

	_topBarShadow->raise();
	updateAdaptiveLayout();
	subscribe(Adaptive::Changed(), [=] { updateAdaptiveLayout(); });

	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		controller,
		static_cast<ListDelegate*>(this)));
	_scroll->move(0, _topBar->height());
	_scroll->show();
	connect(_scroll, &Ui::ScrollArea::scrolled, [=] { onScroll(); });

	setupScrollDownButton();
	setupComposeControls();
}

ScheduledWidget::~ScheduledWidget() = default;

void ScheduledWidget::setupComposeControls() {
	_composeControls->setHistory(_history);

	_composeControls->height(
	) | rpl::start_with_next([=] {
		updateControlsGeometry();
	}, lifetime());

	_composeControls->cancelRequests(
	) | rpl::start_with_next([=] {
		controller()->showBackFromStack();
	}, lifetime());

	_composeControls->sendRequests(
	) | rpl::start_with_next([=] {
		send();
	}, lifetime());

	_composeControls->attachRequests(
	) | rpl::filter([=] {
		return !_choosingAttach;
	}) | rpl::start_with_next([=] {
		_choosingAttach = true;
		App::CallDelayed(
			st::historyAttach.ripple.hideDuration,
			this,
			[=] { _choosingAttach = false; chooseAttach(); });
	}, lifetime());

	_composeControls->fileChosen(
	) | rpl::start_with_next([=](not_null<DocumentData*> document) {
		sendExistingDocument(document);
	}, lifetime());

	_composeControls->photoChosen(
	) | rpl::start_with_next([=](not_null<PhotoData*> photo) {
		sendExistingPhoto(photo);
	}, lifetime());

	_composeControls->inlineResultChosen(
	) | rpl::start_with_next([=](
			ChatHelpers::TabbedSelector::InlineChosen chosen) {
		sendInlineResult(chosen.result, chosen.bot);
	}, lifetime());
}

void ScheduledWidget::chooseAttach() {
	if (const auto error = Data::RestrictionError(
		_history->peer,
		ChatRestriction::f_send_media)) {
		Ui::Toast::Show(*error);
		return;
	}

	const auto filter = FileDialog::AllFilesFilter()
		+ qsl(";;Image files (*")
		+ cImgExtensions().join(qsl(" *"))
		+ qsl(")");

	FileDialog::GetOpenPaths(this, tr::lng_choose_files(tr::now), filter, crl::guard(this, [=](
			FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!result.remoteContent.isEmpty()) {
			auto animated = false;
			auto image = App::readImage(
				result.remoteContent,
				nullptr,
				false,
				&animated);
			if (!image.isNull() && !animated) {
				confirmSendingFiles(
					std::move(image),
					std::move(result.remoteContent),
					CompressConfirm::Auto);
			} else {
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			auto list = Storage::PrepareMediaList(
				result.paths,
				st::sendMediaPreviewSize);
			if (list.allFilesForCompress || list.albumIsPossible) {
				confirmSendingFiles(std::move(list), CompressConfirm::Auto);
			} else if (!showSendingFilesError(list)) {
				confirmSendingFiles(std::move(list), CompressConfirm::No);
			}
		}
	}), nullptr);
}

bool ScheduledWidget::confirmSendingFiles(
		Storage::PreparedList &&list,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	if (showSendingFilesError(list)) {
		return false;
	}

	const auto noCompressOption = (list.files.size() > 1)
		&& !list.allFilesForCompress
		&& !list.albumIsPossible;
	const auto boxCompressConfirm = noCompressOption
		? CompressConfirm::None
		: compressed;

	//const auto cursor = _field->textCursor();
	//const auto position = cursor.position();
	//const auto anchor = cursor.anchor();
	const auto text = _composeControls->getTextWithAppliedMarkdown();//_field->getTextWithTags();
	using SendLimit = SendFilesBox::SendLimit;
	auto box = Box<SendFilesBox>(
		controller(),
		std::move(list),
		text,
		boxCompressConfirm,
		_history->peer->slowmodeApplied() ? SendLimit::One : SendLimit::Many,
		Api::SendType::Scheduled,
		SendMenuType::Disabled);
	//_field->setTextWithTags({});

	box->setConfirmedCallback(crl::guard(this, [=](
			Storage::PreparedList &&list,
			SendFilesWay way,
			TextWithTags &&caption,
			Api::SendOptions options,
			bool ctrlShiftEnter) {
		if (showSendingFilesError(list)) {
			return;
		}
		const auto type = (way == SendFilesWay::Files)
			? SendMediaType::File
			: SendMediaType::Photo;
		const auto album = (way == SendFilesWay::Album)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		uploadFilesAfterConfirmation(
			std::move(list),
			type,
			std::move(caption),
			MsgId(0),//replyToId(),
			options,
			album);
	}));
	//box->setCancelledCallback(crl::guard(this, [=] {
	//	_field->setTextWithTags(text);
	//	auto cursor = _field->textCursor();
	//	cursor.setPosition(anchor);
	//	if (position != anchor) {
	//		cursor.setPosition(position, QTextCursor::KeepAnchor);
	//	}
	//	_field->setTextCursor(cursor);
	//	if (!insertTextOnCancel.isEmpty()) {
	//		_field->textCursor().insertText(insertTextOnCancel);
	//	}
	//}));

	//ActivateWindow(controller());
	const auto shown = Ui::show(std::move(box));
	shown->setCloseByOutsideClick(false);

	return true;
}

bool ScheduledWidget::confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	if (image.isNull()) {
		return false;
	}

	auto list = Storage::PrepareMediaFromImage(
		std::move(image),
		std::move(content),
		st::sendMediaPreviewSize);
	return confirmSendingFiles(
		std::move(list),
		compressed,
		insertTextOnCancel);
}

void ScheduledWidget::uploadFilesAfterConfirmation(
		Storage::PreparedList &&list,
		SendMediaType type,
		TextWithTags &&caption,
		MsgId replyTo,
		Api::SendOptions options,
		std::shared_ptr<SendingAlbum> album) {
	const auto isAlbum = (album != nullptr);
	const auto compressImages = (type == SendMediaType::Photo);
	if (_history->peer->slowmodeApplied()
		&& ((list.files.size() > 1 && !album)
			|| (!list.files.empty()
				&& !caption.text.isEmpty()
				&& !list.canAddCaption(isAlbum, compressImages)))) {
		ShowErrorToast(tr::lng_slowmode_no_many(tr::now));
		return;
	}
	auto action = Api::SendAction(_history);
	action.replyTo = replyTo;
	action.options = options;
	session().api().sendFiles(
		std::move(list),
		type,
		std::move(caption),
		album,
		action);
}

void ScheduledWidget::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	const auto callback = [=](Api::SendOptions options) {
		auto action = Api::SendAction(_history);
		//action.replyTo = replyToId();
		action.options = options;
		session().api().sendFile(fileContent, type, action);
	};
	Ui::show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		LayerOption::KeepOther);
}

bool ScheduledWidget::showSendingFilesError(
		const Storage::PreparedList &list) const {
	const auto text = [&] {
		const auto error = Data::RestrictionError(
			_history->peer,
			ChatRestriction::f_send_media);
		if (error) {
			return *error;
		}
		using Error = Storage::PreparedList::Error;
		switch (list.error) {
		case Error::None: return QString();
		case Error::EmptyFile:
		case Error::Directory:
		case Error::NonLocalUrl: return tr::lng_send_image_empty(
			tr::now,
			lt_name,
			list.errorData);
		case Error::TooLargeFile: return tr::lng_send_image_too_large(
			tr::now,
			lt_name,
			list.errorData);
		}
		return tr::lng_forward_send_files_cant(tr::now);
	}();
	if (text.isEmpty()) {
		return false;
	}

	ShowErrorToast(text);
	return true;
}

void ScheduledWidget::send() {
	if (_composeControls->getTextWithAppliedMarkdown().text.isEmpty()) {
		return;
	}
	const auto callback = [=](Api::SendOptions options) { send(options); };
	Ui::show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		LayerOption::KeepOther);
}

void ScheduledWidget::send(Api::SendOptions options) {
	const auto webPageId = 0;/* _previewCancelled
		? CancelledWebPageId
		: ((_previewData && _previewData->pendingTill >= 0)
			? _previewData->id
			: WebPageId(0));*/

	auto message = ApiWrap::MessageToSend(_history);
	message.textWithTags = _composeControls->getTextWithAppliedMarkdown();
	message.action.options = options;
	//message.action.replyTo = replyToId();
	message.webPageId = webPageId;

	//const auto error = GetErrorTextForSending(
	//	_peer,
	//	_toForward,
	//	message.textWithTags);
	//if (!error.isEmpty()) {
	//	ShowErrorToast(error);
	//	return;
	//}

	session().api().sendMessage(std::move(message));

	_composeControls->clear();
	//_saveDraftText = true;
	//_saveDraftStart = crl::now();
	//onDraftSave();

	_composeControls->hidePanelsAnimated();

	//if (_previewData && _previewData->pendingTill) previewCancel();
	_composeControls->focus();
}

void ScheduledWidget::sendExistingDocument(
		not_null<DocumentData*> document) {
	const auto callback = [=](Api::SendOptions options) {
		sendExistingDocument(document, options);
	};
	Ui::show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		LayerOption::KeepOther);
}

bool ScheduledWidget::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options) {
	const auto error = Data::RestrictionError(
		_history->peer,
		ChatRestriction::f_send_stickers);
	if (error) {
		Ui::show(Box<InformBox>(*error), LayerOption::KeepOther);
		return false;
	}

	auto message = Api::MessageToSend(_history);
	//message.action.replyTo = replyToId();
	message.action.options = options;
	Api::SendExistingDocument(std::move(message), document);

	//if (_fieldAutocomplete->stickersShown()) {
	//	clearFieldText();
	//	//_saveDraftText = true;
	//	//_saveDraftStart = crl::now();
	//	//onDraftSave();
	//	onCloudDraftSave(); // won't be needed if SendInlineBotResult will clear the cloud draft
	//}

	_composeControls->hidePanelsAnimated();
	_composeControls->focus();
	return true;
}

void ScheduledWidget::sendExistingPhoto(not_null<PhotoData*> photo) {
	const auto callback = [=](Api::SendOptions options) {
		sendExistingPhoto(photo, options);
	};
	Ui::show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		LayerOption::KeepOther);
}

bool ScheduledWidget::sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options) {
	const auto error = Data::RestrictionError(
		_history->peer,
		ChatRestriction::f_send_media);
	if (error) {
		Ui::show(Box<InformBox>(*error), LayerOption::KeepOther);
		return false;
	}

	auto message = Api::MessageToSend(_history);
	//message.action.replyTo = replyToId();
	message.action.options = options;
	Api::SendExistingPhoto(std::move(message), photo);

	_composeControls->hidePanelsAnimated();
	_composeControls->focus();
	return true;
}

void ScheduledWidget::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot) {
	const auto errorText = result->getErrorOnSend(_history);
	if (!errorText.isEmpty()) {
		Ui::show(Box<InformBox>(errorText));
		return;
	}
	const auto callback = [=](Api::SendOptions options) {
		sendInlineResult(result, bot, options);
	};
	Ui::show(
		PrepareScheduleBox(this, sendMenuType(), callback),
		LayerOption::KeepOther);
}

void ScheduledWidget::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot,
		Api::SendOptions options) {
	auto action = Api::SendAction(_history);
	//action.replyTo = replyToId();
	action.options = options;
	action.generateLocal = true;
	session().api().sendInlineResult(bot, result, action);

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
		Local::writeRecentHashtagsAndBots();
	}

	_composeControls->hidePanelsAnimated();
	_composeControls->focus();
}

SendMenuType ScheduledWidget::sendMenuType() const {
	return _history->peer->isSelf()
		? SendMenuType::Reminder
		: SendMenuType::Scheduled;
}

void ScheduledWidget::setupScrollDownButton() {
	_scrollDown->setClickedCallback([=] {
		scrollDownClicked();
	});
	Core::InstallEventFilter(_scrollDown, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Wheel) {
			return _scroll->viewportEvent(event);
		}
		return false;
	});
	updateScrollDownVisibility();
}

void ScheduledWidget::scrollDownClicked() {
	showAtPosition(Data::MaxMessagePosition);
}

void ScheduledWidget::showAtPosition(Data::MessagePosition position) {
	if (showAtPositionNow(position)) {
		if (const auto highlight = base::take(_highlightMessageId)) {
			_inner->highlightMessage(highlight);
		}
	} else {
		_nextAnimatedScrollPosition = position;
		_nextAnimatedScrollDelta = _inner->isBelowPosition(position)
			? -_scroll->height()
			: _inner->isAbovePosition(position)
			? _scroll->height()
			: 0;
		auto memento = HistoryView::ListMemento(position);
		_inner->restoreState(&memento);
	}
}

bool ScheduledWidget::showAtPositionNow(Data::MessagePosition position) {
	if (const auto scrollTop = _inner->scrollTopForPosition(position)) {
		const auto currentScrollTop = _scroll->scrollTop();
		const auto wanted = snap(*scrollTop, 0, _scroll->scrollTopMax());
		const auto fullDelta = (wanted - currentScrollTop);
		const auto limit = _scroll->height();
		const auto scrollDelta = snap(fullDelta, -limit, limit);
		_inner->animatedScrollTo(
			wanted,
			position,
			scrollDelta,
			(std::abs(fullDelta) > limit
				? HistoryView::ListWidget::AnimatedScroll::Part
				: HistoryView::ListWidget::AnimatedScroll::Full));
		return true;
	}
	return false;
}

void ScheduledWidget::updateScrollDownVisibility() {
	if (animating()) {
		return;
	}

	const auto scrollDownIsVisible = [&]() -> std::optional<bool> {
		const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
		if (top < _scroll->scrollTopMax()) {
			return true;
		}
		if (_inner->loadedAtBottomKnown()) {
			return !_inner->loadedAtBottom();
		}
		return std::nullopt;
	};
	const auto scrollDownIsShown = scrollDownIsVisible();
	if (!scrollDownIsShown) {
		return;
	}
	if (_scrollDownIsShown != *scrollDownIsShown) {
		_scrollDownIsShown = *scrollDownIsShown;
		_scrollDownShown.start(
			[=] { updateScrollDownPosition(); },
			_scrollDownIsShown ? 0. : 1.,
			_scrollDownIsShown ? 1. : 0.,
			st::historyToDownDuration);
	}
}

void ScheduledWidget::updateScrollDownPosition() {
	// _scrollDown is a child widget of _scroll, not me.
	auto top = anim::interpolate(
		0,
		_scrollDown->height() + st::historyToDownPosition.y(),
		_scrollDownShown.value(_scrollDownIsShown ? 1. : 0.));
	_scrollDown->moveToRight(
		st::historyToDownPosition.x(),
		_scroll->height() - top);
	auto shouldBeHidden = !_scrollDownIsShown && !_scrollDownShown.animating();
	if (shouldBeHidden != _scrollDown->isHidden()) {
		_scrollDown->setVisible(!shouldBeHidden);
	}
}

void ScheduledWidget::scrollDownAnimationFinish() {
	_scrollDownShown.stop();
	updateScrollDownPosition();
}

void ScheduledWidget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		Adaptive::OneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

not_null<History*> ScheduledWidget::history() const {
	return _history;
}

Dialogs::RowDescriptor ScheduledWidget::activeChat() const {
	return {
		_history,
		FullMsgId(_history->channelId(), ShowAtUnreadMsgId)
	};
}

QPixmap ScheduledWidget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	_topBar->updateControlsVisibility();
	if (params.withTopBarShadow) _topBarShadow->hide();
	_composeControls->showForGrab();
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) _topBarShadow->show();
	return result;
}

void ScheduledWidget::doSetInnerFocus() {
	_composeControls->focus();
}

bool ScheduledWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto logMemento = dynamic_cast<ScheduledMemento*>(memento.get())) {
		if (logMemento->getHistory() == history()) {
			restoreState(logMemento);
			return true;
		}
	}
	return false;
}

void ScheduledWidget::setInternalState(
		const QRect &geometry,
		not_null<ScheduledMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

void ScheduledWidget::pushTabbedSelectorToThirdSection(
		const Window::SectionShow &params) {
	_composeControls->pushTabbedSelectorToThirdSection(params);
}

bool ScheduledWidget::returnTabbedSelector() {
	return _composeControls->returnTabbedSelector();
}

std::unique_ptr<Window::SectionMemento> ScheduledWidget::createMemento() {
	auto result = std::make_unique<ScheduledMemento>(history());
	saveState(result.get());
	return std::move(result);
}

void ScheduledWidget::saveState(not_null<ScheduledMemento*> memento) {
	_inner->saveState(memento->list());
}

void ScheduledWidget::restoreState(not_null<ScheduledMemento*> memento) {
	_inner->restoreState(memento->list());
}

void ScheduledWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	_composeControls->resizeToWidth(width());
	updateControlsGeometry();
}

void ScheduledWidget::updateControlsGeometry() {
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

	updateScrollDownPosition();
}

void ScheduledWidget::paintEvent(QPaintEvent *e) {
	if (animating()) {
		SectionWidget::paintEvent(e);
		return;
	}
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}
	//if (hasPendingResizedItems()) {
	//	updateListSize();
	//}

	//auto ms = crl::now();
	//_historyDownShown.step(ms);

	SectionWidget::PaintBackground(this, e->rect());
}

void ScheduledWidget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void ScheduledWidget::updateInnerVisibleArea() {
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	updateScrollDownVisibility();
}

void ScheduledWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
	_composeControls->showStarted();
}

void ScheduledWidget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
	_composeControls->showFinished();
}

bool ScheduledWidget::wheelEventFromFloatPlayer(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect ScheduledWidget::rectForFloatPlayer() const {
	return mapToGlobal(_scroll->geometry());
}

Context ScheduledWidget::listContext() {
	return Context::History;
}

void ScheduledWidget::listScrollTo(int top) {
	if (_scroll->scrollTop() != top) {
		_scroll->scrollToY(top);
	} else {
		updateInnerVisibleArea();
	}
}

void ScheduledWidget::listCancelRequest() {
	controller()->showBackFromStack();
}

void ScheduledWidget::listDeleteRequest() {
	confirmDeleteSelected();
}

rpl::producer<Data::MessagesSlice> ScheduledWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto data = &controller()->session().data();
	return rpl::single(
		rpl::empty_value()
	) | rpl::then(
		data->scheduledMessages().updates(_history)
	) | rpl::map([=] {
		return data->scheduledMessages().list(_history);
	}) | rpl::after_next([=](const Data::MessagesSlice &slice) {
		highlightSingleNewMessage(slice);
	});
}

void ScheduledWidget::highlightSingleNewMessage(
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
	//	_highlightMessageId = newId;
		showAtPosition(item->position());
	}
}

bool ScheduledWidget::listAllowsMultiSelect() {
	return true;
}

bool ScheduledWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return !item->isSending() && !item->hasFailed();
}

bool ScheduledWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void ScheduledWidget::listSelectionChanged(SelectedItems &&items) {
	HistoryView::TopBarWidget::SelectedState state;
	state.count = items.size();
	for (const auto item : items) {
		if (item.canDelete) {
			++state.canDeleteCount;
		}
		if (item.canSendNow) {
			++state.canSendNowCount;
		}
	}
	_topBar->showSelected(state);
}

void ScheduledWidget::listVisibleItemsChanged(HistoryItemsList &&items) {
}

std::optional<int> ScheduledWidget::listUnreadBarView(
		const std::vector<not_null<Element*>> &elements) {
	return std::nullopt;
}

void ScheduledWidget::listContentRefreshed() {
}

ClickHandlerPtr ScheduledWidget::listDateLink(not_null<Element*> view) {
	return nullptr;
}

void ScheduledWidget::confirmSendNowSelected() {
	auto items = _inner->getSelectedItems();
	if (items.empty()) {
		return;
	}
	const auto navigation = controller();
	Window::ShowSendNowMessagesBox(
		navigation,
		_history,
		std::move(items),
		[=] { navigation->showBackFromStack(); });
}

void ScheduledWidget::confirmDeleteSelected() {
	auto items = _inner->getSelectedItems();
	if (items.empty()) {
		return;
	}
	const auto weak = make_weak(this);
	const auto box = Ui::show(Box<DeleteMessagesBox>(
		&_history->session(),
		std::move(items)));
	box->setDeleteConfirmedCallback([=] {
		if (const auto strong = weak.data()) {
			strong->clearSelected();
		}
	});
}

void ScheduledWidget::clearSelected() {
	_inner->cancelSelection();
}

} // namespace HistoryView
