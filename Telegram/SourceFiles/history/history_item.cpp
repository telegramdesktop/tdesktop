/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "history/history_item.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "history/history_item_components.h"
#include "history/history_service_layout.h"
#include "history/history_media_types.h"
#include "history/history_media_grouped.h"
#include "history/history_message.h"
#include "media/media_clip_reader.h"
#include "styles/style_dialogs.h"
#include "styles/style_history.h"
#include "ui/effects/ripple_animation.h"
#include "storage/file_upload.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "auth_session.h"
#include "media/media_audio.h"
#include "messenger.h"
#include "mainwindow.h"
#include "window/window_controller.h"

namespace {

// a new message from the same sender is attached to previous within 15 minutes
constexpr int kAttachMessageToPreviousSecondsDelta = 900;

} // namespace

HistoryTextState::HistoryTextState(not_null<const HistoryItem*> item)
: itemId(item->fullId()) {
}

HistoryTextState::HistoryTextState(
	not_null<const HistoryItem*> item,
	const Text::StateResult &state)
: itemId(item->fullId())
, cursor(state.uponSymbol
	? HistoryInTextCursorState
	: HistoryDefaultCursorState)
, link(state.link)
, afterSymbol(state.afterSymbol)
, symbol(state.symbol) {
}

HistoryTextState::HistoryTextState(
	not_null<const HistoryItem*> item,
	ClickHandlerPtr link)
: itemId(item->fullId())
, link(link) {
}

HistoryMediaPtr::HistoryMediaPtr() = default;

HistoryMediaPtr::HistoryMediaPtr(std::unique_ptr<HistoryMedia> pointer)
: _pointer(std::move(pointer)) {
	if (_pointer) {
		_pointer->attachToParent();
	}
}

void HistoryMediaPtr::reset(std::unique_ptr<HistoryMedia> pointer) {
	*this = std::move(pointer);
}

HistoryMediaPtr &HistoryMediaPtr::operator=(std::unique_ptr<HistoryMedia> pointer) {
	if (_pointer) {
		_pointer->detachFromParent();
	}
	_pointer = std::move(pointer);
	if (_pointer) {
		_pointer->attachToParent();
	}
	return *this;
}

HistoryMediaPtr::~HistoryMediaPtr() {
	reset();
}

namespace internal {

TextSelection unshiftSelection(TextSelection selection, uint16 byLength) {
	if (selection == FullSelection) {
		return selection;
	}
	return ::unshiftSelection(selection, byLength);
}

TextSelection shiftSelection(TextSelection selection, uint16 byLength) {
	if (selection == FullSelection) {
		return selection;
	}
	return ::shiftSelection(selection, byLength);
}

} // namespace internal

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	QDateTime date,
	UserId from) : HistoryElement()
, id(id)
, date(date)
, _history(history)
, _from(from ? App::user(from) : history->peer)
, _flags(flags | MTPDmessage_ClientFlag::f_pending_init_dimensions | MTPDmessage_ClientFlag::f_pending_resize) {
}

void HistoryItem::finishCreate() {
	App::historyRegItem(this);
}

void HistoryItem::finishEdition(int oldKeyboardTop) {
	setPendingInitDimensions();
	invalidateChatsListEntry();
	//if (groupId()) {
	//	history()->fixGroupAfterEdition(this);
	//}
	if (isHiddenByGroup()) {
		// Perhaps caption was changed, we should refresh the group.
		const auto group = Get<HistoryMessageGroup>();
		group->leader->setPendingInitDimensions();
		group->leader->invalidateChatsListEntry();
	}

	if (oldKeyboardTop >= 0) {
		if (auto keyboard = Get<HistoryMessageReplyMarkup>()) {
			keyboard->oldTop = oldKeyboardTop;
		}
	}

	App::historyUpdateDependent(this);
}

HistoryMessageReplyMarkup *HistoryItem::inlineReplyMarkup() {
	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_inline) {
			return markup;
		}
	}
	return nullptr;
}

ReplyKeyboard *HistoryItem::inlineReplyKeyboard() {
	if (const auto markup = inlineReplyMarkup()) {
		return markup->inlineKeyboard.get();
	}
	return nullptr;
}

