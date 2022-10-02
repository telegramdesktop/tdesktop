/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/reactions/history_view_reactions_strip.h"

#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "main/main_session.h"
#include "ui/effects/frame_generator.h"
#include "ui/animated_icon.h"
#include "ui/painter.h"
#include "styles/style_chat.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kSizeForDownscale = 96;
constexpr auto kEmojiCacheIndex = 0;
constexpr auto kHoverScaleDuration = crl::time(200);
constexpr auto kHoverScale = 1.24;

[[nodiscard]] int MainReactionSize() {
	return style::ConvertScale(kSizeForDownscale);
}

[[nodiscard]] std::shared_ptr<Ui::AnimatedIcon> CreateIcon(
		not_null<Data::DocumentMedia*> media,
		int size) {
	Expects(media->loaded());

	return std::make_shared<Ui::AnimatedIcon>(Ui::AnimatedIconDescriptor{
		.generator = DocumentIconFrameGenerator(media),
		.sizeOverride = QSize(size, size),
	});
}

} // namespace

Strip::Strip(
	QRect inner,
	int size,
	Fn<void()> update,
	IconFactory iconFactory)
: _iconFactory(std::move(iconFactory))
, _inner(inner)
, _finalSize(size)
, _update(std::move(update)) {
}

void Strip::applyList(
		const std::vector<not_null<const Data::Reaction*>> &list,
		AddedButton button) {
	if (_button == button
		&& ranges::equal(
			ranges::make_subrange(
				begin(_icons),
				(begin(_icons)
					+ _icons.size()
					- (_button == AddedButton::None ? 0 : 1))),
			list,
			ranges::equal_to(),
			&ReactionIcons::id,
			&Data::Reaction::id)) {
		return;
	}
	const auto selected = _selectedIcon;
	setSelected(-1);
	_icons.clear();
	for (const auto &reaction : list) {
		_icons.push_back({
			.id = reaction->id,
			.appearAnimation = reaction->appearAnimation,
			.selectAnimation = reaction->selectAnimation,
		});
	}
	_button = button;
	if (_button != AddedButton::None) {
		_icons.push_back({ .added = _button });
	}
	setSelected((selected < _icons.size()) ? selected : -1);
	resolveMainReactionIcon();
}

void Strip::paint(
		QPainter &p,
		QPoint position,
		QPoint shift,
		QRect clip,
		float64 scale,
		bool hiding) {
	const auto skip = st::reactionAppearStartSkip;
	const auto animationRect = clip.marginsRemoved({ 0, skip, 0, skip });

	PainterHighQualityEnabler hq(p);
	const auto countTarget = resolveCountTargetMethod(scale);
	for (auto &icon : _icons) {
		const auto target = countTarget(icon).translated(position);
		position += shift;
		if (target.intersects(clip)) {
			paintOne(
				p,
				icon,
				position - shift,
				target,
				!hiding && target.intersects(animationRect));
		} else if (!hiding) {
			clearStateForHidden(icon);
		}
		if (!hiding) {
			clearStateForSelectFinished(icon);
		}
	}
}

auto Strip::resolveCountTargetMethod(float64 scale) const
-> Fn<QRectF(const ReactionIcons&)> {
	const auto hoveredSize = int(base::SafeRound(_finalSize * kHoverScale));
	const auto basicTargetForScale = [&](int size, float64 scale) {
		const auto remove = size * (1. - scale) / 2.;
		return QRectF(QRect(
			_inner.x() + (_inner.width() - size) / 2,
			_inner.y() + (_inner.height() - size) / 2,
			size,
			size
		)).marginsRemoved({ remove, remove, remove, remove });
	};
	const auto basicTarget = basicTargetForScale(_finalSize, scale);
	return [=](const ReactionIcons &icon) {
		const auto selectScale = icon.selectedScale.value(
			icon.selected ? kHoverScale : 1.);
		if (selectScale == 1.) {
			return basicTarget;
		}
		const auto finalScale = scale * selectScale;
		return (finalScale <= 1.)
			? basicTargetForScale(_finalSize, finalScale)
			: basicTargetForScale(hoveredSize, finalScale / kHoverScale);
	};
}

