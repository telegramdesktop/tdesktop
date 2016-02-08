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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "mainwidget.h"
#include "application.h"
#include "fileuploader.h"
#include "window.h"
#include "gui/filedialog.h"

#include "boxes/addcontactbox.h"
#include "boxes/confirmbox.h"

#include "audio.h"
#include "localstorage.h"

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
	inline const TextParseOptions &itemTextOptions(HistoryItem *item) {
		return itemTextOptions(item->history(), item->from());
	}
	inline const TextParseOptions &itemTextNoMonoOptions(const HistoryItem *item) {
		return itemTextNoMonoOptions(item->history(), item->from());
	}
}

void historyInit() {
	_initTextOptions();
}

void DialogRow::paint(Painter &p, int32 w, bool act, bool sel, bool onlyBackground) const {
	QRect fullRect(0, 0, w, st::dlgHeight);
	p.fillRect(fullRect, (act ? st::dlgActiveBG : (sel ? st::dlgHoverBG : st::dlgBG))->b);
	if (onlyBackground) return;

	if (history->peer->migrateTo()) {
		p.drawPixmap(st::dlgPaddingHor, st::dlgPaddingVer, history->peer->migrateTo()->photo->pix(st::dlgPhotoSize));
	} else {
		p.drawPixmap(st::dlgPaddingHor, st::dlgPaddingVer, history->peer->photo->pix(st::dlgPhotoSize));
	}

	int32 nameleft = st::dlgPaddingHor + st::dlgPhotoSize + st::dlgPhotoPadding;
	int32 namewidth = w - nameleft - st::dlgPaddingHor;
	QRect rectForName(nameleft, st::dlgPaddingVer + st::dlgNameTop, namewidth, st::msgNameFont->height);

	// draw chat icon
	if (history->peer->isChat() || history->peer->isMegagroup()) {
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
		if (history->peer->migrateFrom()) {
			if (History *h = App::historyLoaded(history->peer->migrateFrom()->id)) {
				unread += h->unreadCount;
			}
		}
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

	if (history->peer->isUser() && history->peer->isVerified()) {
		rectForName.setWidth(rectForName.width() - st::verifiedCheck.pxWidth() - st::verifiedCheckPos.x());
		p.drawSprite(rectForName.topLeft() + QPoint(qMin(history->peer->dialogName().maxWidth(), rectForName.width()), 0) + st::verifiedCheckPos, (act ? st::verifiedCheckInv : st::verifiedCheck));
	}

	p.setPen((act ? st::dlgActiveColor : st::dlgNameColor)->p);
	history->peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

void FakeDialogRow::paint(Painter &p, int32 w, bool act, bool sel, bool onlyBackground) const {
	QRect fullRect(0, 0, w, st::dlgHeight);
	p.fillRect(fullRect, (act ? st::dlgActiveBG : (sel ? st::dlgHoverBG : st::dlgBG))->b);
	if (onlyBackground) return;

	History *history = _item->history();
	if (history->peer->migrateTo()) {
		p.drawPixmap(st::dlgPaddingHor, st::dlgPaddingVer, history->peer->migrateTo()->photo->pix(st::dlgPhotoSize));
	} else {
		p.drawPixmap(st::dlgPaddingHor, st::dlgPaddingVer, history->peer->photo->pix(st::dlgPhotoSize));
	}

	int32 nameleft = st::dlgPaddingHor + st::dlgPhotoSize + st::dlgPhotoPadding;
	int32 namewidth = w - nameleft - st::dlgPaddingHor;
	QRect rectForName(nameleft, st::dlgPaddingVer + st::dlgNameTop, namewidth, st::msgNameFont->height);

	// draw chat icon
	if (history->peer->isChat() || history->peer->isMegagroup()) {
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
	int32 lastWidth = namewidth;
	_item->drawInDialog(p, QRect(nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, lastWidth, st::dlgFont->height), act, _cacheFor, _cache);

	if (history->peer->isUser() && history->peer->isVerified()) {
		rectForName.setWidth(rectForName.width() - st::verifiedCheck.pxWidth() - st::verifiedCheckPos.x());
		p.drawSprite(rectForName.topLeft() + QPoint(qMin(history->peer->dialogName().maxWidth(), rectForName.width()), 0) + st::verifiedCheckPos, (act ? st::verifiedCheckInv : st::verifiedCheck));
	}

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
, lastKeyboardHiddenId(0)
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
		overviewCountData[i] = -1; // not loaded yet
	}
}

void History::clearLastKeyboard() {
	if (lastKeyboardId) {
		if (lastKeyboardId == lastKeyboardHiddenId) {
			lastKeyboardHiddenId = 0;
		}
		lastKeyboardId = 0;
	}
	lastKeyboardInited = true;
	lastKeyboardFrom = 0;
}

bool History::updateTyping(uint64 ms, bool force) {
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
		if (typingText.lastDots(typingDots % 4)) {
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

ChannelHistory::ChannelHistory(const PeerId &peer) : History(peer),
unreadCountAll(0),
_onlyImportant(!isMegagroup()),
_otherOldLoaded(false), _otherNewLoaded(true),
_collapseMessage(0), _joinedMessage(0) {
}

bool ChannelHistory::isSwitchReadyFor(MsgId switchId, MsgId &fixInScrollMsgId, int32 &fixInScrollMsgTop) {
	if (switchId == SwitchAtTopMsgId) {
		if (_onlyImportant) {
			if (isMegagroup()) switchMode();
			return true;
		}

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
			if (isMegagroup()) return true;
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
	if (_onlyImportant || isMegagroup()) return;

	bool insertAfter = false;
	for (int32 blockIndex = 1, blocksCount = blocks.size(); blockIndex < blocksCount; ++blockIndex) { // skip first date block
		HistoryBlock *block = blocks.at(blockIndex);
		for (int32 itemIndex = 0, itemsCount = block->items.size(); itemIndex < itemsCount; ++itemIndex) {
			HistoryItem *item = block->items.at(itemIndex);
			if (insertAfter || item->id > wasMinId || (item->id == wasMinId && !item->isImportant())) {
				_collapseMessage = new HistoryCollapse(this, block, wasMinId, item->date);
				if (!addNewInTheMiddle(regItem(_collapseMessage), blockIndex, itemIndex)) {
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
				blocks.push_front(dateBlock); // date block CHECK
				height += dh;
			}
		}
	} else {
		setNotLoadedAtBottom();
	}
}

HistoryJoined *ChannelHistory::insertJoinedMessage(bool unread) {
	if (_joinedMessage || !peer->asChannel()->amIn() || (peer->isMegagroup() && peer->asChannel()->mgInfo->joinedMessageFound)) {
		return _joinedMessage;
	}

	UserData *inviter = (peer->asChannel()->inviter > 0) ? App::userLoaded(peer->asChannel()->inviter) : 0;
	if (!inviter) return 0;

	if (peerToUser(inviter->id) == MTP::authedId()) unread = false;
	int32 flags = (unread ? MTPDmessage::flag_unread : 0);
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
					if (peer->isMegagroup() && peer->migrateFrom() && item->isGroupMigrate()) {
						peer->asChannel()->mgInfo->joinedMessageFound = true;
						return 0;
					}

					++itemIndex;
					if (item->date.date() != inviteDate.date()) {
						HistoryDateMsg *joinedDateItem = new HistoryDateMsg(this, block, inviteDate.date());
						if (addNewInTheMiddle(regItem(joinedDateItem), blockIndex, itemIndex)) {
							++itemIndex;
						}
					}
					_joinedMessage = new HistoryJoined(this, block, inviteDate, inviter, flags);
					if (!addNewInTheMiddle(regItem(_joinedMessage), blockIndex, itemIndex)) {
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
	addItemAfterPrevToBlock(regItem(_joinedMessage), 0, block);
	if (till && _joinedMessage && inviteDate.date() != till->date.date()) {
		HistoryItem *dayItem = createDayServiceMsg(this, block, till->date);
		block->items.push_back(dayItem);
		if (width) {
			dayItem->y = block->height;
			block->height += dayItem->resize(width);
		}
	}
	if (!block->items.isEmpty()) {
		blocks.push_front(block); // CHECK
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
		blocks.push_front(dateBlock); // date block CHECK
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
	if (_joinedMessage || peer->asChannel()->inviter <= 0) {
		return;
	}
	if (isEmpty()) {
		if (loadedAtTop() && loadedAtBottom()) {
			if (insertJoinedMessage(createUnread)) {
				if (!_joinedMessage->detached()) {
					setLastMessage(_joinedMessage);
				}
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
			if (!_joinedMessage->detached()) {
				setLastMessage(_joinedMessage);
			}
		}
	}
}

void ChannelHistory::checkMaxReadMessageDate() {
	if (_maxReadMessageDate.isValid()) return;

	for (int32 blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int32 itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			if ((item->isImportant() || isMegagroup()) && !item->unread()) {
				_maxReadMessageDate = item->date;
				if (item->isGroupMigrate() && isMegagroup() && peer->migrateFrom()) {
					_maxReadMessageDate = date(MTP_int(peer->asChannel()->date + 1)); // no report spam panel
				}
				return;
			}
		}
	}
	if (loadedAtTop() && (!isMegagroup() || !isEmpty())) {
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
	bool isImportantFlags = isImportantChannelMessage(idFromMessage(msg), flagsFromMessage(msg));
	bool isImportant = (isChannel() && !isMegagroup()) ? isImportantFlags : true;

	if (!loadedAtBottom()) {
		HistoryItem *item = addToHistory(msg);
		if (item && isImportant) {
			setLastMessage(item);
			if (type == NewMessageUnread) {
				newItemAdded(item);
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

	if (!isImportantFlags && !onlyImportant() && !isEmpty() && type == NewMessageLast) {
		clear(true);
	}

	HistoryBlock *to = 0;
	bool newBlock = blocks.isEmpty();
	if (newBlock) {
		to = new HistoryBlock(this);
	} else {
		to = blocks.back();
	}
	HistoryItem *item = createItem((type == NewMessageLast) ? 0 : to, msg, (type == NewMessageUnread));
	if (type == NewMessageLast) {
		if (!item->detached()) {
			return item;
		}
		item->attach(to);
	}
	return addNewItem(to, newBlock, item, (type == NewMessageUnread));
}

void ChannelHistory::addNewToOther(HistoryItem *item, NewMessageType type) {
	if (!_otherNewLoaded || isMegagroup()) return;

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
	if (isMegagroup() && !_onlyImportant) return;

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
			blocks.push_back(block); // CHECK
			if (width) {
				block->y = height;
				height += block->height;
			}
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

	uint64 ms = getms();
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
		history->typingDots = 0;
		_a_typings.start();
	}
	history->updateTyping(ms, true);
}

void Histories::step_typings(uint64 ms, bool timer) {
	for (TypingHistories::iterator i = typing.begin(), e = typing.end(); i != e;) {
		i.key()->typingDots = (ms - i.value()) / 150;
		i.key()->updateTyping(ms);
		if (i.key()->typing.isEmpty() && i.key()->sendActions.isEmpty()) {
			i = typing.erase(i);
		} else {
			++i;
		}
	}
	if (typing.isEmpty()) {
		_a_typings.stop();
	}
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

HistoryItem *History::createItem(HistoryBlock *block, const MTPMessage &msg, bool applyServiceAction) {
	MsgId msgId = 0;
	switch (msg.type()) {
	case mtpc_messageEmpty: msgId = msg.c_messageEmpty().vid.v; break;
	case mtpc_message: msgId = msg.c_message().vid.v; break;
	case mtpc_messageService: msgId = msg.c_messageService().vid.v; break;
	}
	if (!msgId) return 0;

	HistoryItem *result = App::histItemById(channelId(), msgId);
	if (result) {
		if (block) {
			if (!result->detached()) {
				result->detach();
			}
			result->attach(block);
		}
		if (msg.type() == mtpc_message) {
			result->updateMedia(msg.c_message().has_media() ? (&msg.c_message().vmedia) : 0);
			result->initDimensions();
			if (!block) {
				Notify::historyItemResized(result);
			}
			if (applyServiceAction) {
				App::checkSavedGif(result);
			}
		}
		return result;
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
		if (badMedia == 1) {
			QString text(lng_message_unsupported(lt_link, qsl("https://desktop.telegram.org")));
			EntitiesInText entities = textParseEntities(text, _historyTextNoMonoOptions.flags);
			entities.push_front(EntityInText(EntityInTextItalic, 0, text.size()));
			result = new HistoryMessage(this, block, m.vid.v, m.vflags.v, m.vvia_bot_id.v, date(m.vdate), m.vfrom_id.v, text, entities, 0);
		} else if (badMedia) {
			result = new HistoryServiceMsg(this, block, m.vid.v, date(m.vdate), lang(lng_message_empty), m.vflags.v, 0, m.has_from_id() ? m.vfrom_id.v : 0);
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
				if (peer->isMegagroup()) {
					const QVector<MTPint> &v(d.vusers.c_vector().v);
					for (int32 i = 0, l = v.size(); i < l; ++i) {
						if (UserData *user = App::userLoaded(peerFromUser(v.at(i)))) {
							if (peer->asChannel()->mgInfo->lastParticipants.indexOf(user) < 0) {
								peer->asChannel()->mgInfo->lastParticipants.push_front(user);
								peer->asChannel()->mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsAdminsOutdated;
							}
							if (user->botInfo) {
								peer->asChannel()->mgInfo->bots.insert(user, true);
								if (peer->asChannel()->mgInfo->botStatus != 0 && peer->asChannel()->mgInfo->botStatus < 2) {
									peer->asChannel()->mgInfo->botStatus = 2;
								}
							}
						}
					}
				}
			} break;

			case mtpc_messageActionChatJoinedByLink: {
				const MTPDmessageActionChatJoinedByLink &d(action.c_messageActionChatJoinedByLink());
				if (peer->isMegagroup()) {
					if (result->from()->isUser()) {
						if (peer->asChannel()->mgInfo->lastParticipants.indexOf(result->from()->asUser()) < 0) {
							peer->asChannel()->mgInfo->lastParticipants.push_front(result->from()->asUser());
						}
						if (result->from()->asUser()->botInfo) {
							peer->asChannel()->mgInfo->bots.insert(result->from()->asUser(), true);
							if (peer->asChannel()->mgInfo->botStatus != 0 && peer->asChannel()->mgInfo->botStatus < 2) {
								peer->asChannel()->mgInfo->botStatus = 2;
							}
						}
					}
				}
			} break;

			case mtpc_messageActionChatDeletePhoto: {
				ChatData *chat = peer->asChat();
				if (chat) chat->setPhoto(MTP_chatPhotoEmpty());
			} break;

			case mtpc_messageActionChatDeleteUser: {
				const MTPDmessageActionChatDeleteUser &d(action.c_messageActionChatDeleteUser());
				PeerId uid = peerFromUser(d.vuser_id);
				if (lastKeyboardFrom == uid) {
					clearLastKeyboard();
					if (App::main()) App::main()->updateBotKeyboard(this);
				}
				if (peer->isMegagroup()) {
					if (UserData *user = App::userLoaded(uid)) {
						int32 index = peer->asChannel()->mgInfo->lastParticipants.indexOf(user);
						if (index >= 0) {
							peer->asChannel()->mgInfo->lastParticipants.removeAt(index);
						}
						peer->asChannel()->mgInfo->lastAdmins.remove(user);
						peer->asChannel()->mgInfo->bots.remove(user);
						if (peer->asChannel()->mgInfo->bots.isEmpty() && peer->asChannel()->mgInfo->botStatus > 0) {
							peer->asChannel()->mgInfo->botStatus = -1;
						}
					}
				}
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

			case mtpc_messageActionChatMigrateTo: {
				peer->asChat()->flags |= MTPDchat::flag_deactivated;

				//const MTPDmessageActionChatMigrateTo &d(action.c_messageActionChatMigrateTo());
				//PeerData *channel = App::peerLoaded(peerFromChannel(d.vchannel_id));
			} break;

			case mtpc_messageActionChannelMigrateFrom: {
				//const MTPDmessageActionChannelMigrateFrom &d(action.c_messageActionChannelMigrateFrom());
				//PeerData *chat = App::peerLoaded(peerFromChat(d.vchat_id));
			} break;
			}
		}
	} break;
	}

	if (applyServiceAction) {
		App::checkSavedGif(result);
	}

	return regItem(result);
}

HistoryItem *History::createItemForwarded(HistoryBlock *block, MsgId id, QDateTime date, int32 from, HistoryMessage *msg) {
	return regItem(new HistoryForwarded(this, block, id, date, from, msg));
}

HistoryItem *History::createItemDocument(HistoryBlock *block, MsgId id, int32 flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc, const QString &caption) {
	HistoryItem *result = 0;

	if ((flags & MTPDmessage::flag_reply_to_msg_id) && replyTo > 0) {
		result = new HistoryReply(this, block, id, flags, viaBotId, replyTo, date, from, doc, caption);
	} else {
		result = new HistoryMessage(this, block, id, flags, viaBotId, date, from, doc, caption);
	}

	return regItem(result);
}

HistoryItem *History::createItemPhoto(HistoryBlock *block, MsgId id, int32 flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, PhotoData *photo, const QString &caption) {
	HistoryItem *result = 0;

	if (flags & MTPDmessage::flag_reply_to_msg_id && replyTo > 0) {
		result = new HistoryReply(this, block, id, flags, viaBotId, replyTo, date, from, photo, caption);
	} else {
		result = new HistoryMessage(this, block, id, flags, viaBotId, date, from, photo, caption);
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

	HistoryItem *result = new HistoryServiceMsg(this, to, msgId, date, text, flags, media);
	return addNewItem(to, newBlock, regItem(result), newMsg);
}

HistoryItem *History::addNewMessage(const MTPMessage &msg, NewMessageType type) {
	if (isChannel()) return asChannelHistory()->addNewChannelMessage(msg, type);

	if (type == NewMessageExisting) return addToHistory(msg);
	if (!loadedAtBottom() || peer->migrateTo()) {
		HistoryItem *item = addToHistory(msg);
		if (item) {
			setLastMessage(item);
			if (type == NewMessageUnread) {
				newItemAdded(item);
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
	HistoryItem *item = createItem((type == NewMessageLast) ? 0 : to, msg, (type == NewMessageUnread));
	if (type == NewMessageLast) {
		if (!item->detached()) {
			return item;
		}
		item->attach(to);
	}
	return addNewItem(to, newBlock, item, (type == NewMessageUnread));
}

HistoryItem *History::addToHistory(const MTPMessage &msg) {
	return createItem(0, msg, false);
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

HistoryItem *History::addNewDocument(MsgId id, int32 flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc, const QString &caption) {
	HistoryBlock *to = 0;
	bool newBlock = blocks.isEmpty();
	if (newBlock) {
		to = new HistoryBlock(this);
	} else {
		to = blocks.back();
	}
	return addNewItem(to, newBlock, createItemDocument(to, id, flags, viaBotId, replyTo, date, from, doc, caption), true);
}

HistoryItem *History::addNewPhoto(MsgId id, int32 flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, PhotoData *photo, const QString &caption) {
	HistoryBlock *to = 0;
	bool newBlock = blocks.isEmpty();
	if (newBlock) {
		to = new HistoryBlock(this);
	} else {
		to = blocks.back();
	}
	return addNewItem(to, newBlock, createItemPhoto(to, id, flags, viaBotId, replyTo, date, from, photo, caption), true);
}

void History::createInitialDateBlock(const QDateTime &date) {
	HistoryBlock *dateBlock = new HistoryBlock(this); // date block
	HistoryItem *dayItem = createDayServiceMsg(this, dateBlock, date);
	dateBlock->items.push_back(dayItem);
	if (width) {
		dateBlock->height += dayItem->resize(width);
	}

	blocks.push_front(dateBlock);
	if (width) {
		height += dateBlock->height;
		for (int32 i = 1, l = blocks.size(); i < l; ++i) {
			blocks.at(i)->y += dateBlock->height;
		}
	}
}

bool History::addToOverview(MediaOverviewType type, MsgId msgId, AddToOverviewMethod method) {
	bool adding = false;
	switch (method) {
	case AddToOverviewNew:
	case AddToOverviewFront: adding = (overviewIds[type].constFind(msgId) == overviewIds[type].cend()); break;
	case AddToOverviewBack: adding = (overviewCountData[type] != 0); break;
	}
	if (!adding) return false;

	overviewIds[type].insert(msgId, NullType());
	switch (method) {
	case AddToOverviewNew:
	case AddToOverviewBack: overview[type].push_back(msgId); break;
	case AddToOverviewFront: overview[type].push_front(msgId); break;
	}
	if (method == AddToOverviewNew) {
		if (overviewCountData[type] > 0) {
			++overviewCountData[type];
		}
		if (App::wnd()) App::wnd()->mediaOverviewUpdated(peer, type);
	}
	return true;
}

void History::eraseFromOverview(MediaOverviewType type, MsgId msgId) {
	if (overviewIds[type].isEmpty()) return;

	History::MediaOverviewIds::iterator i = overviewIds[type].find(msgId);
	if (i == overviewIds[type].cend()) return;

	overviewIds[type].erase(i);
	for (History::MediaOverview::iterator i = overview[type].begin(), e = overview[type].end(); i != e; ++i) {
		if ((*i) == msgId) {
			overview[type].erase(i);
			if (overviewCountData[type] > 0) {
				--overviewCountData[type];
			}
			break;
		}
	}
	if (App::wnd()) App::wnd()->mediaOverviewUpdated(peer, type);
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
		if (width) {
			dayItem->y = to->height;

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
		newItemAdded(adding);
	}

	adding->addToOverview(AddToOverviewNew);
	if (adding->from()->id) {
		if (adding->from()->isUser()) {
			QList<UserData*> *lastAuthors = 0;
			if (peer->isChat()) {
				lastAuthors = &peer->asChat()->lastAuthors;
			} else if (peer->isMegagroup()) {
				lastAuthors = &peer->asChannel()->mgInfo->lastParticipants;
				if (adding->from()->asUser()->botInfo) {
					peer->asChannel()->mgInfo->bots.insert(adding->from()->asUser(), true);
					if (peer->asChannel()->mgInfo->botStatus != 0 && peer->asChannel()->mgInfo->botStatus < 2) {
						peer->asChannel()->mgInfo->botStatus = 2;
					}
				}
			}
			if (lastAuthors) {
				int prev = lastAuthors->indexOf(adding->from()->asUser());
				if (prev > 0) {
					lastAuthors->removeAt(prev);
				} else if (prev < 0 && peer->isMegagroup()) { // nothing is outdated if just reordering
					peer->asChannel()->mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsAdminsOutdated;
				}
				if (prev) {
					lastAuthors->push_front(adding->from()->asUser());
				}
			}
		}
		if (adding->hasReplyMarkup()) {
			int32 markupFlags = App::replyMarkup(channelId(), adding->id).flags;
			if (!(markupFlags & MTPDreplyKeyboardMarkup::flag_selective) || adding->mentionsMe()) {
				QMap<PeerData*, bool> *markupSenders = 0;
				if (peer->isChat()) {
					markupSenders = &peer->asChat()->markupSenders;
				} else if (peer->isMegagroup()) {
					markupSenders = &peer->asChannel()->mgInfo->markupSenders;
				}
				if (markupSenders) {
					markupSenders->insert(adding->from(), true);
				}
				if (markupFlags & MTPDreplyKeyboardMarkup_flag_ZERO) { // zero markup means replyKeyboardHide
					if (lastKeyboardFrom == adding->from()->id || (!lastKeyboardInited && !peer->isChat() && !peer->isMegagroup() && !adding->out())) {
						clearLastKeyboard();
					}
				} else {
					bool botNotInChat = false;
					if (peer->isChat()) {
						botNotInChat = adding->from()->isUser() && (!peer->canWrite() || !peer->asChat()->participants.isEmpty()) && !peer->asChat()->participants.contains(adding->from()->asUser());
					} else if (peer->isMegagroup()) {
						botNotInChat = adding->from()->isUser() && (!peer->canWrite() || peer->asChannel()->mgInfo->botStatus != 0) && !peer->asChannel()->mgInfo->bots.contains(adding->from()->asUser());
					}
					if (botNotInChat) {
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
	}

	return adding;
}

void History::unregTyping(UserData *from) {
	uint64 updateAtMs = 0;
	TypingUsers::iterator i = typing.find(from);
	if (i != typing.end()) {
		updateAtMs = getms();
		i.value() = updateAtMs;
	}
	SendActionUsers::iterator j = sendActions.find(from);
	if (j != sendActions.end()) {
		if (!updateAtMs) updateAtMs = getms();
		j.value().until = updateAtMs;
	}
	if (updateAtMs) {
		updateTyping(updateAtMs, true);
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
		if (!item->unread()) {
			outboxRead(item);
		}
	} else if (item->unread()) {
		bool skip = false;
		if (!isChannel() || peer->asChannel()->amIn()) {
			notifies.push_back(item);
			App::main()->newUnreadMsg(this, item);
		}
	} else if (!item->isGroupMigrate() || !peer->isMegagroup()) {
		inboxRead(item);
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
		blocks.push_back(block); // CHECK
		if (width) {
			block->y = height;
		}
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

	HistoryItem *oldFirst = 0, *last = 0;
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

			last = addMessageGroupAfterPrevToBlock(group, last, block);
		}

		last = addItemAfterPrevToBlock(adding, last, block);
	}
	for (; groupsIt != groupsEnd; ++groupsIt) {
		if (groupsIt->type() != mtpc_messageGroup) continue;
		const MTPDmessageGroup &group(groupsIt->c_messageGroup());

		last = addMessageGroupAfterPrevToBlock(group, last, block);
	}

	if (!blocks.isEmpty()) {
		t_assert(blocks.size() > 1);
		oldFirst = blocks.at(1)->items.front();
	}
	while (oldFirst && last && oldFirst->type() == HistoryItemGroup && last->type() == HistoryItemGroup) {
		static_cast<HistoryGroup*>(last)->uniteWith(static_cast<HistoryGroup*>(oldFirst));
		oldFirst->destroy();
		if (blocks.isEmpty()) {
			oldFirst = 0;
		} else {
			t_assert(blocks.size() > 1);
			oldFirst = blocks.at(1)->items.front();
		}
	}
	if (oldFirst && last && last->date.date() != oldFirst->date.date()) {
		HistoryItem *dayItem = createDayServiceMsg(this, block, oldFirst->date);
		block->items.push_back(dayItem);
		if (width) {
			dayItem->y = block->height;
			block->height += dayItem->resize(width);
		}
	}
	if (block->items.isEmpty()) {
		oldLoaded = true;
		delete block;
	} else {
		if (oldFirst) {
			HistoryBlock *initial = blocks.at(0);
			blocks[0] = block;
			blocks.push_front(initial);
			if (width) {
				block->y = initial->height;
				for (int32 i = 2, l = blocks.size(); i < l; ++i) {
					blocks.at(i)->y += block->height;
				}
				height += block->height;
			}
			initial->items.at(0)->setDate(block->items.at(0)->date);
		} else {
			blocks.push_front(block);
			if (width) {
				height = block->height;
			}
			createInitialDateBlock(block->items.at(0)->date);
		}

		if (loadedAtBottom()) { // add photos to overview and authors to lastAuthors / lastParticipants
			bool channel = isChannel();
			int32 mask = 0;
			QList<UserData*> *lastAuthors = 0;
			QMap<PeerData*, bool> *markupSenders = 0;
			if (peer->isChat()) {
				lastAuthors = &peer->asChat()->lastAuthors;
				markupSenders = &peer->asChat()->markupSenders;
			} else if (peer->isMegagroup()) {
				lastAuthors = &peer->asChannel()->mgInfo->lastParticipants;
				markupSenders = &peer->asChannel()->mgInfo->markupSenders;
			}
			for (int32 i = block->items.size(); i > 0; --i) {
				HistoryItem *item = block->items[i - 1];
				mask |= item->addToOverview(AddToOverviewFront);
				if (item->from()->id) {
					if (lastAuthors) { // chats
						if (item->from()->isUser()) {
							if (!lastAuthors->contains(item->from()->asUser())) {
								lastAuthors->push_back(item->from()->asUser());
								if (peer->isMegagroup()) {
									peer->asChannel()->mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsAdminsOutdated;
								}
							}
						}
					}
					if (markupSenders) { // chats with bots
						if (!lastKeyboardInited && item->hasReplyMarkup() && !item->out()) {
							int32 markupFlags = App::replyMarkup(channelId(), item->id).flags;
							if (!(markupFlags & MTPDreplyKeyboardMarkup::flag_selective) || item->mentionsMe()) {
								bool wasKeyboardHide = markupSenders->contains(item->from());
								if (!wasKeyboardHide) {
									markupSenders->insert(item->from(), true);
								}
								if (!(markupFlags & MTPDreplyKeyboardMarkup_flag_ZERO)) {
									if (!lastKeyboardInited) {
										bool botNotInChat = false;
										if (peer->isChat()) {
											botNotInChat = (!peer->canWrite() || !peer->asChat()->participants.isEmpty()) && item->from()->isUser() && !peer->asChat()->participants.contains(item->from()->asUser());
										} else if (peer->isMegagroup()) {
											botNotInChat = (!peer->canWrite() || peer->asChannel()->mgInfo->botStatus != 0) && item->from()->isUser() && !peer->asChannel()->mgInfo->bots.contains(item->from()->asUser());
										}
										if (wasKeyboardHide || botNotInChat) {
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
						if (!(markupFlags & MTPDreplyKeyboardMarkup::flag_selective) || item->mentionsMe()) {
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

		if (block->items.isEmpty()) {
			newLoaded = true;
			setLastMessage(lastImportantMessage());
			delete block;
		} else {
			blocks.push_back(block);
			if (width) {
				block->y = height;
				height += block->height;
			}
			if (blocks.size() == 1) {
				createInitialDateBlock(block->items.at(0)->date);
			}
		}
	}

	if (!wasLoadedAtBottom && loadedAtBottom()) { // add all loaded photos to overview
		int32 mask = 0;
		for (int32 i = 0; i < OverviewCount; ++i) {
			if (overviewCountData[i] == 0) continue; // all loaded
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
				mask |= b->items[j]->addToOverview(AddToOverviewBack);
			}
		}
		for (int32 t = 0; t < OverviewCount; ++t) {
			if ((mask & (1 << t)) && App::wnd()) App::wnd()->mediaOverviewUpdated(peer, MediaOverviewType(t));
		}
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

	if (App::main()) {
		if (!dialogs.isEmpty()) {
			App::main()->dlgUpdated(dialogs[0]);
		}
		if (peer->migrateTo()) {
			if (History *h = App::historyLoaded(peer->migrateTo()->id)) {
				if (!h->dialogs.isEmpty()) {
					App::main()->dlgUpdated(h->dialogs[0]);
				}
			}
		}
	}

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
			if ((channel && !isMegagroup()) ? item->isImportant() : (item->type() == HistoryItemMsg)) {
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
		if (psUpdate && (!mute || cIncludeMuted()) && App::wnd()) App::wnd()->updateCounter();
		if (unreadBar) {
			int32 count = unreadCount;
			if (peer->migrateTo()) {
				if (History *h = App::historyLoaded(peer->migrateTo()->id)) {
					count += h->unreadCount;
				}
			}
			unreadBar->setCount(count);
		}
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

	int32 count = unreadCount;
	if (peer->migrateTo()) {
		if (History *h = App::historyLoaded(peer->migrateTo()->id)) {
			count += h->unreadCount;
		}
	}
	HistoryBlock *block = showFrom->block();
	unreadBar = new HistoryUnreadBar(this, block, count, showFrom->date);
	if (!addNewInTheMiddle(regItem(unreadBar), blocks.indexOf(block), block->items.indexOf(showFrom))) {
		unreadBar = 0;
	}
}

HistoryItem *History::addNewInTheMiddle(HistoryItem *newItem, int32 blockIndex, int32 itemIndex) {
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
	if (msgId < 0 && -msgId < ServerMaxMsgId && peer->migrateFrom()) { // old group history
		return App::history(peer->migrateFrom()->id)->isReadyFor(-msgId, fixInScrollMsgId, fixInScrollMsgTop);
	}

	if (msgId != ShowAtTheEndMsgId && msgId != ShowAtUnreadMsgId && isChannel()) {
		return asChannelHistory()->isSwitchReadyFor(msgId, fixInScrollMsgId, fixInScrollMsgTop);
	}
	fixInScrollMsgId = 0;
	fixInScrollMsgTop = 0;
	if (msgId == ShowAtTheEndMsgId) {
		return loadedAtBottom();
	}
	if (msgId == ShowAtUnreadMsgId) {
		if (peer->migrateFrom()) { // old group history
			if (History *h = App::historyLoaded(peer->migrateFrom()->id)) {
				if (h->unreadCount) {
					return h->isReadyFor(msgId, fixInScrollMsgId, fixInScrollMsgTop);
				}
			}
		}
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
	if (msgId < 0 && -msgId < ServerMaxMsgId && peer->migrateFrom()) {
		History *h = App::history(peer->migrateFrom()->id);
		h->getReadyFor(-msgId, fixInScrollMsgId, fixInScrollMsgTop);
		if (h->isEmpty()) {
			clear(true);
			newLoaded = oldLoaded = false;
			lastWidth = 0;
		}
		return;
	}
	if (msgId != ShowAtTheEndMsgId && msgId != ShowAtUnreadMsgId && isChannel()) {
		return asChannelHistory()->getSwitchReadyFor(msgId, fixInScrollMsgId, fixInScrollMsgTop);
	}
	if (msgId == ShowAtUnreadMsgId && peer->migrateFrom()) {
		if (History *h = App::historyLoaded(peer->migrateFrom()->id)) {
			if (h->unreadCount) {
				clear(true);
				newLoaded = oldLoaded = false;
				lastWidth = 0;
				h->getReadyFor(msgId, fixInScrollMsgId, fixInScrollMsgTop);
				return;
			}
		}
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
	bool updateDialog = (App::main() && (!peer->isChannel() || peer->asChannel()->amIn() || !dialogs.isEmpty()));
	if (peer->migrateTo() && dialogs.isEmpty()) {
		updateDialog = false;
	}
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

int32 History::geomResize(int32 newWidth, int32 *ytransform, const HistoryItem *resizedItem) {
	if (width != newWidth) resizedItem = 0; // recount all items
	if (width != newWidth || resizedItem) {
		width = newWidth;
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
				if (overviewCountData[i] == 0) {
					overviewCountData[i] = overview[i].size();
				}
			} else {
				overviewCountData[i] = -1; // not loaded yet
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
		if (isMegagroup()) {
			peer->asChannel()->mgInfo->markupSenders.clear();
		}
	}
	if (leaveItems && App::main()) App::main()->historyCleared(this);
}

void History::overviewSliceDone(int32 overviewIndex, const MTPmessages_Messages &result, bool onlyCounts) {
	const QVector<MTPMessage> *v = 0;
	switch (result.type()) {
	case mtpc_messages_messages: {
		const MTPDmessages_messages &d(result.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
		overviewCountData[overviewIndex] = 0;
	} break;

	case mtpc_messages_messagesSlice: {
		const MTPDmessages_messagesSlice &d(result.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		overviewCountData[overviewIndex] = d.vcount.v;
		v = &d.vmessages.c_vector().v;
	} break;

	case mtpc_messages_channelMessages: {
		const MTPDmessages_channelMessages &d(result.c_messages_channelMessages());
		if (peer->isChannel()) {
			peer->asChannel()->ptsReceived(d.vpts.v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (History::overviewSliceDone, onlyCounts %1)").arg(Logs::b(onlyCounts)));
		}
		if (d.has_collapsed()) { // should not be returned
			LOG(("API Error: channels.getMessages and messages.getMessages should not return collapsed groups! (History::overviewSliceDone, onlyCounts %1)").arg(Logs::b(onlyCounts)));
		}

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		overviewCountData[overviewIndex] = d.vcount.v;
		v = &d.vmessages.c_vector().v;
	} break;

	default: return;
	}

	if (!onlyCounts && v->isEmpty()) {
		overviewCountData[overviewIndex] = 0;
	} else if (overviewCountData[overviewIndex] > 0) {
		for (History::MediaOverviewIds::const_iterator i = overviewIds[overviewIndex].cbegin(), e = overviewIds[overviewIndex].cend(); i != e; ++i) {
			if (i.key() < 0) {
				++overviewCountData[overviewIndex];
			} else {
				break;
			}
		}
	}

	for (QVector<MTPMessage>::const_iterator i = v->cbegin(), e = v->cend(); i != e; ++i) {
		HistoryItem *item = App::histories().addNewMessage(*i, NewMessageExisting);
		if (item && overviewIds[overviewIndex].constFind(item->id) == overviewIds[overviewIndex].cend()) {
			overviewIds[overviewIndex].insert(item->id, NullType());
			overview[overviewIndex].push_front(item->id);
		}
	}
}

void History::changeMsgId(MsgId oldId, MsgId newId) {
	for (int32 i = 0; i < OverviewCount; ++i) {
		History::MediaOverviewIds::iterator j = overviewIds[i].find(oldId);
		if (j != overviewIds[i].cend()) {
			overviewIds[i].erase(j);
			int32 index = overview[i].indexOf(oldId);
			if (overviewIds[i].constFind(newId) == overviewIds[i].cend()) {
				overviewIds[i].insert(newId, NullType());
				if (index >= 0) {
					overview[i][index] = newId;
				} else {
					overview[i].push_back(newId);
				}
			} else if (index >= 0) {
				overview[i].removeAt(index);
			}
		}
	}
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

void History::clearUpto(MsgId msgId) {
	for (HistoryItem *item = isEmpty() ? 0 : blocks.back()->items.back(); item && (item->id < 0 || item->id >= msgId); item = isEmpty() ? 0 : blocks.back()->items.back()) {
		item->destroy();
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

int32 HistoryBlock::geomResize(int32 newWidth, int32 *ytransform, const HistoryItem *resizedItem) {
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
		history()->clearLastKeyboard();
		if (App::main()) App::main()->updateBotKeyboard(history());
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

void HistoryItem::setId(MsgId newId) {
	history()->changeMsgId(id, newId);
	id = newId;
}

bool HistoryItem::displayFromPhoto() const {
	return Adaptive::Wide() || (!out() && !history()->peer->isUser() && !fromChannel());
}

bool HistoryItem::shiftFromPhoto() const {
	return Adaptive::Wide() && !out() && !history()->peer->isUser() && !fromChannel();
}

void HistoryItem::clipCallback(ClipReaderNotification notification) {
	HistoryMedia *media = getMedia();
	if (!media) return;

	ClipReader *reader = media ? media->getClipReader() : 0;
	if (!reader) return;

	switch (notification) {
	case ClipReaderReinit: {
		bool stopped = false;
		if (reader->paused()) {
			if (MainWidget *m = App::main()) {
				if (!m->isItemVisible(this)) { // stop animation if it is not visible
					media->stopInline(this);
					if (DocumentData *document = media->getDocument()) { // forget data from memory
						document->forget();
					}
					stopped = true;
				}
			}
		}
		if (!stopped) {
			initDimensions();
			Notify::historyItemResized(this);
			Notify::historyItemLayoutChanged(this);
		}
	} break;

	case ClipReaderRepaint: {
		if (!reader->currentDisplayed()) {
			Ui::repaintHistoryItem(this);
		}
	} break;
	}
}

HistoryItem::~HistoryItem() {
	App::historyUnregItem(this);
	if (id < 0 && App::uploader()) {
		App::uploader()->cancel(fullId());
	}
}

HistoryItem *regItem(HistoryItem *item) {
	if (item) {
		App::historyRegItem(item);
		item->initDimensions();
	}
	return item;
}

RadialAnimation::RadialAnimation(AnimationCreator creator)
: _firstStart(0)
, _lastStart(0)
, _lastTime(0)
, _opacity(0)
, a_arcEnd(0, 0)
, a_arcStart(0, FullArcLength)
, _animation(creator) {

}

void RadialAnimation::start(float64 prg) {
	_firstStart = _lastStart = _lastTime = getms();
	int32 iprg = qRound(qMax(prg, 0.0001) * AlmostFullArcLength), iprgstrict = qRound(prg * AlmostFullArcLength);
	a_arcEnd = anim::ivalue(iprgstrict, iprg);
	_animation.start();
}

void RadialAnimation::update(float64 prg, bool finished, uint64 ms) {
	int32 iprg = qRound(qMax(prg, 0.0001) * AlmostFullArcLength);
	if (iprg != a_arcEnd.to()) {
		a_arcEnd.start(iprg);
		_lastStart = _lastTime;
	}
	_lastTime = ms;

	float64 dt = float64(ms - _lastStart), fulldt = float64(ms - _firstStart);
	_opacity = qMin(fulldt / st::radialDuration, 1.);
	if (!finished) {
		a_arcEnd.update(1. - (st::radialDuration / (st::radialDuration + dt)), anim::linear);
	} else if (dt >= st::radialDuration) {
		a_arcEnd.update(1, anim::linear);
		stop();
	} else {
		float64 r = dt / st::radialDuration;
		a_arcEnd.update(r, anim::linear);
		_opacity *= 1 - r;
	}
	float64 fromstart = fulldt / st::radialPeriod;
	a_arcStart.update(fromstart - qFloor(fromstart), anim::linear);
}

void RadialAnimation::stop() {
	_firstStart = _lastStart = _lastTime = 0;
	a_arcEnd = anim::ivalue(0, 0);
	_animation.stop();
}

void RadialAnimation::step(uint64 ms) {
	_animation.step(ms);
}

void RadialAnimation::draw(Painter &p, const QRect &inner, int32 thickness, const style::color &color) {
	float64 o = p.opacity();
	p.setOpacity(o * _opacity);

	QPen pen(color->p), was(p.pen());
	pen.setWidth(thickness);
	p.setPen(pen);

	int32 len = MinArcLength + a_arcEnd.current();
	int32 from = QuarterArcLength - a_arcStart.current() - len;
	if (rtl()) {
		from = QuarterArcLength - (from - QuarterArcLength) - len;
		if (from < 0) from += FullArcLength;
	}

	p.setRenderHint(QPainter::HighQualityAntialiasing);
	p.drawArc(inner, from, len);
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);

	p.setPen(was);
	p.setOpacity(o);
}

namespace {
	int32 videoMaxStatusWidth(VideoData *video) {
		int32 result = st::normalFont->width(formatDownloadText(video->size, video->size));
		result = qMax(result, st::normalFont->width(formatDurationAndSizeText(video->duration, video->size)));
		return result;
	}

	int32 audioMaxStatusWidth(AudioData *audio) {
		int32 result = st::normalFont->width(formatDownloadText(audio->size, audio->size));
		result = qMax(result, st::normalFont->width(formatPlayedText(audio->duration, audio->duration)));
		result = qMax(result, st::normalFont->width(formatDurationAndSizeText(audio->duration, audio->size)));
		return result;
	}

	int32 documentMaxStatusWidth(DocumentData *document) {
		int32 result = st::normalFont->width(formatDownloadText(document->size, document->size));
		if (SongData *song = document->song()) {
			result = qMax(result, st::normalFont->width(formatPlayedText(song->duration, song->duration)));
			result = qMax(result, st::normalFont->width(formatDurationAndSizeText(song->duration, document->size)));
		} else {
			result = qMax(result, st::normalFont->width(formatSizeText(document->size)));
		}
		return result;
	}

	int32 gifMaxStatusWidth(DocumentData *document) {
		int32 result = st::normalFont->width(formatDownloadText(document->size, document->size));
		result = qMax(result, st::normalFont->width(formatGifAndSizeText(document->size)));
		return result;
	}
}

HistoryFileMedia::HistoryFileMedia() : HistoryMedia()
, _animation(0) {
}

void HistoryFileMedia::linkOver(HistoryItem *parent, const TextLinkPtr &lnk) {
	if ((lnk == _savel || lnk == _cancell) && !dataLoaded()) {
		ensureAnimation(parent);
		_animation->a_thumbOver.start(1);
		_animation->_a_thumbOver.start();
	}
}

void HistoryFileMedia::linkOut(HistoryItem *parent, const TextLinkPtr &lnk) {
	if (_animation && (lnk == _savel || lnk == _cancell)) {
		_animation->a_thumbOver.start(0);
		_animation->_a_thumbOver.start();
	}
}

void HistoryFileMedia::setLinks(ITextLink *openl, ITextLink *savel, ITextLink *cancell) {
	_openl.reset(openl);
	_savel.reset(savel);
	_cancell.reset(cancell);
}

void HistoryFileMedia::setStatusSize(int32 newSize, int32 fullSize, int32 duration, qint64 realDuration) const {
	_statusSize = newSize;
	if (_statusSize == FileStatusSizeReady) {
		_statusText = (duration >= 0) ? formatDurationAndSizeText(duration, fullSize) : (duration < -1 ? formatGifAndSizeText(fullSize) : formatSizeText(fullSize));
	} else if (_statusSize == FileStatusSizeLoaded) {
		_statusText = (duration >= 0) ? formatDurationText(duration) : (duration < -1 ? qsl("GIF") : formatSizeText(fullSize));
	} else if (_statusSize == FileStatusSizeFailed) {
		_statusText = lang(lng_attach_failed);
	} else if (_statusSize >= 0) {
		_statusText = formatDownloadText(_statusSize, fullSize);
	} else {
		_statusText = formatPlayedText(-_statusSize - 1, realDuration);
	}
}

void HistoryFileMedia::step_thumbOver(const HistoryItem *parent, float64 ms, bool timer) {
	float64 dt = ms / st::msgFileOverDuration;
	if (dt >= 1) {
		_animation->a_thumbOver.finish();
		_animation->_a_thumbOver.stop();
		checkAnimationFinished();
	} else if (!timer) {
		_animation->a_thumbOver.update(dt, anim::linear);
	}
	if (timer) {
		Ui::repaintHistoryItem(parent);
	}
}

void HistoryFileMedia::step_radial(const HistoryItem *parent, uint64 ms, bool timer) {
	if (timer) {
		Ui::repaintHistoryItem(parent);
	} else {
		_animation->radial.update(dataProgress(), dataFinished(), ms);
		if (!_animation->radial.animating()) {
			checkAnimationFinished();
		}
	}
}

void HistoryFileMedia::ensureAnimation(const HistoryItem *parent) const {
	if (!_animation) {
		_animation = new AnimationData(
			animation(parent, const_cast<HistoryFileMedia*>(this), &HistoryFileMedia::step_thumbOver),
			animation(parent, const_cast<HistoryFileMedia*>(this), &HistoryFileMedia::step_radial));
	}
}

void HistoryFileMedia::checkAnimationFinished() {
	if (_animation && !_animation->_a_thumbOver.animating() && !_animation->radial.animating()) {
		if (dataLoaded()) {
			delete _animation;
			_animation = 0;
		}
	}
}

HistoryFileMedia::~HistoryFileMedia() {
	deleteAndMark(_animation);
}

HistoryPhoto::HistoryPhoto(PhotoData *photo, const QString &caption, const HistoryItem *parent) : HistoryFileMedia()
, _data(photo)
, _pixw(1)
, _pixh(1)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	setLinks(new PhotoLink(_data), new PhotoSaveLink(_data), new PhotoCancelLink(_data));

	if (!caption.isEmpty()) {
		_caption.setText(st::msgFont, caption + parent->skipBlock(), itemTextNoMonoOptions(parent));
	}
	init();
}

HistoryPhoto::HistoryPhoto(PeerData *chat, const MTPDphoto &photo, int32 width) : HistoryFileMedia()
, _data(App::feedPhoto(photo))
, _pixw(1)
, _pixh(1) {
	setLinks(new PhotoLink(_data, chat), new PhotoSaveLink(_data, chat), new PhotoCancelLink(_data));

	_width = width;
	init();
}

HistoryPhoto::HistoryPhoto(const HistoryPhoto &other) : HistoryFileMedia()
, _data(other._data)
, _pixw(other._pixw)
, _pixh(other._pixh)
, _caption(other._caption) {
	setLinks(new PhotoLink(_data), new PhotoSaveLink(_data), new PhotoCancelLink(_data));

	init();
}

void HistoryPhoto::init() {
	_data->thumb->load();
}

void HistoryPhoto::initDimensions(const HistoryItem *parent) {
	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(parent->skipBlockWidth(), parent->skipBlockHeight());
	}

	int32 tw = convertScale(_data->full->width()), th = convertScale(_data->full->height());
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

	if (parent->toHistoryMessage()) {
		bool bubble = parent->hasBubble();

		int32 minWidth = qMax(st::minPhotoSize, parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
		int32 maxActualWidth = qMax(tw, minWidth);
		_maxw = qMax(maxActualWidth, th);
		_minh = qMax(th, int32(st::minPhotoSize));
		if (bubble) {
			maxActualWidth += st::mediaPadding.left() + st::mediaPadding.right();
			_maxw += st::mediaPadding.left() + st::mediaPadding.right();
			_minh += st::mediaPadding.top() + st::mediaPadding.bottom();
			if (!_caption.isEmpty()) {
				_minh += st::mediaCaptionSkip + _caption.countHeight(maxActualWidth - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();
			}
		}
	} else {
		_maxw = _minh = _width;
	}
}

int32 HistoryPhoto::resize(int32 width, const HistoryItem *parent) {
	bool bubble = parent->hasBubble();

	int32 tw = convertScale(_data->full->width()), th = convertScale(_data->full->height());
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	if (th > st::maxMediaSize) {
		tw = (st::maxMediaSize * tw) / th;
		th = st::maxMediaSize;
	}

	_pixw = qMin(width, _maxw);
	if (bubble) {
		_pixw -= st::mediaPadding.left() + st::mediaPadding.right();
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

	int32 minWidth = qMax(st::minPhotoSize, parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_width = qMax(_pixw, int16(minWidth));
	_height = qMax(_pixh, int16(st::minPhotoSize));
	if (bubble) {
		_width += st::mediaPadding.left() + st::mediaPadding.right();
		_height += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			int32 captionw = _width - st::msgPadding.left() - st::msgPadding.right();
			_height += st::mediaCaptionSkip + _caption.countHeight(captionw) + st::msgPadding.bottom();
		}
	}
	return _height;
}

void HistoryPhoto::draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();

	bool notChild = (parent->getMedia() == this);
	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = parent->hasBubble();
	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	int32 captionw = width - st::msgPadding.left() - st::msgPadding.right();

	if (displayLoading) {
		ensureAnimation(parent);
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	bool radial = isRadialAnimation(ms);

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			height -= st::mediaCaptionSkip + _caption.countHeight(captionw) + st::msgPadding.bottom();
		}
	} else {
		App::roundShadow(p, 0, 0, width, height, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	QPixmap pix;
	if (loaded) {
		pix = _data->full->pixSingle(_pixw, _pixh, width, height);
	} else {
		pix = _data->thumb->pixBlurredSingle(_pixw, _pixh, width, height);
	}
	QRect rthumb(rtlrect(skipx, skipy, width, height, _width));
	p.drawPixmap(rthumb.topLeft(), pix);
	if (selected) {
		App::roundRect(p, rthumb, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	if (notChild && (radial || (!loaded && !_data->loading()))) {
		float64 radialOpacity = (radial && loaded && !_data->uploading()) ? _animation->radial.opacity() : 1;
		QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else if (isThumbAnimation(ms)) {
			float64 over = _animation->a_thumbOver.current();
			p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
			p.setBrush(st::black);
		} else {
			bool over = textlnkDrawOver(_data->loading() ? _cancell : _savel);
			p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}

		p.setOpacity(radialOpacity * p.opacity());

		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.drawEllipse(inner);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);

		p.setOpacity(radial ? _animation->radial.opacity() : 1);

		p.setOpacity(radialOpacity);
		style::sprite icon;
		if (radial || _data->loading()) {
			DelayedStorageImage *delayed = _data->full->toDelayedStorageImage();
			if (!delayed || !delayed->location().isNull()) {
				icon = (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
			}
		} else {
			icon = (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
		}
		if (!icon.isEmpty()) {
			p.drawSpriteCenter(inner, icon);
		}
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::msgInBgSelected : st::msgInBg);
		}
	}

	// date
	if (_caption.isEmpty()) {
		if (notChild) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			parent->drawInfo(p, fullRight, fullBottom, 2 * skipx + width, selected, InfoDisplayOverImage);
		}
	} else {
		p.setPen(st::black);
		_caption.draw(p, st::msgPadding.left(), skipy + height + st::mediaPadding.bottom() + st::mediaCaptionSkip, captionw);
	}
}

void HistoryPhoto::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = parent->hasBubble();

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();
		if (!_caption.isEmpty()) {
			int32 captionw = width - st::msgPadding.left() - st::msgPadding.right();
			height -= _caption.countHeight(captionw) + st::msgPadding.bottom();
			if (x >= st::msgPadding.left() && y >= height && x < st::msgPadding.left() + captionw && y < _height) {
				bool inText = false;
				_caption.getState(lnk, inText, x - st::msgPadding.left(), y - height, captionw);
				state = inText ? HistoryInTextCursorState : HistoryDefaultCursorState;
				return;
			}
			height -= st::mediaCaptionSkip;
		}
		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height) {
		if (_data->uploading()) {
			lnk = _cancell;
		} else if (_data->loaded()) {
			lnk = _openl;
		} else if (_data->loading()) {
			DelayedStorageImage *delayed = _data->full->toDelayedStorageImage();
			if (!delayed || !delayed->location().isNull()) {
				lnk = _cancell;
			}
		} else {
			lnk = _savel;
		}
		if (_caption.isEmpty() && parent->getMedia() == this) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			bool inDate = parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
			if (inDate) {
				state = HistoryInDateCursorState;
			}
		}
		return;
	}
}

void HistoryPhoto::updateFrom(const MTPMessageMedia &media, HistoryItem *parent) {
	if (media.type() == mtpc_messageMediaPhoto) {
		const MTPPhoto &photo(media.c_messageMediaPhoto().vphoto);
		App::feedPhoto(photo, _data);

		if (photo.type() == mtpc_photo) {
			const QVector<MTPPhotoSize> &sizes(photo.c_photo().vsizes.c_vector().v);
			int32 max = 0;
			const MTPDfileLocation *maxLocation = 0;
			for (int32 i = 0, l = sizes.size(); i < l; ++i) {
				char size = 0;
				const MTPFileLocation *loc = 0;
				switch (sizes.at(i).type()) {
				case mtpc_photoSize: {
					const string &s(sizes.at(i).c_photoSize().vtype.c_string().v);
					loc = &sizes.at(i).c_photoSize().vlocation;
					if (s.size()) size = s[0];
				} break;

				case mtpc_photoCachedSize: {
					const string &s(sizes.at(i).c_photoCachedSize().vtype.c_string().v);
					loc = &sizes.at(i).c_photoCachedSize().vlocation;
					if (s.size()) size = s[0];
				} break;
				}
				if (!loc || loc->type() != mtpc_fileLocation) continue;
				if (size == 's') {
					Local::writeImage(storageKey(loc->c_fileLocation()), _data->thumb);
				} else if (size == 'm') {
					Local::writeImage(storageKey(loc->c_fileLocation()), _data->medium);
				} else if (size == 'x' && max < 1) {
					max = 1;
					maxLocation = &loc->c_fileLocation();
				} else if (size == 'y' && max < 2) {
					max = 2;
					maxLocation = &loc->c_fileLocation();
				//} else if (size == 'w' && max < 3) {
				//	max = 3;
				//	maxLocation = &loc->c_fileLocation();
				}
			}
			if (maxLocation) {
				Local::writeImage(storageKey(*maxLocation), _data->full);
			}
		}
	}
}

void HistoryPhoto::regItem(HistoryItem *item) {
	App::regPhotoItem(_data, item);
}

void HistoryPhoto::unregItem(HistoryItem *item) {
	App::unregPhotoItem(_data, item);
}

const QString HistoryPhoto::inDialogsText() const {
	return _caption.isEmpty() ? lang(lng_in_dlg_photo) : _caption.original(0, 0xFFFF, Text::ExpandLinksNone);
}

const QString HistoryPhoto::inHistoryText() const {
	return qsl("[ ") + lang(lng_in_dlg_photo) + (_caption.isEmpty() ? QString() : (qsl(", ") + _caption.original(0, 0xFFFF, Text::ExpandLinksAll))) + qsl(" ]");
}

ImagePtr HistoryPhoto::replyPreview() {
	return _data->makeReplyPreview();
}

HistoryVideo::HistoryVideo(const MTPDvideo &video, const QString &caption, HistoryItem *parent) : HistoryFileMedia()
, _data(App::feedVideo(video))
, _thumbw(1)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	if (!caption.isEmpty()) {
		_caption.setText(st::msgFont, caption + parent->skipBlock(), itemTextNoMonoOptions(parent));
	}

	setLinks(new VideoOpenLink(_data), new VideoSaveLink(_data), new VideoCancelLink(_data));

	setStatusSize(FileStatusSizeReady);

	_data->thumb->load();
}

HistoryVideo::HistoryVideo(const HistoryVideo &other) : HistoryFileMedia()
, _data(other._data)
, _thumbw(other._thumbw)
, _caption(other._caption) {
	setLinks(new VideoOpenLink(_data), new VideoSaveLink(_data), new VideoCancelLink(_data));

	setStatusSize(other._statusSize);
}

void HistoryVideo::initDimensions(const HistoryItem *parent) {
	bool bubble = parent->hasBubble();

	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(parent->skipBlockWidth(), parent->skipBlockHeight());
	}

	int32 tw = convertScale(_data->thumb->width()), th = convertScale(_data->thumb->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	if (tw * st::msgVideoSize.height() > th * st::msgVideoSize.width()) {
		th = qRound((st::msgVideoSize.width() / float64(tw)) * th);
		tw = st::msgVideoSize.width();
	} else {
		tw = qRound((st::msgVideoSize.height() / float64(th)) * tw);
		th = st::msgVideoSize.height();
	}

	_thumbw = qMax(tw, 1);
	int32 minWidth = qMax(st::minPhotoSize, parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	minWidth = qMax(minWidth, videoMaxStatusWidth(_data) + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_maxw = qMax(_thumbw, int16(minWidth));
	_minh = qMax(th, int32(st::minPhotoSize));
	if (bubble) {
		_maxw += st::mediaPadding.left() + st::mediaPadding.right();
		_minh += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			_minh += st::mediaCaptionSkip + _caption.countHeight(_maxw - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();
		}
	}
}

int32 HistoryVideo::resize(int32 width, const HistoryItem *parent) {
	bool bubble = parent->hasBubble();

	int32 tw = convertScale(_data->thumb->width()), th = convertScale(_data->thumb->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	if (tw * st::msgVideoSize.height() > th * st::msgVideoSize.width()) {
		th = qRound((st::msgVideoSize.width() / float64(tw)) * th);
		tw = st::msgVideoSize.width();
	} else {
		tw = qRound((st::msgVideoSize.height() / float64(th)) * tw);
		th = st::msgVideoSize.height();
	}

	if (bubble) {
		width -= st::mediaPadding.left() + st::mediaPadding.right();
	}
	if (width < tw) {
		th = qRound((width / float64(tw)) * th);
		tw = width;
	}

	int32 minWidth = qMax(st::minPhotoSize, parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	minWidth = qMax(minWidth, videoMaxStatusWidth(_data) + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_width = qMax(_thumbw, int16(minWidth));
	_height = qMax(th, int32(st::minPhotoSize));
	if (bubble) {
		_width += st::mediaPadding.left() + st::mediaPadding.right();
		_height += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			int32 captionw = _width - st::msgPadding.left() - st::msgPadding.right();
			_height += st::mediaCaptionSkip + _caption.countHeight(captionw) + st::msgPadding.bottom();
		}
	}
	return _height;
}

void HistoryVideo::draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();

	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = parent->hasBubble();
	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	int32 captionw = width - st::msgPadding.left() - st::msgPadding.right();

	if (displayLoading) {
		ensureAnimation(parent);
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	updateStatusText(parent);
	bool radial = isRadialAnimation(ms);

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			height -= st::mediaCaptionSkip + _caption.countHeight(captionw) + st::msgPadding.bottom();
		}
	} else {
		App::roundShadow(p, 0, 0, width, height, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	QRect rthumb(rtlrect(skipx, skipy, width, height, _width));
	p.drawPixmap(rthumb.topLeft(), _data->thumb->pixBlurredSingle(_thumbw, 0, width, height));
	if (selected) {
		App::roundRect(p, rthumb, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
	p.setPen(Qt::NoPen);
	if (selected) {
		p.setBrush(st::msgDateImgBgSelected);
	} else if (isThumbAnimation(ms)) {
		float64 over = _animation->a_thumbOver.current();
		p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
		p.setBrush(st::black);
	} else {
		bool over = textlnkDrawOver(_data->loading() ? _cancell : _savel);
		p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
	}

	p.setRenderHint(QPainter::HighQualityAntialiasing);
	p.drawEllipse(inner);
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);

	if (!selected && _animation) {
		p.setOpacity(1);
	}

	style::sprite icon;
	if (loaded) {
		icon = (selected ? st::msgFileInPlaySelected : st::msgFileInPlay);
	} else if (radial || _data->loading()) {
		icon = (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
	} else {
		icon = (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
	}
	p.drawSpriteCenter(inner, icon);
	if (radial) {
		QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
		_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::msgInBgSelected : st::msgInBg);
	}

	int32 statusX = skipx + st::msgDateImgDelta + st::msgDateImgPadding.x(), statusY = skipy + st::msgDateImgDelta + st::msgDateImgPadding.y();
	int32 statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
	int32 statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
	App::roundRect(p, rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, _width), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	p.setFont(st::normalFont);
	p.setPen(st::white);
	p.drawTextLeft(statusX, statusY, _width, _statusText, statusW - 2 * st::msgDateImgPadding.x());

	// date
	if (_caption.isEmpty()) {
		if (parent->getMedia() == this) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			parent->drawInfo(p, fullRight, fullBottom, 2 * skipx + width, selected, InfoDisplayOverImage);
		}
	} else {
		p.setPen(st::black);
		_caption.draw(p, st::msgPadding.left(), skipy + height + st::mediaPadding.bottom() + st::mediaCaptionSkip, captionw);
	}
}

void HistoryVideo::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	bool loaded = _data->loaded();

	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = parent->hasBubble();

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();
		if (!_caption.isEmpty()) {
			int32 captionw = width - st::msgPadding.left() - st::msgPadding.right();
			height -= _caption.countHeight(captionw) + st::msgPadding.bottom();
			if (x >= st::msgPadding.left() && y >= height && x < st::msgPadding.left() + captionw && y < _height) {
				bool inText = false;
				_caption.getState(lnk, inText, x - st::msgPadding.left(), y - height, captionw);
				state = inText ? HistoryInTextCursorState : HistoryDefaultCursorState;
			}
			height -= st::mediaCaptionSkip;
		}
		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height) {
		lnk = loaded ? _openl : (_data->loading() ? _cancell : _savel);
		if (_caption.isEmpty() && parent->getMedia() == this) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			bool inDate = parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
			if (inDate) {
				state = HistoryInDateCursorState;
			}
		}
		return;
	}
}

void HistoryVideo::setStatusSize(int32 newSize) const {
	HistoryFileMedia::setStatusSize(newSize, _data->size, _data->duration, 0);
}

const QString HistoryVideo::inDialogsText() const {
	return _caption.isEmpty() ? lang(lng_in_dlg_video) : _caption.original(0, 0xFFFF, Text::ExpandLinksNone);
}

const QString HistoryVideo::inHistoryText() const {
	return qsl("[ ") + lang(lng_in_dlg_video) + (_caption.isEmpty() ? QString() : (qsl(", ") + _caption.original(0, 0xFFFF, Text::ExpandLinksAll))) + qsl(" ]");
}

void HistoryVideo::updateStatusText(const HistoryItem *parent) const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (!_data->already().isEmpty()) {
		statusSize = FileStatusSizeLoaded;
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
	}
}

void HistoryVideo::regItem(HistoryItem *item) {
	App::regVideoItem(_data, item);
}

void HistoryVideo::unregItem(HistoryItem *item) {
	App::unregVideoItem(_data, item);
}

ImagePtr HistoryVideo::replyPreview() {
	if (_data->replyPreview->isNull() && !_data->thumb->isNull()) {
		if (_data->thumb->loaded()) {
			int w = _data->thumb->width(), h = _data->thumb->height();
			if (w <= 0) w = 1;
			if (h <= 0) h = 1;
			_data->replyPreview = ImagePtr(w > h ? _data->thumb->pix(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : _data->thumb->pix(st::msgReplyBarSize.height()), "PNG");
		} else {
			_data->thumb->load();
		}
	}
	return _data->replyPreview;
}

HistoryAudio::HistoryAudio(const MTPDaudio &audio) : HistoryFileMedia()
, _data(App::feedAudio(audio)) {
	setLinks(new AudioOpenLink(_data), new AudioOpenLink(_data), new AudioCancelLink(_data));

	setStatusSize(FileStatusSizeReady);
}

HistoryAudio::HistoryAudio(const HistoryAudio &other) : HistoryFileMedia()
, _data(other._data) {
	setLinks(new AudioOpenLink(_data), new AudioOpenLink(_data), new AudioCancelLink(_data));

	setStatusSize(other._statusSize);
}

void HistoryAudio::initDimensions(const HistoryItem *parent) {
	_maxw = st::msgFileMinWidth;

	int32 tleft = 0, tright = 0;

	tleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
	tright = st::msgFileThumbPadding.left();
	_maxw = qMax(_maxw, tleft + audioMaxStatusWidth(_data) + int(st::mediaUnreadSkip + st::mediaUnreadSize) + parent->skipBlockWidth() + st::msgPadding.right());

	_maxw = qMax(tleft + st::semiboldFont->width(lang(lng_media_audio)) + tright, _maxw);
	_maxw = qMin(_maxw, int(st::msgMaxWidth));

	_height = _minh = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
}

void HistoryAudio::draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	if (displayLoading) {
		ensureAnimation(parent);
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	bool showPause = updateStatusText(parent);
	bool radial = isRadialAnimation(ms);

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;

	nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
	nametop = st::msgFileNameTop;
	nameright = st::msgFilePadding.left();
	statustop = st::msgFileStatusTop;

	QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, _width));
	p.setPen(Qt::NoPen);
	if (selected) {
		p.setBrush(outbg ? st::msgFileOutBgSelected : st::msgFileInBgSelected);
	} else if (isThumbAnimation(ms)) {
		float64 over = _animation->a_thumbOver.current();
		p.setBrush(style::interpolate(outbg ? st::msgFileOutBg : st::msgFileInBg, outbg ? st::msgFileOutBgOver : st::msgFileInBgOver, over));
	} else {
		bool over = textlnkDrawOver(_data->loading() ? _cancell : _savel);
		p.setBrush(outbg ? (over ? st::msgFileOutBgOver : st::msgFileOutBg) : (over ? st::msgFileInBgOver : st::msgFileInBg));
	}

	p.setRenderHint(QPainter::HighQualityAntialiasing);
	p.drawEllipse(inner);
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);

	if (radial) {
		QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
		style::color bg(outbg ? (selected ? st::msgOutBgSelected : st::msgOutBg) : (selected ? st::msgInBgSelected : st::msgInBg));
		_animation->radial.draw(p, rinner, st::msgFileRadialLine, bg);
	}

	style::sprite icon;
	if (showPause) {
		icon = outbg ? (selected ? st::msgFileOutPauseSelected : st::msgFileOutPause) : (selected ? st::msgFileInPauseSelected : st::msgFileInPause);
	} else if (radial || _data->loading()) {
		icon = outbg ? (selected ? st::msgFileOutCancelSelected : st::msgFileOutCancel) : (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
	} else if (loaded) {
		icon = outbg ? (selected ? st::msgFileOutPlaySelected : st::msgFileOutPlay) : (selected ? st::msgFileInPlaySelected : st::msgFileInPlay);
	} else {
		icon = outbg ? (selected ? st::msgFileOutDownloadSelected : st::msgFileOutDownload) : (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
	}
	p.drawSpriteCenter(inner, icon);

	int32 namewidth = _width - nameleft - nameright;

	p.setFont(st::semiboldFont);
	p.setPen(st::black);
	p.drawTextLeft(nameleft, nametop, _width, lang(lng_media_audio));

	style::color status(outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg));
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(nameleft, statustop, _width, _statusText);

	if (parent->isMediaUnread()) {
		int32 w = st::normalFont->width(_statusText);
		if (w + st::mediaUnreadSkip + st::mediaUnreadSize <= namewidth) {
			p.setPen(Qt::NoPen);
			p.setBrush(outbg ? (selected ? st::msgFileOutBgSelected : st::msgFileOutBg) : (selected ? st::msgFileInBgSelected : st::msgFileInBg));

			p.setRenderHint(QPainter::HighQualityAntialiasing, true);
			p.drawEllipse(rtlrect(nameleft + w + st::mediaUnreadSkip, statustop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, _width));
			p.setRenderHint(QPainter::HighQualityAntialiasing, false);
		}
	}
}

void HistoryAudio::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;
	bool loaded = _data->loaded();

	bool showPause = updateStatusText(parent);

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;

	QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, _width));
	if ((_data->loading() || _data->status == FileUploading || !loaded) && inner.contains(x, y)) {
		lnk = (_data->loading() || _data->status == FileUploading) ? _cancell : _savel;
		return;
	}

	if (x >= 0 && y >= 0 && x < _width && y < _height && _data->access && !_data->loading()) {
		lnk = _openl;
		return;
	}
}

const QString HistoryAudio::inDialogsText() const {
	return lang(lng_in_dlg_audio);
}

const QString HistoryAudio::inHistoryText() const {
	return qsl("[ ") + lang(lng_in_dlg_audio) + qsl(" ]");
}

void HistoryAudio::regItem(HistoryItem *item) {
	App::regAudioItem(_data, item);
}

void HistoryAudio::unregItem(HistoryItem *item) {
	App::unregAudioItem(_data, item);
}

void HistoryAudio::updateFrom(const MTPMessageMedia &media, HistoryItem *parent) {
	if (media.type() == mtpc_messageMediaAudio) {
		App::feedAudio(media.c_messageMediaAudio().vaudio, _data);
		if (!_data->data().isEmpty()) {
			Local::writeAudio(mediaKey(AudioFileLocation, _data->dc, _data->id), _data->data());
		}
	}
}

void HistoryAudio::setStatusSize(int32 newSize, qint64 realDuration) const {
	HistoryFileMedia::setStatusSize(newSize, _data->size, _data->duration, realDuration);
}

bool HistoryAudio::updateStatusText(const HistoryItem *parent) const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		AudioMsgId playing;
		AudioPlayerState playingState = AudioPlayerStopped;
		int64 playingPosition = 0, playingDuration = 0;
		int32 playingFrequency = 0;
		if (audioPlayer()) {
			audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
		}

		if (playing.msgId == parent->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
			statusSize = -1 - (playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
			realDuration = playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency);
			showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
		} else {
			statusSize = FileStatusSizeLoaded;
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize, realDuration);
	}
	return showPause;
}

HistoryDocument::HistoryDocument(DocumentData *document, const QString &caption, const HistoryItem *parent) : HistoryFileMedia()
, _data(document)
, _linksavel(new DocumentSaveLink(_data))
, _linkcancell(new DocumentCancelLink(_data))
, _name(documentName(_data))
, _namew(st::semiboldFont->width(_name))
, _caption(st::msgFileMinWidth - st::msgPadding.left() - st::msgPadding.right()) {
	setLinks(new DocumentOpenLink(_data), new DocumentSaveLink(_data), new DocumentCancelLink(_data));

	setStatusSize(FileStatusSizeReady);

	if (!caption.isEmpty()) {
		_caption.setText(st::msgFont, caption + parent->skipBlock(), itemTextNoMonoOptions(parent));
	}
}

HistoryDocument::HistoryDocument(const HistoryDocument &other) : HistoryFileMedia()
, _data(other._data)
, _linksavel(new DocumentSaveLink(_data))
, _linkcancell(new DocumentCancelLink(_data))
, _name(other._name)
, _namew(other._namew)
, _thumbw(other._thumbw)
, _caption(other._caption) {
	setLinks(new DocumentOpenLink(_data), new DocumentSaveLink(_data), new DocumentCancelLink(_data));

	setStatusSize(other._statusSize);
}

void HistoryDocument::initDimensions(const HistoryItem *parent) {
	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(parent->skipBlockWidth(), parent->skipBlockHeight());
	}

	if (withThumb()) {
		_data->thumb->load();
		int32 tw = _data->thumb->width(), th = _data->thumb->height();
		if (tw > th) {
			_thumbw = (tw * st::msgFileThumbSize) / th;
		} else {
			_thumbw = st::msgFileThumbSize;
		}
	} else {
		_thumbw = 0;
	}

	_maxw = st::msgFileMinWidth;

	int32 tleft = 0, tright = 0;
	bool wthumb = withThumb();
	if (wthumb) {
		tleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		tright = st::msgFileThumbPadding.left();
		_maxw = qMax(_maxw, tleft + documentMaxStatusWidth(_data) + tright);
	} else {
		tleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		tright = st::msgFileThumbPadding.left();
		_maxw = qMax(_maxw, tleft + documentMaxStatusWidth(_data) + parent->skipBlockWidth() + st::msgPadding.right());
	}

	_maxw = qMax(tleft + _namew + tright, _maxw);
	_maxw = qMin(_maxw, int(st::msgMaxWidth));

	if (wthumb) {
		_minh = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
	} else {
		_minh = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}

	if (_caption.isEmpty()) {
		_height = _minh;
	} else {
		_minh += _caption.countHeight(_maxw - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();
	}
}

int32 HistoryDocument::resize(int32 width, const HistoryItem *parent) {
	if (_caption.isEmpty()) {
		return HistoryFileMedia::resize(width, parent);
	}

	_width = qMin(width, _maxw);
	bool wthumb = withThumb();
	if (wthumb) {
		_height = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
	} else {
		_height = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	_height += _caption.countHeight(_width - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();

	return _height;
}

void HistoryDocument::draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();

	int32 captionw = _width - st::msgPadding.left() - st::msgPadding.right();

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	if (displayLoading) {
		ensureAnimation(parent);
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	bool showPause = updateStatusText(parent);
	bool radial = isRadialAnimation(ms);

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0, bottom = 0;
	bool wthumb = withThumb();
	if (wthumb) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		nametop = st::msgFileThumbNameTop;
		nameright = st::msgFileThumbPadding.left();
		statustop = st::msgFileThumbStatusTop;
		linktop = st::msgFileThumbLinkTop;
		bottom = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();

		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top(), st::msgFileThumbSize, st::msgFileThumbSize, _width));
		QPixmap thumb = loaded ? _data->thumb->pixSingle(_thumbw, 0, st::msgFileThumbSize, st::msgFileThumbSize) : _data->thumb->pixBlurredSingle(_thumbw, 0, st::msgFileThumbSize, st::msgFileThumbSize);
		p.drawPixmap(rthumb.topLeft(), thumb);
		if (selected) {
			App::roundRect(p, rthumb, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
		}

		if (radial || (!loaded && !_data->loading())) {
            float64 radialOpacity = (radial && loaded && !_data->uploading()) ? _animation->radial.opacity() : 1;
			QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
			p.setPen(Qt::NoPen);
			if (selected) {
				p.setBrush(st::msgDateImgBgSelected);
			} else if (isThumbAnimation(ms)) {
				float64 over = _animation->a_thumbOver.current();
				p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
				p.setBrush(st::black);
			} else {
				bool over = textlnkDrawOver(_data->loading() ? _cancell : _savel);
				p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
			}
			p.setOpacity(radialOpacity * p.opacity());

			p.setRenderHint(QPainter::HighQualityAntialiasing);
			p.drawEllipse(inner);
			p.setRenderHint(QPainter::HighQualityAntialiasing, false);

			p.setOpacity(radialOpacity);
			style::sprite icon;
			if (radial || _data->loading()) {
				icon = (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
			} else {
				icon = (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
			}
			p.setOpacity((radial && loaded) ? _animation->radial.opacity() : 1);
			p.drawSpriteCenter(inner, icon);
			if (radial) {
				p.setOpacity(1);

				QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
				_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::msgInBgSelected : st::msgInBg);
			}
		}

		if (_data->status != FileUploadFailed) {
			const TextLinkPtr &lnk((_data->loading() || _data->status == FileUploading) ? _linkcancell : _linksavel);
			bool over = textlnkDrawOver(lnk);
			p.setFont(over ? st::semiboldFont->underline() : st::semiboldFont);
			p.setPen(outbg ? (selected ? st::msgFileThumbLinkOutFgSelected : st::msgFileThumbLinkOutFg) : (selected ? st::msgFileThumbLinkInFgSelected : st::msgFileThumbLinkInFg));
			p.drawTextLeft(nameleft, linktop, _width, _link, _linkw);
		}
	} else {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nametop = st::msgFileNameTop;
		nameright = st::msgFilePadding.left();
		statustop = st::msgFileStatusTop;
		bottom = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();

		QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, _width));
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(outbg ? st::msgFileOutBgSelected : st::msgFileInBgSelected);
		} else if (isThumbAnimation(ms)) {
			float64 over = _animation->a_thumbOver.current();
			p.setBrush(style::interpolate(outbg ? st::msgFileOutBg : st::msgFileInBg, outbg ? st::msgFileOutBgOver : st::msgFileInBgOver, over));
		} else {
			bool over = textlnkDrawOver(_data->loading() ? _cancell : _savel);
			p.setBrush(outbg ? (over ? st::msgFileOutBgOver : st::msgFileOutBg) : (over ? st::msgFileInBgOver : st::msgFileInBg));
		}

		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.drawEllipse(inner);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);

		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			style::color bg(outbg ? (selected ? st::msgOutBgSelected : st::msgOutBg) : (selected ? st::msgInBgSelected : st::msgInBg));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, bg);
		}

		style::sprite icon;
		if (showPause) {
			icon = outbg ? (selected ? st::msgFileOutPauseSelected : st::msgFileOutPause) : (selected ? st::msgFileInPauseSelected : st::msgFileInPause);
		} else if (radial || _data->loading()) {
			icon = outbg ? (selected ? st::msgFileOutCancelSelected : st::msgFileOutCancel) : (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
		} else if (loaded) {
			if (_data->song()) {
				icon = outbg ? (selected ? st::msgFileOutPlaySelected : st::msgFileOutPlay) : (selected ? st::msgFileInPlaySelected : st::msgFileInPlay);
			} else if (_data->isImage()) {
				icon = outbg ? (selected ? st::msgFileOutImageSelected : st::msgFileOutImage) : (selected ? st::msgFileInImageSelected : st::msgFileInImage);
			} else {
				icon = outbg ? (selected ? st::msgFileOutFileSelected : st::msgFileOutFile) : (selected ? st::msgFileInFileSelected : st::msgFileInFile);
			}
		} else {
			icon = outbg ? (selected ? st::msgFileOutDownloadSelected : st::msgFileOutDownload) : (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
		}
		p.drawSpriteCenter(inner, icon);
	}
	int32 namewidth = _width - nameleft - nameright;

	p.setFont(st::semiboldFont);
	p.setPen(st::black);
	if (namewidth < _namew) {
		p.drawTextLeft(nameleft, nametop, _width, st::semiboldFont->elided(_name, namewidth));
	} else {
		p.drawTextLeft(nameleft, nametop, _width, _name, _namew);
	}

	style::color status(outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg));
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(nameleft, statustop, _width, _statusText);

	if (!_caption.isEmpty()) {
		p.setPen(st::black);
		_caption.draw(p, st::msgPadding.left(), bottom, captionw);
	}
}

void HistoryDocument::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;
	bool loaded = _data->loaded();

	bool showPause = updateStatusText(parent);

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0, bottom = 0;
	bool wthumb = withThumb();
	if (wthumb) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		linktop = st::msgFileThumbLinkTop;
		bottom = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();

		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top(), st::msgFileThumbSize, st::msgFileThumbSize, _width));

		if ((_data->loading() || _data->uploading() || !loaded) && rthumb.contains(x, y)) {
			lnk = (_data->loading() || _data->uploading()) ? _cancell : _savel;
			return;
		}

		if (_data->status != FileUploadFailed) {
			if (rtlrect(nameleft, linktop, _linkw, st::semiboldFont->height, _width).contains(x, y)) {
				lnk = (_data->loading() || _data->uploading()) ? _linkcancell : _linksavel;
				return;
			}
		}
	} else {
		bottom = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();

		QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, _width));
		if ((_data->loading() || _data->uploading() || !loaded) && inner.contains(x, y)) {
			lnk = (_data->loading() || _data->uploading()) ? _cancell : _savel;
			return;
		}
	}

	int32 height = _height;
	if (!_caption.isEmpty()) {
		if (y >= bottom) {
			bool inText = false;
			_caption.getState(lnk, inText, x - st::msgPadding.left(), y - bottom, _width - st::msgPadding.left() - st::msgPadding.right());
			state = inText ? HistoryInTextCursorState : HistoryDefaultCursorState;
			return;
		}
		height -= _caption.countHeight(_width - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();
	}
	if (x >= 0 && y >= 0 && x < _width && y < height && !_data->loading() && !_data->uploading() && _data->access) {
		lnk = _openl;
		return;
	}
}

const QString HistoryDocument::inDialogsText() const {
	return (_name.isEmpty() ? lang(lng_in_dlg_file) : _name) + (_caption.isEmpty() ? QString() : (' ' + _caption.original(0, 0xFFFF, Text::ExpandLinksNone)));
}

const QString HistoryDocument::inHistoryText() const {
	return qsl("[ ") + lang(lng_in_dlg_file) + (_name.isEmpty() ? QString() : (qsl(" : ") + _name)) + (_caption.isEmpty() ? QString() : (qsl(", ") + _caption.original(0, 0xFFFF, Text::ExpandLinksAll))) + qsl(" ]");
}

void HistoryDocument::setStatusSize(int32 newSize, qint64 realDuration) const {
	HistoryFileMedia::setStatusSize(newSize, _data->size, _data->song() ? _data->song()->duration : -1, realDuration);

	if (_statusSize == FileStatusSizeReady) {
		_link = lang(lng_media_download).toUpper();
	} else if (_statusSize == FileStatusSizeLoaded) {
		_link = lang(lng_media_open_with).toUpper();
	} else if (_statusSize == FileStatusSizeFailed) {
		_link = lang(lng_media_download).toUpper();
	} else if (_statusSize >= 0) {
		_link = lang(lng_media_cancel).toUpper();
	} else {
		_link = lang(lng_media_open_with).toUpper();
	}
	_linkw = st::semiboldFont->width(_link);
}

bool HistoryDocument::updateStatusText(const HistoryItem *parent) const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		if (_data->song()) {
			SongMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			int64 playingPosition = 0, playingDuration = 0;
			int32 playingFrequency = 0;
			if (audioPlayer()) {
				audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
			}

			if (playing.msgId == parent->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				statusSize = -1 - (playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
				realDuration = playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency);
				showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
			} else {
				statusSize = FileStatusSizeLoaded;
			}
			if (!showPause && playing.msgId == parent->fullId() && App::main() && App::main()->player()->seekingSong(playing)) {
				showPause = true;
			}
		} else {
			statusSize = FileStatusSizeLoaded;
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize, realDuration);
	}
	return showPause;
}

void HistoryDocument::regItem(HistoryItem *item) {
	App::regDocumentItem(_data, item);
}

void HistoryDocument::unregItem(HistoryItem *item) {
	App::unregDocumentItem(_data, item);
}

void HistoryDocument::updateFrom(const MTPMessageMedia &media, HistoryItem *parent) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, _data);
	}
}

ImagePtr HistoryDocument::replyPreview() {
	return _data->makeReplyPreview();
}

HistoryGif::HistoryGif(DocumentData *document, const QString &caption, const HistoryItem *parent) : HistoryFileMedia()
, _data(document)
, _thumbw(1)
, _thumbh(1)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right())
, _gif(0) {
	setLinks(new GifOpenLink(_data), new GifOpenLink(_data), new DocumentCancelLink(_data));

	setStatusSize(FileStatusSizeReady);

	if (!caption.isEmpty()) {
		_caption.setText(st::msgFont, caption + parent->skipBlock(), itemTextNoMonoOptions(parent));
	}

	_data->thumb->load();
}

HistoryGif::HistoryGif(const HistoryGif &other) : HistoryFileMedia()
, _parent(0)
, _data(other._data)
, _thumbw(other._thumbw)
, _thumbh(other._thumbh)
, _caption(other._caption)
, _gif(0) {
	setLinks(new GifOpenLink(_data), new GifOpenLink(_data), new DocumentCancelLink(_data));

	setStatusSize(other._statusSize);
}

void HistoryGif::initDimensions(const HistoryItem *parent) {
	_parent = parent;
	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(parent->skipBlockWidth(), parent->skipBlockHeight());
	}

	bool bubble = parent->hasBubble();
	int32 tw = 0, th = 0;
	if (gif() && _gif->state() == ClipError) {
		if (!_gif->autoplay()) {
			Ui::showLayer(new InformBox(lang(lng_gif_error)));
		}
		App::unregGifItem(_gif);
		delete _gif;
		_gif = BadClipReader;
	}

	if (gif() && _gif->ready()) {
		tw = convertScale(_gif->width());
		th = convertScale(_gif->height());
	} else {
		tw = convertScale(_data->dimensions.width()), th = convertScale(_data->dimensions.height());
		if (!tw || !th) {
			tw = convertScale(_data->thumb->width());
			th = convertScale(_data->thumb->height());
		}
	}
	if (tw > st::maxGifSize) {
		th = (st::maxGifSize * th) / tw;
		tw = st::maxGifSize;
	}
	if (th > st::maxGifSize) {
		tw = (st::maxGifSize * tw) / th;
		th = st::maxGifSize;
	}
	if (!tw || !th) {
		tw = th = 1;
	}
	_thumbw = tw;
	_thumbh = th;
	_maxw = qMax(tw, int32(st::minPhotoSize));
	_minh = qMax(th, int32(st::minPhotoSize));
	if (!gif() || !_gif->ready()) {
		_maxw = qMax(_maxw, parent->infoWidth() + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
		_maxw = qMax(_maxw, gifMaxStatusWidth(_data) + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (bubble) {
		_maxw += st::mediaPadding.left() + st::mediaPadding.right();
		_minh += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			_minh += st::mediaCaptionSkip + _caption.countHeight(_maxw - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();
		}
	}
}

int32 HistoryGif::resize(int32 width, const HistoryItem *parent) {
	bool bubble = parent->hasBubble();

	int32 tw = 0, th = 0;
	if (gif() && _gif->ready()) {
		tw = convertScale(_gif->width());
		th = convertScale(_gif->height());
	} else {
		tw = convertScale(_data->dimensions.width()), th = convertScale(_data->dimensions.height());
		if (!tw || !th) {
			tw = convertScale(_data->thumb->width());
			th = convertScale(_data->thumb->height());
		}
	}
	if (tw > st::maxGifSize) {
		th = (st::maxGifSize * th) / tw;
		tw = st::maxGifSize;
	}
	if (th > st::maxGifSize) {
		tw = (st::maxGifSize * tw) / th;
		th = st::maxGifSize;
	}
	if (!tw || !th) {
		tw = th = 1;
	}

	if (bubble) {
		width -= st::mediaPadding.left() + st::mediaPadding.right();
	}
	if (width < tw) {
		th = qRound((width / float64(tw)) * th);
		tw = width;
	}
	_thumbw = tw;
	_thumbh = th;

	_width = qMax(tw, int32(st::minPhotoSize));
	_height = qMax(th, int32(st::minPhotoSize));
	if (gif() && _gif->ready()) {
		if (!_gif->started()) {
			_gif->start(_thumbw, _thumbh, _width, _height, true);
		}
	} else {
		_width = qMax(_width, parent->infoWidth() + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
		_width = qMax(_width, gifMaxStatusWidth(_data) + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (bubble) {
		_width += st::mediaPadding.left() + st::mediaPadding.right();
		_height += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			_height += st::mediaCaptionSkip + _caption.countHeight(_width - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();
		}
	}

	return _height;
}

void HistoryGif::draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(parent);
	bool loaded = _data->loaded(), displayLoading = (parent->id < 0) || _data->displayLoading();
	if (loaded && !gif() && _gif != BadClipReader && cAutoPlayGif()) {
		const_cast<HistoryGif*>(this)->playInline(const_cast<HistoryItem*>(parent));
		if (gif()) _gif->setAutoplay();
	}

	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = parent->hasBubble();
	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	int32 captionw = width - st::msgPadding.left() - st::msgPadding.right();

	bool animating = (gif() && _gif->started());

	if (!animating || parent->id < 0) {
		if (displayLoading) {
			ensureAnimation(parent);
			if (!_animation->radial.animating()) {
				_animation->radial.start(dataProgress());
			}
		}
		updateStatusText(parent);
	}
	bool radial = isRadialAnimation(ms);

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			height -= st::mediaCaptionSkip + _caption.countHeight(captionw) + st::msgPadding.bottom();
		}
	} else {
		App::roundShadow(p, 0, 0, width, _height, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	QRect rthumb(rtlrect(skipx, skipy, width, height, _width));

	if (animating) {
		p.drawPixmap(rthumb.topLeft(), _gif->current(_thumbw, _thumbh, width, height, (Ui::isLayerShown() || Ui::isMediaViewShown() || Ui::isInlineItemBeingChosen()) ? 0 : ms));
	} else {
		p.drawPixmap(rthumb.topLeft(), _data->thumb->pixBlurredSingle(_thumbw, _thumbh, width, height));
	}
	if (selected) {
		App::roundRect(p, rthumb, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	if (radial || (!_gif && ((!loaded && !_data->loading()) || !cAutoPlayGif())) || (_gif == BadClipReader)) {
        float64 radialOpacity = (radial && loaded && parent->id > 0) ? _animation->radial.opacity() : 1;
		QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else if (isThumbAnimation(ms)) {
			float64 over = _animation->a_thumbOver.current();
			p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
			p.setBrush(st::black);
		} else {
			bool over = textlnkDrawOver(_data->loading() ? _cancell : _savel);
			p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}
		p.setOpacity(radialOpacity * p.opacity());

		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.drawEllipse(inner);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);

		p.setOpacity(radialOpacity);
		style::sprite icon;
		if (_data->loaded() && !radial) {
			icon = (selected ? st::msgFileInPlaySelected : st::msgFileInPlay);
		} else if (radial || _data->loading()) {
			if (parent->id > 0 || _data->uploading()) {
				icon = (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
			}
		} else {
			icon = (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
		}
		if (!icon.isEmpty()) {
			p.drawSpriteCenter(inner, icon);
		}
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::msgInBgSelected : st::msgInBg);
		}

		if (!animating || parent->id < 0) {
			int32 statusX = skipx + st::msgDateImgDelta + st::msgDateImgPadding.x(), statusY = skipy + st::msgDateImgDelta + st::msgDateImgPadding.y();
			int32 statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
			int32 statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
			App::roundRect(p, rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, _width), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
			p.setFont(st::normalFont);
			p.setPen(st::white);
			p.drawTextLeft(statusX, statusY, _width, _statusText, statusW - 2 * st::msgDateImgPadding.x());
		}
	}

	if (!_caption.isEmpty()) {
		p.setPen(st::black);
		_caption.draw(p, st::msgPadding.left(), skipy + height + st::mediaPadding.bottom() + st::mediaCaptionSkip, captionw);
	} else if (parent->getMedia() == this && (_data->uploading() || App::hoveredItem() == parent)) {
		int32 fullRight = skipx + width, fullBottom = skipy + height;
		parent->drawInfo(p, fullRight, fullBottom, 2 * skipx + width, selected, InfoDisplayOverImage);
	}
}

void HistoryGif::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = parent->hasBubble();

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();
		if (!_caption.isEmpty()) {
			int32 captionw = width - st::msgPadding.left() - st::msgPadding.right();
			height -= _caption.countHeight(captionw) + st::msgPadding.bottom();
			if (x >= st::msgPadding.left() && y >= height && x < st::msgPadding.left() + captionw && y < _height) {
				bool inText = false;
				_caption.getState(lnk, inText, x - st::msgPadding.left(), y - height, captionw);
				state = inText ? HistoryInTextCursorState : HistoryDefaultCursorState;
				return;
			}
			height -= st::mediaCaptionSkip;
		}
		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height) {
		if (_data->uploading()) {
			lnk = _cancell;
		} else if (!gif() || !cAutoPlayGif()) {
			lnk = _data->loaded() ? _openl : (_data->loading() ? _cancell : _savel);
		}
		if (parent->getMedia() == this) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			bool inDate = parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
			if (inDate) {
				state = HistoryInDateCursorState;
			}
		}
		return;
	}
}

const QString HistoryGif::inDialogsText() const {
	return qsl("GIF") + (_caption.isEmpty() ? QString() : (' ' + _caption.original(0, 0xFFFF, Text::ExpandLinksNone)));
}

const QString HistoryGif::inHistoryText() const {
	return qsl("[ GIF ") + (_caption.isEmpty() ? QString() : (_caption.original(0, 0xFFFF, Text::ExpandLinksAll) + ' ')) + qsl(" ]");
}

void HistoryGif::setStatusSize(int32 newSize) const {
	HistoryFileMedia::setStatusSize(newSize, _data->size, -2, 0);
}

void HistoryGif::updateStatusText(const HistoryItem *parent) const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		statusSize = FileStatusSizeLoaded;
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
	}
}

void HistoryGif::regItem(HistoryItem *item) {
	App::regDocumentItem(_data, item);
}

void HistoryGif::unregItem(HistoryItem *item) {
	App::unregDocumentItem(_data, item);
}

void HistoryGif::updateFrom(const MTPMessageMedia &media, HistoryItem *parent) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, _data);
	}
}

ImagePtr HistoryGif::replyPreview() {
	return _data->makeReplyPreview();
}

bool HistoryGif::playInline(HistoryItem *parent) {
	if (gif()) {
		stopInline(parent);
	} else {
		if (!cAutoPlayGif()) {
			App::stopGifItems();
		}
		_gif = new ClipReader(_data->location(), _data->data(), func(parent, &HistoryItem::clipCallback));
		App::regGifItem(_gif, parent);
	}
	return true;
}

void HistoryGif::stopInline(HistoryItem *parent) {
	if (gif()) {
		App::unregGifItem(_gif);
		delete _gif;
		_gif = 0;
	}

	parent->initDimensions();
	Notify::historyItemResized(parent);
	Notify::historyItemLayoutChanged(parent);
}

HistoryGif::~HistoryGif() {
	if (gif()) {
		App::unregGifItem(_gif);
		deleteAndMark(_gif);
	}
}

float64 HistoryGif::dataProgress() const {
	return (_data->uploading() || !_parent || _parent->id > 0) ? _data->progress() : 0;
}

bool HistoryGif::dataFinished() const {
	return (!_parent || _parent->id > 0) ? (!_data->loading() && !_data->uploading()) : false;
}

bool HistoryGif::dataLoaded() const {
	return (!_parent || _parent->id > 0) ? _data->loaded() : false;
}

HistorySticker::HistorySticker(DocumentData *document) : HistoryMedia()
, _pixw(1)
, _pixh(1)
, _data(document)
, _emoji(_data->sticker()->alt) {
	_data->thumb->load();
	if (EmojiPtr e = emojiFromText(_emoji)) {
		_emoji = emojiString(e);
	}
}

void HistorySticker::initDimensions(const HistoryItem *parent) {
	_pixw = _data->dimensions.width();
	_pixh = _data->dimensions.height();
	if (_pixw > st::maxStickerSize) {
		_pixh = (st::maxStickerSize * _pixh) / _pixw;
		_pixw = st::maxStickerSize;
	}
	if (_pixh > st::maxStickerSize) {
		_pixw = (st::maxStickerSize * _pixw) / _pixh;
		_pixh = st::maxStickerSize;
	}
	if (_pixw < 1) _pixw = 1;
	if (_pixh < 1) _pixh = 1;
	_maxw = qMax(_pixw, int16(st::minPhotoSize));
	_minh = qMax(_pixh, int16(st::minPhotoSize));
	if (const HistoryReply *reply = toHistoryReply(parent)) {
		_maxw += st::msgReplyPadding.left() + reply->replyToWidth();
	}
	_height = _minh;
}

int32 HistorySticker::resize(int32 width, const HistoryItem *parent) { // return new height
	_width = qMin(width, _maxw);
	if (const HistoryReply *reply = toHistoryReply(parent)) {
		int32 usew = _maxw - st::msgReplyPadding.left() - reply->replyToWidth();
		int32 rw = _width - usew - st::msgReplyPadding.left() - st::msgReplyPadding.left() - st::msgReplyPadding.right();
		reply->resizeVia(rw);
	}
	return _height;
}

void HistorySticker::draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->checkSticker();
	bool loaded = _data->loaded();

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel, hovered, pressed;

	int32 usew = _maxw, usex = 0;
	const HistoryReply *reply = toHistoryReply(parent);
	if (reply) {
		usew -= st::msgReplyPadding.left() + reply->replyToWidth();
		if (fromChannel) {
		} else if (out) {
			usex = _width - usew;
		}
	}
	if (rtl()) usex = _width - usex - usew;

	if (selected) {
		if (_data->sticker()->img->isNull()) {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (_minh - _pixh) / 2), _data->thumb->pixBlurredColored(st::msgStickerOverlay, _pixw, _pixh));
		} else {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (_minh - _pixh) / 2), _data->sticker()->img->pixColored(st::msgStickerOverlay, _pixw, _pixh));
		}
	} else {
		if (_data->sticker()->img->isNull()) {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (_minh - _pixh) / 2), _data->thumb->pixBlurred(_pixw, _pixh));
		} else {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (_minh - _pixh) / 2), _data->sticker()->img->pix(_pixw, _pixh));
		}
	}

	if (parent->getMedia() == this) {
		parent->drawInfo(p, usex + usew, _height, usex * 2 + usew, selected, InfoDisplayOverImage);

		if (reply) {
			int32 rw = _width - usew - st::msgReplyPadding.left(), rh = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
			int32 rx = fromChannel ? (usew + st::msgReplyPadding.left()) : (out ? 0 : (usew + st::msgReplyPadding.left())), ry = _height - rh;
			if (rtl()) rx = _width - rx - rw;

			App::roundRect(p, rx, ry, rw, rh, selected ? App::msgServiceSelectBg() : App::msgServiceBg(), selected ? ServiceSelectedCorners : ServiceCorners);

			reply->drawReplyTo(p, rx + st::msgReplyPadding.left(), ry, rw - st::msgReplyPadding.left() - st::msgReplyPadding.right(), selected, true);
		}
	}
}

void HistorySticker::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	int32 usew = _maxw, usex = 0;
	const HistoryReply *reply = toHistoryReply(parent);
	if (reply) {
		usew -= reply->replyToWidth();
		if (fromChannel) {
		} else if (out) {
			usex = _width - usew;
		}
	}
	if (rtl()) usex = _width - usex - usew;
	if (reply) {
		int32 rw = _width - usew, rh = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		int32 rx = fromChannel ? (usew + st::msgReplyPadding.left()) : (out ? 0 : (usew + st::msgReplyPadding.left())), ry = _height - rh;
		if (rtl()) rx = _width - rx - rw;
		if (x >= rx && y >= ry && x < rx + rw && y < ry + rh) {
			lnk = reply->replyToLink();
			return;
		}
	}
	if (parent->getMedia() == this) {
		bool inDate = parent->pointInTime(usex + usew, _height, x, y, InfoDisplayOverImage);
		if (inDate) {
			state = HistoryInDateCursorState;
		}
	}
}

const QString HistorySticker::inDialogsText() const {
	return _emoji.isEmpty() ? lang(lng_in_dlg_sticker) : lng_in_dlg_sticker_emoji(lt_emoji, _emoji);
}

const QString HistorySticker::inHistoryText() const {
	return qsl("[ ") + inDialogsText() + qsl(" ]");
}

void HistorySticker::regItem(HistoryItem *item) {
	App::regDocumentItem(_data, item);
}

void HistorySticker::unregItem(HistoryItem *item) {
	App::unregDocumentItem(_data, item);
}

void HistorySticker::updateFrom(const MTPMessageMedia &media, HistoryItem *parent) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, _data);
		if (!_data->data().isEmpty()) {
			Local::writeStickerImage(mediaKey(DocumentFileLocation, _data->dc, _data->id), _data->data());
		}
	}
}

void SendMessageLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton) {
		Ui::showPeerHistory(peer()->id, ShowAtUnreadMsgId);
	}
}

void AddContactLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton) {
		if (HistoryItem *item = App::histItemById(peerToChannel(peer()), msgid())) {
			if (HistoryMedia *media = item->getMedia()) {
				if (media->type() == MediaTypeContact) {
					QString fname = static_cast<HistoryContact*>(media)->fname();
					QString lname = static_cast<HistoryContact*>(media)->lname();
					QString phone = static_cast<HistoryContact*>(media)->phone();
					Ui::showLayer(new AddContactBox(fname, lname, phone));
				}
			}
		}
	}
}

HistoryContact::HistoryContact(int32 userId, const QString &first, const QString &last, const QString &phone) : HistoryMedia()
, _userId(userId)
, _contact(0)
, _phonew(0)
, _fname(first)
, _lname(last)
, _phone(App::formatPhone(phone))
, _linkw(0) {
	_name.setText(st::semiboldFont, lng_full_name(lt_first_name, first, lt_last_name, last).trimmed(), _textNameOptions);

	_phonew = st::normalFont->width(_phone);
}

void HistoryContact::initDimensions(const HistoryItem *parent) {
	_maxw = st::msgFileMinWidth;

	_contact = _userId ? App::userLoaded(_userId) : 0;
	if (_contact) {
		_contact->photo->load();
	}
	if (_contact && _contact->contact > 0) {
		_linkl.reset(new SendMessageLink(_contact));
		_link = lang(lng_profile_send_message).toUpper();
	} else if (_userId) {
		_linkl.reset(new AddContactLink(parent->history()->peer->id, parent->id));
		_link = lang(lng_profile_add_contact).toUpper();
	}
	_linkw = _link.isEmpty() ? 0 : st::semiboldFont->width(_link);

	int32 tleft = 0, tright = 0;
	if (_userId) {
		tleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		tright = st::msgFileThumbPadding.left();
		_maxw = qMax(_maxw, tleft + _phonew + tright);
	} else {
		tleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		tright = st::msgFileThumbPadding.left();
		_maxw = qMax(_maxw, tleft + _phonew + parent->skipBlockWidth() + st::msgPadding.right());
	}

	_maxw = qMax(tleft + _name.maxWidth() + tright, _maxw);
	_maxw = qMin(_maxw, int(st::msgMaxWidth));

	if (_userId) {
		_minh = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
	} else {
		_minh = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	_height = _minh;
}

void HistoryContact::draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	if (width >= _maxw) {
		width = _maxw;
	}

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	if (_userId) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		nametop = st::msgFileThumbNameTop;
		nameright = st::msgFileThumbPadding.left();
		statustop = st::msgFileThumbStatusTop;
		linktop = st::msgFileThumbLinkTop;

		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top(), st::msgFileThumbSize, st::msgFileThumbSize, width));
		if (_contact && _contact->photo->loaded()) {
			QPixmap thumb = _contact->photo->pixRounded(st::msgFileThumbSize, st::msgFileThumbSize);
			p.drawPixmap(rthumb.topLeft(), thumb);
		} else {
			p.drawPixmap(rthumb.topLeft(), userDefPhoto(_contact ? _contact->colorIndex : (qAbs(_userId) % UserColorsCount))->pixRounded(st::msgFileThumbSize, st::msgFileThumbSize));
		}
		if (selected) {
			App::roundRect(p, rthumb, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
		}

		bool over = textlnkDrawOver(_linkl);
		p.setFont(over ? st::semiboldFont->underline() : st::semiboldFont);
		p.setPen(outbg ? (selected ? st::msgFileThumbLinkOutFgSelected : st::msgFileThumbLinkOutFg) : (selected ? st::msgFileThumbLinkInFgSelected : st::msgFileThumbLinkInFg));
		p.drawTextLeft(nameleft, linktop, width, _link, _linkw);
	} else {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nametop = st::msgFileNameTop;
		nameright = st::msgFilePadding.left();
		statustop = st::msgFileStatusTop;

		QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, width));
		p.drawPixmap(inner.topLeft(), userDefPhoto(qAbs(parent->id) % UserColorsCount)->pixRounded(st::msgFileSize, st::msgFileSize));
	}
	int32 namewidth = width - nameleft - nameright;

	p.setFont(st::semiboldFont);
	p.setPen(st::black);
	_name.drawLeftElided(p, nameleft, nametop, namewidth, width);

	style::color status(outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg));
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(nameleft, statustop, width, _phone);
}

