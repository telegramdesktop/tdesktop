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
#include "history/history_message.h"

#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "history/history_location_manager.h"
#include "history/history_service_layout.h"
#include "history/history_media_types.h"
#include "history/history_service.h"
#include "auth_session.h"
#include "boxes/share_box.h"
#include "boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "messenger.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"
#include "styles/style_history.h"
#include "window/notifications_manager.h"
#include "observer_peer.h"

namespace {

constexpr auto kPinnedMessageTextLimit = 16;

inline void initTextOptions() {
	_historySrvOptions.dir = _textNameOptions.dir = _textDlgOptions.dir = cLangDir();
	_textDlgOptions.maxw = st::dialogsWidthMax * 2;
}

style::color fromNameFg(int index) {
	Expects(index >= 0 && index < 8);
	style::color colors[] = {
		st::historyPeer1NameFg,
		st::historyPeer2NameFg,
		st::historyPeer3NameFg,
		st::historyPeer4NameFg,
		st::historyPeer5NameFg,
		st::historyPeer6NameFg,
		st::historyPeer7NameFg,
		st::historyPeer8NameFg,
	};
	return colors[index];
}

style::color fromNameFgSelected(int index) {
	Expects(index >= 0 && index < 8);
	style::color colors[] = {
		st::historyPeer1NameFgSelected,
		st::historyPeer2NameFgSelected,
		st::historyPeer3NameFgSelected,
		st::historyPeer4NameFgSelected,
		st::historyPeer5NameFgSelected,
		st::historyPeer6NameFgSelected,
		st::historyPeer7NameFgSelected,
		st::historyPeer8NameFgSelected,
	};
	return colors[index];
}

MTPDmessage::Flags NewForwardedFlags(not_null<PeerData*> peer, UserId from, not_null<HistoryMessage*> fwd) {
	auto result = NewMessageFlags(peer) | MTPDmessage::Flag::f_fwd_from;
	if (from) {
		result |= MTPDmessage::Flag::f_from_id;
	}
	if (fwd->Has<HistoryMessageVia>()) {
		result |= MTPDmessage::Flag::f_via_bot_id;
	}
	if (auto channel = peer->asChannel()) {
		if (auto media = fwd->getMedia()) {
			if (media->type() == MediaTypeWebPage) {
				// Drop web page if we're not allowed to send it.
				if (channel->restrictedRights().is_embed_links()) {
					result &= MTPDmessage::Flag::f_media;
				}
			}
		}
	} else {
		if (auto media = fwd->getMedia()) {
			if (media->type() == MediaTypeVoiceFile) {
				result |= MTPDmessage::Flag::f_media_unread;
//			} else if (media->type() == MediaTypeVideo) {
//				result |= MTPDmessage::flag_media_unread;
			}
		}
	}
	if (fwd->hasViews()) {
		result |= MTPDmessage::Flag::f_views;
	}
	return result;
}

bool HasMediaItems(const SelectedItemSet &items) {
	for_const (auto item, items) {
		if (auto media = item->getMedia()) {
			switch (media->type()) {
			case MediaTypePhoto:
			case MediaTypeVideo:
			case MediaTypeFile:
			case MediaTypeMusicFile:
			case MediaTypeVoiceFile: return true;
			case MediaTypeGif: return media->getDocument()->isRoundVideo();
			}
		}
	}
	return false;
}

bool HasStickerItems(const SelectedItemSet &items) {
	for_const (auto item, items) {
		if (auto media = item->getMedia()) {
			switch (media->type()) {
			case MediaTypeSticker: return true;
			}
		}
	}
	return false;
}

bool HasGifItems(const SelectedItemSet &items) {
	for_const (auto item, items) {
		if (auto media = item->getMedia()) {
			switch (media->type()) {
			case MediaTypeGif: return !media->getDocument()->isRoundVideo();
			}
		}
	}
	return false;
}

bool HasGameItems(const SelectedItemSet &items) {
	for_const (auto item, items) {
		if (auto media = item->getMedia()) {
			switch (media->type()) {
			case MediaTypeGame: return true;
			}
		}
	}
	return false;
}

bool HasInlineItems(const SelectedItemSet &items) {
	for_const (auto item, items) {
		if (item->viaBot()) {
			return true;
		}
	}
	return false;
}

} // namespace

void FastShareMessage(not_null<HistoryItem*> item) {
	struct ShareData {
		ShareData(const FullMsgId &msgId) : msgId(msgId) {
		}
		FullMsgId msgId;
		OrderedSet<mtpRequestId> requests;
	};
	auto data = MakeShared<ShareData>(item->fullId());
	auto isGame = item->getMessageBot() && item->getMedia() && (item->getMedia()->type() == MediaTypeGame);

	auto canCopyLink = item->hasDirectLink();
	if (!canCopyLink) {
		if (auto bot = item->getMessageBot()) {
			if (auto media = item->getMedia()) {
				canCopyLink = (media->type() == MediaTypeGame);
			}
		}
	}
	auto copyCallback = [data]() {
		if (auto main = App::main()) {
			if (auto item = App::histItemById(data->msgId)) {
				if (item->hasDirectLink()) {
					QApplication::clipboard()->setText(item->directLink());

					Ui::Toast::Show(lang(lng_channel_public_link_copied));
				} else if (auto bot = item->getMessageBot()) {
					if (auto media = item->getMedia()) {
						if (media->type() == MediaTypeGame) {
							auto shortName = static_cast<HistoryGame*>(media)->game()->shortName;

							QApplication::clipboard()->setText(Messenger::Instance().createInternalLinkFull(bot->username + qsl("?game=") + shortName));

							Ui::Toast::Show(lang(lng_share_game_link_copied));
						}
					}
				}
			}
		}
	};
	auto submitCallback = [data](const QVector<PeerData*> &result) {
		if (!data->requests.empty()) {
			return; // Share clicked already.
		}
		auto item = App::histItemById(data->msgId);
		if (!item || result.empty()) {
			return;
		}

		auto items = SelectedItemSet();
		auto restrictedSomewhere = false;
		auto restrictedEverywhere = true;
		auto firstError = QString();
		items.insert(item->id, item);
		for_const (auto peer, result) {
			auto error = GetErrorTextForForward(peer, items);
			if (!error.isEmpty()) {
				if (firstError.isEmpty()) {
					firstError = error;
				}
				restrictedSomewhere = true;
				continue;
			}
			restrictedEverywhere = false;
		}
		if (restrictedEverywhere) {
			Ui::show(Box<InformBox>(firstError), KeepOtherLayers);
			return;
		}

		auto doneCallback = [data](const MTPUpdates &updates, mtpRequestId requestId) {
			if (auto main = App::main()) {
				main->sentUpdatesReceived(updates);
			}
			data->requests.remove(requestId);
			if (data->requests.empty()) {
				Ui::Toast::Show(lang(lng_share_done));
				Ui::hideLayer();
			}
		};

		auto sendFlags = MTPmessages_ForwardMessages::Flag::f_with_my_score;
		MTPVector<MTPint> msgIds = MTP_vector<MTPint>(1, MTP_int(data->msgId.msg));
		if (auto main = App::main()) {
			for_const (auto peer, result) {
				if (!GetErrorTextForForward(peer, items).isEmpty()) {
					continue;
				}

				MTPVector<MTPlong> random = MTP_vector<MTPlong>(1, rand_value<MTPlong>());
				auto request = MTPmessages_ForwardMessages(MTP_flags(sendFlags), item->history()->peer->input, msgIds, random, peer->input);
				auto callback = doneCallback;
				auto requestId = MTP::send(request, rpcDone(std::move(callback)));
				data->requests.insert(requestId);
			}
		}
	};
	auto filterCallback = [isGame](PeerData *peer) {
		if (peer->canWrite()) {
			if (auto channel = peer->asChannel()) {
				return isGame ? (!channel->isBroadcast()) : true;
			}
			return true;
		}
		return false;
	};
	auto copyLinkCallback = canCopyLink ? base::lambda<void()>(std::move(copyCallback)) : base::lambda<void()>();
	Ui::show(Box<ShareBox>(std::move(copyLinkCallback), std::move(submitCallback), std::move(filterCallback)));
}

void HistoryInitMessages() {
	initTextOptions();
}

base::lambda<void(ChannelData*, MsgId)> HistoryDependentItemCallback(const FullMsgId &msgId) {
	return [dependent = msgId](ChannelData *channel, MsgId msgId) {
		if (auto item = App::histItemById(dependent)) {
			item->updateDependencyItem();
		}
	};
}

MTPDmessage::Flags NewMessageFlags(not_null<PeerData*> peer) {
	MTPDmessage::Flags result = 0;
	if (!peer->isSelf()) {
		result |= MTPDmessage::Flag::f_out;
		//if (p->isChat() || (p->isUser() && !p->asUser()->botInfo)) {
		//	result |= MTPDmessage::Flag::f_unread;
		//}
	}
	return result;
}

QString GetErrorTextForForward(not_null<PeerData*> peer, const SelectedItemSet &items) {
	if (!peer->canWrite()) {
		return lang(lng_forward_cant);
	}

	if (auto megagroup = peer->asMegagroup()) {
		if (megagroup->restrictedRights().is_send_media() && HasMediaItems(items)) {
			return lang(lng_restricted_send_media);
		} else if (megagroup->restrictedRights().is_send_stickers() && HasStickerItems(items)) {
			return lang(lng_restricted_send_stickers);
		} else if (megagroup->restrictedRights().is_send_gifs() && HasGifItems(items)) {
			return lang(lng_restricted_send_gifs);
		} else if (megagroup->restrictedRights().is_send_games() && HasGameItems(items)) {
			return lang(lng_restricted_send_inline);
		} else if (megagroup->restrictedRights().is_send_inline() && HasInlineItems(items)) {
			return lang(lng_restricted_send_inline);
		}
	}
	return QString();
}

void HistoryMessageVia::create(UserId userId) {
	_bot = App::user(peerFromUser(userId));
	_maxWidth = st::msgServiceNameFont->width(lng_inline_bot_via(lt_inline_bot, '@' + _bot->username));
	_lnk = MakeShared<LambdaClickHandler>([bot = _bot] {
		App::insertBotCommand('@' + bot->username);
	});
}

void HistoryMessageVia::resize(int32 availw) const {
	if (availw < 0) {
		_text = QString();
		_width = 0;
	} else {
		_text = lng_inline_bot_via(lt_inline_bot, '@' + _bot->username);
		if (availw < _maxWidth) {
			_text = st::msgServiceNameFont->elided(_text, availw);
			_width = st::msgServiceNameFont->width(_text);
		} else if (_width < _maxWidth) {
			_width = _maxWidth;
		}
	}
}

void HistoryMessageSigned::create(const QString &author, const QString &date) {
	auto time = qsl(", ") + date;
	auto name = author;
	auto timew = st::msgDateFont->width(time);
	auto namew = st::msgDateFont->width(name);
	if (timew + namew > st::maxSignatureSize) {
		name = st::msgDateFont->elided(author, st::maxSignatureSize - timew);
	}
	_author = author;
	_signature.setText(st::msgDateTextStyle, name + time, _textNameOptions);
}

