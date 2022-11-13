/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_message_view.h"

#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_item_preview.h"
#include "main/main_session.h"
#include "dialogs/ui/dialogs_layout.h"
#include "dialogs/ui/dialogs_topics_view.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "core/ui_integration.h"
#include "lang/lang_keys.h"
#include "lang/lang_text_entity.h"
#include "styles/style_dialogs.h"

namespace {

constexpr auto kEmojiLoopCount = 2;

template <ushort kTag>
struct TextWithTagOffset {
	TextWithTagOffset(TextWithEntities text) : text(std::move(text)) {
	}
	TextWithTagOffset(QString text) : text({ std::move(text) }) {
	}
	static TextWithTagOffset FromString(const QString &text) {
		return { { text } };
	}

	TextWithEntities text;
	int offset = -1;
};

} // namespace

namespace Lang {

template <ushort kTag>
struct ReplaceTag<TextWithTagOffset<kTag>> {
	static TextWithTagOffset<kTag> Call(
		TextWithTagOffset<kTag> &&original,
		ushort tag,
		const TextWithTagOffset<kTag> &replacement);
};

template <ushort kTag>
TextWithTagOffset<kTag> ReplaceTag<TextWithTagOffset<kTag>>::Call(
		TextWithTagOffset<kTag> &&original,
		ushort tag,
		const TextWithTagOffset<kTag> &replacement) {
	const auto replacementPosition = FindTagReplacementPosition(
		original.text.text,
		tag);
	if (replacementPosition < 0) {
		return std::move(original);
	}
	original.text = ReplaceTag<TextWithEntities>::Replace(
		std::move(original.text),
		replacement.text,
		replacementPosition);
	if (tag == kTag) {
		original.offset = replacementPosition;
	} else if (original.offset > replacementPosition) {
		constexpr auto kReplaceCommandLength = 4;
		const auto replacementSize = replacement.text.text.size();
		original.offset += replacementSize - kReplaceCommandLength;
	}
	return std::move(original);
}

} // namespace Lang