void HistoryItem::invalidateChatsListEntry() {
	if (App::main()) {
		App::main()->dlgUpdated(history()->peer, id);
	}

	// invalidate cache for drawInDialog
	if (history()->textCachedFor == this) {
		history()->textCachedFor = nullptr;
	}
}

void HistoryItem::finishEditionToEmpty() {
	recountDisplayDate();
	finishEdition(-1);

	_history->removeNotification(this);
	if (auto channel = history()->peer->asChannel()) {
		if (channel->pinnedMessageId() == id) {
			channel->clearPinnedMessage();
		}
	}
	if (history()->lastKeyboardId == id) {
		history()->clearLastKeyboard();
	}
	if ((!out() || isPost()) && unread() && history()->unreadCount() > 0) {
		history()->setUnreadCount(history()->unreadCount() - 1);
	}

	if (auto next = nextItem()) {
		next->previousItemChanged();
	}
	if (auto previous = previousItem()) {
		previous->nextItemChanged();
	}
}

bool HistoryItem::isMediaUnread() const {
	if (!mentionsMe() && _history->peer->isChannel()) {
		auto now = ::date(unixtime());
		auto passed = date.secsTo(now);
		if (passed >= Global::ChannelsReadMediaPeriod()) {
			return false;
		}
	}
	return _flags & MTPDmessage::Flag::f_media_unread;
}

void HistoryItem::markMediaRead() {
	_flags &= ~MTPDmessage::Flag::f_media_unread;

	if (mentionsMe()) {
		history()->updateChatListEntry();
		history()->eraseFromUnreadMentions(id);
	}
}

bool HistoryItem::definesReplyKeyboard() const {
	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_inline) {
			return false;
		}
		return true;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	return (_flags & MTPDmessage::Flag::f_reply_markup);
}

MTPDreplyKeyboardMarkup::Flags HistoryItem::replyKeyboardFlags() const {
	Expects(definesReplyKeyboard());

	if (const auto markup = Get<HistoryMessageReplyMarkup>()) {
		return markup->flags;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	return MTPDreplyKeyboardMarkup_ClientFlag::f_zero | 0;
}

void HistoryItem::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->inlineKeyboard) {
			markup->inlineKeyboard->clickHandlerActiveChanged(p, active);
		}
	}
	App::hoveredLinkItem(active ? this : nullptr);
	Auth().data().requestItemRepaint(this);
}

void HistoryItem::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->inlineKeyboard) {
			markup->inlineKeyboard->clickHandlerPressedChanged(p, pressed);
		}
	}
	App::pressedLinkItem(pressed ? this : nullptr);
	Auth().data().requestItemRepaint(this);
}

void HistoryItem::addLogEntryOriginal(WebPageId localId, const QString &label, const TextWithEntities &content) {
	Expects(isLogEntry());
	AddComponents(HistoryMessageLogEntryOriginal::Bit());
	auto original = Get<HistoryMessageLogEntryOriginal>();
	auto webpage = App::feedWebPage(localId, label, content);
	original->_page = std::make_unique<HistoryWebPage>(this, webpage);
}

UserData *HistoryItem::viaBot() const {
	if (const auto via = Get<HistoryMessageVia>()) {
		return via->bot;
	}
	return nullptr;
}

void HistoryItem::destroy() {
	if (isLogEntry()) {
		Assert(detached());
	} else {
		// All this must be done for all items manually in History::clear(false)!
		eraseFromUnreadMentions();
		if (IsServerMsgId(id)) {
			if (auto types = sharedMediaTypes()) {
				Auth().storage().remove(Storage::SharedMediaRemoveOne(
					history()->peer->id,
					types,
					id));
			}
		}

		auto wasAtBottom = history()->loadedAtBottom();
		_history->removeNotification(this);
		detach();
		if (auto channel = history()->peer->asChannel()) {
			if (channel->pinnedMessageId() == id) {
				channel->clearPinnedMessage();
			}
		}
		if (history()->lastMsg == this) {
			history()->fixLastMessage(wasAtBottom);
		}
		if (history()->lastKeyboardId == id) {
			history()->clearLastKeyboard();
		}
		if ((!out() || isPost()) && unread() && history()->unreadCount() > 0) {
			history()->setUnreadCount(history()->unreadCount() - 1);
		}
	}
	Global::RefPendingRepaintItems().remove(this);
	delete this;
}