int HistoryMessageSigned::maxWidth() const {
	return _signature.maxWidth();
}

void HistoryMessageEdited::create(const QDateTime &editDate, const QString &date) {
	_editDate = editDate;
	_edited.setText(st::msgDateTextStyle, lang(lng_edited) + ' ' + date, _textNameOptions);
}

int HistoryMessageEdited::maxWidth() const {
	return _edited.maxWidth();
}

void HistoryMessageForwarded::create(const HistoryMessageVia *via) const {
	QString text;
	auto fromChannel = (_originalSender->isChannel() && !_originalSender->isMegagroup());
	if (!_originalAuthor.isEmpty()) {
		text = lng_forwarded_signed(lt_channel, App::peerName(_originalSender), lt_user, _originalAuthor);
	} else {
		text = App::peerName(_originalSender);
	}
	if (via) {
		if (fromChannel) {
			text = lng_forwarded_channel_via(lt_channel, textcmdLink(1, text), lt_inline_bot, textcmdLink(2, '@' + via->_bot->username));
		} else {
			text = lng_forwarded_via(lt_user, textcmdLink(1, text), lt_inline_bot, textcmdLink(2, '@' + via->_bot->username));
		}
	} else {
		if (fromChannel) {
			text = lng_forwarded_channel(lt_channel, textcmdLink(1, text));
		} else {
			text = lng_forwarded(lt_user, textcmdLink(1, text));
		}
	}
	TextParseOptions opts = { TextParseRichText, 0, 0, Qt::LayoutDirectionAuto };
	_text.setText(st::fwdTextStyle, text, opts);
	_text.setLink(1, fromChannel ? goToMessageClickHandler(_originalSender, _originalId) : _originalSender->openLink());
	if (via) {
		_text.setLink(2, via->_lnk);
	}
}

bool HistoryMessageReply::updateData(HistoryMessage *holder, bool force) {
	if (!force) {
		if (replyToMsg || !replyToMsgId) {
			return true;
		}
	}
	if (!replyToMsg) {
		replyToMsg = App::histItemById(holder->channelId(), replyToMsgId);
		if (replyToMsg) {
			App::historyRegDependency(holder, replyToMsg);
		}
	}

	if (replyToMsg) {
		replyToText.setText(st::messageTextStyle, TextUtilities::Clean(replyToMsg->inReplyText()), _textDlgOptions);

		updateName();

		replyToLnk = goToMessageClickHandler(replyToMsg);
		if (!replyToMsg->Has<HistoryMessageForwarded>()) {
			if (auto bot = replyToMsg->viaBot()) {
				_replyToVia = std::make_unique<HistoryMessageVia>();
				_replyToVia->create(peerToUser(bot->id));
			}
		}
	} else if (force) {
		replyToMsgId = 0;
	}
	if (force) {
		holder->setPendingInitDimensions();
	}
	return (replyToMsg || !replyToMsgId);
}

void HistoryMessageReply::clearData(HistoryMessage *holder) {
	_replyToVia = nullptr;
	if (replyToMsg) {
		App::historyUnregDependency(holder, replyToMsg);
		replyToMsg = nullptr;
	}
	replyToMsgId = 0;
}

bool HistoryMessageReply::isNameUpdated() const {
	if (replyToMsg && replyToMsg->author()->nameVersion > replyToVersion) {
		updateName();
		return true;
	}
	return false;
}

void HistoryMessageReply::updateName() const {
	if (replyToMsg) {
		QString name = (_replyToVia && replyToMsg->author()->isUser()) ? replyToMsg->author()->asUser()->firstName : App::peerName(replyToMsg->author());
		replyToName.setText(st::fwdTextStyle, name, _textNameOptions);
		replyToVersion = replyToMsg->author()->nameVersion;
		bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
		int32 previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		int32 w = replyToName.maxWidth();
		if (_replyToVia) {
			w += st::msgServiceFont->spacew + _replyToVia->_maxWidth;
		}

		_maxReplyWidth = previewSkip + qMax(w, qMin(replyToText.maxWidth(), int32(st::maxSignatureSize)));
	} else {
		_maxReplyWidth = st::msgDateFont->width(lang(replyToMsgId ? lng_profile_loading : lng_deleted_message));
	}
	_maxReplyWidth = st::msgReplyPadding.left() + st::msgReplyBarSkip + _maxReplyWidth + st::msgReplyPadding.right();
}

void HistoryMessageReply::resize(int width) const {
	if (_replyToVia) {
		bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
		int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		_replyToVia->resize(width - st::msgReplyBarSkip - previewSkip - replyToName.maxWidth() - st::msgServiceFont->spacew);
	}
}

void HistoryMessageReply::itemRemoved(HistoryMessage *holder, HistoryItem *removed) {
	if (replyToMsg == removed) {
		clearData(holder);
		holder->setPendingInitDimensions();
	}
}

void HistoryMessageReply::paint(Painter &p, const HistoryItem *holder, int x, int y, int w, PaintFlags flags) const {
	bool selected = (flags & PaintFlag::Selected), outbg = holder->hasOutLayout();

	style::color bar = st::msgImgReplyBarColor;
	if (flags & PaintFlag::InBubble) {
		bar = (flags & PaintFlag::Selected) ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor);
	}
	QRect rbar(rtlrect(x + st::msgReplyBarPos.x(), y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height(), w + 2 * x));
	p.fillRect(rbar, bar);

	if (w > st::msgReplyBarSkip) {
		if (replyToMsg) {
			auto hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
			if (hasPreview && w < st::msgReplyBarSkip + st::msgReplyBarSize.height()) {
				hasPreview = false;
			}
			auto previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;

			if (hasPreview) {
				ImagePtr replyPreview = replyToMsg->getMedia()->replyPreview();
				if (!replyPreview->isNull()) {
					auto to = rtlrect(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height(), w + 2 * x);
					auto previewWidth = replyPreview->width() / cIntRetinaFactor();
					auto previewHeight = replyPreview->height() / cIntRetinaFactor();
					auto preview = replyPreview->pixSingle(previewWidth, previewHeight, to.width(), to.height(), ImageRoundRadius::Small, ImageRoundCorner::All, selected ? &st::msgStickerOverlay : nullptr);
					p.drawPixmap(to.x(), to.y(), preview);
				}
			}
			if (w > st::msgReplyBarSkip + previewSkip) {
				if (flags & PaintFlag::InBubble) {
					p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
				} else {
					p.setPen(st::msgImgReplyBarColor);
				}
				replyToName.drawLeftElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top(), w - st::msgReplyBarSkip - previewSkip, w + 2 * x);
				if (_replyToVia && w > st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew) {
					p.setFont(st::msgServiceFont);
					p.drawText(x + st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew, y + st::msgReplyPadding.top() + st::msgServiceFont->ascent, _replyToVia->_text);
				}

				auto replyToAsMsg = replyToMsg->toHistoryMessage();
				if (!(flags & PaintFlag::InBubble)) {
				} else if ((replyToAsMsg && replyToAsMsg->emptyText()) || replyToMsg->serviceMsg()) {
					p.setPen(outbg ? (selected ? st::msgOutDateFgSelected : st::msgOutDateFg) : (selected ? st::msgInDateFgSelected : st::msgInDateFg));
				} else {
					p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
				}
				replyToText.drawLeftElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top() + st::msgServiceNameFont->height, w - st::msgReplyBarSkip - previewSkip, w + 2 * x);
			}
		} else {
			p.setFont(st::msgDateFont);
			auto &date = outbg ? (selected ? st::msgOutDateFgSelected : st::msgOutDateFg) : (selected ? st::msgInDateFgSelected : st::msgInDateFg);
			p.setPen((flags & PaintFlag::InBubble) ? date : st::msgDateImgFg);
			p.drawTextLeft(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2, w + 2 * x, st::msgDateFont->elided(lang(replyToMsgId ? lng_profile_loading : lng_deleted_message), w - st::msgReplyBarSkip));
		}
	}
}

void HistoryMessage::KeyboardStyle::startPaint(Painter &p) const {
	p.setPen(st::msgServiceFg);
}

const style::TextStyle &HistoryMessage::KeyboardStyle::textStyle() const {
	return st::serviceTextStyle;
}

void HistoryMessage::KeyboardStyle::repaint(not_null<const HistoryItem*> item) const {
	Ui::repaintHistoryItem(item);
}

int HistoryMessage::KeyboardStyle::buttonRadius() const {
	return st::dateRadius;
}

void HistoryMessage::KeyboardStyle::paintButtonBg(Painter &p, const QRect &rect, float64 howMuchOver) const {
	App::roundRect(p, rect, st::msgServiceBg, StickerCorners);
	if (howMuchOver > 0) {
		auto o = p.opacity();
		p.setOpacity(o * howMuchOver);
		App::roundRect(p, rect, st::msgBotKbOverBgAdd, BotKbOverCorners);
		p.setOpacity(o);
	}
}

void HistoryMessage::KeyboardStyle::paintButtonIcon(Painter &p, const QRect &rect, int outerWidth, HistoryMessageReplyMarkup::Button::Type type) const {
	using Button = HistoryMessageReplyMarkup::Button;
	auto getIcon = [](Button::Type type) -> const style::icon* {
		switch (type) {
		case Button::Type::Url: return &st::msgBotKbUrlIcon;
		case Button::Type::SwitchInlineSame:
		case Button::Type::SwitchInline: return &st::msgBotKbSwitchPmIcon;
		}
		return nullptr;
	};
	if (auto icon = getIcon(type)) {
		icon->paint(p, rect.x() + rect.width() - icon->width() - st::msgBotKbIconPadding, rect.y() + st::msgBotKbIconPadding, outerWidth);
	}
}

void HistoryMessage::KeyboardStyle::paintButtonLoading(Painter &p, const QRect &rect) const {
	auto icon = &st::historySendingInvertedIcon;
	icon->paint(p, rect.x() + rect.width() - icon->width() - st::msgBotKbIconPadding, rect.y() + rect.height() - icon->height() - st::msgBotKbIconPadding, rect.x() * 2 + rect.width());
}

int HistoryMessage::KeyboardStyle::minButtonWidth(HistoryMessageReplyMarkup::Button::Type type) const {
	using Button = HistoryMessageReplyMarkup::Button;
	int result = 2 * buttonPadding(), iconWidth = 0;
	switch (type) {
	case Button::Type::Url: iconWidth = st::msgBotKbUrlIcon.width(); break;
	case Button::Type::SwitchInlineSame:
	case Button::Type::SwitchInline: iconWidth = st::msgBotKbSwitchPmIcon.width(); break;
	case Button::Type::Callback:
	case Button::Type::Game: iconWidth = st::historySendingInvertedIcon.width(); break;
	}
	if (iconWidth > 0) {
		result = std::max(result, 2 * iconWidth + 4 * int(st::msgBotKbIconPadding));
	}
	return result;
}

