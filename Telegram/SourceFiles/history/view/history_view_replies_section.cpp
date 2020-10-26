/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_replies_section.h"

#include "history/view/history_view_compose_controls.h"
#include "history/view/history_view_top_bar_widget.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/history_view_schedule_box.h"
#include "history/view/history_view_pinned_bar.h"
#include "history/history.h"
#include "history/history_drag_area.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "chat_helpers/send_context_menu.h" // SendMenu::Type.
#include "ui/chat/pinned_bar.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/layers/generic_box.h"
#include "ui/item_text_options.h"
#include "ui/toast/toast.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/special_buttons.h"
#include "ui/ui_utility.h"
#include "ui/toasts/common_toasts.h"
#include "base/timer_rpl.h"
#include "api/api_common.h"
#include "api/api_editing.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "boxes/confirm_box.h"
#include "boxes/edit_caption_box.h"
#include "boxes/send_files_box.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "base/event_filter.h"
#include "base/call_delayed.h"
#include "core/file_utilities.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_replies_list.h"
#include "data/data_changes.h"
#include "storage/storage_media_prepare.h"
#include "storage/storage_account.h"
#include "inline_bots/inline_bot_result.h"
#include "platform/platform_specific.h"
#include "lang/lang_keys.h"
#include "facades.h"
#include "app.h"
#include "styles/style_chat.h"
#include "styles/style_window.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"

#include <QtCore/QMimeData>
#include <QtGui/QGuiApplication>

namespace HistoryView {
namespace {

constexpr auto kReadRequestTimeout = 3 * crl::time(1000);
constexpr auto kRefreshSlowmodeLabelTimeout = crl::time(200);

bool CanSendFiles(not_null<const QMimeData*> data) {
	if (data->hasImage()) {
		return true;
	} else if (const auto urls = data->urls(); !urls.empty()) {
		if (ranges::all_of(urls, &QUrl::isLocalFile)) {
			return true;
		}
	}
	return false;
}

rpl::producer<Ui::MessageBarContent> RootViewContent(
		not_null<History*> history,
		MsgId rootId) {
	return MessageBarContentByItemId(
		&history->session(),
		FullMsgId{ history->channelId(), rootId }
	) | rpl::map([=](Ui::MessageBarContent &&content) {
		const auto item = history->owner().message(
			history->channelId(),
			rootId);
		if (!item) {
			content.text = Ui::Text::Link(tr::lng_deleted_message(tr::now));
		}
		const auto sender = (item && item->discussionPostOriginalSender())
			? item->discussionPostOriginalSender()
			: history->peer.get();
		content.title = sender->name.isEmpty() ? "Message" : sender->name;
		return std::move(content);
	});
}

} // namespace

RepliesMemento::RepliesMemento(
	not_null<HistoryItem*> commentsItem,
	MsgId commentId)
: RepliesMemento(commentsItem->history(), commentsItem->id, commentId) {
	if (commentId) {
		_list.setAroundPosition({
			.fullId = FullMsgId(
				commentsItem->history()->channelId(),
				commentId),
			.date = TimeId(0),
		});
	} else if (commentsItem->computeRepliesInboxReadTillFull() == MsgId(1)) {
		_list.setAroundPosition(Data::MinMessagePosition);
		_list.setScrollTopState(ListMemento::ScrollTopState{
			Data::MinMessagePosition
		});
	}
}

object_ptr<Window::SectionWidget> RepliesMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<RepliesWidget>(
		parent,
		controller,
		_history,
		_rootId);
	result->setInternalState(geometry, this);
	return result;
}

RepliesWidget::RepliesWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history,
	MsgId rootId)
: Window::SectionWidget(parent, controller)
, _history(history)
, _rootId(rootId)
, _root(lookupRoot())
, _areComments(computeAreComments())
, _sendAction(history->owner().repliesSendActionPainter(history, rootId))
, _topBar(this, controller)
, _topBarShadow(this)
, _composeControls(std::make_unique<ComposeControls>(
	this,
	controller,
	ComposeControls::Mode::Normal))
, _scroll(std::make_unique<Ui::ScrollArea>(this, st::historyScroll, false))
, _scrollDown(_scroll.get(), st::historyToDown)
, _readRequestTimer([=] { sendReadTillRequest(); }) {
	setupRoot();
	setupRootView();

	session().api().requestFullPeer(_history->peer);

	_topBar->setActiveChat(
		_history,
		TopBarWidget::Section::Replies,
		_sendAction.get());

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();

	_rootView->move(0, _topBar->height());

	_topBar->deleteSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->forwardSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmForwardSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::start_with_next([=] {
		clearSelected();
	}, _topBar->lifetime());

	_rootView->raise();
	_topBarShadow->raise();
	updateAdaptiveLayout();
	subscribe(Adaptive::Changed(), [=] { updateAdaptiveLayout(); });

	_inner = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		controller,
		static_cast<ListDelegate*>(this)));
	_scroll->move(0, _topBar->height());
	_scroll->show();
	connect(_scroll.get(), &Ui::ScrollArea::scrolled, [=] { onScroll(); });

	_inner->editMessageRequested(
	) | rpl::start_with_next([=](auto fullId) {
		if (const auto item = session().data().message(fullId)) {
			const auto media = item->media();
			if (media && !media->webpage()) {
				if (media->allowsEditCaption()) {
					Ui::show(Box<EditCaptionBox>(controller, item));
				}
			} else {
				_composeControls->editMessage(fullId);
			}
		}
	}, _inner->lifetime());

	_inner->replyToMessageRequested(
	) | rpl::start_with_next([=](auto fullId) {
		_composeControls->replyToMessage(fullId);
	}, _inner->lifetime());

	_composeControls->sendActionUpdates(
	) | rpl::start_with_next([=](ComposeControls::SendActionUpdate &&data) {
		session().sendProgressManager().update(
			_history,
			_rootId,
			data.type,
			data.progress);
	}, lifetime());

	_history->session().changes().messageUpdates(
		Data::MessageUpdate::Flag::Destroyed
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		if (update.item == _root) {
			_root = nullptr;
			updatePinnedVisibility();
			controller->showBackFromStack();
		}
		while (update.item == _replyReturn) {
			calculateNextReplyReturn();
		}
	}, lifetime());

	_history->session().changes().historyUpdates(
		_history,
		Data::HistoryUpdate::Flag::OutboxRead
	) | rpl::start_with_next([=] {
		_inner->update();
	}, lifetime());

	setupScrollDownButton();
	setupComposeControls();
}