void HistoryContact::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const {
	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	if (_userId) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		linktop = st::msgFileThumbLinkTop;
		if (rtlrect(nameleft, linktop, _linkw, st::semiboldFont->height, _width).contains(x, y)) {
			lnk = _linkl;
			return;
		}
	}
	if (x >= 0 && y >= 0 && x < _width && y < _height && _contact) {
		lnk = _contact->lnk;
		return;
	}
}

const QString HistoryContact::inDialogsText() const {
	return lang(lng_in_dlg_contact);
}

const QString HistoryContact::inHistoryText() const {
	return qsl("[ ") + lang(lng_in_dlg_contact) + qsl(" : ") + _name.original() + qsl(", ") + _phone + qsl(" ]");
}

void HistoryContact::regItem(HistoryItem *item) {
	if (_userId) {
		App::regSharedContactItem(_userId, item);
	}
}

void HistoryContact::unregItem(HistoryItem *item) {
	if (_userId) {
		App::unregSharedContactItem(_userId, item);
	}
}

void HistoryContact::updateFrom(const MTPMessageMedia &media, HistoryItem *parent) {
	if (media.type() == mtpc_messageMediaContact) {
		if (_userId != media.c_messageMediaContact().vuser_id.v) {
			unregItem(parent);
			_userId = media.c_messageMediaContact().vuser_id.v;
			regItem(parent);
		}
	}
}