HistoryMessage::HistoryMessage(not_null<History*> history, const MTPDmessage &msg)
: HistoryItem(history, msg.vid.v, msg.vflags.v, ::date(msg.vdate), msg.has_from_id() ? msg.vfrom_id.v : 0) {
	CreateConfig config;

	if (msg.has_fwd_from() && msg.vfwd_from.type() == mtpc_messageFwdHeader) {
		auto &f = msg.vfwd_from.c_messageFwdHeader();
		config.originalDate = ::date(f.vdate);
		if (f.has_from_id() || f.has_channel_id()) {
			config.senderOriginal = f.has_channel_id() ? peerFromChannel(f.vchannel_id) : peerFromUser(f.vfrom_id);
			if (f.has_channel_post()) config.originalId = f.vchannel_post.v;
			if (f.has_post_author()) config.authorOriginal = qs(f.vpost_author);
		}
	}
	if (msg.has_reply_to_msg_id()) config.replyTo = msg.vreply_to_msg_id.v;
	if (msg.has_via_bot_id()) config.viaBotId = msg.vvia_bot_id.v;
	if (msg.has_views()) config.viewsCount = msg.vviews.v;
	if (msg.has_reply_markup()) config.mtpMarkup = &msg.vreply_markup;
	if (msg.has_edit_date()) config.editDate = ::date(msg.vedit_date);
	if (msg.has_post_author()) config.author = qs(msg.vpost_author);

	createComponents(config);

	initMedia(msg.has_media() ? (&msg.vmedia) : nullptr);

	auto text = TextUtilities::Clean(qs(msg.vmessage));
	auto entities = msg.has_entities() ? TextUtilities::EntitiesFromMTP(msg.ventities.v) : EntitiesInText();
	setText({ text, entities });
}

HistoryMessage::HistoryMessage(not_null<History*> history, const MTPDmessageService &msg)
: HistoryItem(history, msg.vid.v, mtpCastFlags(msg.vflags.v), ::date(msg.vdate), msg.has_from_id() ? msg.vfrom_id.v : 0) {
	CreateConfig config;

	if (msg.has_reply_to_msg_id()) config.replyTo = msg.vreply_to_msg_id.v;

	createComponents(config);

	switch (msg.vaction.type()) {
	case mtpc_messageActionPhoneCall: {
		_media = std::make_unique<HistoryCall>(this, msg.vaction.c_messageActionPhoneCall());
	} break;

	default: Unexpected("Service message action type in HistoryMessage.");
	}

	setText(TextWithEntities {});
}

HistoryMessage::HistoryMessage(not_null<History*> history, MsgId id, MTPDmessage::Flags flags, QDateTime date, UserId from, const QString &postAuthor, not_null<HistoryMessage*> fwd)
: HistoryItem(history, id, NewForwardedFlags(history->peer, from, fwd) | flags, date, from) {
	CreateConfig config;

	if (fwd->Has<HistoryMessageForwarded>() || !fwd->history()->peer->isSelf()) {
		// Server doesn't add "fwd_from" to non-forwarded messages from chat with yourself.
		config.originalDate = fwd->dateOriginal();
		auto senderOriginal = fwd->senderOriginal();
		config.senderOriginal = senderOriginal->id;
		config.authorOriginal = fwd->authorOriginal();
		if (senderOriginal->isChannel()) {
			config.originalId = fwd->idOriginal();
		}
	}
	if (flags & MTPDmessage::Flag::f_post_author) {
		config.author = postAuthor;
	}
	auto fwdViaBot = fwd->viaBot();
	if (fwdViaBot) config.viaBotId = peerToUser(fwdViaBot->id);
	int fwdViewsCount = fwd->viewsCount();
	if (fwdViewsCount > 0) {
		config.viewsCount = fwdViewsCount;
	} else if (isPost()) {
		config.viewsCount = 1;
	}

	// Copy inline keyboard when forwarding messages with a game.
	auto mediaOriginal = fwd->getMedia();
	auto mediaType = mediaOriginal ? mediaOriginal->type() : MediaTypeCount;
	if (mediaOriginal && mediaType == MediaTypeGame) {
		config.inlineMarkup = fwd->inlineReplyMarkup();
	}

	createComponents(config);

	auto cloneMedia = [this, history, mediaType] {
		if (mediaType == MediaTypeWebPage) {
			if (auto channel = history->peer->asChannel()) {
				if (channel->restrictedRights().is_embed_links()) {
					return false;
				}
			}
		}
		return (mediaType != MediaTypeCount);
	};
	if (cloneMedia()) {
		_media = mediaOriginal->clone(this);
	}
	setText(fwd->originalText());
}

HistoryMessage::HistoryMessage(not_null<History*> history, MsgId id, MTPDmessage::Flags flags, MsgId replyTo, UserId viaBotId, QDateTime date, UserId from, const QString &postAuthor, const TextWithEntities &textWithEntities)
	: HistoryItem(history, id, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, MTPnullMarkup);

	setText(textWithEntities);
}

HistoryMessage::HistoryMessage(not_null<History*> history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, UserId viaBotId, QDateTime date, UserId from, const QString &postAuthor, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup)
	: HistoryItem(history, msgId, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, markup);

	initMediaFromDocument(doc, caption);
	setText(TextWithEntities());
}

HistoryMessage::HistoryMessage(not_null<History*> history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, UserId viaBotId, QDateTime date, UserId from, const QString &postAuthor, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup)
	: HistoryItem(history, msgId, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, markup);

	_media = std::make_unique<HistoryPhoto>(this, photo, caption);
	setText(TextWithEntities());
}

HistoryMessage::HistoryMessage(not_null<History*> history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, UserId viaBotId, QDateTime date, UserId from, const QString &postAuthor, GameData *game, const MTPReplyMarkup &markup)
	: HistoryItem(history, msgId, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, postAuthor, markup);

	_media = std::make_unique<HistoryGame>(this, game);
	setText(TextWithEntities());
}

void HistoryMessage::createComponentsHelper(MTPDmessage::Flags flags, MsgId replyTo, UserId viaBotId, const QString &postAuthor, const MTPReplyMarkup &markup) {
	CreateConfig config;

	if (flags & MTPDmessage::Flag::f_via_bot_id) config.viaBotId = viaBotId;
	if (flags & MTPDmessage::Flag::f_reply_to_msg_id) config.replyTo = replyTo;
	if (flags & MTPDmessage::Flag::f_reply_markup) config.mtpMarkup = &markup;
	if (flags & MTPDmessage::Flag::f_post_author) config.author = postAuthor;
	if (isPost()) config.viewsCount = 1;

	createComponents(config);
}

void HistoryMessage::updateMediaInBubbleState() {
	auto mediaHasSomethingBelow = false;
	auto mediaHasSomethingAbove = false;
	auto getMediaHasSomethingAbove = [this] {
		return displayFromName() || displayForwardedFrom() || Has<HistoryMessageReply>() || Has<HistoryMessageVia>();
	};
	auto entry = Get<HistoryMessageLogEntryOriginal>();
	if (entry) {
		mediaHasSomethingBelow = true;
		mediaHasSomethingAbove = getMediaHasSomethingAbove();
		auto entryState = (mediaHasSomethingAbove || !emptyText() || (_media && _media->isDisplayed())) ? MediaInBubbleState::Bottom : MediaInBubbleState::None;
		entry->_page->setInBubbleState(entryState);
	}
	if (!_media) {
		return;
	}

	if (!drawBubble()) {
		_media->setInBubbleState(MediaInBubbleState::None);
		return;
	}

	if (!entry) {
		mediaHasSomethingAbove = getMediaHasSomethingAbove();
	}
	if (!emptyText()) {
		if (_media->isAboveMessage()) {
			mediaHasSomethingBelow = true;
		} else {
			mediaHasSomethingAbove = true;
		}
	}
	auto computeState = [mediaHasSomethingAbove, mediaHasSomethingBelow] {
		if (mediaHasSomethingAbove) {
			if (mediaHasSomethingBelow) {
				return MediaInBubbleState::Middle;
			}
			return MediaInBubbleState::Bottom;
		} else if (mediaHasSomethingBelow) {
			return MediaInBubbleState::Top;
		}
		return MediaInBubbleState::None;
	};
	_media->setInBubbleState(computeState());
}

bool HistoryMessage::displayEditedBadge(bool hasViaBotOrInlineMarkup) const {
	if (hasViaBotOrInlineMarkup) {
		return false;
	} else if (!(_flags & MTPDmessage::Flag::f_edit_date)) {
		return false;
	}
	if (auto fromUser = from()->asUser()) {
		if (fromUser->botInfo) {
			return false;
		}
	}
	return true;
}

bool HistoryMessage::uploading() const {
	return _media && _media->uploading();
}

bool HistoryMessage::displayFastShare() const {
	if (_history->peer->isChannel()) {
		return !_history->peer->isMegagroup();
	} else if (auto user = _history->peer->asUser()) {
		if (user->botInfo && !out()) {
			return _media && _media->allowsFastShare();
		}
	}
	return false;
}

void HistoryMessage::createComponents(const CreateConfig &config) {
	uint64 mask = 0;
	if (config.replyTo) {
		mask |= HistoryMessageReply::Bit();
	}
	if (config.viaBotId) {
		mask |= HistoryMessageVia::Bit();
	}
	if (config.viewsCount >= 0) {
		mask |= HistoryMessageViews::Bit();
	}
	if (!config.author.isEmpty()) {
		mask |= HistoryMessageSigned::Bit();
	}
	auto hasViaBot = (config.viaBotId != 0);
	auto hasInlineMarkup = [&config] {
		if (config.mtpMarkup) {
			return (config.mtpMarkup->type() == mtpc_replyInlineMarkup);
		}
		return (config.inlineMarkup != nullptr);
	};
	if (displayEditedBadge(hasViaBot || hasInlineMarkup())) {
		mask |= HistoryMessageEdited::Bit();
	}
	if (config.senderOriginal) {
		mask |= HistoryMessageForwarded::Bit();
	}
	if (config.mtpMarkup) {
		// optimization: don't create markup component for the case
		// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
		if (config.mtpMarkup->type() != mtpc_replyKeyboardHide || config.mtpMarkup->c_replyKeyboardHide().vflags.v != 0) {
			mask |= HistoryMessageReplyMarkup::Bit();
		}
	} else if (config.inlineMarkup) {
		mask |= HistoryMessageReplyMarkup::Bit();
	}

	UpdateComponents(mask);

	if (auto reply = Get<HistoryMessageReply>()) {
		reply->replyToMsgId = config.replyTo;
		if (!reply->updateData(this)) {
			Auth().api().requestMessageData(history()->peer->asChannel(), reply->replyToMsgId, HistoryDependentItemCallback(fullId()));
		}
	}
	if (auto via = Get<HistoryMessageVia>()) {
		via->create(config.viaBotId);
	}
	if (auto views = Get<HistoryMessageViews>()) {
		views->_views = config.viewsCount;
	}
	if (auto edited = Get<HistoryMessageEdited>()) {
		edited->create(config.editDate, date.toString(cTimeFormat()));
		if (auto msgsigned = Get<HistoryMessageSigned>()) {
			msgsigned->create(config.author, edited->_edited.originalText());
		}
	} else if (auto msgsigned = Get<HistoryMessageSigned>()) {
		msgsigned->create(config.author, date.toString(cTimeFormat()));
	}
	if (auto forwarded = Get<HistoryMessageForwarded>()) {
		forwarded->_originalDate = config.originalDate;
		forwarded->_originalSender = App::peer(config.senderOriginal);
		forwarded->_originalId = config.originalId;
		forwarded->_originalAuthor = config.authorOriginal;
	}
	if (auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (config.mtpMarkup) {
			markup->create(*config.mtpMarkup);
		} else if (config.inlineMarkup) {
			markup->create(*config.inlineMarkup);
		}
		if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_has_switch_inline_button) {
			_flags |= MTPDmessage_ClientFlag::f_has_switch_inline_button;
		}
	}
	initTime();
}

