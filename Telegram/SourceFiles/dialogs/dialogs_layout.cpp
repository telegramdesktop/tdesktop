/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_layout.h"

#include "data/data_abstract_structure.h"
#include "data/data_drafts.h"
#include "dialogs/dialogs_list.h"
#include "styles/style_dialogs.h"
#include "storage/localstorage.h"
#include "ui/empty_userpic.h"
#include "ui/text_options.h"
#include "lang/lang_keys.h"
#include "history/history_item.h"
#include "history/history.h"

namespace Dialogs {
namespace Layout {
namespace {

// Show all dates that are in the last 20 hours in time format.
constexpr int kRecentlyInSeconds = 20 * 3600;

void paintRowTopRight(Painter &p, const QString &text, QRect &rectForName, bool active, bool selected) {
	const auto width = st::dialogsDateFont->width(text);
	rectForName.setWidth(rectForName.width() - width - st::dialogsDateSkip);
	p.setFont(st::dialogsDateFont);
	p.setPen(active ? st::dialogsDateFgActive : (selected ? st::dialogsDateFgOver : st::dialogsDateFg));
	p.drawText(rectForName.left() + rectForName.width() + st::dialogsDateSkip, rectForName.top() + st::msgNameFont->height - st::msgDateFont->descent, text);
}

void paintRowDate(Painter &p, QDateTime date, QRect &rectForName, bool active, bool selected) {
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
	paintRowTopRight(p, dt, rectForName, active, selected);
}

enum class Flag {
	Active           = 0x01,
	Selected         = 0x02,
	OnlyBackground   = 0x04,
	SearchResult     = 0x08,
	SavedMessages    = 0x10,
	FeedSearchResult = 0x20,
};
inline constexpr bool is_flag_type(Flag) { return true; }

template <typename PaintItemCallback, typename PaintCounterCallback>
void paintRow(
		Painter &p,
		not_null<const RippleRow*> row,
		not_null<Entry*> entry,
		Dialogs::Key chat,
		PeerData *from,
		HistoryItem *item,
		const Data::Draft *draft,
		QDateTime date,
		int fullWidth,
		base::flags<Flag> flags,
		TimeMs ms,
		PaintItemCallback &&paintItemCallback,
		PaintCounterCallback &&paintCounterCallback) {
	auto active = (flags & Flag::Active);
	auto selected = (flags & Flag::Selected);
	auto fullRect = QRect(0, 0, fullWidth, st::dialogsRowHeight);
	auto bg = active
		? st::dialogsBgActive
		: (selected
			? st::dialogsBgOver
			: st::dialogsBg);
	auto ripple = active
		? st::dialogsRippleBgActive
		: st::dialogsRippleBg;
	p.fillRect(fullRect, bg);
	row->paintRipple(p, 0, 0, fullWidth, ms, &ripple->c);

	if (flags & Flag::OnlyBackground) {
		return;
	}

	if (flags & Flag::SavedMessages) {
		Ui::EmptyUserpic::PaintSavedMessages(
			p,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
	} else if (from) {
		from->paintUserpicLeft(
			p,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
	} else {
		entry->paintUserpicLeft(
			p,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
	}

	auto nameleft = st::dialogsPadding.x()
		+ st::dialogsPhotoSize
		+ st::dialogsPhotoPadding;
	if (fullWidth <= nameleft) {
		if (!draft && item && !item->isEmpty()) {
			paintCounterCallback();
		}
		return;
	}

	const auto history = chat.history();
	auto namewidth = fullWidth - nameleft - st::dialogsPadding.x();
	auto rectForName = QRect(
		nameleft,
		st::dialogsPadding.y() + st::dialogsNameTop,
		namewidth,
		st::msgNameFont->height);

	const auto promoted = chat.entry()->useProxyPromotion();
	if (promoted) {
		const auto text = lang(lng_proxy_sponsor);
		paintRowTopRight(p, text, rectForName, active, selected);
	} else if (from && !(flags & Flag::FeedSearchResult)) {
		if (const auto chatTypeIcon = ChatTypeIcon(from, active, selected)) {
			chatTypeIcon->paint(p, rectForName.topLeft(), fullWidth);
			rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
		}
	} else if (const auto feed = chat.feed()) {
		if (const auto feedTypeIcon = FeedTypeIcon(feed, active, selected)) {
			feedTypeIcon->paint(p, rectForName.topLeft(), fullWidth);
			rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
		}
	}
	auto texttop = st::dialogsPadding.y()
		+ st::msgNameFont->height
		+ st::dialogsSkip;
	if (draft) {
		if (!promoted) {
			paintRowDate(p, date, rectForName, active, selected);
		}

		auto availableWidth = namewidth;
		if (entry->isPinnedDialog()) {
			auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
			icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
			availableWidth -= icon.width() + st::dialogsUnreadPadding;
		}

		p.setFont(st::dialogsTextFont);
		auto &color = active ? st::dialogsTextFgServiceActive : (selected ? st::dialogsTextFgServiceOver : st::dialogsTextFgService);
		if (history && !history->paintSendAction(p, nameleft, texttop, availableWidth, fullWidth, color, ms)) {
			if (history->cloudDraftTextCache.isEmpty()) {
				auto draftWrapped = textcmdLink(1, lng_dialogs_text_from_wrapped(lt_from, lang(lng_from_draft)));
				auto draftText = lng_dialogs_text_with_from(lt_from_part, draftWrapped, lt_message, TextUtilities::Clean(draft->textWithTags.text));
				history->cloudDraftTextCache.setText(st::dialogsTextStyle, draftText, Ui::DialogTextOptions());
			}
			p.setPen(active ? st::dialogsTextFgActive : (selected ? st::dialogsTextFgOver : st::dialogsTextFg));
			p.setTextPalette(active ? st::dialogsTextPaletteDraftActive : (selected ? st::dialogsTextPaletteDraftOver : st::dialogsTextPaletteDraft));
			history->cloudDraftTextCache.drawElided(p, nameleft, texttop, availableWidth, 1);
			p.restoreTextPalette();
		}
	} else if (!item) {
		auto availableWidth = namewidth;
		if (entry->isPinnedDialog()) {
			auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
			icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
			availableWidth -= icon.width() + st::dialogsUnreadPadding;
		}

		auto &color = active ? st::dialogsTextFgServiceActive : (selected ? st::dialogsTextFgServiceOver : st::dialogsTextFgService);
		p.setFont(st::dialogsTextFont);
		if (history && !history->paintSendAction(p, nameleft, texttop, availableWidth, fullWidth, color, ms)) {
			// Empty history
		}
	} else if (!item->isEmpty()) {
		if (!promoted) {
			paintRowDate(p, date, rectForName, active, selected);
		}

		paintItemCallback(nameleft, namewidth);
	} else if (entry->isPinnedDialog()) {
		auto availableWidth = namewidth;
		auto &icon = (active ? st::dialogsPinnedIconActive : (selected ? st::dialogsPinnedIconOver : st::dialogsPinnedIcon));
		icon.paint(p, fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
		availableWidth -= icon.width() + st::dialogsUnreadPadding;
	}
	auto sendStateIcon = [&]() -> const style::icon* {
		if (draft) {
			if (draft->saveRequestId) {
				return &(active
					? st::dialogsSendingIconActive
					: (selected
						? st::dialogsSendingIconOver
						: st::dialogsSendingIcon));
			}
		} else if (item && !item->isEmpty() && item->needCheck()) {
			if (item->id > 0) {
				if (item->unread()) {
					return &(active
						? st::dialogsSentIconActive
						: (selected
							? st::dialogsSentIconOver
							: st::dialogsSentIcon));
				}
				return &(active
					? st::dialogsReceivedIconActive
					: (selected
						? st::dialogsReceivedIconOver
						: st::dialogsReceivedIcon));
			}
			return &(active
				? st::dialogsSendingIconActive
				: (selected
					? st::dialogsSendingIconOver
					: st::dialogsSendingIcon));
		}
		return nullptr;
	}();
	if (sendStateIcon && history) {
		rectForName.setWidth(rectForName.width() - st::dialogsSendStateSkip);
		sendStateIcon->paint(p, rectForName.topLeft() + QPoint(rectForName.width(), 0), fullWidth);
	}

	const auto nameFg = active
		? st::dialogsNameFgActive
		: (selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
	p.setPen(nameFg);
	if (flags & Flag::SavedMessages) {
		p.setFont(st::msgNameFont);
		auto text = lang(lng_saved_messages);
		auto textWidth = st::msgNameFont->width(text);
		if (textWidth > rectForName.width()) {
			text = st::msgNameFont->elided(text, rectForName.width());
		}
		p.drawTextLeft(rectForName.left(), rectForName.top(), fullWidth, text);
	} else if (from) {
		if (!(flags & Flag::SearchResult) && from->isVerified()) {
			auto icon = &(active ? st::dialogsVerifiedIconActive : (selected ? st::dialogsVerifiedIconOver : st::dialogsVerifiedIcon));
			rectForName.setWidth(rectForName.width() - icon->width());
			icon->paint(p, rectForName.topLeft() + QPoint(qMin(from->dialogName().maxWidth(), rectForName.width()), 0), fullWidth);
		}
		from->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
	} else {
		p.setFont(st::msgNameFont);
		auto text = entry->chatsListName(); // TODO feed name with emoji
		auto textWidth = st::msgNameFont->width(text);
		if (textWidth > rectForName.width()) {
			text = st::msgNameFont->elided(text, rectForName.width());
		}
		p.drawTextLeft(rectForName.left(), rectForName.top(), fullWidth, text);
	}
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

const style::icon *ChatTypeIcon(
		not_null<PeerData*> peer,
		bool active,
		bool selected) {
	if (peer->isChat() || peer->isMegagroup()) {
		return &(active
			? st::dialogsChatIconActive
			: (selected ? st::dialogsChatIconOver : st::dialogsChatIcon));
	} else if (peer->isChannel()) {
		return &(active
			? st::dialogsChannelIconActive
			: (selected
				? st::dialogsChannelIconOver
				: st::dialogsChannelIcon));
	}
	return nullptr;
}

const style::icon *FeedTypeIcon(
		not_null<Data::Feed*> feed,
		bool active,
		bool selected) {
	return &(active ? st::dialogsFeedIconActive
		: (selected ? st::dialogsFeedIconOver : st::dialogsFeedIcon));
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

void RowPainter::paint(
		Painter &p,
		not_null<const Row*> row,
		int fullWidth,
		bool active,
		bool selected,
		bool onlyBackground,
		TimeMs ms) {
	const auto entry = row->entry();
	const auto history = row->history();
	const auto peer = history ? history->peer.get() : nullptr;
	const auto unreadCount = entry->chatListUnreadCount();
	const auto unreadMark = entry->chatListUnreadMark();
	const auto unreadMuted = entry->chatListMutedBadge();
	const auto item = entry->chatsListItem();
	const auto cloudDraft = [&]() -> const Data::Draft*{
		if (history && (!item || (!unreadCount && !unreadMark))) {
			// Draw item, if there are unread messages.
			if (const auto draft = history->cloudDraft()) {
				if (!Data::draftIsNull(draft)) {
					return draft;
				}
			}
		}
		return nullptr;
	}();
	const auto displayDate = [item, cloudDraft] {
		if (item) {
			if (cloudDraft) {
				return (item->date() > cloudDraft->date)
					? ItemDateTime(item)
					: ParseDateTime(cloudDraft->date);
			}
			return ItemDateTime(item);
		}
		return cloudDraft ? ParseDateTime(cloudDraft->date) : QDateTime();
	}();

	const auto from = history
		? (history->peer->migrateTo()
			? history->peer->migrateTo()
			: history->peer.get())
		: nullptr;
	const auto flags = (active ? Flag::Active : Flag(0))
		| (selected ? Flag::Selected : Flag(0))
		| (onlyBackground ? Flag::OnlyBackground : Flag(0))
		| (peer && peer->isSelf() ? Flag::SavedMessages : Flag(0));
	const auto paintItemCallback = [&](int nameleft, int namewidth) {
		auto availableWidth = namewidth;
		auto texttop = st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip;
		auto hadOneBadge = false;
		const auto displayMentionBadge = history
			? history->hasUnreadMentions()
			: false;
		const auto displayUnreadCounter = [&] {
			if (displayMentionBadge
				&& unreadCount == 1
				&& item
				&& item->isMediaUnread()
				&& item->mentionsMe()) {
				return false;
			}
			return (unreadCount > 0);
		}();
		const auto displayUnreadMark = !displayUnreadCounter
			&& !displayMentionBadge
			&& history
			&& unreadMark;
		const auto displayPinnedIcon = !displayUnreadCounter
			&& !displayMentionBadge
			&& !displayUnreadMark
			&& entry->isPinnedDialog();
		if (displayUnreadCounter || displayUnreadMark) {
			auto counter = (unreadCount > 0)
				? QString::number(unreadCount)
				: QString();
			auto unreadRight = fullWidth - st::dialogsPadding.x();
			auto unreadTop = texttop + st::dialogsTextFont->ascent - st::dialogsUnreadFont->ascent - (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2;
			auto unreadWidth = 0;

			UnreadBadgeStyle st;
			st.active = active;
			st.muted = unreadMuted;
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
		const auto &color = active
			? st::dialogsTextFgServiceActive
			: (selected
				? st::dialogsTextFgServiceOver
				: st::dialogsTextFgService);
		const auto actionWasPainted = history ? history->paintSendAction(
			p,
			nameleft,
			texttop,
			availableWidth,
			fullWidth,
			color,
			ms) : false;
		if (!actionWasPainted) {
			auto itemRect = QRect(
				nameleft,
				texttop,
				availableWidth,
				st::dialogsTextFont->height);
			item->drawInDialog(
				p,
				itemRect,
				active,
				selected,
				HistoryItem::DrawInDialog::Normal,
				entry->textCachedFor,
				entry->lastItemTextCache);
		}
	};
	const auto paintCounterCallback = [&] {
		if (unreadCount) {
			auto counter = QString::number(unreadCount);
			if (counter.size() > 4) {
				counter = qsl("..") + counter.mid(counter.size() - 3);
			}
			auto unreadRight = st::dialogsPadding.x() + st::dialogsPhotoSize;
			auto unreadTop = st::dialogsPadding.y() + st::dialogsPhotoSize - st::dialogsUnreadHeight;
			auto unreadWidth = 0;

			UnreadBadgeStyle st;
			st.active = active;
			st.muted = unreadMuted;
			paintUnreadCount(p, counter, unreadRight, unreadTop, st, &unreadWidth);
		}
	};
	paintRow(
		p,
		row,
		entry,
		row->key(),
		from,
		item,
		cloudDraft,
		displayDate,
		fullWidth,
		flags,
		ms,
		paintItemCallback,
		paintCounterCallback);
}

void RowPainter::paint(
		Painter &p,
		not_null<const FakeRow*> row,
		int fullWidth,
		bool active,
		bool selected,
		bool onlyBackground,
		TimeMs ms) {
	auto item = row->item();
	auto history = item->history();
	auto cloudDraft = nullptr;
	const auto from = [&] {
		if (const auto searchChat = row->searchInChat()) {
			if (const auto peer = searchChat.peer()) {
				if (peer->isSelf()) {
					return item->senderOriginal().get();
				} else if (!peer->isChannel() || peer->isMegagroup()) {
					return item->from().get();
				}
			}
		}
		return history->peer->migrateTo()
			? history->peer->migrateTo()
			: history->peer.get();
	}();
	const auto drawInDialogWay = [&] {
		if (const auto searchChat = row->searchInChat()) {
			if (const auto peer = searchChat.peer()) {
				if (!peer->isChannel() || peer->isMegagroup()) {
					return HistoryItem::DrawInDialog::WithoutSender;
				}
			}
		}
		return HistoryItem::DrawInDialog::Normal;
	}();
	const auto paintItemCallback = [&](int nameleft, int namewidth) {
		auto lastWidth = namewidth;
		auto texttop = st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip;
		item->drawInDialog(
			p,
			QRect(nameleft, texttop, lastWidth, st::dialogsTextFont->height),
			active,
			selected,
			drawInDialogWay,
			row->_cacheFor,
			row->_cache);
	};
	const auto paintCounterCallback = [] {};
	const auto showSavedMessages = history->peer->isSelf()
		&& !row->searchInChat();
	const auto flags = (active ? Flag::Active : Flag(0))
		| (selected ? Flag::Selected : Flag(0))
		| (onlyBackground ? Flag::OnlyBackground : Flag(0))
		| Flag::SearchResult
		| (showSavedMessages ? Flag::SavedMessages : Flag(0))
		| (row->searchInChat().feed() ? Flag::FeedSearchResult : Flag(0));
	paintRow(
		p,
		row,
		history,
		history,
		from,
		item,
		cloudDraft,
		ItemDateTime(item),
		fullWidth,
		flags,
		ms,
		paintItemCallback,
		paintCounterCallback);
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
