/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "layout.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_service_message.h"
#include "history/history_item_components.h"
#include "history/history_media_types.h"
#include "history/history_media_grouped.h"
#include "history/history_service.h"
#include "history/history_message.h"
#include "history/history.h"
#include "media/media_clip_reader.h"
#include "styles/style_dialogs.h"
#include "styles/style_history.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text_options.h"
#include "storage/file_upload.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "storage/storage_feed_messages.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "media/media_audio.h"
#include "messenger.h"
#include "mainwindow.h"
#include "window/window_controller.h"
#include "core/crash_reports.h"
#include "data/data_session.h"
#include "data/data_messages.h"
#include "data/data_media_types.h"
#include "data/data_feed.h"

namespace {

not_null<HistoryItem*> CreateUnsupportedMessage(
		not_null<History*> history,
		MsgId msgId,
		MTPDmessage::Flags flags,
		MsgId replyTo,
		UserId viaBotId,
		TimeId date,
		UserId from) {
	const auto siteLink = qsl("https://desktop.telegram.org");
	auto text = TextWithEntities{
		lng_message_unsupported(lt_link, siteLink)
	};
	TextUtilities::ParseEntities(text, Ui::ItemTextNoMonoOptions().flags);
	text.entities.push_front(
		EntityInText(EntityInTextItalic, 0, text.text.size()));
	flags &= ~MTPDmessage::Flag::f_post_author;
	return new HistoryMessage(
		history,
		msgId,
		flags,
		replyTo,
		viaBotId,
		date,
		from,
		QString(),
		text);
}

} // namespace

void HistoryItem::HistoryItem::Destroyer::operator()(HistoryItem *value) {
	if (value) {
		value->destroy();
	}
}

HistoryItem::HistoryItem(
	not_null<History*> history,
	MsgId id,
	MTPDmessage::Flags flags,
	TimeId date,
	UserId from)
: id(id)
, _history(history)
, _from(from ? App::user(from) : history->peer)
, _flags(flags)
, _date(date) {
	App::historyRegItem(this);
}

TimeId HistoryItem::date() const {
	return _date;
}

void HistoryItem::finishEdition(int oldKeyboardTop) {
	Auth().data().requestItemViewRefresh(this);
	invalidateChatsListEntry();
	if (const auto group = Auth().data().groups().find(this)) {
		const auto leader = group->items.back();
		if (leader != this) {
			Auth().data().requestItemViewRefresh(leader);
			leader->invalidateChatsListEntry();
		}
	}

	//if (oldKeyboardTop >= 0) { // #TODO edit bot message
	//	if (auto keyboard = Get<HistoryMessageReplyMarkup>()) {
	//		keyboard->oldTop = oldKeyboardTop;
	//	}
	//}

	App::historyUpdateDependent(this);
}

void HistoryItem::setGroupId(MessageGroupId groupId) {
	Expects(!_groupId);

	_groupId = groupId;
	Auth().data().groups().registerMessage(this);
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
	if (const auto main = App::main()) {
		// #TODO feeds search results
		main->repaintDialogRow(history(), id);
	}

	// invalidate cache for drawInDialog
	if (history()->textCachedFor == this) {
		history()->textCachedFor = nullptr;
	}
	if (const auto feed = history()->peer->feed()) {
		if (feed->textCachedFor == this) {
			feed->textCachedFor = nullptr;
			feed->updateChatListEntry();
		}
	}
}

void HistoryItem::finishEditionToEmpty() {
	finishEdition(-1);
	_history->itemVanished(this);
}