QString formatViewsCount(int32 views) {
	if (views > 999999) {
		views /= 100000;
		if (views % 10) {
			return QString::number(views / 10) + '.' + QString::number(views % 10) + 'M';
		}
		return QString::number(views / 10) + 'M';
	} else if (views > 9999) {
		views /= 100;
		if (views % 10) {
			return QString::number(views / 10) + '.' + QString::number(views % 10) + 'K';
		}
		return QString::number(views / 10) + 'K';
	} else if (views > 0) {
		return QString::number(views);
	}
	return qsl("1");
}

void HistoryMessage::initTime() {
	if (auto msgsigned = Get<HistoryMessageSigned>()) {
		_timeWidth = msgsigned->maxWidth();
	} else if (auto edited = Get<HistoryMessageEdited>()) {
		_timeWidth = edited->maxWidth();
	} else {
		_timeText = date.toString(cTimeFormat());
		_timeWidth = st::msgDateFont->width(_timeText);
	}
	if (auto views = Get<HistoryMessageViews>()) {
		views->_viewsText = (views->_views >= 0) ? formatViewsCount(views->_views) : QString();
		views->_viewsWidth = views->_viewsText.isEmpty() ? 0 : st::msgDateFont->width(views->_viewsText);
	}
}

void HistoryMessage::initMedia(const MTPMessageMedia *media) {
	switch (media ? media->type() : mtpc_messageMediaEmpty) {
	case mtpc_messageMediaContact: {
		auto &d = media->c_messageMediaContact();
		_media = std::make_unique<HistoryContact>(this, d.vuser_id.v, qs(d.vfirst_name), qs(d.vlast_name), qs(d.vphone_number));
	} break;
	case mtpc_messageMediaGeo: {
		auto &point = media->c_messageMediaGeo().vgeo;
		if (point.type() == mtpc_geoPoint) {
			_media = std::make_unique<HistoryLocation>(this, LocationCoords(point.c_geoPoint()));
		}
	} break;
	case mtpc_messageMediaVenue: {
		auto &d = media->c_messageMediaVenue();
		if (d.vgeo.type() == mtpc_geoPoint) {
			_media = std::make_unique<HistoryLocation>(this, LocationCoords(d.vgeo.c_geoPoint()), qs(d.vtitle), qs(d.vaddress));
		}
	} break;
	case mtpc_messageMediaPhoto: {
		auto &photo = media->c_messageMediaPhoto();
		if (photo.has_ttl_seconds()) {
			LOG(("App Error: Unexpected MTPMessageMediaPhoto with ttl_seconds in HistoryMessage."));
		} else if (photo.has_photo() && photo.vphoto.type() == mtpc_photo) {
			_media = std::make_unique<HistoryPhoto>(this, App::feedPhoto(photo.vphoto.c_photo()), photo.has_caption() ? qs(photo.vcaption) : QString());
		} else {
			LOG(("API Error: Got MTPMessageMediaPhoto without photo and without ttl_seconds."));
		}
	} break;
	case mtpc_messageMediaDocument: {
		auto &document = media->c_messageMediaDocument();
		if (document.has_ttl_seconds()) {
			LOG(("App Error: Unexpected MTPMessageMediaDocument with ttl_seconds in HistoryMessage."));
		} else if (document.has_document() && document.vdocument.type() == mtpc_document) {
			return initMediaFromDocument(App::feedDocument(document.vdocument.c_document()), document.has_caption() ? qs(document.vcaption) : QString());
		} else {
			LOG(("API Error: Got MTPMessageMediaDocument without document and without ttl_seconds."));
		}
	} break;
	case mtpc_messageMediaWebPage: {
		auto &d = media->c_messageMediaWebPage().vwebpage;
		switch (d.type()) {
		case mtpc_webPageEmpty: break;
		case mtpc_webPagePending: {
			_media = std::make_unique<HistoryWebPage>(this, App::feedWebPage(d.c_webPagePending()));
		} break;
		case mtpc_webPage: {
			_media = std::make_unique<HistoryWebPage>(this, App::feedWebPage(d.c_webPage()));
		} break;
		case mtpc_webPageNotModified: LOG(("API Error: webPageNotModified is unexpected in message media.")); break;
		}
	} break;
	case mtpc_messageMediaGame: {
		auto &d = media->c_messageMediaGame().vgame;
		if (d.type() == mtpc_game) {
			_media = std::make_unique<HistoryGame>(this, App::feedGame(d.c_game()));
		}
	} break;
	case mtpc_messageMediaInvoice: {
		_media = std::make_unique<HistoryInvoice>(this, media->c_messageMediaInvoice());
		if (static_cast<HistoryInvoice*>(getMedia())->getReceiptMsgId()) {
			replaceBuyWithReceiptInMarkup();
		}
	} break;
	};
}

void HistoryMessage::replaceBuyWithReceiptInMarkup() {
	if (auto markup = inlineReplyMarkup()) {
		for (auto &row : markup->rows) {
			for (auto &button : row) {
				if (button.type == HistoryMessageReplyMarkup::Button::Type::Buy) {
					button.text = lang(lng_payments_receipt_button);
				}
			}
		}
	}
}

void HistoryMessage::initMediaFromDocument(DocumentData *doc, const QString &caption) {
	if (doc->sticker()) {
		_media = std::make_unique<HistorySticker>(this, doc);
	} else if (doc->isAnimation()) {
		_media = std::make_unique<HistoryGif>(this, doc, caption);
	} else if (doc->isVideo()) {
		_media = std::make_unique<HistoryVideo>(this, doc, caption);
	} else {
		_media = std::make_unique<HistoryDocument>(this, doc, caption);
	}
}

int32 HistoryMessage::plainMaxWidth() const {
	return st::msgPadding.left() + _text.maxWidth() + st::msgPadding.right();
}

