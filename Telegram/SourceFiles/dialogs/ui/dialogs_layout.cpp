/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_layout.h"

#include "base/unixtime.h"
#include "core/ui_integration.h"
#include "data/data_channel.h"
#include "data/data_drafts.h"
#include "data/data_folder.h"
#include "data/data_forum_topic.h"
#include "data/data_peer_values.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "dialogs/dialogs_list.h"
#include "dialogs/dialogs_three_state_icon.h"
#include "dialogs/ui/dialogs_video_userpic.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/history_unread_things.h"
#include "history/view/history_view_item_preview.h"
#include "history/view/history_view_send_action.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/localstorage.h"
#include "support/support_helper.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/unread_badge.h"
#include "ui/unread_badge_paint.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Dialogs::Ui {
namespace {

const auto kPsaBadgePrefix = "cloud_lng_badge_psa_";

[[nodiscard]] bool ShowUserBotIcon(not_null<UserData*> user) {
	return user->isBot()
		&& !user->isSupport()
		&& !user->isRepliesChat()
		&& !user->isVerifyCodes();
}

[[nodiscard]] bool ShowSendActionInDialogs(Data::Thread *thread) {
	const auto history = thread ? thread->owningHistory().get() : nullptr;
	if (!history) {
		return false;
	} else if (const auto user = history->peer->asUser()) {
		return !user->lastseen().isHidden();
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

int PaintRightButton(QPainter &p, const PaintContext &context) {
	if (context.width < st::columnMinimalWidthLeft) {
		return 0;
	}
	if (const auto rightButton = context.rightButton) {
		const auto size = rightButton->bg.size() / style::DevicePixelRatio();
		const auto left = context.width
			- size.width()
			- st::dialogRowOpenBotRight;
		const auto top = st::dialogRowOpenBotTop;
		p.drawImage(
			left,
			top,
			context.active
				? rightButton->activeBg
				: context.selected
				? rightButton->selectedBg
				: rightButton->bg);
		if (rightButton->ripple) {
			rightButton->ripple->paint(
				p,
				left,
				top,
				size.width() - size.height() / 2,
				context.active
					? &st::universalRippleAnimation.color->c
					: &st::activeButtonBgRipple->c);
			if (rightButton->ripple->empty()) {
				rightButton->ripple.reset();
			}
		}
		p.setPen(context.active
			? st::activeButtonBg
			: context.selected
			? st::activeButtonFgOver
			: st::activeButtonFg);
		rightButton->text.draw(p, {
			.position = QPoint(
				left + size.height() / 2,
				top + (st::dialogRowOpenBotHeight - rightButton->text.minHeight()) / 2),
			.outerWidth = size.width() - size.height() / 2,
			.availableWidth = size.width() - size.height() / 2,
			.elisionLines = 1,
		});
		return size.width() + st::dialogsUnreadPadding;
	}
	return 0;
}

int PaintBadges(
		QPainter &p,
		const PaintContext &context,
		BadgesState badgesState,
		int right,
		int top,
		bool displayPinnedIcon,
		int pinnedIconTop,
		bool narrow) {
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
		const auto counter = (badgesState.unreadCounter <= 0)
			? QString()
			: !narrow
			? QString::number(badgesState.unreadCounter)
			: ((badgesState.mention || badgesState.reaction)
				&& (badgesState.unreadCounter > 999))
			? (u"99+"_q)
			: (badgesState.unreadCounter > 999999)
			? (u"99999+"_q)
			: QString::number(badgesState.unreadCounter);
		const auto badge = PaintUnreadBadge(p, counter, right, top, st);
		right -= badge.width() + st.padding;
	} else if (const auto used = PaintRightButton(p, context)) {
		return used - st::dialogsUnreadPadding;
	} else if (displayPinnedIcon) {
		const auto &icon = ThreeStateIcon(
			st::dialogsPinnedIcon,
			context.active,
			context.selected);
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
		ThreeStateIcon(
			badgesState.mention
				? st::dialogsUnreadMention
				: st::dialogsUnreadReaction,
			st.active,
			st.selected).paintInCenter(p, badge);
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
		top,
		false,
		0,
		true);
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
		texttop,
		false);
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
		.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
		.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
		.elisionHeight = rect.height(),
	});
}