RepliesWidget::~RepliesWidget() {
	if (_readRequestTimer.isActive()) {
		sendReadTillRequest();
	}
	base::take(_sendAction);
	_history->owner().repliesSendActionPainterRemoved(_history, _rootId);
}

void RepliesWidget::sendReadTillRequest() {
	if (!_root) {
		_readRequestPending = true;
		return;
	}
	if (_readRequestTimer.isActive()) {
		_readRequestTimer.cancel();
	}
	_readRequestPending = false;
	const auto api = &_history->session().api();
	api->request(base::take(_readRequestId)).cancel();
	_readRequestId = api->request(MTPmessages_ReadDiscussion(
		_root->history()->peer->input,
		MTP_int(_root->id),
		MTP_int(_root->computeRepliesInboxReadTillFull())
	)).done([=](const MTPBool &) {
	}).send();
}

void RepliesWidget::setupRoot() {
	if (!_root) {
		const auto channel = _history->peer->asChannel();
		const auto done = crl::guard(this, [=](ChannelData*, MsgId) {
			_root = lookupRoot();
			if (_root) {
				_areComments = computeAreComments();
				if (_readRequestPending) {
					sendReadTillRequest();
				}
				_inner->update();
			}
			updatePinnedVisibility();
		});
		_history->session().api().requestMessageData(channel, _rootId, done);
	}
}

void RepliesWidget::setupRootView() {
	auto content = rpl::combine(
		RootViewContent(_history, _rootId),
		_rootVisible.value()
	) | rpl::map([=](Ui::MessageBarContent &&content, bool shown) {
		return shown ? std::move(content) : Ui::MessageBarContent();
	});
	_rootView = std::make_unique<Ui::PinnedBar>(this, std::move(content));

	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		base::ObservableViewer(Adaptive::Changed())
	) | rpl::map([] {
		return Adaptive::OneColumn();
	}) | rpl::start_with_next([=](bool one) {
		_rootView->setShadowGeometryPostprocess([=](QRect geometry) {
			if (!one) {
				geometry.setLeft(geometry.left() + st::lineWidth);
			}
			return geometry;
		});
	}, _rootView->lifetime());

	_rootView->barClicks(
	) | rpl::start_with_next([=] {
		showAtStart();
	}, lifetime());

	_rootViewHeight = 0;
	_rootView->heightValue(
	) | rpl::start_with_next([=](int height) {
		if (const auto delta = height - _rootViewHeight) {
			_rootViewHeight = height;
			setGeometryWithTopMoved(geometry(), delta);
		}
	}, _rootView->lifetime());
}

HistoryItem *RepliesWidget::lookupRoot() const {
	return _history->owner().message(_history->channelId(), _rootId);
}

bool RepliesWidget::computeAreComments() const {
	return _root && _root->isDiscussionPost();
}

void RepliesWidget::setupComposeControls() {
	auto slowmodeSecondsLeft = session().changes().peerFlagsValue(
		_history->peer,
		Data::PeerUpdate::Flag::Slowmode
	) | rpl::map([=] {
		return _history->peer->slowmodeSecondsLeft();
	}) | rpl::map([=](int delay) -> rpl::producer<int> {
		auto start = rpl::single(delay);
		if (!delay) {
			return start;
		}
		return std::move(
			start
		) | rpl::then(base::timer_each(
			kRefreshSlowmodeLabelTimeout
		) | rpl::map([=] {
			return _history->peer->slowmodeSecondsLeft();
		}) | rpl::take_while([=](int delay) {
			return delay > 0;
		})) | rpl::then(rpl::single(0));
	}) | rpl::flatten_latest();

	const auto channel = _history->peer->asChannel();
	Assert(channel != nullptr);

	auto hasSendingMessage = session().changes().historyFlagsValue(
		_history,
		Data::HistoryUpdate::Flag::LocalMessages
	) | rpl::map([=] {
		return _history->latestSendingMessage() != nullptr;
	}) | rpl::distinct_until_changed();

	using namespace rpl::mappers;
	auto sendDisabledBySlowmode = (!channel || channel->amCreator())
		? (rpl::single(false) | rpl::type_erased())
		: rpl::combine(
			channel->slowmodeAppliedValue(),
			std::move(hasSendingMessage),
			_1 && _2);

	auto writeRestriction = session().changes().peerFlagsValue(
		_history->peer,
		Data::PeerUpdate::Flag::Rights
	) | rpl::map([=] {
		return Data::RestrictionError(
			_history->peer,
			ChatRestriction::f_send_messages);
	});

	_composeControls->setHistory({
		.history = _history.get(),
		.showSlowmodeError = [=] { return showSlowmodeError(); },
		.slowmodeSecondsLeft = std::move(slowmodeSecondsLeft),
		.sendDisabledBySlowmode = std::move(sendDisabledBySlowmode),
		.writeRestriction = std::move(writeRestriction),
	});

	_composeControls->height(
	) | rpl::start_with_next([=] {
		const auto wasMax = (_scroll->scrollTopMax() == _scroll->scrollTop());
		updateControlsGeometry();
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
		sendVoice(data.bytes, data.waveform, data.duration);
	}, lifetime());

	const auto saveEditMsgRequestId = lifetime().make_state<mtpRequestId>(0);
	_composeControls->editRequests(
	) | rpl::start_with_next([=](auto data) {
		if (const auto item = session().data().message(data.fullId)) {
			edit(item, data.options, saveEditMsgRequestId);
		}
	}, lifetime());

	_composeControls->attachRequests(
	) | rpl::filter([=] {
		return !_choosingAttach;
	}) | rpl::start_with_next([=] {
		_choosingAttach = true;
		base::call_delayed(
			st::historyAttach.ripple.hideDuration,
			this,
			[=] { _choosingAttach = false; chooseAttach(); });
	}, lifetime());

	using Selector = ChatHelpers::TabbedSelector;

	_composeControls->fileChosen(
	) | rpl::start_with_next([=](Selector::FileChosen chosen) {
		sendExistingDocument(chosen.document);
	}, lifetime());

	_composeControls->photoChosen(
	) | rpl::start_with_next([=](Selector::PhotoChosen chosen) {
		sendExistingPhoto(chosen.photo);
	}, lifetime());

	_composeControls->inlineResultChosen(
	) | rpl::start_with_next([=](Selector::InlineChosen chosen) {
		sendInlineResult(chosen.result, chosen.bot);
	}, lifetime());

	_composeControls->scrollRequests(
	) | rpl::start_with_next([=](Data::MessagePosition pos) {
		showAtPosition(pos);
	}, lifetime());

	_composeControls->keyEvents(
	) | rpl::start_with_next([=](not_null<QKeyEvent*> e) {
		if (e->key() == Qt::Key_Up) {
			if (!_composeControls->isEditingMessage()) {
				// #TODO replies edit last sent message
				//auto &messages = session().data().scheduledMessages();
				//if (const auto item = messages.lastSentMessage(_history)) {
				//	_inner->editMessageRequestNotify(item->fullId());
				//} else {
					_scroll->keyPressEvent(e);
				//}
			} else {
				_scroll->keyPressEvent(e);
			}
			e->accept();
		} else if (e->key() == Qt::Key_Down) {
			_scroll->keyPressEvent(e);
			e->accept();
		} else if (e->key() == Qt::Key_PageDown) {
			_scroll->keyPressEvent(e);
			e->accept();
		} else if (e->key() == Qt::Key_PageUp) {
			_scroll->keyPressEvent(e);
			e->accept();
		}
	}, lifetime());

	_composeControls->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return CanSendFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return confirmSendingFiles(data, std::nullopt, data->text());
		}
		Unexpected("action in MimeData hook.");
	});

	_composeControls->finishAnimating();
}