void HistoryItem::detach() {
	if (detached()) return;

	if (_history->isChannel()) {
		_history->asChannelHistory()->messageDetached(this);
	}
	_block->removeItem(this);
	App::historyItemDetached(this);

	_history->setPendingResize();
}

void HistoryItem::detachFast() {
	_block = nullptr;
	_indexInBlock = -1;

	validateGroupId();
	if (groupId()) {
		makeGroupLeader({});
	}
}

Storage::SharedMediaTypesMask HistoryItem::sharedMediaTypes() const {
	return {};
}

void HistoryItem::previousItemChanged() {
	Expects(!isLogEntry());
	recountDisplayDate();
	recountAttachToPrevious();
}

// Called only if there is no more next item! Not always when it changes!
void HistoryItem::nextItemChanged() {
	Expects(!isLogEntry());
	setAttachToNext(false);
}

bool HistoryItem::computeIsAttachToPrevious(not_null<HistoryItem*> previous) {
	if (!Has<HistoryMessageDate>() && !Has<HistoryMessageUnreadBar>()) {
		const auto possible = !isPost() && !previous->isPost()
			&& !serviceMsg() && !previous->serviceMsg()
			&& !isEmpty() && !previous->isEmpty()
			&& (qAbs(previous->date.secsTo(date)) < kAttachMessageToPreviousSecondsDelta);
		if (possible) {
			if (history()->peer->isSelf()) {
				return previous->senderOriginal() == senderOriginal()
					&& (previous->Has<HistoryMessageForwarded>() == Has<HistoryMessageForwarded>());
			} else {
				return previous->from() == from();
			}
		}
	}
	return false;
}

void HistoryItem::recountAttachToPrevious() {
	Expects(!isLogEntry());
	auto attachToPrevious = false;
	if (auto previous = previousItem()) {
		attachToPrevious = computeIsAttachToPrevious(previous);
		previous->setAttachToNext(attachToPrevious);
	}
	setAttachToPrevious(attachToPrevious);
}

void HistoryItem::setAttachToNext(bool attachToNext) {
	if (attachToNext && !(_flags & MTPDmessage_ClientFlag::f_attach_to_next)) {
		_flags |= MTPDmessage_ClientFlag::f_attach_to_next;
		Global::RefPendingRepaintItems().insert(this);
	} else if (!attachToNext && (_flags & MTPDmessage_ClientFlag::f_attach_to_next)) {
		_flags &= ~MTPDmessage_ClientFlag::f_attach_to_next;
		Global::RefPendingRepaintItems().insert(this);
	}
}

void HistoryItem::setAttachToPrevious(bool attachToPrevious) {
	if (attachToPrevious && !(_flags & MTPDmessage_ClientFlag::f_attach_to_previous)) {
		_flags |= MTPDmessage_ClientFlag::f_attach_to_previous;
		setPendingInitDimensions();
	} else if (!attachToPrevious && (_flags & MTPDmessage_ClientFlag::f_attach_to_previous)) {
		_flags &= ~MTPDmessage_ClientFlag::f_attach_to_previous;
		setPendingInitDimensions();
	}
}

void HistoryItem::setId(MsgId newId) {
	history()->changeMsgId(id, newId);
	id = newId;

	// We don't need to call Notify::replyMarkupUpdated(this) and update keyboard
	// in history widget, because it can't exist for an outgoing message.
	// Only inline keyboards can be in outgoing messages.
	if (auto markup = inlineReplyMarkup()) {
		if (markup->inlineKeyboard) {
			markup->inlineKeyboard->updateMessageId();
		}
	}

	if (_media) {
		_media->refreshParentId(this);
		if (const auto group = Get<HistoryMessageGroup>()) {
			if (group->leader != this) {
				if (const auto media = group->leader->getMedia()) {
					media->refreshParentId(group->leader);
				}
			}
		}
	}
}

bool HistoryItem::isPinned() const {
	if (auto channel = _history->peer->asChannel()) {
		return (channel->pinnedMessageId() == id);
	}
	return false;
}

