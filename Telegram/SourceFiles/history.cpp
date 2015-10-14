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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "history.h"
#include "mainwidget.h"
#include "application.h"
#include "fileuploader.h"
#include "window.h"
#include "gui/filedialog.h"

#include "audio.h"
#include "localstorage.h"

TextParseOptions _textNameOptions = {
	0, // flags
	4096, // maxw
	1, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};
TextParseOptions _textDlgOptions = {
	0, // flags
	0, // maxw is style-dependent
	1, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};
TextParseOptions _historyTextOptions = {
	TextParseLinks | TextParseMentions | TextParseHashtags | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};
TextParseOptions _historyBotOptions = {
	TextParseLinks | TextParseMentions | TextParseHashtags | TextParseBotCommands | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

namespace {
	TextParseOptions _historySrvOptions = {
		TextParseLinks | TextParseMentions | TextParseHashtags | TextParseMultiline | TextParseRichText, // flags
		0, // maxw
		0, // maxh
		Qt::LayoutDirectionAuto, // lang-dependent
	};
	TextParseOptions _webpageTitleOptions = {
		TextParseMultiline | TextParseRichText, // flags
		0, // maxw
		0, // maxh
		Qt::LayoutDirectionAuto, // dir
	};
	TextParseOptions _webpageDescriptionOptions = {
		TextParseLinks | TextParseMultiline | TextParseRichText, // flags
		0, // maxw
		0, // maxh
		Qt::LayoutDirectionAuto, // dir
	};
	TextParseOptions _twitterDescriptionOptions = {
		TextParseLinks | TextParseMentions | TextTwitterMentions | TextParseHashtags | TextTwitterHashtags | TextParseMultiline | TextParseRichText, // flags
		0, // maxw
		0, // maxh
		Qt::LayoutDirectionAuto, // dir
	};
	TextParseOptions _instagramDescriptionOptions = {
		TextParseLinks | TextParseMentions | TextInstagramMentions | TextParseHashtags | TextInstagramHashtags | TextParseMultiline | TextParseRichText, // flags
		0, // maxw
		0, // maxh
		Qt::LayoutDirectionAuto, // dir
	};

	inline void _initTextOptions() {
		_historySrvOptions.dir = _textNameOptions.dir = _textDlgOptions.dir = cLangDir();
		_textDlgOptions.maxw = st::dlgMaxWidth * 2;
		_webpageTitleOptions.maxw = st::msgMaxWidth - st::msgPadding.left() - st::msgPadding.right() - st::webPageLeft;
		_webpageTitleOptions.maxh = st::webPageTitleFont->height * 2;
		_webpageDescriptionOptions.maxw = st::msgMaxWidth - st::msgPadding.left() - st::msgPadding.right() - st::webPageLeft;
		_webpageDescriptionOptions.maxh = st::webPageDescriptionFont->height * 3;
	}

	AnimatedGif animated;

	inline HistoryReply *toHistoryReply(HistoryItem *item) {
		return item ? item->toHistoryReply() : 0;
	}
	inline const HistoryReply *toHistoryReply(const HistoryItem *item) {
		return item ? item->toHistoryReply() : 0;
	}
	inline HistoryForwarded *toHistoryForwarded(HistoryItem *item) {
		return item ? item->toHistoryForwarded() : 0;
	}
	inline const HistoryForwarded *toHistoryForwarded(const HistoryItem *item) {
		return item ? item->toHistoryForwarded() : 0;
	}
	inline const TextParseOptions &itemTextParseOptions(HistoryItem *item) {
		return itemTextParseOptions(item->history(), item->from());
	}
}

const TextParseOptions &itemTextParseOptions(History *h, PeerData *f) {
	if ((h->peer->isUser() && h->peer->asUser()->botInfo) || (f->isUser() && f->asUser()->botInfo) || (h->peer->isChat() && h->peer->asChat()->botStatus >= 0) || (h->peer->isChannel() && h->peer->asChannel()->botStatus >= 0)) {
		return _historyBotOptions;
	}
	return _historyTextOptions;
}

void historyInit() {
	_initTextOptions();
}

void startGif(HistoryItem *row, const QString &file) {
	if (row == animated.msg) {
		stopGif();
	} else {
		animated.start(row, file);
	}
}

void itemReplacedGif(HistoryItem *oldItem, HistoryItem *newItem) {
	if (oldItem == animated.msg) {
		animated.msg = newItem;
	}
}

void itemRemovedGif(HistoryItem *item) {
	if (item == animated.msg) {
		animated.stop(true);
	}
}

void stopGif() {
	animated.stop();
}

void DialogRow::paint(Painter &p, int32 w, bool act, bool sel, bool onlyBackground) const {
	QRect fullRect(0, 0, w, st::dlgHeight);
	p.fillRect(fullRect, (act ? st::dlgActiveBG : (sel ? st::dlgHoverBG : st::dlgBG))->b);
	if (onlyBackground) return;

	p.drawPixmap(st::dlgPaddingHor, st::dlgPaddingVer, history->peer->photo->pix(st::dlgPhotoSize));

	int32 nameleft = st::dlgPaddingHor + st::dlgPhotoSize + st::dlgPhotoPadding;
	int32 namewidth = w - nameleft - st::dlgPaddingHor;
	QRect rectForName(nameleft, st::dlgPaddingVer + st::dlgNameTop, namewidth, st::msgNameFont->height);

	// draw chat icon
	if (history->peer->isChat()) {
		p.drawPixmap(QPoint(rectForName.left() + st::dlgChatImgPos.x(), rectForName.top() + st::dlgChatImgPos.y()), App::sprite(), (act ? st::dlgActiveChatImg : st::dlgChatImg));
		rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
	} else if (history->peer->isChannel()) {
		p.drawPixmap(QPoint(rectForName.left() + st::dlgChannelImgPos.x(), rectForName.top() + st::dlgChannelImgPos.y()), App::sprite(), (act ? st::dlgActiveChannelImg : st::dlgChannelImg));
		rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
	}

	HistoryItem *last = history->lastMsg;
	if (!last) {
		p.setFont(st::dlgHistFont->f);
		p.setPen((act ? st::dlgActiveColor : st::dlgSystemColor)->p);
		if (history->typing.isEmpty() && history->sendActions.isEmpty()) {
			p.drawText(nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgFont->ascent + st::dlgSep, lang(lng_empty_history));
		} else {
			history->typingText.drawElided(p, nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, namewidth);
		}
	} else {
		// draw date
		QDateTime now(QDateTime::currentDateTime()), lastTime(last->date);
		QDate nowDate(now.date()), lastDate(lastTime.date());
		QString dt;
		if (lastDate == nowDate) {
			dt = lastTime.toString(cTimeFormat());
		} else if (lastDate.year() == nowDate.year() && lastDate.weekNumber() == nowDate.weekNumber()) {
			dt = langDayOfWeek(lastDate);
		} else {
			dt = lastDate.toString(qsl("d.MM.yy"));
		}
		int32 dtWidth = st::dlgDateFont->width(dt);
		rectForName.setWidth(rectForName.width() - dtWidth - st::dlgDateSkip);
		p.setFont(st::dlgDateFont->f);
		p.setPen((act ? st::dlgActiveDateColor : st::dlgDateColor)->p);
		p.drawText(rectForName.left() + rectForName.width() + st::dlgDateSkip, rectForName.top() + st::msgNameFont->height - st::msgDateFont->descent, dt);

		// draw check
		if (last->needCheck()) {
			const style::sprite *check;
			if (last->id > 0) {
				if (last->unread()) {
					check = act ? &st::dlgActiveCheckImg : &st::dlgCheckImg;
				} else {
					check = act ? &st::dlgActiveDblCheckImg: &st::dlgDblCheckImg;
				}
			} else {
				check = act ? &st::dlgActiveSendImg : &st::dlgSendImg;
			}
			rectForName.setWidth(rectForName.width() - check->pxWidth() - st::dlgCheckSkip);
			p.drawPixmap(QPoint(rectForName.left() + rectForName.width() + st::dlgCheckLeft, rectForName.top() + st::dlgCheckTop), App::sprite(), *check);
		}

		// draw unread
		int32 lastWidth = namewidth, unread = history->unreadCount;
		if (unread) {
			QString unreadStr = QString::number(unread);
			int32 unreadWidth = st::dlgUnreadFont->width(unreadStr);
			int32 unreadRectWidth = unreadWidth + 2 * st::dlgUnreadPaddingHor;
			int32 unreadRectHeight = st::dlgUnreadFont->height + 2 * st::dlgUnreadPaddingVer;
			int32 unreadRectLeft = w - st::dlgPaddingHor - unreadRectWidth;
			int32 unreadRectTop = st::dlgHeight - st::dlgPaddingVer - unreadRectHeight;
			lastWidth -= unreadRectWidth + st::dlgUnreadPaddingHor;
			p.setBrush((act ? st::dlgActiveUnreadBG : (history->mute ? st::dlgUnreadMutedBG : st::dlgUnreadBG))->b);
			p.setPen(Qt::NoPen);
			p.drawRoundedRect(unreadRectLeft, unreadRectTop, unreadRectWidth, unreadRectHeight, st::dlgUnreadRadius, st::dlgUnreadRadius);
			p.setFont(st::dlgUnreadFont->f);
			p.setPen((act ? st::dlgActiveUnreadColor : st::dlgUnreadColor)->p);
			p.drawText(unreadRectLeft + st::dlgUnreadPaddingHor, unreadRectTop + st::dlgUnreadPaddingVer + st::dlgUnreadFont->ascent, unreadStr);
		}
		if (history->typing.isEmpty() && history->sendActions.isEmpty()) {
			last->drawInDialog(p, QRect(nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, lastWidth, st::dlgFont->height), act, history->textCachedFor, history->lastItemTextCache);
		} else {
			p.setPen((act ? st::dlgActiveColor : st::dlgSystemColor)->p);
			history->typingText.drawElided(p, nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, lastWidth);
		}
	}

	p.setPen((act ? st::dlgActiveColor : st::dlgNameColor)->p);
	history->peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

void FakeDialogRow::paint(Painter &p, int32 w, bool act, bool sel, bool onlyBackground) const {
	QRect fullRect(0, 0, w, st::dlgHeight);
	p.fillRect(fullRect, (act ? st::dlgActiveBG : (sel ? st::dlgHoverBG : st::dlgBG))->b);
	if (onlyBackground) return;
	
	History *history = _item->history();

	p.drawPixmap(st::dlgPaddingHor, st::dlgPaddingVer, history->peer->photo->pix(st::dlgPhotoSize));

	int32 nameleft = st::dlgPaddingHor + st::dlgPhotoSize + st::dlgPhotoPadding;
	int32 namewidth = w - nameleft - st::dlgPaddingHor;
	QRect rectForName(nameleft, st::dlgPaddingVer + st::dlgNameTop, namewidth, st::msgNameFont->height);

	// draw chat icon
	if (history->peer->isChat()) {
		p.drawPixmap(QPoint(rectForName.left() + st::dlgChatImgPos.x(), rectForName.top() + st::dlgChatImgPos.y()), App::sprite(), (act ? st::dlgActiveChatImg : st::dlgChatImg));
		rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
	} else if (history->peer->isChannel()) {
		p.drawPixmap(QPoint(rectForName.left() + st::dlgChannelImgPos.x(), rectForName.top() + st::dlgChannelImgPos.y()), App::sprite(), (act ? st::dlgActiveChannelImg : st::dlgChannelImg));
		rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
	}

	// draw date
	QDateTime now(QDateTime::currentDateTime()), lastTime(_item->date);
	QDate nowDate(now.date()), lastDate(lastTime.date());
	QString dt;
	if (lastDate == nowDate) {
		dt = lastTime.toString(cTimeFormat());
	} else if (lastDate.year() == nowDate.year() && lastDate.weekNumber() == nowDate.weekNumber()) {
		dt = langDayOfWeek(lastDate);
	} else {
		dt = lastDate.toString(qsl("d.MM.yy"));
	}
	int32 dtWidth = st::dlgDateFont->width(dt);
	rectForName.setWidth(rectForName.width() - dtWidth - st::dlgDateSkip);
	p.setFont(st::dlgDateFont->f);
	p.setPen((act ? st::dlgActiveDateColor : st::dlgDateColor)->p);
	p.drawText(rectForName.left() + rectForName.width() + st::dlgDateSkip, rectForName.top() + st::msgNameFont->height - st::msgDateFont->descent, dt);

	// draw check
	if (_item->needCheck()) {
		const style::sprite *check;
		if (_item->id > 0) {
			if (_item->unread()) {
				check = act ? &st::dlgActiveCheckImg : &st::dlgCheckImg;
			} else {
				check = act ? &st::dlgActiveDblCheckImg : &st::dlgDblCheckImg;
			}
		} else {
			check = act ? &st::dlgActiveSendImg : &st::dlgSendImg;
		}
		rectForName.setWidth(rectForName.width() - check->pxWidth() - st::dlgCheckSkip);
		p.drawPixmap(QPoint(rectForName.left() + rectForName.width() + st::dlgCheckLeft, rectForName.top() + st::dlgCheckTop), App::sprite(), *check);
	}

	// draw unread
	int32 lastWidth = namewidth, unread = history->unreadCount;
	_item->drawInDialog(p, QRect(nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, lastWidth, st::dlgFont->height), act, _cacheFor, _cache);

	p.setPen((act ? st::dlgActiveColor : st::dlgNameColor)->p);
	history->peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

History::History(const PeerId &peerId) : width(0), height(0)
, unreadCount(0)
, inboxReadBefore(1)
, outboxReadBefore(1)
, showFrom(0)
, unreadBar(0)
, peer(App::peer(peerId))
, oldLoaded(false)
, newLoaded(true)
, lastMsg(0)
, draftToId(0)
, lastWidth(0)
, lastScrollTop(ScrollMax)
, lastShowAtMsgId(ShowAtUnreadMsgId)
, mute(isNotifyMuted(peer->notify))
, lastKeyboardInited(false)
, lastKeyboardUsed(false)
, lastKeyboardId(0)
, lastKeyboardFrom(0)
, sendRequestId(0)
, textCachedFor(0)
, lastItemTextCache(st::dlgRichMinWidth)
, posInDialogs(0)
, typingText(st::dlgRichMinWidth)
{
	if (peer->isChannel() || (peer->isUser() && peer->asUser()->botInfo)) {
		outboxReadBefore = INT_MAX;
	}
	for (int32 i = 0; i < OverviewCount; ++i) {
		overviewCount[i] = -1; // not loaded yet
	}
}

void History::clearLastKeyboard() {
	lastKeyboardInited = true;
	lastKeyboardId = 0;
	lastKeyboardFrom = 0;
}

bool History::updateTyping(uint64 ms, uint32 dots, bool force) {
	if (!ms) ms = getms(true);
	bool changed = force;
	for (TypingUsers::iterator i = typing.begin(), e = typing.end(); i != e;) {
		if (ms >= i.value()) {
			i = typing.erase(i);
			changed = true;
		} else {
			++i;
		}
	}
	for (SendActionUsers::iterator i = sendActions.begin(); i != sendActions.cend();) {
		if (ms >= i.value().until) {
			i = sendActions.erase(i);
			changed = true;
		} else {
			++i;
		}
	}
	if (changed) {
		QString newTypingStr;
		int32 cnt = typing.size();
		if (cnt > 2) {
			newTypingStr = lng_many_typing(lt_count, cnt);
		} else if (cnt > 1) {
			newTypingStr = lng_users_typing(lt_user, typing.begin().key()->firstName, lt_second_user, (typing.end() - 1).key()->firstName);
		} else if (cnt) {
			newTypingStr = peer->isUser() ? lang(lng_typing) : lng_user_typing(lt_user, typing.begin().key()->firstName);
		} else if (!sendActions.isEmpty()) {
			switch (sendActions.begin().value().type) {
			case SendActionRecordVideo: newTypingStr = peer->isUser() ? lang(lng_send_action_record_video) : lng_user_action_record_video(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionUploadVideo: newTypingStr = peer->isUser() ? lang(lng_send_action_upload_video) : lng_user_action_upload_video(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionRecordAudio: newTypingStr = peer->isUser() ? lang(lng_send_action_record_audio) : lng_user_action_record_audio(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionUploadAudio: newTypingStr = peer->isUser() ? lang(lng_send_action_upload_audio) : lng_user_action_upload_audio(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionUploadPhoto: newTypingStr = peer->isUser() ? lang(lng_send_action_upload_photo) : lng_user_action_upload_photo(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionUploadFile: newTypingStr = peer->isUser() ? lang(lng_send_action_upload_file) : lng_user_action_upload_file(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionChooseLocation: newTypingStr = peer->isUser() ? lang(lng_send_action_geo_location) : lng_user_action_geo_location(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionChooseContact: newTypingStr = peer->isUser() ? lang(lng_send_action_choose_contact) : lng_user_action_choose_contact(lt_user, sendActions.begin().key()->firstName); break;
			}
		}
		if (!newTypingStr.isEmpty()) {
			newTypingStr += qsl("...");
		}
		if (typingStr != newTypingStr) {
			typingText.setText(st::dlgHistFont, (typingStr = newTypingStr), _textNameOptions);
		}
	}
	if (!typingStr.isEmpty()) {
		if (typingText.lastDots(dots % 4)) {
			changed = true;
		}
	}
	if (changed && App::main()) {
		if (!dialogs.isEmpty()) App::main()->dlgUpdated(dialogs[0]);
		if (App::main()->historyPeer() == peer) {
			App::main()->topBar()->update();
		}
	}
	return changed;
}

void History::eraseFromOverview(MediaOverviewType type, MsgId msgId) {
	if (overviewIds[type].isEmpty()) return;

	History::MediaOverviewIds::iterator i = overviewIds[type].find(msgId);
	if (i == overviewIds[type].cend()) return;

	overviewIds[type].erase(i);
	for (History::MediaOverview::iterator i = overview[type].begin(), e = overview[type].end(); i != e; ++i) {
		if ((*i) == msgId) {
			overview[type].erase(i);
			if (overviewCount[type] > 0) {
				--overviewCount[type];
				if (!overviewCount[type]) {
					overviewCount[type] = -1;
				}
			}
			break;
		}
	}
	if (App::wnd()) App::wnd()->mediaOverviewUpdated(peer, type);
}

ChannelHistory::ChannelHistory(const PeerId &peer) : History(peer),
unreadCountAll(0),
_onlyImportant(true),
_otherOldLoaded(false), _otherNewLoaded(true),
_collapseMessage(0), _joinedMessage(0) {
}

bool ChannelHistory::isSwitchReadyFor(MsgId switchId, MsgId &fixInScrollMsgId, int32 &fixInScrollMsgTop) {
	if (switchId == SwitchAtTopMsgId) {
		if (_onlyImportant) return true;

		int32 bottomUnderScrollTop = 0;
		HistoryItem *atTopItem = App::main()->atTopImportantMsg(bottomUnderScrollTop);
		if (atTopItem) {
			fixInScrollMsgId = atTopItem->id;
			fixInScrollMsgTop = atTopItem->y + atTopItem->block()->y + atTopItem->height() - bottomUnderScrollTop - height;
			if (_otherList.indexOf(atTopItem) >= 0) {
				switchMode();
				return true;
			}
			return false;
		}
		if (!_otherList.isEmpty()) {
			switchMode();
			return true;
		}
		return false;
	}
	if (HistoryItem *item = App::histItemById(channelId(), switchId)) {
		HistoryItemType itemType = item->type();
		if (itemType == HistoryItemGroup || itemType == HistoryItemCollapse) {
			if (itemType == HistoryItemGroup && !_onlyImportant) return true;
			if (itemType == HistoryItemCollapse && _onlyImportant) return true;
			bool willNeedCollapse = (itemType == HistoryItemGroup);

			HistoryItem *prev = findPrevItem(item);
			if (prev) {
				fixInScrollMsgId = prev->id;
				fixInScrollMsgTop = prev->y + prev->block()->y + prev->height() - height;
				if (_otherList.indexOf(prev) >= 0) {
					switchMode();
					insertCollapseItem(fixInScrollMsgId);
					return true;
				}
				return false;
			}
			if (itemType == HistoryItemGroup) {
				fixInScrollMsgId = qMax(static_cast<HistoryGroup*>(item)->minId(), 1);
				fixInScrollMsgTop = item->y + item->block()->y - height;
				if (oldLoaded && _otherOldLoaded) {
					switchMode();
					insertCollapseItem(fixInScrollMsgId);
					return true;
				}
			} else if (itemType == HistoryItemCollapse) {
				fixInScrollMsgId = qMax(static_cast<HistoryCollapse*>(item)->wasMinId(), 1);
				fixInScrollMsgTop = item->y + item->block()->y - height;
				if (oldLoaded && _otherOldLoaded) {
					switchMode();
					return true;
				}
			}
			return false;
		}
		if (item->history() == this) {
			if (_onlyImportant && !item->isImportant()) {
				if (_otherList.indexOf(item) >= 0) {
					switchMode();
					return true;
				}
				return false;
			} else if (!item->detached()) {
				return true;
			}
		}
	} else if (switchId < 0) {
		LOG(("App Error: isSwitchReadyFor() switchId not found!"));
		switchMode();
		return true;
	}
	return false;
}

void ChannelHistory::getSwitchReadyFor(MsgId switchId, MsgId &fixInScrollMsgId, int32 &fixInScrollMsgTop) {
	if (!isSwitchReadyFor(switchId, fixInScrollMsgId, fixInScrollMsgTop)) {
		if (switchId > 0) {
			if (HistoryItem *item = App::histItemById(channelId(), switchId)) {
				if (_onlyImportant && !item->isImportant()) {
					_otherList.clear();
					_otherNewLoaded = _otherOldLoaded = false;

					switchMode();
				} else {
					clear(true);
					newLoaded = oldLoaded = false;
					lastWidth = 0;
				}
			} else {
				clear(true);
				newLoaded = oldLoaded = false;
				lastWidth = 0;
			}
		} else {
			_otherList.clear();
			_otherNewLoaded = _otherOldLoaded = false;

			switchMode();
		}
	}
}

void ChannelHistory::insertCollapseItem(MsgId wasMinId) {
	if (_onlyImportant) return;

	bool insertAfter = false;
	for (int32 blockIndex = 1, blocksCount = blocks.size(); blockIndex < blocksCount; ++blockIndex) { // skip first date block
		HistoryBlock *block = blocks.at(blockIndex);
		for (int32 itemIndex = 0, itemsCount = block->items.size(); itemIndex < itemsCount; ++itemIndex) {
			HistoryItem *item = block->items.at(itemIndex);
			if (insertAfter || item->id > wasMinId || (item->id == wasMinId && !item->isImportant())) {
				_collapseMessage = new HistoryCollapse(this, block, wasMinId, item->date);
				if (!addNewInTheMiddle(_collapseMessage, blockIndex, itemIndex)) {
					_collapseMessage = 0;
				}
				return;
			} else if (item->id == wasMinId && item->isImportant()) {
				insertAfter = true;
			}
		}
	}
}

void ChannelHistory::getRangeDifference() {
	MsgId fromId = 0, toId = 0;
	for (int32 blockIndex = 0, blocksCount = blocks.size(); blockIndex < blocksCount; ++blockIndex) {
		HistoryBlock *block = blocks.at(blockIndex);
		for (int32 itemIndex = 0, itemsCount = block->items.size(); itemIndex < itemsCount; ++itemIndex) {
			HistoryItem *item = block->items.at(itemIndex);
			if (item->type() == HistoryItemMsg && item->id > 0) {
				fromId = item->id;
				break;
			} else if (item->type() == HistoryItemGroup) {
				fromId = static_cast<HistoryGroup*>(item)->minId() + 1;
				break;
			}
		}
		if (fromId) break;
	}
	if (!fromId) return;
	for (int32 blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int32 itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			if (item->type() == HistoryItemMsg && item->id > 0) {
				toId = item->id;
				break;
			} else if (item->type() == HistoryItemGroup) {
				toId = static_cast<HistoryGroup*>(item)->maxId() - 1;
				break;
			}
		}
		if (toId) break;
	}
	if (fromId > 0 && peer->asChannel()->pts() > 0) {
		if (_rangeDifferenceRequestId) {
			MTP::cancel(_rangeDifferenceRequestId);
		}
		_rangeDifferenceFromId = fromId;
		_rangeDifferenceToId = toId;

		MTP_LOG(0, ("getChannelDifference { good - after channelDifferenceTooLong was received, validating history part }%1").arg(cTestMode() ? " TESTMODE" : ""));
		getRangeDifferenceNext(peer->asChannel()->pts());
	}
}

void ChannelHistory::getRangeDifferenceNext(int32 pts) {
	if (!App::main() || _rangeDifferenceToId < _rangeDifferenceFromId) return;

	int32 limit = _rangeDifferenceToId + 1 - _rangeDifferenceFromId;
	_rangeDifferenceRequestId = MTP::send(MTPupdates_GetChannelDifference(peer->asChannel()->inputChannel, MTP_channelMessagesFilter(MTP_int(0), MTP_vector<MTPMessageRange>(1, MTP_messageRange(MTP_int(_rangeDifferenceFromId), MTP_int(_rangeDifferenceToId)))), MTP_int(pts), MTP_int(limit)), App::main()->rpcDone(&MainWidget::gotRangeDifference, peer->asChannel()));
}

void ChannelHistory::addNewGroup(const MTPMessageGroup &group) {
	if (group.type() != mtpc_messageGroup) return;
	const MTPDmessageGroup &d(group.c_messageGroup());

	if (onlyImportant()) {
		_otherNewLoaded = false;
	} else if (_otherNewLoaded) {
		if (_otherList.isEmpty() || _otherList.back()->type() != HistoryItemGroup) {
			_otherList.push_back(regItem(new HistoryGroup(this, 0, d, _otherList.isEmpty() ? date(d.vdate) : _otherList.back()->date)));
		} else {
			static_cast<HistoryGroup*>(_otherList.back())->uniteWith(d.vmin_id.v, d.vmax_id.v, d.vcount.v);
		}
	}

	if (onlyImportant()) {
		if (newLoaded) {
			HistoryItem *prev = blocks.isEmpty() ? 0 : blocks.back()->items.back();
			HistoryBlock *to = 0;
			bool newBlock = blocks.isEmpty();
			if (newBlock) {
				to = new HistoryBlock(this);
				to->y = height;
			} else {
				to = blocks.back();
				height -= to->height;
			}
			prev = addMessageGroupAfterPrevToBlock(d, prev, to);
			height += to->height;
			if (newBlock) {
				HistoryBlock *dateBlock = new HistoryBlock(this);
				HistoryItem *dayItem = createDayServiceMsg(this, dateBlock, blocks.front()->items.front()->date);
				dateBlock->items.push_back(dayItem);
				int32 dh = dayItem->resize(width);
				dateBlock->height = dh;
				for (Blocks::iterator i = blocks.begin(), e = blocks.end(); i != e; ++i) {
					(*i)->y += dh;
				}
				blocks.push_front(dateBlock); // date block
				height += dh;
			}
		}
	} else {
		setNotLoadedAtBottom();
	}
}

HistoryJoined *ChannelHistory::insertJoinedMessage(bool unread) {
	if (_joinedMessage || !peer->asChannel()->amIn()) return _joinedMessage;

	UserData *inviter = (peer->asChannel()->inviter > 0) ? App::userLoaded(peer->asChannel()->inviter) : 0;
	if (!inviter) return 0;

	if (peerToUser(inviter->id) == MTP::authedId()) unread = false;
	int32 flags = (unread ? MTPDmessage_flag_unread : 0);
	QDateTime inviteDate = peer->asChannel()->inviteDate;
	if (unread) _maxReadMessageDate = inviteDate;
	if (isEmpty()) {
		HistoryBlock *to = new HistoryBlock(this);
		bool newBlock = true;
		_joinedMessage = new HistoryJoined(this, to, inviteDate, inviter, flags);
		if (!addNewItem(to, newBlock, regItem(_joinedMessage), unread)) {
			_joinedMessage = 0;
		}
		return _joinedMessage;
	}
	HistoryItem *lastSeenDateItem = 0;
	for (int32 blockIndex = blocks.size(); blockIndex > 1;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int32 itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			HistoryItemType type = item->type();
			if (type == HistoryItemMsg || type == HistoryItemGroup) {
				if (item->date <= inviteDate) {
					++itemIndex;
					if (item->date.date() != inviteDate.date()) {
						HistoryDateMsg *joinedDateItem = new HistoryDateMsg(this, block, inviteDate.date());
						if (addNewInTheMiddle(joinedDateItem, blockIndex, itemIndex)) {
							++itemIndex;
						}
					}
					_joinedMessage = new HistoryJoined(this, block, inviteDate, inviter, flags);
					if (!addNewInTheMiddle(_joinedMessage, blockIndex, itemIndex)) {
						_joinedMessage = 0;
					}
					if (lastSeenDateItem && lastSeenDateItem->date.date() == inviteDate.date()) {
						lastSeenDateItem->destroy();
					}
					if (lastMsgDate.isNull() || inviteDate >= lastMsgDate) {
						setLastMessage(_joinedMessage);
						if (unread) {
							newItemAdded(_joinedMessage);
						}
					}
					return _joinedMessage;
				} else {
					lastSeenDateItem = 0;
				}
			} else if (type == HistoryItemDate) {
				lastSeenDateItem = item;
			}
		}
	}

	// adding new item to new block
	int32 addToH = 0, skip = 0;
	if (!blocks.isEmpty()) { // remove date block
		if (width) addToH = -blocks.front()->height;
		delete blocks.front();
		blocks.pop_front();
	}
	HistoryItem *till = blocks.isEmpty() ? 0 : blocks.front()->items.front();

	HistoryBlock *block = new HistoryBlock(this);

	_joinedMessage = new HistoryJoined(this, block, inviteDate, inviter, flags);
	if (regItem(_joinedMessage)) {
		addItemAfterPrevToBlock(_joinedMessage, 0, block);
	} else {
		_joinedMessage = 0;
	}
	if (till && _joinedMessage && inviteDate.date() != till->date.date()) {
		HistoryItem *dayItem = createDayServiceMsg(this, block, till->date);
		block->items.push_back(dayItem);
		if (width) {
			dayItem->y = block->height;
			block->height += dayItem->resize(width);
		}
	}
	if (!block->items.isEmpty()) {
		blocks.push_front(block);
		if (width) {
			addToH += block->height;
			++skip;
		}
	} else {
		delete block;
	}
	if (!blocks.isEmpty()) {
		HistoryBlock *dateBlock = new HistoryBlock(this);
		HistoryItem *dayItem = createDayServiceMsg(this, dateBlock, blocks.front()->items.front()->date);
		dateBlock->items.push_back(dayItem);
		if (width) {
			int32 dh = dayItem->resize(width);
			dateBlock->height = dh;
			if (skip) {
				blocks.front()->y += dh;
			}
			addToH += dh;
			++skip;
		}
		blocks.push_front(dateBlock); // date block
	}
	if (width && addToH) {
		for (Blocks::iterator i = blocks.begin(), e = blocks.end(); i != e; ++i) {
			if (skip) {
				--skip;
			} else {
				(*i)->y += addToH;
			}
		}
		height += addToH;
	}
	if (!lastMsgDate.isNull() && inviteDate >= lastMsgDate) {
		setLastMessage(_joinedMessage);
		if (unread) {
			newItemAdded(_joinedMessage);
		}
	}
	return _joinedMessage;
}

void ChannelHistory::checkJoinedMessage(bool createUnread) {
	if (_joinedMessage || peer->asChannel()->inviter <= 0) return;
	if (isEmpty()) {
		if (loadedAtTop() && loadedAtBottom()) {
			if (insertJoinedMessage(createUnread)) {
				setLastMessage(_joinedMessage);
			}
			return;
		}
	}

	QDateTime inviteDate = peer->asChannel()->inviteDate;
	QDateTime firstDate, lastDate;
	for (int32 blockIndex = 1, blocksCount = blocks.size(); blockIndex < blocksCount; ++blockIndex) {
		HistoryBlock *block = blocks.at(blockIndex);
		int32 itemIndex = 0, itemsCount = block->items.size();
		for (; itemIndex < itemsCount; ++itemIndex) {
			HistoryItem *item = block->items.at(itemIndex);
			HistoryItemType type = item->type();
			if (type == HistoryItemMsg || type == HistoryItemGroup) {
				firstDate = item->date;
				break;
			}
		}
		if (itemIndex < itemsCount) break;
	}
	for (int32 blockIndex = blocks.size(); blockIndex > 1;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		int32 itemIndex = block->items.size();
		for (; itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			HistoryItemType type = item->type();
			if (type == HistoryItemMsg || type == HistoryItemGroup) {
				lastDate = item->date;
				++itemIndex;
				break;
			}
		}
		if (itemIndex) break;
	}

	if (!firstDate.isNull() && !lastDate.isNull() && (firstDate <= inviteDate || loadedAtTop()) && (lastDate > inviteDate || loadedAtBottom())) {
		bool willBeLastMsg = (inviteDate >= lastDate);
		if (insertJoinedMessage(createUnread && willBeLastMsg) && willBeLastMsg) {
			setLastMessage(_joinedMessage);
		}
	}
}

void ChannelHistory::checkMaxReadMessageDate() {
	if (_maxReadMessageDate.isValid()) return;

	for (int32 blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int32 itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			if (item->isImportant() && !item->unread()) {
				_maxReadMessageDate = item->date;
				return;
			}
		}
	}
	if (loadedAtTop()) {
		_maxReadMessageDate = date(MTP_int(peer->asChannel()->date));
	}
}

const QDateTime &ChannelHistory::maxReadMessageDate() {
	return _maxReadMessageDate;
}

HistoryItem *ChannelHistory::addNewChannelMessage(const MTPMessage &msg, NewMessageType type) {
	if (type == NewMessageExisting) return addToHistory(msg);

	HistoryItem *result = addNewToBlocks(msg, type);
	if (result) addNewToOther(result, type);
	return result;
}

HistoryItem *ChannelHistory::addNewToBlocks(const MTPMessage &msg, NewMessageType type) {
	bool isImportant = isChannel() ? isImportantChannelMessage(idFromMessage(msg), flagsFromMessage(msg)) : true;

	if (!loadedAtBottom()) {
		HistoryItem *item = addToHistory(msg);
		if (item && isImportant) {
			setLastMessage(item);
			if (type == NewMessageUnread) {
				if (item->unread()) {
					newItemAdded(item);
				} else if (item->out()) {
					outboxRead(item);
				} else {
					inboxRead(item);
				}
			}
		}
		return item;
	}

	if (!isImportant && onlyImportant()) {
		HistoryItem *item = addToHistory(msg), *prev = isEmpty() ? 0 : blocks.back()->items.back();
		HistoryItem *group = addMessageGroupAfterPrev(item, prev);
		if (group && group != prev) {
			height += group->height();
		}
		return item;
	}

	if (!isImportant && !onlyImportant() && !isEmpty() && type == NewMessageLast) {
		clear(true);
	}

	HistoryBlock *to = 0;
	bool newBlock = blocks.isEmpty();
	if (newBlock) {
		to = new HistoryBlock(this);
	} else {
		to = blocks.back();
	}
	return addNewItem(to, newBlock, createItem(to, msg, (type == NewMessageUnread)), (type == NewMessageUnread));
}

void ChannelHistory::addNewToOther(HistoryItem *item, NewMessageType type) {
	if (!_otherNewLoaded) return;

	if (!item->isImportant()) {
		if (onlyImportant()) {
			if (type == NewMessageLast) {
				_otherList.clear();
				_otherOldLoaded = false;
			}
		} else {
			if (_otherList.isEmpty() || _otherList.back()->type() != HistoryItemGroup) {
				_otherList.push_back(regItem(new HistoryGroup(this, 0, item, _otherList.isEmpty() ? item->date : _otherList.back()->date)));
			} else {
				static_cast<HistoryGroup*>(_otherList.back())->uniteWith(item);
			}
			return;
		}
	}
	_otherList.push_back(item);
}

void ChannelHistory::switchMode() {
	OtherList savedList;
	if (!blocks.isEmpty()) {
		savedList.reserve(((blocks.size() - 2) * MessagesPerPage + blocks.back()->items.size()) * (onlyImportant() ? 2 : 1));
		for (Blocks::const_iterator i = blocks.cbegin(), e = blocks.cend(); i != e; ++i) {
			HistoryBlock *block = *i;
			for (HistoryBlock::Items::const_iterator j = block->items.cbegin(), end = block->items.cend(); j != end; ++j) {
				HistoryItem *item = *j;
				HistoryItemType itemType = item->type();
				if (itemType == HistoryItemMsg || itemType == HistoryItemGroup) {
					savedList.push_back(item);
				}
			}
		}
	}
	bool savedNewLoaded = newLoaded, savedOldLoaded = oldLoaded;

	clear(true);

	newLoaded = _otherNewLoaded;
	oldLoaded = _otherOldLoaded;
	if (int32 count = _otherList.size()) {
		blocks.reserve(qCeil(count / float64(MessagesPerPage)) + 1);
		createInitialDateBlock(_otherList.front()->date);

		HistoryItem *prev = 0;
		for (int32 i = 0; i < count;) {
			HistoryBlock *block = new HistoryBlock(this);
			int32 willAddToBlock = qMin(int32(MessagesPerPage), count - i);
			block->items.reserve(willAddToBlock);
			for (int32 till = i + willAddToBlock; i < till; ++i) {
				HistoryItem *item = _otherList.at(i);
				item->attach(block);
				prev = addItemAfterPrevToBlock(item, prev, block);
			}
			block->y = height;
			blocks.push_back(block);
			height += block->height;
		}
	}

	_otherList = savedList;
	_otherNewLoaded = savedNewLoaded;
	_otherOldLoaded = savedOldLoaded;

	_onlyImportant = !_onlyImportant;

	lastWidth = 0;

	checkJoinedMessage();
}

void ChannelHistory::cleared() {
	_collapseMessage = 0;
	_joinedMessage = 0;
}

HistoryGroup *ChannelHistory::findGroup(MsgId msgId) const { // find message group using binary search
	if (!_onlyImportant) return findGroupInOther(msgId);

	HistoryBlock *block = findGroupBlock(msgId);
	if (!block) return 0;

	int32 itemIndex = 0;
	if (block->items.size() > 1) for (int32 minItem = 0, maxItem = block->items.size();;) {
		for (int32 startCheckItem = (minItem + maxItem) / 2, checkItem = startCheckItem;;) {
			HistoryItem *item = block->items.at(checkItem); // out msgs could be a mess in monotonic ids
			if ((item->id > 0 && !item->out()) || item->type() == HistoryItemGroup) {
				MsgId threshold = (item->id > 0) ? item->id : static_cast<HistoryGroup*>(item)->minId();
				if (threshold > msgId) {
					maxItem = startCheckItem;
				} else {
					minItem = checkItem;
				}
				break;
			}
			if (++checkItem == maxItem) {
				maxItem = startCheckItem;
				break;
			}
		}
		if (minItem + 1 == maxItem) {
			itemIndex = minItem;
			break;
		}
	}

	HistoryItem *item = block->items.at(itemIndex);
	if (item->type() != HistoryItemGroup) return 0;
	HistoryGroup *result = static_cast<HistoryGroup*>(item);
	return (result->minId() < msgId && result->maxId() > msgId) ? result : 0;
}

HistoryBlock *ChannelHistory::findGroupBlock(MsgId msgId) const { // find block with message group using binary search
	if (isEmpty()) return 0;

	int32 blockIndex = 0;
	if (blocks.size() > 1) for (int32 minBlock = 0, maxBlock = blocks.size();;) {
		for (int32 startCheckBlock = (minBlock + maxBlock) / 2, checkBlock = startCheckBlock;;) {
			HistoryBlock *block = blocks.at(checkBlock);
			HistoryBlock::Items::const_iterator i = block->items.cbegin(), e = block->items.cend();
			for (; i != e; ++i) { // out msgs could be a mess in monotonic ids
				if (((*i)->id > 0 && !(*i)->out()) || (*i)->type() == HistoryItemGroup) {
					MsgId threshold = ((*i)->id > 0) ? (*i)->id : static_cast<HistoryGroup*>(*i)->minId();
					if (threshold > msgId) {
						maxBlock = startCheckBlock;
					} else {
						minBlock = checkBlock;
					}
					break;
				}
			}
			if (i != e) {
				break;
			}
			if (++checkBlock == maxBlock) {
				maxBlock = startCheckBlock;
				break;
			}
		}
		if (minBlock + 1 == maxBlock) {
			blockIndex = minBlock;
			break;
		}
	}
	return blocks.at(blockIndex);
}

HistoryGroup *ChannelHistory::findGroupInOther(MsgId msgId) const { // find message group using binary search in _otherList
	if (_otherList.isEmpty()) return 0;
	int32 otherIndex = 0;
	if (_otherList.size() > 1) for (int32 minOther = 0, maxOther = _otherList.size();;) {
		for (int32 startCheckOther = (minOther + maxOther) / 2, checkOther = startCheckOther;;) {
			HistoryItem *item = _otherList.at(checkOther); // out msgs could be a mess in monotonic ids
			if ((item->id > 0 && !item->out()) || item->type() == HistoryItemGroup) {
				MsgId threshold = (item->id > 0) ? item->id : static_cast<HistoryGroup*>(item)->minId();
				if (threshold > msgId) {
					maxOther = startCheckOther;
				} else {
					minOther = checkOther;
				}
				break;
			}
			if (++checkOther == maxOther) {
				maxOther = startCheckOther;
				break;
			}
		}
		if (minOther + 1 == maxOther) {
			otherIndex = minOther;
			break;
		}
	}
	HistoryItem *item = _otherList.at(otherIndex);
	if (item->type() != HistoryItemGroup) return 0;
	HistoryGroup *result = static_cast<HistoryGroup*>(item);
	return (result->minId() < msgId && result->maxId() > msgId) ? result : 0;
}

HistoryItem *ChannelHistory::findPrevItem(HistoryItem *item) const {
	if (item->detached()) return 0;
	int32 itemIndex = item->block()->items.indexOf(item);
	int32 blockIndex = blocks.indexOf(item->block());
	if (itemIndex < 0 || blockIndex < 0) return 0;

	for (++blockIndex, ++itemIndex; blockIndex > 0;) {
		--blockIndex;
		HistoryBlock *block = blocks.at(blockIndex);
		if (!itemIndex) itemIndex = block->items.size();
		for (; itemIndex > 0;) {
			--itemIndex;
			if (block->items.at(itemIndex)->type() == HistoryItemMsg) {
				return block->items.at(itemIndex);
			}
		}
	}
	return 0;
}

void ChannelHistory::messageDetached(HistoryItem *msg) {
	if (_collapseMessage == msg) {
		_collapseMessage = 0;
	} else if (_joinedMessage == msg) {
		_joinedMessage = 0;
	}
}

void ChannelHistory::messageDeleted(HistoryItem *msg) {
	int32 otherIndex = _otherList.indexOf(msg);
	if (otherIndex >= 0) _otherList.removeAt(otherIndex);
	if (msg->isImportant()) { // unite message groups around this important message in _otherList
		if (!_onlyImportant && otherIndex > 0 && otherIndex < _otherList.size()) {
			if (HistoryGroup *groupPrev = (_otherList[otherIndex - 1]->type() == HistoryItemGroup) ? static_cast<HistoryGroup*>(_otherList[otherIndex - 1]) : 0) {
				if (HistoryGroup *groupNext = (_otherList[otherIndex]->type() == HistoryItemGroup) ? static_cast<HistoryGroup*>(_otherList[otherIndex]) : 0) {
					groupPrev->uniteWith(groupNext);
					groupNext->destroy();
				}
			}
		}
	} else {
		messageWithIdDeleted(msg->id);
	}
}

void ChannelHistory::messageWithIdDeleted(MsgId msgId) {
	if (HistoryGroup *group = findGroup(msgId)) {
		if (!group->decrementCount()) {
			group->destroy();
		}
	}
}

bool DialogsList::del(const PeerId &peerId, DialogRow *replacedBy) {
	RowByPeer::iterator i = rowByPeer.find(peerId);
	if (i == rowByPeer.cend()) return false;

	DialogRow *row = i.value();
	emit App::main()->dialogRowReplaced(row, replacedBy);

	if (row == current) {
		current = row->next;
	}
	for (DialogRow *change = row->next; change != end; change = change->next) {
		change->pos--;
	}
	end->pos--;
	remove(row);
	delete row;
	--count;
	rowByPeer.erase(i);

	return true;
}

void DialogsIndexed::peerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars) {
	if (sortMode == DialogsSortByName) {
		DialogRow *mainRow = list.adjustByName(peer);
		if (!mainRow) return;

		History *history = mainRow->history;

		PeerData::NameFirstChars toRemove = oldChars, toAdd;
		for (PeerData::NameFirstChars::const_iterator i = peer->chars.cbegin(), e = peer->chars.cend(); i != e; ++i) {
			PeerData::NameFirstChars::iterator j = toRemove.find(*i);
			if (j == toRemove.cend()) {
				toAdd.insert(*i);
			} else {
				toRemove.erase(j);
				DialogsIndex::iterator k = index.find(*i);
				if (k != index.cend()) {
					k.value()->adjustByName(peer);
				}
			}
		}
		for (PeerData::NameFirstChars::const_iterator i = toRemove.cbegin(), e = toRemove.cend(); i != e; ++i) {
			DialogsIndex::iterator j = index.find(*i);
			if (j != index.cend()) {
				j.value()->del(peer->id, mainRow);
			}
		}
		if (!toAdd.isEmpty()) {
			for (PeerData::NameFirstChars::const_iterator i = toAdd.cbegin(), e = toAdd.cend(); i != e; ++i) {
				DialogsIndex::iterator j = index.find(*i);
				if (j == index.cend()) {
					j = index.insert(*i, new DialogsList(sortMode));
				}
				j.value()->addByName(history);
			}
		}
	} else {
		DialogsList::RowByPeer::const_iterator i = list.rowByPeer.find(peer->id);
		if (i == list.rowByPeer.cend()) return;

		DialogRow *mainRow = i.value();
		History *history = mainRow->history;

		PeerData::NameFirstChars toRemove = oldChars, toAdd;
		for (PeerData::NameFirstChars::const_iterator i = peer->chars.cbegin(), e = peer->chars.cend(); i != e; ++i) {
			PeerData::NameFirstChars::iterator j = toRemove.find(*i);
			if (j == toRemove.cend()) {
				toAdd.insert(*i);
			} else {
				toRemove.erase(j);
			}
		}
		for (PeerData::NameFirstChars::const_iterator i = toRemove.cbegin(), e = toRemove.cend(); i != e; ++i) {
			if (sortMode == DialogsSortByDate) history->dialogs.remove(*i);
			DialogsIndex::iterator j = index.find(*i);
			if (j != index.cend()) {
				j.value()->del(peer->id, mainRow);
			}
		}
		for (PeerData::NameFirstChars::const_iterator i = toAdd.cbegin(), e = toAdd.cend(); i != e; ++i) {
			DialogsIndex::iterator j = index.find(*i);
			if (j == index.cend()) {
				j = index.insert(*i, new DialogsList(sortMode));
			}
			if (sortMode == DialogsSortByDate) {
				history->dialogs.insert(*i, j.value()->addToEnd(history));
			} else {
				j.value()->addToEnd(history);
			}
		}
	}
}

void DialogsIndexed::clear() {
	for (DialogsIndex::iterator i = index.begin(), e = index.end(); i != e; ++i) {
		delete i.value();
	}
	index.clear();
	list.clear();
}

History *Histories::find(const PeerId &peerId) {
	Map::const_iterator i = map.constFind(peerId);
	return (i == map.cend()) ? 0 : i.value();
}

History *Histories::findOrInsert(const PeerId &peerId, int32 unreadCount, int32 maxInboxRead) {
	Map::const_iterator i = map.constFind(peerId);
	if (i == map.cend()) {
		i = map.insert(peerId, peerIsChannel(peerId) ? static_cast<History*>(new ChannelHistory(peerId)) : (new History(peerId)));
		i.value()->setUnreadCount(unreadCount, false);
		i.value()->inboxReadBefore = maxInboxRead + 1;
	}
	return i.value();
}

void Histories::clear() {
	App::historyClearMsgs();
	for (Map::const_iterator i = map.cbegin(), e = map.cend(); i != e; ++i) {
		delete i.value();
	}
	App::historyClearItems();
	typing.clear();
	map.clear();
}

void Histories::regSendAction(History *history, UserData *user, const MTPSendMessageAction &action) {
	if (action.type() == mtpc_sendMessageCancelAction) {
		history->unregTyping(user);
		return;
	}

	uint64 ms = getms(true);
	switch (action.type()) {
	case mtpc_sendMessageTypingAction: history->typing[user] = ms + 6000; break;
	case mtpc_sendMessageRecordVideoAction: history->sendActions.insert(user, SendAction(SendActionRecordVideo, ms + 6000)); break;
	case mtpc_sendMessageUploadVideoAction: history->sendActions.insert(user, SendAction(SendActionUploadVideo, ms + 6000, action.c_sendMessageUploadVideoAction().vprogress.v)); break;
	case mtpc_sendMessageRecordAudioAction: history->sendActions.insert(user, SendAction(SendActionRecordAudio, ms + 6000)); break;
	case mtpc_sendMessageUploadAudioAction: history->sendActions.insert(user, SendAction(SendActionUploadAudio, ms + 6000, action.c_sendMessageUploadAudioAction().vprogress.v)); break;
	case mtpc_sendMessageUploadPhotoAction: history->sendActions.insert(user, SendAction(SendActionUploadPhoto, ms + 6000, action.c_sendMessageUploadPhotoAction().vprogress.v)); break;
	case mtpc_sendMessageUploadDocumentAction: history->sendActions.insert(user, SendAction(SendActionUploadFile, ms + 6000, action.c_sendMessageUploadDocumentAction().vprogress.v)); break;
	case mtpc_sendMessageGeoLocationAction: history->sendActions.insert(user, SendAction(SendActionChooseLocation, ms + 6000)); break;
	case mtpc_sendMessageChooseContactAction: history->sendActions.insert(user, SendAction(SendActionChooseContact, ms + 6000)); break;
	default: return;
	}

	user->madeAction();

	TypingHistories::const_iterator i = typing.find(history);
	if (i == typing.cend()) {
		typing.insert(history, ms);
		history->typingFrame = 0;
	}

	history->updateTyping(ms, history->typingFrame, true);
	anim::start(this);
}

bool Histories::animStep(float64) {
	uint64 ms = getms(true);
	for (TypingHistories::iterator i = typing.begin(), e = typing.end(); i != e;) {
		uint32 typingFrame = (ms - i.value()) / 150;
		i.key()->updateTyping(ms, typingFrame);
		if (i.key()->typing.isEmpty() && i.key()->sendActions.isEmpty()) {
			i = typing.erase(i);
		} else {
			++i;
		}
	}
	return !typing.isEmpty();
}

void Histories::remove(const PeerId &peer) {
	Map::iterator i = map.find(peer);
	if (i != map.cend()) {
		typing.remove(i.value());
		delete i.value();
		map.erase(i);
	}
}

HistoryItem *Histories::addNewMessage(const MTPMessage &msg, NewMessageType type) {
	PeerId peer = peerFromMessage(msg);
	if (!peer) return 0;

	return findOrInsert(peer, 0, 0)->addNewMessage(msg, type);
}

HistoryItem *History::createItem(HistoryBlock *block, const MTPMessage &msg, bool applyServiceAction, bool returnExisting) {
	HistoryItem *result = 0;

	MsgId msgId = 0;
	switch (msg.type()) {
	case mtpc_messageEmpty: msgId = msg.c_messageEmpty().vid.v; break;
	case mtpc_message: msgId = msg.c_message().vid.v; break;
	case mtpc_messageService: msgId = msg.c_messageService().vid.v; break;
	}

	HistoryItem *existing = App::histItemById(channelId(), msgId);
	if (existing) {
		bool regged = false;
		if (existing->detached() && block) {
			existing->attach(block);
			regged = true;
		}

		if (msg.type() == mtpc_message) {
			existing->updateMedia(msg.c_message().has_media() ? (&msg.c_message().vmedia) : 0, (block ? false : true));
		}
		return (returnExisting || regged) ? existing : 0;
	}

	switch (msg.type()) {
	case mtpc_messageEmpty:
		result = new HistoryServiceMsg(this, block, msg.c_messageEmpty().vid.v, date(), lang(lng_message_empty));
	break;

	case mtpc_message: {
		const MTPDmessage m(msg.c_message());
		int badMedia = 0; // 1 - unsupported, 2 - empty
		if (m.has_media()) switch (m.vmedia.type()) {
		case mtpc_messageMediaEmpty:
		case mtpc_messageMediaContact: break;
		case mtpc_messageMediaGeo:
			switch (m.vmedia.c_messageMediaGeo().vgeo.type()) {
			case mtpc_geoPoint: break;
			case mtpc_geoPointEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaVenue:
			switch (m.vmedia.c_messageMediaVenue().vgeo.type()) {
			case mtpc_geoPoint: break;
			case mtpc_geoPointEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaPhoto:
			switch (m.vmedia.c_messageMediaPhoto().vphoto.type()) {
			case mtpc_photo: break;
			case mtpc_photoEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaVideo:
			switch (m.vmedia.c_messageMediaVideo().vvideo.type()) {
			case mtpc_video: break;
			case mtpc_videoEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaAudio:
			switch (m.vmedia.c_messageMediaAudio().vaudio.type()) {
			case mtpc_audio: break;
			case mtpc_audioEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaDocument:
			switch (m.vmedia.c_messageMediaDocument().vdocument.type()) {
			case mtpc_document: break;
			case mtpc_documentEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaWebPage:
			switch (m.vmedia.c_messageMediaWebPage().vwebpage.type()) {
			case mtpc_webPage:
			case mtpc_webPageEmpty:
			case mtpc_webPagePending: break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaUnsupported:
		default: badMedia = 1; break;
		}
		if (badMedia) {
			result = new HistoryServiceMsg(this, block, m.vid.v, date(m.vdate), lang((badMedia == 2) ? lng_message_empty : lng_media_unsupported), m.vflags.v, 0, m.has_from_id() ? m.vfrom_id.v : 0);
		} else {
			if ((m.has_fwd_date() && m.vfwd_date.v > 0) || (m.has_fwd_from_id() && peerFromMTP(m.vfwd_from_id) != 0)) {
				result = new HistoryForwarded(this, block, m);
			} else if (m.has_reply_to_msg_id() && m.vreply_to_msg_id.v > 0) {
				result = new HistoryReply(this, block, m);
			} else {
				result = new HistoryMessage(this, block, m);
			}
			if (m.has_reply_markup()) {
				App::feedReplyMarkup(channelId(), msgId, m.vreply_markup);
			}
		}
	} break;

	case mtpc_messageService: {
		const MTPDmessageService &d(msg.c_messageService());
		result = new HistoryServiceMsg(this, block, d);

		if (applyServiceAction) {
			const MTPmessageAction &action(d.vaction);
			switch (d.vaction.type()) {
			case mtpc_messageActionChatAddUser: {
				const MTPDmessageActionChatAddUser &d(action.c_messageActionChatAddUser());
				// App::user(App::peerFromUser(d.vuser_id)); added
			} break;

			case mtpc_messageActionChatJoinedByLink: {
				const MTPDmessageActionChatJoinedByLink &d(action.c_messageActionChatJoinedByLink());
				// App::user(App::peerFromUser(d.vuser_id)); added
			} break;

			case mtpc_messageActionChatDeletePhoto: {
				ChatData *chat = peer->asChat();
				if (chat) chat->setPhoto(MTP_chatPhotoEmpty());
			} break;

			case mtpc_messageActionChatDeleteUser: {
				const MTPDmessageActionChatDeleteUser &d(action.c_messageActionChatDeleteUser());
				if (lastKeyboardFrom == peerFromUser(d.vuser_id)) {
					clearLastKeyboard();
				}
				// App::peer(App::peerFromUser(d.vuser_id)); left
			} break;

			case mtpc_messageActionChatEditPhoto: {
				const MTPDmessageActionChatEditPhoto &d(action.c_messageActionChatEditPhoto());
				if (d.vphoto.type() == mtpc_photo) {
					const QVector<MTPPhotoSize> &sizes(d.vphoto.c_photo().vsizes.c_vector().v);
					if (!sizes.isEmpty()) {
						PhotoData *photo = App::feedPhoto(d.vphoto.c_photo());
						if (photo) photo->peer = peer;
						const MTPPhotoSize &smallSize(sizes.front()), &bigSize(sizes.back());
						const MTPFileLocation *smallLoc = 0, *bigLoc = 0;
						switch (smallSize.type()) {
						case mtpc_photoSize: smallLoc = &smallSize.c_photoSize().vlocation; break;
						case mtpc_photoCachedSize: smallLoc = &smallSize.c_photoCachedSize().vlocation; break;
						}
						switch (bigSize.type()) {
						case mtpc_photoSize: bigLoc = &bigSize.c_photoSize().vlocation; break;
						case mtpc_photoCachedSize: bigLoc = &bigSize.c_photoCachedSize().vlocation; break;
						}
						if (smallLoc && bigLoc) {
							if (peer->isChat()) {
								peer->asChat()->setPhoto(MTP_chatPhoto(*smallLoc, *bigLoc), photo ? photo->id : 0);
							} else if (peer->isChannel()) {
								peer->asChannel()->setPhoto(MTP_chatPhoto(*smallLoc, *bigLoc), photo ? photo->id : 0);
							}
							peer->photo->load();
						}
					}
				}
			} break;

			case mtpc_messageActionChatEditTitle: {
				const MTPDmessageActionChatEditTitle &d(action.c_messageActionChatEditTitle());
				ChatData *chat = peer->asChat();
				if (chat) chat->updateName(qs(d.vtitle), QString(), QString());
			} break;
			}
		}
	} break;
	}

	return regItem(result, returnExisting);
}

HistoryItem *History::createItemForwarded(HistoryBlock *block, MsgId id, QDateTime date, int32 from, HistoryMessage *msg) {
	HistoryItem *result = 0;

	result = new HistoryForwarded(this, block, id, date, from, msg);

	return regItem(result);
}

HistoryItem *History::createItemDocument(HistoryBlock *block, MsgId id, int32 flags, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc) {
	HistoryItem *result = 0;

	if (flags & MTPDmessage::flag_reply_to_msg_id && replyTo > 0) {
		result = new HistoryReply(this, block, id, flags, replyTo, date, from, doc);
	} else {
		result = new HistoryMessage(this, block, id, flags, date, from, doc);
	}

	return regItem(result);
}

HistoryItem *History::addNewService(MsgId msgId, QDateTime date, const QString &text, int32 flags, HistoryMedia *media, bool newMsg) {
	HistoryBlock *to = 0;
	bool newBlock = blocks.isEmpty();
	if (newBlock) {
		to = new HistoryBlock(this);
	} else {
		to = blocks.back();
	}

	return addNewItem(to, newBlock, regItem(new HistoryServiceMsg(this, to, msgId, date, text, flags, media)), newMsg);
}

HistoryItem *History::addNewMessage(const MTPMessage &msg, NewMessageType type) {
	if (isChannel()) return asChannelHistory()->addNewChannelMessage(msg, type);

	if (type == NewMessageExisting) return addToHistory(msg);
	if (!loadedAtBottom()) {
		HistoryItem *item = addToHistory(msg);
		if (item) {
			setLastMessage(item);
			if (type == NewMessageUnread) {
				if (item->unread()) {
					newItemAdded(item);
				} else if (item->out()) {
					outboxRead(item);
				} else {
					inboxRead(item);
				}
			}
		}
		return item;
	}

	HistoryBlock *to = 0;
	bool newBlock = blocks.isEmpty();
	if (newBlock) {
		to = new HistoryBlock(this);
	} else {
		to = blocks.back();
	}
	return addNewItem(to, newBlock, createItem(to, msg, (type == NewMessageUnread)), (type == NewMessageUnread));
}

HistoryItem *History::addToHistory(const MTPMessage &msg) {
	return createItem(0, msg, false, true);
}

HistoryItem *History::addNewForwarded(MsgId id, QDateTime date, int32 from, HistoryMessage *item) {
	HistoryBlock *to = 0;
	bool newBlock = blocks.isEmpty();
	if (newBlock) {
		to = new HistoryBlock(this);
	} else {
		to = blocks.back();
	}
	return addNewItem(to, newBlock, createItemForwarded(to, id, date, from, item), true);
}

HistoryItem *History::addNewDocument(MsgId id, int32 flags, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc) {
	HistoryBlock *to = 0;
	bool newBlock = blocks.isEmpty();
	if (newBlock) {
		to = new HistoryBlock(this);
	} else {
		to = blocks.back();
	}
	return addNewItem(to, newBlock, createItemDocument(to, id, flags, replyTo, date, from, doc), true);
}

void History::createInitialDateBlock(const QDateTime &date) {
	HistoryBlock *dateBlock = new HistoryBlock(this); // date block
	HistoryItem *dayItem = createDayServiceMsg(this, dateBlock, date);
	dateBlock->items.push_back(dayItem);
	if (width) {
		int32 dh = dayItem->resize(width);
		dateBlock->height = dh;
		height += dh;
		for (int32 i = 0, l = blocks.size(); i < l; ++i) {
			blocks[i]->y += dh;
		}
	}
	blocks.push_front(dateBlock);
}

void History::addToOverview(HistoryItem *item, MediaOverviewType type) {
	if (overviewIds[type].constFind(item->id) == overviewIds[type].cend()) {
		overview[type].push_back(item->id);
		overviewIds[type].insert(item->id, NullType());
		if (overviewCount[type] > 0) ++overviewCount[type];
		if (App::wnd()) App::wnd()->mediaOverviewUpdated(peer, type);
	}
}

bool History::addToOverviewFront(HistoryItem *item, MediaOverviewType type) {
	if (overviewIds[type].constFind(item->id) == overviewIds[type].cend()) {
		overview[type].push_front(item->id);
		overviewIds[type].insert(item->id, NullType());
		return true;
	}
	return false;
}

HistoryItem *History::addNewItem(HistoryBlock *to, bool newBlock, HistoryItem *adding, bool newMsg) {
	if (!adding) {
		if (newBlock) delete to;
		return adding;
	}

	if (newBlock) {
		createInitialDateBlock(adding->date);

		to->y = height;
		blocks.push_back(to);
	} else if (to->items.back()->date.date() != adding->date.date()) {
		HistoryItem *dayItem = createDayServiceMsg(this, to, adding->date);
		to->items.push_back(dayItem);
		dayItem->y = to->height;
		if (width) {
			int32 dh = dayItem->resize(width);
			to->height += dh;
			height += dh;
		}
	}
	to->items.push_back(adding);
	setLastMessage(adding);

	adding->y = to->height;
	if (width) {
		int32 dh = adding->resize(width);
		to->height += dh;
		height += dh;
	}
	if (newMsg) {
		if (adding->unread()) {
			newItemAdded(adding);
		} else if (adding->out()) {
			outboxRead(adding);
		} else {
			inboxRead(adding);
		}
	}

	if (!isChannel() || adding->fromChannel()) {
		HistoryMedia *media = adding->getMedia(true);
		if (media) {
			HistoryMediaType mt = media->type();
			MediaOverviewType t = mediaToOverviewType(mt);
			if (t != OverviewCount) {
				if (mt == MediaTypeDocument && static_cast<HistoryDocument*>(media)->document()->song()) {
					addToOverview(adding, OverviewAudioDocuments);
				} else {
					addToOverview(adding, t);
				}
			}
		}
		if (adding->hasTextLinks()) {
			addToOverview(adding, OverviewLinks);
		}
	}
	if (adding->from()->id) {
		if (peer->isChat() && adding->from()->isUser()) {
			QList<UserData*> *lastAuthors = &(peer->asChat()->lastAuthors);
			int prev = lastAuthors->indexOf(adding->from()->asUser());
			if (prev > 0) {
				lastAuthors->removeAt(prev);
			}
			if (prev) {
				lastAuthors->push_front(adding->from()->asUser());
			}
		}
		if (adding->hasReplyMarkup()) {
			int32 markupFlags = App::replyMarkup(channelId(), adding->id).flags;
			if (!(markupFlags & MTPDreplyKeyboardMarkup_flag_personal) || adding->notifyByFrom()) {
				if (peer->isChat()) {
					peer->asChat()->markupSenders.insert(adding->from(), true);
				}
				if (markupFlags & MTPDreplyKeyboardMarkup_flag_ZERO) { // zero markup means replyKeyboardHide
					if (lastKeyboardFrom == adding->from()->id || (!lastKeyboardInited && !peer->isChat() && !adding->out())) {
						clearLastKeyboard();
					}
				} else if (peer->isChat() && adding->from()->isUser() && (peer->asChat()->count < 1 || !peer->asChat()->participants.isEmpty()) && !peer->asChat()->participants.contains(adding->from()->asUser())) {
					clearLastKeyboard();
				} else {
					lastKeyboardInited = true;
					lastKeyboardId = adding->id;
					lastKeyboardFrom = adding->from()->id;
					lastKeyboardUsed = false;
				}
			}
		}
	}

	return adding;
}

void History::unregTyping(UserData *from) {
	uint64 updateAtMs = 0;
	TypingUsers::iterator i = typing.find(from);
	if (i != typing.end()) {
		updateAtMs = getms(true);
		i.value() = updateAtMs;
	}
	SendActionUsers::iterator j = sendActions.find(from);
	if (j != sendActions.end()) {
		if (!updateAtMs) updateAtMs = getms(true);
		j.value().until = updateAtMs;
	}
	if (updateAtMs) {
		updateTyping(updateAtMs, 0, true);
	}
}

void History::newItemAdded(HistoryItem *item) {
	App::checkImageCacheSize();
	if (item->from() && item->from()->isUser()) {
		unregTyping(item->from()->asUser());
		item->from()->asUser()->madeAction();
	}
	if (item->out()) {
		if (unreadBar) unreadBar->destroy();
	} else if (item->unread()) {
		bool skip = false;
		if (!isChannel() || peer->asChannel()->amIn()) {
			notifies.push_back(item);
			App::main()->newUnreadMsg(this, item);
		}
	}
}

HistoryItem *History::addItemAfterPrevToBlock(HistoryItem *item, HistoryItem *prev, HistoryBlock *block) {
	if (prev && prev->date.date() != item->date.date()) {
		HistoryItem *dayItem = createDayServiceMsg(this, prev->block(), item->date);
		prev->block()->items.push_back(dayItem);
		if (width) {
			dayItem->y = prev->block()->height;
			prev->block()->height += dayItem->resize(width);
			if (prev->block() != block) {
				height += dayItem->height();
			}
		}
	}
	block->items.push_back(item);
	if (width) {
		item->y = block->height;
		block->height += item->resize(width);
	}
	return item;
}

HistoryItem *History::addMessageGroupAfterPrevToBlock(const MTPDmessageGroup &group, HistoryItem *prev, HistoryBlock *block) {
	if (prev && prev->type() == HistoryItemGroup) {
		static_cast<HistoryGroup*>(prev)->uniteWith(group.vmin_id.v, group.vmax_id.v, group.vcount.v);
		return prev;
	}
	return addItemAfterPrevToBlock(regItem(new HistoryGroup(this, block, group, prev ? prev->date : date(group.vdate))), prev, block);
}

HistoryItem *History::addMessageGroupAfterPrev(HistoryItem *newItem, HistoryItem *prev) {
	if (prev && prev->type() == HistoryItemGroup) {
		static_cast<HistoryGroup*>(prev)->uniteWith(newItem);
		return prev;
	}

	QDateTime date = prev ? prev->date : newItem->date;
	HistoryBlock *block = prev ? prev->block() : 0;
	if (!block) {
		createInitialDateBlock(date);

		block = new HistoryBlock(this);
		block->y = height;
		blocks.push_back(block);
	}
	return addItemAfterPrevToBlock(regItem(new HistoryGroup(this, block, newItem, date)), prev, block);
}

void History::addOlderSlice(const QVector<MTPMessage> &slice, const QVector<MTPMessageGroup> *collapsed) {
	if (slice.isEmpty()) {
		oldLoaded = true;
		if (!collapsed || collapsed->isEmpty() || !isChannel()) {
			if (isChannel()) {
				asChannelHistory()->checkJoinedMessage();
				asChannelHistory()->checkMaxReadMessageDate();
			}
			return;
		}
	}

	const MTPMessageGroup *groupsBegin = (isChannel() && collapsed) ? collapsed->constData() : 0, *groupsIt = groupsBegin, *groupsEnd = (isChannel() && collapsed) ? (groupsBegin + collapsed->size()) : 0;

	int32 addToH = 0, skip = 0;
	if (!blocks.isEmpty()) { // remove date block
		if (width) addToH = -blocks.front()->height;
		delete blocks.front();
		blocks.pop_front();
	}
	HistoryItem *till = blocks.isEmpty() ? 0 : blocks.front()->items.front(), *prev = 0;

	HistoryBlock *block = new HistoryBlock(this);
	block->items.reserve(slice.size() + (collapsed ? collapsed->size() : 0));
	for (QVector<MTPmessage>::const_iterator i = slice.cend(), e = slice.cbegin(); i != e;) {
		--i;
		HistoryItem *adding = createItem(block, *i, false);
		if (!adding) continue;

		for (; groupsIt != groupsEnd; ++groupsIt) {
			if (groupsIt->type() != mtpc_messageGroup) continue;
			const MTPDmessageGroup &group(groupsIt->c_messageGroup());
			if (group.vmin_id.v >= adding->id) break;

			prev = addMessageGroupAfterPrevToBlock(group, prev, block);
		}

		prev = addItemAfterPrevToBlock(adding, prev, block);
	}
	for (; groupsIt != groupsEnd; ++groupsIt) {
		if (groupsIt->type() != mtpc_messageGroup) continue;
		const MTPDmessageGroup &group(groupsIt->c_messageGroup());

		prev = addMessageGroupAfterPrevToBlock(group, prev, block);
	}

	while (till && prev && till->type() == HistoryItemGroup && prev->type() == HistoryItemGroup) {
		static_cast<HistoryGroup*>(prev)->uniteWith(static_cast<HistoryGroup*>(till));
		till->detach();
		delete till;
		if (blocks.front()->items.isEmpty()) {
			delete blocks.front();
			blocks.pop_front();
		}
		till = blocks.isEmpty() ? 0 : blocks.front()->items.front();
	}
	if (till && prev && prev->date.date() != till->date.date()) {
		HistoryItem *dayItem = createDayServiceMsg(this, block, till->date);
		block->items.push_back(dayItem);
		if (width) {
			dayItem->y = block->height;
			block->height += dayItem->resize(width);
		}
	}
	if (!block->items.isEmpty()) {
		blocks.push_front(block);
		if (width) {
			addToH += block->height;
			++skip;
		}

		if (loadedAtBottom()) { // add photos to overview and authors to lastAuthors
			bool channel = isChannel();
			int32 mask = 0;
			QList<UserData*> *lastAuthors = peer->isChat() ? &(peer->asChat()->lastAuthors) : 0;
			for (int32 i = block->items.size(); i > 0; --i) {
				HistoryItem *item = block->items[i - 1];
				if (channel && !item->fromChannel()) continue;

				HistoryMedia *media = item->getMedia(true);
				if (media) {
					HistoryMediaType mt = media->type();
					MediaOverviewType t = mediaToOverviewType(mt);
					if (t != OverviewCount) {
						if (mt == MediaTypeDocument && static_cast<HistoryDocument*>(media)->document()->song()) {
							if (addToOverviewFront(item, OverviewAudioDocuments)) mask |= (1 << OverviewAudioDocuments);
						} else {
							if (addToOverviewFront(item, t)) mask |= (1 << t);
						}
					}
				}
				if (item->hasTextLinks()) {
					if (addToOverviewFront(item, OverviewLinks)) mask |= (1 << OverviewLinks);
				}
				if (item->from()->id) {
					if (lastAuthors) { // chats
						if (item->from()->isUser() && !lastAuthors->contains(item->from()->asUser())) {
							lastAuthors->push_back(item->from()->asUser());
						}
						if (!lastKeyboardInited && item->hasReplyMarkup() && !item->out()) { // chats with bots
							int32 markupFlags = App::replyMarkup(channelId(), item->id).flags;
							if (!(markupFlags & MTPDreplyKeyboardMarkup_flag_personal) || item->notifyByFrom()) {
								bool wasKeyboardHide = peer->asChat()->markupSenders.contains(item->from());
								if (!wasKeyboardHide) {
									peer->asChat()->markupSenders.insert(item->from(), true);
								}
								if (!(markupFlags & MTPDreplyKeyboardMarkup_flag_ZERO)) {
									if (!lastKeyboardInited) {
										if (wasKeyboardHide || ((peer->asChat()->count < 1 || !peer->asChat()->participants.isEmpty()) && item->from()->isUser() && !peer->asChat()->participants.contains(item->from()->asUser()))) {
											clearLastKeyboard();
										} else {
											lastKeyboardInited = true;
											lastKeyboardId = item->id;
											lastKeyboardFrom = item->from()->id;
											lastKeyboardUsed = false;
										}
									}
								}
							}
						}
					} else if (!lastKeyboardInited && item->hasReplyMarkup() && !item->out()) { // conversations with bots
						int32 markupFlags = App::replyMarkup(channelId(), item->id).flags;
						if (!(markupFlags & MTPDreplyKeyboardMarkup_flag_personal) || item->notifyByFrom()) {
							if (markupFlags & MTPDreplyKeyboardMarkup_flag_ZERO) {
								clearLastKeyboard();
							} else {
								lastKeyboardInited = true;
								lastKeyboardId = item->id;
								lastKeyboardFrom = item->from()->id;
								lastKeyboardUsed = false;
							}
						}
					}
				}
			}
			for (int32 t = 0; t < OverviewCount; ++t) {
				if ((mask & (1 << t)) && App::wnd()) App::wnd()->mediaOverviewUpdated(peer, MediaOverviewType(t));
			}
		}
	} else {
		delete block;
	}
	if (!blocks.isEmpty()) {
		HistoryBlock *dateBlock = new HistoryBlock(this);
		HistoryItem *dayItem = createDayServiceMsg(this, dateBlock, blocks.front()->items.front()->date);
		dateBlock->items.push_back(dayItem);
		if (width) {
			int32 dh = dayItem->resize(width);
			dateBlock->height = dh;
			if (skip) {
				blocks.front()->y += dh;
			}
			addToH += dh;
			++skip;
		}
		blocks.push_front(dateBlock); // date block
	}
	if (width && addToH) {
		for (Blocks::iterator i = blocks.begin(), e = blocks.end(); i != e; ++i) {
			if (skip) {
				--skip;
			} else {
				(*i)->y += addToH;
			}
		}
		height += addToH;
	}

	if (isChannel()) {
		asChannelHistory()->checkJoinedMessage();
		asChannelHistory()->checkMaxReadMessageDate();
	}
	if (newLoaded && !lastMsg) setLastMessage(lastImportantMessage());
}

void History::addNewerSlice(const QVector<MTPMessage> &slice, const QVector<MTPMessageGroup> *collapsed) {
	bool wasEmpty = isEmpty(), wasLoadedAtBottom = loadedAtBottom();

	if (slice.isEmpty()) {
		newLoaded = true;
		if (!lastMsg) setLastMessage(lastImportantMessage());
	}

	if (!slice.isEmpty() || (isChannel() && collapsed && !collapsed->isEmpty())) {
		const MTPMessageGroup *groupsBegin = (isChannel() && collapsed) ? collapsed->constData() : 0, *groupsIt = groupsBegin, *groupsEnd = (isChannel() && collapsed) ? (groupsBegin + collapsed->size()) : 0;

		HistoryItem *prev = blocks.isEmpty() ? 0 : blocks.back()->items.back();

		HistoryBlock *block = new HistoryBlock(this);
		block->items.reserve(slice.size() + (collapsed ? collapsed->size() : 0));
		for (QVector<MTPmessage>::const_iterator i = slice.cend(), e = slice.cbegin(); i != e;) {
			--i;
			HistoryItem *adding = createItem(block, *i, false);
			if (!adding) continue;

			for (; groupsIt != groupsEnd; ++groupsIt) {
				if (groupsIt->type() != mtpc_messageGroup) continue;
				const MTPDmessageGroup &group(groupsIt->c_messageGroup());
				if (group.vmin_id.v >= adding->id) break;

				prev = addMessageGroupAfterPrevToBlock(group, prev, block);
			}

			prev = addItemAfterPrevToBlock(adding, prev, block);
		}
		for (; groupsIt != groupsEnd; ++groupsIt) {
			if (groupsIt->type() != mtpc_messageGroup) continue;
			const MTPDmessageGroup &group(groupsIt->c_messageGroup());

			prev = addMessageGroupAfterPrevToBlock(group, prev, block);
		}

		if (block->items.size()) {
			block->y = height;
			blocks.push_back(block);
			height += block->height;
		} else {
			newLoaded = true;
			setLastMessage(lastImportantMessage());
			delete block;
		}
	}
	if (!wasLoadedAtBottom && loadedAtBottom()) { // add all loaded photos to overview
		int32 mask = 0;
		for (int32 i = 0; i < OverviewCount; ++i) {
			if (overviewCount[i] == 0) continue; // all loaded
			if (!overview[i].isEmpty() || !overviewIds[i].isEmpty()) {
				overview[i].clear();
				overviewIds[i].clear();
				mask |= (1 << i);
			}
		}
		bool channel = isChannel();
		for (int32 i = 0; i < blocks.size(); ++i) {
			HistoryBlock *b = blocks[i];
			for (int32 j = 0; j < b->items.size(); ++j) {
				HistoryItem *item = b->items[j];
				if (channel && !item->fromChannel()) continue;

				HistoryMedia *media = item->getMedia(true);
				if (media) {
					HistoryMediaType mt = media->type();
					MediaOverviewType t = mediaToOverviewType(mt);
					if (t != OverviewCount) {
						if (mt == MediaTypeDocument && static_cast<HistoryDocument*>(media)->document()->song()) {
							t = OverviewAudioDocuments;
							if (overviewCount[t] != 0) {
								overview[t].push_back(item->id);
								overviewIds[t].insert(item->id, NullType());
								mask |= (1 << t);
							}
						} else {
							if (overviewCount[t] != 0) {
								overview[t].push_back(item->id);
								overviewIds[t].insert(item->id, NullType());
								mask |= (1 << t);
							}
						}
					}
				}
				if (item->hasTextLinks()) {
					MediaOverviewType t = OverviewLinks;
					if (overviewCount[t] != 0) {
						overview[t].push_back(item->id);
						overviewIds[t].insert(item->id, NullType());
						mask |= (1 << t);
					}
				}
			}
		}
		for (int32 t = 0; t < OverviewCount; ++t) {
			if ((mask & (1 << t)) && App::wnd()) App::wnd()->mediaOverviewUpdated(peer, MediaOverviewType(t));
		}
	}
	if (wasEmpty && !isEmpty()) {
		HistoryBlock *dateBlock = new HistoryBlock(this);
		HistoryItem *dayItem = createDayServiceMsg(this, dateBlock, blocks.front()->items.front()->date);
		dateBlock->items.push_back(dayItem);
		int32 dh = dayItem->resize(width);
		dateBlock->height = dh;
		for (Blocks::iterator i = blocks.begin(), e = blocks.end(); i != e; ++i) {
			(*i)->y += dh;
		}
		blocks.push_front(dateBlock); // date block
		height += dh;
	}

	if (isChannel()) asChannelHistory()->checkJoinedMessage();
}

int32 History::countUnread(MsgId upTo) {
	int32 result = 0;
	for (Blocks::const_iterator i = blocks.cend(), e = blocks.cbegin(); i != e;) {
		--i;
		for (HistoryBlock::Items::const_iterator j = (*i)->items.cend(), en = (*i)->items.cbegin(); j != en;) {
			--j;
			if ((*j)->id > 0 && (*j)->id <= upTo) {
				break;
			} else if (!(*j)->out() && (*j)->unread() && (*j)->id > upTo) {
				++result;
			}
		}
	}
	return result;
}

void History::updateShowFrom() {
	if (showFrom) return;

	for (Blocks::const_iterator i = blocks.cend(); i != blocks.cbegin();) {
		--i;
		for (HistoryBlock::Items::const_iterator j = (*i)->items.cend(); j != (*i)->items.cbegin();) {
			--j;
			if ((*j)->type() == HistoryItemMsg && (*j)->id > 0 && (!(*j)->out() || !showFrom)) {
				if ((*j)->id >= inboxReadBefore) {
					showFrom = *j;
				} else {
					return;
				}
			}
		}
	}
}

MsgId History::inboxRead(MsgId upTo) {
	if (upTo < 0) return upTo;
	if (unreadCount) {
		if (upTo && loadedAtBottom()) App::main()->historyToDown(this);
		setUnreadCount(upTo ? countUnread(upTo) : 0);
	}

	if (!upTo) upTo = msgIdForRead();
	inboxReadBefore = qMax(inboxReadBefore, upTo + 1);

	if (!dialogs.isEmpty() && App::main()) App::main()->dlgUpdated(dialogs[0]);

	showFrom = 0;
	App::wnd()->notifyClear(this);
	clearNotifications();

	return upTo;
}

MsgId History::inboxRead(HistoryItem *wasRead) {
	return inboxRead(wasRead ? wasRead->id : 0);
}

MsgId History::outboxRead(int32 upTo) {
	if (upTo < 0) return upTo;
	if (!upTo) upTo = msgIdForRead();
	if (outboxReadBefore < upTo + 1) outboxReadBefore = upTo + 1;

	return upTo;
}

MsgId History::outboxRead(HistoryItem *wasRead) {
	return outboxRead(wasRead ? wasRead->id : 0);
}

HistoryItem *History::lastImportantMessage() const {
	if (isEmpty()) return 0;
	bool channel = isChannel();
	for (int32 blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int32 itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			if (channel ? item->isImportant() : (item->type() == HistoryItemMsg)) {
				return item;
			}
		}
	}
	return 0;
}

void History::setUnreadCount(int32 newUnreadCount, bool psUpdate) {
	if (unreadCount != newUnreadCount) {
		if (newUnreadCount == 1) {
			if (loadedAtBottom()) showFrom = lastImportantMessage();
			inboxReadBefore = qMax(inboxReadBefore, msgIdForRead());
		} else if (!newUnreadCount) {
			showFrom = 0;
			inboxReadBefore = qMax(inboxReadBefore, msgIdForRead() + 1);
		}
		App::histories().unreadFull += newUnreadCount - unreadCount;
		if (mute) App::histories().unreadMuted += newUnreadCount - unreadCount;
		unreadCount = newUnreadCount;
		if (psUpdate && (!mute || cIncludeMuted())) App::wnd()->updateCounter();
		if (unreadBar) unreadBar->setCount(unreadCount);
	}
}

 void History::setMute(bool newMute) {
	if (mute != newMute) {
		App::histories().unreadMuted += newMute ? unreadCount : (-unreadCount);
		mute = newMute;
		if (App::wnd()) App::wnd()->updateCounter();
		if (!dialogs.isEmpty() && App::main()) App::main()->dlgUpdated(dialogs[0]);
	}
}

void History::getNextShowFrom(HistoryBlock *block, int32 i) {
	if (i >= 0) {
		int32 l = block->items.size();
		for (++i; i < l; ++i) {
			if (block->items[i]->type() == HistoryItemMsg) {
				showFrom = block->items[i];
				return;
			}
		}
	}

	int32 j = blocks.indexOf(block), s = blocks.size();
	if (j >= 0) {
		for (++j; j < s; ++j) {
			block = blocks[j];
			for (int32 i = 0, l = block->items.size(); i < l; ++i) {
				if (block->items[i]->type() == HistoryItemMsg) {
					showFrom = block->items[i];
					return;
				}
			}
		}
	}
	showFrom = 0;
}

void History::addUnreadBar() {
	if (unreadBar || !showFrom || showFrom->detached() || !unreadCount) return;
	
	HistoryBlock *block = showFrom->block();
	unreadBar = new HistoryUnreadBar(this, block, unreadCount, showFrom->date);
	if (!addNewInTheMiddle(unreadBar, blocks.indexOf(block), block->items.indexOf(showFrom))) {
		unreadBar = 0;
	}
}

HistoryItem *History::addNewInTheMiddle(HistoryItem *newItem, int32 blockIndex, int32 itemIndex) {
	if (!regItem(newItem)) {
		return 0;
	}
	if (blockIndex < 0 || itemIndex < 0 || blockIndex >= blocks.size() || itemIndex > blocks.at(blockIndex)->items.size()) {
		delete newItem;
		return 0;
	}

	HistoryBlock *block = blocks.at(blockIndex);
	newItem->y = (itemIndex < block->items.size()) ? block->items.at(itemIndex)->y : block->height;
	block->items.insert(itemIndex, newItem);

	if (width) {
		int32 dh = newItem->resize(width), l = block->items.size();
		for (++itemIndex; itemIndex < l; ++itemIndex) {
			block->items[itemIndex]->y += dh;
		}
		block->height += dh;
		for (++blockIndex, l = blocks.size(); blockIndex < l; ++blockIndex) {
			blocks[blockIndex]->y += dh;
		}
		height += dh;
	}
	return newItem;
}

void History::clearNotifications() {
	notifies.clear();
}

bool History::loadedAtBottom() const {
	return newLoaded;
}

bool History::loadedAtTop() const {
	return oldLoaded;
}

bool History::isReadyFor(MsgId msgId, MsgId &fixInScrollMsgId, int32 &fixInScrollMsgTop) {
	if (msgId != ShowAtTheEndMsgId && msgId != ShowAtUnreadMsgId && isChannel()) {
		return asChannelHistory()->isSwitchReadyFor(msgId, fixInScrollMsgId, fixInScrollMsgTop);
	}
	fixInScrollMsgId = 0;
	fixInScrollMsgTop = 0;
	if (msgId == ShowAtTheEndMsgId) {
		return loadedAtBottom();
	}
	if (msgId == ShowAtUnreadMsgId) {
		if (unreadCount) {
			if (!isEmpty()) {
				return (loadedAtTop() || minMsgId() <= inboxReadBefore) && (loadedAtBottom() || maxMsgId() >= inboxReadBefore);
			}
			return false;
		}
		return loadedAtBottom();
	}
	HistoryItem *item = App::histItemById(channelId(), msgId);
	return item && (item->history() == this) && !item->detached();
}

void History::getReadyFor(MsgId msgId, MsgId &fixInScrollMsgId, int32 &fixInScrollMsgTop) {
	if (msgId != ShowAtTheEndMsgId && msgId != ShowAtUnreadMsgId && isChannel()) {
		return asChannelHistory()->getSwitchReadyFor(msgId, fixInScrollMsgId, fixInScrollMsgTop);
	}
	if (!isReadyFor(msgId, fixInScrollMsgId, fixInScrollMsgTop)) {
		clear(true);
		newLoaded = (msgId == ShowAtTheEndMsgId);
		oldLoaded = false;
		lastWidth = 0;
	}
}

void History::setNotLoadedAtBottom() {
	newLoaded = false;
}

namespace {
	uint32 _dialogsPosToTopShift = 0x80000000UL;
}

inline uint64 dialogPosFromDate(const QDateTime &date) {
	return (uint64(date.toTime_t()) << 32) | (++_dialogsPosToTopShift);
}

void History::setLastMessage(HistoryItem *msg) {
	if (msg) {
		if (!lastMsg) Local::removeSavedPeer(peer);
		lastMsg = msg;
		setPosInDialogsDate(msg->date);
	} else {
		lastMsg = 0;
	}
	if (!dialogs.isEmpty() && App::main()) App::main()->dlgUpdated(dialogs[0]);
}

void History::setPosInDialogsDate(const QDateTime &date) {
	bool updateDialog = (App::main() && (!peer->isChannel() || peer->asChannel()->amIn()));
	if (!lastMsgDate.isNull() && lastMsgDate >= date) {
		if (!updateDialog || !dialogs.isEmpty()) {
			return;
		}
	}
	lastMsgDate = date;
	posInDialogs = dialogPosFromDate(lastMsgDate);
	if (updateDialog) {
		App::main()->createDialog(this);
	}
}

void History::fixLastMessage(bool wasAtBottom) {
	setLastMessage(wasAtBottom ? lastImportantMessage() : 0);
}

MsgId History::minMsgId() const {
	for (Blocks::const_iterator i = blocks.cbegin(), e = blocks.cend(); i != e; ++i) {
		for (HistoryBlock::Items::const_iterator j = (*i)->items.cbegin(), en = (*i)->items.cend(); j != en; ++j) {
			if ((*j)->id > 0) {
				return (*j)->id;
			}
		}
	}
	return 0;
}

MsgId History::maxMsgId() const {
	for (Blocks::const_iterator i = blocks.cend(), e = blocks.cbegin(); i != e;) {
		--i;
		for (HistoryBlock::Items::const_iterator j = (*i)->items.cend(), en = (*i)->items.cbegin(); j != en;) {
			--j;
			if ((*j)->id > 0) {
				return (*j)->id;
			}
		}
	}
	return 0;
}

MsgId History::msgIdForRead() const {
	MsgId result = (lastMsg && lastMsg->id > 0) ? lastMsg->id : 0;
	if (loadedAtBottom()) result = qMax(result, maxMsgId());
	return result;
}

int32 History::geomResize(int32 newWidth, int32 *ytransform, HistoryItem *resizedItem) {
	if (width != newWidth) resizedItem = 0; // recount all items
	if (width != newWidth || resizedItem) {
		int32 y = 0;
		for (Blocks::iterator i = blocks.begin(), e = blocks.end(); i != e; ++i) {
			HistoryBlock *block = *i;
			bool updTransform = ytransform && (*ytransform >= block->y) && (*ytransform < block->y + block->height);
			if (updTransform) *ytransform -= block->y;
			if (block->y != y) {
				block->y = y;
			}
			y += block->geomResize(newWidth, ytransform, resizedItem);
			if (updTransform) {
				*ytransform += block->y;
				ytransform = 0;
			}
		}
		width = newWidth;
		height = y;
	}
	return height;
}

ChannelHistory *History::asChannelHistory() {
	return isChannel() ? static_cast<ChannelHistory*>(this) : 0;
}

const ChannelHistory *History::asChannelHistory() const {
	return isChannel() ? static_cast<const ChannelHistory*>(this) : 0;
}

void History::clear(bool leaveItems) {
	if (unreadBar) {
		unreadBar->destroy();
	}
	if (showFrom) {
		showFrom = 0;
	}
	if (!leaveItems) {
		setLastMessage(0);
	}
	for (int32 i = 0; i < OverviewCount; ++i) {
		if (!overview[i].isEmpty() || !overviewIds[i].isEmpty()) {
			if (leaveItems) {
				if (overviewCount[i] == 0) overviewCount[i] = overview[i].size();
			} else {
				overviewCount[i] = -1; // not loaded yet
			}
			overview[i].clear();
			overviewIds[i].clear();
			if (App::wnd() && !App::quiting()) App::wnd()->mediaOverviewUpdated(peer, MediaOverviewType(i));
		}
	}
	for (Blocks::const_iterator i = blocks.cbegin(), e = blocks.cend(); i != e; ++i) {
		if (leaveItems) {
			(*i)->clear(true);
		}
		delete *i;
	}
	blocks.clear();
	if (leaveItems) {
		lastKeyboardInited = false;
	} else {
		setUnreadCount(0);
	}
	height = 0;
	oldLoaded = false;
	if (peer->isChat()) {
		peer->asChat()->lastAuthors.clear();
		peer->asChat()->markupSenders.clear();
	} else if (isChannel()) {
		asChannelHistory()->cleared();
	}
	if (leaveItems && App::main()) App::main()->historyCleared(this);
}

void History::blockResized(HistoryBlock *block, int32 dh) {
	int32 i = blocks.indexOf(block), l = blocks.size();
	if (i >= 0) {
		for (++i; i < l; ++i) {
			blocks[i]->y -= dh;
		}
		height -= dh;
	}
}

void History::removeBlock(HistoryBlock *block) {
	int32 i = blocks.indexOf(block), h = block->height;
	if (i >= 0) {
		blocks.removeAt(i);
		int32 l = blocks.size();
		if (i > 0 && l == 1) { // only fake block with date left
			removeBlock(blocks[0]);
			height = 0;
		} else if (h) {
			for (; i < l; ++i) {
				blocks[i]->y -= h;
			}
			height -= h;
		}
	}
	delete block;
}

int32 HistoryBlock::geomResize(int32 newWidth, int32 *ytransform, HistoryItem *resizedItem) {
	int32 y = 0;
	for (Items::iterator i = items.begin(), e = items.end(); i != e; ++i) {
		HistoryItem *item = *i;
		bool updTransform = ytransform && (*ytransform >= item->y) && (*ytransform < item->y + item->height());
		if (updTransform) *ytransform -= item->y;
		item->y = y;
		if (!resizedItem || resizedItem == item) {
			y += item->resize(newWidth);
		} else {
			y += item->height();
		}
		if (updTransform) {
			*ytransform += item->y;
			ytransform = 0;
		}
	}
	height = y;
	return height;
}

void HistoryBlock::clear(bool leaveItems) {
	if (leaveItems) {
		for (Items::const_iterator i = items.cbegin(), e = items.cend(); i != e; ++i) {
			(*i)->detachFast();
		}
	} else {
		for (Items::const_iterator i = items.cbegin(), e = items.cend(); i != e; ++i) {
			delete *i;
		}
	}
	items.clear();
}

void HistoryBlock::removeItem(HistoryItem *item) {
	int32 i = items.indexOf(item), dh = 0;
	if (history->showFrom == item) {
		history->getNextShowFrom(this, i);
	}
	if (i < 0) {
		return;
	}

	bool createInitialDate = false;
	QDateTime initialDateTime;
	int32 myIndex = history->blocks.indexOf(this);
	if (myIndex >= 0 && item->type() != HistoryItemDate) { // fix message groups and date items
		if (item->isImportant()) { // unite message groups around this important message
			HistoryGroup *nextGroup = 0, *prevGroup = 0;
			HistoryCollapse *nextCollapse = 0;
			HistoryItem *prevItem = 0;
			for (int32 nextBlock = myIndex, nextIndex = qMin(items.size(), i + 1); nextBlock < history->blocks.size(); ++nextBlock) {
				HistoryBlock *block = history->blocks.at(nextBlock);
				for (; nextIndex < block->items.size(); ++nextIndex) {
					HistoryItem *item = block->items.at(nextIndex);
					if (item->type() == HistoryItemMsg) {
						break;
					} else if (item->type() == HistoryItemGroup) {
						nextGroup = static_cast<HistoryGroup*>(item);
						break;
					} else if (item->type() == HistoryItemCollapse) {
						nextCollapse = static_cast<HistoryCollapse*>(item);
						break;
					}
				}
				if (nextIndex == block->items.size()) {
					nextIndex = 0;
				} else {
					break;
				}
			}
			for (int32 prevBlock = myIndex + 1, prevIndex = qMax(1, i); prevBlock > 0;) {
				--prevBlock;
				HistoryBlock *block = history->blocks.at(prevBlock);
				if (!prevIndex) prevIndex = block->items.size();
				for (; prevIndex > 0;) {
					--prevIndex;
					HistoryItem *item = block->items.at(prevIndex);
					if (item->type() == HistoryItemMsg || item->type() == HistoryItemCollapse) {
						prevItem = item;
						++prevIndex;
						break;
					} else if (item->type() == HistoryItemGroup) {
						prevGroup = static_cast<HistoryGroup*>(item);
						++prevIndex;
						break;
					}
				}
				if (prevIndex != 0) {
					break;
				}
			}
			if (nextGroup && prevGroup) {
				prevGroup->uniteWith(nextGroup);
				nextGroup->destroy();
			} else if (nextCollapse && (!prevItem || !prevItem->isImportant())) {
				nextCollapse->destroy();
			}
		}
		
		// fix date items
		HistoryItem *nextItem = (i < items.size() - 1) ? items[i + 1] : ((myIndex < history->blocks.size() - 1) ? history->blocks[myIndex + 1]->items[0] : 0);
		if (nextItem && nextItem == history->unreadBar) { // skip unread bar
			if (i < items.size() - 2) {
				nextItem = items[i + 2];
			} else if (i < items.size() - 1) {
				nextItem = ((myIndex < history->blocks.size() - 1) ? history->blocks[myIndex + 1]->items[0] : 0);
			} else if (myIndex < history->blocks.size() - 1) {
				if (0 < history->blocks[myIndex + 1]->items.size() - 1) {
					nextItem = history->blocks[myIndex + 1]->items[1];
				} else if (myIndex < history->blocks.size() - 2) {
					nextItem = history->blocks[myIndex + 2]->items[0];
				} else {
					nextItem = 0;
				}
			} else {
				nextItem = 0;
			}
		}
		if (!nextItem || nextItem->type() == HistoryItemDate) { // only if there is no next item or it is a date item
			HistoryItem *prevItem = (i > 0) ? items[i - 1] : 0;
			if (prevItem && prevItem == history->unreadBar) { // skip unread bar
				prevItem = (i > 1) ? items[i - 2] : 0;
			}
			if (prevItem) {
				if (prevItem->type() == HistoryItemDate) {
					prevItem->destroy();
					--i;
				}
			} else if (myIndex > 0) {
				HistoryBlock *prevBlock = history->blocks[myIndex - 1];
				if (prevBlock->items.isEmpty() || ((myIndex == 1) && (prevBlock->items.size() != 1 || prevBlock->items.front()->type() != HistoryItemDate))) {
					LOG(("App Error: Found bad history, with no first date block: %1").arg(history->blocks[0]->items.size()));
				} else if (prevBlock->items[prevBlock->items.size() - 1]->type() == HistoryItemDate) {
					prevBlock->items[prevBlock->items.size() - 1]->destroy();
					if (nextItem && myIndex == 1) { // destroy next date (for creating initial then)
						initialDateTime = nextItem->date;
						createInitialDate = true;
						nextItem->destroy();
					}
				}
			}
		}
	}
	// myIndex can be invalid now, because of destroying previous blocks

	dh = item->height();
	items.remove(i);
	int32 l = items.size();
	if ((!item->out() || item->fromChannel()) && item->unread() && history->unreadCount) {
		history->setUnreadCount(history->unreadCount - 1);
	}
	int32 itemType = item->type();
	if (itemType == HistoryItemUnreadBar) {
		if (history->unreadBar == item) {
			history->unreadBar = 0;
		}
	}
	if (createInitialDate) {
		history->createInitialDateBlock(initialDateTime);
	}
	History *h = history;
	if (l) {
		for (; i < l; ++i) {
			items[i]->y -= dh;
		}
		height -= dh;
		history->blockResized(this, dh);
	} else {
		history->removeBlock(this);
	}
}

bool ItemAnimations::animStep(float64 ms) {
	for (Animations::iterator i = _animations.begin(); i != _animations.end();) {
		const HistoryItem *item = i.key();
		if (item->animating()) {
			App::main()->msgUpdated(item->history()->peer->id, item);
			++i;
		} else {
			i = _animations.erase(i);
		}
	}
	return !_animations.isEmpty();
}

uint64 ItemAnimations::animate(const HistoryItem *item, uint64 ms) {
	if (_animations.isEmpty()) {
		_animations.insert(item, ms);
		anim::start(this);
		return 0;
	}
	Animations::const_iterator i = _animations.constFind(item);
	if (i == _animations.cend()) i = _animations.insert(item, ms);
	return ms - i.value();
}

void ItemAnimations::remove(const HistoryItem *item) {
	_animations.remove(item);
}

namespace {
	ItemAnimations _itemAnimations;
}

ItemAnimations &itemAnimations() {
	return _itemAnimations;
}

HistoryItem::HistoryItem(History *history, HistoryBlock *block, MsgId msgId, int32 flags, QDateTime msgDate, int32 from) : y(0)
, id(msgId)
, date(msgDate)
, _from(from ? App::user(from) : history->peer)
, _fromVersion(_from->nameVersion)
, _history(history)
, _block(block)
, _flags(flags)
{
}

void HistoryItem::destroy() {
	bool wasAtBottom = history()->loadedAtBottom();
	_history->removeNotification(this);
	detach();
	if (history()->isChannel()) {
		history()->asChannelHistory()->messageDeleted(this);
	}
	if (history()->lastMsg == this) {
		history()->fixLastMessage(wasAtBottom);
	}
	if (history()->lastKeyboardId == id) {
		history()->lastKeyboardId = 0;
		history()->lastKeyboardFrom = 0;
		if (App::main()) App::main()->updateBotKeyboard();
	}
	HistoryMedia *m = getMedia(true);
	MediaOverviewType t = m ? mediaToOverviewType(m->type()) : OverviewCount;
	if (t != OverviewCount) {
		if (m->type() == MediaTypeDocument && static_cast<HistoryDocument*>(m)->document()->song()) {
			history()->eraseFromOverview(OverviewAudioDocuments, id);
		} else {
			history()->eraseFromOverview(t, id);
		}
	}
	if (hasTextLinks()) {
		history()->eraseFromOverview(OverviewLinks, id);
	}
	delete this;
}

void HistoryItem::detach() {
	if (_history) {
		if (_history->unreadBar == this) {
			_history->unreadBar = 0;
		}
		if (_history->isChannel()) {
			_history->asChannelHistory()->messageDetached(this);
		}
	}
	if (_block) {
		_block->removeItem(this);
		detachFast();
		App::historyItemDetached(this);
	} else {
		if (_history->showFrom == this) {
			_history->showFrom = 0;
		}
	}
	if (_history && _history->unreadBar && _history->blocks.back()->items.back() == _history->unreadBar) {
		_history->unreadBar->destroy();
	}
}

void HistoryItem::detachFast() {
	_block = 0;
}

HistoryItem::~HistoryItem() {
	itemAnimations().remove(this);
	App::historyUnregItem(this);
	if (id < 0) {
		App::app()->uploader()->cancel(fullId());
	}
}

HistoryItem *regItem(HistoryItem *item, bool returnExisting) {
	if (!item) return 0;

	HistoryItem *existing = App::historyRegItem(item);
	if (existing) {
		delete item;
		return returnExisting ? existing : 0;
	}

	item->initDimensions();
	return item;
}

HistoryPhoto::HistoryPhoto(const MTPDphoto &photo, const QString &caption, HistoryItem *parent) : HistoryMedia()
, pixw(1), pixh(1)
, data(App::feedPhoto(photo))
, _caption(st::minPhotoSize)
, openl(new PhotoLink(data)) {
	if (!caption.isEmpty()) {
		_caption.setText(st::msgFont, caption + parent->skipBlock(), itemTextParseOptions(parent));
	}
	init();
}

HistoryPhoto::HistoryPhoto(PeerData *chat, const MTPDphoto &photo, int32 width) : HistoryMedia(width)
, pixw(1), pixh(1)
, data(App::feedPhoto(photo))
, openl(new PhotoLink(data, chat)) {
	init();
}

void HistoryPhoto::init() {
	data->thumb->load();
}

void HistoryPhoto::initDimensions(const HistoryItem *parent) {
	if (_caption.hasSkipBlock()) _caption.setSkipBlock(parent->skipBlockWidth(), parent->skipBlockHeight());

	int32 tw = convertScale(data->full->width()), th = convertScale(data->full->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	int32 thumbw = tw;
	int32 thumbh = th;
	if (!w) {
		w = thumbw;
	} else {
		thumbh = w; // square chat photo updates
	}
	_maxw = qMax(w, int32(st::minPhotoSize));
	_minh = qMax(thumbh, int32(st::minPhotoSize));
	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = toHistoryForwarded(parent);
	if (reply || !_caption.isEmpty()) {
		_maxw += st::mediaPadding.left() + st::mediaPadding.right();
		if (reply) {
			_minh += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		} else {
			if (!parent->displayFromName() || !fwd) {
				_minh += st::msgPadding.top();
			}
			if (fwd) {
				_minh += st::msgServiceNameFont->height + st::msgPadding.top();
			}
		}
		if (parent->displayFromName()) {
			_minh += st::msgPadding.top() + st::msgNameFont->height;
		}
		if (!_caption.isEmpty()) {
			_minh += st::webPagePhotoSkip + _caption.minHeight();
		}
		_minh += st::mediaPadding.bottom();
	}
}

int32 HistoryPhoto::resize(int32 width, const HistoryItem *parent) {
	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = reply ? 0 : toHistoryForwarded(parent);

	pixw = qMin(width, _maxw);
	if (reply || !_caption.isEmpty()) {
		pixw -= st::mediaPadding.left() + st::mediaPadding.right();
	}

	int32 tw = convertScale(data->full->width()), th = convertScale(data->full->height());
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	if (th > st::maxMediaSize) {
		tw = (st::maxMediaSize * tw) / th;
		th = st::maxMediaSize;
	}
	pixh = th;
	if (tw > pixw) {
		pixh = (pixw * pixh / tw);
	} else {
		pixw = tw;
	}
	if (pixh > width) {
		pixw = (pixw * width) / pixh;
		pixh = width;
	}
	if (pixw < 1) pixw = 1;
	if (pixh < 1) pixh = 1;
	w = qMax(pixw, int16(st::minPhotoSize));
	_height = qMax(pixh, int16(st::minPhotoSize));
	if (reply || !_caption.isEmpty()) {
		if (reply) {
			_height += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		} else {
			if (!parent->displayFromName() || !fwd) {
				_height += st::msgPadding.top();
			}
			if (fwd) {
				_height += st::msgServiceNameFont->height + st::msgPadding.top();
			}
		}
		if (parent->displayFromName()) {
			_height += st::msgPadding.top() + st::msgNameFont->height;
		}
		if (!_caption.isEmpty()) {
			_height += st::webPagePhotoSkip + _caption.countHeight(w);
		}
		_height += st::mediaPadding.bottom();
		w += st::mediaPadding.left() + st::mediaPadding.right();
	}
	return _height;
}

const QString HistoryPhoto::inDialogsText() const {
	return _caption.isEmpty() ? lang(lng_in_dlg_photo) : _caption.original(0, 0xFFFF, false);
}

const QString HistoryPhoto::inHistoryText() const {
	return qsl("[ ") + lang(lng_in_dlg_photo) + (_caption.isEmpty() ? QString() : (qsl(", ") + _caption.original(0, 0xFFFF))) + qsl(" ]");
}

const Text &HistoryPhoto::captionForClone() const {
	return _caption;
}

bool HistoryPhoto::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	return (x >= 0 && y >= 0 && x < width && y < _height);
}

void HistoryPhoto::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;

	int skipx = 0, skipy = 0, height = _height;
	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = reply ? 0 : toHistoryForwarded(parent);
	int replyFrom = 0, fwdFrom = 0;
	if (reply || !_caption.isEmpty()) {
		skipx = st::mediaPadding.left();
		if (reply) {
			skipy = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		} else if (fwd) {
			skipy = st::msgServiceNameFont->height + st::msgPadding.top();
		}
		if (parent->displayFromName()) {
			replyFrom = st::msgPadding.top() + st::msgNameFont->height;
			fwdFrom = st::msgPadding.top() + st::msgNameFont->height;
			skipy += replyFrom;
			if (x >= st::mediaPadding.left() && y >= st::msgPadding.top() && x < width - st::mediaPadding.left() - st::mediaPadding.right() && x < st::mediaPadding.left() + parent->from()->nameText.maxWidth() && y < replyFrom) {
				lnk = parent->from()->lnk;
				return;
			}
			if (!reply && !fwd) skipy += st::msgPadding.top();
		} else if (!reply) {
			fwdFrom = st::msgPadding.top();
			skipy += fwdFrom;
		}
		if (reply) {
			if (x >= 0 && y >= replyFrom + st::msgReplyPadding.top() && x < width && y < skipy - st::msgReplyPadding.bottom()) {
				lnk = reply->replyToLink();
				return;
			}
		} else if (fwd) {
			if (y >= fwdFrom && y < fwdFrom + st::msgServiceNameFont->height) {
				return fwd->getForwardedState(lnk, state, x - st::mediaPadding.left(), width - st::mediaPadding.left() - st::mediaPadding.right());
			}
		}
		height -= skipy + st::mediaPadding.bottom();
		width -= st::mediaPadding.left() + st::mediaPadding.right();
		if (!_caption.isEmpty()) {
			int32 fullRight = skipx + width + st::mediaPadding.right(), fullBottom = _height;
			bool inDate = parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayDefault);
			if (inDate) {
				state = HistoryInDateCursorState;
			}

			height -= _caption.countHeight(width) + st::webPagePhotoSkip;
			if (x >= skipx && y >= skipy + height + st::webPagePhotoSkip && x < skipx + width && y < _height) {
				bool inText = false;
				_caption.getState(lnk, inText, x - skipx, y - skipy - height - st::webPagePhotoSkip, width);
				state = inDate ? HistoryInDateCursorState : (inText ? HistoryInTextCursorState : HistoryDefaultCursorState);
			}
		}
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height) {
		lnk = openl;
		if (_caption.isEmpty()) {
			int32 fullRight = skipx + width + (skipx ? st::mediaPadding.right() : 0), fullBottom = _height;
			bool inDate = parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
			if (inDate) {
				state = HistoryInDateCursorState;
			}
		}
		return;
	}
}

HistoryMedia *HistoryPhoto::clone() const {
	return new HistoryPhoto(*this);
}

void HistoryPhoto::updateFrom(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaPhoto) {
		const MTPPhoto &photo(media.c_messageMediaPhoto().vphoto);
		if (photo.type() == mtpc_photo) {
			const QVector<MTPPhotoSize> &sizes(photo.c_photo().vsizes.c_vector().v);
			for (QVector<MTPPhotoSize>::const_iterator i = sizes.cbegin(), e = sizes.cend(); i != e; ++i) {
				char size = 0;
				const MTPFileLocation *loc = 0;
				switch (i->type()) {
				case mtpc_photoSize: {
					const string &s(i->c_photoSize().vtype.c_string().v);
					loc = &i->c_photoSize().vlocation;
					if (s.size()) size = s[0];
				} break;

				case mtpc_photoCachedSize: {
					const string &s(i->c_photoCachedSize().vtype.c_string().v);
					loc = &i->c_photoCachedSize().vlocation;
					if (s.size()) size = s[0];
				} break;
				}
				if (!loc || loc->type() != mtpc_fileLocation) continue;
				if (size == 's') {
					Local::writeImage(storageKey(loc->c_fileLocation()), data->thumb);
				} else if (size == 'm') {
					Local::writeImage(storageKey(loc->c_fileLocation()), data->medium);
				} else if (size == 'x') {
					Local::writeImage(storageKey(loc->c_fileLocation()), data->full);
				}
			}
		}
	}
}

void HistoryPhoto::draw(Painter &p, const HistoryItem *parent, bool selected, int32 width) const {
	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = reply ? 0 : toHistoryForwarded(parent);
	bool fromChannel = parent->fromChannel(), out = parent->out(), outbg = out && !fromChannel;

	if (width < 0) width = w;
	int skipx = 0, skipy = 0, height = _height;
	if (reply || !_caption.isEmpty()) {
		skipx = st::mediaPadding.left();

		style::color bg(selected ? (outbg ? st::msgOutSelectBg : st::msgInSelectBg) : (outbg ? st::msgOutBg : st::msgInBg));
		style::color sh(selected ? (outbg ? st::msgOutSelectShadow : st::msgInSelectShadow) : (outbg ? st::msgOutShadow : st::msgInShadow));
		RoundCorners cors(selected ? (outbg ? MessageOutSelectedCorners : MessageInSelectedCorners) : (outbg ? MessageOutCorners : MessageInCorners));
		App::roundRect(p, 0, 0, width, _height, bg, cors, &sh);

		int replyFrom = 0, fwdFrom = 0;
		if (parent->displayFromName()) {
			replyFrom = st::msgPadding.top() + st::msgNameFont->height;
			fwdFrom = st::msgPadding.top() + st::msgNameFont->height;
			skipy += replyFrom;
			p.setFont(st::msgNameFont->f);
			if (fromChannel) {
				p.setPen(selected ? st::msgInServiceSelColor : st::msgInServiceColor);
			} else {
				p.setPen(parent->from()->color);
			}
			parent->from()->nameText.drawElided(p, st::mediaPadding.left(), st::msgPadding.top(), width - st::mediaPadding.left() - st::mediaPadding.right());
			if (!fwd && !reply) skipy += st::msgPadding.top();
		} else if (!reply) {
			fwdFrom = st::msgPadding.top();
			skipy += fwdFrom;
		}
		if (reply) {
			skipy += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
			reply->drawReplyTo(p, st::msgReplyPadding.left(), replyFrom, width - st::msgReplyPadding.left() - st::msgReplyPadding.right(), selected);
		} else if (fwd) {
			skipy += st::msgServiceNameFont->height + st::msgPadding.top();
			fwd->drawForwardedFrom(p, st::mediaPadding.left(), fwdFrom, width - st::mediaPadding.left() - st::mediaPadding.right(), selected);
		}
		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			height -= st::webPagePhotoSkip + _caption.countHeight(width);
		}
	} else {
		App::roundShadow(p, 0, 0, width, _height, selected ? st::msgInSelectShadow : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}
	data->full->load(false, false);
	bool full = data->full->loaded();
	QPixmap pix;
	if (full) {
		pix = data->full->pixSingle(pixw, pixh, width, height);
	} else {
		pix = data->thumb->pixBlurredSingle(pixw, pixh, width, height);
	}
	p.drawPixmap(skipx, skipy, pix);
	if (!full) {
		uint64 dt = itemAnimations().animate(parent, getms());
		int32 cnt = int32(st::photoLoaderCnt), period = int32(st::photoLoaderPeriod), t = dt % period, delta = int32(st::photoLoaderDelta);

		int32 x = (width - st::photoLoader.width()) / 2, y = (height - st::photoLoader.height()) / 2;
		p.fillRect(skipx + x, skipy + y, st::photoLoader.width(), st::photoLoader.height(), st::photoLoaderBg->b);
		x += (st::photoLoader.width() - cnt * st::photoLoaderPoint.width() - (cnt - 1) * st::photoLoaderSkip) / 2;
		y += (st::photoLoader.height() - st::photoLoaderPoint.height()) / 2;
		QColor c(st::white->c);
		QBrush b(c);
		for (int32 i = 0; i < cnt; ++i) {
			t -= delta;
			while (t < 0) t += period;
				
			float64 alpha = (t >= st::photoLoaderDuration1 + st::photoLoaderDuration2) ? 0 : ((t > st::photoLoaderDuration1 ? ((st::photoLoaderDuration1 + st::photoLoaderDuration2 - t) / st::photoLoaderDuration2) : (t / st::photoLoaderDuration1)));
			c.setAlphaF(st::photoLoaderAlphaMin + alpha * (1 - st::photoLoaderAlphaMin));
			b.setColor(c);
			p.fillRect(skipx + x + i * (st::photoLoaderPoint.width() + st::photoLoaderSkip), skipy + y, st::photoLoaderPoint.width(), st::photoLoaderPoint.height(), b);
		}
	}

	if (selected) {
		App::roundRect(p, skipx, skipy, width, height, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	// date
	QString time(parent->timeText());
	if (_caption.isEmpty()) {
		int32 fullRight = skipx + width + (skipx ? st::mediaPadding.right() : 0), fullBottom = _height;
		parent->drawInfo(p, fullRight, fullBottom, selected, InfoDisplayOverImage);
	} else {
		p.setPen(st::black->p);
		_caption.draw(p, skipx, skipy + height + st::webPagePhotoSkip, width);

		int32 fullRight = skipx + width + st::mediaPadding.right(), fullBottom = _height;
		parent->drawInfo(p, fullRight, fullBottom, selected, InfoDisplayDefault);
	}
}

ImagePtr HistoryPhoto::replyPreview() {
	return data->makeReplyPreview();
}

QString formatSizeText(qint64 size) {
	if (size >= 1024 * 1024) { // more than 1 mb
		qint64 sizeTenthMb = (size * 10 / (1024 * 1024));
		return QString::number(sizeTenthMb / 10) + '.' + QString::number(sizeTenthMb % 10) + qsl(" MB");
	}
	if (size >= 1024) {
		qint64 sizeTenthKb = (size * 10 / 1024);
		return QString::number(sizeTenthKb / 10) + '.' + QString::number(sizeTenthKb % 10) + qsl(" KB");
	}
	return QString::number(size) + qsl(" B");
}

QString formatDownloadText(qint64 ready, qint64 total) {
	QString readyStr, totalStr, mb;
	if (total >= 1024 * 1024) { // more than 1 mb
		qint64 readyTenthMb = (ready * 10 / (1024 * 1024)), totalTenthMb = (total * 10 / (1024 * 1024));
		readyStr = QString::number(readyTenthMb / 10) + '.' + QString::number(readyTenthMb % 10);
		totalStr = QString::number(totalTenthMb / 10) + '.' + QString::number(totalTenthMb % 10);
		mb = qsl("MB");
	} else if (total >= 1024) {
		qint64 readyKb = (ready / 1024), totalKb = (total / 1024);
		readyStr = QString::number(readyKb);
		totalStr = QString::number(totalKb);
		mb = qsl("KB");
	} else {
		readyStr = QString::number(ready);
		totalStr = QString::number(total);
		mb = qsl("B");
	}
	return lng_save_downloaded(lt_ready, readyStr, lt_total, totalStr, lt_mb, mb);
}

QString formatDurationText(qint64 duration) {
	qint64 hours = (duration / 3600), minutes = (duration % 3600) / 60, seconds = duration % 60;
	return (hours ? QString::number(hours) + ':' : QString()) + (minutes >= 10 ? QString() : QString('0')) + QString::number(minutes) + ':' + (seconds >= 10 ? QString() : QString('0')) + QString::number(seconds);
}

QString formatDurationAndSizeText(qint64 duration, qint64 size) {
	return lng_duration_and_size(lt_duration, formatDurationText(duration), lt_size, formatSizeText(size));
}

int32 _downloadWidth = 0, _openWithWidth = 0, _cancelWidth = 0, _buttonWidth = 0;

HistoryVideo::HistoryVideo(const MTPDvideo &video, const QString &caption, HistoryItem *parent) : HistoryMedia()
, data(App::feedVideo(video))
, _openl(new VideoOpenLink(data))
, _savel(new VideoSaveLink(data))
, _cancell(new VideoCancelLink(data))
, _caption(st::minPhotoSize)
, _dldDone(0)
, _uplDone(0)
{
	if (!caption.isEmpty()) {
		_caption.setText(st::msgFont, caption + parent->skipBlock(), itemTextParseOptions(parent));
	}

	_size = formatDurationAndSizeText(data->duration, data->size);

	if (!_openWithWidth) {
		_downloadWidth = st::mediaSaveButton.font->width(lang(lng_media_download));
		_openWithWidth = st::mediaSaveButton.font->width(lang(lng_media_open_with));
		_cancelWidth = st::mediaSaveButton.font->width(lang(lng_media_cancel));
		_buttonWidth = (st::mediaSaveButton.width > 0) ? st::mediaSaveButton.width : ((_downloadWidth > _openWithWidth ? (_downloadWidth > _cancelWidth ? _downloadWidth : _cancelWidth) : _openWithWidth) - st::mediaSaveButton.width);
	}

	data->thumb->load();

	int32 tw = data->thumb->width(), th = data->thumb->height();
	if (data->thumb->isNull() || !tw || !th) {
		_thumbw = 0;
	} else if (tw > th) {
		_thumbw = (tw * st::mediaThumbSize) / th;
	} else {
		_thumbw = st::mediaThumbSize;
	}
}

void HistoryVideo::initDimensions(const HistoryItem *parent) {
	if (_caption.hasSkipBlock()) _caption.setSkipBlock(parent->skipBlockWidth(), parent->skipBlockHeight());

	_maxw = st::mediaMaxWidth;
	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	if (!parent->out() || parent->fromChannel()) { // add Download / Save As button
		_maxw += st::mediaSaveDelta + _buttonWidth;
	}
	_minh = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();
	if (parent->displayFromName()) {
		_minh += st::msgPadding.top() + st::msgNameFont->height;
	}
	if (const HistoryReply *reply = toHistoryReply(parent)) {
		_minh += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (const HistoryForwarded *fwd = toHistoryForwarded(parent)) {
		if (!parent->displayFromName()) {
			_minh += st::msgPadding.top();
		}
		_minh += st::msgServiceNameFont->height;
	}
	if (_caption.isEmpty()) {
		_height = _minh;
	} else {
		_minh += st::webPagePhotoSkip + _caption.minHeight();
	}
}

void HistoryVideo::regItem(HistoryItem *item) {
	App::regVideoItem(data, item);
}

void HistoryVideo::unregItem(HistoryItem *item) {
	App::unregVideoItem(data, item);
}

const QString HistoryVideo::inDialogsText() const {
	return _caption.isEmpty() ? lang(lng_in_dlg_video) : _caption.original(0, 0xFFFF, false);
}

const QString HistoryVideo::inHistoryText() const {
	return qsl("[ ") + lang(lng_in_dlg_video) + (_caption.isEmpty() ? QString() : (qsl(", ") + _caption.original(0, 0xFFFF))) + qsl(" ]");
}

bool HistoryVideo::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	int32 height = _height;
	if (width < 0) {
		width = w;
	} else if (!_caption.isEmpty()) {
		height = countHeight(parent, width);
	}
	if (width >= _maxw) {
		width = _maxw;
	}
	return (x >= 0 && y >= 0 && x < width && y < height);
}

int32 HistoryVideo::countHeight(const HistoryItem *parent, int32 width) const {
	if (_caption.isEmpty()) return _height;

	if (width < 0) width = w;
	if (width >= _maxw) {
		width = _maxw;
	}

	int32 h = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();
	if (parent->displayFromName()) {
		h += st::msgPadding.top() + st::msgNameFont->height;
	}
	if (const HistoryReply *reply = toHistoryReply(parent)) {
		h += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (const HistoryForwarded *fwd = toHistoryForwarded(parent)) {
		if (!parent->displayFromName()) {
			h += st::msgPadding.top();
		}
		h += st::msgServiceNameFont->height;
	}
	if (!_caption.isEmpty()) {
		int32 textw = width - st::mediaPadding.left() - st::mediaPadding.right();
		bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;
		if (!outbg) { // substract Download / Save As button
			textw -= st::mediaSaveDelta + _buttonWidth;
		}
		h += st::webPagePhotoSkip + _caption.countHeight(textw);
	}
	return h;
}

void HistoryVideo::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	int32 height = _height;
	if (width < 0) {
		width = w;
	} else if (!_caption.isEmpty()) {
		height = countHeight(parent, width);
	}
	if (width < 1) return;

	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = reply ? 0 : toHistoryForwarded(parent);
	int skipy = 0, replyFrom = 0, fwdFrom = 0;
	if (reply) {
		skipy = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (fwd) {
		skipy = st::msgServiceNameFont->height;
	}
	if (parent->displayFromName()) {
		replyFrom = st::msgPadding.top() + st::msgNameFont->height;
		fwdFrom = st::msgPadding.top() + st::msgNameFont->height;
		skipy += replyFrom;
	} else if (fwd) {
		fwdFrom = st::msgPadding.top();
		skipy += fwdFrom;
	}

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel, hovered, pressed;
	if (width >= _maxw) {
		width = _maxw;
	}

	if (!outbg) { // draw Download / Save As button
		int32 h = height;
		if (!_caption.isEmpty()) {
			h -= st::webPagePhotoSkip + _caption.countHeight(width - _buttonWidth - st::mediaSaveDelta - st::mediaPadding.left() - st::mediaPadding.right());
		}
		int32 btnw = _buttonWidth, btnh = st::mediaSaveButton.height, btnx = width - _buttonWidth, btny = skipy + (h - skipy - btnh) / 2;
		if (x >= btnx && y >= btny && x < btnx + btnw && y < btny + btnh) {
			lnk = data->loader ? _cancell : _savel;
			return;
		}
		width -= btnw + st::mediaSaveDelta;
	}

	if (parent->displayFromName()) {
		if (x >= st::mediaPadding.left() && y >= st::msgPadding.top() && x < width - st::mediaPadding.left() - st::mediaPadding.right() && x < st::mediaPadding.left() + parent->from()->nameText.maxWidth() && y < replyFrom) {
			lnk = parent->from()->lnk;
			return;
		}
	}
	if (reply) {
		if (x >= 0 && y >= replyFrom + st::msgReplyPadding.top() && x < width && y < skipy - st::msgReplyPadding.bottom()) {
			lnk = reply->replyToLink();
			return;
		}
	} else if (fwd) {
		if (y >= fwdFrom && y < skipy) {
			return fwd->getForwardedState(lnk, state, x - st::mediaPadding.left(), width - st::mediaPadding.left() - st::mediaPadding.right());
		}
	}

	bool inDate = parent->pointInTime(width, height, x, y, InfoDisplayDefault);
	if (inDate) {
		state = HistoryInDateCursorState;
	}

	int32 tw = width - st::mediaPadding.left() - st::mediaPadding.right();
	if (x >= st::mediaPadding.left() && y >= skipy + st::mediaPadding.top() && x < st::mediaPadding.left() + tw && y < skipy + st::mediaPadding.top() + st::mediaThumbSize && !data->loader && data->access) {
		lnk = _openl;
		return;
	}
	if (!_caption.isEmpty() && x >= st::mediaPadding.left() && x < st::mediaPadding.left() + tw && y >= skipy + st::mediaPadding.top() + st::mediaThumbSize + st::webPagePhotoSkip) {
		bool inText = false;
		_caption.getState(lnk, inText, x - st::mediaPadding.left(), y - skipy - st::mediaPadding.top() - st::mediaThumbSize - st::webPagePhotoSkip, tw);
		state = inDate ? HistoryInDateCursorState : (inText ? HistoryInTextCursorState : HistoryDefaultCursorState);
	}
}

HistoryMedia *HistoryVideo::clone() const {
	return new HistoryVideo(*this);
}

void HistoryVideo::draw(Painter &p, const HistoryItem *parent, bool selected, int32 width) const {
	int32 height = _height;
	if (width < 0) {
		width = w;
	} else if (!_caption.isEmpty()) {
		height = countHeight(parent, width);
	}
	if (width < 1) return;

	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = reply ? 0 : toHistoryForwarded(parent);
	int skipy = 0, replyFrom = 0, fwdFrom = 0;
	if (reply) {
		skipy = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (fwd) {
		skipy = st::msgServiceNameFont->height;
	}
	if (parent->displayFromName()) {
		replyFrom = st::msgPadding.top() + st::msgNameFont->height;
		fwdFrom = st::msgPadding.top() + st::msgNameFont->height;
		skipy += replyFrom;
	} else if (fwd) {
		fwdFrom = st::msgPadding.top();
		skipy += fwdFrom;
	}

	data->thumb->checkload();

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel, hovered, pressed;
	if (width >= _maxw) {
		width = _maxw;
	}

	if (!outbg) { // draw Download / Save As button
		hovered = ((data->loader ? _cancell : _savel) == textlnkOver());
		pressed = hovered && ((data->loader ? _cancell : _savel) == textlnkDown());
		if (hovered && !pressed && textlnkDown()) hovered = false;

		int32 h = height;
		if (!_caption.isEmpty()) {
			h -= st::webPagePhotoSkip + _caption.countHeight(width - _buttonWidth - st::mediaSaveDelta - st::mediaPadding.left() - st::mediaPadding.right());
		}

		int32 btnw = _buttonWidth, btnh = st::mediaSaveButton.height, btnx = width - _buttonWidth, btny = skipy + (h - skipy - btnh) / 2;
		style::color bg(selected ? st::msgInSelectBg : (hovered ? st::mediaSaveButton.overBgColor : st::mediaSaveButton.bgColor));
		style::color sh(selected ? st::msgInSelectShadow : st::msgInShadow);
		RoundCorners cors(selected ? MessageInSelectedCorners : (hovered ? ButtonHoverCorners : MessageInCorners));
		App::roundRect(p, btnx, btny, btnw, btnh, bg, cors, &sh);

		p.setPen((hovered ? st::mediaSaveButton.overColor : st::mediaSaveButton.color)->p);
		p.setFont(st::mediaSaveButton.font->f);
		QString btnText(lang(data->loader ? lng_media_cancel : (data->already().isEmpty() ? lng_media_download : lng_media_open_with)));
		int32 btnTextWidth = data->loader ? _cancelWidth : (data->already().isEmpty() ? _downloadWidth : _openWithWidth);
		p.drawText(btnx + (btnw - btnTextWidth) / 2, btny + (pressed ? st::mediaSaveButton.downTextTop : st::mediaSaveButton.textTop) + st::mediaSaveButton.font->ascent, btnText);
		width -= btnw + st::mediaSaveDelta;
	}

	style::color bg(selected ? (outbg ? st::msgOutSelectBg : st::msgInSelectBg) : (outbg ? st::msgOutBg : st::msgInBg));
	style::color sh(selected ? (outbg ? st::msgOutSelectShadow : st::msgInSelectShadow) : (outbg ? st::msgOutShadow : st::msgInShadow));
	RoundCorners cors(selected ? (outbg ? MessageOutSelectedCorners : MessageInSelectedCorners) : (outbg ? MessageOutCorners : MessageInCorners));
	App::roundRect(p, 0, 0, width, height, bg, cors, &sh);

	if (parent->displayFromName()) {
		p.setFont(st::msgNameFont->f);
		if (fromChannel) {
			p.setPen(selected ? st::msgInServiceSelColor : st::msgInServiceColor);
		} else {
			p.setPen(parent->from()->color);
		}
		parent->from()->nameText.drawElided(p, st::mediaPadding.left(), st::msgPadding.top(), width - st::mediaPadding.left() - st::mediaPadding.right());
	}
	if (reply) {
		reply->drawReplyTo(p, st::msgReplyPadding.left(), replyFrom, width - st::msgReplyPadding.left() - st::msgReplyPadding.right(), selected);
	} else if (fwd) {
		fwd->drawForwardedFrom(p, st::mediaPadding.left(), fwdFrom, width - st::mediaPadding.left() - st::mediaPadding.right(), selected);
	}

	if (_thumbw) {
		p.drawPixmap(QPoint(st::mediaPadding.left(), skipy + st::mediaPadding.top()), data->thumb->pixSingle(_thumbw, 0, st::mediaThumbSize, st::mediaThumbSize));
	} else {
		p.drawPixmap(QPoint(st::mediaPadding.left(), skipy + st::mediaPadding.top()), App::sprite(), (outbg ? st::mediaDocOutImg : st::mediaDocInImg));
	}
	if (selected) {
		App::roundRect(p, st::mediaPadding.left(), skipy + st::mediaPadding.top(), st::mediaThumbSize, st::mediaThumbSize, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	int32 twidth = width - tleft - st::mediaPadding.right();
	int32 secondwidth = width - tleft - st::msgPadding.right() - parent->skipBlockWidth();

	p.setFont(st::mediaFont->f);
	p.setPen(st::black->c);
	p.drawText(tleft, skipy + st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, lang(lng_media_video));

	QString statusText;

	style::color status(selected ? (outbg ? st::mediaOutSelectColor : st::mediaInSelectColor) : (outbg ? st::mediaOutColor : st::mediaInColor));
	p.setPen(status->p);

	if (data->loader) {
		int32 offset = data->loader->currentOffset();
		if (_dldTextCache.isEmpty() || _dldDone != offset) {
			_dldDone = offset;
			_dldTextCache = formatDownloadText(_dldDone, data->size);
		}
		statusText = _dldTextCache;
	} else {
		if (data->status == FileFailed) {
			statusText = lang(lng_attach_failed);
		} else if (data->status == FileUploading) {
			if (_uplTextCache.isEmpty() || _uplDone != data->uploadOffset) {
				_uplDone = data->uploadOffset;
				_uplTextCache = formatDownloadText(_uplDone, data->size);
			}
			statusText = _uplTextCache;
		} else {
			statusText = _size;
		}
	}
	int32 texty = skipy + st::mediaPadding.top() + st::mediaThumbSize - st::mediaDetailsShift - st::mediaFont->height;
	p.drawText(tleft, texty + st::mediaFont->ascent, statusText);
	if (parent->isMediaUnread()) {
		int32 w = st::mediaFont->width(statusText);
		if (w + st::mediaUnreadSkip + st::mediaUnreadSize <= twidth) {
			p.setRenderHint(QPainter::HighQualityAntialiasing, true);
			p.setPen(Qt::NoPen);
			p.setBrush((outbg ? (selected ? st::mediaOutUnreadSelectColor : st::mediaOutUnreadColor) : (selected ? st::mediaInUnreadSelectColor : st::mediaInUnreadColor))->b);
			p.drawEllipse(QRect(tleft + w + st::mediaUnreadSkip, texty + ((st::mediaFont->height - st::mediaUnreadSize) / 2), st::mediaUnreadSize, st::mediaUnreadSize));
			p.setRenderHint(QPainter::HighQualityAntialiasing, false);
		}
	}
	p.setFont(st::msgDateFont->f);

	if (!_caption.isEmpty()) {
		p.setPen(st::black->p);
		_caption.draw(p, st::mediaPadding.left(), skipy + st::mediaPadding.top() + st::mediaThumbSize + st::webPagePhotoSkip, width - st::mediaPadding.left() - st::mediaPadding.right());
	}

	int32 fullRight = width, fullBottom = height;
	parent->drawInfo(p, fullRight, fullBottom, selected, InfoDisplayDefault);
}

int32 HistoryVideo::resize(int32 width, const HistoryItem *parent) {
	w = qMin(width, _maxw);
	if (_caption.isEmpty()) return _height;

	_height = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();
	if (parent->displayFromName()) {
		_height += st::msgPadding.top() + st::msgNameFont->height;
	}
	if (const HistoryReply *reply = toHistoryReply(parent)) {
		_height += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (const HistoryForwarded *fwd = toHistoryForwarded(parent)) {
		if (!parent->displayFromName()) {
			_height += st::msgPadding.top();
		}
		_height += st::msgServiceNameFont->height;
	}
	if (!_caption.isEmpty()) {
		int32 textw = w - st::mediaPadding.left() - st::mediaPadding.right();
		bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;
		if (!outbg) { // substract Download / Save As button
			textw -= st::mediaSaveDelta + _buttonWidth;
		}
		_height += st::webPagePhotoSkip + _caption.countHeight(textw);
	}
	return _height;
}

ImagePtr HistoryVideo::replyPreview() {
	if (data->replyPreview->isNull() && !data->thumb->isNull()) {
		if (data->thumb->loaded()) {
			int w = data->thumb->width(), h = data->thumb->height();
			if (w <= 0) w = 1;
			if (h <= 0) h = 1;
			data->replyPreview = ImagePtr(w > h ? data->thumb->pix(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : data->thumb->pix(st::msgReplyBarSize.height()), "PNG");
		} else {
			data->thumb->load();
		}
	}
	return data->replyPreview;
}

HistoryAudio::HistoryAudio(const MTPDaudio &audio) : HistoryMedia()
, data(App::feedAudio(audio))
, _openl(new AudioOpenLink(data))
, _savel(new AudioSaveLink(data))
, _cancell(new AudioCancelLink(data))
, _dldDone(0)
, _uplDone(0)
{
	_size = formatDurationAndSizeText(data->duration, data->size);

	if (!_openWithWidth) {
		_downloadWidth = st::mediaSaveButton.font->width(lang(lng_media_download));
		_openWithWidth = st::mediaSaveButton.font->width(lang(lng_media_open_with));
		_cancelWidth = st::mediaSaveButton.font->width(lang(lng_media_cancel));
		_buttonWidth = (st::mediaSaveButton.width > 0) ? st::mediaSaveButton.width : ((_downloadWidth > _openWithWidth ? (_downloadWidth > _cancelWidth ? _downloadWidth : _cancelWidth) : _openWithWidth) - st::mediaSaveButton.width);
	}
}

void HistoryAudio::initDimensions(const HistoryItem *parent) {
	_maxw = st::mediaMaxWidth;
	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;
	if (!outbg) { // add Download / Save As button
		_maxw += st::mediaSaveDelta + _buttonWidth;
	}

	_minh = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();
	if (parent->displayFromName()) {
		_minh += st::msgPadding.top() + st::msgNameFont->height;
	}
	if (const HistoryReply *reply = toHistoryReply(parent)) {
		_minh += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (const HistoryForwarded *fwd = toHistoryForwarded(parent)) {
		if (!parent->displayFromName()) {
			_minh += st::msgPadding.top();
		}
		_minh += st::msgServiceNameFont->height;
	}
	_height = _minh;
}

void HistoryAudio::draw(Painter &p, const HistoryItem *parent, bool selected, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;

	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = reply ? 0 : toHistoryForwarded(parent);
	int skipy = 0, replyFrom = 0, fwdFrom = 0;
	if (reply) {
		skipy = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (fwd) {
		skipy = st::msgServiceNameFont->height;
	}
	if (parent->displayFromName()) {
		replyFrom = st::msgPadding.top() + st::msgNameFont->height;
		fwdFrom = st::msgPadding.top() + st::msgNameFont->height;
		skipy += replyFrom;
	} else if (fwd) {
		fwdFrom = st::msgPadding.top();
		skipy += fwdFrom;
	}

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel, hovered, pressed;
	bool already = !data->already().isEmpty(), hasdata = !data->data.isEmpty();
	if (width >= _maxw) {
		width = _maxw;
	}

	if (!data->loader && data->status != FileFailed && !already && !hasdata && data->size < AudioVoiceMsgInMemory) {
		data->save(QString());
	}

	if (!outbg) { // draw Download / Save As button
		hovered = ((data->loader ? _cancell : _savel) == textlnkOver());
		pressed = hovered && ((data->loader ? _cancell : _savel) == textlnkDown());
		if (hovered && !pressed && textlnkDown()) hovered = false;

		int32 btnw = _buttonWidth, btnh = st::mediaSaveButton.height, btnx = width - _buttonWidth, btny = skipy + (_height - skipy - btnh) / 2;
		style::color bg(selected ? st::msgInSelectBg : (hovered ? st::mediaSaveButton.overBgColor : st::mediaSaveButton.bgColor));
		style::color sh(selected ? st::msgInSelectShadow : st::msgInShadow);
		RoundCorners cors(selected ? MessageInSelectedCorners : (hovered ? ButtonHoverCorners : MessageInCorners));
		App::roundRect(p, btnx, btny, btnw, btnh, bg, cors, &sh);

		p.setPen((hovered ? st::mediaSaveButton.overColor : st::mediaSaveButton.color)->p);
		p.setFont(st::mediaSaveButton.font->f);
		QString btnText(lang(data->loader ? lng_media_cancel : (already ? lng_media_open_with : lng_media_download)));
		int32 btnTextWidth = data->loader ? _cancelWidth : (already ? _openWithWidth : _downloadWidth);
		p.drawText(btnx + (btnw - btnTextWidth) / 2, btny + (pressed ? st::mediaSaveButton.downTextTop : st::mediaSaveButton.textTop) + st::mediaSaveButton.font->ascent, btnText);
		width -= btnw + st::mediaSaveDelta;
	}

	style::color bg(selected ? (outbg ? st::msgOutSelectBg : st::msgInSelectBg) : (outbg ? st::msgOutBg : st::msgInBg));
	style::color sh(selected ? (outbg ? st::msgOutSelectShadow : st::msgInSelectShadow) : (outbg ? st::msgOutShadow : st::msgInShadow));
	RoundCorners cors(selected ? (outbg ? MessageOutSelectedCorners : MessageInSelectedCorners) : (outbg ? MessageOutCorners : MessageInCorners));
	App::roundRect(p, 0, 0, width, _height, bg, cors, &sh);

	if (parent->displayFromName()) {
		p.setFont(st::msgNameFont->f);
		if (fromChannel) {
			p.setPen(selected ? st::msgInServiceSelColor : st::msgInServiceColor);
		} else {
			p.setPen(parent->from()->color);
		}
		parent->from()->nameText.drawElided(p, st::mediaPadding.left(), st::msgPadding.top(), width - st::mediaPadding.left() - st::mediaPadding.right());
	}
	if (reply) {
		reply->drawReplyTo(p, st::msgReplyPadding.left(), replyFrom, width - st::msgReplyPadding.left() - st::msgReplyPadding.right(), selected);
	} else if (fwd) {
		fwd->drawForwardedFrom(p, st::mediaPadding.left(), fwdFrom, width - st::mediaPadding.left() - st::mediaPadding.right(), selected);
	}

	AudioMsgId playing;
	AudioPlayerState playingState = AudioPlayerStopped;
	int64 playingPosition = 0, playingDuration = 0;
	int32 playingFrequency = 0;
	if (audioPlayer()) {
		audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
	}

	QRect img;
	QString statusText;
	if (data->status == FileFailed) {
		statusText = lang(lng_attach_failed);
		img = outbg ? st::mediaAudioOutImg : st::mediaAudioInImg;
	} else if (data->status == FileUploading) {
		if (_uplTextCache.isEmpty() || _uplDone != data->uploadOffset) {
			_uplDone = data->uploadOffset;
			_uplTextCache = formatDownloadText(_uplDone, data->size);
		}
		statusText = _uplTextCache;
		img = outbg ? st::mediaAudioOutImg : st::mediaAudioInImg;
	} else if (already || hasdata) {
		bool showPause = false;
		if (playing.msgId == parent->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
			statusText = formatDurationText(playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency)) + qsl(" / ") + formatDurationText(playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
			showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
		} else {
			statusText = formatDurationText(data->duration);
		}
		img = outbg ? (showPause ? st::mediaPauseOutImg : st::mediaPlayOutImg) : (showPause ? st::mediaPauseInImg : st::mediaPlayInImg);
	} else {
		if (data->loader) {
			int32 offset = data->loader->currentOffset();
			if (_dldTextCache.isEmpty() || _dldDone != offset) {
				_dldDone = offset;
				_dldTextCache = formatDownloadText(_dldDone, data->size);
			}
			statusText = _dldTextCache;
		} else {
			statusText = _size;
		}
		img = outbg ? st::mediaAudioOutImg : st::mediaAudioInImg;
	}

	p.drawPixmap(QPoint(st::mediaPadding.left(), skipy + st::mediaPadding.top()), App::sprite(), img);
	if (selected) {
		App::roundRect(p, st::mediaPadding.left(), skipy + st::mediaPadding.top(), st::mediaThumbSize, st::mediaThumbSize, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	int32 twidth = width - tleft - st::mediaPadding.right();
	int32 secondwidth = width - tleft - st::msgPadding.right() - parent->skipBlockWidth();

	p.setFont(st::mediaFont->f);
	p.setPen(st::black->c);
	p.drawText(tleft, skipy + st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, lang(lng_media_audio));

	style::color status(selected ? (outbg ? st::mediaOutSelectColor : st::mediaInSelectColor) : (outbg ? st::mediaOutColor : st::mediaInColor));
	p.setPen(status->p);
	int32 texty = skipy + st::mediaPadding.top() + st::mediaThumbSize - st::mediaDetailsShift - st::mediaFont->height;
	p.drawText(tleft, texty + st::mediaFont->ascent, statusText);
	if (parent->isMediaUnread()) {
		int32 w = st::mediaFont->width(statusText);
		if (w + st::mediaUnreadSkip + st::mediaUnreadSize <= twidth) {
			p.setRenderHint(QPainter::HighQualityAntialiasing, true);
			p.setPen(Qt::NoPen);
			p.setBrush((outbg ? (selected ? st::mediaOutUnreadSelectColor : st::mediaOutUnreadColor) : (selected ? st::mediaInUnreadSelectColor : st::mediaInUnreadColor))->b);
			p.drawEllipse(QRect(tleft + w + st::mediaUnreadSkip, texty + ((st::mediaFont->height - st::mediaUnreadSize) / 2), st::mediaUnreadSize, st::mediaUnreadSize));
			p.setRenderHint(QPainter::HighQualityAntialiasing, false);
		}
	}
	p.setFont(st::msgDateFont->f);

	style::color date(selected ? (outbg ? st::msgOutSelectDateColor : st::msgInSelectDateColor) : (outbg ? st::msgOutDateColor : st::msgInDateColor));
	p.setPen(date->p);

	int32 fullRight = width, fullBottom = _height;
	parent->drawInfo(p, fullRight, fullBottom, selected, InfoDisplayDefault);
}

void HistoryAudio::regItem(HistoryItem *item) {
	App::regAudioItem(data, item);
}

void HistoryAudio::unregItem(HistoryItem *item) {
	App::unregAudioItem(data, item);
}

void HistoryAudio::updateFrom(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaAudio) {
		App::feedAudio(media.c_messageMediaAudio().vaudio, data);
		if (!data->data.isEmpty()) {
			Local::writeAudio(mediaKey(mtpToLocationType(mtpc_inputAudioFileLocation), data->dc, data->id), data->data);
		}
	}
}

const QString HistoryAudio::inDialogsText() const {
	return lang(lng_in_dlg_audio);
}

const QString HistoryAudio::inHistoryText() const {
	return qsl("[ ") + lang(lng_in_dlg_audio) + qsl(" ]");
}

bool HistoryAudio::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width >= _maxw) {
		width = _maxw;
	}
	return (x >= 0 && y >= 0 && x < width && y < _height);
}

void HistoryAudio::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;

	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = reply ? 0 : toHistoryForwarded(parent);
	int skipy = 0, replyFrom = 0, fwdFrom = 0;
	if (reply) {
		skipy = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (fwd) {
		skipy = st::msgServiceNameFont->height;
	}
	if (parent->displayFromName()) {
		replyFrom = st::msgPadding.top() + st::msgNameFont->height;
		fwdFrom = st::msgPadding.top() + st::msgNameFont->height;
		skipy += replyFrom;
	} else if (fwd) {
		fwdFrom = st::msgPadding.top();
		skipy += fwdFrom;
	}

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel, hovered, pressed;
	if (width >= _maxw) {
		width = _maxw;
	}

	if (!outbg) { // draw Download / Save As button
		int32 btnw = _buttonWidth, btnh = st::mediaSaveButton.height, btnx = width - _buttonWidth, btny = skipy + (_height - skipy - btnh) / 2;
		if (x >= btnx && y >= btny && x < btnx + btnw && y < btny + btnh) {
			lnk = data->loader ? _cancell : _savel;
			return;
		}
		width -= btnw + st::mediaSaveDelta;
	}

	if (parent->displayFromName()) {
		if (x >= st::mediaPadding.left() && y >= st::msgPadding.top() && x < width - st::mediaPadding.left() - st::mediaPadding.right() && x < st::mediaPadding.left() + parent->from()->nameText.maxWidth() && y < replyFrom) {
			lnk = parent->from()->lnk;
			return;
		}
	}
	if (reply) {
		if (x >= 0 && y >= replyFrom + st::msgReplyPadding.top() && x < width && y < skipy - st::msgReplyPadding.bottom()) {
			lnk = reply->replyToLink();
			return;
		}
	} else if (fwd) {
		if (y >= fwdFrom && y < skipy) {
			return fwd->getForwardedState(lnk, state, x - st::mediaPadding.left(), width - st::mediaPadding.left() - st::mediaPadding.right());
		}
	}

	if (x >= 0 && y >= skipy && x < width && y < _height && !data->loader && data->access) {
		lnk = _openl;

		bool inDate = parent->pointInTime(width, _height, x, y, InfoDisplayDefault);
		if (inDate) {
			state = HistoryInDateCursorState;
		}

		return;
	}
}

HistoryMedia *HistoryAudio::clone() const {
	return new HistoryAudio(*this);
}

namespace {
	QString documentName(DocumentData *document) {
		SongData *song = document->song();
		if (!song || (song->title.isEmpty() && song->performer.isEmpty())) return document->name;
		if (song->performer.isEmpty()) return song->title;
		return song->performer + QString::fromUtf8(" \xe2\x80\x93 ") + (song->title.isEmpty() ? qsl("Unknown Track") : song->title);
	}
}

HistoryDocument::HistoryDocument(DocumentData *document) : HistoryMedia()
, data(document)
, _openl(new DocumentOpenLink(data))
, _savel(new DocumentSaveLink(data))
, _cancell(new DocumentCancelLink(data))
, _name(documentName(data))
, _dldDone(0)
, _uplDone(0)
{
	_namew = st::mediaFont->width(_name.isEmpty() ? qsl("Document") : _name);
	_size = document->song() ? formatDurationAndSizeText(document->song()->duration, data->size) : formatSizeText(data->size);

	_height = _minh = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();

	if (!_openWithWidth) {
		_downloadWidth = st::mediaSaveButton.font->width(lang(lng_media_download));
		_openWithWidth = st::mediaSaveButton.font->width(lang(lng_media_open_with));
		_cancelWidth = st::mediaSaveButton.font->width(lang(lng_media_cancel));
		_buttonWidth = (st::mediaSaveButton.width > 0) ? st::mediaSaveButton.width : ((_downloadWidth > _openWithWidth ? (_downloadWidth > _cancelWidth ? _downloadWidth : _cancelWidth) : _openWithWidth) - st::mediaSaveButton.width);
	}

	data->thumb->load();

	int32 tw = data->thumb->width(), th = data->thumb->height();
	if (data->thumb->isNull() || !tw || !th) {
		_thumbw = 0;
	} else if (tw > th) {
		_thumbw = (tw * st::mediaThumbSize) / th;
	} else {
		_thumbw = st::mediaThumbSize;
	}
}

void HistoryDocument::initDimensions(const HistoryItem *parent) {
	if (parent == animated.msg) {
		_maxw = animated.w / cIntRetinaFactor();
		_minh = animated.h / cIntRetinaFactor();
	} else {
		_maxw = st::mediaMaxWidth;
		int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
		if (_namew + tleft + st::mediaPadding.right() > _maxw) {
			_maxw = _namew + tleft + st::mediaPadding.right();
		}
		bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;
		if (!outbg) { // add Download / Save As button
			_maxw += st::mediaSaveDelta + _buttonWidth;
		}
		_minh = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();

		if (parent->displayFromName()) {
			_minh += st::msgPadding.top() + st::msgNameFont->height;
		}
		if (const HistoryReply *reply = toHistoryReply(parent)) {
			_minh += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		} else if (const HistoryForwarded *fwd = toHistoryForwarded(parent)) {
			if (!data->song()) {
				if (!parent->displayFromName()) {
					_minh += st::msgPadding.top();
				}
				_minh += st::msgServiceNameFont->height;
			}
		}
	}
	_height = _minh;
}

void HistoryDocument::draw(Painter &p, const HistoryItem *parent, bool selected, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel, hovered, pressed;
	bool already = !data->already().isEmpty(), hasdata = !data->data.isEmpty();
	if (parent == animated.msg) {
		int32 pw = animated.w / cIntRetinaFactor(), ph = animated.h / cIntRetinaFactor();
		if (width < pw) {
			pw = width;
			ph = (pw == w) ? _height : (pw * animated.h / animated.w);
			if (ph < 1) ph = 1;
		}

		App::roundShadow(p, 0, 0, pw, ph, selected ? st::msgInSelectShadow : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);

		p.drawPixmap(0, 0, animated.current(pw * cIntRetinaFactor(), ph * cIntRetinaFactor(), true));
		if (selected) {
			App::roundRect(p, 0, 0, pw, ph, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
		}
		return;
	}

	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = (reply || data->song()) ? 0 : toHistoryForwarded(parent);
	int skipy = 0, replyFrom = 0, fwdFrom = 0;
	if (reply) {
		skipy = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (fwd) {
		skipy = st::msgServiceNameFont->height;
	}
	if (parent->displayFromName()) {
		replyFrom = st::msgPadding.top() + st::msgNameFont->height;
		fwdFrom = st::msgPadding.top() + st::msgNameFont->height;
		skipy += replyFrom;
	} else if (fwd) {
		fwdFrom = st::msgPadding.top();
		skipy += fwdFrom;
	}

	if (width >= _maxw) {
		width = _maxw;
	}

	if (!outbg) { // draw Download / Save As button
		hovered = ((data->loader ? _cancell : _savel) == textlnkOver());
		pressed = hovered && ((data->loader ? _cancell : _savel) == textlnkDown());
		if (hovered && !pressed && textlnkDown()) hovered = false;

		int32 btnw = _buttonWidth, btnh = st::mediaSaveButton.height, btnx = width - _buttonWidth, btny = skipy + (_height - skipy - btnh) / 2;
		style::color bg(selected ? st::msgInSelectBg : (hovered ? st::mediaSaveButton.overBgColor : st::mediaSaveButton.bgColor));
		style::color sh(selected ? st::msgInSelectShadow : st::msgInShadow);
		RoundCorners cors(selected ? MessageInSelectedCorners : (hovered ? ButtonHoverCorners : MessageInCorners));
		App::roundRect(p, btnx, btny, btnw, btnh, bg, cors, &sh);

		p.setPen((hovered ? st::mediaSaveButton.overColor : st::mediaSaveButton.color)->p);
		p.setFont(st::mediaSaveButton.font->f);
		QString btnText(lang(data->loader ? lng_media_cancel : (already ? lng_media_open_with : lng_media_download)));
		int32 btnTextWidth = data->loader ? _cancelWidth : (already ? _openWithWidth : _downloadWidth);
		p.drawText(btnx + (btnw - btnTextWidth) / 2, btny + (pressed ? st::mediaSaveButton.downTextTop : st::mediaSaveButton.textTop) + st::mediaSaveButton.font->ascent, btnText);
		width -= btnw + st::mediaSaveDelta;
	}

	style::color bg(selected ? (outbg ? st::msgOutSelectBg : st::msgInSelectBg) : (outbg ? st::msgOutBg : st::msgInBg));
	style::color sh(selected ? (outbg ? st::msgOutSelectShadow : st::msgInSelectShadow) : (outbg ? st::msgOutShadow : st::msgInShadow));
	RoundCorners cors(selected ? (outbg ? MessageOutSelectedCorners : MessageInSelectedCorners) : (outbg ? MessageOutCorners : MessageInCorners));
	App::roundRect(p, 0, 0, width, _height, bg, cors, &sh);

	if (parent->displayFromName()) {
		p.setFont(st::msgNameFont->f);
		if (fromChannel) {
			p.setPen(selected ? st::msgInServiceSelColor : st::msgInServiceColor);
		} else {
			p.setPen(parent->from()->color);
		}
		parent->from()->nameText.drawElided(p, st::mediaPadding.left(), st::msgPadding.top(), width - st::mediaPadding.left() - st::mediaPadding.right());
	}
	if (reply) {
		reply->drawReplyTo(p, st::msgReplyPadding.left(), replyFrom, width - st::msgReplyPadding.left() - st::msgReplyPadding.right(), selected);
	} else if (fwd) {
		fwd->drawForwardedFrom(p, st::mediaPadding.left(), fwdFrom, width - st::mediaPadding.left() - st::mediaPadding.right(), selected);
	}

	QString statusText;
	if (data->song()) {
		SongMsgId playing;
		AudioPlayerState playingState = AudioPlayerStopped;
		int64 playingPosition = 0, playingDuration = 0;
		int32 playingFrequency = 0;
		if (audioPlayer()) {
			audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
		}

		QRect img;
		if (data->status == FileFailed) {
			statusText = lang(lng_attach_failed);
			img = outbg ? st::mediaMusicOutImg : st::mediaMusicInImg;
		} else if (data->status == FileUploading) {
			if (_uplTextCache.isEmpty() || _uplDone != data->uploadOffset) {
				_uplDone = data->uploadOffset;
				_uplTextCache = formatDownloadText(_uplDone, data->size);
			}
			statusText = _uplTextCache;
			img = outbg ? st::mediaMusicOutImg : st::mediaMusicInImg;
		} else if (already || hasdata) {
			bool showPause = false;
			if (playing.msgId == parent->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				statusText = formatDurationText(playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency)) + qsl(" / ") + formatDurationText(playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
				showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
			} else {
				statusText = formatDurationText(data->song()->duration);
			}
			if (!showPause && playing.msgId == parent->fullId() && App::main() && App::main()->player()->seekingSong(playing)) showPause = true;
			img = outbg ? (showPause ? st::mediaPauseOutImg : st::mediaPlayOutImg) : (showPause ? st::mediaPauseInImg : st::mediaPlayInImg);
		} else {
			if (data->loader) {
				int32 offset = data->loader->currentOffset();
				if (_dldTextCache.isEmpty() || _dldDone != offset) {
					_dldDone = offset;
					_dldTextCache = formatDownloadText(_dldDone, data->size);
				}
				statusText = _dldTextCache;
			} else {
				statusText = _size;
			}
			img = outbg ? st::mediaMusicOutImg : st::mediaMusicInImg;
		}

		p.drawPixmap(QPoint(st::mediaPadding.left(), skipy + st::mediaPadding.top()), App::sprite(), img);
	} else {
		if (data->status == FileFailed) {
			statusText = lang(lng_attach_failed);
		} else if (data->status == FileUploading) {
			if (_uplTextCache.isEmpty() || _uplDone != data->uploadOffset) {
				_uplDone = data->uploadOffset;
				_uplTextCache = formatDownloadText(_uplDone, data->size);
			}
			statusText = _uplTextCache;
		} else if (data->loader) {
			int32 offset = data->loader->currentOffset();
			if (_dldTextCache.isEmpty() || _dldDone != offset) {
				_dldDone = offset;
				_dldTextCache = formatDownloadText(_dldDone, data->size);
			}
			statusText = _dldTextCache;
		} else {
			statusText = _size;
		}

		if (_thumbw) {
			data->thumb->checkload();
			p.drawPixmap(QPoint(st::mediaPadding.left(), skipy + st::mediaPadding.top()), data->thumb->pixSingle(_thumbw, 0, st::mediaThumbSize, st::mediaThumbSize));
		} else {
			p.drawPixmap(QPoint(st::mediaPadding.left(), skipy + st::mediaPadding.top()), App::sprite(), (outbg ? st::mediaDocOutImg : st::mediaDocInImg));
		}
	}
	if (selected) {
		App::roundRect(p, st::mediaPadding.left(), skipy + st::mediaPadding.top(), st::mediaThumbSize, st::mediaThumbSize, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	int32 twidth = width - tleft - st::mediaPadding.right();
	int32 secondwidth = width - tleft - st::msgPadding.right() - parent->skipBlockWidth();

	p.setFont(st::mediaFont->f);
	p.setPen(st::black->c);
	if (twidth < _namew) {
		p.drawText(tleft, skipy + st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, st::mediaFont->elided(_name, twidth));
	} else {
		p.drawText(tleft, skipy + st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, _name);
	}

	style::color status(selected ? (outbg ? st::mediaOutSelectColor : st::mediaInSelectColor) : (outbg ? st::mediaOutColor : st::mediaInColor));
	p.setPen(status->p);

	p.drawText(tleft, skipy + st::mediaPadding.top() + st::mediaThumbSize - st::mediaDetailsShift - st::mediaFont->descent, statusText);

	p.setFont(st::msgDateFont->f);

	style::color date(selected ? (outbg ? st::msgOutSelectDateColor : st::msgInSelectDateColor) : (outbg ? st::msgOutDateColor : st::msgInDateColor));
	p.setPen(date->p);

	int32 fullRight = width, fullBottom = _height;
	parent->drawInfo(p, fullRight, fullBottom, selected, InfoDisplayDefault);
}

void HistoryDocument::drawInPlaylist(Painter &p, const HistoryItem *parent, bool selected, bool over, int32 width) const {
	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;
	bool already = !data->already().isEmpty(), hasdata = !data->data.isEmpty();
	int32 height = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();

	style::color bg(selected ? st::msgInSelectBg : (over ? st::playlistHoverBg : st::msgInBg));
	p.fillRect(0, 0, width, height, bg->b);

	QString statusText;
	if (data->song()) {
		SongMsgId playing;
		AudioPlayerState playingState = AudioPlayerStopped;
		int64 playingPosition = 0, playingDuration = 0;
		int32 playingFrequency = 0;
		if (audioPlayer()) {
			audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
		}

		QRect img;
		if (data->status == FileFailed) {
			statusText = lang(lng_attach_failed);
			img = st::mediaMusicInImg;
		} else if (data->status == FileUploading) {
			if (_uplTextCache.isEmpty() || _uplDone != data->uploadOffset) {
				_uplDone = data->uploadOffset;
				_uplTextCache = formatDownloadText(_uplDone, data->size);
			}
			statusText = _uplTextCache;
			img = st::mediaMusicInImg;
		} else if (already || hasdata) {
			bool isPlaying = (playing.msgId == parent->fullId());
			bool showPause = false;
			if (playing.msgId == parent->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				statusText = formatDurationText(playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency)) + qsl(" / ") + formatDurationText(playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
				showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
			} else {
				statusText = formatDurationText(data->song()->duration);
			}
			if (!showPause && playing.msgId == parent->fullId() && App::main() && App::main()->player()->seekingSong(playing)) showPause = true;
			img = isPlaying ? (showPause ? st::mediaPauseOutImg : st::mediaPlayOutImg) : (showPause ? st::mediaPauseInImg : st::mediaPlayInImg);
		} else {
			if (data->loader) {
				int32 offset = data->loader->currentOffset();
				if (_dldTextCache.isEmpty() || _dldDone != offset) {
					_dldDone = offset;
					_dldTextCache = formatDownloadText(_dldDone, data->size);
				}
				statusText = _dldTextCache;
			} else {
				statusText = _size;
			}
			img = st::mediaMusicInImg;
		}

		p.drawPixmap(QPoint(st::mediaPadding.left(), st::mediaPadding.top()), App::sprite(), img);
	} else {
		if (data->status == FileFailed) {
			statusText = lang(lng_attach_failed);
		} else if (data->status == FileUploading) {
			if (_uplTextCache.isEmpty() || _uplDone != data->uploadOffset) {
				_uplDone = data->uploadOffset;
				_uplTextCache = formatDownloadText(_uplDone, data->size);
			}
			statusText = _uplTextCache;
		} else if (data->loader) {
			int32 offset = data->loader->currentOffset();
			if (_dldTextCache.isEmpty() || _dldDone != offset) {
				_dldDone = offset;
				_dldTextCache = formatDownloadText(_dldDone, data->size);
			}
			statusText = _dldTextCache;
		} else {
			statusText = _size;
		}

		if (_thumbw) {
			data->thumb->checkload();
			p.drawPixmap(QPoint(st::mediaPadding.left(), st::mediaPadding.top()), data->thumb->pixSingle(_thumbw, 0, st::mediaThumbSize, st::mediaThumbSize));
		} else {
			p.drawPixmap(QPoint(st::mediaPadding.left(), st::mediaPadding.top()), App::sprite(), st::mediaDocInImg);
		}
	}
	if (selected) {
		App::roundRect(p, st::mediaPadding.left(), st::mediaPadding.top(), st::mediaThumbSize, st::mediaThumbSize, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	int32 twidth = width - tleft - st::mediaPadding.right();
	int32 secondwidth = width - tleft - st::msgPadding.right() - parent->skipBlockWidth();

	p.setFont(st::mediaFont->f);
	p.setPen(st::black->c);
	if (twidth < _namew) {
		p.drawText(tleft, st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, st::mediaFont->elided(_name, twidth));
	} else {
		p.drawText(tleft, st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, _name);
	}

	style::color status(selected ? st::mediaInSelectColor : st::mediaInColor);
	p.setPen(status->p);
	p.drawText(tleft, st::mediaPadding.top() + st::mediaThumbSize - st::mediaDetailsShift - st::mediaFont->descent, statusText);
}

TextLinkPtr HistoryDocument::linkInPlaylist() {
	if (!data->loader && data->access) {
		return _openl;
	}
	return TextLinkPtr();
}

void HistoryDocument::regItem(HistoryItem *item) {
	App::regDocumentItem(data, item);
}

void HistoryDocument::unregItem(HistoryItem *item) {
	App::unregDocumentItem(data, item);
}

void HistoryDocument::updateFrom(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, data);
	}
}

int32 HistoryDocument::resize(int32 width, const HistoryItem *parent) {
	w = qMin(width, _maxw);
	if (parent == animated.msg) {
		if (w > st::maxMediaSize) {
			w = st::maxMediaSize;
		}
		_height = animated.h / cIntRetinaFactor();
		if (animated.w / cIntRetinaFactor() > w) {
			_height = (w * _height / (animated.w / cIntRetinaFactor()));
			if (_height <= 0) _height = 1;
		}
	} else {
		_height = _minh;
	}
	return _height;
}

const QString HistoryDocument::inDialogsText() const {
	return _name.isEmpty() ? lang(lng_in_dlg_file) : _name;
}

const QString HistoryDocument::inHistoryText() const {
	return qsl("[ ") + lang(lng_in_dlg_file) + (_name.isEmpty() ? QString() : (qsl(" : ") + _name)) + qsl(" ]");
}

bool HistoryDocument::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width >= _maxw) {
		width = _maxw;
	}
	if (parent == animated.msg) {
		int32 h = (width == w) ? _height : (width * animated.h / animated.w);
		if (h < 1) h = 1;
		return (x >= 0 && y >= 0 && x < width && y < h);
	}
	return (x >= 0 && y >= 0 && x < width && y < _height);
}

int32 HistoryDocument::countHeight(const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width >= _maxw) {
		width = _maxw;
	}
	if (parent == animated.msg) {
		int32 h = (width == w) ? _height : (width * animated.h / animated.w);
		if (h < 1) h = 1;
		return h;
	}
	return _height;
}

void HistoryDocument::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel, hovered, pressed;
	if (width >= _maxw) {
		width = _maxw;
	}
	if (parent == animated.msg) {
		int32 h = (width == w) ? _height : (width * animated.h / animated.w);
		if (h < 1) h = 1;
		lnk = (x >= 0 && y >= 0 && x < width && y < h) ? _openl : TextLinkPtr();
		return;
	}

	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = (reply || data->song()) ? 0 : toHistoryForwarded(parent);
	int skipy = 0, replyFrom = 0, fwdFrom = 0;
	if (reply) {
		skipy = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (fwd) {
		skipy = st::msgServiceNameFont->height;
	}
	if (parent->displayFromName()) {
		replyFrom = st::msgPadding.top() + st::msgNameFont->height;
		fwdFrom = st::msgPadding.top() + st::msgNameFont->height;
		skipy += replyFrom;
	} else if (fwd) {
		fwdFrom = st::msgPadding.top();
		skipy += fwdFrom;
	}

	if (!outbg) { // draw Download / Save As button
		int32 btnw = _buttonWidth, btnh = st::mediaSaveButton.height, btnx = width - _buttonWidth, btny = skipy + (_height - skipy - btnh) / 2;
		if (x >= btnx && y >= btny && x < btnx + btnw && y < btny + btnh) {
			lnk = data->loader ? _cancell : _savel;
			return;
		}
		width -= btnw + st::mediaSaveDelta;
	}

	if (parent->displayFromName()) {
		if (x >= st::mediaPadding.left() && y >= st::msgPadding.top() && x < width - st::mediaPadding.left() - st::mediaPadding.right() && x < st::mediaPadding.left() + parent->from()->nameText.maxWidth() && y < replyFrom) {
			lnk = parent->from()->lnk;
			return;
		}
	}
	if (reply) {
		if (x >= 0 && y >= replyFrom + st::msgReplyPadding.top() && x < width && y < skipy - st::msgReplyPadding.bottom()) {
			lnk = reply->replyToLink();
			return;
		}
	} else if (fwd) {
		if (y >= fwdFrom && y < skipy) {
			return fwd->getForwardedState(lnk, state, x - st::mediaPadding.left(), width - st::mediaPadding.left() - st::mediaPadding.right());
		}
	}

	if (x >= 0 && y >= skipy && x < width && y < _height && !data->loader && data->access) {
		lnk = _openl;

		bool inDate = parent->pointInTime(width, _height, x, y, InfoDisplayDefault);
		if (inDate) {
			state = HistoryInDateCursorState;
		}

		return;
	}
}

HistoryMedia *HistoryDocument::clone() const {
	return new HistoryDocument(*this);
}

ImagePtr HistoryDocument::replyPreview() {
	return data->makeReplyPreview();
}

HistorySticker::HistorySticker(DocumentData *document) : HistoryMedia()
, pixw(1), pixh(1), data(document), lastw(0)
{
	data->thumb->load();
	if (!data->sticker()->alt.isEmpty()) {
		_emoji = data->sticker()->alt;
	}
}

void HistorySticker::initDimensions(const HistoryItem *parent) {
	pixw = data->dimensions.width();
	pixh = data->dimensions.height();
	if (pixw > st::maxStickerSize) {
		pixh = (st::maxStickerSize * pixh) / pixw;
		pixw = st::maxStickerSize;
	}
	if (pixh > st::maxStickerSize) {
		pixw = (st::maxStickerSize * pixw) / pixh;
		pixh = st::maxStickerSize;
	}
	if (pixw < 1) pixw = 1;
	if (pixh < 1) pixh = 1;
	_maxw = qMax(pixw, int16(st::minPhotoSize));
	_minh = qMax(pixh, int16(st::minPhotoSize));
	if (const HistoryReply *reply = toHistoryReply(parent)) {
		_maxw += st::msgReplyPadding.left() + reply->replyToWidth();
	}
	_height = _minh;
	w = qMin(lastw, _maxw);
}

void HistorySticker::draw(Painter &p, const HistoryItem *parent, bool selected, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;
	if (width > _maxw) width = _maxw;

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel, hovered, pressed;
	bool already = !data->already().isEmpty(), hasdata = !data->data.isEmpty();

	int32 usew = _maxw, usex = 0;
	const HistoryReply *reply = toHistoryReply(parent);
	if (reply) {
		usew -= st::msgReplyPadding.left() + reply->replyToWidth();
		if (fromChannel) {
		} else if (out) {
			usex = width - usew;
		}
	}

	if (!data->loader && data->status != FileFailed && !already && !hasdata) {
		data->save(QString());
	}
	if (data->sticker()->img->isNull() && (already || hasdata)) {
		if (already) {
			data->sticker()->img = ImagePtr(data->already());
		} else {
			data->sticker()->img = ImagePtr(data->data);
		}
	}
	if (selected) {
		if (data->sticker()->img->isNull()) {
			p.drawPixmap(QPoint(usex + (usew - pixw) / 2, (_minh - pixh) / 2), data->thumb->pixBlurredColored(st::msgStickerOverlay, pixw, pixh));
		} else {
			p.drawPixmap(QPoint(usex + (usew - pixw) / 2, (_minh - pixh) / 2), data->sticker()->img->pixColored(st::msgStickerOverlay, pixw, pixh));
		}
	} else {
		if (data->sticker()->img->isNull()) {
			p.drawPixmap(QPoint(usex + (usew - pixw) / 2, (_minh - pixh) / 2), data->thumb->pixBlurred(pixw, pixh));
		} else {
			p.drawPixmap(QPoint(usex + (usew - pixw) / 2, (_minh - pixh) / 2), data->sticker()->img->pix(pixw, pixh));
		}
	}

	parent->drawInfo(p, usex + usew, _height, selected, InfoDisplayOverImage);

	if (reply) {
		int32 rw = width - usew - st::msgReplyPadding.left(), rh = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		int32 rx = fromChannel ? (usew + st::msgReplyPadding.left()) : (out ? 0 : (usew + st::msgReplyPadding.left())), ry = _height - rh;
		
		App::roundRect(p, rx, ry, rw, rh, selected ? App::msgServiceSelectBg() : App::msgServiceBg(), selected ? ServiceSelectedCorners : ServiceCorners);

		reply->drawReplyTo(p, rx + st::msgReplyPadding.left(), ry, rw - st::msgReplyPadding.left() - st::msgReplyPadding.right(), selected, true);
	}
}

int32 HistorySticker::resize(int32 width, const HistoryItem *parent) {
	w = qMin(width, _maxw);
	lastw = width;
	return _height;
}

void HistorySticker::regItem(HistoryItem *item) {
	App::regDocumentItem(data, item);
}

void HistorySticker::unregItem(HistoryItem *item) {
	App::unregDocumentItem(data, item);
}

void HistorySticker::updateFrom(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, data);
		if (!data->data.isEmpty()) {
			Local::writeStickerImage(mediaKey(mtpToLocationType(mtpc_inputDocumentFileLocation), data->dc, data->id), data->data);
		}
	}
}

const QString HistorySticker::inDialogsText() const {
	return _emoji.isEmpty() ? lang(lng_in_dlg_sticker) : lng_in_dlg_sticker_emoji(lt_emoji, _emoji);
}

const QString HistorySticker::inHistoryText() const {
	return qsl("[ ") + inDialogsText() + qsl(" ]");
}

bool HistorySticker::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	return (x >= 0 && y >= 0 && x < _maxw && y < _minh);
}

int32 HistorySticker::countHeight(const HistoryItem *parent, int32 width) const {
	return _minh;
}

void HistorySticker::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;

	if (width > _maxw) width = _maxw;

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	int32 usew = _maxw, usex = 0;
	const HistoryReply *reply = toHistoryReply(parent);
	if (reply) {
		usew -= reply->replyToWidth();
		if (fromChannel) {
		} else if (out) {
			usex = width - usew;
		}

		int32 rw = width - usew, rh = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		int32 rx = fromChannel ? (usew + st::msgReplyPadding.left()) : (out ? 0 : (usew + st::msgReplyPadding.left())), ry = _height - rh;
		if (x >= rx && y >= ry && x < rx + rw && y < ry + rh) {
			lnk = reply->replyToLink();
			return;
		}
	}
	bool inDate = parent->pointInTime(usex + usew, _height, x, y, InfoDisplayOverImage);
	if (inDate) {
		state = HistoryInDateCursorState;
	}
}

HistoryMedia *HistorySticker::clone() const {
	return new HistorySticker(*this);
}

HistoryContact::HistoryContact(int32 userId, const QString &first, const QString &last, const QString &phone) : HistoryMedia(0)
, userId(userId)
, phone(App::formatPhone(phone))
, contact(App::userLoaded(userId))
{
	App::regSharedContactPhone(userId, phone);

	_maxw = st::mediaMaxWidth;
	name.setText(st::mediaFont, lng_full_name(lt_first_name, first, lt_last_name, last).trimmed(), _textNameOptions);

	phonew = st::mediaFont->width(phone);

	if (contact) {
		contact->photo->load();
	}
}

HistoryContact::HistoryContact(int32 userId, const QString &fullname, const QString &phone) : HistoryMedia(0)
, userId(userId)
, phone(App::formatPhone(phone))
, contact(App::userLoaded(userId))
{
	App::regSharedContactPhone(userId, phone);

	_maxw = st::mediaMaxWidth;
	name.setText(st::mediaFont, fullname.trimmed(), _textNameOptions);

	phonew = st::mediaFont->width(phone);

	if (contact) {
		contact->photo->load();
	}
}

void HistoryContact::initDimensions(const HistoryItem *parent) {
	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	int32 fullInfoWidth = parent->skipBlockWidth() + st::msgPadding.right();
	if (name.maxWidth() + tleft + fullInfoWidth > _maxw) {
		_maxw = name.maxWidth() + tleft + fullInfoWidth;
	}
	if (phonew + tleft + st::mediaPadding.right() > _maxw) {
		_maxw = phonew + tleft + st::mediaPadding.right();
	}
	_minh = st::mediaPadding.top() + st::mediaThumbSize + st::mediaPadding.bottom();
	if (parent->displayFromName()) {
		_minh += st::msgPadding.top() + st::msgNameFont->height;
	}
	if (const HistoryReply *reply = toHistoryReply(parent)) {
		_minh += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (const HistoryForwarded *fwd = toHistoryForwarded(parent)) {
		if (!parent->displayFromName()) {
			_minh += st::msgPadding.top();
		}
		_minh += st::msgServiceNameFont->height;
	}
	_height = _minh;
}

const QString HistoryContact::inDialogsText() const {
	return lang(lng_in_dlg_contact);
}

const QString HistoryContact::inHistoryText() const {
	return qsl("[ ") + lang(lng_in_dlg_contact) + qsl(" : ") + name.original(0, 0xFFFF, false) + qsl(", ") + phone + qsl(" ]");
}

bool HistoryContact::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	return (x >= 0 && y <= 0 && x < w && y < _height);
}

void HistoryContact::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;

	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = reply ? 0 : toHistoryForwarded(parent);
	int skipy = 0, replyFrom = 0, fwdFrom = 0;
	if (reply) {
		skipy = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (fwd) {
		skipy = st::msgServiceNameFont->height;
	}
	if (parent->displayFromName()) {
		replyFrom = st::msgPadding.top() + st::msgNameFont->height;
		fwdFrom = st::msgPadding.top() + st::msgNameFont->height;
		skipy += replyFrom;
	} else if (fwd) {
		fwdFrom = st::msgPadding.top();
		skipy += fwdFrom;
	}

	if (parent->displayFromName()) {
		if (x >= st::mediaPadding.left() && y >= st::msgPadding.top() && x < width - st::mediaPadding.left() - st::mediaPadding.right() && x < st::mediaPadding.left() + parent->from()->nameText.maxWidth() && y < replyFrom) {
			lnk = parent->from()->lnk;
			return;
		}
	}
	if (reply) {
		if (x >= 0 && y >= replyFrom + st::msgReplyPadding.top() && x < width && y < skipy - st::msgReplyPadding.bottom()) {
			lnk = reply->replyToLink();
			return;
		}
	} else if (fwd) {
		if (y >= fwdFrom && y < skipy) {
			return fwd->getForwardedState(lnk, state, x - st::mediaPadding.left(), width - st::mediaPadding.left() - st::mediaPadding.right());
		}
	}

	if (x >= 0 && y >= skipy && x < width && y < _height && contact) {
		lnk = contact->lnk;

		bool inDate = parent->pointInTime(width, _height, x, y, InfoDisplayDefault);
		if (inDate) {
			state = HistoryInDateCursorState;
		}

		return;
	}
}

HistoryMedia *HistoryContact::clone() const {
	return new HistoryContact(userId, name.original(0, 0xFFFF, false), phone);
}

void HistoryContact::draw(Painter &p, const HistoryItem *parent, bool selected, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;

	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = reply ? 0 : toHistoryForwarded(parent);
	int skipy = 0, replyFrom = 0, fwdFrom = 0;
	if (reply) {
		skipy = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	} else if (fwd) {
		skipy = st::msgServiceNameFont->height;
	}
	if (parent->displayFromName()) {
		replyFrom = st::msgPadding.top() + st::msgNameFont->height;
		fwdFrom = st::msgPadding.top() + st::msgNameFont->height;
		skipy += replyFrom;
	} else if (fwd) {
		fwdFrom = st::msgPadding.top();
		skipy += fwdFrom;
	}

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;
	if (width >= _maxw) {
		width = _maxw;
	}

	style::color bg(selected ? (outbg ? st::msgOutSelectBg : st::msgInSelectBg) : (outbg ? st::msgOutBg : st::msgInBg));
	style::color sh(selected ? (outbg ? st::msgOutSelectShadow : st::msgInSelectShadow) : (outbg ? st::msgOutShadow : st::msgInShadow));
	RoundCorners cors(selected ? (outbg ? MessageOutSelectedCorners : MessageInSelectedCorners) : (outbg ? MessageOutCorners : MessageInCorners));
	App::roundRect(p, 0, 0, width, _height, bg, cors, &sh);

	if (parent->displayFromName()) {
		p.setFont(st::msgNameFont->f);
		if (fromChannel) {
			p.setPen(selected ? st::msgInServiceSelColor : st::msgInServiceColor);
		} else {
			p.setPen(parent->from()->color);
		}
		parent->from()->nameText.drawElided(p, st::mediaPadding.left(), st::msgPadding.top(), width - st::mediaPadding.left() - st::mediaPadding.right());
	}
	if (reply) {
		reply->drawReplyTo(p, st::msgReplyPadding.left(), replyFrom, width - st::msgReplyPadding.left() - st::msgReplyPadding.right(), selected);
	} else if (fwd) {
		fwd->drawForwardedFrom(p, st::mediaPadding.left(), fwdFrom, width - st::mediaPadding.left() - st::mediaPadding.right(), selected);
	}

	p.drawPixmap(st::mediaPadding.left(), skipy + st::mediaPadding.top(), (contact ? contact->photo : userDefPhoto(1))->pixRounded(st::mediaThumbSize));
	if (selected) {
		App::roundRect(p, st::mediaPadding.left(), skipy + st::mediaPadding.top(), st::mediaThumbSize, st::mediaThumbSize, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
	int32 twidth = width - tleft - st::mediaPadding.right();
	int32 secondwidth = width - tleft - st::msgPadding.right() - parent->skipBlockWidth();

	p.setFont(st::mediaFont->f);
	p.setPen(st::black->c);
	if (twidth < phonew) {
		p.drawText(tleft, skipy + st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, st::mediaFont->elided(phone, twidth));
	} else {
		p.drawText(tleft, skipy + st::mediaPadding.top() + st::mediaNameTop + st::mediaFont->ascent, phone);
	}

	style::color status(selected ? (outbg ? st::mediaOutSelectColor : st::mediaInSelectColor) : (outbg ? st::mediaOutColor : st::mediaInColor));
	p.setPen(status->p);

	name.drawElided(p, tleft, skipy + st::mediaPadding.top() + st::mediaThumbSize - st::mediaDetailsShift - st::mediaFont->height, secondwidth);

	int32 fullRight = width, fullBottom = _height;
	parent->drawInfo(p, fullRight, fullBottom, selected, InfoDisplayDefault);
}

void HistoryContact::updateFrom(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaContact) {
		userId = media.c_messageMediaContact().vuser_id.v;
		contact = App::userLoaded(userId);
		if (contact) {
			if (contact->phone.isEmpty()) {
				contact->setPhone(phone);
			}
			if (contact->contact < 0) {
				contact->contact = 0;
			}
			contact->photo->load();
		}
	}
}

HistoryWebPage::HistoryWebPage(WebPageData *data) : HistoryMedia()
, data(data)
, _openl(0)
, _attachl(0)
, _asArticle(false)
, _title(st::msgMinWidth - st::webPageLeft)
, _description(st::msgMinWidth - st::webPageLeft)
, _siteNameWidth(0)
, _durationWidth(0)
, _docNameWidth(0)
, _docThumbWidth(0)
, _docDownloadDone(0)
, _pixw(0), _pixh(0)
{
}

void HistoryWebPage::initDimensions(const HistoryItem *parent) {
	if (data->pendingTill) {
		_maxw = _minh = _height = 0;
		//_maxw = st::webPageLeft + st::linkFont->width(lang((data->pendingTill < 0) ? lng_attach_failed : lng_profile_loading));
		//_minh = st::replyHeight;
		//_height = _minh;
		return;
	}
	if (!_openl && !data->url.isEmpty()) _openl = TextLinkPtr(new TextLink(data->url));
	if (!_attachl && data->photo && data->type != WebPageVideo) _attachl = TextLinkPtr(new PhotoLink(data->photo));
	if (!_attachl && data->doc) _attachl = TextLinkPtr(new DocumentOpenLink(data->doc));

	if (data->photo && data->type != WebPagePhoto && data->type != WebPageVideo) {
		if (data->type == WebPageProfile) {
			_asArticle = true;
		} else if (data->siteName == qstr("Twitter") || data->siteName == qstr("Facebook")) {
			_asArticle = false;
		} else {
			_asArticle = true;
		}
	} else {
		_asArticle = false;
	}

	if (_asArticle) {
		w = st::webPagePhotoSize;
		_maxw = st::webPageLeft + st::webPagePhotoSize;
		_minh = st::webPagePhotoSize;
		_minh += st::webPagePhotoSkip + (st::msgDateFont->height - st::msgDateDelta.y());
	} else if (data->photo) {
		int32 tw = convertScale(data->photo->full->width()), th = convertScale(data->photo->full->height());
		if (!tw || !th) {
			tw = th = 1;
		}
		if (tw > st::maxMediaSize) {
			th = (st::maxMediaSize * th) / tw;
			tw = st::maxMediaSize;
		}
		if (th > st::maxMediaSize) {
			tw = (st::maxMediaSize * tw) / th;
			th = st::maxMediaSize;
		}
		int32 thumbw = tw;
		int32 thumbh = th;

		w = thumbw;

		_maxw = st::webPageLeft + qMax(thumbh, qMax(w, int32(st::minPhotoSize))) + parent->skipBlockWidth();
		_minh = qMax(thumbh, int32(st::minPhotoSize));
		_minh += st::webPagePhotoSkip;
	} else if (data->doc) {
		if (!data->doc->thumb->isNull()) {
			data->doc->thumb->load();

			int32 tw = data->doc->thumb->width(), th = data->doc->thumb->height();
			if (data->doc->thumb->isNull() || !tw || !th) {
				_docThumbWidth = 0;
			} else if (tw > th) {
				_docThumbWidth = (tw * st::mediaThumbSize) / th;
			} else {
				_docThumbWidth = st::mediaThumbSize;
			}
		}
		_docName = documentName(data->doc);
		_docSize = data->doc->song() ? formatDurationAndSizeText(data->doc->song()->duration, data->doc->size) : formatSizeText(data->doc->size);
		_docNameWidth = st::mediaFont->width(_docName.isEmpty() ? qsl("Document") : _docName);

		if (parent == animated.msg) {
			_maxw = st::webPageLeft + (animated.w / cIntRetinaFactor()) + parent->skipBlockWidth();
			_minh = animated.h / cIntRetinaFactor();
			_minh += st::webPagePhotoSkip;
		} else {
			_maxw = qMax(st::webPageLeft + st::mediaThumbSize + st::mediaPadding.right() + _docNameWidth + st::mediaPadding.right(), st::mediaMaxWidth);
			_minh = st::mediaThumbSize;
		}
	} else {
		_maxw = st::webPageLeft;
		_minh = 0;
	}

	if (!data->siteName.isEmpty()) {
		_siteNameWidth = st::webPageTitleFont->width(data->siteName);
		if (_asArticle) {
			_maxw = qMax(_maxw, int32(st::webPageLeft + _siteNameWidth + st::webPagePhotoDelta + st::webPagePhotoSize));
		} else {
			_maxw = qMax(_maxw, int32(st::webPageLeft + _siteNameWidth + parent->skipBlockWidth()));
			_minh += st::webPageTitleFont->height;
		}
	}
	QString title(data->title.isEmpty() ? data->author : data->title);
	if (!title.isEmpty()) {
		title = textClean(title);
		if (!_asArticle && !data->photo && data->description.isEmpty()) title += parent->skipBlock();
		_title.setText(st::webPageTitleFont, title, _webpageTitleOptions);
		if (_asArticle) {
			_maxw = qMax(_maxw, int32(st::webPageLeft + _title.maxWidth() + st::webPagePhotoDelta + st::webPagePhotoSize));
		} else {
			_maxw = qMax(_maxw, int32(st::webPageLeft + _title.maxWidth()));
			_minh += qMin(_title.minHeight(), 2 * st::webPageTitleFont->height);
		}
	}
	if (!data->description.isEmpty()) {
		QString text = textClean(data->description);
		if (!_asArticle && !data->photo) text += parent->skipBlock();
		const TextParseOptions *opts = &_webpageDescriptionOptions;
		if (data->siteName == qstr("Twitter")) {
			opts = &_twitterDescriptionOptions;
		} else if (data->siteName == qstr("Instagram")) {
			opts = &_instagramDescriptionOptions;
		}
		_description.setText(st::webPageDescriptionFont, text, *opts);
		if (_asArticle) {
			_maxw = qMax(_maxw, int32(st::webPageLeft + _description.maxWidth() + st::webPagePhotoDelta + st::webPagePhotoSize));
		} else {
			_maxw = qMax(_maxw, int32(st::webPageLeft + _description.maxWidth()));
			_minh += qMin(_description.minHeight(), 3 * st::webPageTitleFont->height);
		}
	}
	if (!_asArticle && (data->photo || data->doc) && (_siteNameWidth || !_title.isEmpty() || !_description.isEmpty())) {
		_minh += st::webPagePhotoSkip;
	}
	if (data->type == WebPageVideo && data->duration) {
		_duration = formatDurationText(data->duration);
		_durationWidth = st::msgDateFont->width(_duration);
	}
	_height = _minh;
}

void HistoryWebPage::draw(Painter &p, const HistoryItem *parent, bool selected, int32 width) const {
	if (width < 0) width = w;
	if (width < 1 || data->pendingTill) return;

	int16 animw = 0, animh = 0;
	if (data->doc && animated.msg == parent) {
		animw = animated.w / cIntRetinaFactor();
		animh = animated.h / cIntRetinaFactor();
		if (width - st::webPageLeft < animw) {
			animw = width - st::webPageLeft;
			animh = (animw * animated.h / animated.w);
			if (animh < 1) animh = 1;
		}
	}

	int32 bottomSkip = 0;
	if (data->photo) {
		bottomSkip += st::webPagePhotoSkip;
		if (_asArticle || (st::webPageLeft + qMax(_pixw, int16(st::minPhotoSize)) + parent->skipBlockWidth() > width)) {
			bottomSkip += (st::msgDateFont->height - st::msgDateDelta.y());
		}
	} else if (data->doc && animated.msg == parent) {
		bottomSkip += st::webPagePhotoSkip;
		if (st::webPageLeft + qMax(animw, int16(st::minPhotoSize)) + parent->skipBlockWidth() > width) {
			bottomSkip += (st::msgDateFont->height - st::msgDateDelta.y());
		}
	}

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	style::color bar = (selected ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor));
	style::color semibold = (selected ? (outbg ? st::msgOutServiceSelColor : st::msgInServiceSelColor) : (outbg ? st::msgOutServiceColor : st::msgInServiceColor));
	style::color regular = (selected ? (outbg ? st::msgOutSelectDateColor : st::msgInSelectDateColor) : (outbg ? st::msgOutDateColor : st::msgInDateColor));
	p.fillRect(0, 0, st::webPageBar, _height - bottomSkip, bar->b);

	p.save();
	p.translate(st::webPageLeft, 0);

	width -= st::webPageLeft;

	if (_asArticle) {
		int32 pixwidth = st::webPagePhotoSize, pixheight = st::webPagePhotoSize;
		data->photo->medium->load(false, false);
		bool full = data->photo->medium->loaded();
		QPixmap pix;
		if (full) {
			pix = data->photo->medium->pixSingle(_pixw, _pixh, pixwidth, pixheight);
		} else {
			pix = data->photo->thumb->pixBlurredSingle(_pixw, _pixh, pixwidth, pixheight);
		}
		p.drawPixmap(width - pixwidth, 0, pix);
		if (selected) {
			App::roundRect(p, width - pixwidth, 0, pixwidth, pixheight, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
		}
	}
	int32 articleLines = 5;
	if (_siteNameWidth) {
		int32 availw = width;
		if (_asArticle) {
			availw -= st::webPagePhotoSize + st::webPagePhotoDelta;
		} else if (_title.isEmpty() && _description.isEmpty() && !data->photo) {
			availw -= parent->skipBlockWidth();
		}
		p.setFont(st::webPageTitleFont->f);
		p.setPen(semibold->p);
		p.drawText(0, st::webPageTitleFont->ascent, (availw >= _siteNameWidth) ? data->siteName : st::webPageTitleFont->elided(data->siteName, availw));
		p.translate(0, st::webPageTitleFont->height);
		--articleLines;
	}
	if (!_title.isEmpty()) {
		p.setPen(st::black->p);
		int32 availw = width, endskip = 0;
		if (_asArticle) {
			availw -= st::webPagePhotoSize + st::webPagePhotoDelta;
		} else if (_description.isEmpty() && !data->photo) {
			endskip = parent->skipBlockWidth();
		}
		_title.drawElided(p, 0, 0, availw, 2, style::al_left, 0, -1, endskip);
		int32 h = _title.countHeight(availw);
		if (h > st::webPageTitleFont->height) {
			articleLines -= 2;
			p.translate(0, st::webPageTitleFont->height * 2);
		} else {
			--articleLines;
			p.translate(0, h);
		}
	}
	if (!_description.isEmpty()) {
		p.setPen(st::black->p);
		int32 availw = width, endskip = 0;
		if (_asArticle) {
			availw -= st::webPagePhotoSize + st::webPagePhotoDelta;
			if (articleLines > 3) articleLines = 3;
		} else {
			if (!data->photo) endskip = parent->skipBlockWidth();
			articleLines = 3;
		}
		_description.drawElided(p, 0, 0, availw, articleLines, style::al_left, 0, -1, endskip);
		p.translate(0, qMin(_description.countHeight(availw), st::webPageDescriptionFont->height * articleLines));
	}
	if (!_asArticle && data->photo) {
		if (_siteNameWidth || !_title.isEmpty() || !_description.isEmpty()) {
			p.translate(0, st::webPagePhotoSkip);
		}

		int32 pixwidth = qMax(_pixw, int16(st::minPhotoSize)), pixheight = qMax(_pixh, int16(st::minPhotoSize));
		data->photo->full->load(false, false);
		bool full = data->photo->full->loaded();
		QPixmap pix;
		if (full) {
			pix = data->photo->full->pixSingle(_pixw, _pixh, pixwidth, pixheight);
		} else {
			pix = data->photo->thumb->pixBlurredSingle(_pixw, _pixh, pixwidth, pixheight);
		}
		p.drawPixmap(0, 0, pix);
		if (!full) {
			uint64 dt = itemAnimations().animate(parent, getms());
			int32 cnt = int32(st::photoLoaderCnt), period = int32(st::photoLoaderPeriod), t = dt % period, delta = int32(st::photoLoaderDelta);

			int32 x = (pixwidth - st::photoLoader.width()) / 2, y = (pixheight - st::photoLoader.height()) / 2;
			p.fillRect(x, y, st::photoLoader.width(), st::photoLoader.height(), st::photoLoaderBg->b);
			x += (st::photoLoader.width() - cnt * st::photoLoaderPoint.width() - (cnt - 1) * st::photoLoaderSkip) / 2;
			y += (st::photoLoader.height() - st::photoLoaderPoint.height()) / 2;
			QColor c(st::white->c);
			QBrush b(c);
			for (int32 i = 0; i < cnt; ++i) {
				t -= delta;
				while (t < 0) t += period;

				float64 alpha = (t >= st::photoLoaderDuration1 + st::photoLoaderDuration2) ? 0 : ((t > st::photoLoaderDuration1 ? ((st::photoLoaderDuration1 + st::photoLoaderDuration2 - t) / st::photoLoaderDuration2) : (t / st::photoLoaderDuration1)));
				c.setAlphaF(st::photoLoaderAlphaMin + alpha * (1 - st::photoLoaderAlphaMin));
				b.setColor(c);
				p.fillRect(x + i * (st::photoLoaderPoint.width() + st::photoLoaderSkip), y, st::photoLoaderPoint.width(), st::photoLoaderPoint.height(), b);
			}
		}

		if (selected) {
			App::roundRect(p, 0, 0, pixwidth, pixheight, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
		}

		if (data->type == WebPageVideo) {
			if (data->siteName == qstr("YouTube")) {
				p.drawPixmap(QPoint((pixwidth - st::youtubeIcon.pxWidth()) / 2, (pixheight - st::youtubeIcon.pxHeight()) / 2), App::sprite(), st::youtubeIcon);
			} else {
				p.drawPixmap(QPoint((pixwidth - st::videoIcon.pxWidth()) / 2, (pixheight - st::videoIcon.pxHeight()) / 2), App::sprite(), st::videoIcon);
			}
			if (_durationWidth) {
				int32 dateX = pixwidth - _durationWidth - st::msgDateImgDelta - 2 * st::msgDateImgPadding.x();
				int32 dateY = pixheight - st::msgDateFont->height - 2 * st::msgDateImgPadding.y() - st::msgDateImgDelta;
				int32 dateW = pixwidth - dateX - st::msgDateImgDelta;
				int32 dateH = pixheight - dateY - st::msgDateImgDelta;

				App::roundRect(p, dateX, dateY, dateW, dateH, selected ? st::msgDateImgSelectBg : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);

				p.setFont(st::msgDateFont->f);
				p.setPen(st::msgDateImgColor->p);
				p.drawText(dateX + st::msgDateImgPadding.x(), dateY + st::msgDateImgPadding.y() + st::msgDateFont->ascent, _duration);
			}
		}

		p.translate(0, pixheight);
	} else if (!_asArticle && data->doc) {
		if (_siteNameWidth || !_title.isEmpty() || !_description.isEmpty()) {
			p.translate(0, st::webPagePhotoSkip);
		}

		if (parent == animated.msg) {
			p.drawPixmap(0, 0, animated.current(animw * cIntRetinaFactor(), animh * cIntRetinaFactor(), true));
			if (selected) {
				App::roundRect(p, 0, 0, animw, animh, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
			}
		} else {
			QString statusText;
			if (data->doc->song()) {
				SongMsgId playing;
				AudioPlayerState playingState = AudioPlayerStopped;
				int64 playingPosition = 0, playingDuration = 0;
				int32 playingFrequency = 0;
				if (audioPlayer()) {
					audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
				}

				bool already = !data->doc->already().isEmpty(), hasdata = !data->doc->data.isEmpty();
				QRect img;
				if (data->doc->status == FileFailed) {
					statusText = lang(lng_attach_failed);
					img = outbg ? st::mediaMusicOutImg : st::mediaMusicInImg;
				} else if (already || hasdata) {
					bool showPause = false;
					if (playing.msgId == parent->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
						statusText = formatDurationText(playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency)) + qsl(" / ") + formatDurationText(playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
						showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
					} else {
						statusText = formatDurationText(data->doc->song()->duration);
					}
					if (!showPause && playing.msgId == parent->fullId() && App::main() && App::main()->player()->seekingSong(playing)) showPause = true;
					img = outbg ? (showPause ? st::mediaPauseOutImg : st::mediaPlayOutImg) : (showPause ? st::mediaPauseInImg : st::mediaPlayInImg);
				} else {
					if (data->doc->loader) {
						int32 offset = data->doc->loader->currentOffset();
						if (_docDownloadTextCache.isEmpty() || _docDownloadDone != offset) {
							_docDownloadDone = offset;
							_docDownloadTextCache = formatDownloadText(_docDownloadDone, data->doc->size);
						}
						statusText = _docDownloadTextCache;
					} else {
						statusText = _docSize;
					}
					img = outbg ? st::mediaMusicOutImg : st::mediaMusicInImg;
				}

				p.drawPixmap(QPoint(0, 0), App::sprite(), img);
			} else {
				if (data->doc->status == FileFailed) {
					statusText = lang(lng_attach_failed);
				} else if (data->doc->loader) {
					int32 offset = data->doc->loader->currentOffset();
					if (_docDownloadTextCache.isEmpty() || _docDownloadDone != offset) {
						_docDownloadDone = offset;
						_docDownloadTextCache = formatDownloadText(_docDownloadDone, data->doc->size);
					}
					statusText = _docDownloadTextCache;
				} else {
					statusText = _docSize;
				}

				if (_docThumbWidth) {
					data->doc->thumb->checkload();
					p.drawPixmap(QPoint(0, 0), data->doc->thumb->pixSingle(_docThumbWidth, 0, st::mediaThumbSize, st::mediaThumbSize));
				} else {
					p.drawPixmap(QPoint(0, 0), App::sprite(), (outbg ? st::mediaDocOutImg : st::mediaDocInImg));
				}
			}
			if (selected) {
				App::roundRect(p, 0, 0, st::mediaThumbSize, st::mediaThumbSize, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
			}

			int32 tleft = st::mediaPadding.left() + st::mediaThumbSize + st::mediaPadding.right();
			int32 twidth = width - tleft - st::mediaPadding.right();
			int32 secondwidth = width - tleft - st::msgPadding.right() - parent->skipBlockWidth();

			p.setFont(st::mediaFont->f);
			p.setPen(st::black->c);
			if (twidth < _docNameWidth) {
				p.drawText(tleft, st::mediaNameTop + st::mediaFont->ascent, st::mediaFont->elided(_docName, twidth));
			} else {
				p.drawText(tleft, st::mediaNameTop + st::mediaFont->ascent, _docName);
			}

			style::color status(selected ? (outbg ? st::mediaOutSelectColor : st::mediaInSelectColor) : (outbg ? st::mediaOutColor : st::mediaInColor));
			p.setPen(status->p);

			p.drawText(tleft, st::mediaThumbSize - st::mediaDetailsShift - st::mediaFont->descent, statusText);
		}
	}

	p.restore();
}

int32 HistoryWebPage::resize(int32 width, const HistoryItem *parent) {
	if (data->pendingTill) {
		w = width;
		_height = _minh;
		return _height;
	}

	w = width;
	width -= st::webPageLeft;
	if (_asArticle) {
		int32 tw = convertScale(data->photo->medium->width()), th = convertScale(data->photo->medium->height());
		if (tw > st::webPagePhotoSize) {
			if (th > tw) {
				th = th * st::webPagePhotoSize / tw;
				tw = st::webPagePhotoSize;
			} else if (th > st::webPagePhotoSize) {
				tw = tw * st::webPagePhotoSize / th;
				th = st::webPagePhotoSize;
			}
		}
		_pixw = tw;
		_pixh = th;
		if (_pixw < 1) _pixw = 1;
		if (_pixh < 1) _pixh = 1;
		_height = st::webPagePhotoSize;
		_height += st::webPagePhotoSkip + (st::msgDateFont->height - st::msgDateDelta.y());
	} else if (data->photo) {
		_pixw = qMin(width, int32(_maxw - st::webPageLeft - parent->skipBlockWidth()));

		int32 tw = convertScale(data->photo->full->width()), th = convertScale(data->photo->full->height());
		if (tw > st::maxMediaSize) {
			th = (st::maxMediaSize * th) / tw;
			tw = st::maxMediaSize;
		}
		if (th > st::maxMediaSize) {
			tw = (st::maxMediaSize * tw) / th;
			th = st::maxMediaSize;
		}
		_pixh = th;
		if (tw > _pixw) {
			_pixh = (_pixw * _pixh / tw);
		} else {
			_pixw = tw;
		}
		if (_pixh > width) {
			_pixw = (_pixw * width) / _pixh;
			_pixh = width;
		}
		if (_pixw < 1) _pixw = 1;
		if (_pixh < 1) _pixh = 1;
		_height = qMax(_pixh, int16(st::minPhotoSize));
		_height += st::webPagePhotoSkip;
		if (qMax(_pixw, int16(st::minPhotoSize)) + parent->skipBlockWidth() > width) {
			_height += (st::msgDateFont->height - st::msgDateDelta.y());
		}
	} else if (data->doc) {
		if (parent == animated.msg) {
			int32 w = qMin(width, int32(animated.w / cIntRetinaFactor()));
			if (w > st::maxMediaSize) {
				w = st::maxMediaSize;
			}
			_height = animated.h / cIntRetinaFactor();
			if (animated.w / cIntRetinaFactor() > w) {
				_height = (w * _height / (animated.w / cIntRetinaFactor()));
				if (_height <= 0) _height = 1;
			}
			_height += st::webPagePhotoSkip;
			if (w + parent->skipBlockWidth() > width) {
				_height += (st::msgDateFont->height - st::msgDateDelta.y());
			}
		} else {
			_height = st::mediaThumbSize;
		}
	} else {
		_height = 0;
	}

	if (!_asArticle) {
		if (!data->siteName.isEmpty()) {
			_height += st::webPageTitleFont->height;
		}
		if (!_title.isEmpty()) {
			_height += qMin(_title.countHeight(width), st::webPageTitleFont->height * 2);
		}
		if (!_description.isEmpty()) {
			_height += qMin(_description.countHeight(width), st::webPageDescriptionFont->height * 3);
		}
		if ((data->photo || data->doc) && (_siteNameWidth || !_title.isEmpty() || !_description.isEmpty())) {
			_height += st::webPagePhotoSkip;
		}
	}

	return _height;
}

void HistoryWebPage::regItem(HistoryItem *item) {
	App::regWebPageItem(data, item);
	if (data->doc) App::regDocumentItem(data->doc, item);
}

void HistoryWebPage::unregItem(HistoryItem *item) {
	App::unregWebPageItem(data, item);
	if (data->doc) App::unregDocumentItem(data->doc, item);
}

const QString HistoryWebPage::inDialogsText() const {
	return QString();
}

const QString HistoryWebPage::inHistoryText() const {
	return QString();
}

bool HistoryWebPage::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width >= _maxw) width = _maxw;

	return (x >= 0 && y >= 0 && x < width && y < _height);
}

void HistoryWebPage::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	if (width < 1) return;

	width -= st::webPageLeft;
	x -= st::webPageLeft;

	if (_asArticle) {
		int32 pixwidth = st::webPagePhotoSize, pixheight = st::webPagePhotoSize;
		if (x >= width - pixwidth && x < width && y >= 0 && y < pixheight) {
			lnk = _openl;
			return;
		}
	}
	int32 articleLines = 5;
	if (_siteNameWidth) {
		y -= st::webPageTitleFont->height;
		--articleLines;
	}
	if (!_title.isEmpty()) {
		int32 availw = width;
		if (_asArticle) {
			availw -= st::webPagePhotoSize + st::webPagePhotoDelta;
		}
		int32 h = _title.countHeight(availw);
		if (h > st::webPageTitleFont->height) {
			articleLines -= 2;
			y -= st::webPageTitleFont->height * 2;
		} else {
			--articleLines;
			y -= h;
		}
	}
	if (!_description.isEmpty()) {
		int32 availw = width;
		if (_asArticle) {
			availw -= st::webPagePhotoSize + st::webPagePhotoDelta;
			if (articleLines > 3) articleLines = 3;
		} else if (!data->photo) {
			articleLines = 3;
		}
		int32 desch = qMin(_description.countHeight(width), st::webPageDescriptionFont->height * articleLines);
		if (y >= 0 && y < desch) {
			bool inText = false;
			_description.getState(lnk, inText, x, y, availw);
			state = inText ? HistoryInTextCursorState : HistoryDefaultCursorState;
			return;
		}
		y -= desch;
	}
	if (_siteNameWidth || !_title.isEmpty() || !_description.isEmpty()) {
		y -= st::webPagePhotoSkip;
	}
	if (!_asArticle) {
		if (data->doc && parent == animated.msg) {
			int32 h = (width == w) ? _height : (width * animated.h / animated.w);
			if (h < 1) h = 1;
			if (x >= 0 && y >= 0 && x < width && y < h) {
				lnk = _attachl;
				return;
			}
		} else {
			int32 attachwidth = data->doc ? (width - st::mediaPadding.right()) : qMax(_pixw, int16(st::minPhotoSize));
			int32 attachheight = data->doc ? st::mediaThumbSize : qMax(_pixh, int16(st::minPhotoSize));
			if (x >= 0 && y >= 0 && x < attachwidth && y < attachheight) {
				lnk = _attachl ? _attachl : _openl;
				return;
			}
		}
	}
}

HistoryMedia *HistoryWebPage::clone() const {
	return new HistoryWebPage(*this);
}

ImagePtr HistoryWebPage::replyPreview() {
	return data->photo ? data->photo->makeReplyPreview() : (data->doc ? data->doc->makeReplyPreview() : ImagePtr());
}

namespace {
	QRegularExpression reYouTube1(qsl("^(https?://)?(www\\.|m\\.)?youtube\\.com/watch\\?([^#]+&)?v=([a-z0-9_-]+)(&[^\\s]*)?$"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpression reYouTube2(qsl("^(https?://)?(www\\.)?youtu\\.be/([a-z0-9_-]+)([/\\?#][^\\s]*)?$"), QRegularExpression::CaseInsensitiveOption);
	QRegularExpression reInstagram(qsl("^(https?://)?(www\\.)?instagram\\.com/p/([a-z0-9_-]+)([/\\?][^\\s]*)?$"), QRegularExpression::CaseInsensitiveOption);
	QRegularExpression reVimeo(qsl("^(https?://)?(www\\.)?vimeo\\.com/([0-9]+)([/\\?][^\\s]*)?$"), QRegularExpression::CaseInsensitiveOption);

	ImageLinkManager manager;
}

void ImageLinkManager::init() {
	if (manager) delete manager;
	manager = new QNetworkAccessManager();
	App::setProxySettings(*manager);

	connect(manager, SIGNAL(authenticationRequired(QNetworkReply*, QAuthenticator*)), this, SLOT(onFailed(QNetworkReply*)));
	connect(manager, SIGNAL(sslErrors(QNetworkReply*, const QList<QSslError>&)), this, SLOT(onFailed(QNetworkReply*)));
	connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(onFinished(QNetworkReply*)));

	if (black) delete black;
	QImage b(cIntRetinaFactor(), cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	{
		QPainter p(&b);
		p.fillRect(QRect(0, 0, cIntRetinaFactor(), cIntRetinaFactor()), st::white->b);
	}
	QPixmap p = QPixmap::fromImage(b, Qt::ColorOnly);
	p.setDevicePixelRatio(cRetinaFactor());
	black = new ImagePtr(p, "PNG");
}

void ImageLinkManager::reinit() {
	if (manager) App::setProxySettings(*manager);
}

void ImageLinkManager::deinit() {
	if (manager) {
		delete manager;
		manager = 0;
	}
	if (black) {
		delete black;
		black = 0;
	}
	dataLoadings.clear();
	imageLoadings.clear();
}

void initImageLinkManager() {
	manager.init();
}

void reinitImageLinkManager() {
	manager.reinit();
}

void deinitImageLinkManager() {
	manager.deinit();
}

void ImageLinkManager::getData(ImageLinkData *data) {
	if (!manager) {
		DEBUG_LOG(("App Error: getting image link data without manager init!"));
		return failed(data);
	}
	QString url;
	switch (data->type) {
	case YouTubeLink: {
		url = qsl("https://gdata.youtube.com/feeds/api/videos/") + data->id.mid(8) + qsl("?v=2&alt=json");
		QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));
		dataLoadings[reply] = data;
	} break;
	case VimeoLink: {
		url = qsl("https://vimeo.com/api/v2/video/") + data->id.mid(6) + qsl(".json");
		QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));
		dataLoadings[reply] = data;
	} break;
	case InstagramLink: {
		//url = qsl("https://api.instagram.com/oembed?url=http://instagr.am/p/") + data->id.mid(10) + '/';
		url = qsl("https://instagram.com/p/") + data->id.mid(10) + qsl("/media/?size=l");
		QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));
		imageLoadings[reply] = data;
	} break;
	case GoogleMapsLink: {
		int32 w = st::locationSize.width(), h = st::locationSize.height();
		int32 zoom = 13, scale = 1;
		if (cScale() == dbisTwo || cRetina()) {
			scale = 2;
		} else {
			w = convertScale(w);
			h = convertScale(h);
		}
		url = qsl("https://maps.googleapis.com/maps/api/staticmap?center=") + data->id.mid(9) + qsl("&zoom=%1&size=%2x%3&maptype=roadmap&scale=%4&markers=color:red|size:big|").arg(zoom).arg(w).arg(h).arg(scale) + data->id.mid(9) + qsl("&sensor=false");
		QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));
		imageLoadings[reply] = data;
	} break;
	default: {
		failed(data);
	} break;
	}
}

void ImageLinkManager::onFinished(QNetworkReply *reply) {
	if (!manager) return;
	if (reply->error() != QNetworkReply::NoError) return onFailed(reply);

	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	if (statusCode.isValid()) {
		int status = statusCode.toInt();
		if (status == 301 || status == 302) {
			QString loc = reply->header(QNetworkRequest::LocationHeader).toString();
			if (!loc.isEmpty()) {
				QMap<QNetworkReply*, ImageLinkData*>::iterator i = dataLoadings.find(reply);
				if (i != dataLoadings.cend()) {
					ImageLinkData *d = i.value();
					if (serverRedirects.constFind(d) == serverRedirects.cend()) {
						serverRedirects.insert(d, 1);
					} else if (++serverRedirects[d] > MaxHttpRedirects) {
						DEBUG_LOG(("Network Error: Too many HTTP redirects in onFinished() for image link: %1").arg(loc));
						return onFailed(reply);
					}
					dataLoadings.erase(i);
					dataLoadings.insert(manager->get(QNetworkRequest(loc)), d);
					return;
				} else if ((i = imageLoadings.find(reply)) != imageLoadings.cend()) {
					ImageLinkData *d = i.value();
					if (serverRedirects.constFind(d) == serverRedirects.cend()) {
						serverRedirects.insert(d, 1);
					} else if (++serverRedirects[d] > MaxHttpRedirects) {
						DEBUG_LOG(("Network Error: Too many HTTP redirects in onFinished() for image link: %1").arg(loc));
						return onFailed(reply);
					}
					imageLoadings.erase(i);
					imageLoadings.insert(manager->get(QNetworkRequest(loc)), d);
					return;
				}
			}
		}
		if (status != 200) {
			DEBUG_LOG(("Network Error: Bad HTTP status received in onFinished() for image link: %1").arg(status));
			return onFailed(reply);
		}
	}

	ImageLinkData *d = 0;
	QMap<QNetworkReply*, ImageLinkData*>::iterator i = dataLoadings.find(reply);
	if (i != dataLoadings.cend()) {
		d = i.value();
		dataLoadings.erase(i);

		QJsonParseError e;
		QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &e);
		if (e.error != QJsonParseError::NoError) {
			DEBUG_LOG(("JSON Error: Bad json received in onFinished() for image link"));
			return onFailed(reply);
		}
		switch (d->type) {
		case YouTubeLink: {
			QJsonObject obj = doc.object();
			QString thumb;
			int32 seconds = 0;
			QJsonObject::const_iterator entryIt = obj.constFind(qsl("entry"));
			if (entryIt != obj.constEnd() && entryIt.value().isObject()) {
				QJsonObject entry = entryIt.value().toObject();
				QJsonObject::const_iterator mediaIt = entry.constFind(qsl("media$group"));
				if (mediaIt != entry.constEnd() && mediaIt.value().isObject()) {
					QJsonObject media = mediaIt.value().toObject();

					// title from media
					QJsonObject::const_iterator titleIt = media.constFind(qsl("media$title"));
					if (titleIt != media.constEnd() && titleIt.value().isObject()) {
						QJsonObject title = titleIt.value().toObject();
						QJsonObject::const_iterator tIt = title.constFind(qsl("$t"));
						if (tIt != title.constEnd() && tIt.value().isString()) {
							d->title = tIt.value().toString();
						}
					}

					// thumb
					QJsonObject::const_iterator thumbnailsIt = media.constFind(qsl("media$thumbnail"));
					int32 bestLevel = 0;
					if (thumbnailsIt != media.constEnd() && thumbnailsIt.value().isArray()) {
						QJsonArray thumbnails = thumbnailsIt.value().toArray();
						for (int32 i = 0, l = thumbnails.size(); i < l; ++i) {
							QJsonValue thumbnailVal = thumbnails.at(i);
							if (!thumbnailVal.isObject()) continue;
							
							QJsonObject thumbnail = thumbnailVal.toObject();
							QJsonObject::const_iterator urlIt = thumbnail.constFind(qsl("url"));
							if (urlIt == thumbnail.constEnd() || !urlIt.value().isString()) continue;

							int32 level = 0;
							if (thumbnail.constFind(qsl("time")) == thumbnail.constEnd()) {
								level += 10;
							}
							QJsonObject::const_iterator wIt = thumbnail.constFind(qsl("width"));
							if (wIt != thumbnail.constEnd()) {
								int32 w = 0;
								if (wIt.value().isDouble()) {
									w = qMax(qRound(wIt.value().toDouble()), 0);
								} else if (wIt.value().isString()) {
									w = qMax(qRound(wIt.value().toString().toDouble()), 0);
								}
								switch (w) {
								case 640: level += 4; break;
								case 480: level += 3; break;
								case 320: level += 2; break;
								case 120: level += 1; break;
								}
							}
							if (level > bestLevel) {
								thumb = urlIt.value().toString();
								bestLevel = level;
							}
						}
					}

					// duration
					QJsonObject::const_iterator durationIt = media.constFind(qsl("yt$duration"));
					if (durationIt != media.constEnd() && durationIt.value().isObject()) {
						QJsonObject duration = durationIt.value().toObject();
						QJsonObject::const_iterator secondsIt = duration.constFind(qsl("seconds"));
						if (secondsIt != duration.constEnd()) {
							if (secondsIt.value().isDouble()) {
								seconds = qRound(secondsIt.value().toDouble());
							} else if (secondsIt.value().isString()) {
								seconds = qRound(secondsIt.value().toString().toDouble());
							}
						}
					}
				}

				// title field
				if (d->title.isEmpty()) {
					QJsonObject::const_iterator titleIt = entry.constFind(qsl("title"));
					if (titleIt != entry.constEnd() && titleIt.value().isObject()) {
						QJsonObject title = titleIt.value().toObject();
						QJsonObject::const_iterator tIt = title.constFind(qsl("$t"));
						if (tIt != title.constEnd() && tIt.value().isString()) {
							d->title = tIt.value().toString();
						}
					}
				}
			}

			if (seconds > 0) {
				d->duration = formatDurationText(seconds);
			}
			if (thumb.isEmpty()) {
				failed(d);
			} else {
				imageLoadings.insert(manager->get(QNetworkRequest(thumb)), d);
			}
		} break;
		
		case VimeoLink: {
			QString thumb;
			int32 seconds = 0;
			QJsonArray arr = doc.array();
			if (!arr.isEmpty()) {
				QJsonObject obj = arr.at(0).toObject();
				QJsonObject::const_iterator titleIt = obj.constFind(qsl("title"));
				if (titleIt != obj.constEnd() && titleIt.value().isString()) {
					d->title = titleIt.value().toString();
				}
				QJsonObject::const_iterator thumbnailsIt = obj.constFind(qsl("thumbnail_large"));
				if (thumbnailsIt != obj.constEnd() && thumbnailsIt.value().isString()) {
					thumb = thumbnailsIt.value().toString();
				}
				QJsonObject::const_iterator secondsIt = obj.constFind(qsl("duration"));
				if (secondsIt != obj.constEnd()) {
					if (secondsIt.value().isDouble()) {
						seconds = qRound(secondsIt.value().toDouble());
					} else if (secondsIt.value().isString()) {
						seconds = qRound(secondsIt.value().toString().toDouble());
					}
				}
			}
			if (seconds > 0) {
				d->duration = formatDurationText(seconds);
			}
			if (thumb.isEmpty()) {
				failed(d);
			} else {
				imageLoadings.insert(manager->get(QNetworkRequest(thumb)), d);
			}
		} break;

		case InstagramLink: failed(d); break;
		case GoogleMapsLink: failed(d); break;
		}

		if (App::main()) App::main()->update();
	} else {
		i = imageLoadings.find(reply);
		if (i != imageLoadings.cend()) {
			d = i.value();
			imageLoadings.erase(i);

			QPixmap thumb;
			QByteArray format;
			QByteArray data(reply->readAll());
			{
				QBuffer buffer(&data);
				QImageReader reader(&buffer);
				thumb = QPixmap::fromImageReader(&reader, Qt::ColorOnly);
				format = reader.format();
				thumb.setDevicePixelRatio(cRetinaFactor());
				if (format.isEmpty()) format = QByteArray("JPG");
			}
			d->loading = false;
			d->thumb = thumb.isNull() ? (*black) : ImagePtr(thumb, format);
			serverRedirects.remove(d);
			if (App::main()) App::main()->update();
		}
	}
}

void ImageLinkManager::onFailed(QNetworkReply *reply) {
	if (!manager) return;

	ImageLinkData *d = 0;
	QMap<QNetworkReply*, ImageLinkData*>::iterator i = dataLoadings.find(reply);
	if (i != dataLoadings.cend()) {
		d = i.value();
		dataLoadings.erase(i);
	} else {
		i = imageLoadings.find(reply);
		if (i != imageLoadings.cend()) {
			d = i.value();
			imageLoadings.erase(i);
		}
	}
	DEBUG_LOG(("Network Error: failed to get data for image link %1, error %2").arg(d ? d->id : 0).arg(reply->errorString()));
	if (d) {
		failed(d);
	}
}

void ImageLinkManager::failed(ImageLinkData *data) {
	data->loading = false;
	data->thumb = *black;
	serverRedirects.remove(data);
}

void ImageLinkData::load() {
	if (!thumb->isNull()) return thumb->load(false, false);
	if (loading) return;

	loading = true;
	manager.getData(this);
}

HistoryImageLink::HistoryImageLink(const QString &url, const QString &title, const QString &description) : HistoryMedia(),
_title(st::msgMinWidth),
_description(st::msgMinWidth) {
	if (!title.isEmpty()) {
		_title.setText(st::webPageTitleFont, textClean(title), _webpageTitleOptions);
	}
	if (!description.isEmpty()) {
		_description.setText(st::webPageDescriptionFont, textClean(description), _webpageDescriptionOptions);
	}

	if (url.startsWith(qsl("location:"))) {
		QString lnk = qsl("https://maps.google.com/maps?q=") + url.mid(9) + qsl("&ll=") + url.mid(9) + qsl("&z=17");
		link.reset(new TextLink(lnk));

		data = App::imageLinkSet(url, GoogleMapsLink, lnk);
	} else {
		link.reset(new TextLink(url));

		int matchIndex = 4;
		QRegularExpressionMatch m = reYouTube1.match(url);
		if (!m.hasMatch()) {
			m = reYouTube2.match(url);
			matchIndex = 3;
		}
		if (m.hasMatch()) {
			data = App::imageLinkSet(qsl("youtube:") + m.captured(matchIndex), YouTubeLink, url);
		} else {
			m = reVimeo.match(url);
			if (m.hasMatch()) {
				data = App::imageLinkSet(qsl("vimeo:") + m.captured(3), VimeoLink, url);
			} else {
				m = reInstagram.match(url);
				if (m.hasMatch()) {
					data = App::imageLinkSet(qsl("instagram:") + m.captured(3), InstagramLink, url);
					data->title = qsl("instagram.com/p/") + m.captured(3);
				} else {
					data = 0;
				}
			}
		}
	}
}

int32 HistoryImageLink::fullWidth() const {
	if (data) {
		switch (data->type) {
		case YouTubeLink: return 640;
		case VimeoLink: return 640;
		case InstagramLink: return 640;
		case GoogleMapsLink: return st::locationSize.width();
		}
	}
	return st::minPhotoSize;
}

int32 HistoryImageLink::fullHeight() const {
	if (data) {
		switch (data->type) {
		case YouTubeLink: return 480;
		case VimeoLink: return 480;
		case InstagramLink: return 640;
		case GoogleMapsLink: return st::locationSize.height();
		}
	}
	return st::minPhotoSize;
}

void HistoryImageLink::initDimensions(const HistoryItem *parent) {
	int32 tw = convertScale(fullWidth()), th = convertScale(fullHeight());
	int32 thumbw = qMax(tw, int32(st::minPhotoSize)), maxthumbh = thumbw;
	int32 thumbh = qRound(th * float64(thumbw) / tw);
	if (thumbh > maxthumbh) {
		thumbw = qRound(thumbw * float64(maxthumbh) / thumbh);
		thumbh = maxthumbh;
		if (thumbw < st::minPhotoSize) {
			thumbw = st::minPhotoSize;
		}
	}
	if (thumbh < st::minPhotoSize) {
		thumbh = st::minPhotoSize;
	}
	if (!w) {
		w = thumbw;
	}
	_maxw = w;
	_minh = thumbh;
	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = toHistoryForwarded(parent);
	if (reply || !_title.isEmpty() || !_description.isEmpty()) {
		_maxw += st::mediaPadding.left() + st::mediaPadding.right();
		if (reply) {
			_minh += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		} else {
			if (!parent->displayFromName() || !fwd) {
				_minh += st::msgPadding.top();
			}
			if (fwd) {
				_minh += st::msgServiceNameFont->height + st::msgPadding.top();
			}
		}
		if (parent->displayFromName()) {
			_minh += st::msgPadding.top() + st::msgNameFont->height;
		}
		if (!_title.isEmpty()) {
			_maxw = qMax(_maxw, int32(st::webPageLeft + _title.maxWidth()));
			_minh += qMin(_title.minHeight(), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			_maxw = qMax(_maxw, int32(st::webPageLeft + _description.maxWidth()));
			_minh += qMin(_description.minHeight(), 3 * st::webPageTitleFont->height);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			_minh += st::webPagePhotoSkip;
		}
		_minh += st::mediaPadding.bottom();
	}
	_height = _minh;
}

void HistoryImageLink::draw(Painter &p, const HistoryItem *parent, bool selected, int32 width) const {
	if (width < 0) width = w;
	int skipx = 0, skipy = 0, height = _height;
	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = toHistoryForwarded(parent);
	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	if (reply || !_title.isEmpty() || !_description.isEmpty()) {
		skipx = st::mediaPadding.left();

		style::color bg(selected ? (outbg ? st::msgOutSelectBg : st::msgInSelectBg) : (outbg ? st::msgOutBg : st::msgInBg));
		style::color sh(selected ? (outbg ? st::msgOutSelectShadow : st::msgInSelectShadow) : (outbg ? st::msgOutShadow : st::msgInShadow));
		RoundCorners cors(selected ? (outbg ? MessageOutSelectedCorners : MessageInSelectedCorners) : (outbg ? MessageOutCorners : MessageInCorners));
		App::roundRect(p, 0, 0, width, _height, bg, cors, &sh);

		int replyFrom = 0, fwdFrom = 0;
		if (parent->displayFromName()) {
			replyFrom = st::msgPadding.top() + st::msgNameFont->height;
			fwdFrom = st::msgPadding.top() + st::msgNameFont->height;
			skipy += replyFrom;
			p.setFont(st::msgNameFont->f);
			if (fromChannel) {
				p.setPen(selected ? st::msgInServiceSelColor : st::msgInServiceColor);
			} else {
				p.setPen(parent->from()->color);
			}
			parent->from()->nameText.drawElided(p, st::mediaPadding.left(), st::msgPadding.top(), width - st::mediaPadding.left() - st::mediaPadding.right());
			if (!fwd && !reply) skipy += st::msgPadding.top();
		} else if (!reply) {
			fwdFrom = st::msgPadding.top();
			skipy += fwdFrom;
		}
		if (reply) {
			skipy += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
			reply->drawReplyTo(p, st::msgReplyPadding.left(), replyFrom, width - st::msgReplyPadding.left() - st::msgReplyPadding.right(), selected);
		} else if (fwd) {
			skipy += st::msgServiceNameFont->height + st::msgPadding.top();
			fwd->drawForwardedFrom(p, st::mediaPadding.left(), fwdFrom, width - st::mediaPadding.left() - st::mediaPadding.right(), selected);
		}
		width -= st::mediaPadding.left() + st::mediaPadding.right();

		if (!_title.isEmpty()) {
			p.setPen(st::black->p);
			_title.drawElided(p, st::mediaPadding.left(), skipy, width, 2);
			skipy += qMin(_title.countHeight(width), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			p.setPen(st::black->p);
			_description.drawElided(p, st::mediaPadding.left(), skipy, width, 3);
			skipy += qMin(_description.countHeight(width), 3 * st::webPageDescriptionFont->height);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			skipy += st::webPagePhotoSkip;
		}
		height -= skipy + st::mediaPadding.bottom();
	} else {
		App::roundShadow(p, 0, 0, width, _height, selected ? st::msgInSelectShadow : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	data->load();
	QPixmap toDraw;
	if (data && !data->thumb->isNull()) {
		int32 w = data->thumb->width(), h = data->thumb->height();
		QPixmap pix;
		if (width * h == height * w || (w == convertScale(fullWidth()) && h == convertScale(fullHeight()))) {
			pix = data->thumb->pixSingle(width, height, width, height);
		} else if (width * h > height * w) {
			int32 nw = height * w / h;
			pix = data->thumb->pixSingle(nw, height, width, height);
		} else {
			int32 nh = width * h / w;
			pix = data->thumb->pixSingle(width, nh, width, height);
		}
		p.drawPixmap(QPoint(skipx, skipy), pix);
	} else {
		App::roundRect(p, skipx, skipy, width, height, st::black, BlackCorners);
	}
	if (data) {
		switch (data->type) {
		case YouTubeLink: p.drawPixmap(QPoint(skipx + (width - st::youtubeIcon.pxWidth()) / 2, skipy + (height - st::youtubeIcon.pxHeight()) / 2), App::sprite(), st::youtubeIcon); break;
		case VimeoLink: p.drawPixmap(QPoint(skipx + (width - st::vimeoIcon.pxWidth()) / 2, skipy + (height - st::vimeoIcon.pxHeight()) / 2), App::sprite(), st::vimeoIcon); break;
		}
		if (!data->title.isEmpty() || !data->duration.isEmpty()) {
			p.fillRect(skipx, skipy, width, st::msgDateFont->height + 2 * st::msgDateImgPadding.y(), st::msgDateImgBg->b);
			p.setFont(st::msgDateFont->f);
			p.setPen(st::msgDateImgColor->p);
			int32 titleWidth = width - 2 * st::msgDateImgPadding.x();
			if (!data->duration.isEmpty()) {
				int32 durationWidth = st::msgDateFont->width(data->duration);
				p.drawText(skipx + width - st::msgDateImgPadding.x() - durationWidth, skipy + st::msgDateImgPadding.y() + st::msgDateFont->ascent, data->duration);
				titleWidth -= durationWidth + st::msgDateImgPadding.x();
			}
			if (!data->title.isEmpty()) {
				p.drawText(skipx + st::msgDateImgPadding.x(), skipy + st::msgDateImgPadding.y() + st::msgDateFont->ascent, st::msgDateFont->elided(data->title, titleWidth));
			}
		}
	}
	if (selected) {
		App::roundRect(p, skipx, skipy, width, height, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	int32 fullRight = skipx + width + (skipx ? st::mediaPadding.right() : 0), fullBottom = _height;
	parent->drawInfo(p, fullRight, fullBottom, selected, InfoDisplayOverImage);
}

int32 HistoryImageLink::resize(int32 width, const HistoryItem *parent) {
	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = toHistoryForwarded(parent);

	w = qMin(width, _maxw);
	if (reply || !_title.isEmpty() || !_description.isEmpty()) {
		w -= st::mediaPadding.left() + st::mediaPadding.right();
	}

	int32 tw = convertScale(fullWidth()), th = convertScale(fullHeight());
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	_height = th;
	if (tw > w) {
		_height = (w * _height / tw);
	} else {
		w = tw;
	}
	if (_height > width) {
		w = (w * width) / _height;
		_height = width;
	}
	if (w < st::minPhotoSize) {
		w = st::minPhotoSize;
	}
	if (_height < st::minPhotoSize) {
		_height = st::minPhotoSize;
	}
	if (reply || !_title.isEmpty() || !_description.isEmpty()) {
		if (parent->displayFromName()) {
			_height += st::msgPadding.top() + st::msgNameFont->height;
		}
		if (reply) {
			_height += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		} else {
			if (!parent->displayFromName() || !fwd) {
				_height += st::msgPadding.top();
			}
			if (fwd) {
				_height += st::msgServiceNameFont->height + st::msgPadding.top();
			}
		}
		if (!_title.isEmpty()) {
			_height += qMin(_title.countHeight(w), st::webPageTitleFont->height * 2);
		}
		if (!_description.isEmpty()) {
			_height += qMin(_description.countHeight(w), st::webPageDescriptionFont->height * 3);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			_height += st::webPagePhotoSkip;
		}
		_height += st::mediaPadding.bottom();
		w += st::mediaPadding.left() + st::mediaPadding.right();
	}
	return _height;
}

const QString HistoryImageLink::inDialogsText() const {
	if (data) {
		switch (data->type) {
		case YouTubeLink: return qsl("YouTube Video");
		case VimeoLink: return qsl("Vimeo Video");
		case InstagramLink: return qsl("Instagram Link");
		case GoogleMapsLink: return lang(lng_maps_point);
		}
	}
	return QString();
}

const QString HistoryImageLink::inHistoryText() const {
	if (data) {
		switch (data->type) {
		case YouTubeLink: return qsl("[ YouTube Video : ") + link->text() + qsl(" ]");
		case VimeoLink: return qsl("[ Vimeo Video : ") + link->text() + qsl(" ]");
		case InstagramLink: return qsl("[ Instagram Link : ") + link->text() + qsl(" ]");
		case GoogleMapsLink: return qsl("[ ") + lang(lng_maps_point) + qsl(" : ") + link->text() + qsl(" ]");
		}
	}
	return qsl("[ Link : ") + link->text() + qsl(" ]");
}

bool HistoryImageLink::hasPoint(int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;
	return (x >= 0 && y >= 0 && x < width && y < _height);
}

void HistoryImageLink::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent, int32 width) const {
	if (width < 0) width = w;

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	int skipx = 0, skipy = 0, height = _height;
	const HistoryReply *reply = toHistoryReply(parent);
	const HistoryForwarded *fwd = reply ? 0 : toHistoryForwarded(parent);
	int replyFrom = 0, fwdFrom = 0;
	if (reply || !_title.isEmpty() || !_description.isEmpty()) {
		skipx = st::mediaPadding.left();
		if (reply) {
			skipy = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		} else if (fwd) {
			skipy = st::msgServiceNameFont->height + st::msgPadding.top();
		}
		if (parent->displayFromName()) {
			replyFrom = st::msgPadding.top() + st::msgNameFont->height;
			fwdFrom = st::msgPadding.top() + st::msgNameFont->height;
			skipy += replyFrom;
			if (x >= st::mediaPadding.left() && y >= st::msgPadding.top() && x < width - st::mediaPadding.left() - st::mediaPadding.right() && x < st::mediaPadding.left() + parent->from()->nameText.maxWidth() && y < replyFrom) {
				lnk = parent->from()->lnk;
				return;
			}
			if (!fwd && !reply) skipy += st::msgPadding.top();
		} else if (!reply) {
			fwdFrom = st::msgPadding.top();
			skipy += fwdFrom;
		}
		if (reply) {
			if (x >= 0 && y >= replyFrom + st::msgReplyPadding.top() && x < width && y < skipy - st::msgReplyPadding.bottom()) {
				lnk = reply->replyToLink();
				return;
			}
		} else if (fwd) {
			if (y >= fwdFrom && y < fwdFrom + st::msgServiceNameFont->height) {
				return fwd->getForwardedState(lnk, state, x - st::mediaPadding.left(), width - st::mediaPadding.left() - st::mediaPadding.right());
			}
		}
		height -= skipy + st::mediaPadding.bottom();
		width -= st::mediaPadding.left() + st::mediaPadding.right();
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height && data) {
		lnk = link;

		bool inDate = parent->pointInTime(skipx + width + (skipx ? st::mediaPadding.right() : 0), _height, x, y, InfoDisplayOverImage);
		if (inDate) {
			state = HistoryInDateCursorState;
		}

		return;
	}
}

HistoryMedia *HistoryImageLink::clone() const {
	return new HistoryImageLink(*this);
}

HistoryMessage::HistoryMessage(History *history, HistoryBlock *block, const MTPDmessage &msg) :
	HistoryItem(history, block, msg.vid.v, msg.vflags.v, ::date(msg.vdate), msg.has_from_id() ? msg.vfrom_id.v : 0)
, _text(st::msgMinWidth)
, _textWidth(0)
, _textHeight(0)
, _media(0)
, _views(msg.has_views() ? msg.vviews.v : -1)
{
	QString text(textClean(qs(msg.vmessage)));
	initTime();
	initMedia(msg.has_media() ? (&msg.vmedia) : 0, text);
	setText(text, msg.has_entities() ? linksFromMTP(msg.ventities.c_vector().v) : LinksInText());
}

HistoryMessage::HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, QDateTime date, int32 from, const QString &msg, const LinksInText &links, HistoryMedia *fromMedia) :
HistoryItem(history, block, msgId, flags, date, from)
, _text(st::msgMinWidth)
, _textWidth(0)
, _textHeight(0)
, _media(0)
, _views(fromChannel() ? 1 : -1)
{
	initTime();
	if (fromMedia) {
		_media = fromMedia->clone();
		_media->regItem(this);
	}
	setText(msg, links);
}

HistoryMessage::HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, QDateTime date, int32 from, DocumentData *doc) :
HistoryItem(history, block, msgId, flags, date, from)
, _text(st::msgMinWidth)
, _textWidth(0)
, _textHeight(0)
, _media(0)
, _views(fromChannel() ? 1 : -1)
{
	initTime();
	initMediaFromDocument(doc);
	setText(QString(), LinksInText());
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
	_timeText = date.toString(cTimeFormat());
	_timeWidth = st::msgDateFont->width(_timeText);

	_viewsText = (_views >= 0) ? formatViewsCount(_views) : QString();
	_viewsWidth = _viewsText.isEmpty() ? 0 : st::msgDateFont->width(_viewsText);
}

void HistoryMessage::initMedia(const MTPMessageMedia *media, QString &currentText) {
	switch (media ? media->type() : mtpc_messageMediaEmpty) {
	case mtpc_messageMediaContact: {
		const MTPDmessageMediaContact &d(media->c_messageMediaContact());
		_media = new HistoryContact(d.vuser_id.v, qs(d.vfirst_name), qs(d.vlast_name), qs(d.vphone_number));
	} break;
	case mtpc_messageMediaGeo: {
		const MTPGeoPoint &point(media->c_messageMediaGeo().vgeo);
		if (point.type() == mtpc_geoPoint) {
			const MTPDgeoPoint &d(point.c_geoPoint());
			_media = new HistoryImageLink(qsl("location:%1,%2").arg(d.vlat.v).arg(d.vlong.v));
		}
	} break;
	case mtpc_messageMediaVenue: {
		const MTPDmessageMediaVenue &d(media->c_messageMediaVenue());
		if (d.vgeo.type() == mtpc_geoPoint) {
			const MTPDgeoPoint &g(d.vgeo.c_geoPoint());
			_media = new HistoryImageLink(qsl("location:%1,%2").arg(g.vlat.v).arg(g.vlong.v), qs(d.vtitle), qs(d.vaddress));
		}
	} break;
	case mtpc_messageMediaPhoto: {
		const MTPDmessageMediaPhoto &photo(media->c_messageMediaPhoto());
		if (photo.vphoto.type() == mtpc_photo) {
			_media = new HistoryPhoto(photo.vphoto.c_photo(), qs(photo.vcaption), this);
		}
	} break;
	case mtpc_messageMediaVideo: {
		const MTPDmessageMediaVideo &video(media->c_messageMediaVideo());
		if (video.vvideo.type() == mtpc_video) {
			_media = new HistoryVideo(video.vvideo.c_video(), qs(video.vcaption), this);
		}
	} break;
	case mtpc_messageMediaAudio: {
		const MTPAudio &audio(media->c_messageMediaAudio().vaudio);
		if (audio.type() == mtpc_audio) {
			_media = new HistoryAudio(audio.c_audio());
		}
	} break;
	case mtpc_messageMediaDocument: {
		const MTPDocument &document(media->c_messageMediaDocument().vdocument);
		if (document.type() == mtpc_document) {
			DocumentData *doc = App::feedDocument(document);
			return initMediaFromDocument(doc);
		}
	} break;
	case mtpc_messageMediaWebPage: {
		const MTPWebPage &d(media->c_messageMediaWebPage().vwebpage);
		switch (d.type()) {
		case mtpc_webPageEmpty: initMediaFromText(currentText); break;
		case mtpc_webPagePending: {
			WebPageData *webPage = App::feedWebPage(d.c_webPagePending());
			_media = new HistoryWebPage(webPage);
		} break;
		case mtpc_webPage: {
			_media = new HistoryWebPage(App::feedWebPage(d.c_webPage()));
		} break;
		}
	} break;
	default: initMediaFromText(currentText); break;
	};
	if (_media) _media->regItem(this);
}

void HistoryMessage::initMediaFromText(QString &currentText)  {
	QString lnk = currentText.trimmed();
	if (/*reYouTube1.match(currentText).hasMatch() || reYouTube2.match(currentText).hasMatch() || reInstagram.match(currentText).hasMatch() || */reVimeo.match(currentText).hasMatch()) {
		_media = new HistoryImageLink(lnk);
		currentText = QString();
	}
}

void HistoryMessage::initMediaFromDocument(DocumentData *doc) {
	if (doc->sticker()) {
		_media = new HistorySticker(doc);
	} else {
		_media = new HistoryDocument(doc);
	}
	_media->regItem(this);
}

void HistoryMessage::initDimensions() {
	if (justMedia()) {
		_media->initDimensions(this);
		_maxw = _media->maxWidth();
		_minh = _media->minHeight();
	} else {
		_maxw = _text.maxWidth();
		_minh = _text.minHeight();
		_maxw += st::msgPadding.left() + st::msgPadding.right();
		if (_media) {
			_media->initDimensions(this);
			if (_media->isDisplayed() && _text.hasSkipBlock()) {
				_text.removeSkipBlock();
				_textWidth = 0;
				_textHeight = 0;
			} else if (!_media->isDisplayed() && !_text.hasSkipBlock()) {
				_text.setSkipBlock(skipBlockWidth(), skipBlockHeight());
				_textWidth = 0;
				_textHeight = 0;
			}
			if (_media->isDisplayed()) {
				int32 maxw = _media->maxWidth() + st::msgPadding.left() + st::msgPadding.right();
				if (maxw > _maxw) _maxw = maxw;
				_minh += st::msgPadding.bottom() + _media->minHeight();
			}
		}
	}
	fromNameUpdated();
}

void HistoryMessage::fromNameUpdated() const {
	if (_media) return;
	int32 _namew = (displayFromName() ? _from->nameText.maxWidth() : 0) + st::msgPadding.left() + st::msgPadding.right();
	if (_namew > _maxw) _maxw = _namew;
}

bool HistoryMessage::uploading() const {
	return _media ? _media->uploading() : false;
}

QString HistoryMessage::selectedText(uint32 selection) const {
	if (_media && selection == FullItemSel) {
		QString text = _text.original(0, 0xFFFF), mediaText = _media->inHistoryText();
		return text.isEmpty() ? mediaText : (mediaText.isEmpty() ? text : (text + ' ' + mediaText));
	}
	uint16 selectedFrom = (selection == FullItemSel) ? 0 : (selection >> 16) & 0xFFFF;
	uint16 selectedTo = (selection == FullItemSel) ? 0xFFFF : (selection & 0xFFFF);
	return _text.original(selectedFrom, selectedTo);
}

LinksInText HistoryMessage::textLinks() const {
	return _text.calcLinksInText();
}

QString HistoryMessage::inDialogsText() const {
	QString result = _media ? _media->inDialogsText() : QString();
	return result.isEmpty() ? _text.original(0, 0xFFFF, false) : result;
}

HistoryMedia *HistoryMessage::getMedia(bool inOverview) const {
	return _media;
}

void HistoryMessage::setMedia(const MTPMessageMedia *media, bool allowEmitResize) {
	if ((!_media || _media->isImageLink()) && (!media || media->type() == mtpc_messageMediaEmpty)) return;

	bool mediaWasDisplayed = false;
	if (_media) {
		mediaWasDisplayed = _media->isDisplayed();
		delete _media;
		_media = 0;
	}
	QString t;
	initMedia(media, t);
	if (_media && _media->isDisplayed() && !mediaWasDisplayed) {
		_text.removeSkipBlock();
		_textWidth = 0;
		_textHeight = 0;
	} else if (mediaWasDisplayed && (!_media || !_media->isDisplayed())) {
		_text.setSkipBlock(skipBlockWidth(), skipBlockHeight());
		_textWidth = 0;
		_textHeight = 0;
	}
	initDimensions();
	if (allowEmitResize && App::main()) App::main()->itemResized(this);
}

void HistoryMessage::setText(const QString &text, const LinksInText &links) {
	if (!_media || !text.isEmpty()) { // !justMedia()
		if (_media && _media->isDisplayed()) {
			_text.setMarkedText(st::msgFont, text, links, itemTextParseOptions(this));
		} else {
			_text.setMarkedText(st::msgFont, text + skipBlock(), links, itemTextParseOptions(this));
		}
		if (id > 0) {
			for (int32 i = 0, l = links.size(); i != l; ++i) {
				if (links.at(i).type == LinkInTextUrl || links.at(i).type == LinkInTextCustomUrl || links.at(i).type == LinkInTextEmail) {
					_flags |= MTPDmessage_flag_HAS_TEXT_LINKS;
					break;
				}
			}
		}
		_textWidth = 0;
		_textHeight = 0;
	}
}

void HistoryMessage::getTextWithLinks(QString &text, LinksInText &links) {
	if (_text.isEmpty()) return;
	links = _text.calcLinksInText();
	text = _text.original();
}

bool HistoryMessage::textHasLinks() {
	return _text.hasLinks();
}

void HistoryMessage::drawInfo(Painter &p, int32 right, int32 bottom, bool selected, InfoDisplayType type) const {
	p.setFont(st::msgDateFont->f);

	bool outbg = out() && !fromChannel(), overimg = (type == InfoDisplayOverImage);
	int32 infoRight = right, infoBottom = bottom;
	switch (type) {
	case InfoDisplayDefault:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
		p.setPen((selected ? (outbg ? st::msgOutSelectDateColor : st::msgInSelectDateColor) : (outbg ? st::msgOutDateColor : st::msgInDateColor))->p);
	break;
	case InfoDisplayOverImage:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st::msgDateImgColor->p);
	break;
	}

	int32 infoW = HistoryMessage::infoWidth();
	int32 dateX = infoRight - infoW;
	int32 dateY = infoBottom - st::msgDateFont->height;
	if (type == InfoDisplayOverImage) {
		int32 dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		App::roundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? st::msgDateImgSelectBg : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	}
	dateX += HistoryMessage::timeLeft();

	p.drawText(dateX, dateY + st::msgDateFont->ascent, _timeText);

	QPoint iconPos;
	const QRect *iconRect = 0;
	if (!_viewsText.isEmpty()) {
		iconPos = QPoint(infoRight - infoW + st::msgViewsPos.x(), infoBottom - st::msgViewsImg.pxHeight() + st::msgViewsPos.y());
		if (id > 0) {
			if (out() && !fromChannel()) {
				iconRect = &(overimg ? st::msgInvViewsImg : (selected ? st::msgSelectOutViewsImg : st::msgOutViewsImg));
			} else {
				iconRect = &(overimg ? st::msgInvViewsImg : (selected ? st::msgSelectViewsImg : st::msgViewsImg));
			}
			p.drawText(iconPos.x() + st::msgViewsImg.pxWidth() + st::msgDateCheckSpace, infoBottom - st::msgDateFont->descent, _viewsText);
		} else {
			iconPos.setX(iconPos.x() + st::msgDateViewsSpace + _viewsWidth);
			if (out() && !fromChannel()) {
				iconRect = &(overimg ? st::msgInvSendingViewsImg : st::msgSendingOutViewsImg);
			} else {
				iconRect = &(overimg ? st::msgInvSendingViewsImg : st::msgSendingViewsImg);
			}
		}
		p.drawPixmap(iconPos, App::sprite(), *iconRect);
	}
	if (out() && !fromChannel()) {
		iconPos = QPoint(infoRight - st::msgCheckImg.pxWidth() + st::msgCheckPos.x(), infoBottom - st::msgCheckImg.pxHeight() + st::msgCheckPos.y());
		if (id > 0) {
			if (unread()) {
				iconRect = &(overimg ? st::msgInvCheckImg : (selected ? st::msgSelectCheckImg : st::msgCheckImg));
			} else {
				iconRect = &(overimg ? st::msgInvDblCheckImg : (selected ? st::msgSelectDblCheckImg : st::msgDblCheckImg));
			}
		} else {
			iconRect = &(overimg ? st::msgInvSendingImg : st::msgSendingImg);
		}
		p.drawPixmap(iconPos, App::sprite(), *iconRect);
	}
}

void HistoryMessage::setViewsCount(int32 count) {
	if (_views == count || (_views >= 0 && count >= 0 && _views > count)) return;

	int32 was = _viewsWidth;
	_views = count;
	_viewsText = (_views >= 0) ? formatViewsCount(_views) : QString();
	_viewsWidth = _viewsText.isEmpty() ? 0 : st::msgDateFont->width(_viewsText);
	if (was == _viewsWidth) {
		if (App::main()) App::main()->msgUpdated(history()->peer->id, this);
	} else {
		if (_text.hasSkipBlock()) {
			_text.setSkipBlock(HistoryMessage::skipBlockWidth(), HistoryMessage::skipBlockHeight());
			_textWidth = 0;
			_textHeight = 0;
		}
		initDimensions();
		if (App::main()) App::main()->itemResized(this);
	}
}

void HistoryMessage::draw(Painter &p, uint32 selection) const {
	bool outbg = out() && !fromChannel();

	textstyleSet(&(outbg ? st::outTextStyle : st::inTextStyle));

	uint64 ms = App::main() ? App::main()->animActiveTime(id) : 0;
	if (ms) {
		if (ms > st::activeFadeInDuration + st::activeFadeOutDuration) {
			App::main()->stopAnimActive();
		} else {
			float64 dt = (ms > st::activeFadeInDuration) ? (1 - (ms - st::activeFadeInDuration) / float64(st::activeFadeOutDuration)) : (ms / float64(st::activeFadeInDuration));
			float64 o = p.opacity();
			p.setOpacity(o * dt);
			p.fillRect(0, 0, _history->width, _height, textstyleCurrent()->selectOverlay->b);
			p.setOpacity(o);
		}
	}

	bool selected = (selection == FullItemSel);
	if (_from->nameVersion > _fromVersion) {
		fromNameUpdated();
		_fromVersion = _from->nameVersion;
	}
	int32 left = fromChannel() ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out() ? st::msgMargin.right() : st::msgMargin.left()), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
	if (justMedia()) {
		if (_media->maxWidth() > mwidth) mwidth = _media->maxWidth();
		if (_media->currentWidth() < mwidth) mwidth = _media->currentWidth();
	}
	if (width > mwidth) {
		if (fromChannel()) {
//			left += (width - mwidth) / 2;
		} else if (out()) {
			left += width - mwidth;
		}
		width = mwidth;
	}

	if (displayFromPhoto()) {
		p.drawPixmap(left, _height - st::msgMargin.bottom() - st::msgPhotoSize, _from->photo->pixRounded(st::msgPhotoSize));
//		width -= st::msgPhotoSkip;
		left += st::msgPhotoSkip;
	}
	if (width < 1) return;

	if (width >= _maxw) {
		if (fromChannel()) {
//			left += (width - _maxw) / 2;
		} else if (out()) {
			left += width - _maxw;
		}
		width = _maxw;
	}
	if (justMedia()) {
		p.save();
		p.translate(left, st::msgMargin.top());
		_media->draw(p, this, selected);
		p.restore();
	} else {
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());

		style::color bg(selected ? (outbg ? st::msgOutSelectBg : st::msgInSelectBg) : (outbg ? st::msgOutBg : st::msgInBg));
		style::color sh(selected ? (outbg ? st::msgOutSelectShadow : st::msgInSelectShadow) : (outbg ? st::msgOutShadow : st::msgInShadow));
		RoundCorners cors(selected ? (outbg ? MessageOutSelectedCorners : MessageInSelectedCorners) : (outbg ? MessageOutCorners : MessageInCorners));
		App::roundRect(p, r, bg, cors, &sh);

		if (displayFromName()) {
			p.setFont(st::msgNameFont->f);
			if (fromChannel()) {
				p.setPen(selected ? st::msgInServiceSelColor : st::msgInServiceColor);
			} else {
				p.setPen(_from->color);
			}
			_from->nameText.drawElided(p, r.left() + st::msgPadding.left(), r.top() + st::msgPadding.top(), width - st::msgPadding.left() - st::msgPadding.right());
			r.setTop(r.top() + st::msgNameFont->height);
		}
		QRect trect(r.marginsAdded(-st::msgPadding));
		drawMessageText(p, trect, selection);

		if (_media) {
			p.save();
			p.translate(left + st::msgPadding.left(), _height - st::msgMargin.bottom() - st::msgPadding.bottom() - _media->height());
			_media->draw(p, this, selected);
			p.restore();
		}
		HistoryMessage::drawInfo(p, r.x() + r.width(), r.y() + r.height(), selected, InfoDisplayDefault);
	}
}

void HistoryMessage::drawMessageText(Painter &p, const QRect &trect, uint32 selection) const {
	p.setPen(st::msgColor->p);
	p.setFont(st::msgFont->f);
	uint16 selectedFrom = (selection == FullItemSel) ? 0 : (selection >> 16) & 0xFFFF;
	uint16 selectedTo = (selection == FullItemSel) ? 0 : selection & 0xFFFF;
	_text.draw(p, trect.x(), trect.y(), trect.width(), Qt::AlignLeft, 0, -1, selectedFrom, selectedTo);

	textstyleRestore();
}

int32 HistoryMessage::resize(int32 width) {
	if (width < st::msgMinWidth) return _height;

	width -= st::msgMargin.left() + st::msgMargin.right();
	if (justMedia()) {
		_height = _media->resize(width, this);
	} else {
		if (width < st::msgPadding.left() + st::msgPadding.right() + 1) {
			width = st::msgPadding.left() + st::msgPadding.right() + 1;
		} else if (width > st::msgMaxWidth) {
			width = st::msgMaxWidth;
		}
		int32 nwidth = qMax(width - st::msgPadding.left() - st::msgPadding.right(), 0);
		if (nwidth != _textWidth) {
			_textWidth = nwidth;
			_textHeight = _text.countHeight(nwidth);
		}
		if (width >= _maxw) {
			_height = _minh;
			if (_media) _media->resize(_maxw - st::msgPadding.left() - st::msgPadding.right(), this);
		} else {
			_height = _textHeight;
			if (_media && _media->isDisplayed()) _height += st::msgPadding.bottom() + _media->resize(nwidth, this);
		}
		if (displayFromName()) {
			_height += st::msgNameFont->height;
		}
		_height += st::msgPadding.top() + st::msgPadding.bottom();
	}
	_height += st::msgMargin.top() + st::msgMargin.bottom();
	return _height;
}

bool HistoryMessage::hasPoint(int32 x, int32 y) const {
	int32 left = fromChannel() ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out() ? st::msgMargin.right() : st::msgMargin.left()), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
	if (justMedia()) {
		if (_media->maxWidth() > mwidth) mwidth = _media->maxWidth();
		if (_media->currentWidth() < mwidth) mwidth = _media->currentWidth();
	}
	if (width > mwidth) {
		if (fromChannel()) {
//			left += (width - mwidth) / 2;
		} else if (out()) {
			left += width - mwidth;
		}
		width = mwidth;
	}

	if (displayFromPhoto()) { // from user left photo
		left += st::msgPhotoSkip;
	}
	if (width < 1) return false;

	if (width >= _maxw) {
		if (fromChannel()) {
//			left += (width - _maxw) / 2;
		} else if (out()) {
			left += width - _maxw;
		}
		width = _maxw;
	}
	if (justMedia()) {
		return _media->hasPoint(x - left, y - st::msgMargin.top(), this);
	}
	QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
	return r.contains(x, y);
}

bool HistoryMessage::pointInTime(int32 right, int32 bottom, int32 x, int32 y, InfoDisplayType type) const {
	int32 infoRight = right, infoBottom = bottom;
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
	int32 dateX = infoRight - HistoryMessage::infoWidth() + HistoryMessage::timeLeft();
	int32 dateY = infoBottom - st::msgDateFont->height;
	return QRect(dateX, dateY, HistoryMessage::timeWidth(), st::msgDateFont->height).contains(x, y);
}

void HistoryMessage::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const {
	state = HistoryDefaultCursorState;
	lnk = TextLinkPtr();

	int32 left = fromChannel() ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out() ? st::msgMargin.right() : st::msgMargin.left()), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
	if (justMedia()) {
		if (_media->maxWidth() > mwidth) mwidth = _media->maxWidth();
		if (_media->currentWidth() < mwidth) mwidth = _media->currentWidth();
	}
	if (width > mwidth) {
		if (fromChannel()) {
//			left += (width - mwidth) / 2;
		} else if (out()) {
			left += width - mwidth;
		}
		width = mwidth;
	}

	if (displayFromPhoto()) { // from user left photo
		if (x >= left && x < left + st::msgPhotoSize && y >= _height - st::msgMargin.bottom() - st::msgPhotoSize && y < _height - st::msgMargin.bottom()) {
			lnk = _from->lnk;
			return;
		}
//		width -= st::msgPhotoSkip;
		left += st::msgPhotoSkip;
	}
	if (width < 1) return;

	if (width >= _maxw) {
		if (fromChannel()) {
//			left += (width - _maxw) / 2;
		} else if (out()) {
			left += width - _maxw;
		}
		width = _maxw;
	}
	if (justMedia()) {
		_media->getState(lnk, state, x - left, y - st::msgMargin.top(), this);
		return;
	}
	QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
	if (displayFromName()) { // from user left name
		if (x >= r.left() + st::msgPadding.left() && y >= r.top() + st::msgPadding.top() && y < r.top() + st::msgPadding.top() + st::msgNameFont->height && x < r.left() + r.width() - st::msgPadding.right() && x < r.left() + st::msgPadding.left() + _from->nameText.maxWidth()) {
			lnk = _from->lnk;
			return;
		}
		r.setTop(r.top() + st::msgNameFont->height);
	}

	getStateFromMessageText(lnk, state, x, y, r);
}

void HistoryMessage::getStateFromMessageText(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const QRect &r) const {
	bool inDate = HistoryMessage::pointInTime(r.x() + r.width(), r.y() + r.height(), x, y, InfoDisplayDefault);

	QRect trect(r.marginsAdded(-st::msgPadding));
	TextLinkPtr medialnk;
	if (_media && _media->isDisplayed()) {
		if (y >= trect.bottom() - _media->height() && y < trect.bottom()) {
			_media->getState(lnk, state, x - trect.left(), y + _media->height() - trect.bottom(), this);
			if (inDate) state = HistoryInDateCursorState;
			return;
		}
		trect.setBottom(trect.bottom() - _media->height() - st::msgPadding.bottom());
	}
	bool inText = false;
	_text.getState(lnk, inText, x - trect.x(), y - trect.y(), trect.width());

	if (inDate) {
		state = HistoryInDateCursorState;
	} else if (inText) {
		state = HistoryInTextCursorState;
	} else {
		state = HistoryDefaultCursorState;
	}
}

void HistoryMessage::getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const {
	symbol = 0;
	after = false;
	upon = false;
	if (justMedia()) return;

	int32 left = fromChannel() ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out() ? st::msgMargin.right() : st::msgMargin.left()), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
	if (width > mwidth) {
		if (fromChannel()) {
//			left += (width - mwidth) / 2;
		} else if (out()) {
			left += width - mwidth;
		}
		width = mwidth;
	}

	if (displayFromPhoto()) { // from user left photo
//		width -= st::msgPhotoSkip;
		left += st::msgPhotoSkip;
	}
	if (width < 1) return;

	if (width >= _maxw) {
		if (fromChannel()) {
//			left += (width - _maxw) / 2;
		} else if (out()) {
			left += width - _maxw;
		}
		width = _maxw;
	}
	QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
	if (displayFromName()) { // from user left name
		r.setTop(r.top() + st::msgNameFont->height);
	}
	QRect trect(r.marginsAdded(-st::msgPadding));
	if (_media && _media->isDisplayed()) {
		trect.setBottom(trect.bottom() - _media->height() - st::msgPadding.bottom());
	}
	_text.getSymbol(symbol, after, upon, x - trect.x(), y - trect.y(), trect.width());
}

void HistoryMessage::drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const {
	if (cacheFor != this) {
		cacheFor = this;
		QString msg(inDialogsText());
		if ((!_history->peer->isUser() || out()) && !fromChannel()) {
			TextCustomTagsMap custom;
			custom.insert(QChar('c'), qMakePair(textcmdStartLink(1), textcmdStopLink()));
			msg = lng_message_with_from(lt_from, textRichPrepare((_from == App::self()) ? lang(lng_from_you) : _from->shortName()), lt_message, textRichPrepare(msg));
			cache.setRichText(st::dlgHistFont, msg, _textDlgOptions, custom);
		} else {
			cache.setText(st::dlgHistFont, msg, _textDlgOptions);
		}
	}
	if (r.width()) {
		textstyleSet(&(act ? st::dlgActiveTextStyle : st::dlgTextStyle));
		p.setFont(st::dlgHistFont->f);
		p.setPen((act ? st::dlgActiveColor : (justMedia() ? st::dlgSystemColor : st::dlgTextColor))->p);
		cache.drawElided(p, r.left(), r.top(), r.width(), r.height() / st::dlgHistFont->height);
		textstyleRestore();
	}
}

QString HistoryMessage::notificationHeader() const {
    return (!_history->peer->isUser() && !fromChannel()) ? from()->name : QString();
}

QString HistoryMessage::notificationText() const {
	QString msg(inDialogsText());
    if (msg.size() > 0xFF) msg = msg.mid(0, 0xFF) + qsl("..");
    return msg;
}

HistoryMessage::~HistoryMessage() {
	if (_media) {
		_media->unregItem(this);
		delete _media;
	}
	if (_flags & MTPDmessage::flag_reply_markup) {
		App::clearReplyMarkup(channelId(), id);
	}
}

HistoryForwarded::HistoryForwarded(History *history, HistoryBlock *block, const MTPDmessage &msg) : HistoryMessage(history, block, msg)
, fwdDate(::date(msg.vfwd_date))
, fwdFrom(App::peer(peerFromMTP(msg.vfwd_from_id)))
, fwdFromVersion(fwdFrom->nameVersion)
, fromWidth(st::msgServiceFont->width(lang(lng_forwarded_from)) + st::msgServiceFont->spacew)
{
	fwdNameUpdated();
}

HistoryForwarded::HistoryForwarded(History *history, HistoryBlock *block, MsgId id, QDateTime date, int32 from, HistoryMessage *msg) : HistoryMessage(history, block, id, newMessageFlags(history->peer) | (!history->peer->isChannel() && msg->getMedia() && (msg->getMedia()->type() == MediaTypeAudio/* || msg->getMedia()->type() == MediaTypeVideo*/) ? MTPDmessage_flag_media_unread : 0), date, from, msg->justMedia() ? QString() : msg->HistoryMessage::selectedText(FullItemSel), msg->HistoryMessage::textLinks(), msg->getMedia())
, fwdDate(msg->dateForwarded())
, fwdFrom(msg->fromForwarded())
, fwdFromVersion(fwdFrom->nameVersion)
, fromWidth(st::msgServiceFont->width(lang(lng_forwarded_from)) + st::msgServiceFont->spacew)
{
	fwdNameUpdated();
}

QString HistoryForwarded::selectedText(uint32 selection) const {
	if (selection != FullItemSel) return HistoryMessage::selectedText(selection);
	QString result, original = HistoryMessage::selectedText(selection);
	result.reserve(lang(lng_forwarded_from).size() + fwdFrom->name.size() + 4 + original.size());
	result.append('[').append(lang(lng_forwarded_from)).append(' ').append(fwdFrom->name).append(qsl("]\n")).append(original);
	return result;
}

void HistoryForwarded::initDimensions() {
	HistoryMessage::initDimensions();
	fwdNameUpdated();
}

void HistoryForwarded::fwdNameUpdated() const {
	fwdFromName.setText(st::msgServiceNameFont, App::peerName(fwdFrom), _textNameOptions);
	if (justMedia()) return;
	int32 _namew = fromWidth + fwdFromName.maxWidth() + st::msgPadding.left() + st::msgPadding.right();
	if (_namew > _maxw) _maxw = _namew;
}

void HistoryForwarded::draw(Painter &p, uint32 selection) const {
	if (!justMedia() && fwdFrom->nameVersion > fwdFromVersion) {
		fwdNameUpdated();
		fwdFromVersion = fwdFrom->nameVersion;
	}
	HistoryMessage::draw(p, selection);
}

void HistoryForwarded::drawForwardedFrom(Painter &p, int32 x, int32 y, int32 w, bool selected) const {
	style::font serviceFont(st::msgServiceFont), serviceName(st::msgServiceNameFont);

	bool outbg = out() && !fromChannel();
	p.setPen((selected ? (outbg ? st::msgOutServiceSelColor : st::msgInServiceSelColor) : (outbg ? st::msgOutServiceColor : st::msgInServiceColor))->p);
	p.setFont(serviceFont->f);

	if (w >= fromWidth) {
		p.drawText(x, y + serviceFont->ascent, lang(lng_forwarded_from));

		p.setFont(serviceName->f);
		fwdFromName.drawElided(p, x + fromWidth, y, w - fromWidth);
	} else {
		p.drawText(x, y + serviceFont->ascent, serviceFont->elided(lang(lng_forwarded_from), w));
	}
}

void HistoryForwarded::drawMessageText(Painter &p, const QRect &trect, uint32 selection) const {
	drawForwardedFrom(p, trect.x(), trect.y(), trect.width(), (selection == FullItemSel));

	QRect realtrect(trect);
	realtrect.setY(trect.y() + st::msgServiceNameFont->height);
	HistoryMessage::drawMessageText(p, realtrect, selection);
}

int32 HistoryForwarded::resize(int32 width) {
	HistoryMessage::resize(width);

	if (!justMedia()) _height += st::msgServiceNameFont->height;
	return _height;
}

bool HistoryForwarded::hasPoint(int32 x, int32 y) const {
	if (!justMedia()) {
		int32 left = fromChannel() ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out() ? st::msgMargin.right() : st::msgMargin.left()), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
		if (width > mwidth) {
			if (fromChannel()) {
//f				left += (width - mwidth) / 2;
			} else if (out()) {
				left += width - mwidth;
			}
			width = mwidth;
		}

		if (displayFromPhoto()) { // from user left photo
//			width -= st::msgPhotoSkip;
			left += st::msgPhotoSkip;
		}
		if (width < 1) return false;

		if (width >= _maxw) {
			if (fromChannel()) {
//				left += (width - _maxw) / 2;
			} else if (out()) {
				left += width - _maxw;
			}
			width = _maxw;
		}
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		return r.contains(x, y);
	}
	return HistoryMessage::hasPoint(x, y);
}

void HistoryForwarded::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const {
	lnk = TextLinkPtr();
	state = HistoryDefaultCursorState;

	if (!justMedia()) {
		int32 left = fromChannel() ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out() ? st::msgMargin.right() : st::msgMargin.left()), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
		if (width > mwidth) {
			if (fromChannel()) {
//				left += (width - mwidth) / 2;
			} else if (out()) {
				left += width - mwidth;
			}
			width = mwidth;
		}

		if (displayFromPhoto()) { // from user left photo
			if (x >= left && x < left + st::msgPhotoSize) {
				return HistoryMessage::getState(lnk, state, x, y);
			}
//			width -= st::msgPhotoSkip;
			left += st::msgPhotoSkip;
		}
		if (width < 1) return;

		if (width >= _maxw) {
			if (fromChannel()) {
//				left += (width - _maxw) / 2;
			} else if (out()) {
				left += width - _maxw;
			}
			width = _maxw;
		}
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		if (displayFromName()) {
			style::font nameFont(st::msgNameFont);
			if (y >= r.top() + st::msgPadding.top() && y < r.top() + st::msgPadding.top() + nameFont->height) {
				return HistoryMessage::getState(lnk, state, x, y);
			}
			r.setTop(r.top() + nameFont->height);
		}
		QRect trect(r.marginsAdded(-st::msgPadding));

		if (y >= trect.top() && y < trect.top() + st::msgServiceNameFont->height) {
			return getForwardedState(lnk, state, x - trect.left(), trect.right() - trect.left());
		}
		y -= st::msgServiceNameFont->height;
	}
	return HistoryMessage::getState(lnk, state, x, y);
}

void HistoryForwarded::getStateFromMessageText(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const QRect &r) const {
	QRect realr(r);
	realr.setHeight(r.height() - st::msgServiceNameFont->height);
	HistoryMessage::getStateFromMessageText(lnk, state, x, y, realr);
}

void HistoryForwarded::getForwardedState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 w) const {
	state = HistoryDefaultCursorState;
	if (x >= fromWidth && x < w && x < fromWidth + fwdFromName.maxWidth()) {
		lnk = fwdFrom->lnk;
	} else {
		lnk = TextLinkPtr();
	}
}

void HistoryForwarded::getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const {
	symbol = 0;
	after = false;
	upon = false;

	if (!justMedia()) {
		int32 left = fromChannel() ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out() ? st::msgMargin.right() : st::msgMargin.left()), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
		if (width > mwidth) {
			if (fromChannel()) {
//				left += (width - mwidth) / 2;
			} else if (out()) {
				left += width - mwidth;
			}
			width = mwidth;
		}

		if (displayFromPhoto()) { // from user left photo
//			width -= st::msgPhotoSkip;
			left += st::msgPhotoSkip;
		}
		if (width < 1) return;

		if (width >= _maxw) {
			if (fromChannel()) {
//				left += (width - _maxw) / 2;
			} else if (out()) {
				left += width - _maxw;
			}
			width = _maxw;
		}
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		if (displayFromName()) {
			style::font nameFont(st::msgNameFont);
			if (y >= r.top() + st::msgPadding.top() && y < r.top() + st::msgPadding.top() + nameFont->height) {
				return HistoryMessage::getSymbol(symbol, after, upon, x, y);
			}
			r.setTop(r.top() + nameFont->height);
		}
		QRect trect(r.marginsAdded(-st::msgPadding));

		y -= st::msgServiceNameFont->height;
	}
	return HistoryMessage::getSymbol(symbol, after, upon, x, y);
}

HistoryReply::HistoryReply(History *history, HistoryBlock *block, const MTPDmessage &msg) : HistoryMessage(history, block, msg)
, replyToMsgId(msg.vreply_to_msg_id.v)
, replyToMsg(0)
, replyToVersion(0)
, _maxReplyWidth(0)
{
	if (!updateReplyTo() && App::api()) {
		App::api()->requestReplyTo(this, history->peer->asChannel(), replyToMsgId);
	}
}

HistoryReply::HistoryReply(History *history, HistoryBlock *block, MsgId msgId, int32 flags, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc) : HistoryMessage(history, block, msgId, flags, date, from, doc)
, replyToMsgId(replyTo)
, replyToMsg(0)
, replyToVersion(0)
, _maxReplyWidth(0)
{
	if (!updateReplyTo() && App::api()) {
		App::api()->requestReplyTo(this, history->peer->asChannel(), replyToMsgId);
	}
}

QString HistoryReply::selectedText(uint32 selection) const {
	if (selection != FullItemSel || !replyToMsg) return HistoryMessage::selectedText(selection);
	QString result, original = HistoryMessage::selectedText(selection);
	result.reserve(lang(lng_in_reply_to).size() + replyToMsg->from()->name.size() + 4 + original.size());
	result.append('[').append(lang(lng_in_reply_to)).append(' ').append(replyToMsg->from()->name).append(qsl("]\n")).append(original);
	return result;
}

void HistoryReply::initDimensions() {
	if (!replyToMsg) {
		_maxReplyWidth = st::msgReplyBarSkip + st::msgDateFont->width(lang(replyToMsgId ? lng_profile_loading : lng_deleted_message)) + st::msgPadding.left() + st::msgPadding.right();
	}
	HistoryMessage::initDimensions();
	if (replyToMsg) {
		replyToNameUpdated();
	} else if (!justMedia()) {
		int maxw = _maxReplyWidth - st::msgReplyPadding.left() - st::msgReplyPadding.right() + st::msgPadding.left() + st::msgPadding.right();
		if (maxw > _maxw) _maxw = maxw;
	}
}

bool HistoryReply::updateReplyTo(bool force) {
	if (replyToMsg || !replyToMsgId) return true;
	replyToMsg = App::histItemById(channelId(), replyToMsgId);
	if (replyToMsg) {
		App::historyRegReply(this, replyToMsg);
		replyToText.setText(st::msgFont, replyToMsg->inReplyText(), _textDlgOptions);

		replyToNameUpdated();
		
		replyToLnk = TextLinkPtr(new MessageLink(replyToMsg->history()->peer->id, replyToMsg->id));
	} else if (force) {
		replyToMsgId = 0;
	}
	if (force) {
		initDimensions();
		if (App::main()) App::main()->itemResized(this);
	}
	return (replyToMsg || !replyToMsgId);
}

void HistoryReply::replyToNameUpdated() const {
	if (!replyToMsg) return;
	replyToName.setText(st::msgServiceNameFont, App::peerName(replyToMsg->from()), _textNameOptions);
	replyToVersion = replyToMsg->from()->nameVersion;
	bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
	int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;

	_maxReplyWidth = st::msgReplyPadding.left() + st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgReplyPadding.right();
	int32 _textw = st::msgReplyPadding.left() + st::msgReplyBarSkip + previewSkip + qMin(replyToText.maxWidth(), 4 * replyToName.maxWidth()) + st::msgReplyPadding.right();
	if (_textw > _maxReplyWidth) _maxReplyWidth = _textw;
	if (!justMedia()) {
		int maxw = _maxReplyWidth - st::msgReplyPadding.left() - st::msgReplyPadding.right() + st::msgPadding.left() + st::msgPadding.right();
		if (maxw > _maxw) _maxw = maxw;
	}
}

int32 HistoryReply::replyToWidth() const {
	return _maxReplyWidth;
}

TextLinkPtr HistoryReply::replyToLink() const {
	return replyToLnk;
}

MsgId HistoryReply::replyToId() const {
	return replyToMsgId;
}

HistoryItem *HistoryReply::replyToMessage() const {
	return replyToMsg;
}

void HistoryReply::replyToReplaced(HistoryItem *oldItem, HistoryItem *newItem) {
	if (replyToMsg == oldItem) {
		replyToMsg = newItem;
		if (!newItem) {
			replyToMsgId = 0;
			initDimensions();
		}
	}
}

void HistoryReply::draw(Painter &p, uint32 selection) const {
	if (replyToMsg && replyToMsg->from()->nameVersion > replyToVersion) {
		replyToNameUpdated();
	}
	HistoryMessage::draw(p, selection);
}

void HistoryReply::drawReplyTo(Painter &p, int32 x, int32 y, int32 w, bool selected, bool likeService) const {
	style::color bar;
	bool outbg = out() && !fromChannel();
	if (likeService) {
		bar = st::white;
	} else {
		bar = (selected ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor));
	}
	p.fillRect(x + st::msgReplyBarPos.x(), y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height(), bar->b);

	if (w > st::msgReplyBarSkip) {
		if (replyToMsg) {
			bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
			int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;

			if (hasPreview) {
				ImagePtr replyPreview = replyToMsg->getMedia()->replyPreview();
				if (!replyPreview->isNull()) {
					QRect to(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
					p.drawPixmap(to.x(), to.y(), replyPreview->pixSingle(replyPreview->width() / cIntRetinaFactor(), replyPreview->height() / cIntRetinaFactor(), to.width(), to.height()));
					if (selected) {
						App::roundRect(p, to, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
					}
				}
			}
			if (w > st::msgReplyBarSkip + previewSkip) {
				if (likeService) {
					p.setPen(st::white->p);
				} else {
					p.setPen((selected ? (outbg ? st::msgOutServiceSelColor : st::msgInServiceSelColor) : (outbg ? st::msgOutServiceColor : st::msgInServiceColor))->p);
				}
				replyToName.drawElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top(), w - st::msgReplyBarSkip - previewSkip);

				HistoryMessage *replyToAsMsg = replyToMsg->toHistoryMessage();
				if (likeService) {
				} else if ((replyToAsMsg && replyToAsMsg->justMedia()) || replyToMsg->serviceMsg()) {
					style::color date(selected ? (outbg ? st::msgOutSelectDateColor : st::msgInSelectDateColor) : (outbg ? st::msgOutDateColor : st::msgInDateColor));
					p.setPen(date->p);
				} else {
					p.setPen(st::msgColor->p);
				}
				replyToText.drawElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top() + st::msgServiceNameFont->height, w - st::msgReplyBarSkip - previewSkip);
			}
		} else {
			p.setFont(st::msgDateFont->f);
			style::color date(selected ? (outbg ? st::msgOutSelectDateColor : st::msgInSelectDateColor) : (outbg ? st::msgOutDateColor : st::msgInDateColor));
			if (likeService) {
				p.setPen(st::white->p);
			} else {
				p.setPen(date->p);
			}
			p.drawText(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->elided(lang(replyToMsgId ? lng_profile_loading : lng_deleted_message), w - st::msgReplyBarSkip));
		}
	}
}

void HistoryReply::drawMessageText(Painter &p, const QRect &trect, uint32 selection) const {
	int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();

	drawReplyTo(p, trect.x(), trect.y(), trect.width(), (selection == FullItemSel));

	QRect realtrect(trect);
	realtrect.setY(trect.y() + h);
	HistoryMessage::drawMessageText(p, realtrect, selection);
}

int32 HistoryReply::resize(int32 width) {
	HistoryMessage::resize(width);

	if (!justMedia()) _height += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	return _height;
}

bool HistoryReply::hasPoint(int32 x, int32 y) const {
	if (!justMedia()) {
		int32 left = fromChannel() ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out() ? st::msgMargin.right() : st::msgMargin.left()), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
		if (width > mwidth) {
			if (fromChannel()) {
//				left += (width - mwidth) / 2;
			} else if (out()) {
				left += width - mwidth;
			}
			width = mwidth;
		}

		if (displayFromPhoto()) { // from user left photo
//			width -= st::msgPhotoSkip;
			left += st::msgPhotoSkip;
		}
		if (width < 1) return false;

		if (width >= _maxw) {
			if (fromChannel()) {
//				left += (width - _maxw) / 2;
			} else if (out()) {
				left += width - _maxw;
			}
			width = _maxw;
		}
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		return r.contains(x, y);
	}
	return HistoryMessage::hasPoint(x, y);
}