void RepliesWidget::chooseAttach() {
	if (const auto error = Data::RestrictionError(
			_history->peer,
			ChatRestriction::f_send_media)) {
		Ui::ShowMultilineToast({
			.text = { *error },
		});
		return;
	} else if (showSlowmodeError()) {
		return;
	}

	const auto filter = FileDialog::AllOrImagesFilter();
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
					std::move(result.remoteContent));
			} else {
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			auto list = Storage::PrepareMediaList(
				result.paths,
				st::sendMediaPreviewSize);
			confirmSendingFiles(std::move(list));
		}
	}), nullptr);
}

bool RepliesWidget::confirmSendingFiles(
		not_null<const QMimeData*> data,
		std::optional<bool> overrideSendImagesAsPhotos,
		const QString &insertTextOnCancel) {
	const auto hasImage = data->hasImage();

	if (const auto urls = data->urls(); !urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize);
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

	if (hasImage) {
		auto image = Platform::GetImageFromClipboard();
		if (image.isNull()) {
			image = qvariant_cast<QImage>(data->imageData());
		}
		if (!image.isNull()) {
			confirmSendingFiles(
				std::move(image),
				QByteArray(),
				overrideSendImagesAsPhotos,
				insertTextOnCancel);
			return true;
		}
	}
	return false;
}

bool RepliesWidget::confirmSendingFiles(
		Ui::PreparedList &&list,
		const QString &insertTextOnCancel) {
	if (showSendingFilesError(list)) {
		return false;
	}

	//const auto cursor = _field->textCursor();
	//const auto position = cursor.position();
	//const auto anchor = cursor.anchor();
	const auto text = _composeControls->getTextWithAppliedMarkdown();//_field->getTextWithTags();
	using SendLimit = SendFilesBox::SendLimit;
	auto box = Box<SendFilesBox>(
		controller(),
		std::move(list),
		text,
		_history->peer->slowmodeApplied() ? SendLimit::One : SendLimit::Many,
		Api::SendType::Normal,
		SendMenu::Type::Disabled); // #TODO replies schedule
	_composeControls->setText({});

	const auto replyTo = replyToId();
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
	box->setCancelledCallback(crl::guard(this, [=] {
		_composeControls->setText(text);
		//auto cursor = _field->textCursor();
		//cursor.setPosition(anchor);
		//if (position != anchor) {
		//	cursor.setPosition(position, QTextCursor::KeepAnchor);
		//}
		//_field->setTextCursor(cursor);
		//if (!insertTextOnCancel.isEmpty()) {
		//	_field->textCursor().insertText(insertTextOnCancel);
		//}
	}));

	//ActivateWindow(controller());
	const auto shown = Ui::show(std::move(box));
	shown->setCloseByOutsideClick(false);

	return true;
}

void RepliesWidget::sendingFilesConfirmed(
		Ui::PreparedList &&list,
		Ui::SendFilesWay way,
		TextWithTags &&caption,
		Api::SendOptions options,
		bool ctrlShiftEnter) {
	Expects(list.filesToProcess.empty());

	if (showSendingFilesError(list)) {
		return;
	}
	auto groups = DivideByGroups(
		std::move(list),
		way,
		_history->peer->slowmodeApplied());
	const auto replyTo = replyToId();
	const auto type = way.sendImagesAsPhotos()
		? SendMediaType::Photo
		: SendMediaType::File;
	auto action = Api::SendAction(_history);
	action.replyTo = replyTo ? replyTo : _rootId;
	action.options = options;
	action.clearDraft = false;
	if ((groups.size() != 1 || !groups.front().sentWithCaption())
		&& !caption.text.isEmpty()) {
		auto message = Api::MessageToSend(_history);
		message.textWithTags = base::take(caption);
		message.action = action;
		session().api().sendMessage(std::move(message));
	}
	for (auto &group : groups) {
		const auto album = (group.type != Ui::AlbumType::None)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		session().api().sendFiles(
			std::move(group.list),
			type,
			base::take(caption),
			album,
			action);
	}
	if (_composeControls->replyingToMessage().msg == replyTo) {
		_composeControls->cancelReplyMessage();
	}
}

