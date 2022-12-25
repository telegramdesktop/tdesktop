/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_layout.h"

#include "data/data_abstract_structure.h"
#include "data/data_drafts.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "dialogs/dialogs_list.h"
#include "dialogs/ui/dialogs_video_userpic.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "storage/localstorage.h"
#include "ui/empty_userpic.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/unread_badge.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "core/ui_integration.h"
#include "lang/lang_keys.h"
#include "support/support_helper.h"
#include "main/main_session.h"
#include "history/view/history_view_send_action.h"
#include "history/view/history_view_item_preview.h"
#include "history/history_unread_things.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_folder.h"
#include "data/data_peer_values.h"

namespace Dialogs::Ui {
namespace {

// Show all dates that are in the last 20 hours in time format.
constexpr int kRecentlyInSeconds = 20 * 3600;
const auto kPsaBadgePrefix = "cloud_lng_badge_psa_";

[[nodiscard]] bool ShowUserBotIcon(not_null<UserData*> user) {
	return user->isBot() && !user->isSupport() && !user->isRepliesChat();
}

[[nodiscard]] bool ShowSendActionInDialogs(Data::Thread *thread) {
	const auto history = thread ? thread->owningHistory().get() : nullptr;
	if (!history) {
		return false;
	} else if (const auto user = history->peer->asUser()) {
		return (user->onlineTill > 0);
	}
	return !history->isForum();
}

void PaintRowTopRight(
		QPainter &p,
		const QString &text,
		QRect &rectForName,
		const PaintContext &context) {
	const auto width = st::dialogsDateFont->width(text);
	rectForName.setWidth(rectForName.width() - width - st::dialogsDateSkip);
	p.setFont(st::dialogsDateFont);
	p.setPen(context.active
		? st::dialogsDateFgActive
		: context.selected
		? st::dialogsDateFgOver
		: st::dialogsDateFg);
	p.drawText(
		rectForName.left() + rectForName.width() + st::dialogsDateSkip,
		rectForName.top() + st::semiboldFont->height - st::normalFont->descent,
		text);
}

void PaintRowDate(
		QPainter &p,
		QDateTime date,
		QRect &rectForName,
		const PaintContext &context) {
	const auto now = QDateTime::currentDateTime();
	const auto &lastTime = date;
	const auto nowDate = now.date();
	const auto lastDate = lastTime.date();

	const auto dt = [&] {
		const auto wasSameDay = (lastDate == nowDate);
		const auto wasRecently = qAbs(lastTime.secsTo(now)) < kRecentlyInSeconds;
		if (wasSameDay || wasRecently) {
			return QLocale().toString(lastTime, cTimeFormat());
		} else if (lastDate.year() == nowDate.year()
			&& lastDate.weekNumber() == nowDate.weekNumber()) {
			return langDayOfWeek(lastDate);
		} else {
			return QLocale().toString(lastDate, cDateFormat());
		}
	}();
	PaintRowTopRight(p, dt, rectForName, context);
}

int PaintBadges(
		QPainter &p,
		const PaintContext &context,
		BadgesState badgesState,
		int right,
		int top,
		bool displayPinnedIcon = false,
		int pinnedIconTop = 0) {
	auto initial = right;
	if (badgesState.unread
		&& !badgesState.unreadCounter
		&& context.st->unreadMarkDiameter > 0) {
		const auto d = context.st->unreadMarkDiameter;
		UnreadBadgeStyle st;
		PainterHighQualityEnabler hq(p);
		const auto rect = QRect(
			right - st.size + (st.size - d) / 2,
			top + (st.size - d) / 2,
			d,
			d);
		p.setPen(Qt::NoPen);
		p.setBrush(badgesState.unreadMuted
			? (context.active
				? st::dialogsUnreadBgMutedActive
				: context.selected
				? st::dialogsUnreadBgMutedOver
				: st::dialogsUnreadBgMuted)
			: (context.active
				? st::dialogsUnreadBgActive
				: context.selected
				? st::dialogsUnreadBgOver
				: st::dialogsUnreadBg));
		p.drawEllipse(rect);
		right -= st.size + st.padding;
	} else if (badgesState.unread) {
		UnreadBadgeStyle st;
		st.active = context.active;
		st.selected = context.selected;
		st.muted = badgesState.unreadMuted;
		const auto counter = (badgesState.unreadCounter > 0)
			? QString::number(badgesState.unreadCounter)
			: QString();
		const auto badge = PaintUnreadBadge(p, counter, right, top, st);
		right -= badge.width() + st.padding;
	} else if (displayPinnedIcon) {
		const auto &icon = context.active
			? st::dialogsPinnedIconActive
			: context.selected
			? st::dialogsPinnedIconOver
			: st::dialogsPinnedIcon;
		icon.paint(p, right - icon.width(), pinnedIconTop, context.width);
		right -= icon.width() + st::dialogsUnreadPadding;
	}
	if (badgesState.mention || badgesState.reaction) {
		UnreadBadgeStyle st;
		st.sizeId = badgesState.mention
			? UnreadBadgeSize::Dialogs
			: UnreadBadgeSize::ReactionInDialogs;
		st.active = context.active;
		st.selected = context.selected;
		st.muted = badgesState.mention
			? badgesState.mentionMuted
			: badgesState.reactionMuted;
		st.padding = 0;
		st.textTop = 0;
		const auto counter = QString();
		const auto badge = PaintUnreadBadge(p, counter, right, top, st);
		(badgesState.mention
			? (st.active
				? st::dialogsUnreadMentionActive
				: st.selected
				? st::dialogsUnreadMentionOver
				: st::dialogsUnreadMention)
			: (st.active
				? st::dialogsUnreadReactionActive
				: st.selected
				? st::dialogsUnreadReactionOver
				: st::dialogsUnreadReaction)).paintInCenter(p, badge);
		right -= badge.width() + st.padding + st::dialogsUnreadPadding;
	}
	return (initial - right);
}

void PaintExpandedTopicsBar(QPainter &p, float64 progress) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = st::roundRadiusLarge;
	const auto width = st::forumDialogRow.padding.left() / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(st::dialogsBgActive);
	p.drawRoundedRect(
		QRectF(
			-3. * radius - width * (1. - progress),
			st::forumDialogRow.padding.top(),
			3. * radius + width,
			st::forumDialogRow.photoSize),
		radius,
		radius);
}

void PaintNarrowCounter(
		QPainter &p,
		const PaintContext &context,
		BadgesState badgesState) {
	const auto top = context.st->padding.top()
		+ context.st->photoSize
		- st::dialogsUnreadHeight;
	PaintBadges(
		p,
		context,
		badgesState,
		context.st->padding.left() + context.st->photoSize,
		top);
}

int PaintWideCounter(
		QPainter &p,
		const PaintContext &context,
		BadgesState badgesState,
		int texttop,
		int availableWidth,
		bool displayPinnedIcon) {
	const auto top = texttop
		+ st::dialogsTextFont->ascent
		- st::dialogsUnreadFont->ascent
		- (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2;
	const auto used = PaintBadges(
		p,
		context,
		badgesState,
		context.width - context.st->padding.right(),
		top,
		displayPinnedIcon,
		texttop);
	return availableWidth - used;
}

void PaintFolderEntryText(
		Painter &p,
		not_null<Data::Folder*> folder,
		const PaintContext &context,
		QRect rect) {
	if (rect.isEmpty()) {
		return;
	}
	folder->validateListEntryCache();
	p.setFont(st::dialogsTextFont);
	p.setPen(context.active
		? st::dialogsTextFgActive
		: context.selected
		? st::dialogsTextFgOver
		: st::dialogsTextFg);
	folder->listEntryCache().draw(p, {
		.position = rect.topLeft(),
		.availableWidth = rect.width(),
		.palette = &(context.active
			? st::dialogsTextPaletteArchiveActive
			: context.selected
			? st::dialogsTextPaletteArchiveOver
			: st::dialogsTextPaletteArchive),
		.spoiler = Text::DefaultSpoilerCache(),
		.now = context.now,
		.paused = context.paused,
		.elisionLines = rect.height() / st::dialogsTextFont->height,
	});
}

enum class Flag {
	SavedMessages    = 0x08,
	RepliesMessages  = 0x10,
	AllowUserOnline  = 0x20,
	TopicJumpRipple  = 0x40,
};
inline constexpr bool is_flag_type(Flag) { return true; }

template <typename PaintItemCallback>
void PaintRow(
		Painter &p,
		not_null<const BasicRow*> row,
		QRect geometry,
		not_null<Entry*> entry,
		VideoUserpic *videoUserpic,
		PeerData *from,
		PeerBadge &fromBadge,
		Fn<void()> customEmojiRepaint,
		const Text::String &fromName,
		const HiddenSenderInfo *hiddenSenderInfo,
		HistoryItem *item,
		const Data::Draft *draft,
		QDateTime date,
		const PaintContext &context,
		BadgesState badgesState,
		base::flags<Flag> flags,
		PaintItemCallback &&paintItemCallback) {
	const auto supportMode = entry->session().supportMode();
	if (supportMode) {
		draft = nullptr;
	}

	auto bg = context.active
		? st::dialogsBgActive
		: context.selected
		? st::dialogsBgOver
		: context.currentBg;
	p.fillRect(geometry, bg);
	if (!(flags & Flag::TopicJumpRipple)) {
		auto ripple = context.active
			? st::dialogsRippleBgActive
			: st::dialogsRippleBg;
		row->paintRipple(p, 0, 0, context.width, &ripple->c);
	}

	const auto history = entry->asHistory();
	const auto thread = entry->asThread();

	if (flags & Flag::SavedMessages) {
		EmptyUserpic::PaintSavedMessages(
			p,
			context.st->padding.left(),
			context.st->padding.top(),
			context.width,
			context.st->photoSize);
	} else if (flags & Flag::RepliesMessages) {
		EmptyUserpic::PaintRepliesMessages(
			p,
			context.st->padding.left(),
			context.st->padding.top(),
			context.width,
			context.st->photoSize);
	} else if (from) {
		row->paintUserpic(
			p,
			from,
			videoUserpic,
			(flags & Flag::AllowUserOnline) ? history : nullptr,
			context);
	} else if (hiddenSenderInfo) {
		hiddenSenderInfo->emptyUserpic.paintCircle(
			p,
			context.st->padding.left(),
			context.st->padding.top(),
			context.width,
			context.st->photoSize);
	} else {
		entry->paintUserpic(p, row->userpicView(), context);
	}

	auto nameleft = context.st->nameLeft;
	if (context.topicsExpanded > 0.) {
		PaintExpandedTopicsBar(p, context.topicsExpanded);
	}
	if (context.narrow) {
		if (!draft && item && !item->isEmpty()) {
			PaintNarrowCounter(p, context, badgesState);
		}
		return;
	}

	auto namewidth = context.width - nameleft - context.st->padding.right();
	auto rectForName = QRect(
		nameleft,
		context.st->nameTop,
		namewidth,
		st::semiboldFont->height);

	const auto promoted = (history && history->useTopPromotion())
		&& !context.search;
	if (promoted) {
		const auto type = history->topPromotionType();
		const auto custom = type.isEmpty()
			? QString()
			: Lang::GetNonDefaultValue(kPsaBadgePrefix + type.toUtf8());
		const auto text = type.isEmpty()
			? tr::lng_proxy_sponsor(tr::now)
			: custom.isEmpty()
			? tr::lng_badge_psa_default(tr::now)
			: custom;
		PaintRowTopRight(p, text, rectForName, context);
	} else if (from) {
		if (const auto chatTypeIcon = ChatTypeIcon(from, context)) {
			chatTypeIcon->paint(p, rectForName.topLeft(), context.width);
			rectForName.setLeft(rectForName.left()
				+ chatTypeIcon->width()
				+ st::dialogsChatTypeSkip);
		}
	}
	auto texttop = context.st->textTop;
	if (const auto folder = entry->asFolder()) {
		const auto availableWidth = PaintWideCounter(
			p,
			context,
			badgesState,
			texttop,
			namewidth,
			false);
		const auto rect = QRect(
			nameleft,
			texttop,
			availableWidth,
			st::dialogsTextFont->height);
		PaintFolderEntryText(p, folder, context, rect);
	} else if (promoted && !history->topPromotionMessage().isEmpty()) {
		auto availableWidth = namewidth;
		p.setFont(st::dialogsTextFont);
		if (history->cloudDraftTextCache().isEmpty()) {
			history->cloudDraftTextCache().setText(
				st::dialogsTextStyle,
				history->topPromotionMessage(),
				DialogTextOptions());
		}
		p.setPen(context.active
			? st::dialogsTextFgActive
			: context.selected
			? st::dialogsTextFgOver
			: st::dialogsTextFg);
		history->cloudDraftTextCache().draw(p, {
			.position = { nameleft, texttop },
			.availableWidth = availableWidth,
			.spoiler = Text::DefaultSpoilerCache(),
			.now = context.now,
			.paused = context.paused,
			.elisionLines = 1,
		});
	} else if (draft
		|| (supportMode
			&& entry->session().supportHelper().isOccupiedBySomeone(history))) {
		if (!promoted) {
			PaintRowDate(p, date, rectForName, context);
		}

		auto availableWidth = namewidth;
		if (entry->isPinnedDialog(context.filter)
			&& (context.filter || !entry->fixedOnTopIndex())) {
			auto &icon = context.active
				? st::dialogsPinnedIconActive
				: context.selected
				? st::dialogsPinnedIconOver
				: st::dialogsPinnedIcon;
			icon.paint(
				p,
				context.width - context.st->padding.right() - icon.width(),
				texttop,
				context.width);
			availableWidth -= icon.width() + st::dialogsUnreadPadding;
		}

		p.setFont(st::dialogsTextFont);
		auto &color = context.active
			? st::dialogsTextFgServiceActive
			: context.selected
			? st::dialogsTextFgServiceOver
			: st::dialogsTextFgService;
		if (!ShowSendActionInDialogs(thread)
			|| !thread->sendActionPainter()->paint(
				p,
				nameleft,
				texttop,
				availableWidth,
				context.width,
				color,
				context.paused)) {
			auto &cache = thread->cloudDraftTextCache();
			if (cache.isEmpty()) {
				using namespace TextUtilities;
				auto draftWrapped = Text::PlainLink(
					tr::lng_dialogs_text_from_wrapped(
						tr::now,
						lt_from,
						tr::lng_from_draft(tr::now)));
				auto draftText = supportMode
					? Text::PlainLink(
						Support::ChatOccupiedString(history))
					: tr::lng_dialogs_text_with_from(
						tr::now,
						lt_from_part,
						draftWrapped,
						lt_message,
						DialogsPreviewText({
							.text = draft->textWithTags.text,
							.entities = ConvertTextTagsToEntities(
								draft->textWithTags.tags),
						}),
						Text::WithEntities);
				const auto context = Core::MarkedTextContext{
					.session = &thread->session(),
					.customEmojiRepaint = customEmojiRepaint,
				};
				cache.setMarkedText(
					st::dialogsTextStyle,
					draftText,
					DialogTextOptions(),
					context);
			}
			p.setPen(context.active
				? st::dialogsTextFgActive
				: context.selected
				? st::dialogsTextFgOver
				: st::dialogsTextFg);
			cache.draw(p, {
				.position = { nameleft, texttop },
				.availableWidth = availableWidth,
				.palette = &(supportMode
					? (context.active
						? st::dialogsTextPaletteTakenActive
						: context.selected
						? st::dialogsTextPaletteTakenOver
						: st::dialogsTextPaletteTaken)
					: (context.active
						? st::dialogsTextPaletteDraftActive
						: context.selected
						? st::dialogsTextPaletteDraftOver
						: st::dialogsTextPaletteDraft)),
				.spoiler = Text::DefaultSpoilerCache(),
				.now = context.now,
				.paused = context.paused,
				.elisionLines = 1,
			});
		}
	} else if (!item) {
		auto availableWidth = namewidth;
		if (entry->isPinnedDialog(context.filter)
			&& (context.filter || !entry->fixedOnTopIndex())) {
			auto &icon = context.active
				? st::dialogsPinnedIconActive
				: context.selected
				? st::dialogsPinnedIconOver
				: st::dialogsPinnedIcon;
			icon.paint(p, context.width - context.st->padding.right() - icon.width(), texttop, context.width);
			availableWidth -= icon.width() + st::dialogsUnreadPadding;
		}

		auto &color = context.active
			? st::dialogsTextFgServiceActive
			: context.selected
			? st::dialogsTextFgServiceOver
			: st::dialogsTextFgService;
		p.setFont(st::dialogsTextFont);
		if (!ShowSendActionInDialogs(thread)
			|| !thread->sendActionPainter()->paint(
				p,
				nameleft,
				texttop,
				availableWidth,
				context.width,
				color,
				context.now)) {
			// Empty history
		}
	} else if (!item->isEmpty()) {
		if (thread && !promoted) {
			PaintRowDate(p, date, rectForName, context);
		}

		paintItemCallback(nameleft, namewidth);
	} else if (entry->isPinnedDialog(context.filter)
		&& (context.filter || !entry->fixedOnTopIndex())) {
		auto &icon = context.active
			? st::dialogsPinnedIconActive
			: context.selected
			? st::dialogsPinnedIconOver
			: st::dialogsPinnedIcon;
		icon.paint(p, context.width - context.st->padding.right() - icon.width(), texttop, context.width);
	}
	const auto sendStateIcon = [&]() -> const style::icon* {
		if (!thread) {
			return nullptr;
		} else if (const auto topic = thread->asTopic()
			; !context.search && topic && topic->closed()) {
			return &(context.active
				? st::dialogsLockIconActive
				: context.selected
				? st::dialogsLockIconOver
				: st::dialogsLockIcon);
		} else if (draft) {
			if (draft->saveRequestId) {
				return &(context.active
					? st::dialogsSendingIconActive
					: context.selected
					? st::dialogsSendingIconOver
					: st::dialogsSendingIcon);
			}
		} else if (item && !item->isEmpty() && item->needCheck()) {
			if (!item->isSending() && !item->hasFailed()) {
				if (item->unread(thread)) {
					return &(context.active
						? st::dialogsSentIconActive
						: context.selected
						? st::dialogsSentIconOver
						: st::dialogsSentIcon);
				}
				return &(context.active
					? st::dialogsReceivedIconActive
					: context.selected
					? st::dialogsReceivedIconOver
					: st::dialogsReceivedIcon);
			}
			return &(context.active
				? st::dialogsSendingIconActive
				: context.selected
				? st::dialogsSendingIconOver
				: st::dialogsSendingIcon);
		}
		return nullptr;
	}();
	if (sendStateIcon) {
		rectForName.setWidth(rectForName.width() - st::dialogsSendStateSkip);
		sendStateIcon->paint(p, rectForName.topLeft() + QPoint(rectForName.width(), 0), context.width);
	}

	p.setFont(st::semiboldFont);
	if (flags & (Flag::SavedMessages | Flag::RepliesMessages)) {
		auto text = (flags & Flag::SavedMessages)
			? tr::lng_saved_messages(tr::now)
			: tr::lng_replies_messages(tr::now);
		const auto textWidth = st::semiboldFont->width(text);
		if (textWidth > rectForName.width()) {
			text = st::semiboldFont->elided(text, rectForName.width());
		}
		p.setPen(context.active
			? st::dialogsNameFgActive
			: context.selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
		p.drawTextLeft(rectForName.left(), rectForName.top(), context.width, text);
	} else if (from) {
		if (history && !context.search) {
			const auto badgeWidth = fromBadge.drawGetWidth(
				p,
				rectForName,
				fromName.maxWidth(),
				context.width,
				{
					.peer = from,
					.verified = (context.active
						? &st::dialogsVerifiedIconActive
						: context.selected
						? &st::dialogsVerifiedIconOver
						: &st::dialogsVerifiedIcon),
					.premium = (context.active
						? &st::dialogsPremiumIconActive
						: context.selected
						? &st::dialogsPremiumIconOver
						: &st::dialogsPremiumIcon),
					.scam = (context.active
						? &st::dialogsScamFgActive
						: context.selected
						? &st::dialogsScamFgOver
						: &st::dialogsScamFg),
					.premiumFg = (context.active
						? &st::dialogsVerifiedIconBgActive
						: context.selected
						? &st::dialogsVerifiedIconBgOver
						: &st::dialogsVerifiedIconBg),
					.preview = (context.active
						? st::dialogsScamFgActive
						: context.selected
						? st::windowBgRipple
						: st::windowBgOver)->c,
					.customEmojiRepaint = customEmojiRepaint,
					.now = context.now,
					.paused = context.paused,
				});
			rectForName.setWidth(rectForName.width() - badgeWidth);
		}
		p.setPen(context.active
			? st::dialogsNameFgActive
			: context.selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
		fromName.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
	} else if (hiddenSenderInfo) {
		p.setPen(context.active
			? st::dialogsNameFgActive
			: context.selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
		hiddenSenderInfo->nameText().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
	} else {
		p.setPen(context.active
			? st::dialogsNameFgActive
			: entry->folder()
			? (context.selected
				? st::dialogsArchiveFgOver
				: st::dialogsArchiveFg)
			: (context.selected
				? st::dialogsNameFgOver
				: st::dialogsNameFg));
		auto text = entry->chatListName(); // TODO feed name with emoji
		auto textWidth = st::semiboldFont->width(text);
		if (textWidth > rectForName.width()) {
			text = st::semiboldFont->elided(text, rectForName.width());
		}
		p.drawTextLeft(rectForName.left(), rectForName.top(), context.width, text);
	}
}

struct UnreadBadgeSizeData {
	QImage circle;
	QPixmap left[6], right[6];
};
class UnreadBadgeStyleData : public Data::AbstractStructure {
public:
	UnreadBadgeStyleData();

	UnreadBadgeSizeData sizes[static_cast<int>(UnreadBadgeSize::kCount)];
	style::color bg[6] = {
		st::dialogsUnreadBg,
		st::dialogsUnreadBgOver,
		st::dialogsUnreadBgActive,
		st::dialogsUnreadBgMuted,
		st::dialogsUnreadBgMutedOver,
		st::dialogsUnreadBgMutedActive
	};
	style::color reactionBg[6] = {
		st::dialogsDraftFg,
		st::dialogsDraftFgOver,
		st::dialogsDraftFgActive,
		st::dialogsUnreadBgMuted,
		st::dialogsUnreadBgMutedOver,
		st::dialogsUnreadBgMutedActive
	};
	rpl::lifetime lifetime;
};
Data::GlobalStructurePointer<UnreadBadgeStyleData> unreadBadgeStyle;

UnreadBadgeStyleData::UnreadBadgeStyleData() {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &data : sizes) {
			for (auto &left : data.left) {
				left = QPixmap();
			}
			for (auto &right : data.right) {
				right = QPixmap();
			}
		}
	}, lifetime);
}

void createCircleMask(UnreadBadgeSizeData *data, int size) {
	if (!data->circle.isNull()) return;

	data->circle = style::createCircleMask(size);
}

QImage colorizeCircleHalf(UnreadBadgeSizeData *data, int size, int half, int xoffset, style::color color) {
	auto result = style::colorizeImage(data->circle, color, QRect(xoffset, 0, half, size));
	result.setDevicePixelRatio(cRetinaFactor());
	return result;
}

void PaintUnreadBadge(QPainter &p, const QRect &rect, const UnreadBadgeStyle &st) {
	Assert(rect.height() == st.size);

	int index = (st.muted ? 0x03 : 0x00) + (st.active ? 0x02 : (st.selected ? 0x01 : 0x00));
	int size = st.size, sizehalf = size / 2;

	unreadBadgeStyle.createIfNull();
	auto badgeData = unreadBadgeStyle->sizes;
	if (st.sizeId > UnreadBadgeSize()) {
		Assert(st.sizeId < UnreadBadgeSize::kCount);
		badgeData = &unreadBadgeStyle->sizes[static_cast<int>(st.sizeId)];
	}
	const auto bg = (st.sizeId == UnreadBadgeSize::ReactionInDialogs)
		? unreadBadgeStyle->reactionBg[index]
		: unreadBadgeStyle->bg[index];
	if (badgeData->left[index].isNull()) {
		int imgsize = size * cIntRetinaFactor(), imgsizehalf = sizehalf * cIntRetinaFactor();
		createCircleMask(badgeData, size);
		badgeData->left[index] = PixmapFromImage(
			colorizeCircleHalf(badgeData, imgsize, imgsizehalf, 0, bg));
		badgeData->right[index] = PixmapFromImage(colorizeCircleHalf(
			badgeData,
			imgsize,
			imgsizehalf,
			imgsize - imgsizehalf,
			bg));
	}

	int bar = rect.width() - 2 * sizehalf;
	p.drawPixmap(rect.x(), rect.y(), badgeData->left[index]);
	if (bar) {
		p.fillRect(rect.x() + sizehalf, rect.y(), bar, rect.height(), bg);
	}
	p.drawPixmap(rect.x() + sizehalf + bar, rect.y(), badgeData->right[index]);
}

[[nodiscard]] QString ComputeUnreadBadgeText(
	const QString &unreadCount,
	int allowDigits) {
	return (allowDigits > 0) && (unreadCount.size() > allowDigits + 1)
		? u".."_q + unreadCount.mid(unreadCount.size() - allowDigits)
		: unreadCount;
}

} // namespace

const style::icon *ChatTypeIcon(not_null<PeerData*> peer) {
	return ChatTypeIcon(peer, {
		.st = &st::defaultDialogRow,
		.currentBg = st::windowBg,
	});
}

const style::icon *ChatTypeIcon(
		not_null<PeerData*> peer,
		const PaintContext &context) {
	if (const auto user = peer->asUser()) {
		if (ShowUserBotIcon(user)) {
			return &(context.active
				? st::dialogsBotIconActive
				: context.selected
				? st::dialogsBotIconOver
				: st::dialogsBotIcon);
		}
	} else if (peer->isBroadcast()) {
		return &(context.active
			? st::dialogsChannelIconActive
			: context.selected
			? st::dialogsChannelIconOver
			: st::dialogsChannelIcon);
	} else if (peer->isForum()) {
		return &(context.active
			? st::dialogsForumIconActive
			: context.selected
			? st::dialogsForumIconOver
			: st::dialogsForumIcon);
	} else {
		return &(context.active
			? st::dialogsChatIconActive
			: context.selected
			? st::dialogsChatIconOver
			: st::dialogsChatIcon);
	}
	return nullptr;
}

UnreadBadgeStyle::UnreadBadgeStyle()
: size(st::dialogsUnreadHeight)
, padding(st::dialogsUnreadPadding)
, font(st::dialogsUnreadFont) {
}

QSize CountUnreadBadgeSize(
		const QString &unreadCount,
		const UnreadBadgeStyle &st,
		int allowDigits) {
	const auto text = ComputeUnreadBadgeText(unreadCount, allowDigits);
	const auto unreadRectHeight = st.size;
	const auto unreadWidth = st.font->width(text);
	return {
		std::max(unreadWidth + 2 * st.padding, unreadRectHeight),
		unreadRectHeight,
	};
}

QRect PaintUnreadBadge(
		QPainter &p,
		const QString &unreadCount,
		int x,
		int y,
		const UnreadBadgeStyle &st,
		int allowDigits) {
	const auto text = ComputeUnreadBadgeText(unreadCount, allowDigits);
	const auto unreadRectHeight = st.size;
	const auto unreadWidth = st.font->width(text);
	const auto unreadRectWidth = std::max(
		unreadWidth + 2 * st.padding,
		unreadRectHeight);

	const auto unreadRectLeft = ((st.align & Qt::AlignHorizontal_Mask) & style::al_center)
		? (x - unreadRectWidth) / 2
		: ((st.align & Qt::AlignHorizontal_Mask) & style::al_right)
		? (x - unreadRectWidth)
		: x;
	const auto unreadRectTop = y;

	const auto badge = QRect(unreadRectLeft, unreadRectTop, unreadRectWidth, unreadRectHeight);
	PaintUnreadBadge(p, badge, st);

	const auto textTop = st.textTop ? st.textTop : (unreadRectHeight - st.font->height) / 2;
	p.setFont(st.font);
	p.setPen(st.active
		? st::dialogsUnreadFgActive
		: st.selected
		? st::dialogsUnreadFgOver
		: st::dialogsUnreadFg);
	p.drawText(unreadRectLeft + (unreadRectWidth - unreadWidth) / 2, unreadRectTop + textTop + st.font->ascent, text);

	return badge;
}

void RowPainter::Paint(
		Painter &p,
		not_null<const Row*> row,
		VideoUserpic *videoUserpic,
		const PaintContext &context) {
	const auto entry = row->entry();
	const auto history = row->history();
	const auto thread = row->thread();
	const auto peer = history ? history->peer.get() : nullptr;
	const auto badgesState = entry->chatListBadgesState();
	const auto item = entry->chatListMessage();
	const auto cloudDraft = [&]() -> const Data::Draft*{
		if (!thread) {
			return nullptr;
		}
		if ((!peer || !peer->isForum()) && (!item || !badgesState.unread)) {
			// Draw item, if there are unread messages.
			const auto draft = thread->owningHistory()->cloudDraft(
				thread->topicRootId());
			if (!Data::DraftIsNull(draft)) {
				return draft;
			}
		}
		return nullptr;
	}();
	const auto displayDate = [&] {
		if (item) {
			if (cloudDraft) {
				return (item->date() > cloudDraft->date)
					? ItemDateTime(item)
					: base::unixtime::parse(cloudDraft->date);
			}
			return ItemDateTime(item);
		}
		return cloudDraft
			? base::unixtime::parse(cloudDraft->date)
			: QDateTime();
	}();
	const auto displayPinnedIcon = badgesState.empty()
		&& entry->isPinnedDialog(context.filter)
		&& (context.filter || !entry->fixedOnTopIndex());

	const auto from = history
		? (history->peer->migrateTo()
			? history->peer->migrateTo()
			: history->peer.get())
		: nullptr;
	const auto allowUserOnline = !context.narrow || badgesState.empty();
	const auto flags = (allowUserOnline ? Flag::AllowUserOnline : Flag(0))
		| (peer && peer->isSelf() ? Flag::SavedMessages : Flag(0))
		| (peer && peer->isRepliesChat() ? Flag::RepliesMessages : Flag(0))
		| (row->topicJumpRipple() ? Flag::TopicJumpRipple : Flag(0));
	const auto paintItemCallback = [&](int nameleft, int namewidth) {
		const auto texttop = context.st->textTop;
		const auto availableWidth = PaintWideCounter(
			p,
			context,
			badgesState,
			texttop,
			namewidth,
			displayPinnedIcon);
		const auto &color = context.active
			? st::dialogsTextFgServiceActive
			: context.selected
			? st::dialogsTextFgServiceOver
			: st::dialogsTextFgService;
		auto rect = QRect(
			nameleft,
			texttop,
			availableWidth,
			st::dialogsTextFont->height);
		const auto actionWasPainted = ShowSendActionInDialogs(thread)
			? thread->sendActionPainter()->paint(
				p,
				rect.x(),
				rect.y(),
				rect.width(),
				context.width,
				color,
				context.now)
			: false;
		const auto view = actionWasPainted
			? nullptr
			: thread
			? &thread->lastItemDialogsView()
			: nullptr;
		if (view) {
			const auto forum = context.st->topicsHeight
				? row->history()->peer->forum()
				: nullptr;
			if (!view->prepared(item, forum)) {
				view->prepare(
					item,
					forum,
					[=] { entry->updateChatListEntry(); },
					{});
			}
			if (forum) {
				rect.setHeight(context.st->topicsHeight + rect.height());
			}
			view->paint(p, rect, context);
		}
	};
	PaintRow(
		p,
		row,
		QRect(0, 0, context.width, row->height()),
		entry,
		videoUserpic,
		from,
		entry->chatListPeerBadge(),
		[=] { entry->updateChatListEntry(); },
		entry->chatListNameText(),
		nullptr,
		item,
		cloudDraft,
		displayDate,
		context,
		badgesState,
		flags,
		paintItemCallback);
}

void RowPainter::Paint(
		Painter &p,
		not_null<const FakeRow*> row,
		const PaintContext &context) {
	const auto item = row->item();
	const auto topic = context.forum ? row->topic() : nullptr;
	const auto history = topic ? nullptr : item->history().get();
	const auto entry = topic ? (Entry*)topic : (Entry*)history;
	auto cloudDraft = nullptr;
	const auto from = [&] {
		const auto in = row->searchInChat();
		return (topic && (in.topic() != topic))
			? nullptr
			: in
			? item->displayFrom()
			: history->peer->migrateTo()
			? history->peer->migrateTo()
			: history->peer.get();
	}();
	const auto hiddenSenderInfo = [&]() -> const HiddenSenderInfo* {
		if (const auto searchChat = row->searchInChat()) {
			if (const auto peer = searchChat.peer()) {
				if (const auto forwarded = item->Get<HistoryMessageForwarded>()) {
					if (peer->isSelf() || forwarded->imported) {
						return forwarded->hiddenSenderInfo.get();
					}
				}
			}
		}
		return nullptr;
	}();
	const auto previewOptions = [&]() -> HistoryView::ToPreviewOptions {
		if (topic) {
			return {};
		} else if (const auto searchChat = row->searchInChat()) {
			if (const auto peer = searchChat.peer()) {
				if (!peer->isChannel() || peer->isMegagroup()) {
					return { .hideSender = true };
				}
			}
		}
		return {};
	}();

	const auto badgesState = context.displayUnreadInfo
		? entry->chatListBadgesState()
		: BadgesState();
	const auto displayPinnedIcon = false;

	const auto paintItemCallback = [&](int nameleft, int namewidth) {
		const auto texttop = context.st->textTop;
		const auto availableWidth = PaintWideCounter(
			p,
			context,
			badgesState,
			texttop,
			namewidth,
			displayPinnedIcon);

		const auto itemRect = QRect(
			nameleft,
			texttop,
			availableWidth,
			st::dialogsTextFont->height);
		auto &view = row->itemView();
		if (!view.prepared(item, nullptr)) {
			view.prepare(item, nullptr, row->repaint(), previewOptions);
		}
		view.paint(p, itemRect, context);
	};
	const auto showSavedMessages = history
		&& history->peer->isSelf()
		&& !row->searchInChat();
	const auto showRepliesMessages = history
		&& history->peer->isRepliesChat()
		&& !row->searchInChat();
	const auto flags = (showSavedMessages ? Flag::SavedMessages : Flag(0))
		| (showRepliesMessages ? Flag::RepliesMessages : Flag(0));
	PaintRow(
		p,
		row,
		QRect(0, 0, context.width, context.st->height),
		entry,
		nullptr,
		from,
		row->badge(),
		row->repaint(),
		row->name(),
		hiddenSenderInfo,
		item,
		cloudDraft,
		ItemDateTime(item),
		context,
		badgesState,
		flags,
		paintItemCallback);
}

QRect RowPainter::SendActionAnimationRect(
		not_null<const style::DialogRow*> st,
		int animationLeft,
		int animationWidth,
		int animationHeight,
		int fullWidth,
		bool textUpdated) {
	const auto nameleft = st->nameLeft;
	const auto namewidth = fullWidth - nameleft - st->padding.right();
	const auto texttop = st->textTop;
	return QRect(
		nameleft + (textUpdated ? 0 : animationLeft),
		texttop,
		textUpdated ? namewidth : animationWidth,
		animationHeight);
}

void PaintCollapsedRow(
		Painter &p,
		const BasicRow &row,
		Data::Folder *folder,
		const QString &text,
		int unread,
		const PaintContext &context) {
	p.fillRect(
		QRect{ 0, 0, context.width, st::dialogsImportantBarHeight },
		context.selected ? st::dialogsBgOver : context.currentBg);

	row.paintRipple(p, 0, 0, context.width);

	const auto unreadTop = (st::dialogsImportantBarHeight - st::dialogsUnreadHeight) / 2;
	if (!context.narrow || !folder) {
		p.setFont(st::semiboldFont);
		p.setPen(st::dialogsNameFg);

		const auto textBaseline = unreadTop
			+ (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2
			+ st::dialogsUnreadFont->ascent;
		const auto left = context.narrow
			? ((context.width - st::semiboldFont->width(text)) / 2)
			: context.st->padding.left();
		p.drawText(left, textBaseline, text);
	} else {
		folder->paintUserpic(
			p,
			(context.width - st::dialogsUnreadHeight) / 2,
			unreadTop,
			st::dialogsUnreadHeight);
	}
	if (!context.narrow && unread) {
		const auto unreadRight = context.width - context.st->padding.right();
		UnreadBadgeStyle st;
		st.muted = true;
		PaintUnreadBadge(
			p,
			QString::number(unread),
			unreadRight,
			unreadTop,
			st);
	}
}

} // namespace Dialogs::Ui