void HistoryMessage::initDimensions() {
	updateMediaInBubbleState();
	if (drawBubble()) {
		auto forwarded = Get<HistoryMessageForwarded>();
		auto reply = Get<HistoryMessageReply>();
		auto via = Get<HistoryMessageVia>();
		auto entry = Get<HistoryMessageLogEntryOriginal>();
		if (forwarded) {
			forwarded->create(via);
		}
		if (reply) {
			reply->updateName();
		}

		auto mediaDisplayed = false;
		if (_media) {
			mediaDisplayed = _media->isDisplayed();
			_media->initDimensions();
		}
		if (entry) {
			entry->_page->initDimensions();
		}

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && _media->isBubbleBottom()) || (entry/* && entry->_page->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && _media->isBubbleTop()) || (entry && entry->_page->isBubbleTop());

		if (mediaOnBottom) {
			if (_text.hasSkipBlock()) {
				_text.removeSkipBlock();
				_textWidth = -1;
				_textHeight = 0;
			}
		} else if (!_text.hasSkipBlock()) {
			_text.setSkipBlock(skipBlockWidth(), skipBlockHeight());
			_textWidth = -1;
			_textHeight = 0;
		}

		_maxw = plainMaxWidth();
		_minh = emptyText() ? 0 : _text.minHeight();
		if (!mediaOnBottom) {
			_minh += st::msgPadding.bottom();
			if (mediaDisplayed) _minh += st::mediaInBubbleSkip;
		}
		if (!mediaOnTop) {
			_minh += st::msgPadding.top();
			if (mediaDisplayed) _minh += st::mediaInBubbleSkip;
			if (entry) _minh += st::mediaInBubbleSkip;
		}
		if (mediaDisplayed) {
			// Parts don't participate in maxWidth() in case of media message.
			accumulate_max(_maxw, _media->maxWidth());
			_minh += _media->minHeight();
		} else {
			// Count parts in maxWidth(), don't count them in minHeight().
			// They will be added in resizeGetHeight() anyway.
			if (displayFromName()) {
				auto namew = st::msgPadding.left() + author()->nameText.maxWidth() + st::msgPadding.right();
				if (via && !forwarded) {
					namew += st::msgServiceFont->spacew + via->_maxWidth;
				}
				accumulate_max(_maxw, namew);
			} else if (via && !forwarded) {
				accumulate_max(_maxw, st::msgPadding.left() + via->_maxWidth + st::msgPadding.right());
			}
			if (forwarded) {
				auto namew = st::msgPadding.left() + forwarded->_text.maxWidth() + st::msgPadding.right();
				if (via) {
					namew += st::msgServiceFont->spacew + via->_maxWidth;
				}
				accumulate_max(_maxw, namew);
			}
			if (reply) {
				auto replyw = st::msgPadding.left() + reply->_maxReplyWidth - st::msgReplyPadding.left() - st::msgReplyPadding.right() + st::msgPadding.right();
				if (reply->_replyToVia) {
					replyw += st::msgServiceFont->spacew + reply->_replyToVia->_maxWidth;
				}
				accumulate_max(_maxw, replyw);
			}
			if (entry) {
				accumulate_max(_maxw, entry->_page->maxWidth());
				_minh += entry->_page->minHeight();
			}
		}
	} else if (_media) {
		_media->initDimensions();
		_maxw = _media->maxWidth();
		_minh = _media->minHeight();
	} else {
		_maxw = st::msgMinWidth;
		_minh = 0;
	}
	if (auto markup = inlineReplyMarkup()) {
		if (!markup->inlineKeyboard) {
			markup->inlineKeyboard = std::make_unique<ReplyKeyboard>(this, std::make_unique<KeyboardStyle>(st::msgBotKbButton));
		}

		// if we have a text bubble we can resize it to fit the keyboard
		// but if we have only media we don't do that
		if (!emptyText()) {
			accumulate_max(_maxw, markup->inlineKeyboard->naturalWidth());
		}
	}
}

bool HistoryMessage::drawBubble() const {
	if (Has<HistoryMessageLogEntryOriginal>()) {
		return true;
	}
	return _media ? (!emptyText() || _media->needsBubble()) : !isEmpty();
}

QRect HistoryMessage::countGeometry() const {
	auto maxwidth = qMin(st::msgMaxWidth, _maxw);
	if (_media && _media->currentWidth() < maxwidth) {
		maxwidth = qMax(_media->currentWidth(), qMin(maxwidth, plainMaxWidth()));
	}

	auto contentLeft = (!isPost() && out() && !Adaptive::ChatWide()) ? st::msgMargin.right() : st::msgMargin.left();
	if (hasFromPhoto()) {
		contentLeft += st::msgPhotoSkip;
//	} else if (!Adaptive::Wide() && !out() && !fromChannel() && st::msgPhotoSkip - (hmaxwidth - hwidth) > 0) {
//		contentLeft += st::msgPhotoSkip - (hmaxwidth - hwidth);
	}

	auto contentWidth = width() - st::msgMargin.left() - st::msgMargin.right();
	if (contentWidth > maxwidth) {
		if (!isPost() && out() && !Adaptive::ChatWide()) {
			contentLeft += contentWidth - maxwidth;
		}
		contentWidth = maxwidth;
	}

	auto contentTop = marginTop();
	return QRect(contentLeft, contentTop, contentWidth, _height - contentTop - marginBottom());
}

void HistoryMessage::fromNameUpdated(int32 width) const {
	_authorNameVersion = author()->nameVersion;
	if (!Has<HistoryMessageForwarded>()) {
		if (auto via = Get<HistoryMessageVia>()) {
			via->resize(width - st::msgPadding.left() - st::msgPadding.right() - author()->nameText.maxWidth() - st::msgServiceFont->spacew);
		}
	}
}

void HistoryMessage::applyEdition(const MTPDmessage &message) {
	int keyboardTop = -1;
	if (!pendingResize()) {
		if (auto keyboard = inlineReplyKeyboard()) {
			int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
			keyboardTop = _height - h + st::msgBotKbButton.margin - marginBottom();
		}
	}

	if (message.has_edit_date()) {
		_flags |= MTPDmessage::Flag::f_edit_date;
		auto hasViaBotId = Has<HistoryMessageVia>();
		auto hasInlineMarkup = (inlineReplyMarkup() != nullptr);
		if (displayEditedBadge(hasViaBotId || hasInlineMarkup)) {
			if (!Has<HistoryMessageEdited>()) {
				AddComponents(HistoryMessageEdited::Bit());
			}
			auto edited = Get<HistoryMessageEdited>();
			edited->create(::date(message.vedit_date), date.toString(cTimeFormat()));
			if (auto msgsigned = Get<HistoryMessageSigned>()) {
				msgsigned->create(msgsigned->_author, edited->_edited.originalText());
			}
		} else if (Has<HistoryMessageEdited>()) {
			RemoveComponents(HistoryMessageEdited::Bit());
			if (auto msgsigned = Get<HistoryMessageSigned>()) {
				msgsigned->create(msgsigned->_author, date.toString(cTimeFormat()));
			}
		}
		initTime();
	}

	TextWithEntities textWithEntities = { qs(message.vmessage), EntitiesInText() };
	if (message.has_entities()) {
		textWithEntities.entities = TextUtilities::EntitiesFromMTP(message.ventities.v);
	}
	setText(textWithEntities);
	setReplyMarkup(message.has_reply_markup() ? (&message.vreply_markup) : nullptr);
	setMedia(message.has_media() ? (&message.vmedia) : nullptr);
	setViewsCount(message.has_views() ? message.vviews.v : -1);

	finishEdition(keyboardTop);
}

void HistoryMessage::applyEdition(const MTPDmessageService &message) {
	if (message.vaction.type() == mtpc_messageActionHistoryClear) {
		applyEditionToEmpty();
	}
}

void HistoryMessage::applyEditionToEmpty() {
	setEmptyText();
	setMedia(nullptr);
	setReplyMarkup(nullptr);
	setViewsCount(-1);

	finishEditionToEmpty();
}

bool HistoryMessage::displayForwardedFrom() const {
	if (auto forwarded = Get<HistoryMessageForwarded>()) {
		return Has<HistoryMessageVia>()
			|| !_media
			|| !_media->isDisplayed()
			|| !_media->hideForwardedFrom()
			|| forwarded->_originalSender->isChannel();
	}
	return false;
}

void HistoryMessage::updateMedia(const MTPMessageMedia *media) {
	auto setMediaAllowed = [](HistoryMediaType type) {
		return (type == MediaTypeWebPage || type == MediaTypeGame || type == MediaTypeLocation);
	};
	if (_flags & MTPDmessage_ClientFlag::f_from_inline_bot) {
		bool needReSet = true;
		if (media && _media) {
			needReSet = _media->needReSetInlineResultMedia(*media);
		}
		if (needReSet) {
			setMedia(media);
		}
		_flags &= ~MTPDmessage_ClientFlag::f_from_inline_bot;
	} else if (media && _media && !setMediaAllowed(_media->type())) {
		_media->updateSentMedia(*media);
	} else {
		setMedia(media);
	}
	setPendingInitDimensions();
}

int32 HistoryMessage::addToOverview(AddToOverviewMethod method) {
	if (!indexInOverview()) return 0;

	int32 result = 0;
	if (auto media = getMedia()) {
		result |= media->addToOverview(method);
	}
	if (hasTextLinks()) {
		if (history()->addToOverview(OverviewLinks, id, method)) {
			result |= (1 << OverviewLinks);
		}
	}
	if (mentionsMe() && isMediaUnread()) {
		if (history()->addToUnreadMentions(id, method)) {
			Notify::peerUpdatedDelayed(history()->peer, Notify::PeerUpdate::Flag::UnreadMentionsChanged);
		}
	}
	return result;
}

void HistoryMessage::eraseFromOverview() {
	if (auto media = getMedia()) {
		media->eraseFromOverview();
	}
	if (hasTextLinks()) {
		history()->eraseFromOverview(OverviewLinks, id);
	}
	if (mentionsMe() && isMediaUnread()) {
		history()->eraseFromUnreadMentions(id);
	}
}

TextWithEntities HistoryMessage::selectedText(TextSelection selection) const {
	TextWithEntities logEntryOriginalResult;
	auto textResult = _text.originalTextWithEntities((selection == FullSelection) ? AllTextSelection : selection, ExpandLinksAll);
	auto skipped = skipTextSelection(selection);
	auto mediaDisplayed = (_media && _media->isDisplayed());
	auto mediaResult = mediaDisplayed ? _media->selectedText(skipped) : TextWithEntities();
	if (auto entry = Get<HistoryMessageLogEntryOriginal>()) {
		logEntryOriginalResult = entry->_page->selectedText(mediaDisplayed ? _media->skipSelection(skipped) : skipped);
	}
	auto result = textResult;
	if (result.text.isEmpty()) {
		result = std::move(mediaResult);
	} else if (!mediaResult.text.isEmpty()) {
		result.text += qstr("\n\n");
		TextUtilities::Append(result, std::move(mediaResult));
	}
	if (result.text.isEmpty()) {
		result = std::move(logEntryOriginalResult);
	} else if (!logEntryOriginalResult.text.isEmpty()) {
		result.text += qstr("\n\n");
		TextUtilities::Append(result, std::move(logEntryOriginalResult));
	}
	if (auto reply = Get<HistoryMessageReply>()) {
		if (selection == FullSelection && reply->replyToMsg) {
			TextWithEntities wrapped;
			wrapped.text.reserve(lang(lng_in_reply_to).size() + reply->replyToMsg->author()->name.size() + 4 + result.text.size());
			wrapped.text.append('[').append(lang(lng_in_reply_to)).append(' ').append(reply->replyToMsg->author()->name).append(qsl("]\n"));
			TextUtilities::Append(wrapped, std::move(result));
			result = wrapped;
		}
	}
	if (auto forwarded = Get<HistoryMessageForwarded>()) {
		if (selection == FullSelection) {
			auto fwdinfo = forwarded->_text.originalTextWithEntities(AllTextSelection, ExpandLinksAll);
			auto wrapped = TextWithEntities();
			wrapped.text.reserve(fwdinfo.text.size() + 4 + result.text.size());
			wrapped.entities.reserve(fwdinfo.entities.size() + result.entities.size());
			wrapped.text.append('[');
			TextUtilities::Append(wrapped, std::move(fwdinfo));
			wrapped.text.append(qsl("]\n"));
			TextUtilities::Append(wrapped, std::move(result));
			result = wrapped;
		}
	}
	return result;
}

void HistoryMessage::setMedia(const MTPMessageMedia *media) {
	if (!_media && (!media || media->type() == mtpc_messageMediaEmpty)) return;

	bool mediaRemovedSkipBlock = false;
	if (_media) {
		// Don't update Game media because we loose the consumed text of the message.
		if (_media->type() == MediaTypeGame) return;

		mediaRemovedSkipBlock = _media->isDisplayed() && _media->isBubbleBottom();
		_media.reset();
	}
	initMedia(media);
	auto mediaDisplayed = _media && _media->isDisplayed();
	if (mediaDisplayed && _media->isBubbleBottom() && !mediaRemovedSkipBlock) {
		_text.removeSkipBlock();
		_textWidth = -1;
		_textHeight = 0;
	} else if (mediaRemovedSkipBlock && (!mediaDisplayed || !_media->isBubbleBottom())) {
		_text.setSkipBlock(skipBlockWidth(), skipBlockHeight());
		_textWidth = -1;
		_textHeight = 0;
	}
}

void HistoryMessage::setText(const TextWithEntities &textWithEntities) {
	for_const (auto &entity, textWithEntities.entities) {
		auto type = entity.type();
		if (type == EntityInTextUrl || type == EntityInTextCustomUrl || type == EntityInTextEmail) {
			_flags |= MTPDmessage_ClientFlag::f_has_text_links;
			break;
		}
	}

	auto mediaDisplayed = _media && _media->isDisplayed();
	if (mediaDisplayed && _media->consumeMessageText(textWithEntities)) {
		setEmptyText();
	} else {
		auto mediaOnBottom = (_media && _media->isDisplayed() && _media->isBubbleBottom()) || Has<HistoryMessageLogEntryOriginal>();
		if (mediaOnBottom) {
			_text.setMarkedText(st::messageTextStyle, textWithEntities, itemTextOptions(this));
		} else {
			_text.setMarkedText(st::messageTextStyle, { textWithEntities.text + skipBlock(), textWithEntities.entities }, itemTextOptions(this));
		}
		_textWidth = -1;
		_textHeight = 0;
	}
}

void HistoryMessage::setEmptyText() {
	_text.setMarkedText(st::messageTextStyle, { QString(), EntitiesInText() }, itemTextOptions(this));

	_textWidth = -1;
	_textHeight = 0;
}

void HistoryMessage::setReplyMarkup(const MTPReplyMarkup *markup) {
	if (!markup) {
		if (_flags & MTPDmessage::Flag::f_reply_markup) {
			_flags &= ~MTPDmessage::Flag::f_reply_markup;
			if (Has<HistoryMessageReplyMarkup>()) {
				RemoveComponents(HistoryMessageReplyMarkup::Bit());
			}
			setPendingInitDimensions();
			Notify::replyMarkupUpdated(this);
		}
		return;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	if (markup->type() == mtpc_replyKeyboardHide && markup->c_replyKeyboardHide().vflags.v == 0) {
		bool changed = false;
		if (Has<HistoryMessageReplyMarkup>()) {
			RemoveComponents(HistoryMessageReplyMarkup::Bit());
			changed = true;
		}
		if (!(_flags & MTPDmessage::Flag::f_reply_markup)) {
			_flags |= MTPDmessage::Flag::f_reply_markup;
			changed = true;
		}
		if (changed) {
			setPendingInitDimensions();

			Notify::replyMarkupUpdated(this);
		}
	} else {
		if (!(_flags & MTPDmessage::Flag::f_reply_markup)) {
			_flags |= MTPDmessage::Flag::f_reply_markup;
		}
		if (!Has<HistoryMessageReplyMarkup>()) {
			AddComponents(HistoryMessageReplyMarkup::Bit());
		}
		Get<HistoryMessageReplyMarkup>()->create(*markup);
		setPendingInitDimensions();

		Notify::replyMarkupUpdated(this);
	}
}

TextWithEntities HistoryMessage::originalText() const {
	if (emptyText()) {
		return { QString(), EntitiesInText() };
	}
	return _text.originalTextWithEntities();
}

bool HistoryMessage::textHasLinks() const {
	return emptyText() ? false : _text.hasLinks();
}

int HistoryMessage::infoWidth() const {
	int result = _timeWidth;
	if (auto views = Get<HistoryMessageViews>()) {
		result += st::historyViewsSpace + views->_viewsWidth + st::historyViewsWidth;
	} else if (id < 0 && history()->peer->isSelf()) {
		result += st::historySendStateSpace;
	}
	if (out() && !isPost()) {
		result += st::historySendStateSpace;
	}
	return result;
}
int HistoryMessage::timeLeft() const {
	int result = 0;
	if (auto views = Get<HistoryMessageViews>()) {
		result += st::historyViewsSpace + views->_viewsWidth + st::historyViewsWidth;
	} else if (id < 0 && history()->peer->isSelf()) {
		result += st::historySendStateSpace;
	}
	return result;
}

void HistoryMessage::drawInfo(Painter &p, int32 right, int32 bottom, int32 width, bool selected, InfoDisplayType type) const {
	p.setFont(st::msgDateFont);

	bool outbg = out() && !isPost();
	bool invertedsprites = (type == InfoDisplayOverImage || type == InfoDisplayOverBackground);
	int32 infoRight = right, infoBottom = bottom;
	switch (type) {
	case InfoDisplayDefault:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
		p.setPen(selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg));
	break;
	case InfoDisplayOverImage:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st::msgDateImgFg);
	break;
	case InfoDisplayOverBackground:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st::msgServiceFg);
	break;
	}

	int32 infoW = HistoryMessage::infoWidth();
	if (rtl()) infoRight = width - infoRight + infoW;

	int32 dateX = infoRight - infoW;
	int32 dateY = infoBottom - st::msgDateFont->height;
	if (type == InfoDisplayOverImage) {
		int32 dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		App::roundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	} else if (type == InfoDisplayOverBackground) {
		int32 dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		App::roundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
	}
	dateX += HistoryMessage::timeLeft();

	if (auto msgsigned = Get<HistoryMessageSigned>()) {
		msgsigned->_signature.drawElided(p, dateX, dateY, _timeWidth);
	} else if (auto edited = Get<HistoryMessageEdited>()) {
		edited->_edited.drawElided(p, dateX, dateY, _timeWidth);
	} else {
		p.drawText(dateX, dateY + st::msgDateFont->ascent, _timeText);
	}

	if (auto views = Get<HistoryMessageViews>()) {
		auto icon = ([this, outbg, invertedsprites, selected] {
			if (id > 0) {
				if (outbg) {
					return &(invertedsprites ? st::historyViewsInvertedIcon : (selected ? st::historyViewsOutSelectedIcon : st::historyViewsOutIcon));
				}
				return &(invertedsprites ? st::historyViewsInvertedIcon : (selected ? st::historyViewsInSelectedIcon : st::historyViewsInIcon));
			}
			return &(invertedsprites ? st::historyViewsSendingInvertedIcon : st::historyViewsSendingIcon);
		})();
		if (id > 0) {
			icon->paint(p, infoRight - infoW, infoBottom + st::historyViewsTop, width);
			p.drawText(infoRight - infoW + st::historyViewsWidth, infoBottom - st::msgDateFont->descent, views->_viewsText);
		} else if (!outbg) { // sending outbg icon will be painted below
			auto iconSkip = st::historyViewsSpace + views->_viewsWidth;
			icon->paint(p, infoRight - infoW + iconSkip, infoBottom + st::historyViewsTop, width);
		}
	} else if (id < 0 && history()->peer->isSelf()) {
		auto icon = &(invertedsprites ? st::historyViewsSendingInvertedIcon : st::historyViewsSendingIcon);
		icon->paint(p, infoRight - infoW, infoBottom + st::historyViewsTop, width);
	}
	if (outbg) {
		auto icon = ([this, invertedsprites, selected] {
			if (id > 0) {
				if (unread()) {
					return &(invertedsprites ? st::historySentInvertedIcon : (selected ? st::historySentSelectedIcon : st::historySentIcon));
				}
				return &(invertedsprites ? st::historyReceivedInvertedIcon : (selected ? st::historyReceivedSelectedIcon : st::historyReceivedIcon));
			}
			return &(invertedsprites ? st::historySendingInvertedIcon : st::historySendingIcon);
		})();
		icon->paint(p, QPoint(infoRight, infoBottom) + st::historySendStatePosition, width);
	}
}

void HistoryMessage::setViewsCount(int32 count) {
	auto views = Get<HistoryMessageViews>();
	if (!views || views->_views == count || (count >= 0 && views->_views > count)) return;

	int32 was = views->_viewsWidth;
	views->_views = count;
	views->_viewsText = (views->_views >= 0) ? formatViewsCount(views->_views) : QString();
	views->_viewsWidth = views->_viewsText.isEmpty() ? 0 : st::msgDateFont->width(views->_viewsText);
	if (was == views->_viewsWidth) {
		Ui::repaintHistoryItem(this);
	} else {
		if (_text.hasSkipBlock()) {
			_text.setSkipBlock(HistoryMessage::skipBlockWidth(), HistoryMessage::skipBlockHeight());
			_textWidth = -1;
			_textHeight = 0;
		}
		setPendingInitDimensions();
	}
}

void HistoryMessage::setId(MsgId newId) {
	bool wasPositive = (id > 0), positive = (newId > 0);
	HistoryItem::setId(newId);
	if (wasPositive == positive) {
		Ui::repaintHistoryItem(this);
	} else {
		if (_text.hasSkipBlock()) {
			_text.setSkipBlock(HistoryMessage::skipBlockWidth(), HistoryMessage::skipBlockHeight());
			_textWidth = -1;
			_textHeight = 0;
		}
		setPendingInitDimensions();
	}
}

void HistoryMessage::draw(Painter &p, QRect clip, TextSelection selection, TimeMs ms) const {
	bool outbg = out() && !isPost(), bubble = drawBubble(), selected = (selection == FullSelection);

	auto g = countGeometry();
	if (g.width() < 1) {
		return;
	}

	auto dateh = 0;
	if (auto date = Get<HistoryMessageDate>()) {
		dateh = date->height();
	}
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		auto unreadbarh = unreadbar->height();
		if (clip.intersects(QRect(0, dateh, width(), unreadbarh))) {
			p.translate(0, dateh);
			unreadbar->paint(p, 0, width());
			p.translate(0, -dateh);
		}
	}

	auto fullAnimMs = App::main() ? App::main()->highlightStartTime(this) : 0LL;
	if (fullAnimMs > 0 && fullAnimMs <= ms) {
		auto animms = ms - fullAnimMs;
		if (animms < st::activeFadeInDuration + st::activeFadeOutDuration) {
			auto top = marginTop();
			auto bottom = marginBottom();
			auto fill = qMin(top, bottom);
			auto skiptop = top - fill;
			auto fillheight = fill + g.height() + fill;

			auto dt = (animms > st::activeFadeInDuration) ? (1. - (animms - st::activeFadeInDuration) / float64(st::activeFadeOutDuration)) : (animms / float64(st::activeFadeInDuration));
			auto o = p.opacity();
			p.setOpacity(o * dt);
			p.fillRect(0, skiptop, width(), fillheight, st::defaultTextPalette.selectOverlay);
			p.setOpacity(o);
		}
	}

	p.setTextPalette(selected ? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected) : (outbg ? st::outTextPalette : st::inTextPalette));

	auto keyboard = inlineReplyKeyboard();
	if (keyboard) {
		auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		g.setHeight(g.height() - keyboardHeight);
		auto keyboardPosition = QPoint(g.left(), g.top() + g.height() + st::msgBotKbButton.margin);
		p.translate(keyboardPosition);
		keyboard->paint(p, g.width(), clip.translated(-keyboardPosition), ms);
		p.translate(-keyboardPosition);
	}

	if (bubble) {
		if (displayFromName() && author()->nameVersion > _authorNameVersion) {
			fromNameUpdated(g.width());
		}

		auto entry = Get<HistoryMessageLogEntryOriginal>();
		auto mediaDisplayed = _media && _media->isDisplayed();

		auto skipTail = isAttachedToNext() || (_media && _media->skipBubbleTail()) || (keyboard != nullptr);
		auto displayTail = skipTail ? RectPart::None : (outbg && !Adaptive::ChatWide()) ? RectPart::Right : RectPart::Left;
		HistoryLayout::paintBubble(p, g, width(), selected, outbg, displayTail);

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && _media->isBubbleBottom()) || (entry/* && entry->_page->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && _media->isBubbleTop()) || (entry && entry->_page->isBubbleTop());

		auto trect = g.marginsRemoved(st::msgPadding);
		if (mediaOnBottom) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}
		if (mediaOnTop) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			paintFromName(p, trect, selected);
			paintForwardedInfo(p, trect, selected);
			paintReplyInfo(p, trect, selected);
			paintViaBotIdInfo(p, trect, selected);
		}
		if (entry) {
			trect.setHeight(trect.height() - entry->_page->height());
		}
		auto needDrawInfo = mediaOnBottom ? !(entry ? entry->_page->customInfoLayout() : _media->customInfoLayout()) : true;
		if (mediaDisplayed) {
			auto mediaAboveText = _media->isAboveMessage();
			auto mediaHeight = _media->height();
			auto mediaLeft = g.left();
			auto mediaTop = mediaAboveText ? trect.y() : (trect.y() + trect.height() - mediaHeight);
			if (!mediaAboveText) {
				paintText(p, trect, selection);
			}
			p.translate(mediaLeft, mediaTop);
			_media->draw(p, clip.translated(-mediaLeft, -mediaTop), skipTextSelection(selection), ms);
			p.translate(-mediaLeft, -mediaTop);

			if (mediaAboveText) {
				trect.setY(trect.y() + mediaHeight);
				paintText(p, trect, selection);
			} else {
				needDrawInfo = !_media->customInfoLayout();
			}
		} else {
			paintText(p, trect, selection);
		}
		if (entry) {
			auto entryLeft = g.left();
			auto entryTop = trect.y() + trect.height();
			p.translate(entryLeft, entryTop);
			auto entrySelection = skipTextSelection(selection);
			if (mediaDisplayed) {
				entrySelection = _media->skipSelection(entrySelection);
			}
			entry->_page->draw(p, clip.translated(-entryLeft, -entryTop), entrySelection, ms);
			p.translate(-entryLeft, -entryTop);
		}
		if (needDrawInfo) {
			HistoryMessage::drawInfo(p, g.left() + g.width(), g.top() + g.height(), 2 * g.left() + g.width(), selected, InfoDisplayDefault);
		}
		if (displayFastShare()) {
			auto fastShareLeft = g.left() + g.width() + st::historyFastShareLeft;
			auto fastShareTop = g.top() + g.height() - st::historyFastShareBottom - st::historyFastShareSize;
			drawFastShare(p, fastShareLeft, fastShareTop, width());
		}
	} else if (_media) {
		p.translate(g.topLeft());
		_media->draw(p, clip.translated(-g.topLeft()), skipTextSelection(selection), ms);
		p.translate(-g.topLeft());
	}

	p.restoreTextPalette();

	auto reply = Get<HistoryMessageReply>();
	if (reply && reply->isNameUpdated()) {
		const_cast<HistoryMessage*>(this)->setPendingInitDimensions();
	}
}

void HistoryMessage::drawFastShare(Painter &p, int left, int top, int outerWidth) const {
	{
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgServiceBg);

		PainterHighQualityEnabler hq(p);
		p.drawEllipse(rtlrect(left, top, st::historyFastShareSize, st::historyFastShareSize, outerWidth));
	}
	st::historyFastShareIcon.paint(p, left, top, outerWidth);
}

void HistoryMessage::paintFromName(Painter &p, QRect &trect, bool selected) const {
	if (displayFromName()) {
		p.setFont(st::msgNameFont);
		if (isPost()) {
			p.setPen(selected ? st::msgInServiceFgSelected : st::msgInServiceFg);
		} else {
			p.setPen(selected ? fromNameFgSelected(author()->colorIndex()) : fromNameFg(author()->colorIndex()));
		}
		author()->nameText.drawElided(p, trect.left(), trect.top(), trect.width());

		auto forwarded = Get<HistoryMessageForwarded>();
		auto via = Get<HistoryMessageVia>();
		if (via && !forwarded && trect.width() > author()->nameText.maxWidth() + st::msgServiceFont->spacew) {
			bool outbg = out() && !isPost();
			p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
			p.drawText(trect.left() + author()->nameText.maxWidth() + st::msgServiceFont->spacew, trect.top() + st::msgServiceFont->ascent, via->_text);
		}
		trect.setY(trect.y() + st::msgNameFont->height);
	}
}

void HistoryMessage::paintForwardedInfo(Painter &p, QRect &trect, bool selected) const {
	if (displayForwardedFrom()) {
		style::font serviceFont(st::msgServiceFont), serviceName(st::msgServiceNameFont);

		auto outbg = hasOutLayout();
		p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
		p.setFont(serviceFont);

		auto forwarded = Get<HistoryMessageForwarded>();
		auto breakEverywhere = (forwarded->_text.countHeight(trect.width()) > 2 * serviceFont->height);
		p.setTextPalette(selected ? (outbg ? st::outFwdTextPaletteSelected : st::inFwdTextPaletteSelected) : (outbg ? st::outFwdTextPalette : st::inFwdTextPalette));
		forwarded->_text.drawElided(p, trect.x(), trect.y(), trect.width(), 2, style::al_left, 0, -1, 0, breakEverywhere);
		p.setTextPalette(selected ? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected) : (outbg ? st::outTextPalette : st::inTextPalette));

		trect.setY(trect.y() + (((forwarded->_text.maxWidth() > trect.width()) ? 2 : 1) * serviceFont->height));
	}
}

void HistoryMessage::paintReplyInfo(Painter &p, QRect &trect, bool selected) const {
	if (auto reply = Get<HistoryMessageReply>()) {
		int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();

		auto flags = HistoryMessageReply::PaintFlag::InBubble | 0;
		if (selected) {
			flags |= HistoryMessageReply::PaintFlag::Selected;
		}
		reply->paint(p, this, trect.x(), trect.y(), trect.width(), flags);

		trect.setY(trect.y() + h);
	}
}

void HistoryMessage::paintViaBotIdInfo(Painter &p, QRect &trect, bool selected) const {
	if (!displayFromName() && !Has<HistoryMessageForwarded>()) {
		if (auto via = Get<HistoryMessageVia>()) {
			p.setFont(st::msgServiceNameFont);
			p.setPen(selected ? (hasOutLayout() ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (hasOutLayout() ? st::msgOutServiceFg : st::msgInServiceFg));
			p.drawTextLeft(trect.left(), trect.top(), width(), via->_text);
			trect.setY(trect.y() + st::msgServiceNameFont->height);
		}
	}
}

void HistoryMessage::paintText(Painter &p, QRect &trect, TextSelection selection) const {
	auto outbg = out() && !isPost();
	auto selected = (selection == FullSelection);
	p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
	p.setFont(st::msgFont);
	_text.draw(p, trect.x(), trect.y(), trect.width(), style::al_left, 0, -1, selection);
}

void HistoryMessage::dependencyItemRemoved(HistoryItem *dependency) {
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->itemRemoved(this, dependency);
	}
}

int HistoryMessage::resizeContentGetHeight() {
	int result = performResizeGetHeight();

	auto keyboard = inlineReplyKeyboard();
	if (auto markup = Get<HistoryMessageReplyMarkup>()) {
		int oldTop = markup->oldTop;
		if (oldTop >= 0) {
			markup->oldTop = -1;
			if (keyboard) {
				int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
				int keyboardTop = _height - h + st::msgBotKbButton.margin - marginBottom();
				if (keyboardTop != oldTop) {
					Notify::inlineKeyboardMoved(this, oldTop, keyboardTop);
				}
			}
		}
	}

	return result;
}

int HistoryMessage::performResizeGetHeight() {
	if (width() < st::msgMinWidth) return _height;

	auto contentWidth = width() - (st::msgMargin.left() + st::msgMargin.right());
	if (contentWidth < st::msgPadding.left() + st::msgPadding.right() + 1) {
		contentWidth = st::msgPadding.left() + st::msgPadding.right() + 1;
	} else if (contentWidth > st::msgMaxWidth) {
		contentWidth = st::msgMaxWidth;
	}
	if (drawBubble()) {
		auto forwarded = Get<HistoryMessageForwarded>();
		auto reply = Get<HistoryMessageReply>();
		auto via = Get<HistoryMessageVia>();
		auto entry = Get<HistoryMessageLogEntryOriginal>();

		auto mediaDisplayed = false;
		if (_media) {
			mediaDisplayed = _media->isDisplayed();
		}

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && _media->isBubbleBottom()) || (entry/* && entry->_page->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && _media->isBubbleTop()) || (entry && entry->_page->isBubbleTop());

		if (contentWidth >= _maxw) {
			_height = _minh;
			if (mediaDisplayed) {
				_media->resizeGetHeight(_maxw);
				if (entry) {
					_height += entry->_page->resizeGetHeight(countGeometry().width());
				}
			} else if (entry) {
				// In case of text-only message it is counted in _minh already.
				entry->_page->resizeGetHeight(countGeometry().width());
			}
		} else {
			if (emptyText()) {
				_height = 0;
			} else {
				auto textWidth = qMax(contentWidth - st::msgPadding.left() - st::msgPadding.right(), 1);
				if (textWidth != _textWidth) {
					_textWidth = textWidth;
					_textHeight = _text.countHeight(textWidth);
				}
				_height = _textHeight;
			}
			if (!mediaOnBottom) {
				_height += st::msgPadding.bottom();
				if (mediaDisplayed) _height += st::mediaInBubbleSkip;
			}
			if (!mediaOnTop) {
				_height += st::msgPadding.top();
				if (mediaDisplayed) _height += st::mediaInBubbleSkip;
				if (entry) _height += st::mediaInBubbleSkip;
			}
			if (mediaDisplayed) {
				_height += _media->resizeGetHeight(contentWidth);
				if (entry) {
					_height += entry->_page->resizeGetHeight(countGeometry().width());
				}
			} else if (entry) {
				_height += entry->_page->resizeGetHeight(contentWidth);
			}
		}

		if (displayFromName()) {
			fromNameUpdated(countGeometry().width());
			_height += st::msgNameFont->height;
		} else if (via && !forwarded) {
			via->resize(countGeometry().width() - st::msgPadding.left() - st::msgPadding.right());
			_height += st::msgNameFont->height;
		}

		if (displayForwardedFrom()) {
			auto fwdheight = ((forwarded->_text.maxWidth() > (countGeometry().width() - st::msgPadding.left() - st::msgPadding.right())) ? 2 : 1) * st::semiboldFont->height;
			_height += fwdheight;
		}

		if (reply) {
			reply->resize(countGeometry().width() - st::msgPadding.left() - st::msgPadding.right());
			_height += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		}
	} else if (_media) {
		_height = _media->resizeGetHeight(contentWidth);
	} else {
		_height = 0;
	}
	if (auto keyboard = inlineReplyKeyboard()) {
		auto g = countGeometry();
		auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		_height += keyboardHeight;
		keyboard->resize(g.width(), keyboardHeight - st::msgBotKbButton.margin);
	}

	_height += marginTop() + marginBottom();
	return _height;
}

bool HistoryMessage::hasPoint(QPoint point) const {
	auto g = countGeometry();
	if (g.width() < 1) {
		return false;
	}

	if (drawBubble()) {
		return g.contains(point);
	} else if (_media) {
		return _media->hasPoint(point - g.topLeft());
	} else {
		return false;
	}
}

bool HistoryMessage::pointInTime(int right, int bottom, QPoint point, InfoDisplayType type) const {
	auto infoRight = right;
	auto infoBottom = bottom;
	switch (type) {
	case InfoDisplayDefault:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
	break;
	case InfoDisplayOverImage:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
	break;
	}
	auto dateX = infoRight - HistoryMessage::infoWidth() + HistoryMessage::timeLeft();
	auto dateY = infoBottom - st::msgDateFont->height;
	return QRect(dateX, dateY, HistoryMessage::timeWidth(), st::msgDateFont->height).contains(point);
}

HistoryTextState HistoryMessage::getState(QPoint point, HistoryStateRequest request) const {
	HistoryTextState result;

	auto g = countGeometry();
	if (g.width() < 1) {
		return result;
	}

	auto keyboard = inlineReplyKeyboard();
	auto keyboardHeight = 0;
	if (keyboard) {
		keyboardHeight = keyboard->naturalHeight();
		g.setHeight(g.height() - st::msgBotKbButton.margin - keyboardHeight);
	}

	if (drawBubble()) {
		auto entry = Get<HistoryMessageLogEntryOriginal>();
		auto mediaDisplayed = _media && _media->isDisplayed();

		// Entry page is always a bubble bottom.
		auto mediaOnBottom = (mediaDisplayed && _media->isBubbleBottom()) || (entry/* && entry->_page->isBubbleBottom()*/);
		auto mediaOnTop = (mediaDisplayed && _media->isBubbleTop()) || (entry && entry->_page->isBubbleTop());

		auto trect = g.marginsRemoved(st::msgPadding);
		if (mediaOnBottom) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}
		if (mediaOnTop) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			if (getStateFromName(point, trect, &result)) return result;
			if (getStateForwardedInfo(point, trect, &result, request)) return result;
			if (getStateReplyInfo(point, trect, &result)) return result;
			if (getStateViaBotIdInfo(point, trect, &result)) return result;
		}
		if (entry) {
			auto entryHeight = entry->_page->height();
			trect.setHeight(trect.height() - entryHeight);
			auto entryLeft = g.left();
			auto entryTop = trect.y() + trect.height();
			if (point.y() >= entryTop && point.y() < entryTop + entryHeight) {
				result = entry->_page->getState(point - QPoint(entryLeft, entryTop), request);
				result.symbol += _text.length() + (mediaDisplayed ? _media->fullSelectionLength() : 0);
			}
		}

		auto needDateCheck = mediaOnBottom ? !(entry ? entry->_page->customInfoLayout() : _media->customInfoLayout()) : true;
		if (mediaDisplayed) {
			auto mediaAboveText = _media->isAboveMessage();
			auto mediaHeight = _media->height();
			auto mediaLeft = trect.x() - st::msgPadding.left();
			auto mediaTop = mediaAboveText ? trect.y() : (trect.y() + trect.height() - mediaHeight);

			if (point.y() >= mediaTop && point.y() < mediaTop + mediaHeight) {
				result = _media->getState(point - QPoint(mediaLeft, mediaTop), request);
				result.symbol += _text.length();
			} else {
				if (mediaAboveText) {
					trect.setY(trect.y() + mediaHeight);
				}
				if (trect.contains(point)) {
					getStateText(point, trect, &result, request);
				}
			}
		} else if (trect.contains(point)) {
			getStateText(point, trect, &result, request);
		}
		if (needDateCheck) {
			if (HistoryMessage::pointInTime(g.left() + g.width(), g.top() + g.height(), point, InfoDisplayDefault)) {
				result.cursor = HistoryInDateCursorState;
			}
		}
		if (displayFastShare()) {
			auto fastShareLeft = g.left() + g.width() + st::historyFastShareLeft;
			auto fastShareTop = g.top() + g.height() - st::historyFastShareBottom - st::historyFastShareSize;
			if (QRect(fastShareLeft, fastShareTop, st::historyFastShareSize, st::historyFastShareSize).contains(point)) {
				result.link = fastShareLink();
			}
		}
	} else if (_media) {
		result = _media->getState(point - g.topLeft(), request);
		result.symbol += _text.length();
	}

	if (keyboard && !isLogEntry()) {
		auto keyboardTop = g.top() + g.height() + st::msgBotKbButton.margin;
		if (QRect(g.left(), keyboardTop, g.width(), keyboardHeight).contains(point)) {
			result.link = keyboard->getState(point - QPoint(g.left(), keyboardTop));
			return result;
		}
	}

	return result;
}