bool RepliesWidget::confirmSendingFiles(
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

bool RepliesWidget::showSlowmodeError() {
	const auto text = [&] {
		if (const auto left = _history->peer->slowmodeSecondsLeft()) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				Ui::FormatDurationWords(left));
		} else if (_history->peer->slowmodeApplied()) {
			if (const auto item = _history->latestSendingMessage()) {
				showAtPositionNow(item->position(), nullptr);
				return tr::lng_slowmode_no_many(tr::now);
			}
		}
		return QString();
	}();
	if (text.isEmpty()) {
		return false;
	}
	Ui::ShowMultilineToast({
		.text = { text },
	});
	return true;
}

std::optional<QString> RepliesWidget::writeRestriction() const {
	return Data::RestrictionError(
		_history->peer,
		ChatRestriction::f_send_messages);
}

void RepliesWidget::pushReplyReturn(not_null<HistoryItem*> item) {
	if (item->history() == _history && item->replyToTop() == _rootId) {
		_replyReturns.push_back(item->id);
	} else {
		return;
	}
	_replyReturn = item;
	updateScrollDownVisibility();
}

void RepliesWidget::restoreReplyReturns(const std::vector<MsgId> &list) {
	_replyReturns = list;
	computeCurrentReplyReturn();
	if (!_replyReturn) {
		calculateNextReplyReturn();
	}
}

void RepliesWidget::computeCurrentReplyReturn() {
	_replyReturn = _replyReturns.empty()
		? nullptr
		: _history->owner().message(
			_history->channelId(),
			_replyReturns.back());
}

void RepliesWidget::calculateNextReplyReturn() {
	_replyReturn = nullptr;
	while (!_replyReturns.empty() && !_replyReturn) {
		_replyReturns.pop_back();
		computeCurrentReplyReturn();
	}
	if (!_replyReturn) {
		updateScrollDownVisibility();
	}
}

void RepliesWidget::checkReplyReturns() {
	const auto currentTop = _scroll->scrollTop();
	for (; _replyReturn != nullptr; calculateNextReplyReturn()) {
		const auto position = _replyReturn->position();
		const auto scrollTop = _inner->scrollTopForPosition(position);
		const auto scrolledBelow = scrollTop
			? (currentTop >= std::min(*scrollTop, _scroll->scrollTopMax()))
			: _inner->isBelowPosition(position);
		if (!scrolledBelow) {
			break;
		}
	}
}

void RepliesWidget::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	// #TODO replies schedule
	auto action = Api::SendAction(_history);
	action.replyTo = replyToId();
	session().api().sendFile(fileContent, type, action);
}

bool RepliesWidget::showSendingFilesError(
		const Ui::PreparedList &list) const {
	const auto text = [&] {
		const auto peer = _history->peer;
		const auto error = Data::RestrictionError(
			peer,
			ChatRestriction::f_send_media);
		if (error) {
			return *error;
		}
		if (peer->slowmodeApplied() && !list.canBeSentInSlowmode()) {
			return tr::lng_slowmode_no_many(tr::now);
		} else if (const auto left = _history->peer->slowmodeSecondsLeft()) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				Ui::FormatDurationWords(left));
		}
		using Error = Ui::PreparedList::Error;
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

	Ui::ShowMultilineToast({
		.text = { text },
	});
	return true;
}

void RepliesWidget::send() {
	if (_composeControls->getTextWithAppliedMarkdown().text.isEmpty()) {
		return;
	}
	send(Api::SendOptions());
	// #TODO replies schedule
	//const auto callback = [=](Api::SendOptions options) { send(options); };
	//Ui::show(
	//	PrepareScheduleBox(this, sendMenuType(), callback),
	//	Ui::LayerOption::KeepOther);
}

void RepliesWidget::sendVoice(
		QByteArray bytes,
		VoiceWaveform waveform,
		int duration) {
	auto action = Api::SendAction(_history);
	action.replyTo = replyToId();
	session().api().sendVoiceMessage(bytes, waveform, duration, action);
}

void RepliesWidget::send(Api::SendOptions options) {
	if (!options.scheduled && showSlowmodeError()) {
		return;
	}

	const auto webPageId = _composeControls->webPageId();/* _previewCancelled
		? CancelledWebPageId
		: ((_previewData && _previewData->pendingTill >= 0)
			? _previewData->id
			: WebPageId(0));*/

	auto message = ApiWrap::MessageToSend(_history);
	message.textWithTags = _composeControls->getTextWithAppliedMarkdown();
	message.action.options = options;
	message.action.replyTo = replyToId();
	message.webPageId = webPageId;

	//const auto error = GetErrorTextForSending(
	//	_peer,
	//	_toForward,
	//	message.textWithTags);
	//if (!error.isEmpty()) {
	//	Ui::ShowMultilineToast({
	//		.text = { error },
	//	});
	//	return;
	//}

	session().api().sendMessage(std::move(message));

	_composeControls->clear();
	session().sendProgressManager().update(
		_history,
		_rootId,
		Api::SendProgressType::Typing,
		-1);

	//_saveDraftText = true;
	//_saveDraftStart = crl::now();
	//onDraftSave();

	finishSending();
}

