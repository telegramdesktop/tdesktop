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
#include "dialogs/dialogs_layout.h"

#include "dialogs/dialogs_list.h"
#include "lang.h"

namespace Dialogs {
namespace Layout {

void RowPainter::paint(Painter &p, const Row *row, int w, bool active, bool selected, bool onlyBackground) {
	QRect fullRect(0, 0, w, st::dlgHeight);
	p.fillRect(fullRect, active ? st::dlgActiveBG : (selected ? st::dlgHoverBG : st::dlgBG));
	if (onlyBackground) return;

	History *history = row->history();
	PeerData *userpicPeer = (history->peer->migrateTo() ? history->peer->migrateTo() : history->peer);
	userpicPeer->paintUserpicLeft(p, st::dlgPhotoSize, st::dlgPaddingHor, st::dlgPaddingVer, w);

	int32 nameleft = st::dlgPaddingHor + st::dlgPhotoSize + st::dlgPhotoPadding;
	int32 namewidth = w - nameleft - st::dlgPaddingHor;
	QRect rectForName(nameleft, st::dlgPaddingVer + st::dlgNameTop, namewidth, st::msgNameFont->height);

	// draw chat icon
	if (history->peer->isChat() || history->peer->isMegagroup()) {
		p.drawSprite(QPoint(rectForName.left() + st::dlgChatImgPos.x(), rectForName.top() + st::dlgChatImgPos.y()), (active ? st::dlgActiveChatImg : st::dlgChatImg));
		rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
	} else if (history->peer->isChannel()) {
		p.drawSprite(QPoint(rectForName.left() + st::dlgChannelImgPos.x(), rectForName.top() + st::dlgChannelImgPos.y()), (active ? st::dlgActiveChannelImg : st::dlgChannelImg));
		rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
	}

	HistoryItem *last = history->lastMsg;
	if (!last) {
		p.setFont(st::dlgHistFont);
		p.setPen(active ? st::dlgActiveColor : st::dlgSystemColor);
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
		p.setPen((active ? st::dlgActiveDateColor : st::dlgDateColor)->p);
		p.drawText(rectForName.left() + rectForName.width() + st::dlgDateSkip, rectForName.top() + st::msgNameFont->height - st::msgDateFont->descent, dt);

		// draw check
		if (last->needCheck()) {
			const style::sprite *check;
			if (last->id > 0) {
				if (last->unread()) {
					check = active ? &st::dlgActiveCheckImg : &st::dlgCheckImg;
				} else {
					check = active ? &st::dlgActiveDblCheckImg : &st::dlgDblCheckImg;
				}
			} else {
				check = active ? &st::dlgActiveSendImg : &st::dlgSendImg;
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
			p.setBrush(active ? (history->mute ? st::dlgActiveUnreadMutedBG : st::dlgActiveUnreadBG) : (history->mute ? st::dlgUnreadMutedBG : st::dlgUnreadBG));
			p.setPen(Qt::NoPen);
			p.drawRoundedRect(unreadRectLeft, unreadRectTop, unreadRectWidth, unreadRectHeight, st::dlgUnreadRadius, st::dlgUnreadRadius);
			p.setFont(st::dlgUnreadFont->f);
			p.setPen(active ? st::dlgActiveUnreadColor : st::dlgUnreadColor);
			p.drawText(unreadRectLeft + st::dlgUnreadPaddingHor, unreadRectTop + st::dlgUnreadPaddingVer + st::dlgUnreadFont->ascent, unreadStr);
		}
		if (history->typing.isEmpty() && history->sendActions.isEmpty()) {
			last->drawInDialog(p, QRect(nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, lastWidth, st::dlgFont->height), active, history->textCachedFor, history->lastItemTextCache);
		} else {
			p.setPen(active ? st::dlgActiveColor : st::dlgSystemColor);
			history->typingText.drawElided(p, nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, lastWidth);
		}
	}

	if (history->peer->isUser() && history->peer->isVerified()) {
		rectForName.setWidth(rectForName.width() - st::verifiedCheck.pxWidth() - st::verifiedCheckPos.x());
		p.drawSprite(rectForName.topLeft() + QPoint(qMin(history->peer->dialogName().maxWidth(), rectForName.width()), 0) + st::verifiedCheckPos, (active ? st::verifiedCheckInv : st::verifiedCheck));
	}

	p.setPen(active ? st::dlgActiveColor : st::dlgNameColor);
	history->peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

void RowPainter::paint(Painter &p, const FakeRow *row, int w, bool active, bool selected, bool onlyBackground) {
	QRect fullRect(0, 0, w, st::dlgHeight);
	p.fillRect(fullRect, (active ? st::dlgActiveBG : (selected ? st::dlgHoverBG : st::dlgBG))->b);
	if (onlyBackground) return;

	auto item = row->item();
	auto history = item->history();
	PeerData *userpicPeer = (history->peer->migrateTo() ? history->peer->migrateTo() : history->peer);
	userpicPeer->paintUserpicLeft(p, st::dlgPhotoSize, st::dlgPaddingHor, st::dlgPaddingVer, w);

	int32 nameleft = st::dlgPaddingHor + st::dlgPhotoSize + st::dlgPhotoPadding;
	int32 namewidth = w - nameleft - st::dlgPaddingHor;
	QRect rectForName(nameleft, st::dlgPaddingVer + st::dlgNameTop, namewidth, st::msgNameFont->height);

	// draw chat icon
	if (history->peer->isChat() || history->peer->isMegagroup()) {
		p.drawSprite(QPoint(rectForName.left() + st::dlgChatImgPos.x(), rectForName.top() + st::dlgChatImgPos.y()), (active ? st::dlgActiveChatImg : st::dlgChatImg));
		rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
	} else if (history->peer->isChannel()) {
		p.drawSprite(QPoint(rectForName.left() + st::dlgChannelImgPos.x(), rectForName.top() + st::dlgChannelImgPos.y()), (active ? st::dlgActiveChannelImg : st::dlgChannelImg));
		rectForName.setLeft(rectForName.left() + st::dlgImgSkip);
	}

	// draw date
	QDateTime now(QDateTime::currentDateTime()), lastTime(item->date);
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
	p.setFont(st::dlgDateFont);
	p.setPen(active ? st::dlgActiveDateColor : st::dlgDateColor);
	p.drawText(rectForName.left() + rectForName.width() + st::dlgDateSkip, rectForName.top() + st::msgNameFont->height - st::msgDateFont->descent, dt);

	// draw check
	if (item->needCheck()) {
		const style::sprite *check;
		if (item->id > 0) {
			if (item->unread()) {
				check = active ? &st::dlgActiveCheckImg : &st::dlgCheckImg;
			} else {
				check = active ? &st::dlgActiveDblCheckImg : &st::dlgDblCheckImg;
			}
		} else {
			check = active ? &st::dlgActiveSendImg : &st::dlgSendImg;
		}
		rectForName.setWidth(rectForName.width() - check->pxWidth() - st::dlgCheckSkip);
		p.drawSprite(QPoint(rectForName.left() + rectForName.width() + st::dlgCheckLeft, rectForName.top() + st::dlgCheckTop), *check);
	}

	// draw unread
	int32 lastWidth = namewidth;
	item->drawInDialog(p, QRect(nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, lastWidth, st::dlgFont->height), active, row->_cacheFor, row->_cache);

	if (history->peer->isUser() && history->peer->isVerified()) {
		rectForName.setWidth(rectForName.width() - st::verifiedCheck.pxWidth() - st::verifiedCheckPos.x());
		p.drawSprite(rectForName.topLeft() + QPoint(qMin(history->peer->dialogName().maxWidth(), rectForName.width()), 0) + st::verifiedCheckPos, (active ? st::verifiedCheckInv : st::verifiedCheck));
	}

	p.setPen(active ? st::dlgActiveColor : st::dlgNameColor);
	history->peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());

}

} // namespace Layout
} // namespace Dialogs