ClickHandlerPtr HistoryMessage::fastShareLink() const {
	if (!_fastShareLink) {
		_fastShareLink = MakeShared<LambdaClickHandler>([id = fullId()] {
			if (auto item = App::histItemById(id)) {
				FastShareMessage(item->toHistoryMessage());
			}
		});
	}
	return _fastShareLink;
}

// Forward to _media.
void HistoryMessage::updatePressed(QPoint point) {
	if (!_media) return;

	auto g = countGeometry();
	auto keyboard = inlineReplyKeyboard();
	if (keyboard) {
		auto keyboardHeight = st::msgBotKbButton.margin + keyboard->naturalHeight();
		g.setHeight(g.height() - keyboardHeight);
	}

	if (drawBubble()) {
		auto mediaDisplayed = _media && _media->isDisplayed();
		auto top = marginTop();
		auto trect = g.marginsAdded(-st::msgPadding);
		if (mediaDisplayed && _media->isBubbleTop()) {
			trect.setY(trect.y() - st::msgPadding.top());
		} else {
			if (displayFromName()) trect.setTop(trect.top() + st::msgNameFont->height);
			if (displayForwardedFrom()) {
				auto forwarded = Get<HistoryMessageForwarded>();
				auto fwdheight = ((forwarded->_text.maxWidth() > trect.width()) ? 2 : 1) * st::semiboldFont->height;
				trect.setTop(trect.top() + fwdheight);
			}
			if (Get<HistoryMessageReply>()) {
				auto h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
				trect.setTop(trect.top() + h);
			}
			if (!displayFromName() && !Has<HistoryMessageForwarded>()) {
				if (auto via = Get<HistoryMessageVia>()) {
					trect.setTop(trect.top() + st::msgNameFont->height);
				}
			}
		}
		if (mediaDisplayed && _media->isBubbleBottom()) {
			trect.setHeight(trect.height() + st::msgPadding.bottom());
		}

		auto needDateCheck = true;
		if (mediaDisplayed) {
			auto mediaAboveText = _media->isAboveMessage();
			auto mediaHeight = _media->height();
			auto mediaLeft = trect.x() - st::msgPadding.left();
			auto mediaTop = mediaAboveText ? trect.y() : (trect.y() + trect.height() - mediaHeight);
			_media->updatePressed(point - QPoint(mediaLeft, mediaTop));
		}
	} else {
		_media->updatePressed(point - g.topLeft());
	}
}