void HistoryReply::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const {
	lnk = TextLinkPtr();
	state = HistoryDefaultCursorState;

	if (!justMedia()) {
		int32 left = fromChannel() ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out() ? st::msgMargin.right() : st::msgMargin.left()), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
		if (width > mwidth) {
			if (fromChannel()) {
//				left += (width - mwidth) / 2;
			} else if (out()) {
				left += width - mwidth;
			}
			width = mwidth;
		}

		if (displayFromPhoto()) { // from user left photo
			if (x >= left && x < left + st::msgPhotoSize) {
				return HistoryMessage::getState(lnk, state, x, y);
			}
//			width -= st::msgPhotoSkip;
			left += st::msgPhotoSkip;
		}
		if (width < 1) return;

		if (width >= _maxw) {
			if (fromChannel()) {
//				left += (width - _maxw) / 2;
			} else if (out()) {
				left += width - _maxw;
			}
			width = _maxw;
		}
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		if (displayFromName()) {
			style::font nameFont(st::msgNameFont);
			if (y >= r.top() + st::msgPadding.top() && y < r.top() + st::msgPadding.top() + nameFont->height) {
				return HistoryMessage::getState(lnk, state, x, y);
			}
			r.setTop(r.top() + nameFont->height);
		}
		QRect trect(r.marginsAdded(-st::msgPadding));

		int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		if (y >= trect.top() && y < trect.top() + h) {
			if (replyToMsg && y >= trect.top() + st::msgReplyPadding.top() && y < trect.top() + st::msgReplyPadding.top() + st::msgReplyBarSize.height() && x >= trect.left() && x < trect.right()) {
				lnk = replyToLnk;
			}
			return;
		}
		y -= h;
	}
	return HistoryMessage::getState(lnk, state, x, y);
}