enum class Flag {
	SavedMessages    = 0x008,
	RepliesMessages  = 0x010,
	VerifyCodes      = 0x020,
	AllowUserOnline  = 0x040,
	TopicJumpRipple  = 0x080,
	HiddenAuthor     = 0x100,
	MyNotes          = 0x200,
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
		PeerBadge &rowBadge,
		Fn<void()> customEmojiRepaint,
		const Text::String &rowName,
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
	const auto sublist = entry->asSublist();

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
	} else if (flags & Flag::HiddenAuthor) {
		EmptyUserpic::PaintHiddenAuthor(
			p,
			context.st->padding.left(),
			context.st->padding.top(),
			context.width,
			context.st->photoSize);
	} else if (flags & Flag::MyNotes) {
		EmptyUserpic::PaintMyNotes(
			p,
			context.st->padding.left(),
			context.st->padding.top(),
			context.width,
			context.st->photoSize);
	} else if (!from && hiddenSenderInfo) {
		hiddenSenderInfo->emptyUserpic.paintCircle(
			p,
			context.st->padding.left(),
			context.st->padding.top(),
			context.width,
			context.st->photoSize);
	} else if (!(flags & Flag::AllowUserOnline)) {
		PaintUserpic(
			p,
			entry,
			from,
			videoUserpic,
			row->userpicView(),
			context);
	} else {
		row->paintUserpic(
			p,
			entry,
			from,
			videoUserpic,
			context,
			(context.narrow
				&& !badgesState.empty()
				&& !draft
				&& item
				&& !item->isEmpty()));
	}

	const auto nameleft = context.st->nameLeft;
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
	const auto verifyInfo = (from && !from->isSelf())
		? from->botVerifyDetails()
		: nullptr;
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
	} else if (verifyInfo) {
		if (!rowBadge.ready(verifyInfo)) {
			rowBadge.set(
				verifyInfo,
				from->owner().customEmojiManager().factory(),
				customEmojiRepaint);
		}
		const auto &st = Ui::VerifiedStyle(context);
		const auto position = rectForName.topLeft();
		const auto skip = rowBadge.drawVerified(p, position, st);
		rectForName.setLeft(position.x() + skip + st::dialogsChatTypeSkip);
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
			.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
			.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
			.elisionLines = 1,
		});
	} else if (draft
		|| (supportMode
			&& entry->session().supportHelper().isOccupiedBySomeone(history))) {
		if (!promoted) {
			const auto dateString = Ui::FormatDialogsDate(date);
			PaintRowTopRight(p, dateString, rectForName, context);
		}

		auto availableWidth = namewidth;
		if (const auto used = PaintRightButton(p, context)) {
			availableWidth -= used;
		} else if (entry->isPinnedDialog(context.filter)
			&& (context.filter || !entry->fixedOnTopIndex())) {
			auto &icon = ThreeStateIcon(
				st::dialogsPinnedIcon,
				context.active,
				context.selected);
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
				auto draftWrapped = Text::Colorized(
					tr::lng_dialogs_text_from_wrapped(
						tr::now,
						lt_from,
						tr::lng_from_draft(tr::now)));
				auto draftText = supportMode
					? Text::Colorized(
						Support::ChatOccupiedString(history))
					: tr::lng_dialogs_text_with_from(
						tr::now,
						lt_from_part,
						std::move(draftWrapped),
						lt_message,
						DialogsPreviewText({
							.text = draft->textWithTags.text,
							.entities = ConvertTextTagsToEntities(
								draft->textWithTags.tags),
						}),
						Text::WithEntities);
				if (draft && draft->reply) {
					auto &data = thread->owner().customEmojiManager();
					draftText = Ui::Text::Colorized(
						Ui::Text::SingleCustomEmoji(
							data.registerInternalEmoji(
								st::dialogsMiniReplyIcon,
								{},
								true))).append(std::move(draftText));
				}
				const auto context = Core::MarkedTextContext{
					.session = &thread->session(),
					.customEmojiRepaint = customEmojiRepaint,
				};
				cache.setMarkedText(
					st::dialogsTextStyle,
					std::move(draftText),
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
				.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
				.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
				.elisionLines = 1,
			});
		}
	} else if (!item) {
		auto availableWidth = namewidth;
		if (const auto used = PaintRightButton(p, context)) {
			availableWidth -= used;
		} else if (entry->isPinnedDialog(context.filter)
			&& (context.filter || !entry->fixedOnTopIndex())) {
			auto &icon = ThreeStateIcon(
				st::dialogsPinnedIcon,
				context.active,
				context.selected);
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
		if ((thread || sublist) && !promoted) {
			const auto dateString = Ui::FormatDialogsDate(date);
			PaintRowTopRight(p, dateString, rectForName, context);
		}

		paintItemCallback(nameleft, namewidth);
	} else if (entry->isPinnedDialog(context.filter)
		&& (context.filter || !entry->fixedOnTopIndex())) {
		auto &icon = ThreeStateIcon(
			st::dialogsPinnedIcon,
			context.active,
			context.selected);
		icon.paint(
			p,
			context.width - context.st->padding.right() - icon.width(),
			texttop,
			context.width);
	}
	const auto sendStateIcon = [&]() -> const style::icon* {
		if (!thread) {
			return nullptr;
		} else if (const auto topic = thread->asTopic()
			; !context.search && topic && topic->closed()) {
			return &ThreeStateIcon(
				st::dialogsLockIcon,
				context.active,
				context.selected);
		} else if (draft) {
			if (draft->saveRequestId) {
				return &ThreeStateIcon(
					st::dialogsSendingIcon,
					context.active,
					context.selected);
			}
		} else if (item && !item->isEmpty() && item->needCheck()) {
			if (!item->isSending() && !item->hasFailed()) {
				if (item->unread(thread)) {
					return &ThreeStateIcon(
						st::dialogsSentIcon,
						context.active,
						context.selected);
				}
				return &ThreeStateIcon(
					st::dialogsReceivedIcon,
					context.active,
					context.selected);
			}
			return &ThreeStateIcon(
				st::dialogsSendingIcon,
				context.active,
				context.selected);
		}
		return nullptr;
	}();
	if (sendStateIcon) {
		rectForName.setWidth(rectForName.width() - st::dialogsSendStateSkip);
		sendStateIcon->paint(p, rectForName.topLeft() + QPoint(rectForName.width(), 0), context.width);
	}

	p.setFont(st::semiboldFont);
	const auto paintPeerBadge = [&](int rowNameWidth) {
		const auto badgeWidth = rowBadge.drawGetWidth(p, {
			.peer = from,
			.rectForName = rectForName,
			.nameWidth = rowNameWidth,
			.outerWidth = context.width,
			.verified = (context.active
				? &st::dialogsVerifiedIconActive
				: context.selected
				? &st::dialogsVerifiedIconOver
				: &st::dialogsVerifiedIcon),
			.premium = &ThreeStateIcon(
				st::dialogsPremiumIcon,
				context.active,
				context.selected),
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
			.customEmojiRepaint = customEmojiRepaint,
			.now = context.now,
			.paused = context.paused,
		});
		rectForName.setWidth(rectForName.width() - badgeWidth);
	};
	if (flags
		& (Flag::SavedMessages
			| Flag::RepliesMessages
			| Flag::VerifyCodes
			| Flag::HiddenAuthor
			| Flag::MyNotes)) {
		auto text = (flags & Flag::SavedMessages)
			? tr::lng_saved_messages(tr::now)
			: (flags & Flag::RepliesMessages)
			? tr::lng_replies_messages(tr::now)
			: (flags & Flag::VerifyCodes)
			? tr::lng_verification_codes(tr::now)
			: (flags & Flag::MyNotes)
			? tr::lng_my_notes(tr::now)
			: tr::lng_hidden_author_messages(tr::now);
		const auto textWidth = st::semiboldFont->width(text);
		if (!context.search && (flags & Flag::VerifyCodes)) {
			paintPeerBadge(textWidth);
		}
		if (textWidth > rectForName.width()) {
			text = st::semiboldFont->elided(text, rectForName.width());
		}
		p.setPen(context.active
			? st::dialogsNameFgActive
			: context.selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
		p.drawTextLeft(
			rectForName.left(),
			rectForName.top(),
			context.width,
			text);
	} else if (from) {
		if ((history || sublist) && !context.search) {
			paintPeerBadge(rowName.maxWidth());
		}
		p.setPen(context.active
			? st::dialogsNameFgActive
			: context.selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
		rowName.draw(p, {
			.position = rectForName.topLeft(),
			.availableWidth = rectForName.width(),
			.elisionLines = 1,
		});
	} else if (hiddenSenderInfo) {
		p.setPen(context.active
			? st::dialogsNameFgActive
			: context.selected
			? st::dialogsNameFgOver
			: st::dialogsNameFg);
		hiddenSenderInfo->nameText().draw(p, {
			.position = rectForName.topLeft(),
			.availableWidth = rectForName.width(),
			.elisionLines = 1,
		});
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
		rowName.draw(p, {
			.position = rectForName.topLeft(),
			.availableWidth = rectForName.width(),
			.elisionLines = 1,
		});
	}

	if (const auto tags = context.chatsFilterTags) {
		auto left = nameleft;
		for (const auto &tag : *tags) {
			p.drawImage(left, context.st->tagTop, *tag);
			left += st::dialogRowFilterTagSkip
				+ (tag->width() / style::DevicePixelRatio());
		}
	}
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
			return &ThreeStateIcon(
				st::dialogsBotIcon,
				context.active,
				context.selected);
		}
	} else if (peer->isBroadcast()) {
		return &ThreeStateIcon(
			st::dialogsChannelIcon,
			context.active,
			context.selected);
	} else if (peer->isForum()) {
		return &ThreeStateIcon(
			st::dialogsForumIcon,
			context.active,
			context.selected);
	} else {
		return &ThreeStateIcon(
			st::dialogsChatIcon,
			context.active,
			context.selected);
	}
	return nullptr;
}

