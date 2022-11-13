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

bool TopicsView::prepared() const {
	return (_version == _forum->recentTopicsListVersion());
}

void TopicsView::prepare(MsgId frontRootId, Fn<void()> customEmojiRepaint) {
	const auto &list = _forum->recentTopics();
	_version = _forum->recentTopicsListVersion();
	_titles.reserve(list.size());
	auto index = 0;
	for (const auto &topic : list) {
		const auto from = begin(_titles) + index;
		const auto rootId = topic->rootId();
		const auto i = ranges::find(
			from,
			end(_titles),
			rootId,
			&Title::topicRootId);
		if (i != end(_titles)) {
			if (i != from) {
				ranges::rotate(from, i, i + 1);
			}
		} else if (index >= _titles.size()) {
			_titles.emplace_back();
		}
		auto &title = _titles[index++];
		title.topicRootId = rootId;

		const auto unread = topic->chatListBadgesState().unread;
		if (title.unread == unread
			&& title.version == topic->titleVersion()) {
			continue;
		}
		const auto context = Core::MarkedTextContext{
			.session = &topic->session(),
			.customEmojiRepaint = customEmojiRepaint,
			.customEmojiLoopLimit = kIconLoopCount,
		};
		auto topicTitle = topic->titleWithIcon();
		title.version = topic->titleVersion();
		title.unread = unread;
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
	}
	while (_titles.size() > index) {
		_titles.pop_back();
	}
	const auto i = frontRootId
		? ranges::find(_titles, frontRootId, &Title::topicRootId)
		: end(_titles);
	_jumpToTopic = (i != end(_titles));
	if (_jumpToTopic) {
		if (i != begin(_titles)) {
			ranges::rotate(begin(_titles), i, i + 1);
		}
		if (!_titles.front().unread) {
			_jumpToTopic = false;
		}
	}
}

int TopicsView::jumpToTopicWidth() const {
	return _jumpToTopic ? _titles.front().title.maxWidth() : 0;
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
	auto skipBig = _jumpToTopic && !context.active;
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
		const auto skip = skipBig
			? context.st->topicsSkipBig
			: context.st->topicsSkip;
		rect.setLeft(rect.left() + title.title.maxWidth() + skip);
		skipBig = false;
	}
}

} // namespace Dialogs::Ui