void HistoryReply::getStateFromMessageText(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const QRect &r) const {
	int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();

	QRect realr(r);
	realr.setHeight(r.height() - h);
	HistoryMessage::getStateFromMessageText(lnk, state, x, y, realr);
}

void HistoryReply::getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const {
	symbol = 0;
	after = false;
	upon = false;

	if (!justMedia()) {
		int32 left = fromChannel() ? (st::msgMargin.left() + st::msgMargin.left()) / 2 : (out() ? st::msgMargin.right() : st::msgMargin.left()), width = _history->width - st::msgMargin.left() - st::msgMargin.right(), mwidth = st::msgMaxWidth;
		if (width > mwidth) {
			if (fromChannel()) {
//				left += (width - mwidth) / 2;
			} else if (out()) {
				left += width - mwidth;
			}
			width = mwidth;
		}

		if (displayFromPhoto()) { // from user left photo
//			width -= st::msgPhotoSkip;
			left += st::msgPhotoSkip;
		}
		if (width < 1) return;

		if (width >= _maxw) {
			if (fromChannel()) {
//				left += (width - _maxw) / 2;
			} else if (out()) {
				left += width - _maxw;
			}
			width = _maxw;
		}
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		if (displayFromName()) {
			style::font nameFont(st::msgNameFont);
			if (y >= r.top() + st::msgPadding.top() && y < r.top() + st::msgPadding.top() + nameFont->height) {
				return HistoryMessage::getSymbol(symbol, after, upon, x, y);
			}
			r.setTop(r.top() + nameFont->height);
		}
		QRect trect(r.marginsAdded(-st::msgPadding));

		int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		y -= h;
	}
	return HistoryMessage::getSymbol(symbol, after, upon, x, y);
}