bool HistoryItem::canPin() const {
	if (id < 0 || !toHistoryMessage()) {
		return false;
	}
	if (auto channel = _history->peer->asChannel()) {
		return channel->canPinMessages();
	}
	return false;
}

bool HistoryItem::canForward() const {
	if (id < 0 || isLogEntry()) {
		return false;
	}
	if (auto message = toHistoryMessage()) {
		if (auto media = message->getMedia()) {
			if (media->type() == MediaTypeCall) {
				return false;
			}
		}
		return true;
	}
	return false;
}

bool HistoryItem::canEdit(const QDateTime &cur) const {
	auto messageToMyself = _history->peer->isSelf();
	auto canPinInMegagroup = [&] {
		if (auto megagroup = _history->peer->asMegagroup()) {
			return megagroup->canPinMessages();
		}
		return false;
	}();
	auto messageTooOld = (messageToMyself || canPinInMegagroup)
		? false
		: (date.secsTo(cur) >= Global::EditTimeLimit());
	if (id < 0 || messageTooOld) {
		return false;
	}

	if (auto message = toHistoryMessage()) {
		if (message->Has<HistoryMessageVia>() || message->Has<HistoryMessageForwarded>()) {
			return false;
		}

		if (auto media = message->getMedia()) {
			if (!media->canEditCaption() && media->type() != MediaTypeWebPage) {
				return false;
			}
		}
		if (messageToMyself) {
			return true;
		}
		if (auto channel = _history->peer->asChannel()) {
			if (isPost() && channel->canEditMessages()) {
				return true;
			}
			if (out()) {
				return !isPost() || channel->canPublish();
			}
		} else {
			return out();
		}
	}
	return false;
}

bool HistoryItem::canDelete() const {
	if (isLogEntry()) {
		return false;
	}
	auto channel = _history->peer->asChannel();
	if (!channel) {
		return !(_flags & MTPDmessage_ClientFlag::f_is_group_migrate);
	}

	if (id == 1) {
		return false;
	}
	if (channel->canDeleteMessages()) {
		return true;
	}
	if (out() && toHistoryMessage()) {
		return isPost() ? channel->canPublish() : true;
	}
	return false;
}

bool HistoryItem::canDeleteForEveryone(const QDateTime &cur) const {
	auto messageToMyself = _history->peer->isSelf();
	auto messageTooOld = messageToMyself ? false : (date.secsTo(cur) >= Global::EditTimeLimit());
	if (id < 0 || messageToMyself || messageTooOld || isPost()) {
		return false;
	}
	if (history()->peer->isChannel()) {
		return false;
	} else if (auto user = history()->peer->asUser()) {
		// Bots receive all messages and there is no sense in revoking them.
		// See https://github.com/telegramdesktop/tdesktop/issues/3818
		if (user->botInfo) {
			return false;
		}
	}
	if (!toHistoryMessage()) {
		return false;
	}
	if (auto media = getMedia()) {
		if (media->type() == MediaTypeCall) {
			return false;
		}
	}
	if (!out()) {
		if (auto chat = history()->peer->asChat()) {
			if (!chat->amCreator() && (!chat->amAdmin() || !chat->adminsEnabled())) {
				return false;
			}
		} else {
			return false;
		}
	}
	return true;
}

bool HistoryItem::suggestBanReport() const {
	auto channel = history()->peer->asChannel();
	auto fromUser = from()->asUser();
	if (!channel || !fromUser || !channel->canRestrictUser(fromUser)) {
		return false;
	}
	return !isPost() && !out() && toHistoryMessage();
}

bool HistoryItem::suggestDeleteAllReport() const {
	auto channel = history()->peer->asChannel();
	if (!channel || !channel->canDeleteMessages()) {
		return false;
	}
	return !isPost() && !out() && from()->isUser() && toHistoryMessage();
}

bool HistoryItem::hasDirectLink() const {
	if (id <= 0) {
		return false;
	}
	if (auto channel = _history->peer->asChannel()) {
		return channel->isPublic();
	}
	return false;
}