bool HistoryMessage::getStateFromName(QPoint point, QRect &trect, HistoryTextState *outResult) const {
	if (displayFromName()) {
		if (point.y() >= trect.top() && point.y() < trect.top() + st::msgNameFont->height) {
			if (point.x() >= trect.left() && point.x() < trect.left() + trect.width() && point.x() < trect.left() + author()->nameText.maxWidth()) {
				outResult->link = author()->openLink();
				return true;
			}
			auto forwarded = Get<HistoryMessageForwarded>();
			auto via = Get<HistoryMessageVia>();
			if (via && !forwarded && point.x() >= trect.left() + author()->nameText.maxWidth() + st::msgServiceFont->spacew && point.x() < trect.left() + author()->nameText.maxWidth() + st::msgServiceFont->spacew + via->_width) {
				outResult->link = via->_lnk;
				return true;
			}
		}
		trect.setTop(trect.top() + st::msgNameFont->height);
	}
	return false;
}

bool HistoryMessage::getStateForwardedInfo(QPoint point, QRect &trect, HistoryTextState *outResult, const HistoryStateRequest &request) const {
	if (displayForwardedFrom()) {
		auto forwarded = Get<HistoryMessageForwarded>();
		auto fwdheight = ((forwarded->_text.maxWidth() > trect.width()) ? 2 : 1) * st::semiboldFont->height;
		if (point.y() >= trect.top() && point.y() < trect.top() + fwdheight) {
			auto breakEverywhere = (forwarded->_text.countHeight(trect.width()) > 2 * st::semiboldFont->height);
			auto textRequest = request.forText();
			if (breakEverywhere) {
				textRequest.flags |= Text::StateRequest::Flag::BreakEverywhere;
			}
			*outResult = forwarded->_text.getState(point - trect.topLeft(), trect.width(), textRequest);
			outResult->symbol = 0;
			outResult->afterSymbol = false;
			if (breakEverywhere) {
				outResult->cursor = HistoryInForwardedCursorState;
			} else {
				outResult->cursor = HistoryDefaultCursorState;
			}
			return true;
		}
		trect.setTop(trect.top() + fwdheight);
	}
	return false;
}