HistoryReply::~HistoryReply() {
	if (replyToMsg) {
		App::historyUnregReply(this, replyToMsg);
	} else if (replyToMsgId && App::api()) {
		App::api()->itemRemoved(this);
	}
}

void HistoryServiceMsg::setMessageByAction(const MTPmessageAction &action) {
	TextLinkPtr second;
	LangString text = lang(lng_message_empty);
	QString from = textcmdLink(1, _from->name);

	switch (action.type()) {
	case mtpc_messageActionChatAddUser: {
		const MTPDmessageActionChatAddUser &d(action.c_messageActionChatAddUser());
		if (peerFromUser(d.vuser_id) == _from->id) {
			text = lng_action_user_joined(lt_from, from);
		} else {
			UserData *u = App::user(peerFromUser(d.vuser_id));
			second = TextLinkPtr(new PeerLink(u));
			text = lng_action_add_user(lt_from, from, lt_user, textcmdLink(2, u->name));
			if (d.vuser_id.v == MTP::authedId() && unread()) {
				if (history()->peer->isChat() && !history()->peer->asChat()->inviterForSpamReport && _from->isUser()) {
					history()->peer->asChat()->inviterForSpamReport = peerToUser(_from->id);
				}
			}
		}
	} break;

	case mtpc_messageActionChatJoinedByLink: {
		const MTPDmessageActionChatJoinedByLink &d(action.c_messageActionChatJoinedByLink());
		if (true || peerFromUser(d.vinviter_id) == _from->id) {
			text = lng_action_user_joined_by_link(lt_from, from);
		//} else {
			//UserData *u = App::user(App::peerFromUser(d.vinviter_id));
			//second = TextLinkPtr(new PeerLink(u));
			//text = lng_action_user_joined_by_link_from(lt_from, from, lt_inviter, textcmdLink(2, u->name));
		}
	} break;

	case mtpc_messageActionChatCreate: {
		const MTPDmessageActionChatCreate &d(action.c_messageActionChatCreate());
		text = lng_action_created_chat(lt_from, from, lt_title, textClean(qs(d.vtitle)));
		if (unread()) {
			if (history()->peer->isChat() && !history()->peer->asChat()->inviterForSpamReport && _from->isUser() && peerToUser(_from->id) != MTP::authedId()) {
				history()->peer->asChat()->inviterForSpamReport = peerToUser(_from->id);
			}
		}
	} break;

	case mtpc_messageActionChannelCreate: {
		const MTPDmessageActionChannelCreate &d(action.c_messageActionChannelCreate());
		text = lng_action_created_channel(lt_title, textClean(qs(d.vtitle)));
	} break;

	case mtpc_messageActionChatDeletePhoto: {
		text = fromChannel() ? lang(lng_action_removed_photo_channel) : lng_action_removed_photo(lt_from, from);
	} break;

	case mtpc_messageActionChatDeleteUser: {
		const MTPDmessageActionChatDeleteUser &d(action.c_messageActionChatDeleteUser());
		if (peerFromUser(d.vuser_id) == _from->id) {
			text = lng_action_user_left(lt_from, from);
		} else {
			UserData *u = App::user(peerFromUser(d.vuser_id));
			second = TextLinkPtr(new PeerLink(u));
			text = lng_action_kick_user(lt_from, from, lt_user, textcmdLink(2, u->name));
		}
	} break;

	case mtpc_messageActionChatEditPhoto: {
		const MTPDmessageActionChatEditPhoto &d(action.c_messageActionChatEditPhoto());
		if (d.vphoto.type() == mtpc_photo) {
			_media = new HistoryPhoto(history()->peer, d.vphoto.c_photo(), st::msgServicePhotoWidth);
		}
		text = fromChannel() ? lang(lng_action_changed_photo_channel) : lng_action_changed_photo(lt_from, from);
	} break;

	case mtpc_messageActionChatEditTitle: {
		const MTPDmessageActionChatEditTitle &d(action.c_messageActionChatEditTitle());
		text = fromChannel() ? lng_action_changed_title_channel(lt_title, textClean(qs(d.vtitle))) : lng_action_changed_title(lt_from, from, lt_title, textClean(qs(d.vtitle)));
	} break;

	default: from = QString(); break;
	}

	_text.setText(st::msgServiceFont, text, _historySrvOptions);
	if (!from.isEmpty()) {
		_text.setLink(1, TextLinkPtr(new PeerLink(_from)));
	}
	if (second) {
		_text.setLink(2, second);
	}
	return ;
}