void RepliesWidget::edit(
		not_null<HistoryItem*> item,
		Api::SendOptions options,
		mtpRequestId *const saveEditMsgRequestId) {
	if (*saveEditMsgRequestId) {
		return;
	}
	const auto textWithTags = _composeControls->getTextWithAppliedMarkdown();
	const auto prepareFlags = Ui::ItemTextOptions(
		_history,
		session().user()).flags;
	auto sending = TextWithEntities();
	auto left = TextWithEntities {
		textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(textWithTags.tags) };
	TextUtilities::PrepareForSending(left, prepareFlags);

	if (!TextUtilities::CutPart(sending, left, MaxMessageSize)) {
		if (item) {
			Ui::show(Box<DeleteMessagesBox>(item, false));
		} else {
			doSetInnerFocus();
		}
		return;
	} else if (!left.text.isEmpty()) {
		Ui::show(Box<InformBox>(tr::lng_edit_too_long(tr::now)));
		return;
	}

	lifetime().add([=] {
		if (!*saveEditMsgRequestId) {
			return;
		}
		session().api().request(base::take(*saveEditMsgRequestId)).cancel();
	});

	const auto done = [=](const MTPUpdates &result, mtpRequestId requestId) {
		if (requestId == *saveEditMsgRequestId) {
			*saveEditMsgRequestId = 0;
			_composeControls->cancelEditMessage();
		}
	};

	const auto fail = [=](const RPCError &error, mtpRequestId requestId) {
		if (requestId == *saveEditMsgRequestId) {
			*saveEditMsgRequestId = 0;
		}

		const auto &err = error.type();
		if (ranges::contains(Api::kDefaultEditMessagesErrors, err)) {
			Ui::show(Box<InformBox>(tr::lng_edit_error(tr::now)));
		} else if (err == u"MESSAGE_NOT_MODIFIED"_q) {
			_composeControls->cancelEditMessage();
		} else if (err == u"MESSAGE_EMPTY"_q) {
			doSetInnerFocus();
		} else {
			Ui::show(Box<InformBox>(tr::lng_edit_error(tr::now)));
		}
		update();
		return true;
	};

	*saveEditMsgRequestId = Api::EditTextMessage(
		item,
		sending,
		options,
		crl::guard(this, done),
		crl::guard(this, fail));

	_composeControls->hidePanelsAnimated();
	doSetInnerFocus();
}

void RepliesWidget::sendExistingDocument(
		not_null<DocumentData*> document) {
	sendExistingDocument(document, Api::SendOptions());
	// #TODO replies schedule
	//const auto callback = [=](Api::SendOptions options) {
	//	sendExistingDocument(document, options);
	//};
	//Ui::show(
	//	PrepareScheduleBox(this, sendMenuType(), callback),
	//	Ui::LayerOption::KeepOther);
}

bool RepliesWidget::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options) {
	const auto error = Data::RestrictionError(
		_history->peer,
		ChatRestriction::f_send_stickers);
	if (error) {
		Ui::show(Box<InformBox>(*error), Ui::LayerOption::KeepOther);
		return false;
	} else if (showSlowmodeError()) {
		return false;
	}

	auto message = Api::MessageToSend(_history);
	message.action.replyTo = replyToId();
	message.action.options = options;
	Api::SendExistingDocument(std::move(message), document);

	//if (_fieldAutocomplete->stickersShown()) {
	//	clearFieldText();
	//	//_saveDraftText = true;
	//	//_saveDraftStart = crl::now();
	//	//onDraftSave();
	//	onCloudDraftSave(); // won't be needed if SendInlineBotResult will clear the cloud draft
	//}

	_composeControls->cancelReplyMessage();
	finishSending();
	return true;
}

void RepliesWidget::sendExistingPhoto(not_null<PhotoData*> photo) {
	sendExistingPhoto(photo, Api::SendOptions());
	// #TODO replies schedule
	//const auto callback = [=](Api::SendOptions options) {
	//	sendExistingPhoto(photo, options);
	//};
	//Ui::show(
	//	PrepareScheduleBox(this, sendMenuType(), callback),
	//	Ui::LayerOption::KeepOther);
}

bool RepliesWidget::sendExistingPhoto(
		not_null<PhotoData*> photo,
		Api::SendOptions options) {
	const auto error = Data::RestrictionError(
		_history->peer,
		ChatRestriction::f_send_media);
	if (error) {
		Ui::show(Box<InformBox>(*error), Ui::LayerOption::KeepOther);
		return false;
	} else if (showSlowmodeError()) {
		return false;
	}

	auto message = Api::MessageToSend(_history);
	message.action.replyTo = replyToId();
	message.action.options = options;
	Api::SendExistingPhoto(std::move(message), photo);

	_composeControls->cancelReplyMessage();
	finishSending();
	return true;
}

void RepliesWidget::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot) {
	const auto errorText = result->getErrorOnSend(_history);
	if (!errorText.isEmpty()) {
		Ui::show(Box<InformBox>(errorText));
		return;
	}
	sendInlineResult(result, bot, Api::SendOptions());
	//const auto callback = [=](Api::SendOptions options) {
	//	sendInlineResult(result, bot, options);
	//};
	//Ui::show(
	//	PrepareScheduleBox(this, sendMenuType(), callback),
	//	Ui::LayerOption::KeepOther);
}

void RepliesWidget::sendInlineResult(
		not_null<InlineBots::Result*> result,
		not_null<UserData*> bot,
		Api::SendOptions options) {
	auto action = Api::SendAction(_history);
	action.replyTo = replyToId();
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
		bot->session().local().writeRecentHashtagsAndBots();
	}
	finishSending();
}

SendMenu::Type RepliesWidget::sendMenuType() const {
	// #TODO replies schedule
	return _history->peer->isSelf()
		? SendMenu::Type::Reminder
		: HistoryView::CanScheduleUntilOnline(_history->peer)
		? SendMenu::Type::ScheduledToUser
		: SendMenu::Type::Scheduled;
}

MsgId RepliesWidget::replyToId() const {
	const auto custom = _composeControls->replyingToMessage().msg;
	return custom ? custom : _rootId;
}