void Strip::paintOne(
		QPainter &p,
		ReactionIcons &icon,
		QPoint position,
		QRectF target,
		bool allowAppearStart) {
	if (icon.added == AddedButton::Premium) {
		paintPremiumIcon(p, position, target);
	} else if (icon.added == AddedButton::Expand) {
		paintExpandIcon(p, position, target);
	} else {
		const auto paintFrame = [&](not_null<Ui::AnimatedIcon*> animation) {
			const auto size = int(std::floor(target.width() + 0.01));
			const auto frame = animation->frame({ size, size }, _update);
			p.drawImage(target, frame.image);
		};

		const auto appear = icon.appear.get();
		if (appear && !icon.appearAnimated && allowAppearStart) {
			icon.appearAnimated = true;
			appear->animate(_update);
		}
		if (appear && appear->animating()) {
			paintFrame(appear);
		} else if (const auto select = icon.select.get()) {
			paintFrame(select);
		}
	}
}

void Strip::paintOne(
		QPainter &p,
		int index,
		QPoint position,
		float64 scale) {
	Expects(index >= 0 && index < _icons.size());

	auto &icon = _icons[index];
	const auto countTarget = resolveCountTargetMethod(scale);
	const auto target = countTarget(icon).translated(position);
	paintOne(p, icon, position, target, false);
}

bool Strip::inDefaultState(int index) const {
	Expects(index >= 0 && index < _icons.size());

	const auto &icon = _icons[index];
	return !icon.selected
		&& !icon.selectedScale.animating()
		&& icon.select
		&& !icon.select->animating()
		&& (!icon.appear || !icon.appear->animating());
}

bool Strip::empty() const {
	return _icons.empty();
}

int Strip::count() const {
	return _icons.size();
}

bool Strip::onlyAddedButton() const {
	return (_icons.size() == 1)
		&& (_icons.front().added != AddedButton::None);
}

int Strip::fillChosenIconGetIndex(ChosenReaction &chosen) const {
	const auto i = ranges::find(_icons, chosen.id, &ReactionIcons::id);
	if (i == end(_icons)) {
		return -1;
	}
	const auto &icon = *i;
	if (const auto &appear = icon.appear; appear && appear->animating()) {
		chosen.icon = appear->frame();
	} else if (const auto &select = icon.select; select && select->valid()) {
		chosen.icon = select->frame();
	}
	return (i - begin(_icons));
}

void Strip::paintPremiumIcon(
		QPainter &p,
		QPoint position,
		QRectF target) const {
	const auto to = QRect(
		_inner.x() + (_inner.width() - _finalSize) / 2,
		_inner.y() + (_inner.height() - _finalSize) / 2,
		_finalSize,
		_finalSize
	).translated(position);
	const auto scale = target.width() / to.width();
	if (scale != 1.) {
		p.save();
		p.translate(target.center());
		p.scale(scale, scale);
		p.translate(-target.center());
	}
	auto hq = PainterHighQualityEnabler(p);
	st::reactionPremiumLocked.paintInCenter(p, to);
	if (scale != 1.) {
		p.restore();
	}
}

void Strip::paintExpandIcon(
		QPainter &p,
		QPoint position,
		QRectF target) const {
	const auto to = QRect(
		_inner.x() + (_inner.width() - _finalSize) / 2,
		_inner.y() + (_inner.height() - _finalSize) / 2,
		_finalSize,
		_finalSize
	).translated(position);
	const auto scale = target.width() / to.width();
	if (scale != 1.) {
		p.save();
		p.translate(target.center());
		p.scale(scale, scale);
		p.translate(-target.center());
	}
	auto hq = PainterHighQualityEnabler(p);
	((_finalSize == st::reactionCornerImage)
		? st::reactionsExpandDropdown
		: st::reactionExpandPanel).paintInCenter(p, to);
	if (scale != 1.) {
		p.restore();
	}
}