namespace Dialogs::Ui {

TextWithEntities DialogsPreviewText(TextWithEntities text) {
	auto result = Ui::Text::Filtered(
		std::move(text),
		{
			EntityType::Pre,
			EntityType::Code,
			EntityType::Spoiler,
			EntityType::StrikeOut,
			EntityType::Underline,
			EntityType::Italic,
			EntityType::CustomEmoji,
			EntityType::PlainLink,
		});
	for (auto &entity : result.entities) {
		if (entity.type() == EntityType::Pre) {
			entity = EntityInText(
				EntityType::Code,
				entity.offset(),
				entity.length());
		}
	}
	return result;
}

struct MessageView::LoadingContext {
	std::any context;
	rpl::lifetime lifetime;
};

MessageView::MessageView()
: _senderCache(st::dialogsTextWidthMin)
, _textCache(st::dialogsTextWidthMin) {
}

MessageView::~MessageView() = default;

void MessageView::itemInvalidated(not_null<const HistoryItem*> item) {
	if (_textCachedFor == item.get()) {
		_textCachedFor = nullptr;
	}
}

bool MessageView::dependsOn(not_null<const HistoryItem*> item) const {
	return (_textCachedFor == item.get());
}

bool MessageView::prepared(
		not_null<const HistoryItem*> item,
		Data::Forum *forum) const {
	return (_textCachedFor == item.get())
		&& (!forum
			|| (_topics
				&& _topics->forum() == forum
				&& _topics->prepared()));
}

void MessageView::prepare(
		not_null<const HistoryItem*> item,
		Data::Forum *forum,
		Fn<void()> customEmojiRepaint,
		ToPreviewOptions options) {
	if (!forum) {
		_topics = nullptr;
	} else if (!_topics || _topics->forum() != forum) {
		_topics = std::make_unique<TopicsView>(forum);
		_topics->prepare(item->topicRootId(), customEmojiRepaint);
	} else if (!_topics->prepared()) {
		_topics->prepare(item->topicRootId(), customEmojiRepaint);
	}
	if (_textCachedFor == item.get()) {
		return;
	}
	options.existing = &_imagesCache;
	options.ignoreTopic = true;
	auto preview = item->toPreview(options);
	const auto hasImages = !preview.images.empty();
	const auto history = item->history();
	const auto context = Core::MarkedTextContext{
		.session = &history->session(),
		.customEmojiRepaint = customEmojiRepaint,
		.customEmojiLoopLimit = kEmojiLoopCount,
	};
	const auto senderTill = (preview.arrowInTextPosition > 0)
		? preview.arrowInTextPosition
		: preview.imagesInTextPosition;
	if (hasImages && senderTill > 0) {
		auto sender = Text::Mid(preview.text, 0, senderTill);
		TextUtilities::Trim(sender);
		_senderCache.setMarkedText(
			st::dialogsTextStyle,
			std::move(sender),
			DialogTextOptions());
		const auto topicTill = preview.imagesInTextPosition;
		preview.text = Text::Mid(preview.text, senderTill);
	} else {
		_senderCache = { st::dialogsTextWidthMin };
	}
	TextUtilities::Trim(preview.text);
	_textCache.setMarkedText(
		st::dialogsTextStyle,
		DialogsPreviewText(std::move(preview.text)),
		DialogTextOptions(),
		context);
	_textCachedFor = item;
	_imagesCache = std::move(preview.images);
	if (preview.loadingContext.has_value()) {
		if (!_loadingContext) {
			_loadingContext = std::make_unique<LoadingContext>();
			item->history()->session().downloaderTaskFinished(
			) | rpl::start_with_next([=] {
				_textCachedFor = nullptr;
			}, _loadingContext->lifetime);
		}
		_loadingContext->context = std::move(preview.loadingContext);
	} else {
		_loadingContext = nullptr;
	}
}

int MessageView::countWidth() const {
	auto result = 0;
	if (!_senderCache.isEmpty()) {
		result += _senderCache.maxWidth();
		if (!_imagesCache.empty()) {
			result += st::dialogsMiniPreviewSkip
				+ st::dialogsMiniPreviewRight;
		}
	}
	if (!_imagesCache.empty()) {
		result += (_imagesCache.size()
			* (st::dialogsMiniPreview + st::dialogsMiniPreviewSkip))
			+ st::dialogsMiniPreviewRight;
	}
	return result + _textCache.maxWidth();
}

void MessageView::paint(
		Painter &p,
		const QRect &geometry,
		const PaintContext &context) const {
	if (geometry.isEmpty()) {
		return;
	}
	p.setFont(st::dialogsTextFont);
	p.setPen(context.active
		? st::dialogsTextFgActive
		: context.selected
		? st::dialogsTextFgOver
		: st::dialogsTextFg);
	const auto withTopic = _topics && context.st->topicsHeight;
	const auto palette = &(withTopic
		? (context.active
			? st::dialogsTextPaletteInTopicActive
			: context.selected
			? st::dialogsTextPaletteInTopicOver
			: st::dialogsTextPaletteInTopic)
		: (context.active
			? st::dialogsTextPaletteActive
			: context.selected
			? st::dialogsTextPaletteOver
			: st::dialogsTextPalette));

	auto rect = geometry;
	const auto checkJump = withTopic && !context.active;
	const auto jump1 = checkJump ? _topics->jumpToTopicWidth() : 0;
	if (jump1) {
		paintJumpToLast(p, rect, context, jump1);
	}

	if (withTopic) {
		_topics->paint(p, rect, context);
		rect.setTop(rect.top() + context.st->topicsHeight);
	}

	auto finalRight = rect.x() + rect.width();
	if (jump1) {
		rect.setWidth(rect.width() - st::forumDialogJumpArrowSkip);
		finalRight -= st::forumDialogJumpArrowSkip;
	}
	const auto lines = rect.height() / st::dialogsTextFont->height;
	if (!_senderCache.isEmpty()) {
		_senderCache.draw(p, {
			.position = rect.topLeft(),
			.availableWidth = rect.width(),
			.palette = palette,
			.elisionLines = lines,
		});
		rect.setLeft(rect.x() + _senderCache.maxWidth());
		if (!_imagesCache.empty()) {
			const auto skip = st::dialogsMiniPreviewSkip
				+ st::dialogsMiniPreviewRight;
			rect.setLeft(rect.x() + skip);
		}
	}
	for (const auto &image : _imagesCache) {
		if (rect.width() < st::dialogsMiniPreview) {
			break;
		}
		p.drawImage(
			rect.x(),
			rect.y() + st::dialogsMiniPreviewTop,
			image.data);
		rect.setLeft(rect.x()
			+ st::dialogsMiniPreview
			+ st::dialogsMiniPreviewSkip);
	}
	if (!_imagesCache.empty()) {
		rect.setLeft(rect.x() + st::dialogsMiniPreviewRight);
	}
	if (!rect.isEmpty()) {
		_textCache.draw(p, {
			.position = rect.topLeft(),
			.availableWidth = rect.width(),
			.palette = palette,
			.spoiler = Text::DefaultSpoilerCache(),
			.now = context.now,
			.paused = context.paused,
			.elisionLines = lines,
		});
		rect.setLeft(rect.x() + _textCache.maxWidth());
	}
	if (jump1) {
		const auto x = (rect.width() > st::forumDialogJumpArrowSkip)
			? rect.x()
			: finalRight;
		const auto add = st::forumDialogJumpArrowLeft;
		const auto y = rect.y() + st::forumDialogJumpArrowTop;
		(context.selected
			? st::forumDialogJumpArrowOver
			: st::forumDialogJumpArrow).paint(p, x + add, y, context.width);
	}
}

void MessageView::paintJumpToLast(
		Painter &p,
		const QRect &rect,
		const PaintContext &context,
		int width1) const {
	if (!context.topicJumpCache) {
		return;
	}
	FillJumpToLastBg(p, {
		.st = context.st,
		.corners = (context.selected
			? &context.topicJumpCache->over
			: &context.topicJumpCache->corners),
		.geometry = rect,
		.bg = (context.selected
			? st::dialogsRippleBg
			: st::dialogsBgOver),
		.width1 = width1,
		.width2 = countWidth() + st::forumDialogJumpArrowSkip,
	});
}

void FillJumpToLastBg(QPainter &p, JumpToLastBg context) {
	const auto availableWidth = context.geometry.width();
	const auto use1 = std::min(context.width1, availableWidth);
	const auto use2 = std::min(context.width2, availableWidth);
	const auto padding = st::forumDialogJumpPadding;
	const auto radius = st::forumDialogJumpRadius;
	const auto &bg = context.bg;
	auto &normal = context.corners->normal;
	auto &inverted = context.corners->inverted;
	auto &small = context.corners->small;
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	const auto origin = context.geometry.topLeft();
	const auto delta = std::abs(use1 - use2);
	if (delta <= context.st->topicsSkip / 2) {
		if (normal.p[0].isNull()) {
			normal = Ui::PrepareCornerPixmaps(radius, bg);
		}
		const auto w = std::max(use1, use2);
		const auto h = context.st->topicsHeight + st::normalFont->height;
		const auto fill = QRect(origin, QSize(w, h));
		Ui::FillRoundRect(p, fill.marginsAdded(padding), bg, normal);
	} else {
		const auto h1 = context.st->topicsHeight;
		const auto h2 = st::normalFont->height;
		const auto hmin = std::min(h1, h2);
		const auto wantedInvertedRadius = hmin - radius;
		const auto invertedr = std::min(wantedInvertedRadius, delta / 2);
		const auto smallr = std::min(radius, delta - invertedr);
		const auto smallkey = (use1 < use2) ? smallr : (-smallr);
		if (normal.p[0].isNull()) {
			normal = Ui::PrepareCornerPixmaps(radius, bg);
		}
		if (inverted.p[0].isNull()
			|| context.corners->invertedRadius != invertedr) {
			context.corners->invertedRadius = invertedr;
			inverted = Ui::PrepareInvertedCornerPixmaps(invertedr, bg);
		}
		if (smallr != radius
			&& (small.isNull() || context.corners->smallKey != smallkey)) {
			context.corners->smallKey = smallr;
			auto pixmaps = Ui::PrepareCornerPixmaps(smallr, bg);
			small = pixmaps.p[(use1 < use2) ? 1 : 3];
		}
		const auto rect1 = QRect(origin, QSize(use1, h1));
		auto no1 = normal;
		no1.p[2] = QPixmap();
		if (use1 < use2) {
			no1.p[3] = QPixmap();
		} else if (smallr != radius) {
			no1.p[3] = small;
		}
		auto fill1 = rect1.marginsAdded({
			padding.left(),
			padding.top(),
			padding.right(),
			(use1 < use2 ? -padding.top() : padding.bottom()),
		});
		Ui::FillRoundRect(p, fill1, bg, no1);
		if (use1 < use2) {
			p.drawPixmap(
				fill1.x() + fill1.width(),
				fill1.y() + fill1.height() - invertedr,
				inverted.p[3]);
		}
		const auto add = QPoint(0, h1);
		const auto rect2 = QRect(origin + add, QSize(use2, h2));
		const auto fill2 = rect2.marginsAdded({
			padding.left(),
			(use2 < use1 ? -padding.bottom() : padding.top()),
			padding.right(),
			padding.bottom(),
		});
		auto no2 = normal;
		no2.p[0] = QPixmap();
		if (use2 < use1) {
			no2.p[1] = QPixmap();
		} else if (smallr != radius) {
			no2.p[1] = small;
		}
		Ui::FillRoundRect(p, fill2, bg, no2);
		if (use2 < use1) {
			p.drawPixmap(
				fill2.x() + fill2.width(),
				fill2.y(),
				inverted.p[0]);
		}
	}
}

HistoryView::ItemPreview PreviewWithSender(
		HistoryView::ItemPreview &&preview,
		const QString &sender,
		TextWithEntities topic) {
	auto senderWithOffset = topic.empty()
		? TextWithTagOffset<lt_from>::FromString(sender)
		: tr::lng_dialogs_text_from_in_topic(
			tr::now,
			lt_from,
			{ sender },
			lt_topic,
			std::move(topic),
			TextWithTagOffset<lt_from>::FromString);
	auto wrappedWithOffset = tr::lng_dialogs_text_from_wrapped(
		tr::now,
		lt_from,
		std::move(senderWithOffset.text),
		TextWithTagOffset<lt_from>::FromString);
	const auto wrappedSize = wrappedWithOffset.text.text.size();
	auto fullWithOffset = tr::lng_dialogs_text_with_from(
		tr::now,
		lt_from_part,
		Ui::Text::PlainLink(std::move(wrappedWithOffset.text)),
		lt_message,
		std::move(preview.text),
		TextWithTagOffset<lt_from_part>::FromString);
	preview.text = std::move(fullWithOffset.text);
	preview.arrowInTextPosition = (fullWithOffset.offset < 0
		|| wrappedWithOffset.offset < 0
		|| senderWithOffset.offset < 0)
		? -1
		: (fullWithOffset.offset
			+ wrappedWithOffset.offset
			+ senderWithOffset.offset
			+ sender.size());
	preview.imagesInTextPosition = (fullWithOffset.offset < 0)
		? 0
		: (fullWithOffset.offset + wrappedSize);
	return std::move(preview);
}

} // namespace Dialogs::Ui
