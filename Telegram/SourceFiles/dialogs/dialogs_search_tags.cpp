/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_search_tags.h"

#include "base/qt/qt_key_modifiers.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "history/view/reactions/history_view_reactions.h"
#include "ui/effects/animation_value.h"
#include "ui/power_saving.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace Dialogs {

struct SearchTags::Tag {
	Data::ReactionId id;
	std::unique_ptr<Ui::Text::CustomEmoji> custom;
	mutable QImage image;
	QRect geometry;
	ClickHandlerPtr link;
	bool selected = false;
};

SearchTags::SearchTags(
	not_null<Data::Session*> owner,
	rpl::producer<std::vector<Data::Reaction>> tags,
	std::vector<Data::ReactionId> selected)
: _owner(owner)
, _added(selected) {
	std::move(
		tags
	) | rpl::start_with_next([=](const std::vector<Data::Reaction> &list) {
		fill(list);
	}, _lifetime);

	// Mark the `selected` reactions as selected in `_tags`.
	for (const auto &id : selected) {
		const auto i = ranges::find(_tags, id, &Tag::id);
		if (i != end(_tags)) {
			i->selected = true;
		}
	}

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_normalBg = _selectedBg = QImage();
	}, _lifetime);
}

SearchTags::~SearchTags() = default;

void SearchTags::fill(const std::vector<Data::Reaction> &list) {
	const auto selected = collectSelected();
	_tags.clear();
	_tags.reserve(list.size());
	const auto link = [&](Data::ReactionId id) {
		return std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
			const auto i = ranges::find(_tags, id, &Tag::id);
			if (i != end(_tags)) {
				if (!i->selected && !base::IsShiftPressed()) {
					for (auto &tag : _tags) {
						tag.selected = false;
					}
				}
				i->selected = !i->selected;
				_selectedChanges.fire({});
			}
		}));
	};
	const auto push = [&](Data::ReactionId id) {
		const auto customId = id.custom();
		_tags.push_back({
			.id = id,
			.custom = (customId
				? _owner->customEmojiManager().create(
					customId,
					[=] { _repaintRequests.fire({}); })
				: nullptr),
			.link = link(id),
			.selected = ranges::contains(selected, id),
		});
		if (!customId) {
			_owner->reactions().preloadImageFor(id);
		}
	};
	for (const auto &reaction : list) {
		push(reaction.id);
	}
	for (const auto &reaction : _added) {
		if (!ranges::contains(_tags, reaction, &Tag::id)) {
			push(reaction);
		}
	}
	if (_width > 0) {
		layout();
	}
}

void SearchTags::layout() {
	Expects(_width > 0);

	const auto &bg = validateBg(false);
	const auto skip = st::dialogsSearchTagSkip;
	const auto size = bg.size() / bg.devicePixelRatio();
	const auto xsingle = size.width() + skip.x();
	const auto ysingle = size.height() + skip.y();
	const auto columns = std::max((_width + skip.x()) / xsingle, 1);
	const auto rows = (_tags.size() + columns - 1) / columns;
	for (auto row = 0; row != rows; ++row) {
		for (auto column = 0; column != columns; ++column) {
			const auto index = row * columns + column;
			if (index >= _tags.size()) {
				break;
			}
			const auto x = column * xsingle;
			const auto y = row * ysingle;
			_tags[index].geometry = QRect(QPoint(x, y), size);
		}
	}
	const auto bottom = st::dialogsSearchTagBottom;
	_height = rows ? (rows * ysingle - skip.y() + bottom) : 0;
}

void SearchTags::resizeToWidth(int width) {
	if (_width == width || width <= 0) {
		return;
	}
	_width = width;
	layout();
}

int SearchTags::height() const {
	return _height.current();
}

rpl::producer<int> SearchTags::heightValue() const {
	return _height.value();
}

rpl::producer<> SearchTags::repaintRequests() const {
	return _repaintRequests.events();
}