HistoryServiceMsg::HistoryServiceMsg(History *history, HistoryBlock *block, const MTPDmessageService &msg) :
	HistoryItem(history, block, msg.vid.v, msg.vflags.v, ::date(msg.vdate), msg.has_from_id() ? msg.vfrom_id.v : 0)
, _text(st::msgMinWidth)
, _media(0)
{
	setMessageByAction(msg.vaction);
}

HistoryServiceMsg::HistoryServiceMsg(History *history, HistoryBlock *block, MsgId msgId, QDateTime date, const QString &msg, int32 flags, HistoryMedia *media, int32 from) :
	HistoryItem(history, block, msgId, flags, date, from)
, _text(st::msgServiceFont, msg, _historySrvOptions, st::dlgMinWidth)
, _media(media)
{
}

void HistoryServiceMsg::initDimensions() {
	_maxw = _text.maxWidth() + st::msgServicePadding.left() + st::msgServicePadding.right();
	_minh = _text.minHeight();
	if (_media) _media->initDimensions(this);
}

QString HistoryServiceMsg::selectedText(uint32 selection) const {
	uint16 selectedFrom = (selection == FullItemSel) ? 0 : (selection >> 16) & 0xFFFF;
	uint16 selectedTo = (selection == FullItemSel) ? 0xFFFF : (selection & 0xFFFF);
	return _text.original(selectedFrom, selectedTo);
}