void Strip::setSelected(int index) const {
	const auto set = [&](int index, bool selected) {
		if (index < 0 || index >= _icons.size()) {
			return;
		}
		auto &icon = _icons[index];
		if (icon.selected == selected) {
			return;
		}
		icon.selected = selected;
		icon.selectedScale.start(
			_update,
			selected ? 1. : kHoverScale,
			selected ? kHoverScale : 1.,
			kHoverScaleDuration,
			anim::sineInOut);
		if (selected) {
			const auto skipAnimation = icon.selectAnimated
				|| !icon.appearAnimated
				|| (icon.select && icon.select->animating())
				|| (icon.appear && icon.appear->animating());
			const auto select = skipAnimation ? nullptr : icon.select.get();
			if (select && !icon.selectAnimated) {
				icon.selectAnimated = true;
				select->animate(_update);
			}
		}
	};
	if (_selectedIcon != index) {
		set(_selectedIcon, false);
		_selectedIcon = index;
	}
	set(index, true);
}

auto Strip::selected() const -> std::variant<AddedButton, ReactionId> {
	if (_selectedIcon < 0 || _selectedIcon >= _icons.size()) {
		return {};
	}
	const auto &icon = _icons[_selectedIcon];
	if (icon.added != AddedButton::None) {
		return icon.added;
	}
	return icon.id;
}

int Strip::computeOverSize() const {
	return int(base::SafeRound(_finalSize * kHoverScale));
}

void Strip::clearAppearAnimations(bool mainAppeared) {
	auto main = mainAppeared;
	for (auto &icon : _icons) {
		if (!main) {
			if (icon.selected) {
				setSelected(-1);
			}
			icon.selectedScale.stop();
			if (const auto select = icon.select.get()) {
				select->jumpToStart(nullptr);
			}
			icon.selectAnimated = false;
		}
		if (icon.appearAnimated != main) {
			if (const auto appear = icon.appear.get()) {
				appear->jumpToStart(nullptr);
			}
			icon.appearAnimated = main;
		}
		main = false;
	}
}

void Strip::clearStateForHidden(ReactionIcons &icon) {
	if (const auto appear = icon.appear.get()) {
		appear->jumpToStart(nullptr);
	}
	if (icon.selected) {
		setSelected(-1);
	}
	icon.appearAnimated = false;
	icon.selectAnimated = false;
	if (const auto select = icon.select.get()) {
		select->jumpToStart(nullptr);
	}
	icon.selectedScale.stop();
}

void Strip::clearStateForSelectFinished(ReactionIcons &icon) {
	if (icon.selectAnimated
		&& !icon.select->animating()
		&& !icon.selected) {
		icon.selectAnimated = false;
	}
}

bool Strip::checkIconLoaded(ReactionDocument &entry) const {
	if (!entry.media) {
		return true;
	} else if (!entry.media->loaded()) {
		return false;
	}
	const auto size = (entry.media == _mainReactionMedia)
		? MainReactionSize()
		: _finalSize;
	entry.icon = _iconFactory(entry.media.get(), size);
	entry.media = nullptr;
	return true;
}

void Strip::loadIcons() {
	const auto load = [&](not_null<DocumentData*> document) {
		if (const auto i = _loadCache.find(document); i != end(_loadCache)) {
			return i->second.icon;
		}
		auto &entry = _loadCache.emplace(document).first->second;
		entry.media = document->createMediaView();
		entry.media->checkStickerLarge();
		if (!checkIconLoaded(entry) && !_loadCacheLifetime) {
			document->session().downloaderTaskFinished(
			) | rpl::start_with_next([=] {
				checkIcons();
			}, _loadCacheLifetime);
		}
		return entry.icon;
	};
	auto all = true;
	for (auto &icon : _icons) {
		if (icon.appearAnimation && !icon.appear) {
			icon.appear = load(icon.appearAnimation);
			if (!icon.appear) {
				all = false;
			}
		}
		if (icon.selectAnimation && !icon.select) {
			icon.select = load(icon.selectAnimation);
			if (!icon.select) {
				all = false;
			}
		}
	}
	if (all && !_icons.empty() && _icons.front().selectAnimation) {
		auto &data = _icons.front().selectAnimation->owner().reactions();
		for (const auto &icon : _icons) {
			data.preloadAnimationsFor(icon.id);
		}
	}
}