ClickHandlerPtr SearchTags::lookupHandler(QPoint point) const {
	for (const auto &tag : _tags) {
		if (tag.geometry.contains(point.x(), point.y())) {
			return tag.link;
		}
	}
	return nullptr;
}

auto SearchTags::selectedValue() const
-> rpl::producer<std::vector<Data::ReactionId>> {
	return _selectedChanges.events() | rpl::map([=] {
		return collectSelected();
	});
}

void SearchTags::paintCustomFrame(
		QPainter &p,
		not_null<Ui::Text::CustomEmoji*> emoji,
		QPoint innerTopLeft,
		crl::time now,
		bool paused,
		const QColor &textColor) const {
	if (_customCache.isNull()) {
		using namespace Ui::Text;
		const auto size = st::emojiSize;
		const auto factor = style::DevicePixelRatio();
		const auto adjusted = AdjustCustomEmojiSize(size);
		_customCache = QImage(
			QSize(adjusted, adjusted) * factor,
			QImage::Format_ARGB32_Premultiplied);
		_customCache.setDevicePixelRatio(factor);
		_customSkip = (size - adjusted) / 2;
	}
	_customCache.fill(Qt::transparent);
	auto q = QPainter(&_customCache);
	emoji->paint(q, {
		.textColor = textColor,
		.now = now,
		.paused = paused || On(PowerSaving::kEmojiChat),
	});
	q.end();
	_customCache = Images::Round(
		std::move(_customCache),
		(Images::Option::RoundLarge
			| Images::Option::RoundSkipTopRight
			| Images::Option::RoundSkipBottomRight));

	p.drawImage(
		innerTopLeft + QPoint(_customSkip, _customSkip),
		_customCache);
}

void SearchTags::paint(
		QPainter &p,
		QPoint position,
		crl::time now,
		bool paused) const {
	const auto size = st::reactionInlineSize;
	const auto skip = (size - st::reactionInlineImage) / 2;
	const auto padding = st::reactionInlinePadding;
	for (const auto &tag : _tags) {
		const auto geometry = tag.geometry.translated(position);
		p.drawImage(geometry.topLeft(), validateBg(tag.selected));
		if (!tag.custom && tag.image.isNull()) {
			tag.image = _owner->reactions().resolveImageFor(
				tag.id,
				::Data::Reactions::ImageSize::InlineList);
		}
		const auto inner = geometry.marginsRemoved(padding);
		const auto image = QRect(
			inner.topLeft() + QPoint(skip, skip),
			QSize(st::reactionInlineImage, st::reactionInlineImage));
		if (const auto custom = tag.custom.get()) {
			const auto textFg = tag.selected
				? st::dialogsNameFgActive->c
				: st::dialogsNameFgOver->c;
			paintCustomFrame(
				p,
				custom,
				inner.topLeft(),
				now,
				paused,
				textFg);
		} else if (!tag.image.isNull()) {
			p.drawImage(image.topLeft(), tag.image);
		}
	}
}

const QImage &SearchTags::validateBg(bool selected) const {
	using namespace HistoryView::Reactions;
	auto &image = selected ? _selectedBg : _normalBg;
	if (image.isNull()) {
		const auto tagBg = selected
			? st::dialogsBgActive->c
			: st::dialogsBgOver->c;
		const auto dotBg = selected
			? anim::with_alpha(tagBg, InlineList::TagDotAlpha())
			: st::windowSubTextFg->c;
		image = InlineList::PrepareTagBg(tagBg, dotBg);
	}
	return image;
}

std::vector<Data::ReactionId> SearchTags::collectSelected() const {
	return _tags | ranges::views::filter(
		&Tag::selected
	) | ranges::views::transform(
		&Tag::id
	) | ranges::to_vector;
}

rpl::lifetime &SearchTags::lifetime() {
	return _lifetime;
}

} // namespace Dialogs