QString HistoryServiceMsg::inDialogsText() const {
	return _text.original(0, 0xFFFF, false);
}

QString HistoryServiceMsg::inReplyText() const {
	QString result = HistoryServiceMsg::inDialogsText();
	return result.trimmed().startsWith(from()->name) ? result.trimmed().mid(from()->name.size()).trimmed() : result;
}

void HistoryServiceMsg::setServiceText(const QString &text) {
	_text.setText(st::msgServiceFont, text, _historySrvOptions);
	initDimensions();
}

void HistoryServiceMsg::draw(Painter &p, uint32 selection) const {
	uint64 ms = App::main() ? App::main()->animActiveTime(id) : 0;
	if (ms) {
		if (ms > st::activeFadeInDuration + st::activeFadeOutDuration) {
			App::main()->stopAnimActive();
		} else {
			textstyleSet(&st::inTextStyle);
			float64 dt = (ms > st::activeFadeInDuration) ? (1 - (ms - st::activeFadeInDuration) / float64(st::activeFadeOutDuration)) : (ms / float64(st::activeFadeInDuration));
			float64 o = p.opacity();
			p.setOpacity(o * dt);
			p.fillRect(0, 0, _history->width, _height, textstyleCurrent()->selectOverlay->b);
			p.setOpacity(o);
		}
	}

	textstyleSet(&st::serviceTextStyle);

	int32 left = st::msgServiceMargin.left(), width = _history->width - st::msgServiceMargin.left() - st::msgServiceMargin.left(), height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	if (width < 1) return;

	if (_media) {
		height -= st::msgServiceMargin.top() + _media->height();
		p.save();
		p.translate(st::msgServiceMargin.left() + (width - _media->maxWidth()) / 2, st::msgServiceMargin.top() + height + st::msgServiceMargin.top());
		_media->draw(p, this, selection == FullItemSel);
		p.restore();
	}

	QRect trect(QRect(left, st::msgServiceMargin.top(), width, height).marginsAdded(-st::msgServicePadding));

	if (width > _maxw) {
		left += (width - _maxw) / 2;
		width = _maxw;
	}
	App::roundRect(p, left, st::msgServiceMargin.top(), width, height, App::msgServiceBg(), (selection == FullItemSel) ? ServiceSelectedCorners : ServiceCorners);

	p.setBrush(Qt::NoBrush);
	p.setPen(st::msgServiceColor->p);
	p.setFont(st::msgServiceFont->f);
	uint16 selectedFrom = (selection == FullItemSel) ? 0 : (selection >> 16) & 0xFFFF;
	uint16 selectedTo = (selection == FullItemSel) ? 0 : selection & 0xFFFF;
	_text.draw(p, trect.x(), trect.y(), trect.width(), Qt::AlignCenter, 0, -1, selectedFrom, selectedTo);
	textstyleRestore();
}

