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

namespace {

template <typename PaintItemCallback>
void paintRow(Painter &p, History *history, HistoryItem *item, int w, bool active, bool selected, bool onlyBackground, PaintItemCallback paintItemCallback) {
	QRect fullRect(0, 0, w, st::dlgHeight);
	p.fillRect(fullRect, active ? st::dlgActiveBG : (selected ? st::dlgHoverBG : st::dlgBG));
	if (onlyBackground) return;

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

	if (!item) {
		p.setFont(st::dlgHistFont);
		p.setPen(active ? st::dlgActiveColor : st::dlgSystemColor);
		if (history->typing.isEmpty() && history->sendActions.isEmpty()) {
			p.drawText(nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgFont->ascent + st::dlgSep, lang(lng_empty_history));
		} else {
			history->typingText.drawElided(p, nameleft, st::dlgPaddingVer + st::dlgFont->height + st::dlgSep, namewidth);
		}
	} else {
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

		paintItemCallback(nameleft, namewidth, item);
	}

	if (history->peer->isUser() && history->peer->isVerified()) {
		rectForName.setWidth(rectForName.width() - st::verifiedCheck.pxWidth() - st::verifiedCheckPos.x());
		p.drawSprite(rectForName.topLeft() + QPoint(qMin(history->peer->dialogName().maxWidth(), rectForName.width()), 0) + st::verifiedCheckPos, (active ? st::verifiedCheckInv : st::verifiedCheck));
	}

	p.setPen(active ? st::dlgActiveColor : st::dlgNameColor);
	history->peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

class UnreadBadgeStyle : public StyleSheet {
public:
	QImage circle;
	QPixmap left[4], right[4];
	style::color bg[4] = { st::dlgUnreadBG, st::dlgActiveUnreadBG, st::dlgUnreadMutedBG, st::dlgActiveUnreadMutedBG };
};
StyleSheetPointer<UnreadBadgeStyle> unreadBadgeStyle;

void createCircleMask(int size) {
	if (!unreadBadgeStyle->circle.isNull()) return;

	unreadBadgeStyle->circle = QImage(size, size, QImage::Format::Format_Grayscale8);
	{
		QPainter pcircle(&unreadBadgeStyle->circle);
		pcircle.setRenderHint(QPainter::HighQualityAntialiasing, true);
		pcircle.fillRect(0, 0, size, size, QColor(0, 0, 0));
		pcircle.setPen(Qt::NoPen);
		pcircle.setBrush(QColor(255, 255, 255));
		pcircle.drawEllipse(0, 0, size, size);
	}
	unreadBadgeStyle->circle.setDevicePixelRatio(cRetinaFactor());
}

QImage colorizeCircleHalf(int size, int half, int xoffset, style::color color) {
	auto result = style::colorizeImage(unreadBadgeStyle->circle, color, QRect(xoffset, 0, half, size));
	result.setDevicePixelRatio(cRetinaFactor());
	return result;
}

void paintUnreadBadge(Painter &p, const QRect &rect, bool active, bool muted) {
	int index = (active ? 0x01 : 0x00) | (muted ? 0x02 : 0x00);
	int size = rect.height(), sizehalf = size / 2;

	unreadBadgeStyle.createIfNull();
	style::color bg = unreadBadgeStyle->bg[index];
	if (unreadBadgeStyle->left[index].isNull()) {
		int imgsize = size * cIntRetinaFactor(), imgsizehalf = sizehalf * cIntRetinaFactor();
		createCircleMask(imgsize);
		unreadBadgeStyle->left[index] = QPixmap::fromImage(colorizeCircleHalf(imgsize, imgsizehalf, 0, bg));
		unreadBadgeStyle->right[index] = QPixmap::fromImage(colorizeCircleHalf(imgsize, imgsizehalf, imgsize - imgsizehalf, bg));
	}

	int bar = rect.width() - 2 * sizehalf;
	p.drawPixmap(rect.x(), rect.y(), unreadBadgeStyle->left[index]);
	if (bar) {
		p.fillRect(rect.x() + sizehalf, rect.y(), bar, rect.height(), bg);
	}
	p.drawPixmap(rect.x() + sizehalf + bar, rect.y(), unreadBadgeStyle->right[index]);
}

void paintUnreadCount(Painter &p, const QString &text, int top, int w, bool active, bool muted, int *outAvailableWidth) {
	int unreadWidth = st::dlgUnreadFont->width(text);
	int unreadRectWidth = unreadWidth + 2 * st::dlgUnreadPaddingHor;
	int unreadRectHeight = st::dlgUnreadHeight;
	accumulate_max(unreadRectWidth, unreadRectHeight);

	int unreadRectLeft = w - st::dlgPaddingHor - unreadRectWidth;
	int unreadRectTop =top;
	if (outAvailableWidth) {
		*outAvailableWidth -= unreadRectWidth + st::dlgUnreadPaddingHor;
	}

	paintUnreadBadge(p, QRect(unreadRectLeft, unreadRectTop, unreadRectWidth, unreadRectHeight), active, muted);

	p.setFont(st::dlgUnreadFont);
	p.setPen(active ? st::dlgActiveUnreadColor : st::dlgUnreadColor);
	p.drawText(unreadRectLeft + (unreadRectWidth - unreadWidth) / 2, unreadRectTop + st::dlgUnreadTop + st::dlgUnreadFont->ascent, text);
}

} // namepsace

void RowPainter::paint(Painter &p, const Row *row, int w, bool active, bool selected, bool onlyBackground) {
	auto history = row->history();
	auto item = history->lastMsg;
	paintRow(p, history, item, w, active, selected, onlyBackground, [&p, w, active, history](int nameleft, int namewidth, HistoryItem *item) {
		int32 unread = history->unreadCount();
		if (history->peer->migrateFrom()) {
			if (History *h = App::historyLoaded(history->peer->migrateFrom()->id)) {
				unread += h->unreadCount();
			}
		}
		int availableWidth = namewidth;
		int texttop = st::dlgPaddingVer + st::dlgFont->height + st::dlgSep;
		if (unread) {
			int unreadTop = texttop + st::dlgHistFont->ascent - st::dlgUnreadFont->ascent - st::dlgUnreadTop;
			paintUnreadCount(p, QString::number(unread), unreadTop, w, active, history->mute(), &availableWidth);
		}
		if (history->typing.isEmpty() && history->sendActions.isEmpty()) {
			item->drawInDialog(p, QRect(nameleft, texttop, availableWidth, st::dlgFont->height), active, history->textCachedFor, history->lastItemTextCache);
		} else {
			p.setPen(active ? st::dlgActiveColor : st::dlgSystemColor);
			history->typingText.drawElided(p, nameleft, texttop, availableWidth);
		}
	});
}

void RowPainter::paint(Painter &p, const FakeRow *row, int w, bool active, bool selected, bool onlyBackground) {
	auto item = row->item();
	auto history = item->history();
	paintRow(p, history, item, w, active, selected, onlyBackground, [&p, row, active](int nameleft, int namewidth, HistoryItem *item) {
		int lastWidth = namewidth, texttop = st::dlgPaddingVer + st::dlgFont->height + st::dlgSep;
		item->drawInDialog(p, QRect(nameleft, texttop, lastWidth, st::dlgFont->height), active, row->_cacheFor, row->_cache);
	});
}

void paintImportantSwitch(Painter &p, Mode current, int w, bool selected, bool onlyBackground) {
	p.fillRect(0, 0, w, st::dlgImportantHeight, selected ? st::dlgHoverBG : st::white);
	if (onlyBackground) {
		return;
	}

	p.setFont(st::semiboldFont);
	p.setPen(st::black);

	int unreadTop = (st::dlgImportantHeight - st::dlgUnreadHeight) / 2;
	bool mutedHidden = (current == Dialogs::Mode::Important);
	QString text = mutedHidden ? qsl("Show all chats") : qsl("Hide muted chats");
	int textBaseline = unreadTop + st::dlgUnreadTop + st::dlgUnreadFont->ascent;
	p.drawText(st::dlgPaddingHor, textBaseline, text);

	if (mutedHidden) {
		if (int32 unread = App::histories().unreadMutedCount()) {
			paintUnreadCount(p, QString::number(unread), unreadTop, w, false, true, nullptr);
		}
	}
}

namespace {

using StyleSheets = OrderedSet<StyleSheet**>;
NeverFreedPointer<StyleSheets> styleSheets;

}

namespace internal {

void registerStyleSheet(StyleSheet **p) {
	styleSheets.makeIfNull();
	styleSheets->insert(p);
}

} // namespace internal

void clearStyleSheets() {
	if (!styleSheets) return;
	for (auto &p : *styleSheets) {
		delete (*p);
		*p = nullptr;
	}
	styleSheets.clear();
}

} // namespace Layout
} // namespace Dialogs
