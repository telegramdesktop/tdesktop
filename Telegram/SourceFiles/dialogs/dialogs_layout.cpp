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
#include "dialogs/dialogs_layout.h"

#include "data/data_abstract_structure.h"
#include "data/data_drafts.h"
#include "dialogs/dialogs_list.h"
#include "styles/style_dialogs.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"

namespace Dialogs {
namespace Layout {
namespace {

// Show all dates that are in the last 20 hours in time format.
constexpr int kRecentlyInSeconds = 20 * 3600;

void paintRowDate(Painter &p, const QDateTime &date, QRect &rectForName, bool active, bool selected) {
	auto now = QDateTime::currentDateTime();
	auto lastTime = date;
	auto nowDate = now.date();
	auto lastDate = lastTime.date();

	QString dt;
	bool wasSameDay = (lastDate == nowDate);
	bool wasRecently = qAbs(lastTime.secsTo(now)) < kRecentlyInSeconds;
	if (wasSameDay || wasRecently) {
		dt = lastTime.toString(cTimeFormat());
	} else if (lastDate.year() == nowDate.year() && lastDate.weekNumber() == nowDate.weekNumber()) {
		dt = langDayOfWeek(lastDate);
	} else {
		dt = lastDate.toString(qsl("d.MM.yy"));
	}
	int32 dtWidth = st::dialogsDateFont->width(dt);
	rectForName.setWidth(rectForName.width() - dtWidth - st::dialogsDateSkip);
	p.setFont(st::dialogsDateFont);
	p.setPen(active ? st::dialogsDateFgActive : (selected ? st::dialogsDateFgOver : st::dialogsDateFg));
	p.drawText(rectForName.left() + rectForName.width() + st::dialogsDateSkip, rectForName.top() + st::msgNameFont->height - st::msgDateFont->descent, dt);
}

template <typename PaintItemCallback, typename PaintCounterCallback>
void paintRow(Painter &p, const RippleRow *row, History *history, not_null<PeerData*> from, HistoryItem *item, Data::Draft *draft, QDateTime date, int fullWidth, bool active, bool selected, bool onlyBackground, TimeMs ms, PaintItemCallback paintItemCallback, PaintCounterCallback paintCounterCallback) {
	QRect fullRect(0, 0, fullWidth, st::dialogsRowHeight);
	p.fillRect(fullRect, active ? st::dialogsBgActive : (selected ? st::dialogsBgOver : st::dialogsBg));
	row->paintRipple(p, 0, 0, fullWidth, ms, &(active ? st::dialogsRippleBgActive : st::dialogsRippleBg)->c);
	if (onlyBackground) return;

	from->paintUserpicLeft(p, st::dialogsPadding.x(), st::dialogsPadding.y(), fullWidth, st::dialogsPhotoSize);

	auto nameleft = st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPhotoPadding;
	if (fullWidth <= nameleft) {
		if (!draft && item && !item->isEmpty()) {
			paintCounterCallback();
		}
		return;
	}

	auto namewidth = fullWidth - nameleft - st::dialogsPadding.x();
	QRect rectForName(nameleft, st::dialogsPadding.y() + st::dialogsNameTop, namewidth, st::msgNameFont->height);

	if (auto chatTypeIcon = ChatTypeIcon(from, active, selected)) {
		chatTypeIcon->paint(p, rectForName.topLeft(), fullWidth);
		rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
	}

	int texttop = st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip;
	if (draft) {
		paintRowDate(p, date, rectForName, active, selected);

		auto availableWidth = namewidth;
		if (history->isPinnedDialog()) {
			auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
			icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
			availableWidth -= icon.width() + st::dialogsUnreadPadding;
		}

		p.setFont(st::dialogsTextFont);
		auto &color = active ? st::dialogsTextFgServiceActive : (selected ? st::dialogsTextFgServiceOver : st::dialogsTextFgService);
		if (!history->paintSendAction(p, nameleft, texttop, availableWidth, fullWidth, color, ms)) {
			if (history->cloudDraftTextCache.isEmpty()) {
				auto draftWrapped = textcmdLink(1, lng_dialogs_text_from_wrapped(lt_from, lang(lng_from_draft)));
				auto draftText = lng_dialogs_text_with_from(lt_from_part, draftWrapped, lt_message, TextUtilities::Clean(draft->textWithTags.text));
				history->cloudDraftTextCache.setText(st::dialogsTextStyle, draftText, _textDlgOptions);
			}
			p.setPen(active ? st::dialogsTextFgActive : (selected ? st::dialogsTextFgOver : st::dialogsTextFg));
			p.setTextPalette(active ? st::dialogsTextPaletteDraftActive : (selected ? st::dialogsTextPaletteDraftOver : st::dialogsTextPaletteDraft));
			history->cloudDraftTextCache.drawElided(p, nameleft, texttop, availableWidth, 1);
			p.restoreTextPalette();
		}
	} else if (!item) {
		auto availableWidth = namewidth;
		if (history->isPinnedDialog()) {
			auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
			icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
			availableWidth -= icon.width() + st::dialogsUnreadPadding;
		}

		auto &color = active ? st::dialogsTextFgServiceActive : (selected ? st::dialogsTextFgServiceOver : st::dialogsTextFgService);
		p.setFont(st::dialogsTextFont);
		if (!history->paintSendAction(p, nameleft, texttop, availableWidth, fullWidth, color, ms)) {
			// Empty history
		}
	} else if (!item->isEmpty()) {
		paintRowDate(p, date, rectForName, active, selected);

		paintItemCallback(nameleft, namewidth, item);
	} else if (history->isPinnedDialog()) {
		auto availableWidth = namewidth;
		auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
		icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
		availableWidth -= icon.width() + st::dialogsUnreadPadding;
	}
	auto sendStateIcon = ([draft, item, active, selected]() -> const style::icon* {
		if (draft) {
			if (draft->saveRequestId) {
				return &(active ? st::dialogsSendingIconActive : (selected ? st::dialogsSendingIconOver : st::dialogsSendingIcon));
			}
		} else if (item && !item->isEmpty() && item->needCheck()) {
			if (item->id > 0) {
				if (item->unread()) {
					return &(active ? st::dialogsSentIconActive : (selected ? st::dialogsSentIconOver : st::dialogsSentIcon));
				}
				return &(active ? st::dialogsReceivedIconActive : (selected ? st::dialogsReceivedIconOver : st::dialogsReceivedIcon));
			}
			return &(active ? st::dialogsSendingIconActive : (selected ? st::dialogsSendingIconOver : st::dialogsSendingIcon));
		}
		return nullptr;
	})();
	if (sendStateIcon) {
		rectForName.setWidth(rectForName.width() - st::dialogsSendStateSkip);
		sendStateIcon->paint(p, rectForName.topLeft() + QPoint(rectForName.width(), 0), fullWidth);
	}

	if (from == history->peer && from->isVerified()) {
		auto icon = &(active ? st::dialogsVerifiedIconActive : (selected ? st::dialogsVerifiedIconOver : st::dialogsVerifiedIcon));
		rectForName.setWidth(rectForName.width() - icon->width());
		icon->paint(p, rectForName.topLeft() + QPoint(qMin(from->dialogName().maxWidth(), rectForName.width()), 0), fullWidth);
	}

	p.setPen(active ? st::dialogsNameFgActive : (selected ? st::dialogsNameFgOver : st::dialogsNameFg));
	from->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
}

struct UnreadBadgeSizeData {
	QImage circle;
	QPixmap left[6], right[6];
};
class UnreadBadgeStyleData : public Data::AbstractStructure {
public:
	UnreadBadgeSizeData sizes[UnreadBadgeSizesCount];
	style::color bg[6] = {
		st::dialogsUnreadBg,
		st::dialogsUnreadBgOver,
		st::dialogsUnreadBgActive,
		st::dialogsUnreadBgMuted,
		st::dialogsUnreadBgMutedOver,
		st::dialogsUnreadBgMutedActive
	};
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

const style::icon *ChatTypeIcon(PeerData *peer, bool active, bool selected) {
	if (peer->isChat() || peer->isMegagroup()) {
		return &(active ? st::dialogsChatIconActive : (selected ? st::dialogsChatIconOver : st::dialogsChatIcon));
	} else if (peer->isChannel()) {
		return &(active ? st::dialogsChannelIconActive : (selected ? st::dialogsChannelIconOver : st::dialogsChannelIcon));
	}
	return nullptr;
}

void paintUnreadBadge(Painter &p, const QRect &rect, const UnreadBadgeStyle &st) {
	Assert(rect.height() == st.size);

	int index = (st.muted ? 0x03 : 0x00) + (st.active ? 0x02 : (st.selected ? 0x01 : 0x00));
	int size = st.size, sizehalf = size / 2;

	unreadBadgeStyle.createIfNull();
	auto badgeData = unreadBadgeStyle->sizes;
	if (st.sizeId > 0) {
		Assert(st.sizeId < UnreadBadgeSizesCount);
		badgeData = &unreadBadgeStyle->sizes[st.sizeId];
	}
	auto bg = unreadBadgeStyle->bg[index];
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
, selected(false)
, muted(false)
, size(st::dialogsUnreadHeight)
, padding(st::dialogsUnreadPadding)
, sizeId(UnreadBadgeInDialogs)
, font(st::dialogsUnreadFont) {
}

void paintUnreadCount(Painter &p, const QString &text, int x, int y, const UnreadBadgeStyle &st, int *outUnreadWidth) {
	int unreadWidth = st.font->width(text);
	int unreadRectWidth = unreadWidth + 2 * st.padding;
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

	auto textTop = st.textTop ? st.textTop : (unreadRectHeight - st.font->height) / 2;
	p.setFont(st.font);
	p.setPen(st.active ? st::dialogsUnreadFgActive : (st.selected ? st::dialogsUnreadFgOver : st::dialogsUnreadFg));
	p.drawText(unreadRectLeft + (unreadRectWidth - unreadWidth) / 2, unreadRectTop + textTop + st.font->ascent, text);
}

void RowPainter::paint(Painter &p, const Row *row, int fullWidth, bool active, bool selected, bool onlyBackground, TimeMs ms) {
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
	auto from = (history->peer->migrateTo() ? history->peer->migrateTo() : history->peer);
	paintRow(p, row, history, from, item, cloudDraft, displayDate(), fullWidth, active, selected, onlyBackground, ms, [&p, fullWidth, active, selected, ms, history, unreadCount](int nameleft, int namewidth, HistoryItem *item) {
		auto availableWidth = namewidth;
		auto texttop = st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip;
		auto hadOneBadge = false;
		auto displayUnreadCounter = (unreadCount != 0);
		auto displayMentionBadge = history->hasUnreadMentions();
		auto displayPinnedIcon = !displayUnreadCounter && history->isPinnedDialog();
		if (displayMentionBadge
			&& unreadCount == 1
			&& item
			&& item->isMediaUnread()
			&& item->mentionsMe()) {
			displayUnreadCounter = false;
		}
		if (displayUnreadCounter) {
			auto counter = QString::number(unreadCount);
			auto mutedCounter = history->mute();
			auto unreadRight = fullWidth - st::dialogsPadding.x();
			auto unreadTop = texttop + st::dialogsTextFont->ascent - st::dialogsUnreadFont->ascent - (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2;
			auto unreadWidth = 0;

			UnreadBadgeStyle st;
			st.active = active;
			st.muted = history->mute();
			paintUnreadCount(p, counter, unreadRight, unreadTop, st, &unreadWidth);
			availableWidth -= unreadWidth + st.padding;

			hadOneBadge = true;
		} else if (displayPinnedIcon) {
			auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
			icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
			availableWidth -= icon.width() + st::dialogsUnreadPadding;

			hadOneBadge = true;
		}
		if (displayMentionBadge) {
			auto counter = qsl("@");
			auto unreadRight = fullWidth - st::dialogsPadding.x() - (namewidth - availableWidth);
			auto unreadTop = texttop + st::dialogsTextFont->ascent - st::dialogsUnreadFont->ascent - (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2;
			auto unreadWidth = 0;

			UnreadBadgeStyle st;
			st.active = active;
			st.muted = false;
			st.padding = 0;
			st.textTop = 0;
			paintUnreadCount(p, counter, unreadRight, unreadTop, st, &unreadWidth);
			availableWidth -= unreadWidth + st.padding + (hadOneBadge ? st::dialogsUnreadPadding : 0);
		}
		auto &color = active ? st::dialogsTextFgServiceActive : (selected ? st::dialogsTextFgServiceOver : st::dialogsTextFgService);
		if (!history->paintSendAction(p, nameleft, texttop, availableWidth, fullWidth, color, ms)) {
			item->drawInDialog(
				p,
				QRect(nameleft, texttop, availableWidth, st::dialogsTextFont->height),
				active,
				selected,
				HistoryItem::DrawInDialog::Normal,
				history->textCachedFor,
				history->lastItemTextCache);
		}
	}, [&p, fullWidth, active, selected, ms, history, unreadCount] {
		if (unreadCount) {
			auto counter = QString::number(unreadCount);
			if (counter.size() > 4) {
				counter = qsl("..") + counter.mid(counter.size() - 3);
			}
			auto mutedCounter = history->mute();
			auto unreadRight = st::dialogsPadding.x() + st::dialogsPhotoSize;
			auto unreadTop = st::dialogsPadding.y() + st::dialogsPhotoSize - st::dialogsUnreadHeight;
			auto unreadWidth = 0;

			UnreadBadgeStyle st;
			st.active = active;
			st.muted = history->mute();
			paintUnreadCount(p, counter, unreadRight, unreadTop, st, &unreadWidth);
		}
	});
}

void RowPainter::paint(Painter &p, const FakeRow *row, int fullWidth, bool active, bool selected, bool onlyBackground, TimeMs ms) {
	auto item = row->item();
	auto history = item->history();
	auto from = [&] {
		if (auto searchPeer = row->searchInPeer()) {
			if (!searchPeer->isChannel() || searchPeer->isMegagroup()) {
				return item->from();
			}
		}
		return (history->peer->migrateTo() ? history->peer->migrateTo() : history->peer);
	}();
	paintRow(p, row, history, from, item, nullptr, item->date, fullWidth, active, selected, onlyBackground, ms, [&p, row, active, selected](int nameleft, int namewidth, HistoryItem *item) {
		int lastWidth = namewidth, texttop = st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip;
		item->drawInDialog(
			p,
			QRect(nameleft, texttop, lastWidth, st::dialogsTextFont->height),
			active,
			selected,
			HistoryItem::DrawInDialog::WithoutSender,
			row->_cacheFor,
			row->_cache);
	}, [] {
	});
}

QRect RowPainter::sendActionAnimationRect(int animationWidth, int animationHeight, int fullWidth, bool textUpdated) {
	auto nameleft = st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPhotoPadding;
	auto namewidth = fullWidth - nameleft - st::dialogsPadding.x();
	auto texttop = st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip;
	return QRect(nameleft, texttop, textUpdated ? namewidth : animationWidth, animationHeight);
}

void paintImportantSwitch(Painter &p, Mode current, int fullWidth, bool selected, bool onlyBackground) {
	p.fillRect(0, 0, fullWidth, st::dialogsImportantBarHeight, selected ? st::dialogsBgOver : st::dialogsBg);
	if (onlyBackground) {
		return;
	}

	p.setFont(st::semiboldFont);
	p.setPen(st::dialogsNameFg);

	int unreadTop = (st::dialogsImportantBarHeight - st::dialogsUnreadHeight) / 2;
	bool mutedHidden = (current == Dialogs::Mode::Important);
	QString text = lang(mutedHidden ? lng_dialogs_show_all_chats : lng_dialogs_hide_muted_chats);
	int textBaseline = unreadTop + (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2 + st::dialogsUnreadFont->ascent;
	p.drawText(st::dialogsPadding.x(), textBaseline, text);

	if (mutedHidden) {
		if (int32 unread = App::histories().unreadMutedCount()) {
			int unreadRight = fullWidth - st::dialogsPadding.x();
			UnreadBadgeStyle st;
			st.muted = true;
			paintUnreadCount(p, QString::number(unread), unreadRight, unreadTop, st, nullptr);
		}
	}
}

void clearUnreadBadgesCache() {
	if (unreadBadgeStyle) {
		for (auto &data : unreadBadgeStyle->sizes) {
			for (auto &left : data.left) {
				left = QPixmap();
			}
			for (auto &right : data.right) {
				right = QPixmap();
			}
		}
	}
}

} // namespace Layout
} // namespace Dialogs