namespace {
	QString siteNameFromUrl(const QString &url) {
		QUrl u(url);
		QString pretty = u.isValid() ? u.toDisplayString() : url;
		QRegularExpressionMatch m = QRegularExpression(qsl("^[a-zA-Z0-9]+://")).match(pretty);
		if (m.hasMatch()) pretty = pretty.mid(m.capturedLength());
		int32 slash = pretty.indexOf('/');
		if (slash > 0) pretty = pretty.mid(0, slash);
		QStringList components = pretty.split('.', QString::SkipEmptyParts);
		if (components.size() >= 2) {
			components = components.mid(components.size() - 2);
			return components.at(0).at(0).toUpper() + components.at(0).mid(1) + '.' + components.at(1);
		}
		return QString();
	}

	int32 articleThumbWidth(PhotoData *thumb, int32 height) {
		int32 w = thumb->medium->width(), h = thumb->medium->height();
		return qMax(qMin(height * w / h, height), 1);
	}

	int32 articleThumbHeight(PhotoData *thumb, int32 width) {
		return qMax(thumb->medium->height() * width / thumb->medium->width(), 1);
	}

	int32 _lineHeight = 0;
}

HistoryWebPage::HistoryWebPage(WebPageData *data) : HistoryMedia()
, _data(data)
, _openl(0)
, _attach(0)
, _asArticle(false)
, _title(st::msgMinWidth - st::webPageLeft)
, _description(st::msgMinWidth - st::webPageLeft)
, _siteNameWidth(0)
, _durationWidth(0)
, _pixw(0)
, _pixh(0) {
}

