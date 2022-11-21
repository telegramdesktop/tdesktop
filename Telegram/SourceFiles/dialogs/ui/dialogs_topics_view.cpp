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
#include "ui/effects/ripple_animation.h"
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

bool TopicsView::changeTopicJumpGeometry(JumpToLastGeometry geometry) {
	if (_lastTopicJumpGeometry != geometry) {
		_lastTopicJumpGeometry = geometry;
		return true;
	}
	return false;
}

void TopicsView::clearTopicJumpGeometry() {
	changeTopicJumpGeometry({});
}

bool TopicsView::isInTopicJumpArea(int x, int y) const {
	return _lastTopicJumpGeometry.area1.contains(x, y)
		|| _lastTopicJumpGeometry.area2.contains(x, y);
}

void TopicsView::addTopicJumpRipple(
		QPoint origin,
		not_null<TopicJumpCache*> topicJumpCache,
		Fn<void()> updateCallback) {
	auto mask = topicJumpRippleMask(topicJumpCache);
	if (mask.isNull()) {
		return;
	}
	_ripple = std::make_unique<Ui::RippleAnimation>(
		st::dialogsRipple,
		std::move(mask),
		std::move(updateCallback));
	_ripple->add(origin);
}

void TopicsView::stopLastRipple() {
	if (_ripple) {
		_ripple->lastStop();
	}
}

void TopicsView::paintRipple(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		const QColor *colorOverride) const {
	if (_ripple) {
		_ripple->paint(p, x, y, outerWidth, colorOverride);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
}

QImage TopicsView::topicJumpRippleMask(
		not_null<TopicJumpCache*> topicJumpCache) const {
	const auto &st = st::forumDialogRow;
	const auto area1 = _lastTopicJumpGeometry.area1;
	if (area1.isEmpty()) {
		return QImage();
	}
	const auto area2 = _lastTopicJumpGeometry.area2;
	const auto drawer = [&](QPainter &p) {
		const auto white = style::complex_color([] { return Qt::white; });
		// p.setOpacity(.1);
		FillJumpToLastPrepared(p, {
			.st = &st,
			.corners = &topicJumpCache->rippleMask,
			.bg = white.color(),
			.prepared = _lastTopicJumpGeometry,
		});
	};
	return Ui::RippleAnimation::MaskByDrawer(
		QRect(0, 0, 1, 1).united(area1).united(area2).size(),
		false,
		drawer);
}

JumpToLastGeometry FillJumpToLastBg(QPainter &p, JumpToLastBg context) {
	const auto availableWidth = context.geometry.width();
	const auto use1 = std::min(context.width1, availableWidth);
	const auto use2 = std::min(context.width2, availableWidth);
	const auto padding = st::forumDialogJumpPadding;
	const auto origin = context.geometry.topLeft();
	const auto delta = std::abs(use1 - use2);
	if (delta <= context.st->topicsSkip / 2) {
		const auto w = std::max(use1, use2);
		const auto h = context.st->topicsHeight + st::normalFont->height;
		const auto fill = QRect(origin, QSize(w, h));
		const auto full = fill.marginsAdded(padding);
		auto result = JumpToLastGeometry{ full };
		FillJumpToLastPrepared(p, {
			.st = context.st,
			.corners = context.corners,
			.bg = context.bg,
			.prepared = result,
		});
		return result;
	}
	const auto h1 = context.st->topicsHeight;
	const auto h2 = st::normalFont->height;
	const auto rect1 = QRect(origin, QSize(use1, h1));
	const auto fill1 = rect1.marginsAdded({
		padding.left(),
		padding.top(),
		padding.right(),
		(use1 < use2 ? -padding.top() : padding.bottom()),
	});
	const auto add = QPoint(0, h1);
	const auto rect2 = QRect(origin + add, QSize(use2, h2));
	const auto fill2 = rect2.marginsAdded({
		padding.left(),
		(use2 < use1 ? -padding.bottom() : padding.top()),
		padding.right(),
		padding.bottom(),
	});
	auto result = JumpToLastGeometry{ fill1, fill2 };
	FillJumpToLastPrepared(p, {
		.st = context.st,
		.corners = context.corners,
		.bg = context.bg,
		.prepared = result,
	});
	return result;
}

void FillJumpToLastPrepared(QPainter &p, JumpToLastPrepared context) {
	auto &normal = context.corners->normal;
	auto &inverted = context.corners->inverted;
	auto &small = context.corners->small;
	const auto radius = st::forumDialogJumpRadius;
	const auto &bg = context.bg;
	const auto area1 = context.prepared.area1;
	const auto area2 = context.prepared.area2;
	if (area2.isNull()) {
		if (normal.p[0].isNull()) {
			normal = Ui::PrepareCornerPixmaps(radius, bg);
		}
		Ui::FillRoundRect(p, area1, bg, normal);
		return;
	}
	const auto width1 = area1.width();
	const auto width2 = area2.width();
	const auto delta = std::abs(width1 - width2);
	const auto h1 = context.st->topicsHeight;
	const auto h2 = st::normalFont->height;
	const auto hmin = std::min(h1, h2);
	const auto wantedInvertedRadius = hmin - radius;
	const auto invertedr = std::min(wantedInvertedRadius, delta / 2);
	const auto smallr = std::min(radius, delta - invertedr);
	const auto smallkey = (width1 < width2) ? smallr : (-smallr);
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
		small = pixmaps.p[(width1 < width2) ? 1 : 3];
	}
	auto no1 = normal;
	no1.p[2] = QPixmap();
	if (width1 < width2) {
		no1.p[3] = QPixmap();
	} else if (smallr != radius) {
		no1.p[3] = small;
	}
	Ui::FillRoundRect(p, area1, bg, no1);
	if (width1 < width2) {
		p.drawPixmap(
			area1.x() + width1,
			area1.y() + area1.height() - invertedr,
			inverted.p[3]);
	}
	auto no2 = normal;
	no2.p[0] = QPixmap();
	if (width2 < width1) {
		no2.p[1] = QPixmap();
	} else if (smallr != radius) {
		no2.p[1] = small;
	}
	Ui::FillRoundRect(p, area2, bg, no2);
	if (width2 < width1) {
		p.drawPixmap(
			area2.x() + width2,
			area2.y(),
			inverted.p[0]);
	}
}

} // namespace Dialogs::Ui
