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

#include "data/data_abstract_structure.h"
#include "data/data_drafts.h"
#include "dialogs/dialogs_list.h"
#include "styles/style_dialogs.h"
#include "localstorage.h"
#include "lang.h"

namespace Dialogs {
namespace Layout {

namespace {

void paintRowDate(Painter &p, const QDateTime &date, QRect &rectForName, bool active) {
	QDateTime now(QDateTime::currentDateTime()), lastTime(date);
	QDate nowDate(now.date()), lastDate(lastTime.date());
	QString dt;
	if (lastDate == nowDate) {
		dt = lastTime.toString(cTimeFormat());
	} else if (lastDate.year() == nowDate.year() && lastDate.weekNumber() == nowDate.weekNumber()) {
		dt = langDayOfWeek(lastDate);
	} else {
		dt = lastDate.toString(qsl("d.MM.yy"));
	}
	int32 dtWidth = st::dialogsDateFont->width(dt);
	rectForName.setWidth(rectForName.width() - dtWidth - st::dialogsDateSkip);
	p.setFont(st::dialogsDateFont);
	p.setPen(active ? st::dialogsDateFgActive : st::dialogsDateFg);
	p.drawText(rectForName.left() + rectForName.width() + st::dialogsDateSkip, rectForName.top() + st::msgNameFont->height - st::msgDateFont->descent, dt);
}

template <typename PaintItemCallback>
void paintRow(Painter &p, History *history, HistoryItem *item, Data::Draft *draft, QDateTime date, int w, bool active, bool selected, bool onlyBackground, PaintItemCallback paintItemCallback) {
	QRect fullRect(0, 0, w, st::dialogsRowHeight);
	p.fillRect(fullRect, active ? st::dialogsBgActive : (selected ? st::dialogsBgOver : st::dialogsBg));
	if (onlyBackground) return;

	PeerData *userpicPeer = (history->peer->migrateTo() ? history->peer->migrateTo() : history->peer);
	userpicPeer->paintUserpicLeft(p, st::dialogsPhotoSize, st::dialogsPadding.x(), st::dialogsPadding.y(), w);

	int32 nameleft = st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPhotoPadding;
	int32 namewidth = w - nameleft - st::dialogsPadding.x();
	QRect rectForName(nameleft, st::dialogsPadding.y() + st::dialogsNameTop, namewidth, st::msgNameFont->height);

	// draw chat icon
	if (history->peer->isChat() || history->peer->isMegagroup()) {
		p.drawSprite(QPoint(rectForName.left() + st::dialogsChatImgPos.x(), rectForName.top() + st::dialogsChatImgPos.y()), (active ? st::dlgActiveChatImg : st::dlgChatImg));
		rectForName.setLeft(rectForName.left() + st::dialogsImgSkip);
	} else if (history->peer->isChannel()) {
		p.drawSprite(QPoint(rectForName.left() + st::dialogsChannelImgPos.x(), rectForName.top() + st::dialogsChannelImgPos.y()), (active ? st::dlgActiveChannelImg : st::dlgChannelImg));
		rectForName.setLeft(rectForName.left() + st::dialogsImgSkip);
	}

	int texttop = st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip;
	if (draft) {
		paintRowDate(p, date, rectForName, active);

		// draw check
		if (draft->saveRequestId) {
			auto check = active ? &st::dlgActiveSendImg : &st::dlgSendImg;
			rectForName.setWidth(rectForName.width() - check->pxWidth() - st::dialogsCheckSkip);
			p.drawSprite(QPoint(rectForName.left() + rectForName.width() + st::dialogsCheckLeft, rectForName.top() + st::dialogsCheckTop), *check);
		}

		p.setFont(st::dialogsTextFont);
		p.setPen(active ? st::dialogsTextFgActive : st::dialogsTextFgService);
		if (history->typing.isEmpty() && history->sendActions.isEmpty()) {
			if (history->cloudDraftTextCache.isEmpty()) {
				auto draftWrapped = textcmdLink(1, lng_dialogs_text_from_wrapped(lt_from, lang(lng_from_draft)));
				auto draftText = lng_dialogs_text_with_from(lt_from_part, draftWrapped, lt_message, textClean(draft->textWithTags.text));
				history->cloudDraftTextCache.setText(st::dialogsTextFont, draftText, _textDlgOptions);
			}
			textstyleSet(&(active ? st::dialogsTextStyleDraftActive : st::dialogsTextStyleDraft));
			p.setFont(st::dialogsTextFont);
			p.setPen(active ? st::dialogsTextFgActive : st::dialogsTextFg);
			history->cloudDraftTextCache.drawElided(p, nameleft, texttop, namewidth, 1);
			textstyleRestore();
		} else {
			history->typingText.drawElided(p, nameleft, texttop, namewidth);
		}
	} else if (!item) {
		p.setFont(st::dialogsTextFont);
		p.setPen(active ? st::dialogsTextFgActive : st::dialogsTextFgService);
		if (history->typing.isEmpty() && history->sendActions.isEmpty()) {
			p.drawText(nameleft, texttop + st::msgNameFont->ascent, lang(lng_empty_history));
		} else {
			history->typingText.drawElided(p, nameleft, texttop, namewidth);
		}
	} else if (!item->isEmpty()) {
		paintRowDate(p, date, rectForName, active);

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
			rectForName.setWidth(rectForName.width() - check->pxWidth() - st::dialogsCheckSkip);
			p.drawSprite(QPoint(rectForName.left() + rectForName.width() + st::dialogsCheckLeft, rectForName.top() + st::dialogsCheckTop), *check);
		}

		paintItemCallback(nameleft, namewidth, item);
	}

