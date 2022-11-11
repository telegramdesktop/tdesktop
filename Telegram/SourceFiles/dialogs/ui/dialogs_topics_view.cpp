/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_topics_view.h"

#include "dialogs/ui/dialogs_layout.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "core/ui_integration.h"
#include "ui/painter.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "styles/style_dialogs.h"

namespace Dialogs::Ui {
namespace {

constexpr auto kIconLoopCount = 1;

} // namespace

TopicsView::TopicsView(not_null<Data::Forum*> forum)
: _forum(forum) {
}

TopicsView::~TopicsView() = default;

void TopicsView::prepare(
		const QRect &geometry,
		not_null<const style::DialogRow*> st,
		Fn<void()> customEmojiRepaint) {
	auto index = 0;
	auto available = geometry.width();
	for (const auto &topic : _forum->recentTopics()) {
		if (available <= 0) {
			break;
		} else if (_titles.size() == index) {
			_titles.emplace_back();
		}
		auto &title = _titles[index];
		const auto rootId = topic->rootId();
		const auto unread = topic->chatListBadgesState().unread;
		if (title.topicRootId != rootId || title.unread != unread) {
			const auto context = Core::MarkedTextContext{
				.session = &topic->session(),
				.customEmojiRepaint = customEmojiRepaint,
				.customEmojiLoopLimit = kIconLoopCount,
			};
			auto topicTitle = topic->titleWithIcon();
			title.title.setMarkedText(
				st::dialogsTextStyle,
				(unread
					? Ui::Text::PlainLink(
						Ui::Text::Wrapped(
							std::move(topicTitle),
							EntityType::Bold))
					: std::move(topicTitle)),
				DialogTextOptions(),
				context);
			title.topicRootId = rootId;
			title.unread = unread;
		}
		available -= title.title.maxWidth() + st->topicsSkip;
		++index;
	}
	while (_titles.size() > index) {
		_titles.pop_back();
	}
}

void TopicsView::paint(
		Painter &p,
		const QRect &geometry,
		const PaintContext &context) const {
	auto available = geometry.width();

	p.setFont(st::dialogsTextFont);
	p.setPen(context.active
		? st::dialogsTextFgActive
		: context.selected
		? st::dialogsTextFgOver
		: st::dialogsTextFg);
	const auto palette = &(context.active
		? st::dialogsTextPaletteArchiveActive
		: context.selected
		? st::dialogsTextPaletteArchiveOver
		: st::dialogsTextPaletteArchive);
	auto index = 0;
	auto rect = geometry;
	for (const auto &title : _titles) {
		if (rect.width() <= 0) {
			break;
		}
		title.title.draw(p, {
			.position = rect.topLeft(),
			.availableWidth = rect.width(),
			.palette = palette,
			.spoiler = Text::DefaultSpoilerCache(),
			.now = context.now,
			.paused = context.paused,
			.elisionLines = 1,
		});
		rect.setLeft(
			rect.left() + title.title.maxWidth() + context.st->topicsSkip);
	}
}

} // namespace Dialogs::Ui