bool HistoryMessage::getStateReplyInfo(QPoint point, QRect &trect, HistoryTextState *outResult) const {
	if (auto reply = Get<HistoryMessageReply>()) {
		int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		if (point.y() >= trect.top() && point.y() < trect.top() + h) {
			if (reply->replyToMsg && QRect(trect.x(), trect.y() + st::msgReplyPadding.top(), trect.width(), st::msgReplyBarSize.height()).contains(point)) {
				outResult->link = reply->replyToLink();
			}
			return true;
		}
		trect.setTop(trect.top() + h);
	}
	return false;
}

bool HistoryMessage::getStateViaBotIdInfo(QPoint point, QRect &trect, HistoryTextState *outResult) const {
	if (!displayFromName() && !Has<HistoryMessageForwarded>()) {
		if (auto via = Get<HistoryMessageVia>()) {
			if (QRect(trect.x(), trect.y(), via->_width, st::msgNameFont->height).contains(point)) {
				outResult->link = via->_lnk;
				return true;
			}
			trect.setTop(trect.top() + st::msgNameFont->height);
		}
	}
	return false;
}

bool HistoryMessage::getStateText(QPoint point, QRect &trect, HistoryTextState *outResult, const HistoryStateRequest &request) const {
	if (trect.contains(point)) {
		*outResult = _text.getState(point - trect.topLeft(), trect.width(), request.forText());
		return true;
	}
	return false;
}

TextSelection HistoryMessage::adjustSelection(TextSelection selection, TextSelectType type) const {
	auto result = _text.adjustSelection(selection, type);
	auto beforeMediaLength = _text.length();
	if (selection.to <= beforeMediaLength) {
		return result;
	}
	auto mediaDisplayed = _media && _media->isDisplayed();
	if (mediaDisplayed) {
		auto mediaSelection = unskipTextSelection(_media->adjustSelection(skipTextSelection(selection), type));
		if (selection.from >= beforeMediaLength) {
			result = mediaSelection;
		} else {
			result.to = mediaSelection.to;
		}
	}
	auto beforeEntryLength = beforeMediaLength + (mediaDisplayed ? _media->fullSelectionLength() : 0);
	if (selection.to <= beforeEntryLength) {
		return result;
	}
	if (auto entry = Get<HistoryMessageLogEntryOriginal>()) {
		auto entrySelection = mediaDisplayed ? _media->skipSelection(skipTextSelection(selection)) : skipTextSelection(selection);
		auto logEntryOriginalSelection = entry->_page->adjustSelection(entrySelection, type);
		if (mediaDisplayed) {
			logEntryOriginalSelection = _media->unskipSelection(logEntryOriginalSelection);
		}
		logEntryOriginalSelection = unskipTextSelection(logEntryOriginalSelection);
		if (selection.from >= beforeEntryLength) {
			result = logEntryOriginalSelection;
		} else {
			result.to = logEntryOriginalSelection.to;
		}
	}
	return result;
}

void HistoryMessage::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (_media) _media->clickHandlerActiveChanged(p, active);
	HistoryItem::clickHandlerActiveChanged(p, active);
}

void HistoryMessage::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (_media) _media->clickHandlerPressedChanged(p, pressed);
	HistoryItem::clickHandlerPressedChanged(p, pressed);
}

QString HistoryMessage::notificationHeader() const {
	return (!_history->peer->isUser() && !isPost()) ? from()->name : QString();
}

bool HistoryMessage::displayFromPhoto() const {
	return hasFromPhoto() && !isAttachedToNext();
}

bool HistoryMessage::hasFromPhoto() const {
	return (Adaptive::ChatWide() || (!out() && !history()->peer->isUser())) && !isPost() && !isEmpty();
}

HistoryMessage::~HistoryMessage() {
	_media.reset();
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->clearData(this);
	}
}