bool HistoryItem::isMediaUnread() const {
	if (!mentionsMe() && _history->peer->isChannel()) {
		auto passed = unixtime() - date();
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

void HistoryItem::addLogEntryOriginal(
		WebPageId localId,
		const QString &label,
		const TextWithEntities &content) {
	Expects(isLogEntry());

	AddComponents(HistoryMessageLogEntryOriginal::Bit());
	Get<HistoryMessageLogEntryOriginal>()->page = Auth().data().webpage(
		localId,
		label,
		content);
}

UserData *HistoryItem::viaBot() const {
	if (const auto via = Get<HistoryMessageVia>()) {
		return via->bot;
	}
	return nullptr;
}

UserData *HistoryItem::getMessageBot() const {
	if (const auto bot = viaBot()) {
		return bot;
	}
	auto bot = from()->asUser();
	if (!bot) {
		bot = history()->peer->asUser();
	}
	return (bot && bot->botInfo) ? bot : nullptr;
};

void HistoryItem::destroy() {
	const auto history = this->history();
	if (isLogEntry()) {
		Assert(!mainView());
	} else {
		// All this must be done for all items manually in History::clear()!
		eraseFromUnreadMentions();
		if (IsServerMsgId(id)) {
			if (const auto types = sharedMediaTypes()) {
				Auth().storage().remove(Storage::SharedMediaRemoveOne(
					history->peer->id,
					types,
					id));
			}
		} else {
			Auth().api().cancelLocalItem(this);
		}
		_history->itemRemoved(this);
	}
	delete this;
}

void HistoryItem::refreshMainView() {
	if (const auto view = mainView()) {
		Auth().data().notifyHistoryChangeDelayed(_history);
		view->refreshInBlock();
	}
}

void HistoryItem::removeMainView() {
	if (const auto view = mainView()) {
		Auth().data().notifyHistoryChangeDelayed(_history);
		view->removeFromBlock();
	}
}

void HistoryItem::clearMainView() {
	_mainView = nullptr;
}

void HistoryItem::addToUnreadMentions(UnreadMentionType type) {
}

void HistoryItem::indexAsNewItem() {
	if (IsServerMsgId(id)) {
		CrashReports::SetAnnotation("addToUnreadMentions", QString::number(id));
		addToUnreadMentions(UnreadMentionType::New);
		CrashReports::ClearAnnotation("addToUnreadMentions");
		if (const auto types = sharedMediaTypes()) {
			Auth().storage().add(Storage::SharedMediaAddNew(
				history()->peer->id,
				types,
				id));
		}
		if (const auto channel = history()->peer->asChannel()) {
			if (const auto feed = channel->feed()) {
				Auth().storage().add(Storage::FeedMessagesAddNew(
					feed->id(),
					position()));
			}
		}
	}
}

void HistoryItem::setRealId(MsgId newId) {
	Expects(!IsServerMsgId(id));

	App::historyUnregItem(this);
	const auto oldId = std::exchange(id, newId);
	App::historyRegItem(this);

	// We don't need to call Notify::replyMarkupUpdated(this) and update keyboard
	// in history widget, because it can't exist for an outgoing message.
	// Only inline keyboards can be in outgoing messages.
	if (const auto markup = inlineReplyMarkup()) {
		if (markup->inlineKeyboard) {
			markup->inlineKeyboard->updateMessageId();
		}
	}

	Auth().data().notifyItemIdChange({ this, oldId });
	Auth().data().requestItemRepaint(this);
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

bool HistoryItem::allowsForward() const {
	return false;
}

bool HistoryItem::allowsEdit(TimeId now) const {
	return false;
}

bool HistoryItem::canDelete() const {
	if (isLogEntry() || (!IsServerMsgId(id) && serviceMsg())) {
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

bool HistoryItem::canDeleteForEveryone(TimeId now) const {
	auto messageToMyself = _history->peer->isSelf();
	auto messageTooOld = messageToMyself
		? false
		: (now >= date() + Global::EditTimeLimit());
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
	if (const auto media = this->media()) {
		if (!media->allowsRevoke()) {
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
	if (!IsServerMsgId(id)) {
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
			if (const auto media = this->media()) {
				if (const auto document = media->document()) {
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

ChannelId HistoryItem::channelId() const {
	return _history->channelId();
}

Data::MessagePosition HistoryItem::position() const {
	return Data::MessagePosition(date(), fullId());
}

MsgId HistoryItem::replyToId() const {
	if (auto reply = Get<HistoryMessageReply>()) {
		return reply->replyToId();
	}
	return 0;
}

not_null<PeerData*> HistoryItem::author() const {
	return isPost() ? history()->peer : from();
}

TimeId HistoryItem::dateOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalDate;
	}
	return date();
}

not_null<PeerData*> HistoryItem::senderOriginal() const {
	if (const auto forwarded = Get<HistoryMessageForwarded>()) {
		return forwarded->originalSender;
	}
	const auto peer = history()->peer;
	return (peer->isChannel() && !peer->isMegagroup()) ? peer : from();
}

not_null<PeerData*> HistoryItem::fromOriginal() const {
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

bool HistoryItem::needCheck() const {
	return out() || (id < 0 && history()->peer->isSelf());
}

bool HistoryItem::unread() const {
	// Messages from myself are always read.
	if (history()->peer->isSelf()) return false;

	if (out()) {
		// Outgoing messages in converted chats are always read.
		if (history()->peer->migrateTo()) {
			return false;
		}

		if (IsServerMsgId(id)) {
			if (!history()->isServerSideUnread(this)) {
				return false;
			}
			if (const auto user = history()->peer->asUser()) {
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

	if (IsServerMsgId(id)) {
		if (!history()->isServerSideUnread(this)) {
			return false;
		}
		return true;
	}
	return (_flags & MTPDmessage_ClientFlag::f_clientside_unread);
}

MessageGroupId HistoryItem::groupId() const {
	return _groupId;
}

bool HistoryItem::isEmpty() const {
	return _text.isEmpty()
		&& !_media
		&& !Has<HistoryMessageLogEntryOriginal>();
}

QString HistoryItem::notificationText() const {
	auto getText = [this]() {
		if (_media) {
			return _media->notificationText();
		} else if (!emptyText()) {
			return _text.originalText();
		}
		return QString();
	};

	auto result = getText();
	if (result.size() > 0xFF) {
		result = result.mid(0, 0xFF) + qsl("...");
	}
	return result;
}

QString HistoryItem::inDialogsText(DrawInDialog way) const {
	auto getText = [this]() {
		if (_media) {
			return _media->chatsListText();
		} else if (!emptyText()) {
			return TextUtilities::Clean(_text.originalText());
		}
		return QString();
	};
	const auto plainText = getText();
	const auto sender = [&]() -> PeerData* {
		if (isPost() || isEmpty() || (way == DrawInDialog::WithoutSender)) {
			return nullptr;
		} else if (!_history->peer->isUser() || out()) {
			return author();
		} else if (_history->peer->isSelf() && !Has<HistoryMessageForwarded>()) {
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
		cache.setText(st::dialogsTextStyle, inDialogsText(way), Ui::DialogTextOptions());
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
	Auth().data().notifyItemRemoved(this);
	App::historyUnregItem(this);
	if (id < 0 && !App::quitting()) {
		Auth().uploader().cancel(fullId());
	}
}

QDateTime ItemDateTime(not_null<const HistoryItem*> item) {
	return ParseDateTime(item->date());
}

ClickHandlerPtr goToMessageClickHandler(
		not_null<HistoryItem*> item,
		FullMsgId returnToId) {
	return goToMessageClickHandler(
		item->history()->peer,
		item->id,
		returnToId);
}

ClickHandlerPtr goToMessageClickHandler(
		not_null<PeerData*> peer,
		MsgId msgId,
		FullMsgId returnToId) {
	return std::make_shared<LambdaClickHandler>([=] {
		if (const auto main = App::main()) {
			if (const auto returnTo = App::histItemById(returnToId)) {
				if (returnTo->history()->peer == peer) {
					main->pushReplyReturn(returnTo);
				}
			}
			App::wnd()->controller()->showPeerHistory(
				peer,
				Window::SectionShow::Way::Forward,
				msgId);
		}
	});
}

not_null<HistoryItem*> HistoryItem::Create(
		not_null<History*> history,
		const MTPMessage &message) {
	switch (message.type()) {
	case mtpc_messageEmpty: {
		const auto &data = message.c_messageEmpty();
		const auto text = HistoryService::PreparedText {
			lang(lng_message_empty)
		};
		return new HistoryService(history, data.vid.v, TimeId(0), text);
	} break;

	case mtpc_message: {
		const auto &data = message.c_message();
		enum class MediaCheckResult {
			Good,
			Unsupported,
			Empty,
			HasTimeToLive,
		};
		auto badMedia = MediaCheckResult::Good;
		const auto &media = data.vmedia;
		if (data.has_media()) switch (media.type()) {
		case mtpc_messageMediaEmpty:
		case mtpc_messageMediaContact: break;
		case mtpc_messageMediaGeo:
			switch (media.c_messageMediaGeo().vgeo.type()) {
			case mtpc_geoPoint: break;
			case mtpc_geoPointEmpty: badMedia = MediaCheckResult::Empty; break;
			default: badMedia = MediaCheckResult::Unsupported; break;
			}
			break;
		case mtpc_messageMediaVenue:
			switch (media.c_messageMediaVenue().vgeo.type()) {
			case mtpc_geoPoint: break;
			case mtpc_geoPointEmpty: badMedia = MediaCheckResult::Empty; break;
			default: badMedia = MediaCheckResult::Unsupported; break;
			}
			break;
		case mtpc_messageMediaGeoLive:
			switch (media.c_messageMediaGeoLive().vgeo.type()) {
			case mtpc_geoPoint: break;
			case mtpc_geoPointEmpty: badMedia = MediaCheckResult::Empty; break;
			default: badMedia = MediaCheckResult::Unsupported; break;
			}
			break;
		case mtpc_messageMediaPhoto: {
			auto &photo = media.c_messageMediaPhoto();
			if (photo.has_ttl_seconds()) {
				badMedia = MediaCheckResult::HasTimeToLive;
			} else if (!photo.has_photo()) {
				badMedia = MediaCheckResult::Empty;
			} else {
				switch (photo.vphoto.type()) {
				case mtpc_photo: break;
				case mtpc_photoEmpty: badMedia = MediaCheckResult::Empty; break;
				default: badMedia = MediaCheckResult::Unsupported; break;
				}
			}
		} break;
		case mtpc_messageMediaDocument: {
			auto &document = media.c_messageMediaDocument();
			if (document.has_ttl_seconds()) {
				badMedia = MediaCheckResult::HasTimeToLive;
			} else if (!document.has_document()) {
				badMedia = MediaCheckResult::Empty;
			} else {
				switch (document.vdocument.type()) {
				case mtpc_document: break;
				case mtpc_documentEmpty: badMedia = MediaCheckResult::Empty; break;
				default: badMedia = MediaCheckResult::Unsupported; break;
				}
			}
		} break;
		case mtpc_messageMediaWebPage:
			switch (media.c_messageMediaWebPage().vwebpage.type()) {
			case mtpc_webPage:
			case mtpc_webPageEmpty:
			case mtpc_webPagePending: break;
			case mtpc_webPageNotModified:
			default: badMedia = MediaCheckResult::Unsupported; break;
			}
			break;
		case mtpc_messageMediaGame:
		switch (media.c_messageMediaGame().vgame.type()) {
			case mtpc_game: break;
			default: badMedia = MediaCheckResult::Unsupported; break;
			}
			break;
		case mtpc_messageMediaInvoice:
			break;
		case mtpc_messageMediaUnsupported:
		default: badMedia = MediaCheckResult::Unsupported; break;
		}
		if (badMedia == MediaCheckResult::Unsupported) {
			return CreateUnsupportedMessage(
				history,
				data.vid.v,
				data.vflags.v,
				data.vreply_to_msg_id.v,
				data.vvia_bot_id.v,
				data.vdate.v,
				data.vfrom_id.v);
		} else if (badMedia == MediaCheckResult::Empty) {
			const auto text = HistoryService::PreparedText {
				lang(lng_message_empty)
			};
			return new HistoryService(
				history,
				data.vid.v,
				data.vdate.v,
				text,
				data.vflags.v,
				data.has_from_id() ? data.vfrom_id.v : UserId(0));
		} else if (badMedia == MediaCheckResult::HasTimeToLive) {
			return new HistoryService(history, data);
		}
		return new HistoryMessage(history, data);
	} break;

	case mtpc_messageService: {
		auto &data = message.c_messageService();
		if (data.vaction.type() == mtpc_messageActionPhoneCall) {
			return new HistoryMessage(history, data);
		}
		return new HistoryService(history, data);
	} break;
	}

	Unexpected("Type in HistoryItem::Create().");
}
