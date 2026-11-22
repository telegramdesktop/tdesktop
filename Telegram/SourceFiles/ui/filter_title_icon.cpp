/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/filter_title_icon.h"

#include "base/invoke_queued.h"
#include "base/weak_ptr.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_chat_filters.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/filter_icons.h"
#include "ui/rp_widget.h"

#include <QtGui/QPainter>

namespace Ui {

FilterTitleIcon ResolveFilterTitleIcon(
		not_null<Main::Session*> session,
		const Data::ChatFilter &filter,
		const Data::ChatFilterTitle &title,
		not_null<RpWidget*> widget,
		rpl::lifetime &lifetime,
		Fn<void()> refresh) {
	if (LookupFilterIconByEmoji(filter.iconEmoji())) {
		return {};
	}
	const auto &text = title.text;
	const auto isCustomEmojiPrefix = [&](const EntityInText &e) {
		return e.offset() == 0
			&& e.type() == EntityType::CustomEmoji
			&& e.validForText(text.text.size());
	};
	const auto entity = ranges::find_if(
		text.entities,
		isCustomEmojiPrefix);
	if (entity == text.entities.end()) {
		return {};
	}
	const auto id = Data::ParseCustomEmojiData(entity->data());
	if (!id) {
		return {};
	}
	auto length = entity->length();
	while (length < text.text.size() && text.text[length].isSpace()) {
		++length;
	}
	const auto eligible = [](not_null<DocumentData*> document) {
		const auto sticker = document->sticker();
		return sticker
			&& sticker->isLottie()
			&& document->emojiUsesTextColor();
	};
	const auto document = session->data().document(id);
	if (document->sticker()) {
		return eligible(document)
			? FilterTitleIcon{ document, length }
			: FilterTitleIcon();
	}
	const auto guard = base::make_weak(
		lifetime.make_state<base::has_weak_ptr>());
	session->data().customEmojiManager().resolve(
		id
	) | rpl::on_next_error(
		[=](not_null<DocumentData*> resolved) {
			if (eligible(resolved)) {
				InvokeQueued(
					widget,
					[=] {
						if (guard) {
							refresh();
						}
					});
			}
		},
		[](rpl::empty_error) {},
		lifetime);
	return {};
}

TextWithEntities FilterTitleWithoutIcon(
		TextWithEntities title,
		FilterTitleIcon icon) {
	if (!icon.document) {
		return title;
	}
	title.text.remove(0, icon.prefixLength);
	for (auto &entity : title.entities) {
		entity.shiftLeft(icon.prefixLength);
	}
	title.entities.removeIf([](const auto &entity) {
		return !entity.length();
	});
	return title;
}

FilterEmojiIconPaint MakeFilterEmojiIcon(
		not_null<RpWidget*> widget,
		FilterTitleIcon icon,
		bool isStatic,
		Fn<bool()> paused) {
	if (!icon.document) {
		return nullptr;
	}
	constexpr auto kTag = Data::CustomEmojiSizeTag::Large;
	const auto frameSize = Data::FrameSizeFromTag(kTag)
		/ style::DevicePixelRatio();
	const auto emoji = std::shared_ptr<Text::CustomEmoji>(
		icon.document->owner().customEmojiManager().create(
			icon.document,
			[widget] { widget->update(); },
			kTag));
	return [=](QPainter &p, QRect rect, QColor color) {
		if (!emoji->ready()) {
			return false;
		}
		const auto size = std::min(rect.width(), rect.height());
		const auto target = style::centerrect(
			rect,
			QRect(0, 0, size, size));
		const auto scale = size / float64(frameSize);
		p.save();
		p.translate(target.topLeft());
		p.scale(scale, scale);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		emoji->paint(
			p,
			{
				.textColor = color,
				.now = crl::now(),
				.scale = 1.,
				.paused = paused && paused(),
				.internal = { .forceFirstFrame = isStatic },
			});
		p.restore();
		return true;
	};
}

} // namespace Ui