HistoryWebPage::HistoryWebPage(const HistoryWebPage &other) : HistoryMedia()
, _data(other._data)
, _openl(0)
, _attach(other._attach ? other._attach->clone() : 0)
, _asArticle(other._asArticle)
, _title(other._title)
, _description(other._description)
, _siteNameWidth(other._siteNameWidth)
, _durationWidth(other._durationWidth)
, _pixw(other._pixw)
, _pixh(other._pixh) {
}

void HistoryWebPage::initDimensions(const HistoryItem *parent) {
	if (_data->pendingTill) {
		_maxw = _minh = _height = 0;
		return;
	}

	if (!_lineHeight) _lineHeight = qMax(st::webPageTitleFont->height, st::webPageDescriptionFont->height);

	if (!_openl && !_data->url.isEmpty()) _openl = TextLinkPtr(new TextLink(_data->url));

	// init layout
	QString title(_data->title.isEmpty() ? _data->author : _data->title);
	if (!_data->description.isEmpty() && title.isEmpty() && _data->siteName.isEmpty() && !_data->url.isEmpty()) {
		_data->siteName = siteNameFromUrl(_data->url);
	}
	if (!_data->doc && _data->photo && _data->type != WebPagePhoto && _data->type != WebPageVideo) {
		if (_data->type == WebPageProfile) {
			_asArticle = true;
		} else if (_data->siteName == qstr("Twitter") || _data->siteName == qstr("Facebook")) {
			_asArticle = false;
		} else {
			_asArticle = true;
		}
		if (_asArticle && (_data->description.isEmpty() || (title.isEmpty() && _data->siteName.isEmpty()))) {
			_asArticle = false;
		}
	} else {
		_asArticle = false;
	}

	// init attach
	if (!_asArticle && !_attach) {
		if (_data->doc) {
			if (_data->doc->sticker()) {
				_attach = new HistorySticker(_data->doc);
			} else if (_data->doc->isAnimation()) {
				_attach = new HistoryGif(_data->doc, QString(), parent);
			} else {
				_attach = new HistoryDocument(_data->doc, QString(), parent);
			}
		} else if (_data->photo) {
			_attach = new HistoryPhoto(_data->photo, QString(), parent);
		}
	}

	// init strings
	if (_description.isEmpty() && !_data->description.isEmpty()) {
		QString text = textClean(_data->description);
		if (text.isEmpty()) {
			_data->description = QString();
		} else {
			if (!_asArticle && !_attach) {
				text += parent->skipBlock();
			}
			const TextParseOptions *opts = &_webpageDescriptionOptions;
			if (_data->siteName == qstr("Twitter")) {
				opts = &_twitterDescriptionOptions;
			} else if (_data->siteName == qstr("Instagram")) {
				opts = &_instagramDescriptionOptions;
			}
			_description.setText(st::webPageDescriptionFont, text, *opts);
		}
	}
	if (_title.isEmpty() && !title.isEmpty()) {
		title = textOneLine(textClean(title));
		if (title.isEmpty()) {
			if (_data->title.isEmpty()) {
				_data->author = QString();
			} else {
				_data->title = QString();
			}
		} else {
			if (!_asArticle && !_attach && _description.isEmpty()) {
				title += parent->skipBlock();
			}
			_title.setText(st::webPageTitleFont, title, _webpageTitleOptions);
		}
	}
	if (!_siteNameWidth && !_data->siteName.isEmpty()) {
		_siteNameWidth = st::webPageTitleFont->width(_data->siteName);
	}

	// init dimensions
	int32 l = st::msgPadding.left() + st::webPageLeft, r = st::msgPadding.right();
	int32 skipBlockWidth = parent->skipBlockWidth();
	_maxw = skipBlockWidth;
	_minh = 0;

	int32 siteNameHeight = _data->siteName.isEmpty() ? 0 : _lineHeight;
	int32 titleMinHeight = _title.isEmpty() ? 0 : _lineHeight;
	int32 descMaxLines = (3 + (siteNameHeight ? 0 : 1) + (titleMinHeight ? 0 : 1));
	int32 descriptionMinHeight = _description.isEmpty() ? 0 : qMin(_description.minHeight(), descMaxLines * _lineHeight);
	int32 articleMinHeight = siteNameHeight + titleMinHeight + descriptionMinHeight;
	int32 articlePhotoMaxWidth = 0;
	if (_asArticle) {
		articlePhotoMaxWidth = st::webPagePhotoDelta + qMax(articleThumbWidth(_data->photo, articleMinHeight), _lineHeight);
	}

	if (_siteNameWidth) {
		if (_title.isEmpty() && _description.isEmpty()) {
			_maxw = qMax(_maxw, int32(_siteNameWidth + parent->skipBlockWidth()));
		} else {
			_maxw = qMax(_maxw, int32(_siteNameWidth + articlePhotoMaxWidth));
		}
		_minh += _lineHeight;
	}
	if (!_title.isEmpty()) {
		_maxw = qMax(_maxw, int32(_title.maxWidth() + articlePhotoMaxWidth));
		_minh += titleMinHeight;
	}
	if (!_description.isEmpty()) {
		_maxw = qMax(_maxw, int32(_description.maxWidth() + articlePhotoMaxWidth));
		_minh += descriptionMinHeight;
	}
	if (_attach) {
		if (_minh) _minh += st::webPagePhotoSkip;
		_attach->initDimensions(parent);
		QMargins bubble(_attach->bubbleMargins());
		_maxw = qMax(_maxw, int32(_attach->maxWidth() - bubble.left() - bubble.top() + (_attach->customInfoLayout() ? skipBlockWidth : 0)));
		_minh += _attach->minHeight() - bubble.top() - bubble.bottom();
	}
	if (_data->type == WebPageVideo && _data->duration) {
		_duration = formatDurationText(_data->duration);
		_durationWidth = st::msgDateFont->width(_duration);
	}
	_maxw += st::msgPadding.left() + st::webPageLeft + st::msgPadding.right();
	_minh += st::msgPadding.bottom();
	if (_asArticle) {
		_minh = resize(_maxw, parent); // hack
//		_minh += st::msgDateFont->height;
	}
}