void RepliesWidget::setupScrollDownButton() {
	_scrollDown->setClickedCallback([=] {
		scrollDownClicked();
	});
	base::install_event_filter(_scrollDown, [=](not_null<QEvent*> event) {
		if (event->type() != QEvent::Wheel) {
			return base::EventFilterResult::Continue;
		}
		return _scroll->viewportEvent(event)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
	updateScrollDownVisibility();
}

void RepliesWidget::scrollDownClicked() {
	if (QGuiApplication::keyboardModifiers() == Qt::ControlModifier) {
		showAtEnd();
	} else if (_replyReturn) {
		showAtPosition(_replyReturn->position());
	} else {
		showAtEnd();
	}
}

void RepliesWidget::showAtStart() {
	showAtPosition(Data::MinMessagePosition);
}

void RepliesWidget::showAtEnd() {
	showAtPosition(Data::MaxMessagePosition);
}

void RepliesWidget::finishSending() {
	_composeControls->hidePanelsAnimated();
	//if (_previewData && _previewData->pendingTill) previewCancel();
	doSetInnerFocus();
	showAtEnd();
}

void RepliesWidget::showAtPosition(
		Data::MessagePosition position,
		HistoryItem *originItem) {
	if (!showAtPositionNow(position, originItem)) {
		_inner->showAroundPosition(position, [=] {
			return showAtPositionNow(position, originItem);
		});
	}
}

bool RepliesWidget::showAtPositionNow(
		Data::MessagePosition position,
		HistoryItem *originItem,
		anim::type animated) {
	using AnimatedScroll = HistoryView::ListWidget::AnimatedScroll;
	const auto item = position.fullId
		? _history->owner().message(position.fullId)
		: nullptr;
	const auto use = item ? item->position() : position;
	if (const auto scrollTop = _inner->scrollTopForPosition(use)) {
		while (_replyReturn && use.fullId.msg == _replyReturn->id) {
			calculateNextReplyReturn();
		}
		const auto currentScrollTop = _scroll->scrollTop();
		const auto wanted = snap(*scrollTop, 0, _scroll->scrollTopMax());
		const auto fullDelta = (wanted - currentScrollTop);
		const auto limit = _scroll->height();
		const auto scrollDelta = snap(fullDelta, -limit, limit);
		const auto type = (animated == anim::type::instant)
			? AnimatedScroll::None
			: (std::abs(fullDelta) > limit)
			? AnimatedScroll::Part
			: AnimatedScroll::Full;
		_inner->scrollTo(
			wanted,
			use,
			scrollDelta,
			type);
		if (use != Data::MaxMessagePosition
			&& use != Data::UnreadMessagePosition) {
			_inner->highlightMessage(use.fullId);
		}
		if (originItem) {
			pushReplyReturn(originItem);
		}
		return true;
	}
	return false;
}

void RepliesWidget::updateScrollDownVisibility() {
	if (animating()) {
		return;
	}

	const auto scrollDownIsVisible = [&]() -> std::optional<bool> {
		const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
		if (top < _scroll->scrollTopMax() || _replyReturn) {
			return true;
		} else if (_inner->loadedAtBottomKnown()) {
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

void RepliesWidget::updateScrollDownPosition() {
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

void RepliesWidget::scrollDownAnimationFinish() {
	_scrollDownShown.stop();
	updateScrollDownPosition();
}

void RepliesWidget::updateAdaptiveLayout() {
	_topBarShadow->moveToLeft(
		Adaptive::OneColumn() ? 0 : st::lineWidth,
		_topBar->height());
}

not_null<History*> RepliesWidget::history() const {
	return _history;
}

Dialogs::RowDescriptor RepliesWidget::activeChat() const {
	return {
		_history,
		FullMsgId(_history->channelId(), ShowAtUnreadMsgId)
	};
}

QPixmap RepliesWidget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	_topBar->updateControlsVisibility();
	if (params.withTopBarShadow) _topBarShadow->hide();
	_composeControls->showForGrab();
	auto result = Ui::GrabWidget(this);
	if (params.withTopBarShadow) _topBarShadow->show();
	return result;
}

void RepliesWidget::doSetInnerFocus() {
	if (!_inner->getSelectedText().rich.text.isEmpty()
		|| !_inner->getSelectedItems().empty()
		|| !_composeControls->focus()) {
		_inner->setFocus();
	}
}

bool RepliesWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto logMemento = dynamic_cast<RepliesMemento*>(memento.get())) {
		if (logMemento->getHistory() == history()
			&& logMemento->getRootId() == _rootId) {
			restoreState(logMemento);
			return true;
		}
	}
	return false;
}

void RepliesWidget::setInternalState(
		const QRect &geometry,
		not_null<RepliesMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

bool RepliesWidget::pushTabbedSelectorToThirdSection(
		not_null<PeerData*> peer,
		const Window::SectionShow &params) {
	return _composeControls->pushTabbedSelectorToThirdSection(peer, params);
}

bool RepliesWidget::returnTabbedSelector() {
	return _composeControls->returnTabbedSelector();
}

std::unique_ptr<Window::SectionMemento> RepliesWidget::createMemento() {
	auto result = std::make_unique<RepliesMemento>(history(), _rootId);
	saveState(result.get());
	return result;
}

bool RepliesWidget::showMessage(
		PeerId peerId,
		const Window::SectionShow &params,
		MsgId messageId) {
	if (peerId != _history->peer->id) {
		return false;
	}
	const auto id = FullMsgId{
		_history->channelId(),
		messageId
	};
	const auto message = _history->owner().message(id);
	if (!message || message->replyToTop() != _rootId) {
		return false;
	}

	const auto originItem = [&]() -> HistoryItem* {
		using OriginMessage = Window::SectionShow::OriginMessage;
		if (const auto origin = std::get_if<OriginMessage>(&params.origin)) {
			if (const auto returnTo = session().data().message(origin->id)) {
				if (returnTo->history() == _history
					&& returnTo->replyToTop() == _rootId
					&& _replyReturn != returnTo) {
					return returnTo;
				}
			}
		}
		return nullptr;
	}();
	showAtPosition(
		Data::MessagePosition{ .fullId = id, .date = message->date() },
		originItem);
	return true;
}

bool RepliesWidget::replyToMessage(not_null<HistoryItem*> item) {
	if (item->history() != _history || item->replyToTop() != _rootId) {
		return false;
	}
	_composeControls->replyToMessage(item->fullId());
	return true;
}

void RepliesWidget::saveState(not_null<RepliesMemento*> memento) {
	memento->setReplies(_replies);
	memento->setReplyReturns(_replyReturns);
	_inner->saveState(memento->list());
}

void RepliesWidget::restoreState(not_null<RepliesMemento*> memento) {
	const auto setReplies = [&](std::shared_ptr<Data::RepliesList> replies) {
		_replies = std::move(replies);

		rpl::combine(
			rpl::single(0) | rpl::then(_replies->fullCount()),
			_areComments.value()
		) | rpl::map([=](int count, bool areComments) {
			return count
				? (areComments
					? tr::lng_comments_header
					: tr::lng_replies_header)(
						lt_count,
						rpl::single(count) | tr::to_count())
				: (areComments
					? tr::lng_comments_header_none
					: tr::lng_replies_header_none)();
		}) | rpl::flatten_latest(
		) | rpl::start_with_next([=](const QString &text) {
			_topBar->setCustomTitle(text);
		}, lifetime());
	};
	if (auto replies = memento->getReplies()) {
		setReplies(std::move(replies));
	} else if (!_replies) {
		setReplies(std::make_shared<Data::RepliesList>(_history, _rootId));
	}
	restoreReplyReturns(memento->replyReturns());
	_inner->restoreState(memento->list());
	if (const auto highlight = memento->getHighlightId()) {
		const auto position = Data::MessagePosition{
			.fullId = FullMsgId(_history->channelId(), highlight),
			.date = TimeId(0),
		};
		_inner->showAroundPosition(position, [=] {
			return showAtPositionNow(position, nullptr);
		});
	}
}

void RepliesWidget::resizeEvent(QResizeEvent *e) {
	if (!width() || !height()) {
		return;
	}
	_composeControls->resizeToWidth(width());
	recountChatWidth();
	updateControlsGeometry();
}

void RepliesWidget::recountChatWidth() {
	auto layout = (width() < st::adaptiveChatWideWidth)
		? Adaptive::ChatLayout::Normal
		: Adaptive::ChatLayout::Wide;
	if (layout != Global::AdaptiveChatLayout()) {
		Global::SetAdaptiveChatLayout(layout);
		Adaptive::Changed().notify(true);
	}
}

void RepliesWidget::updateControlsGeometry() {
	const auto contentWidth = width();

	const auto newScrollTop = _scroll->isHidden()
		? std::nullopt
		: base::make_optional(_scroll->scrollTop() + topDelta());
	_topBar->resizeToWidth(contentWidth);
	_topBarShadow->resize(contentWidth, st::lineWidth);
	if (_rootView) {
		_rootView->resizeToWidth(contentWidth);
	}
	_rootView->resizeToWidth(contentWidth);

	const auto bottom = height();
	const auto controlsHeight = _composeControls->heightCurrent();
	const auto scrollY = _topBar->height() + _rootViewHeight;
	const auto scrollHeight = bottom - scrollY - controlsHeight;
	const auto scrollSize = QSize(contentWidth, scrollHeight);
	if (_scroll->size() != scrollSize) {
		_skipScrollEvent = true;
		_scroll->resize(scrollSize);
		_inner->resizeToWidth(scrollSize.width(), _scroll->height());
		_skipScrollEvent = false;
	}
	_scroll->move(0, scrollY);
	if (!_scroll->isHidden()) {
		if (newScrollTop) {
			_scroll->scrollToY(*newScrollTop);
		}
		updateInnerVisibleArea();
	}
	_composeControls->move(0, bottom - controlsHeight);

	updateScrollDownPosition();
}

void RepliesWidget::paintEvent(QPaintEvent *e) {
	if (animating()) {
		SectionWidget::paintEvent(e);
		return;
	} else if (Ui::skipPaintEvent(this, e)) {
		return;
	}

	const auto aboveHeight = _topBar->height();
	const auto bg = e->rect().intersected(
		QRect(0, aboveHeight, width(), height() - aboveHeight));
	SectionWidget::PaintBackground(controller(), this, bg);
}

void RepliesWidget::onScroll() {
	if (_skipScrollEvent) {
		return;
	}
	updateInnerVisibleArea();
}

void RepliesWidget::updateInnerVisibleArea() {
	if (!_inner->animatedScrolling()) {
		checkReplyReturns();
	}
	const auto scrollTop = _scroll->scrollTop();
	_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	updatePinnedVisibility();
	updateScrollDownVisibility();
}

void RepliesWidget::updatePinnedVisibility() {
	if (!_loaded) {
		return;
	} else if (!_root) {
		setPinnedVisibility(true);
		return;
	}
	const auto item = [&] {
		if (const auto group = _history->owner().groups().find(_root)) {
			return group->items.front().get();
		}
		return _root;
	}();
	const auto view = _inner->viewByPosition(item->position());
	const auto visible = !view
		|| (view->y() + view->height() <= _scroll->scrollTop());
	setPinnedVisibility(visible);
}

void RepliesWidget::setPinnedVisibility(bool shown) {
	if (!animating()) {
		if (!_rootViewInited) {
			const auto height = shown ? st::historyReplyHeight : 0;
			if (const auto delta = height - _rootViewHeight) {
				_rootViewHeight = height;
				if (_scroll->scrollTop() == _scroll->scrollTopMax()) {
					setGeometryWithTopMoved(geometry(), delta);
				} else {
					updateControlsGeometry();
				}
			}
			if (shown) {
				_rootView->show();
			} else {
				_rootView->hide();
			}
			_rootVisible = shown;
			_rootView->finishAnimating();
			_rootViewInited = true;
		} else {
			_rootVisible = shown;
		}
	}
}

void RepliesWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBar->setAnimatingMode(true);
	if (params.withTopBarShadow) {
		_topBarShadow->show();
	}
	_composeControls->showStarted();
}

void RepliesWidget::showFinishedHook() {
	_topBar->setAnimatingMode(false);
	_composeControls->showFinished();

	// We should setup the drag area only after
	// the section animation is finished,
	// because after that the method showChildren() is called.
	setupDragArea();
	updatePinnedVisibility();
}

bool RepliesWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect RepliesWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

Context RepliesWidget::listContext() {
	return Context::Replies;
}

void RepliesWidget::listScrollTo(int top) {
	if (_scroll->scrollTop() != top) {
		_scroll->scrollToY(top);
	} else {
		updateInnerVisibleArea();
	}
}

void RepliesWidget::listCancelRequest() {
	if (_inner && !_inner->getSelectedItems().empty()) {
		clearSelected();
		return;
	}
	if (_composeControls->isEditingMessage()) {
		_composeControls->cancelEditMessage();
		return;
	} else if (_composeControls->replyingToMessage()) {
		_composeControls->cancelReplyMessage();
		return;
	}
	controller()->showBackFromStack();
}

void RepliesWidget::listDeleteRequest() {
	confirmDeleteSelected();
}

rpl::producer<Data::MessagesSlice> RepliesWidget::listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	return _replies->source(
		aroundId,
		limitBefore,
		limitAfter
	) | rpl::before_next([=] { // after_next makes a copy of value.
		if (!_loaded) {
			_loaded = true;
			crl::on_main(this, [=] {
				updatePinnedVisibility();
			});
		}
	});
}

bool RepliesWidget::listAllowsMultiSelect() {
	return true;
}

bool RepliesWidget::listIsItemGoodForSelection(
		not_null<HistoryItem*> item) {
	return IsServerMsgId(item->id);
}

bool RepliesWidget::listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) {
	return first->position() < second->position();
}

void RepliesWidget::listSelectionChanged(SelectedItems &&items) {
	HistoryView::TopBarWidget::SelectedState state;
	state.count = items.size();
	for (const auto item : items) {
		if (item.canDelete) {
			++state.canDeleteCount;
		}
		if (item.canForward) {
			++state.canForwardCount;
		}
	}
	_topBar->showSelected(state);
}

void RepliesWidget::readTill(not_null<HistoryItem*> item) {
	if (!_root) {
		return;
	}
	const auto was = _root->computeRepliesInboxReadTillFull();
	const auto now = item->id;
	const auto fast = item->out();
	if (was < now) {
		_root->setRepliesInboxReadTill(now);
		if (const auto post = _root->lookupDiscussionPostOriginal()) {
			post->setRepliesInboxReadTill(now);
		}
		if (!_readRequestTimer.isActive()) {
			_readRequestTimer.callOnce(fast ? 0 : kReadRequestTimeout);
		} else if (fast && _readRequestTimer.remainingTime() > 0) {
			_readRequestTimer.callOnce(0);
		}
	}
}

void RepliesWidget::listVisibleItemsChanged(HistoryItemsList &&items) {
	const auto reversed = ranges::view::reverse(items);
	const auto good = ranges::find_if(reversed, [](auto item) {
		return IsServerMsgId(item->id);
	});
	if (good != end(reversed)) {
		readTill(*good);
	}
}

MessagesBarData RepliesWidget::listMessagesBar(
		const std::vector<not_null<Element*>> &elements) {
	if (!_root || elements.empty()) {
		return {};
	}
	const auto till = _root->computeRepliesInboxReadTillFull();
	if (till < 2) {
		return {};
	}
	for (auto i = 0, count = int(elements.size()); i != count; ++i) {
		const auto item = elements[i]->data();
		if (IsServerMsgId(item->id) && item->id > till) {
			if (item->out() || !item->replyToId()) {
				readTill(item);
			} else {
				return MessagesBarData{
					// Designated initializers here crash MSVC 16.7.3.
					MessagesBar{
						.element = elements[i],
						.focus = true,
					},
					tr::lng_unread_bar_some(),
				};
			}
		}
	}
	return {};
}

void RepliesWidget::listContentRefreshed() {
}

ClickHandlerPtr RepliesWidget::listDateLink(not_null<Element*> view) {
	return nullptr;
}

bool RepliesWidget::listElementHideReply(not_null<const Element*> view) {
	return (view->data()->replyToId() == _rootId);
}

bool RepliesWidget::listElementShownUnread(not_null<const Element*> view) {
	if (!_root) {
		return false;
	}
	const auto item = view->data();
	const auto till = item->out()
		? _root->computeRepliesOutboxReadTillFull()
		: _root->computeRepliesInboxReadTillFull();
	return (item->id > till);
}

bool RepliesWidget::listIsGoodForAroundPosition(
		not_null<const Element*> view) {
	return IsServerMsgId(view->data()->id);
}

void RepliesWidget::confirmDeleteSelected() {
	ConfirmDeleteSelectedItems(_inner);
}

void RepliesWidget::confirmForwardSelected() {
	ConfirmForwardSelectedItems(_inner);
}

void RepliesWidget::clearSelected() {
	_inner->cancelSelection();
}

void RepliesWidget::setupDragArea() {
	const auto areas = DragArea::SetupDragAreaToContainer(
		this,
		[=](not_null<const QMimeData*> d) { return _history; },
		nullptr,
		[=] { updateControlsGeometry(); });

	const auto droppedCallback = [=](bool overrideSendImagesAsPhotos) {
		return [=](const QMimeData *data) {
			confirmSendingFiles(data, overrideSendImagesAsPhotos);
			Window::ActivateWindow(controller());
		};
	};
	areas.document->setDroppedCallback(droppedCallback(false));
	areas.photo->setDroppedCallback(droppedCallback(true));
}

} // namespace HistoryView