int32 HistoryServiceMsg::resize(int32 width) {
	width -= st::msgServiceMargin.left() + st::msgServiceMargin.left(); // two small margins
	if (width < st::msgServicePadding.left() + st::msgServicePadding.right() + 1) width = st::msgServicePadding.left() + st::msgServicePadding.right() + 1;

	int32 nwidth = qMax(width - st::msgPadding.left() - st::msgPadding.right(), 0);
	if (nwidth != _textWidth) {
		_textWidth = nwidth;
		_textHeight = _text.countHeight(nwidth);
	}
	if (width >= _maxw) {
		_height = _minh;
	} else {
		_height = _textHeight;
	}
	_height += st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom();
	if (_media) {
		_height += st::msgServiceMargin.top() + _media->resize(_media->currentWidth(), this);
	}
	return _height;
}

bool HistoryServiceMsg::hasPoint(int32 x, int32 y) const {
	int32 left = st::msgServiceMargin.left(), width = _history->width - st::msgServiceMargin.left() - st::msgServiceMargin.left(), height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	if (width < 1) return false;

	if (_media) {
		height -= st::msgServiceMargin.top() + _media->height();
	}
	return QRect(left, st::msgServiceMargin.top(), width, height).contains(x, y);
}

void HistoryServiceMsg::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const {
	lnk = TextLinkPtr();
	state = HistoryDefaultCursorState;

	int32 left = st::msgServiceMargin.left(), width = _history->width - st::msgServiceMargin.left() - st::msgServiceMargin.left(), height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	if (width < 1) return;

	if (_media) {
		height -= st::msgServiceMargin.top() + _media->height();
	}
	QRect trect(QRect(left, st::msgServiceMargin.top(), width, height).marginsAdded(-st::msgServicePadding));
	if (trect.contains(x, y)) {
		bool inText = false;
		_text.getState(lnk, inText, x - trect.x(), y - trect.y(), trect.width(), Qt::AlignCenter);
		state = inText ? HistoryInTextCursorState : HistoryDefaultCursorState;
	} else if (_media) {
		_media->getState(lnk, state, x - st::msgServiceMargin.left() - (width - _media->maxWidth()) / 2, y - st::msgServiceMargin.top() - height - st::msgServiceMargin.top(), this);
	}
}