const style::VerifiedBadge &VerifiedStyle(const PaintContext &context) {
	return context.active
		? st::dialogsVerifiedColorsActive
		: context.selected
		? st::dialogsVerifiedColorsOver
		: st::dialogsVerifiedColors;
}

void RowPainter::Paint(
		Painter &p,
		not_null<const Row*> row,
		VideoUserpic *videoUserpic,
		const PaintContext &context) {
	const auto entry = row->entry();
	const auto history = row->history();
	const auto thread = row->thread();
	const auto sublist = row->sublist();
	const auto peer = history ? history->peer.get() : nullptr;
	const auto badgesState = entry->chatListBadgesState();
	entry->chatListPreloadData(); // Allow chat list message resolve.
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
		: sublist
		? sublist->peer().get()
		: nullptr;
	const auto allowUserOnline = true;// !context.narrow || badgesState.empty();
	const auto flags = (allowUserOnline ? Flag::AllowUserOnline : Flag(0))
		| ((sublist && from->isSelf())
			? Flag::MyNotes
			: (peer && peer->isSelf())
			? Flag::SavedMessages
			: Flag(0))
		| ((from && from->isRepliesChat())
			? Flag::RepliesMessages
			: Flag(0))
		| ((from && from->isVerifyCodes())
			? Flag::VerifyCodes
			: Flag(0))
		| ((sublist && from->isSavedHiddenAuthor())
			? Flag::HiddenAuthor
			: Flag(0))
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
			: sublist
			? &sublist->lastItemDialogsView()
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
						return forwarded->savedFromHiddenSenderInfo.get()
							? forwarded->savedFromHiddenSenderInfo.get()
							: forwarded->originalHiddenSenderInfo.get();
					}
				}
			}
		}
		return nullptr;
	}();
	auto previewOptions = [&]() -> HistoryView::ToPreviewOptions {
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
	previewOptions.ignoreGroup = true;

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
	const auto showVerifyCodes = history
		&& history->peer->isVerifyCodes()
		&& !row->searchInChat();
	const auto flags = (showSavedMessages ? Flag::SavedMessages : Flag(0))
		| (showRepliesMessages ? Flag::RepliesMessages : Flag(0))
		| (showVerifyCodes ? Flag::VerifyCodes : Flag(0));
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