void Strip::checkIcons() {
	auto all = true;
	for (auto &[document, entry] : _loadCache) {
		if (!checkIconLoaded(entry)) {
			all = false;
		}
	}
	if (all) {
		_loadCacheLifetime.destroy();
		loadIcons();
	}
}

void Strip::resolveMainReactionIcon() {
	if (_icons.empty() || onlyAddedButton()) {
		_mainReactionMedia = nullptr;
		_mainReactionLifetime.destroy();
		return;
	}
	const auto main = _icons.front().selectAnimation;
	Assert(main != nullptr);
	_icons.front().appearAnimated = true;
	if (_mainReactionMedia && _mainReactionMedia->owner() == main) {
		if (!_mainReactionLifetime) {
			loadIcons();
		}
		return;
	}
	_mainReactionMedia = main->createMediaView();
	_mainReactionMedia->checkStickerLarge();
	if (_mainReactionMedia->loaded()) {
		_mainReactionLifetime.destroy();
		setMainReactionIcon();
	} else if (!_mainReactionLifetime) {
		main->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return _mainReactionMedia->loaded();
		}) | rpl::take(1) | rpl::start_with_next([=] {
			setMainReactionIcon();
		}, _mainReactionLifetime);
	}
}

void Strip::setMainReactionIcon() {
	_mainReactionLifetime.destroy();
	ranges::fill(_validEmoji, false);
	loadIcons();
	const auto i = _loadCache.find(_mainReactionMedia->owner());
	if (i != end(_loadCache) && i->second.icon) {
		const auto &icon = i->second.icon;
		if (!icon->frameIndex() && icon->width() == MainReactionSize()) {
			_mainReactionImage = i->second.icon->frame();
			return;
		}
	}
	_mainReactionImage = QImage();
	_mainReactionIcon = DefaultIconFactory(
		_mainReactionMedia.get(),
		MainReactionSize());
}

bool Strip::onlyMainEmojiVisible() const {
	if (_icons.empty()) {
		return true;
	}
	const auto &icon = _icons.front();
	if (icon.selected
		|| icon.selectedScale.animating()
		|| (icon.select && icon.select->animating())) {
		return false;
	}
	icon.selectAnimated = false;
	return true;
}

Ui::ImageSubrect Strip::validateEmoji(int frameIndex, float64 scale) {
	const auto area = _inner.size();
	const auto size = int(base::SafeRound(_finalSize * scale));
	const auto result = Ui::ImageSubrect{
		&_emojiParts,
		Ui::RoundAreaWithShadow::FrameCacheRect(
			frameIndex,
			kEmojiCacheIndex,
			area),
	};
	if (_validEmoji[frameIndex]) {
		return result;
	} else if (_emojiParts.isNull()) {
		_emojiParts = Ui::RoundAreaWithShadow::PrepareFramesCache(area);
	}

	auto p = QPainter(result.image);
	const auto ratio = style::DevicePixelRatio();
	const auto position = result.rect.topLeft() / ratio;
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(QRect(position, result.rect.size() / ratio), Qt::transparent);
	if (_mainReactionImage.isNull()
		&& _mainReactionIcon) {
		_mainReactionImage = base::take(_mainReactionIcon)->frame();
	}
	if (!_mainReactionImage.isNull()) {
		const auto target = QRect(
			(_inner.width() - size) / 2,
			(_inner.height() - size) / 2,
			size,
			size
		).translated(position);

		p.drawImage(target, _mainReactionImage.scaled(
			target.size() * ratio,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation));
	}

	_validEmoji[frameIndex] = true;
	return result;
}

IconFactory CachedIconFactory::createMethod() {
	return [=](not_null<Data::DocumentMedia*> media, int size) {
		const auto owned = media->owner()->createMediaView();
		const auto i = _cache.find(owned);
		return (i != end(_cache))
			? i->second
			: _cache.emplace(
				owned,
				DefaultIconFactory(media, size)).first->second;
	};
}

std::shared_ptr<Ui::AnimatedIcon> DefaultIconFactory(
		not_null<Data::DocumentMedia*> media,
		int size) {
	return CreateIcon(media, size);
}

} // namespace HistoryView::Reactions