int32 HistoryWebPage::resize(int32 width, const HistoryItem *parent) {
	if (_data->pendingTill) {
		_width = width;
		_height = _minh;
		return _height;
	}

	_width = qMin(width, _maxw);
	width -= st::msgPadding.left() + st::webPageLeft + st::msgPadding.right();

	int32 linesMax = 5;
	int32 siteNameLines = _siteNameWidth ? 1 : 0, siteNameHeight = _siteNameWidth ? _lineHeight : 0;
	if (_asArticle) {
		_pixh = linesMax * _lineHeight;
		do {
			_pixw = articleThumbWidth(_data->photo, _pixh);
			int32 wleft = width - st::webPagePhotoDelta - qMax(_pixw, int16(_lineHeight));

			_height = siteNameHeight;

			if (_title.isEmpty()) {
				_titleLines = 0;
			} else {
				if (_title.countHeight(wleft) < 2 * st::webPageTitleFont->height) {
					_titleLines = 1;
				} else {
					_titleLines = 2;
				}
				_height += _titleLines * _lineHeight;
			}

			int32 descriptionHeight = _description.countHeight(wleft);
			if (descriptionHeight < (linesMax - siteNameLines - _titleLines) * st::webPageDescriptionFont->height) {
				_descriptionLines = (descriptionHeight / st::webPageDescriptionFont->height);
			} else {
				_descriptionLines = (linesMax - siteNameLines - _titleLines);
			}
			_height += _descriptionLines * _lineHeight;

			if (_height >= _pixh) {
				break;
			}

			_pixh -= _lineHeight;
		} while (_pixh > _lineHeight);
		_height += st::msgDateFont->height;
	} else {
		_height = siteNameHeight;

		if (_title.isEmpty()) {
			_titleLines = 0;
		} else {
			if (_title.countHeight(width) < 2 * st::webPageTitleFont->height) {
				_titleLines = 1;
			} else {
				_titleLines = 2;
			}
			_height += _titleLines * _lineHeight;
		}

		if (_description.isEmpty()) {
			_descriptionLines = 0;
		} else {
			int32 descriptionHeight = _description.countHeight(width);
			if (descriptionHeight < (linesMax - siteNameLines - _titleLines) * st::webPageDescriptionFont->height) {
				_descriptionLines = (descriptionHeight / st::webPageDescriptionFont->height);
			} else {
				_descriptionLines = (linesMax - siteNameLines - _titleLines);
			}
			_height += _descriptionLines * _lineHeight;
		}

		if (_attach) {
			if (_height) _height += st::webPagePhotoSkip;

			QMargins bubble(_attach->bubbleMargins());

			_attach->resize(width + bubble.left() + bubble.right(), parent);
			_height += _attach->height() - bubble.top() - bubble.bottom();
			if (_attach->customInfoLayout() && _attach->currentWidth() + parent->skipBlockWidth() > width + bubble.left() + bubble.right()) {
				_height += st::msgDateFont->height;
			}
		}
	}
	_height += st::msgPadding.bottom();

	return _height;
}

void HistoryWebPage::draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;

	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	style::color barfg = (selected ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor));
	style::color semibold = (selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
	style::color regular = (selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg));

	int32 lshift = st::msgPadding.left() + st::webPageLeft, rshift = st::msgPadding.right(), bshift = st::msgPadding.bottom();
	width -= lshift + rshift;
	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	if (_asArticle || (_attach && _attach->customInfoLayout() && _attach->currentWidth() + parent->skipBlockWidth() > width + bubble.left() + bubble.right())) {
		bshift += st::msgDateFont->height;
	}

	QRect bar(rtlrect(st::msgPadding.left(), 0, st::webPageBar, _height - bshift, _width));
	p.fillRect(bar, barfg);

	if (_asArticle) {
		_data->photo->medium->load(false, false);
		bool full = _data->photo->medium->loaded();
		QPixmap pix;
		int32 pw = qMax(_pixw, int16(_lineHeight)), ph = _pixh;
		int32 pixw = _pixw, pixh = articleThumbHeight(_data->photo, _pixw);
		int32 maxw = convertScale(_data->photo->medium->width()), maxh = convertScale(_data->photo->medium->height());
		if (pixw * ph != pixh * pw) {
			float64 coef = (pixw * ph > pixh * pw) ? qMin(ph / float64(pixh), maxh / float64(pixh)) : qMin(pw / float64(pixw), maxw / float64(pixw));
			pixh = qRound(pixh * coef);
			pixw = qRound(pixw * coef);
		}
		if (full) {
			pix = _data->photo->medium->pixSingle(pixw, pixh, pw, ph);
		} else {
			pix = _data->photo->thumb->pixBlurredSingle(pixw, pixh, pw, ph);
		}
		p.drawPixmapLeft(lshift + width - pw, 0, _width, pix);
		if (selected) {
			App::roundRect(p, rtlrect(lshift + width - pw, 0, pw, _pixh, _width), textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
		}
		width -= pw + st::webPagePhotoDelta;
	}
	int32 tshift = 0;
	if (_siteNameWidth) {
		p.setFont(st::webPageTitleFont);
		p.setPen(semibold);
		p.drawTextLeft(lshift, tshift, _width, (width >= _siteNameWidth) ? _data->siteName : st::webPageTitleFont->elided(_data->siteName, width));
		tshift += _lineHeight;
	}
	if (_titleLines) {
		p.setPen(st::black);
		int32 endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = parent->skipBlockWidth();
		}
		_title.drawLeftElided(p, lshift, tshift, width, _width, _titleLines, style::al_left, 0, -1, endskip);
		tshift += _titleLines * _lineHeight;
	}
	if (_descriptionLines) {
		p.setPen(st::black);
		int32 endskip = 0;
		if (_description.hasSkipBlock()) {
			endskip = parent->skipBlockWidth();
		}
		_description.drawLeftElided(p, lshift, tshift, width, _width, _descriptionLines, style::al_left, 0, -1, endskip);
		tshift += _descriptionLines * _lineHeight;
	}
	if (_attach) {
		if (tshift) tshift += st::webPagePhotoSkip;

		int32 attachLeft = lshift - bubble.left(), attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = _width - attachLeft - _attach->currentWidth();

		p.save();
		p.translate(attachLeft, attachTop);

		_attach->draw(p, parent, r.translated(-attachLeft, -attachTop), selected, ms);
		int32 pixwidth = _attach->currentWidth(), pixheight = _attach->height();

		if (_data->type == WebPageVideo) {
			if (_data->siteName == qstr("YouTube")) {
				p.drawPixmap(QPoint((pixwidth - st::youtubeIcon.pxWidth()) / 2, (pixheight - st::youtubeIcon.pxHeight()) / 2), App::sprite(), st::youtubeIcon);
			} else {
				p.drawPixmap(QPoint((pixwidth - st::videoIcon.pxWidth()) / 2, (pixheight - st::videoIcon.pxHeight()) / 2), App::sprite(), st::videoIcon);
			}
			if (_durationWidth) {
				int32 dateX = pixwidth - _durationWidth - st::msgDateImgDelta - 2 * st::msgDateImgPadding.x();
				int32 dateY = pixheight - st::msgDateFont->height - 2 * st::msgDateImgPadding.y() - st::msgDateImgDelta;
				int32 dateW = pixwidth - dateX - st::msgDateImgDelta;
				int32 dateH = pixheight - dateY - st::msgDateImgDelta;

				App::roundRect(p, dateX, dateY, dateW, dateH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);

				p.setFont(st::msgDateFont);
				p.setPen(st::msgDateImgColor);
				p.drawTextLeft(dateX + st::msgDateImgPadding.x(), dateY + st::msgDateImgPadding.y(), pixwidth, _duration);
			}
		}

		p.restore();
	}
}

void HistoryWebPage::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;

	int32 lshift = st::msgPadding.left() + st::webPageLeft, rshift = st::msgPadding.right(), bshift = st::msgPadding.bottom();
	width -= lshift + rshift;
	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	if (_asArticle || (_attach && _attach->customInfoLayout() && _attach->currentWidth() + parent->skipBlockWidth() > width + bubble.left() + bubble.right())) {
		bshift += st::msgDateFont->height;
	}

	if (_asArticle) {
		int32 pw = qMax(_pixw, int16(_lineHeight));
		if (rtlrect(lshift + width - pw, 0, pw, _pixh, _width).contains(x, y)) {
			lnk = _openl;
			return;
		}
		width -= pw + st::webPagePhotoDelta;
	}
	int32 tshift = 0;
	if (_siteNameWidth) {
		tshift += _lineHeight;
	}
	if (_titleLines) {
		tshift += _titleLines * _lineHeight;
	}
	if (_descriptionLines) {
		if (y >= tshift && y < tshift + _descriptionLines * _lineHeight) {
			bool inText = false;
			_description.getStateLeft(lnk, inText, x - lshift, y - tshift, width, _width);
			state = inText ? HistoryInTextCursorState : HistoryDefaultCursorState;
			return;
		}
		tshift += _descriptionLines * _lineHeight;
	}
	if (_attach) {
		if (tshift) tshift += st::webPagePhotoSkip;

		if (x >= lshift && x < lshift + width && y >= tshift && y < _height - st::msgPadding.bottom()) {
			int32 attachLeft = lshift - bubble.left(), attachTop = tshift - bubble.top();
			if (rtl()) attachLeft = _width - attachLeft - _attach->currentWidth();
			_attach->getState(lnk, state, x - attachLeft, y - attachTop, parent);
			if (lnk && !_data->doc && _data->photo) {
				if (_data->type == WebPageProfile || _data->type == WebPageVideo) {
					lnk = _openl;
				} else if (_data->type == WebPagePhoto || _data->siteName == qstr("Twitter") || _data->siteName == qstr("Facebook")) {
					// leave photo link
				} else {
					lnk = _openl;
				}
			}
		}
	}
}

void HistoryWebPage::linkOver(HistoryItem *parent, const TextLinkPtr &lnk) {
	if (_attach) {
		_attach->linkOver(parent, lnk);
	}
}

void HistoryWebPage::linkOut(HistoryItem *parent, const TextLinkPtr &lnk) {
	if (_attach) {
		_attach->linkOut(parent, lnk);
	}
}

void HistoryWebPage::regItem(HistoryItem *item) {
	App::regWebPageItem(_data, item);
	if (_attach) _attach->regItem(item);
}

void HistoryWebPage::unregItem(HistoryItem *item) {
	App::unregWebPageItem(_data, item);
	if (_attach) _attach->unregItem(item);
}

const QString HistoryWebPage::inDialogsText() const {
	return QString();
}

const QString HistoryWebPage::inHistoryText() const {
	return QString();
}

ImagePtr HistoryWebPage::replyPreview() {
	return _attach ? _attach->replyPreview() : (_data->photo ? _data->photo->makeReplyPreview() : ImagePtr());
}

HistoryWebPage::~HistoryWebPage() {
	deleteAndMark(_attach);
}

namespace {
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
#if QT_VERSION >= QT_VERSION_CHECK(5, 5, 0)
				reader.setAutoTransform(true);
#endif
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
		_link.reset(new TextLink(lnk));

		_data = App::imageLinkSet(url, GoogleMapsLink, lnk);
	} else {
		_link.reset(new TextLink(url));
	}
}

void HistoryImageLink::initDimensions(const HistoryItem *parent) {
	bool bubble = parent->hasBubble();

	int32 tw = fullWidth(), th = fullHeight();
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	int32 minWidth = qMax(st::minPhotoSize, parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_maxw = qMax(tw, int32(minWidth));
	_minh = qMax(th, int32(st::minPhotoSize));

	if (bubble) {
		_maxw += st::mediaPadding.left() + st::mediaPadding.right();
		if (!_title.isEmpty()) {
			_minh += qMin(_title.countHeight(_maxw - st::msgPadding.left() - st::msgPadding.right()), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			_maxw = qMax(_maxw, int32(st::msgPadding.left() + _description.maxWidth() + st::msgPadding.right()));
			_minh += qMin(_description.countHeight(_maxw - st::msgPadding.left() - st::msgPadding.right()), 3 * st::webPageDescriptionFont->height);
		}
		_minh += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_title.isEmpty() || !_description.isEmpty()) {
			_minh += st::webPagePhotoSkip;
			if (!parent->toHistoryForwarded() && !parent->toHistoryReply()) {
				_minh += st::msgPadding.top();
			}
		}
	}
}

int32 HistoryImageLink::resize(int32 width, const HistoryItem *parent) {
	bool bubble = parent->hasBubble();

	_width = qMin(width, _maxw);
	if (bubble) {
		_width -= st::mediaPadding.left() + st::mediaPadding.right();
	}

	int32 tw = fullWidth(), th = fullHeight();
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	_height = th;
	if (tw > _width) {
		_height = (_width * _height / tw);
	} else {
		_width = tw;
	}
	int32 minWidth = qMax(st::minPhotoSize, parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_width = qMax(_width, int32(minWidth));
	_height = qMax(_height, int32(st::minPhotoSize));
	if (bubble) {
		_width += st::mediaPadding.left() + st::mediaPadding.right();
		_height += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_title.isEmpty()) {
			_height += qMin(_title.countHeight(_width - st::msgPadding.left() - st::msgPadding.right()), st::webPageTitleFont->height * 2);
		}
		if (!_description.isEmpty()) {
			_height += qMin(_description.countHeight(_width - st::msgPadding.left() - st::msgPadding.right()), st::webPageDescriptionFont->height * 3);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			_height += st::webPagePhotoSkip;
			if (!parent->toHistoryForwarded() && !parent->toHistoryReply()) {
				_height += st::msgPadding.top();
			}
		}
	}
	return _height;
}

void HistoryImageLink::draw(Painter &p, const HistoryItem *parent, const QRect &r, bool selected, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = parent->hasBubble();
	bool out = parent->out(), fromChannel = parent->fromChannel(), outbg = out && !fromChannel;

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		if (!_title.isEmpty() || !_description.isEmpty()) {
			if (!parent->toHistoryForwarded() && !parent->toHistoryReply()) {
				skipy += st::msgPadding.top();
			}
		}

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		int32 textw = _width - st::msgPadding.left() - st::msgPadding.right();

		p.setPen(st::black);
		if (!_title.isEmpty()) {
			_title.drawLeftElided(p, skipx + st::msgPadding.left(), skipy, textw, _width, 2);
			skipy += qMin(_title.countHeight(textw), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			_description.drawLeftElided(p, skipx + st::msgPadding.left(), skipy, textw, _width, 3);
			skipy += qMin(_description.countHeight(textw), 3 * st::webPageDescriptionFont->height);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			skipy += st::webPagePhotoSkip;
		}
		height -= skipy + st::mediaPadding.bottom();
	} else {
		App::roundShadow(p, 0, 0, width, height, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	_data->load();
	QPixmap toDraw;
	if (_data && !_data->thumb->isNull()) {
		int32 w = _data->thumb->width(), h = _data->thumb->height();
		QPixmap pix;
		if (width * h == height * w || (w == fullWidth() && h == fullHeight())) {
			pix = _data->thumb->pixSingle(width, height, width, height);
		} else if (width * h > height * w) {
			int32 nw = height * w / h;
			pix = _data->thumb->pixSingle(nw, height, width, height);
		} else {
			int32 nh = width * h / w;
			pix = _data->thumb->pixSingle(width, nh, width, height);
		}
		p.drawPixmap(QPoint(skipx, skipy), pix);
	} else {
		App::roundRect(p, skipx, skipy, width, height, st::white, MessageInCorners);
	}
	if (selected) {
		App::roundRect(p, skipx, skipy, width, height, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	if (parent->getMedia() == this) {
		int32 fullRight = skipx + width, fullBottom = _height - (skipx ? st::mediaPadding.bottom() : 0);
		parent->drawInfo(p, fullRight, fullBottom, skipx * 2 + width, selected, InfoDisplayOverImage);
	}
}

void HistoryImageLink::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const HistoryItem *parent) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = parent->hasBubble();

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		if (!_title.isEmpty() || !_description.isEmpty()) {
			if (!parent->toHistoryForwarded() && !parent->toHistoryReply()) {
				skipy += st::msgPadding.top();
			}
		}

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		int32 textw = _width - st::msgPadding.left() - st::msgPadding.right();

		if (!_title.isEmpty()) {
			skipy += qMin(_title.countHeight(textw), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			skipy += qMin(_description.countHeight(textw), 3 * st::webPageDescriptionFont->height);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			skipy += st::webPagePhotoSkip;
		}
		height -= skipy + st::mediaPadding.bottom();
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height && _data) {
		lnk = _link;

		int32 fullRight = skipx + width, fullBottom = _height - (skipx ? st::mediaPadding.bottom() : 0);
		bool inDate = parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
		if (inDate) {
			state = HistoryInDateCursorState;
		}

		return;
	}
}

const QString HistoryImageLink::inDialogsText() const {
	if (_data) {
		switch (_data->type) {
		case GoogleMapsLink: return lang(lng_maps_point);
		}
	}
	return QString();
}

const QString HistoryImageLink::inHistoryText() const {
	if (_data) {
		switch (_data->type) {
		case GoogleMapsLink: return qsl("[ ") + lang(lng_maps_point) + qsl(" : ") + _link->text() + qsl(" ]");
		}
	}
	return qsl("[ Link : ") + _link->text() + qsl(" ]");
}

int32 HistoryImageLink::fullWidth() const {
	if (_data) {
		switch (_data->type) {
		case GoogleMapsLink: return st::locationSize.width();
		}
	}
	return st::minPhotoSize;
}

int32 HistoryImageLink::fullHeight() const {
	if (_data) {
		switch (_data->type) {
		case GoogleMapsLink: return st::locationSize.height();
		}
	}
	return st::minPhotoSize;
}

void ViaInlineBotLink::onClick(Qt::MouseButton button) const {
	App::insertBotCommand('@' + _bot->username);
}

HistoryMessageVia::HistoryMessageVia(int32 userId)
: bot(App::userLoaded(peerFromUser(userId)))
, width(0)
, maxWidth(bot ? st::msgServiceNameFont->width(lng_inline_bot_via(lt_inline_bot, '@' + bot->username)) : 0)
, lnk(new ViaInlineBotLink(bot)) {
}

bool HistoryMessageVia::isNull() const {
	return !bot || bot->username.isEmpty();
}

void HistoryMessageVia::resize(int32 availw) {
	if (availw < 0) {
		text = QString();
		width = 0;
	} else {
		text = lng_inline_bot_via(lt_inline_bot, '@' + bot->username);
		if (availw < maxWidth) {
			text = st::msgServiceNameFont->elided(text, availw);
			width = st::msgServiceNameFont->width(text);
		} else if (width < maxWidth) {
			width = maxWidth;
		}
	}
}

HistoryMessage::HistoryMessage(History *history, HistoryBlock *block, const MTPDmessage &msg) :
	HistoryItem(history, block, msg.vid.v, msg.vflags.v, ::date(msg.vdate), msg.has_from_id() ? msg.vfrom_id.v : 0)
, _text(st::msgMinWidth)
, _textWidth(0)
, _textHeight(0)
, _via(msg.has_via_bot_id() ? new HistoryMessageVia(msg.vvia_bot_id.v) : 0)
, _media(0)
, _views(msg.has_views() ? msg.vviews.v : -1) {
	QString text(textClean(qs(msg.vmessage)));
	initTime();
	initMedia(msg.has_media() ? (&msg.vmedia) : 0, text);
	setText(text, msg.has_entities() ? entitiesFromMTP(msg.ventities.c_vector().v) : EntitiesInText());
}

HistoryMessage::HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, int32 viaBotId, QDateTime date, int32 from, const QString &msg, const EntitiesInText &entities, HistoryMedia *fromMedia) :
HistoryItem(history, block, msgId, flags, date, (flags & MTPDmessage::flag_from_id) ? from : 0)
, _text(st::msgMinWidth)
, _textWidth(0)
, _textHeight(0)
, _via((flags & MTPDmessage::flag_via_bot_id) ? new HistoryMessageVia(viaBotId) : 0)
, _media(0)
, _views(fromChannel() ? 1 : -1) {
	initTime();
	if (fromMedia) {
		_media = fromMedia->clone();
		_media->regItem(this);
	}
	setText(msg, entities);
}

HistoryMessage::HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, int32 viaBotId, QDateTime date, int32 from, DocumentData *doc, const QString &caption) :
HistoryItem(history, block, msgId, flags, date, (flags & MTPDmessage::flag_from_id) ? from : 0)
, _text(st::msgMinWidth)
, _textWidth(0)
, _textHeight(0)
, _via((flags & MTPDmessage::flag_via_bot_id) ? new HistoryMessageVia(viaBotId) : 0)
, _media(0)
, _views(fromChannel() ? 1 : -1) {
	initTime();
	initMediaFromDocument(doc, caption);
	setText(QString(), EntitiesInText());
}

HistoryMessage::HistoryMessage(History *history, HistoryBlock *block, MsgId msgId, int32 flags, int32 viaBotId, QDateTime date, int32 from, PhotoData *photo, const QString &caption) :
HistoryItem(history, block, msgId, flags, date, (flags & MTPDmessage::flag_from_id) ? from : 0)
, _text(st::msgMinWidth)
, _textWidth(0)
, _textHeight(0)
, _via((flags & MTPDmessage::flag_via_bot_id) ? new HistoryMessageVia(viaBotId) : 0)
, _media(0)
, _views(fromChannel() ? 1 : -1) {
	initTime();
	_media = new HistoryPhoto(photo, caption, this);
	_media->regItem(this);
	setText(QString(), EntitiesInText());
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
			_media = new HistoryPhoto(App::feedPhoto(photo.vphoto.c_photo()), qs(photo.vcaption), this);
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
			return initMediaFromDocument(App::feedDocument(document), qs(media->c_messageMediaDocument().vcaption));
		}
	} break;
	case mtpc_messageMediaWebPage: {
		const MTPWebPage &d(media->c_messageMediaWebPage().vwebpage);
		switch (d.type()) {
		case mtpc_webPageEmpty: break;
		case mtpc_webPagePending: {
			_media = new HistoryWebPage(App::feedWebPage(d.c_webPagePending()));
		} break;
		case mtpc_webPage: {
			_media = new HistoryWebPage(App::feedWebPage(d.c_webPage()));
		} break;
		}
	} break;
	};
	if (_media) _media->regItem(this);
}