void HistoryServiceMsg::getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const {
	symbol = 0;
	after = false;
	upon = false;

	int32 left = st::msgServiceMargin.left(), width = _history->width - st::msgServiceMargin.left() - st::msgServiceMargin.left(), height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	if (width < 1) return;

	if (_media) {
		height -= st::msgServiceMargin.top() + _media->height();
	}
	QRect trect(QRect(left, st::msgServiceMargin.top(), width, height).marginsAdded(-st::msgServicePadding));
	return _text.getSymbol(symbol, after, upon, x - trect.x(), y - trect.y(), trect.width(), Qt::AlignCenter);
}

void HistoryServiceMsg::drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const {
	if (cacheFor != this) {
		cacheFor = this;
		cache.setText(st::dlgHistFont, inDialogsText(), _textDlgOptions);
	}
	QRect tr(r);
	p.setPen((act ? st::dlgActiveColor : st::dlgSystemColor)->p);
	cache.drawElided(p, tr.left(), tr.top(), tr.width(), tr.height() / st::dlgHistFont->height);
}

QString HistoryServiceMsg::notificationText() const {
    QString msg = _text.original(0, 0xFFFF);
    if (msg.size() > 0xFF) msg = msg.mid(0, 0xFF) + qsl("..");
    return msg;
}

HistoryMedia *HistoryServiceMsg::getMedia(bool inOverview) const {
	return inOverview ? 0 : _media;
}

HistoryServiceMsg::~HistoryServiceMsg() {
	delete _media;
}

HistoryDateMsg::HistoryDateMsg(History *history, HistoryBlock *block, const QDate &date) :
HistoryServiceMsg(history, block, clientMsgId(), QDateTime(date), langDayOfMonth(date)) {
}

HistoryItem *createDayServiceMsg(History *history, HistoryBlock *block, QDateTime date) {
	return regItem(new HistoryDateMsg(history, block, date.date()));
}

HistoryGroup::HistoryGroup(History *history, HistoryBlock *block, const MTPDmessageGroup &group, const QDateTime &date) :
HistoryServiceMsg(history, block, clientMsgId(), date, lng_channel_comments_count(lt_count, group.vcount.v)/* + qsl(" (%1 .. %2)").arg(group.vmin_id.v).arg(group.vmax_id.v)*/),
_minId(group.vmin_id.v), _maxId(group.vmax_id.v), _count(group.vcount.v), _lnk(new CommentsLink(this)) {
}

HistoryGroup::HistoryGroup(History *history, HistoryBlock *block, HistoryItem *newItem, const QDateTime &date) :
HistoryServiceMsg(history, block, clientMsgId(), date, lng_channel_comments_count(lt_count, 1)/* + qsl(" (%1 .. %2)").arg(newItem->id - 1).arg(newItem->id + 1)*/),
_minId(newItem->id - 1), _maxId(newItem->id + 1), _count(1), _lnk(new CommentsLink(this)) {
}

void HistoryGroup::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const {
	lnk = TextLinkPtr();
	state = HistoryDefaultCursorState;

	int32 left = st::msgServiceMargin.left(), width = _history->width - st::msgServiceMargin.left() - st::msgServiceMargin.left(), height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	if (width < 1) return;

	QRect trect(QRect(left, st::msgServiceMargin.top(), width, height).marginsAdded(-st::msgServicePadding));
	if (width > _maxw) {
		left += (width - _maxw) / 2;
		width = _maxw;
	}
	if (QRect(left, st::msgServiceMargin.top(), width, height).contains(x, y)) {
		lnk = _lnk;
	}
}

void HistoryGroup::uniteWith(MsgId minId, MsgId maxId, int32 count) {
	if (minId < 0 || maxId < 0) return;

	if (minId == _minId && maxId == _maxId && count == _count) return;

	if (minId < _minId) {
		if (maxId <= _minId + 1) {
			_count += count;
		} else if (maxId <= _maxId) { // :( smth not precise
			_count += qMax(0, count - (maxId - _minId - 1));
		} else { // :( smth not precise
			_count = qMax(count, _count);
			_maxId = maxId;
		}
		_minId = minId;
	} else if (maxId > _maxId) {
		if (minId + 1 >= _maxId) {
			_count += count;
		} else if (minId >= _minId) { // :( smth not precise
			_count += qMax(0, count - (_maxId - minId - 1));
		} else { // :( smth not precise
			_count = qMax(count, _count);
			_minId = minId;
		}
		_maxId = maxId;
	} else if (count > _count) { // :( smth not precise
		_count = count;
	}
	updateText();
}

bool HistoryGroup::decrementCount() {
	if (_count > 1) {
		--_count;
		updateText();
		return true;
	}
	return false;
}

void HistoryGroup::updateText() {
	setServiceText(lng_channel_comments_count(lt_count, _count)/* + qsl(" (%1 .. %2)").arg(_minId).arg(_maxId)*/);
}

HistoryCollapse::HistoryCollapse(History *history, HistoryBlock *block, MsgId wasMinId, const QDateTime &date) :
HistoryServiceMsg(history, block, clientMsgId(), date, qsl("-")),
_wasMinId(wasMinId) {
}

void HistoryCollapse::draw(Painter &p, uint32 selection) const {
}

void HistoryCollapse::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const {
	lnk = TextLinkPtr();
	state = HistoryDefaultCursorState;
}

HistoryJoined::HistoryJoined(History *history, HistoryBlock *block, const QDateTime &inviteDate, UserData *inviter, int32 flags) :
HistoryServiceMsg(history, block, clientMsgId(), inviteDate, QString(), flags) {
	if (peerToUser(inviter->id) == MTP::authedId()) {
		_text.setText(st::msgServiceFont, lang(lng_action_you_joined), _historySrvOptions);
	} else {
		_text.setText(st::msgServiceFont, lng_action_add_you(lt_from, textcmdLink(1, inviter->name)), _historySrvOptions);
		_text.setLink(1, TextLinkPtr(new PeerLink(inviter)));
	}
}

HistoryUnreadBar::HistoryUnreadBar(History *history, HistoryBlock *block, int32 count, const QDateTime &date) : HistoryItem(history, block, clientMsgId(), 0, date, 0), freezed(false) {
	setCount(count);
	initDimensions();
}

void HistoryUnreadBar::initDimensions() {
	_maxw = st::msgPadding.left() + st::msgPadding.right() + 1;
	_minh = st::unreadBarHeight;
}

void HistoryUnreadBar::setCount(int32 count) {
	if (!count) freezed = true;
	if (freezed) return;
	text = lng_unread_bar(lt_count, count);
}

void HistoryUnreadBar::draw(Painter &p, uint32 selection) const {
	p.fillRect(0, st::lineWidth, _history->width, st::unreadBarHeight - 2 * st::lineWidth, st::unreadBarBG->b);
	p.fillRect(0, st::unreadBarHeight - st::lineWidth, _history->width, st::lineWidth, st::unreadBarBorder->b);
	p.setFont(st::unreadBarFont->f);
	p.setPen(st::unreadBarColor->p);
	p.drawText(QRect(0, 0, _history->width, st::unreadBarHeight - st::lineWidth), text, style::al_center);
}

int32 HistoryUnreadBar::resize(int32 width) {
	_height = st::unreadBarHeight;
	return _height;
}

void HistoryUnreadBar::drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const {
}

QString HistoryUnreadBar::notificationText() const {
    return QString();
}