	if (history->peer->isUser() && history->peer->isVerified()) {
		rectForName.setWidth(rectForName.width() - st::verifiedCheck.pxWidth() - st::verifiedCheckPos.x());
		p.drawSprite(rectForName.topLeft() + QPoint(qMin(history->peer->dialogName().maxWidth(), rectForName.width()), 0) + st::verifiedCheckPos, (active ? st::verifiedCheckInv : st::verifiedCheck));
	}

	p.setPen(active ? st::dialogsTextFgActive : st::dialogsNameFg);
	history->peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

struct UnreadBadgeSizeData {
	QImage circle;
	QPixmap left[4], right[4];
};
class UnreadBadgeStyleData : public Data::AbstractStructure {
public:
	UnreadBadgeSizeData sizes[UnreadBadgeSizesCount];
	style::color bg[4] = { st::dialogsUnreadBg, st::dialogsUnreadBgActive, st::dialogsUnreadBgMuted, st::dialogsUnreadBgMutedActive };
};
Data::GlobalStructurePointer<UnreadBadgeStyleData> unreadBadgeStyle;

void createCircleMask(UnreadBadgeSizeData *data, int size) {
	if (!data->circle.isNull()) return;

	data->circle = style::createCircleMask(size);
}

QImage colorizeCircleHalf(UnreadBadgeSizeData *data, int size, int half, int xoffset, style::color color) {
	auto result = style::colorizeImage(data->circle, color, QRect(xoffset, 0, half, size));
	result.setDevicePixelRatio(cRetinaFactor());
	return result;
}

} // namepsace

void paintUnreadBadge(Painter &p, const QRect &rect, const UnreadBadgeStyle &st) {
	t_assert(rect.height() == st.size);

	int index = (st.active ? 0x01 : 0x00) | (st.muted ? 0x02 : 0x00);
	int size = st.size, sizehalf = size / 2;

	unreadBadgeStyle.createIfNull();
	auto badgeData = unreadBadgeStyle->sizes;
	if (st.sizeId > 0) {
		t_assert(st.sizeId < UnreadBadgeSizesCount);
		badgeData = &unreadBadgeStyle->sizes[st.sizeId];
	}
	style::color bg = unreadBadgeStyle->bg[index];
	if (badgeData->left[index].isNull()) {
		int imgsize = size * cIntRetinaFactor(), imgsizehalf = sizehalf * cIntRetinaFactor();
		createCircleMask(badgeData, size);
		badgeData->left[index] = App::pixmapFromImageInPlace(colorizeCircleHalf(badgeData, imgsize, imgsizehalf, 0, bg));
		badgeData->right[index] = App::pixmapFromImageInPlace(colorizeCircleHalf(badgeData, imgsize, imgsizehalf, imgsize - imgsizehalf, bg));
	}

	int bar = rect.width() - 2 * sizehalf;
	p.drawPixmap(rect.x(), rect.y(), badgeData->left[index]);
	if (bar) {
		p.fillRect(rect.x() + sizehalf, rect.y(), bar, rect.height(), bg);
	}
	p.drawPixmap(rect.x() + sizehalf + bar, rect.y(), badgeData->right[index]);
}

UnreadBadgeStyle::UnreadBadgeStyle()
: align(style::al_right)
, active(false)
, muted(false)
, size(st::dialogsUnreadHeight)
, sizeId(UnreadBadgeInDialogs)
, font(st::dialogsUnreadFont) {
}

void paintUnreadCount(Painter &p, const QString &text, int x, int y, const UnreadBadgeStyle &st, int *outUnreadWidth) {
	int unreadWidth = st.font->width(text);
	int unreadRectWidth = unreadWidth + 2 * st::dialogsUnreadPadding;
	int unreadRectHeight = st.size;
	accumulate_max(unreadRectWidth, unreadRectHeight);

	int unreadRectLeft = x;
	if ((st.align & Qt::AlignHorizontal_Mask) & style::al_center) {
		unreadRectLeft = (x - unreadRectWidth) / 2;
	} else if ((st.align & Qt::AlignHorizontal_Mask) & style::al_right) {
		unreadRectLeft = x - unreadRectWidth;
	}
	int unreadRectTop = y;
	if (outUnreadWidth) {
		*outUnreadWidth = unreadRectWidth;
	}

	paintUnreadBadge(p, QRect(unreadRectLeft, unreadRectTop, unreadRectWidth, unreadRectHeight), st);

	p.setFont(st.font);
	p.setPen(st.active ? st::dialogsUnreadFgActive : st::dialogsUnreadFg);
	p.drawText(unreadRectLeft + (unreadRectWidth - unreadWidth) / 2, unreadRectTop + st::dialogsUnreadTop + st.font->ascent, text);
}

void RowPainter::paint(Painter &p, const Row *row, int w, bool active, bool selected, bool onlyBackground) {
	auto history = row->history();
	auto item = history->lastMsg;
	auto cloudDraft = history->cloudDraft();
	if (Data::draftIsNull(cloudDraft)) {
		cloudDraft = nullptr;
	}
	auto displayDate = [item, cloudDraft]() {
		if (item) {
			if (cloudDraft) {
				return (item->date > cloudDraft->date) ? item->date : cloudDraft->date;
			}
			return item->date;
		}
		return cloudDraft ? cloudDraft->date : QDateTime();
	};
	int unreadCount = history->unreadCount();
	if (history->peer->migrateFrom()) {
		if (auto migrated = App::historyLoaded(history->peer->migrateFrom()->id)) {
			unreadCount += migrated->unreadCount();
		}
	}

	if (item && cloudDraft && unreadCount > 0) {
		cloudDraft = nullptr; // Draw item, if draft is older.
	}
	paintRow(p, history, item, cloudDraft, displayDate(), w, active, selected, onlyBackground, [&p, w, active, history, unreadCount](int nameleft, int namewidth, HistoryItem *item) {
		int availableWidth = namewidth;
		int texttop = st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip;
		if (unreadCount) {
			auto counter = QString::number(unreadCount);
			auto mutedCounter = history->mute();
			int unreadRight = w - st::dialogsPadding.x();
			int unreadTop = texttop + st::dialogsTextFont->ascent - st::dialogsUnreadFont->ascent - st::dialogsUnreadTop;
			int unreadWidth = 0;

			UnreadBadgeStyle st;
			st.active = active;
			st.muted = history->mute();
			paintUnreadCount(p, counter, unreadRight, unreadTop, st, &unreadWidth);
			availableWidth -= unreadWidth + st::dialogsUnreadPadding;
		}
		if (history->typing.isEmpty() && history->sendActions.isEmpty()) {
			item->drawInDialog(p, QRect(nameleft, texttop, availableWidth, st::dialogsTextFont->height), active, history->textCachedFor, history->lastItemTextCache);
		} else {
			p.setPen(active ? st::dialogsTextFgActive : st::dialogsTextFgService);
			history->typingText.drawElided(p, nameleft, texttop, availableWidth);
		}
	});
}

void RowPainter::paint(Painter &p, const FakeRow *row, int w, bool active, bool selected, bool onlyBackground) {
	auto item = row->item();
	auto history = item->history();
	paintRow(p, history, item, nullptr, item->date, w, active, selected, onlyBackground, [&p, row, active](int nameleft, int namewidth, HistoryItem *item) {
		int lastWidth = namewidth, texttop = st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip;
		item->drawInDialog(p, QRect(nameleft, texttop, lastWidth, st::dialogsTextFont->height), active, row->_cacheFor, row->_cache);
	});
}

void paintImportantSwitch(Painter &p, Mode current, int w, bool selected, bool onlyBackground) {
	p.fillRect(0, 0, w, st::dialogsImportantBarHeight, selected ? st::dialogsBgOver : st::white);
	if (onlyBackground) {
		return;
	}

	p.setFont(st::semiboldFont);
	p.setPen(st::black);

	int unreadTop = (st::dialogsImportantBarHeight - st::dialogsUnreadHeight) / 2;
	bool mutedHidden = (current == Dialogs::Mode::Important);
	QString text = mutedHidden ? qsl("Show all chats") : qsl("Hide muted chats");
	int textBaseline = unreadTop + st::dialogsUnreadTop + st::dialogsUnreadFont->ascent;
	p.drawText(st::dialogsPadding.x(), textBaseline, text);

	if (mutedHidden) {
		if (int32 unread = App::histories().unreadMutedCount()) {
			int unreadRight = w - st::dialogsPadding.x();
			UnreadBadgeStyle st;
			st.muted = true;
			paintUnreadCount(p, QString::number(unread), unreadRight, unreadTop, st, nullptr);
		}
	}
}

} // namespace Layout
} // namespace Dialogs