QString HistoryItem::directLink() const {
	if (hasDirectLink()) {
		auto channel = _history->peer->asChannel();
		Assert(channel != nullptr);
		auto query = channel->username + '/' + QString::number(id);
		if (!channel->isMegagroup()) {
			if (auto media = getMedia()) {
				if (auto document = media->getDocument()) {
					if (document->isVideoMessage()) {
						return qsl("https://telesco.pe/") + query;
					}
				}
			}
		}
		return Messenger::Instance().createInternalLinkFull(query);
	}
	return QString();
}

MsgId HistoryItem::replyToId() const {
	if (auto reply = Get<HistoryMessageReply>()) {
		return reply->replyToId();
	}
	return 0;
}

QDateTime HistoryItem::dateOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalDate;
	}
	return date;
}

PeerData *HistoryItem::senderOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalSender;
	}
	const auto peer = history()->peer;
	return (peer->isChannel() && !peer->isMegagroup()) ? peer : from();
}

PeerData *HistoryItem::fromOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		if (const auto user = forwarded->originalSender->asUser()) {
			return user;
		}
	}
	return from();
}

QString HistoryItem::authorOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalAuthor;
	} else if (const auto msgsigned = Get<HistoryMessageSigned>()) {
		return msgsigned->author;
	}
	return QString();
}

MsgId HistoryItem::idOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalId;
	}
	return id;
}

bool HistoryItem::hasOutLayout() const {
	if (history()->peer->isSelf()) {
		return !Has<HistoryMessageForwarded>();
	}
	return out() && !isPost();
}

bool HistoryItem::unread() const {
	// Messages from myself are always read.
	if (history()->peer->isSelf()) return false;

	if (out()) {
		// Outgoing messages in converted chats are always read.
		if (history()->peer->migrateTo()) {
			return false;
		}

		if (id > 0) {
			if (id < history()->outboxReadBefore) {
				return false;
			}
			if (auto user = history()->peer->asUser()) {
				if (user->botInfo) {
					return false;
				}
			} else if (auto channel = history()->peer->asChannel()) {
				if (!channel->isMegagroup()) {
					return false;
				}
			}
		}
		return true;
	}

	if (id > 0) {
		if (id < history()->inboxReadBefore) {
			return false;
		}
		return true;
	}
	return (_flags & MTPDmessage_ClientFlag::f_clientside_unread);
}

void HistoryItem::destroyUnreadBar() {
	if (Has<HistoryMessageUnreadBar>()) {
		Assert(!isLogEntry());

		RemoveComponents(HistoryMessageUnreadBar::Bit());
		setPendingInitDimensions();
		if (_history->unreadBar == this) {
			_history->unreadBar = nullptr;
		}

		recountAttachToPrevious();
	}
}

void HistoryItem::setUnreadBarCount(int count) {
	Expects(!isLogEntry());
	if (count > 0) {
		HistoryMessageUnreadBar *bar;
		if (!Has<HistoryMessageUnreadBar>()) {
			AddComponents(HistoryMessageUnreadBar::Bit());
			setPendingInitDimensions();

			recountAttachToPrevious();

			bar = Get<HistoryMessageUnreadBar>();
		} else {
			bar = Get<HistoryMessageUnreadBar>();
			if (bar->_freezed) {
				return;
			}
			Global::RefPendingRepaintItems().insert(this);
		}
		bar->init(count);
	} else {
		destroyUnreadBar();
	}
}

void HistoryItem::setUnreadBarFreezed() {
	Expects(!isLogEntry());

	if (const auto bar = Get<HistoryMessageUnreadBar>()) {
		bar->_freezed = true;
	}
}

MessageGroupId HistoryItem::groupId() const {
	if (const auto group = Get<HistoryMessageGroup>()) {
		return group->groupId;
	}
	return MessageGroupId::None;
}

bool HistoryItem::groupIdValidityChanged() {
	if (Has<HistoryMessageGroup>()) {
		if (_media && _media->canBeGrouped()) {
			return false;
		}
		RemoveComponents(HistoryMessageGroup::Bit());
		setPendingInitDimensions();
		return true;
	}
	return false;
}