void HistoryMessage::initMediaFromDocument(DocumentData *doc, const QString &caption) {
	if (doc->sticker()) {
		_media = new HistorySticker(doc);
	} else if (doc->isAnimation()) {
		_media = new HistoryGif(doc, caption, this);
	} else {
		_media = new HistoryDocument(doc, caption, this);
	}
	_media->regItem(this);
}

int32 HistoryMessage::plainMaxWidth() const {
	return st::msgPadding.left() + _text.maxWidth() + st::msgPadding.right();
}

void HistoryMessage::initDimensions() {
	if (drawBubble()) {
		if (_media) {
			_media->initDimensions(this);
			if (_media->isDisplayed()) {
				if (_text.hasSkipBlock()) {
					_text.removeSkipBlock();
					_textWidth = 0;
					_textHeight = 0;
				}
			} else if (!_text.hasSkipBlock()) {
				_text.setSkipBlock(skipBlockWidth(), skipBlockHeight());
				_textWidth = 0;
				_textHeight = 0;
			}
		}

		_maxw = plainMaxWidth();
		if (_text.isEmpty()) {
			_minh = 0;
		} else {
			_minh = st::msgPadding.top() + _text.minHeight() + st::msgPadding.bottom();
		}
		if (_media && _media->isDisplayed()) {
			int32 maxw = _media->maxWidth();
			if (maxw > _maxw) _maxw = maxw;
			_minh += _media->minHeight();
		}
		if (!_media) {
			if (displayFromName()) {
				int32 namew = st::msgPadding.left() + _from->nameText.maxWidth() + st::msgPadding.right();
				if (via() && !toHistoryForwarded()) {
					namew += st::msgServiceFont->spacew + via()->maxWidth;
				}
				if (namew > _maxw) _maxw = namew;
			} else if (via() && !toHistoryForwarded()) {
				if (st::msgPadding.left() + via()->maxWidth + st::msgPadding.right() > _maxw) {
					_maxw = st::msgPadding.left() + via()->maxWidth + st::msgPadding.right();
				}
			}
		}
	} else {
		_media->initDimensions(this);
		_maxw = _media->maxWidth();
		_minh = _media->minHeight();
	}
}

void HistoryMessage::countPositionAndSize(int32 &left, int32 &width) const {
	int32 maxwidth = qMin(int(st::msgMaxWidth), _maxw), hwidth = _history->width, hmaxwidth = st::historyMaxWidth + (Adaptive::Wide() ? (2 * st::msgPhotoSkip) : 0);
	if (_media && _media->currentWidth() < maxwidth) {
		maxwidth = qMax(_media->currentWidth(), qMin(maxwidth, plainMaxWidth()));
	}

	left = 0;
	if (hwidth > hmaxwidth) {
		left = (hwidth - hmaxwidth) / 2;
		hwidth = hmaxwidth;
	}
	left += (!fromChannel() && out()) ? st::msgMargin.right() : st::msgMargin.left();
	if (displayFromPhoto()) {
		if (!fromChannel() && out()) {
			left -= st::msgPhotoSkip;
		} else {
			left += st::msgPhotoSkip;
			if (shiftFromPhoto()) {
				left += st::msgPhotoSkip;
			}
		}
	}

	width = hwidth - st::msgMargin.left() - st::msgMargin.right();
	if (width > maxwidth) {
		if (!fromChannel() && out()) {
			left += width - maxwidth;
		}
		width = maxwidth;
	}
}

void HistoryMessage::fromNameUpdated(int32 width) const {
	_fromVersion = _from->nameVersion;
	if (drawBubble() && displayFromName()) {
		if (via() && !toHistoryForwarded()) {
			via()->resize(width - st::msgPadding.left() - st::msgPadding.right() - _from->nameText.maxWidth() - st::msgServiceFont->spacew);
		}
	}
}

int32 HistoryMessage::addToOverview(AddToOverviewMethod method) {
	if (!indexInOverview()) return 0;

	int32 result = 0;
	if (HistoryMedia *media = getMedia(true)) {
		MediaOverviewType type = mediaToOverviewType(media);
		if (type != OverviewCount) {
			if (history()->addToOverview(type, id, method)) {
				result |= (1 << type);
			}
		}
	}
	if (hasTextLinks()) {
		if (history()->addToOverview(OverviewLinks, id, method)) {
			result |= (1 << OverviewLinks);
		}
	}
	return result;
}

void HistoryMessage::eraseFromOverview() {
	if (HistoryMedia *media = getMedia(true)) {
		MediaOverviewType type = mediaToOverviewType(media);
		if (type != OverviewCount) {
			history()->eraseFromOverview(type, id);
		}
	}
	if (hasTextLinks()) {
		history()->eraseFromOverview(OverviewLinks, id);
	}
}

QString HistoryMessage::selectedText(uint32 selection) const {
	if (_media && selection == FullSelection) {
		QString text = _text.original(0, 0xFFFF, Text::ExpandLinksAll), mediaText = _media->inHistoryText();
		return text.isEmpty() ? mediaText : (mediaText.isEmpty() ? text : (text + ' ' + mediaText));
	}
	uint16 selectedFrom = (selection == FullSelection) ? 0 : ((selection >> 16) & 0xFFFF);
	uint16 selectedTo = (selection == FullSelection) ? 0xFFFF : (selection & 0xFFFF);
	return _text.original(selectedFrom, selectedTo, Text::ExpandLinksAll);
}

QString HistoryMessage::inDialogsText() const {
	return emptyText() ? (_media ? _media->inDialogsText() : QString()) : _text.original(0, 0xFFFF, Text::ExpandLinksNone);
}

HistoryMedia *HistoryMessage::getMedia(bool inOverview) const {
	return _media;
}

void HistoryMessage::setMedia(const MTPMessageMedia *media) {
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
}

void HistoryMessage::setText(const QString &text, const EntitiesInText &entities) {
	textstyleSet(&((out() && !fromChannel()) ? st::outTextStyle : st::inTextStyle));
	if (_media && _media->isDisplayed()) {
		_text.setMarkedText(st::msgFont, text, entities, itemTextOptions(this));
	} else {
		_text.setMarkedText(st::msgFont, text + skipBlock(), entities, itemTextOptions(this));
	}
	textstyleRestore();

	for (int32 i = 0, l = entities.size(); i != l; ++i) {
		if (entities.at(i).type == EntityInTextUrl || entities.at(i).type == EntityInTextCustomUrl || entities.at(i).type == EntityInTextEmail) {
			_flags |= MTPDmessage_flag_HAS_TEXT_LINKS;
			break;
		}
	}
	_textWidth = 0;
	_textHeight = 0;
}

QString HistoryMessage::originalText() const {
	return emptyText() ? QString() : _text.original();
}

EntitiesInText HistoryMessage::originalEntities() const {
	return emptyText() ? EntitiesInText() : _text.originalEntities();
}

bool HistoryMessage::textHasLinks() {
	return emptyText() ? false : _text.hasLinks();
}

void HistoryMessage::drawInfo(Painter &p, int32 right, int32 bottom, int32 width, bool selected, InfoDisplayType type) const {
	p.setFont(st::msgDateFont);

	bool outbg = out() && !fromChannel(), overimg = (type == InfoDisplayOverImage);
	int32 infoRight = right, infoBottom = bottom;
	switch (type) {
	case InfoDisplayDefault:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
		p.setPen((selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg))->p);
	break;
	case InfoDisplayOverImage:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st::msgDateImgColor->p);
	break;
	}

	int32 infoW = HistoryMessage::infoWidth();
	if (rtl()) infoRight = width - infoRight + infoW;

	int32 dateX = infoRight - infoW;
	int32 dateY = infoBottom - st::msgDateFont->height;
	if (type == InfoDisplayOverImage) {
		int32 dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		App::roundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
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
	} else if (id < 0 && history()->peer->isSelf()) {
		iconPos = QPoint(infoRight - infoW, infoBottom - st::msgViewsImg.pxHeight() + st::msgViewsPos.y());
		iconRect = &(overimg ? st::msgInvSendingViewsImg : st::msgSendingViewsImg);
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

void HistoryMessage::setViewsCount(int32 count, bool reinit) {
	if (_views == count || (count >= 0 && _views > count)) return;

	int32 was = _viewsWidth;
	_views = count;
	_viewsText = (_views >= 0) ? formatViewsCount(_views) : QString();
	_viewsWidth = _viewsText.isEmpty() ? 0 : st::msgDateFont->width(_viewsText);
	if (was == _viewsWidth) {
		Ui::repaintHistoryItem(this);
	} else {
		if (_text.hasSkipBlock()) {
			_text.setSkipBlock(HistoryMessage::skipBlockWidth(), HistoryMessage::skipBlockHeight());
			_textWidth = 0;
			_textHeight = 0;
		}
		if (reinit) {
			initDimensions();
			Notify::historyItemResized(this);
		}
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
			_textWidth = 0;
			_textHeight = 0;
		}
		initDimensions();
		Notify::historyItemResized(this);
	}
}

void HistoryMessage::draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const {
	bool outbg = out() && !fromChannel(), bubble = drawBubble(), selected = (selection == FullSelection);

	textstyleSet(&(outbg ? st::outTextStyle : st::inTextStyle));

	uint64 animms = App::main() ? App::main()->animActiveTimeStart(this) : 0;
	if (animms > 0 && animms <= ms) {
		animms = ms - animms;
		if (animms > st::activeFadeInDuration + st::activeFadeOutDuration) {
			App::main()->stopAnimActive();
		} else {
			float64 dt = (animms > st::activeFadeInDuration) ? (1 - (animms - st::activeFadeInDuration) / float64(st::activeFadeOutDuration)) : (animms / float64(st::activeFadeInDuration));
			float64 o = p.opacity();
			p.setOpacity(o * dt);
			p.fillRect(0, 0, _history->width, _height, textstyleCurrent()->selectOverlay->b);
			p.setOpacity(o);
		}
	}

	int32 left = 0, width = 0;
	countPositionAndSize(left, width);
	if (_from->nameVersion > _fromVersion) {
		fromNameUpdated(width);
	}

	if (displayFromPhoto()) {
		int32 photoleft = left + ((!fromChannel() && out()) ? (width + (st::msgPhotoSkip - st::msgPhotoSize)) : (-st::msgPhotoSkip));
		p.drawPixmap(photoleft, _height - st::msgMargin.bottom() - st::msgPhotoSize, _from->photo->pixRounded(st::msgPhotoSize));
	}
	if (width < 1) return;

	if (bubble) {
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());

		style::color bg(selected ? (outbg ? st::msgOutBgSelected : st::msgInBgSelected) : (outbg ? st::msgOutBg : st::msgInBg));
		style::color sh(selected ? (outbg ? st::msgOutShadowSelected : st::msgInShadowSelected) : (outbg ? st::msgOutShadow : st::msgInShadow));
		RoundCorners cors(selected ? (outbg ? MessageOutSelectedCorners : MessageInSelectedCorners) : (outbg ? MessageOutCorners : MessageInCorners));
		App::roundRect(p, r, bg, cors, &sh);

		if (displayFromName()) {
			p.setFont(st::msgNameFont);
			if (fromChannel()) {
				p.setPen(selected ? st::msgInServiceFgSelected : st::msgInServiceFg);
			} else {
				p.setPen(_from->color);
			}
			_from->nameText.drawElided(p, r.left() + st::msgPadding.left(), r.top() + st::msgPadding.top(), width - st::msgPadding.left() - st::msgPadding.right());
			if (via() && !toHistoryForwarded() && width > st::msgPadding.left() + st::msgPadding.right() + _from->nameText.maxWidth() + st::msgServiceFont->spacew) {
				p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
				p.drawText(r.left() + st::msgPadding.left() + _from->nameText.maxWidth() + st::msgServiceFont->spacew, r.top() + st::msgPadding.top() + st::msgServiceFont->ascent, via()->text);
			}
			r.setTop(r.top() + st::msgNameFont->height);
		}

		QRect trect(r.marginsAdded(-st::msgPadding));
		drawMessageText(p, trect, selection);

		if (_media && _media->isDisplayed()) {
			p.save();
			int32 top = _height - st::msgMargin.bottom() - _media->height();
			p.translate(left, top);
			_media->draw(p, this, r.translated(-left, -top), selected, ms);
			p.restore();
			if (!_media->customInfoLayout()) {
				HistoryMessage::drawInfo(p, r.x() + r.width(), r.y() + r.height(), 2 * r.x() + r.width(), selected, InfoDisplayDefault);
			}
		} else {
			HistoryMessage::drawInfo(p, r.x() + r.width(), r.y() + r.height(), 2 * r.x() + r.width(), selected, InfoDisplayDefault);
		}
	} else {
		p.save();
		int32 top = st::msgMargin.top();
		p.translate(left, top);
		_media->draw(p, this, r.translated(-left, -top), selected, ms);
		p.restore();
	}

	textstyleRestore();
}

void HistoryMessage::drawMessageText(Painter &p, QRect trect, uint32 selection) const {
	bool outbg = out() && !fromChannel(), selected = (selection == FullSelection);
	if (!displayFromName() && via() && !toHistoryForwarded()) {
		p.setFont(st::msgServiceNameFont);
		p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
		p.drawTextLeft(trect.left(), trect.top(), _history->width, via()->text);
		trect.setY(trect.y() + st::msgServiceNameFont->height);
	}

	p.setPen(st::msgColor);
	p.setFont(st::msgFont);
	uint16 selectedFrom = (selection == FullSelection) ? 0 : (selection >> 16) & 0xFFFF;
	uint16 selectedTo = (selection == FullSelection) ? 0 : selection & 0xFFFF;
	_text.draw(p, trect.x(), trect.y(), trect.width(), style::al_left, 0, -1, selectedFrom, selectedTo);
}

void HistoryMessage::destroy() {
	eraseFromOverview();
	HistoryItem::destroy();
}

int32 HistoryMessage::resize(int32 width) {
	if (width < st::msgMinWidth) return _height;

	width -= st::msgMargin.left() + st::msgMargin.right();
	if (width < st::msgPadding.left() + st::msgPadding.right() + 1) {
		width = st::msgPadding.left() + st::msgPadding.right() + 1;
	} else if (width > st::msgMaxWidth) {
		width = st::msgMaxWidth;
	}
	if (drawBubble()) {
		bool media = (_media && _media->isDisplayed());
		if (width >= _maxw) {
			_height = _minh;
			if (media) _media->resize(_maxw, this);
		} else {
			if (_text.isEmpty()) {
				_height = 0;
			} else {
				int32 textWidth = qMax(width - st::msgPadding.left() - st::msgPadding.right(), 1);
				if (textWidth != _textWidth) {
					textstyleSet(&((out() && !fromChannel()) ? st::outTextStyle : st::inTextStyle));
					_textWidth = textWidth;
					_textHeight = _text.countHeight(textWidth);
					textstyleRestore();
				}
				_height = st::msgPadding.top() + _textHeight + st::msgPadding.bottom();
			}
			if (media) _height += _media->resize(width, this);
		}

		if (displayFromName()) {
			if (emptyText()) {
				_height += st::msgPadding.top() + st::msgNameFont->height + st::mediaHeaderSkip;
			} else {
				_height += st::msgNameFont->height;
			}
			int32 l = 0, w = 0;
			countPositionAndSize(l, w);
			fromNameUpdated(w);
		} else if (via() && !toHistoryForwarded()) {
			int32 l = 0, w = 0;
			countPositionAndSize(l, w);
			via()->resize(w - st::msgPadding.left() - st::msgPadding.right());
			if (emptyText() && !displayFromName()) {
				_height += st::msgPadding.top() + st::msgNameFont->height + st::mediaHeaderSkip;
			} else {
				_height += st::msgNameFont->height;
			}
		}
	} else {
		_height = _media->resize(width, this);
	}
	_height += st::msgMargin.top() + st::msgMargin.bottom();
	return _height;
}

bool HistoryMessage::hasPoint(int32 x, int32 y) const {
	int32 left = 0, width = 0;
	countPositionAndSize(left, width);
	if (width < 1) return false;

	if (drawBubble()) {
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		return r.contains(x, y);
	} else {
		return _media->hasPoint(x - left, y - st::msgMargin.top(), this);
	}
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

	int32 left = 0, width = 0;
	countPositionAndSize(left, width);
	if (displayFromPhoto()) {
		int32 photoleft = left + ((!fromChannel() && out()) ? (width + (st::msgPhotoSkip - st::msgPhotoSize)) : (-st::msgPhotoSkip));
		if (x >= photoleft && x < photoleft + st::msgPhotoSize && y >= _height - st::msgMargin.bottom() - st::msgPhotoSize && y < _height - st::msgMargin.bottom()) {
			lnk = _from->lnk;
			return;
		}
	}
	if (width < 1) return;

	if (drawBubble()) {
		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		if (displayFromName()) { // from user left name
			if (y >= r.top() + st::msgPadding.top() && y < r.top() + st::msgPadding.top() + st::msgNameFont->height) {
				if (x >= r.left() + st::msgPadding.left() && x < r.left() + r.width() - st::msgPadding.right() && x < r.left() + st::msgPadding.left() + _from->nameText.maxWidth()) {
					lnk = _from->lnk;
					return;
				}
				if (via() && !toHistoryForwarded() && x >= r.left() + st::msgPadding.left() + _from->nameText.maxWidth() + st::msgServiceFont->spacew && x < r.left() + st::msgPadding.left() + _from->nameText.maxWidth() + st::msgServiceFont->spacew + via()->width) {
					lnk = via()->lnk;
					return;
				}
			}
			r.setTop(r.top() + st::msgNameFont->height);
		}
		getStateFromMessageText(lnk, state, x, y, r);
	} else {
		_media->getState(lnk, state, x - left, y - st::msgMargin.top(), this);
	}
}

void HistoryMessage::getStateFromMessageText(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y, const QRect &r) const {
	bool inDate = false;

	QRect trect(r.marginsAdded(-st::msgPadding));

	if (!displayFromName() && via() && !toHistoryForwarded()) {
		if (x >= trect.left() && y >= trect.top() && y < trect.top() + st::msgNameFont->height && x < trect.left() + via()->width) {
			lnk = via()->lnk;
			return;
		}
		trect.setTop(trect.top() + st::msgNameFont->height);
	}

	TextLinkPtr medialnk;
	if (_media && _media->isDisplayed()) {
		if (!_media->customInfoLayout()) {
			inDate = HistoryMessage::pointInTime(r.x() + r.width(), r.y() + r.height(), x, y, InfoDisplayDefault);
		}
		if (y >= r.bottom() - _media->height() && y < r.bottom()) {
			_media->getState(lnk, state, x - r.left(), y - (r.bottom() - _media->height()), this);
			if (inDate) state = HistoryInDateCursorState;
			return;
		}
		trect.setBottom(trect.bottom() - _media->height());
	} else {
		inDate = HistoryMessage::pointInTime(r.x() + r.width(), r.y() + r.height(), x, y, InfoDisplayDefault);
	}

	textstyleSet(&((out() && !fromChannel()) ? st::outTextStyle : st::inTextStyle));
	bool inText = false;
	_text.getState(lnk, inText, x - trect.x(), y - trect.y(), trect.width());
	textstyleRestore();

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
	if (drawBubble()) {
		int32 left = 0, width = 0;
		countPositionAndSize(left, width);
		if (width < 1) return;

		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		if (displayFromName()) { // from user left name
			r.setTop(r.top() + st::msgNameFont->height);
		} else if (via() && !toHistoryForwarded()) {
			r.setTop(r.top() + st::msgNameFont->height);
		}
		QRect trect(r.marginsAdded(-st::msgPadding));
		if (_media && _media->isDisplayed()) {
			trect.setBottom(trect.bottom() - _media->height());
		}

		textstyleSet(&((out() && !fromChannel()) ? st::outTextStyle : st::inTextStyle));
		_text.getSymbol(symbol, after, upon, x - trect.x(), y - trect.y(), trect.width());
		textstyleRestore();
	}
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
		p.setPen((act ? st::dlgActiveColor : (emptyText() ? st::dlgSystemColor : st::dlgTextColor))->p);
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
		deleteAndMark(_media);
	}
	deleteAndMark(_via);
	if (_flags & MTPDmessage::flag_reply_markup) {
		App::clearReplyMarkup(channelId(), id);
	}
}

HistoryForwarded::HistoryForwarded(History *history, HistoryBlock *block, const MTPDmessage &msg)
: HistoryMessage(history, block, msg)
, fwdDate(::date(msg.vfwd_date))
, fwdFrom(App::peer(peerFromMTP(msg.vfwd_from_id)))
, fwdFromVersion(fwdFrom->nameVersion)
, fromWidth(st::msgServiceFont->width(lang(lng_forwarded_from)) + st::msgServiceFont->spacew) {
}

HistoryForwarded::HistoryForwarded(History *history, HistoryBlock *block, MsgId id, QDateTime date, int32 from, HistoryMessage *msg)
: HistoryMessage(history, block, id, newForwardedFlags(history->peer, from, msg), msg->via() ? peerToUser(msg->viaBot()->id) : 0, date, from, msg->HistoryMessage::originalText(), msg->HistoryMessage::originalEntities(), msg->getMedia())
, fwdDate(msg->dateForwarded())
, fwdFrom(msg->fromForwarded())
, fwdFromVersion(fwdFrom->nameVersion)
, fromWidth(st::msgServiceFont->width(lang(lng_forwarded_from)) + st::msgServiceFont->spacew) {
}

QString HistoryForwarded::selectedText(uint32 selection) const {
	if (selection != FullSelection) return HistoryMessage::selectedText(selection);
	QString result, original = HistoryMessage::selectedText(selection);
	result.reserve(lang(lng_forwarded_from).size() + fwdFrom->name.size() + 4 + original.size());
	result.append('[').append(lang(lng_forwarded_from)).append(' ').append(fwdFrom->name).append(qsl("]\n")).append(original);
	return result;
}

void HistoryForwarded::initDimensions() {
	fwdNameUpdated();
	HistoryMessage::initDimensions();
	if (!_media) {
		int32 _namew = st::msgPadding.left() + fromWidth + fwdFromName.maxWidth() + st::msgPadding.right();
		if (via()) {
			_namew += st::msgServiceFont->spacew + via()->maxWidth;
		}
		if (_namew > _maxw) _maxw = _namew;
	}
}

void HistoryForwarded::fwdNameUpdated() const {
	QString fwdName((via() && fwdFrom->isUser()) ? fwdFrom->asUser()->firstName : App::peerName(fwdFrom));
	fwdFromName.setText(st::msgServiceNameFont, fwdName, _textNameOptions);
	if (via()) {
		int32 l = 0, w = 0;
		countPositionAndSize(l, w);
		via()->resize(w - st::msgPadding.left() - st::msgPadding.right() - fromWidth - fwdFromName.maxWidth() - st::msgServiceFont->spacew);
	}
}