void HistoryItem::makeGroupMember(not_null<HistoryItem*> leader) {
	Expects(leader != this);

	const auto group = Get<HistoryMessageGroup>();
	Assert(group != nullptr);
	if (group->leader == this) {
		if (auto single = _media ? _media->takeLastFromGroup() : nullptr) {
			_media = std::move(single);
		}
		_flags |= MTPDmessage_ClientFlag::f_hidden_by_group;
		setPendingInitDimensions();

		group->leader = leader;
		base::take(group->others);
	} else if (group->leader != leader) {
		group->leader = leader;
	}

	Ensures(isHiddenByGroup());
	Ensures(group->others.empty());
}

void HistoryItem::makeGroupLeader(
		std::vector<not_null<HistoryItem*>> &&others) {
	const auto group = Get<HistoryMessageGroup>();
	Assert(group != nullptr);

	const auto leaderChanged = (group->leader != this);
	if (leaderChanged) {
		group->leader = this;
		_flags &= ~MTPDmessage_ClientFlag::f_hidden_by_group;
		setPendingInitDimensions();
	}
	group->others = std::move(others);
	if (!_media || !_media->applyGroup(group->others)) {
		resetGroupMedia(group->others);
		invalidateChatsListEntry();
	}

	Ensures(!isHiddenByGroup());
}

HistoryMessageGroup *HistoryItem::getFullGroup() {
	if (const auto group = Get<HistoryMessageGroup>()) {
		if (group->leader == this) {
			return group;
		}
		return group->leader->Get<HistoryMessageGroup>();
	}
	return nullptr;
}

void HistoryItem::resetGroupMedia(
		const std::vector<not_null<HistoryItem*>> &others) {
	if (!others.empty()) {
		_media = std::make_unique<HistoryGroupedMedia>(this, others);
	} else if (_media) {
		_media = _media->takeLastFromGroup();
	}
	setPendingInitDimensions();
}

int HistoryItem::displayedDateHeight() const {
	if (auto date = Get<HistoryMessageDate>()) {
		return date->height();
	}
	return 0;
}

int HistoryItem::marginTop() const {
	auto result = 0;
	if (!isHiddenByGroup()) {
		if (isAttachedToPrevious()) {
			result += st::msgMarginTopAttached;
		} else {
			result += st::msgMargin.top();
		}
	}
	result += displayedDateHeight();
	if (const auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		result += unreadbar->height();
	}
	return result;
}

bool HistoryItem::displayDate() const {
	return Has<HistoryMessageDate>();
}

bool HistoryItem::isEmpty() const {
	return _text.isEmpty()
		&& !_media
		&& !Has<HistoryMessageLogEntryOriginal>();
}

int HistoryItem::marginBottom() const {
	return isHiddenByGroup() ? 0 : st::msgMargin.bottom();
}

void HistoryItem::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;

	auto media = getMedia();
	if (!media) {
		return;
	}

	auto reader = media->getClipReader();
	if (!reader) {
		return;
	}

	switch (notification) {
	case NotificationReinit: {
		auto stopped = false;
		if (reader->autoPausedGif()) {
			auto amVisible = false;
			Auth().data().queryItemVisibility().notify({ this, &amVisible }, true);
			if (!amVisible) { // stop animation if it is not visible
				media->stopInline();
				if (auto document = media->getDocument()) { // forget data from memory
					document->forget();
				}
				stopped = true;
			}
		} else if (reader->mode() == Media::Clip::Reader::Mode::Video && reader->state() == Media::Clip::State::Finished) {
			// Stop finished video message.
			media->stopInline();
		}
		if (!stopped) {
			setPendingInitDimensions();
			if (detached()) {
				// We still want to handle our pending initDimensions and
				// resize state even if we're detached in history.
				_history->setHasPendingResizedItems();
			}
			Auth().data().markItemLayoutChanged(this);
			Global::RefPendingRepaintItems().insert(this);
		}
	} break;

	case NotificationRepaint: {
		if (!reader->currentDisplayed()) {
			Auth().data().requestItemRepaint(this);
		}
	} break;
	}
}

void HistoryItem::audioTrackUpdated() {
	auto media = getMedia();
	if (!media) {
		return;
	}

	auto reader = media->getClipReader();
	if (!reader || reader->mode() != Media::Clip::Reader::Mode::Video) {
		return;
	}

	auto audio = reader->audioMsgId();
	auto current = Media::Player::mixer()->currentState(audio.type());
	if (current.id != audio || Media::Player::IsStoppedOrStopping(current.state)) {
		media->stopInline();
	} else if (Media::Player::IsPaused(current.state) || current.state == Media::Player::State::Pausing) {
		if (!reader->videoPaused()) {
			reader->pauseResumeVideo();
		}
	} else {
		if (reader->videoPaused()) {
			reader->pauseResumeVideo();
		}
	}
}

void HistoryItem::recountDisplayDate() {
	Expects(!isLogEntry());
	setDisplayDate([&] {
		if (isEmpty()) {
			return false;
		}

		if (auto previous = previousItem()) {
			return previous->isEmpty() || (previous->date.date() != date.date());
		}
		return true;
	}());
}

void HistoryItem::setDisplayDate(bool displayDate) {
	if (displayDate && !Has<HistoryMessageDate>()) {
		AddComponents(HistoryMessageDate::Bit());
		Get<HistoryMessageDate>()->init(date);
		setPendingInitDimensions();
	} else if (!displayDate && Has<HistoryMessageDate>()) {
		RemoveComponents(HistoryMessageDate::Bit());
		setPendingInitDimensions();
	}
}

QString HistoryItem::notificationText() const {
	auto getText = [this]() {
		if (emptyText()) {
			return _media ? _media->notificationText() : QString();
		}
		return _text.originalText();
	};

	auto result = getText();
	if (result.size() > 0xFF) {
		result = result.mid(0, 0xFF) + qsl("...");
	}
	return result;
}

QString HistoryItem::inDialogsText(DrawInDialog way) const {
	auto getText = [this]() {
		if (emptyText()) {
			return _media ? _media->inDialogsText() : QString();
		}
		return TextUtilities::Clean(_text.originalText());
	};
	const auto plainText = getText();
	const auto sender = [&]() -> PeerData* {
		if (isPost() || isEmpty() || (way == DrawInDialog::WithoutSender)) {
			return nullptr;
		} else if (!_history->peer->isUser() || out()) {
			return author();
		} else if (_history->peer->isSelf() && !hasOutLayout()) {
			return senderOriginal();
		}
		return nullptr;
	}();
	if (sender) {
		auto fromText = sender->isSelf() ? lang(lng_from_you) : sender->shortName();
		auto fromWrapped = textcmdLink(1, lng_dialogs_text_from_wrapped(lt_from, TextUtilities::Clean(fromText)));
		return lng_dialogs_text_with_from(lt_from_part, fromWrapped, lt_message, plainText);
	}
	return plainText;
}

void HistoryItem::drawInDialog(
		Painter &p,
		const QRect &r,
		bool active,
		bool selected,
		DrawInDialog way,
		const HistoryItem *&cacheFor,
		Text &cache) const {
	if (cacheFor != this) {
		cacheFor = this;
		cache.setText(st::dialogsTextStyle, inDialogsText(way), _textDlgOptions);
	}
	if (r.width()) {
		p.setTextPalette(active ? st::dialogsTextPaletteActive : (selected ? st::dialogsTextPaletteOver : st::dialogsTextPalette));
		p.setFont(st::dialogsTextFont);
		p.setPen(active ? st::dialogsTextFgActive : (selected ? st::dialogsTextFgOver : st::dialogsTextFg));
		cache.drawElided(p, r.left(), r.top(), r.width(), r.height() / st::dialogsTextFont->height);
		p.restoreTextPalette();
	}
}

HistoryItem::~HistoryItem() {
	App::historyUnregItem(this);
	if (id < 0 && !App::quitting()) {
		Auth().uploader().cancel(fullId());
	}
}

ClickHandlerPtr goToMessageClickHandler(PeerData *peer, MsgId msgId) {
	return std::make_shared<LambdaClickHandler>([peer, msgId] {
		if (App::main()) {
			auto current = App::mousedItem();
			if (current && current->history()->peer == peer) {
				App::main()->pushReplyReturn(current);
			}
			App::wnd()->controller()->showPeerHistory(
				peer,
				Window::SectionShow::Way::Forward,
				msgId);
		}
	});
}