void HistoryForwarded::draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const {
	if (drawBubble() && fwdFrom->nameVersion > fwdFromVersion) {
		fwdNameUpdated();
		fwdFromVersion = fwdFrom->nameVersion;
	}
	HistoryMessage::draw(p, r, selection, ms);
}

void HistoryForwarded::drawForwardedFrom(Painter &p, int32 x, int32 y, int32 w, bool selected) const {
	style::font serviceFont(st::msgServiceFont), serviceName(st::msgServiceNameFont);

	bool outbg = out() && !fromChannel();
	p.setPen((selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg))->p);
	p.setFont(serviceFont);

	if (via() && w > fromWidth + fwdFromName.maxWidth() + serviceFont->spacew) {
		p.drawText(x, y + serviceFont->ascent, lang(lng_forwarded_from));

		p.setFont(serviceName);
		fwdFromName.draw(p, x + fromWidth, y, w - fromWidth);

		p.drawText(x + fromWidth + fwdFromName.maxWidth() + serviceFont->spacew, y + serviceFont->ascent, via()->text);
	} else if (w > fromWidth) {
		p.drawText(x, y + serviceFont->ascent, lang(lng_forwarded_from));

		p.setFont(serviceName);
		fwdFromName.drawElided(p, x + fromWidth, y, w - fromWidth);
	} else {
		p.drawText(x, y + serviceFont->ascent, serviceFont->elided(lang(lng_forwarded_from), w));
	}
}

void HistoryForwarded::drawMessageText(Painter &p, QRect trect, uint32 selection) const {
	if (displayForwardedFrom()) {
		drawForwardedFrom(p, trect.x(), trect.y(), trect.width(), (selection == FullSelection));
		trect.setY(trect.y() + st::msgServiceNameFont->height);
	}
	HistoryMessage::drawMessageText(p, trect, selection);
}

int32 HistoryForwarded::resize(int32 width) {
	HistoryMessage::resize(width);
	if (drawBubble()) {
		if (displayForwardedFrom()) {
			if (emptyText() && !displayFromName()) {
				_height += st::msgPadding.top() + st::msgServiceNameFont->height + st::mediaHeaderSkip;
			} else {
				_height += st::msgServiceNameFont->height;
			}
			if (via()) {
				int32 l = 0, w = 0;
				countPositionAndSize(l, w);
				via()->resize(w - st::msgPadding.left() - st::msgPadding.right() - fromWidth - fwdFromName.maxWidth() - st::msgServiceFont->spacew);
			}
		}
	}
	return _height;
}

bool HistoryForwarded::hasPoint(int32 x, int32 y) const {
	if (drawBubble() && displayForwardedFrom()) {
		int32 left = 0, width = 0;
		countPositionAndSize(left, width);
		if (width < 1) return false;

		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		return r.contains(x, y);
	}
	return HistoryMessage::hasPoint(x, y);
}

void HistoryForwarded::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const {
	lnk = TextLinkPtr();
	state = HistoryDefaultCursorState;

	if (drawBubble() && displayForwardedFrom()) {
		int32 left = 0, width = 0;
		countPositionAndSize(left, width);
		if (displayFromPhoto()) {
			int32 photoleft = left + ((!fromChannel() && out()) ? (width + (st::msgPhotoSkip - st::msgPhotoSize)) : (-st::msgPhotoSkip));
			if (x >= photoleft && x < photoleft + st::msgPhotoSize) {
				return HistoryMessage::getState(lnk, state, x, y);
			}
		}
		if (width < 1) return;

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
	if (drawBubble() && displayForwardedFrom()) {
		realr.setHeight(r.height() - st::msgServiceNameFont->height);
	}
	HistoryMessage::getStateFromMessageText(lnk, state, x, y, realr);
}

void HistoryForwarded::getForwardedState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 w) const {
	state = HistoryDefaultCursorState;
	if (x >= fromWidth && x < w && x < fromWidth + fwdFromName.maxWidth()) {
		lnk = fwdFrom->lnk;
	} else if (via() && x >= fromWidth + fwdFromName.maxWidth() + st::msgServiceFont->spacew && x < w && x < fromWidth + fwdFromName.maxWidth() + st::msgServiceFont->spacew + via()->maxWidth) {
		lnk = via()->lnk;
	} else {
		lnk = TextLinkPtr();
	}
}

void HistoryForwarded::getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y) const {
	symbol = 0;
	after = false;
	upon = false;

	if (drawBubble() && displayForwardedFrom()) {
		int32 left = 0, width = 0;
		countPositionAndSize(left, width);
		if (width < 1) return;

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
, _replyToVia(0) {
	if (!updateReplyTo() && App::api()) {
		App::api()->requestReplyTo(this, history->peer->asChannel(), replyToMsgId);
	}
}

HistoryReply::HistoryReply(History *history, HistoryBlock *block, MsgId msgId, int32 flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc, const QString &caption)
: HistoryMessage(history, block, msgId, flags, viaBotId, date, from, doc, caption)
, replyToMsgId(replyTo)
, replyToMsg(0)
, replyToVersion(0)
, _maxReplyWidth(0)
, _replyToVia(0) {
	if (!updateReplyTo() && App::api()) {
		App::api()->requestReplyTo(this, history->peer->asChannel(), replyToMsgId);
	}
}

HistoryReply::HistoryReply(History *history, HistoryBlock *block, MsgId msgId, int32 flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, PhotoData *photo, const QString &caption)
: HistoryMessage(history, block, msgId, flags, viaBotId, date, from, photo, caption)
, replyToMsgId(replyTo)
, replyToMsg(0)
, replyToVersion(0)
, _maxReplyWidth(0)
, _replyToVia(0) {
	if (!updateReplyTo() && App::api()) {
		App::api()->requestReplyTo(this, history->peer->asChannel(), replyToMsgId);
	}
	replyToNameUpdated();
}

QString HistoryReply::selectedText(uint32 selection) const {
	if (selection != FullSelection || !replyToMsg) return HistoryMessage::selectedText(selection);
	QString result, original = HistoryMessage::selectedText(selection);
	result.reserve(lang(lng_in_reply_to).size() + replyToMsg->from()->name.size() + 4 + original.size());
	result.append('[').append(lang(lng_in_reply_to)).append(' ').append(replyToMsg->from()->name).append(qsl("]\n")).append(original);
	return result;
}

void HistoryReply::initDimensions() {
	replyToNameUpdated();
	HistoryMessage::initDimensions();
	if (!_media) {
		int32 replyw = st::msgPadding.left() + _maxReplyWidth - st::msgReplyPadding.left() - st::msgReplyPadding.right() + st::msgPadding.right();
		if (replyToVia()) {
			replyw += st::msgServiceFont->spacew + replyToVia()->maxWidth;
		}
		if (replyw > _maxw) _maxw = replyw;
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
		if (!replyToMsg->toHistoryForwarded()) {
			if (UserData *bot = replyToMsg->viaBot()) {
				_replyToVia = new HistoryMessageVia(peerToUser(bot->id));
			}
		}
	} else if (force) {
		replyToMsgId = 0;
	}
	if (force) {
		initDimensions();
		Notify::historyItemResized(this);
	}
	return (replyToMsg || !replyToMsgId);
}

void HistoryReply::replyToNameUpdated() const {
	if (replyToMsg) {
		QString name = (replyToVia() && replyToMsg->from()->isUser()) ? replyToMsg->from()->asUser()->firstName : App::peerName(replyToMsg->from());
		replyToName.setText(st::msgServiceNameFont, name, _textNameOptions);
		replyToVersion = replyToMsg->from()->nameVersion;
		bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
		int32 previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		int32 w = replyToName.maxWidth();
		if (replyToVia()) {
			w += st::msgServiceFont->spacew + replyToVia()->maxWidth;
		}

		_maxReplyWidth = previewSkip + qMax(w, qMin(replyToText.maxWidth(), 4 * w));
	} else {
		_maxReplyWidth = st::msgDateFont->width(lang(replyToMsgId ? lng_profile_loading : lng_deleted_message));
	}
	_maxReplyWidth = st::msgReplyPadding.left() + st::msgReplyBarSkip + _maxReplyWidth + st::msgReplyPadding.right();
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
		delete _replyToVia;
		_replyToVia = 0;
		replyToMsg = newItem;
		if (!newItem) {
			replyToMsgId = 0;
			initDimensions();
		} else if (!replyToMsg->toHistoryForwarded()) {
			if (UserData *bot = replyToMsg->viaBot()) {
				_replyToVia = new HistoryMessageVia(peerToUser(bot->id));
			}
		}
	}
}

void HistoryReply::draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const {
	if (replyToMsg && replyToMsg->from()->nameVersion > replyToVersion) {
		replyToNameUpdated();
	}
	HistoryMessage::draw(p, r, selection, ms);
}

void HistoryReply::drawReplyTo(Painter &p, int32 x, int32 y, int32 w, bool selected, bool likeService) const {
	style::color bar;
	bool outbg = out() && !fromChannel();
	if (likeService) {
		bar = st::white;
	} else {
		bar = (selected ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor));
	}
	QRect rbar(rtlrect(x + st::msgReplyBarPos.x(), y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height(), w + 2 * x));
	p.fillRect(rbar, bar);

	if (w > st::msgReplyBarSkip) {
		if (replyToMsg) {
			bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
			int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;

			if (hasPreview) {
				ImagePtr replyPreview = replyToMsg->getMedia()->replyPreview();
				if (!replyPreview->isNull()) {
					QRect to(rtlrect(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height(), w + 2 * x));
					p.drawPixmap(to.x(), to.y(), replyPreview->pixSingle(replyPreview->width() / cIntRetinaFactor(), replyPreview->height() / cIntRetinaFactor(), to.width(), to.height()));
					if (selected) {
						App::roundRect(p, to, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
					}
				}
			}
			if (w > st::msgReplyBarSkip + previewSkip) {
				if (likeService) {
					p.setPen(st::white);
				} else {
					p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
				}
				replyToName.drawLeftElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top(), w - st::msgReplyBarSkip - previewSkip, w + 2 * x);
				if (replyToVia() && w > st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew) {
					p.setFont(st::msgServiceFont);
					p.drawText(x + st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew, y + st::msgReplyPadding.top() + st::msgServiceFont->ascent, replyToVia()->text);
				}

				HistoryMessage *replyToAsMsg = replyToMsg->toHistoryMessage();
				if (likeService) {
				} else if ((replyToAsMsg && replyToAsMsg->emptyText()) || replyToMsg->serviceMsg()) {
					style::color date(outbg ? (selected ? st::msgOutDateFgSelected : st::msgOutDateFg) : (selected ? st::msgInDateFgSelected : st::msgInDateFg));
					p.setPen(date);
				} else {
					p.setPen(st::msgColor);
				}
				replyToText.drawLeftElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top() + st::msgServiceNameFont->height, w - st::msgReplyBarSkip - previewSkip, w + 2 * x);
			}
		} else {
			p.setFont(st::msgDateFont);
			style::color date(outbg ? (selected ? st::msgOutDateFgSelected : st::msgOutDateFg) : (selected ? st::msgInDateFgSelected : st::msgInDateFg));
			p.setPen(likeService ? st::white : date);
			p.drawTextLeft(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2, w + 2 * x, st::msgDateFont->elided(lang(replyToMsgId ? lng_profile_loading : lng_deleted_message), w - st::msgReplyBarSkip));
		}
	}
}

void HistoryReply::drawMessageText(Painter &p, QRect trect, uint32 selection) const {
	int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();

	drawReplyTo(p, trect.x(), trect.y(), trect.width(), (selection == FullSelection));

	trect.setY(trect.y() + h);
	HistoryMessage::drawMessageText(p, trect, selection);
}

int32 HistoryReply::resize(int32 width) {
	HistoryMessage::resize(width);

	if (drawBubble()) {
		if (emptyText() && !displayFromName() && !via()) {
			_height += st::msgPadding.top() + st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom() + st::mediaHeaderSkip;
		} else {
			_height += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
		}
		if (replyToVia()) {
			bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
			int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
			replyToVia()->resize(width - st::msgPadding.left() - st::msgPadding.right() - st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew);
		}
	}
	return _height;
}

void HistoryReply::resizeVia(int32 w) const {
	if (!replyToVia()) return;

	bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
	int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
	replyToVia()->resize(w - st::msgReplyBarSkip - previewSkip - replyToName.maxWidth() - st::msgServiceFont->spacew);
}

bool HistoryReply::hasPoint(int32 x, int32 y) const {
	if (drawBubble()) {
		int32 left = 0, width = 0;
		countPositionAndSize(left, width);
		if (width < 1) return false;

		QRect r(left, st::msgMargin.top(), width, _height - st::msgMargin.top() - st::msgMargin.bottom());
		return r.contains(x, y);
	}
	return HistoryMessage::hasPoint(x, y);
}

void HistoryReply::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const {
	lnk = TextLinkPtr();
	state = HistoryDefaultCursorState;

	if (drawBubble()) {
		int32 left = 0, width = 0;
		countPositionAndSize(left, width);
		if (displayFromPhoto()) {
			int32 photoleft = left + ((!fromChannel() && out()) ? (width + (st::msgPhotoSkip - st::msgPhotoSize)) : (-st::msgPhotoSkip));
			if (x >= photoleft && x < photoleft + st::msgPhotoSize) {
				return HistoryMessage::getState(lnk, state, x, y);
			}
		}
		if (width < 1) return;

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

	if (drawBubble()) {
		int32 left = 0, width = 0;
		countPositionAndSize(left, width);
		if (width < 1) return;

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
	deleteAndMark(_replyToVia);
}

void HistoryServiceMsg::setMessageByAction(const MTPmessageAction &action) {
	QList<TextLinkPtr> links;
	LangString text = lang(lng_message_empty);
	QString from = textcmdLink(1, _from->name);

	switch (action.type()) {
	case mtpc_messageActionChatAddUser: {
		const MTPDmessageActionChatAddUser &d(action.c_messageActionChatAddUser());
		const QVector<MTPint> &v(d.vusers.c_vector().v);
		bool foundSelf = false;
		for (int32 i = 0, l = v.size(); i < l; ++i) {
			if (v.at(i).v == MTP::authedId()) {
				foundSelf = true;
				break;
			}
		}
		if (v.size() == 1) {
			UserData *u = App::user(peerFromUser(v.at(0)));
			if (u == _from) {
				text = lng_action_user_joined(lt_from, from);
			} else {
				links.push_back(TextLinkPtr(new PeerLink(u)));
				text = lng_action_add_user(lt_from, from, lt_user, textcmdLink(2, u->name));
			}
		} else if (v.isEmpty()) {
			text = lng_action_add_user(lt_from, from, lt_user, "somebody");
		} else {
			for (int32 i = 0, l = v.size(); i < l; ++i) {
				UserData *u = App::user(peerFromUser(v.at(i)));
				QString linkText = textcmdLink(i + 2, u->name);
				if (i == 0) {
					text = linkText;
				} else if (i + 1 < l) {
					text = lng_action_add_users_and_one(lt_accumulated, text, lt_user, linkText);
				} else {
					text = lng_action_add_users_and_last(lt_accumulated, text, lt_user, linkText);
				}
				links.push_back(TextLinkPtr(new PeerLink(u)));
			}
			text = lng_action_add_users_many(lt_from, from, lt_users, text);
		}
		if (foundSelf) {
			if (unread() && history()->peer->isChat() && !history()->peer->asChat()->inviterForSpamReport && _from->isUser()) {
				history()->peer->asChat()->inviterForSpamReport = peerToUser(_from->id);
			}
			if (history()->peer->isMegagroup()) {
				history()->peer->asChannel()->mgInfo->joinedMessageFound = true;
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
		if (_from->isSelf() && history()->peer->isMegagroup()) {
			history()->peer->asChannel()->mgInfo->joinedMessageFound = true;
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
		if (fromChannel()) {
			text = lng_action_created_channel(lt_title, textClean(qs(d.vtitle)));
		} else {
			text = lng_action_created_chat(lt_from, from, lt_title, textClean(qs(d.vtitle)));
		}
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
			links.push_back(TextLinkPtr(new PeerLink(u)));
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

	case mtpc_messageActionChatMigrateTo: {
		_flags |= MTPDmessage_flag_IS_GROUP_MIGRATE;
		const MTPDmessageActionChatMigrateTo &d(action.c_messageActionChatMigrateTo());
		if (true/*PeerData *channel = App::peerLoaded(peerFromChannel(d.vchannel_id))*/) {
			text = lang(lng_action_group_migrate);
		} else {
			text = lang(lng_contacts_loading);
		}
	} break;

	case mtpc_messageActionChannelMigrateFrom: {
		_flags |= MTPDmessage_flag_IS_GROUP_MIGRATE;
		const MTPDmessageActionChannelMigrateFrom &d(action.c_messageActionChannelMigrateFrom());
		if (true/*PeerData *chat = App::peerLoaded(peerFromChannel(d.vchat_id))*/) {
			text = lang(lng_action_group_migrate);
		} else {
			text = lang(lng_contacts_loading);
		}
	} break;

	default: from = QString(); break;
	}

	textstyleSet(&st::serviceTextStyle);
	_text.setText(st::msgServiceFont, text, _historySrvOptions);
	textstyleRestore();
	if (!from.isEmpty()) {
		_text.setLink(1, TextLinkPtr(new PeerLink(_from)));
	}
	for (int32 i = 0, l = links.size(); i < l; ++i) {
		_text.setLink(i + 2, links.at(i));
	}
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
	uint16 selectedFrom = (selection == FullSelection) ? 0 : (selection >> 16) & 0xFFFF;
	uint16 selectedTo = (selection == FullSelection) ? 0xFFFF : (selection & 0xFFFF);
	return _text.original(selectedFrom, selectedTo);
}

QString HistoryServiceMsg::inDialogsText() const {
	return _text.original(0, 0xFFFF, Text::ExpandLinksNone);
}

QString HistoryServiceMsg::inReplyText() const {
	QString result = HistoryServiceMsg::inDialogsText();
	return result.trimmed().startsWith(from()->name) ? result.trimmed().mid(from()->name.size()).trimmed() : result;
}

void HistoryServiceMsg::setServiceText(const QString &text) {
	textstyleSet(&st::serviceTextStyle);
	_text.setText(st::msgServiceFont, text, _historySrvOptions);
	textstyleRestore();
	initDimensions();
}

void HistoryServiceMsg::draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const {
	uint64 animms = App::main() ? App::main()->animActiveTimeStart(this) : 0;
	if (animms > 0 && animms <= ms) {
		animms = ms - animms;
		if (animms > st::activeFadeInDuration + st::activeFadeOutDuration) {
			App::main()->stopAnimActive();
		} else {
			textstyleSet(&st::inTextStyle);
			float64 dt = (animms > st::activeFadeInDuration) ? (1 - (animms - st::activeFadeInDuration) / float64(st::activeFadeOutDuration)) : (animms / float64(st::activeFadeInDuration));
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
		int32 left = st::msgServiceMargin.left() + (width - _media->maxWidth()) / 2, top = st::msgServiceMargin.top() + height + st::msgServiceMargin.top();
		p.translate(left, top);
		_media->draw(p, this, r.translated(-left, -top), selection == FullSelection, ms);
		p.restore();
	}

	QRect trect(QRect(left, st::msgServiceMargin.top(), width, height).marginsAdded(-st::msgServicePadding));

	if (width > _maxw) {
		left += (width - _maxw) / 2;
		width = _maxw;
	}
	App::roundRect(p, left, st::msgServiceMargin.top(), width, height, App::msgServiceBg(), (selection == FullSelection) ? ServiceSelectedCorners : ServiceCorners);

	p.setBrush(Qt::NoBrush);
	p.setPen(st::msgServiceColor->p);
	p.setFont(st::msgServiceFont->f);
	uint16 selectedFrom = (selection == FullSelection) ? 0 : (selection >> 16) & 0xFFFF;
	uint16 selectedTo = (selection == FullSelection) ? 0 : selection & 0xFFFF;
	_text.draw(p, trect.x(), trect.y(), trect.width(), Qt::AlignCenter, 0, -1, selectedFrom, selectedTo);
	textstyleRestore();
}

int32 HistoryServiceMsg::resize(int32 width) {
	width -= st::msgServiceMargin.left() + st::msgServiceMargin.left(); // two small margins
	if (width < st::msgServicePadding.left() + st::msgServicePadding.right() + 1) width = st::msgServicePadding.left() + st::msgServicePadding.right() + 1;

	int32 nwidth = qMax(width - st::msgPadding.left() - st::msgPadding.right(), 0);
	if (nwidth != _textWidth) {
		_textWidth = nwidth;
		textstyleSet(&st::serviceTextStyle);
		_textHeight = _text.countHeight(nwidth);
		textstyleRestore();
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
		textstyleSet(&st::serviceTextStyle);
		bool inText = false;
		_text.getState(lnk, inText, x - trect.x(), y - trect.y(), trect.width(), Qt::AlignCenter);
		textstyleRestore();
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
	textstyleSet(&st::serviceTextStyle);
	_text.getSymbol(symbol, after, upon, x - trect.x(), y - trect.y(), trect.width(), Qt::AlignCenter);
	textstyleRestore();
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
    QString msg = _text.original();
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
HistoryServiceMsg(history, block, clientMsgId(), QDateTime(date), langDayOfMonthFull(date)) {
}

void HistoryDateMsg::setDate(const QDateTime &date) {
	if (this->date.date() != date.date()) {
		setServiceText(langDayOfMonthFull(date.date()));
	}
	HistoryServiceMsg::setDate(date);
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

void HistoryCollapse::draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const {
}

void HistoryCollapse::getState(TextLinkPtr &lnk, HistoryCursorState &state, int32 x, int32 y) const {
	lnk = TextLinkPtr();
	state = HistoryDefaultCursorState;
}

HistoryJoined::HistoryJoined(History *history, HistoryBlock *block, const QDateTime &inviteDate, UserData *inviter, int32 flags) :
HistoryServiceMsg(history, block, clientMsgId(), inviteDate, QString(), flags) {
	textstyleSet(&st::serviceTextStyle);
	if (peerToUser(inviter->id) == MTP::authedId()) {
		_text.setText(st::msgServiceFont, lang(history->isMegagroup() ? lng_action_you_joined_group : lng_action_you_joined), _historySrvOptions);
	} else {
		_text.setText(st::msgServiceFont, history->isMegagroup() ? lng_action_add_you_group(lt_from, textcmdLink(1, inviter->name)) : lng_action_add_you(lt_from, textcmdLink(1, inviter->name)), _historySrvOptions);
		_text.setLink(1, TextLinkPtr(new PeerLink(inviter)));
	}
	textstyleRestore();
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

void HistoryUnreadBar::draw(Painter &p, const QRect &r, uint32 selection, uint64 ms) const {
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

