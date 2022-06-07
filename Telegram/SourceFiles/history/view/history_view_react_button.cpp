/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_react_button.h"

#include "history/view/history_view_cursor_state.h"
#include "history/history_item.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "ui/widgets/popup_menu.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_peer_values.h"
#include "lang/lang_keys.h"
#include "core/click_handler_types.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "base/event_filter.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kDivider = 4;
constexpr auto kToggleDuration = crl::time(120);
constexpr auto kActivateDuration = crl::time(150);
constexpr auto kExpandDuration = crl::time(300);
constexpr auto kCollapseDuration = crl::time(250);
constexpr auto kBgCacheIndex = 0;
constexpr auto kShadowCacheIndex = 0;
constexpr auto kEmojiCacheIndex = 1;
constexpr auto kCacheColumsCount = 2;
constexpr auto kOverlayMaskCacheIndex = 0;
constexpr auto kOverlayShadowCacheIndex = 1;
constexpr auto kOverlayCacheColumsCount = 2;
constexpr auto kButtonShowDelay = crl::time(300);
constexpr auto kButtonExpandDelay = crl::time(25);
constexpr auto kButtonHideDelay = crl::time(300);
constexpr auto kButtonExpandedHideDelay = crl::time(0);
constexpr auto kSizeForDownscale = 96;
constexpr auto kHoverScaleDuration = crl::time(200);
constexpr auto kHoverScale = 1.24;
constexpr auto kMaxReactionsScrollAtOnce = 2;

[[nodiscard]] QPoint LocalPosition(not_null<QWheelEvent*> e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return e->position().toPoint();
#else // Qt >= 6.0
	return e->pos();
#endif // Qt >= 6.0
}

[[nodiscard]] QSize CountMaxSizeWithMargins(style::margins margins) {
	return QRect(
		QPoint(),
		st::reactionCornerSize
	).marginsAdded(margins).size();
}

[[nodiscard]] QSize CountOuterSize() {
	return CountMaxSizeWithMargins(st::reactionCornerShadow);
}

[[nodiscard]] int CornerImageSize(float64 scale) {
	return int(base::SafeRound(st::reactionCornerImage * scale));
}

[[nodiscard]] int MainReactionSize() {
	return style::ConvertScale(kSizeForDownscale);
}

[[nodiscard]] std::shared_ptr<Lottie::Icon> CreateIcon(
		not_null<Data::DocumentMedia*> media,
		int size,
		int frame) {
	Expects(media->loaded());

	return std::make_shared<Lottie::Icon>(Lottie::IconDescriptor{
		.path = media->owner()->filepath(true),
		.json = media->bytes(),
		.sizeOverride = QSize(size, size),
		.frame = frame,
	});
}

} // namespace

Button::Button(
	Fn<void(QRect)> update,
	ButtonParameters parameters,
	Fn<void()> hideMe)
: _update(std::move(update))
, _finalScale(ScaleForState(_state))
, _collapsed(QPoint(), CountOuterSize())
, _finalHeight(_collapsed.height())
, _expandTimer([=] { applyState(State::Inside, _update); })
, _hideTimer(hideMe) {
	applyParameters(parameters, nullptr);
}

Button::~Button() = default;

bool Button::isHidden() const {
	return (_state == State::Hidden) && !_opacityAnimation.animating();
}

QRect Button::geometry() const {
	return _geometry;
}

int Button::expandedHeight() const {
	return _expandedHeight;
}

int Button::scroll() const {
	return _scroll;
}

int Button::scrollMax() const {
	return _expandedInnerHeight - _expandedHeight;
}

float64 Button::expandAnimationOpacity(float64 expandRatio) const {
	return (_collapseType == CollapseType::Fade)
		? expandRatio
		: 1.;
}

int Button::expandAnimationScroll(float64 expandRatio) const {
	return (_collapseType == CollapseType::Scroll && expandRatio < 1.)
		? std::clamp(int(base::SafeRound(expandRatio * _scroll)), 0, _scroll)
		: _scroll;
}

bool Button::expandUp() const {
	return (_expandDirection == ExpandDirection::Up);
}

bool Button::consumeWheelEvent(not_null<QWheelEvent*> e) {
	const auto scrollMax = (_expandedInnerHeight - _expandedHeight);
	if (_state != State::Inside
		|| scrollMax <= 0
		|| !_geometry.contains(LocalPosition(e))) {
		return false;
	}
	const auto delta = e->angleDelta();
	const auto horizontal = std::abs(delta.x()) > std::abs(delta.y());
	if (horizontal) {
		return false;
	}
	const auto between = st::reactionCornerSkip;
	const auto oneHeight = (st::reactionCornerSize.height() + between);
	const auto max = oneHeight * kMaxReactionsScrollAtOnce;
	const auto shift = std::clamp(
		delta.y() * (expandUp() ? 1 : -1),
		-max,
		max);
	_scroll = std::clamp(_scroll + shift, 0, scrollMax);
	_update(_geometry);
	e->accept();
	return true;
}

void Button::applyParameters(ButtonParameters parameters) {
	applyParameters(std::move(parameters), _update);
}

void Button::applyParameters(
		ButtonParameters parameters,
		Fn<void(QRect)> update) {
	const auto shift = parameters.center - _collapsed.center();
	_collapsed = _collapsed.translated(shift);
	updateGeometry(update);
	const auto inner = _geometry.marginsRemoved(st::reactionCornerShadow);
	const auto active = inner.marginsAdded(
		st::reactionCornerActiveAreaPadding
	).contains(parameters.pointer);
	const auto inside = inner.contains(parameters.pointer)
		|| (active && (_state == State::Inside));
	if (_state != State::Inside && !_heightAnimation.animating()) {
		updateExpandDirection(parameters);
	}
	const auto delayInside = inside && (_state != State::Inside);
	if (!delayInside) {
		_expandTimer.cancel();
		_lastGlobalPosition = std::nullopt;
	} else {
		const auto globalPositionChanged = _lastGlobalPosition
			&& (*_lastGlobalPosition != parameters.globalPointer);
		if (globalPositionChanged || _state == State::Hidden) {
			_expandTimer.callOnce(kButtonExpandDelay);
		}
		_lastGlobalPosition = parameters.globalPointer;
	}
	const auto wasInside = (_state == State::Inside);
	const auto state = (inside && !delayInside)
		? State::Inside
		: active
		? State::Active
		: State::Shown;
	applyState(state, update);
	if (parameters.outside && _state == State::Shown) {
		_hideTimer.callOnce(wasInside
			? kButtonExpandedHideDelay
			: kButtonHideDelay);
	} else {
		_hideTimer.cancel();
	}
}

void Button::updateExpandDirection(const ButtonParameters &parameters) {
	const auto maxAddedHeight = (parameters.reactionsCount - 1)
		* (st::reactionCornerSize.height() + st::reactionCornerSkip)
		+ (parameters.reactionsCount > 1 ? 2 * st::reactionExpandedSkip : 0);
	_expandedInnerHeight = _collapsed.height() + maxAddedHeight;
	const auto addedHeight = std::min(
		maxAddedHeight,
		st::reactionCornerAddedHeightMax);
	_expandedHeight = _collapsed.height() + addedHeight;
	_scroll = std::clamp(_scroll, 0, scrollMax());
	if (parameters.reactionsCount < 2) {
		return;
	}
	const auto up = (_collapsed.y() - addedHeight >= parameters.visibleTop)
		|| (_collapsed.y() + _collapsed.height() + addedHeight
			> parameters.visibleBottom);
	_expandDirection = up ? ExpandDirection::Up : ExpandDirection::Down;
}

void Button::updateGeometry(Fn<void(QRect)> update) {
	const auto added = int(base::SafeRound(
		_heightAnimation.value(_finalHeight)
	)) - _collapsed.height();
	if (!added && _state != State::Inside) {
		_scroll = 0;
	}
	const auto geometry = _collapsed.marginsAdded({
		0,
		(_expandDirection == ExpandDirection::Up) ? added : 0,
		0,
		(_expandDirection == ExpandDirection::Down) ? added : 0,
	});
	if (_geometry != geometry) {
		if (update) {
			update(_geometry);
		}
		_geometry = geometry;
		if (update) {
			update(_geometry);
		}
	}
}

void Button::applyState(State state) {
	applyState(state, _update);
}

void Button::applyState(State state, Fn<void(QRect)> update) {
	if (state == State::Hidden) {
		_expandTimer.cancel();
		_hideTimer.cancel();
	}
	const auto finalHeight = (state == State::Hidden)
		? _heightAnimation.value(_finalHeight)
		: (state == State::Inside)
		? _expandedHeight
		: _collapsed.height();
	if (_finalHeight != finalHeight) {
		if (state == State::Hidden) {
			_heightAnimation.stop();
		} else {
			if (!_heightAnimation.animating()) {
				_collapseType = (_scroll < st::reactionCollapseFadeThreshold)
					? CollapseType::Scroll
					: CollapseType::Fade;
			}
			_heightAnimation.start(
				[=] { updateGeometry(_update); },
				_finalHeight,
				finalHeight,
				(state == State::Inside
					? kExpandDuration
					: kCollapseDuration),
				anim::easeOutCirc);
		}
		_finalHeight = finalHeight;
	}
	updateGeometry(update);
	if (_state == state) {
		return;
	}
	const auto duration = (state == State::Hidden || _state == State::Hidden)
		? kToggleDuration
		: kActivateDuration;
	const auto finalScale = ScaleForState(state);
	_opacityAnimation.start(
		[=] { _update(_geometry); },
		OpacityForScale(ScaleForState(_state)),
		OpacityForScale(ScaleForState(state)),
		duration,
		anim::sineInOut);
	if (state != State::Hidden && _finalScale != finalScale) {
		_scaleAnimation.start(
			[=] { _update(_geometry); },
			_finalScale,
			finalScale,
			duration,
			anim::sineInOut);
		_finalScale = finalScale;
	}
	_state = state;
}

float64 Button::ScaleForState(State state) {
	switch (state) {
	case State::Hidden: return 1. / 3;
	case State::Shown: return 2. / 3;
	case State::Active:
	case State::Inside: return 1.;
	}
	Unexpected("State in ReactionButton::ScaleForState.");
}

float64 Button::OpacityForScale(float64 scale) {
	return std::min(
		((scale - ScaleForState(State::Hidden))
			/ (ScaleForState(State::Shown) - ScaleForState(State::Hidden))),
		1.);
}

float64 Button::currentScale() const {
	return _scaleAnimation.value(_finalScale);
}

float64 Button::currentOpacity() const {
	return _opacityAnimation.value(OpacityForScale(ScaleForState(_state)));
}

Manager::Manager(
	QWidget *wheelEventsTarget,
	rpl::producer<int> uniqueLimitValue,
	Fn<void(QRect)> buttonUpdate,
	IconFactory iconFactory)
: _iconFactory(std::move(iconFactory))
, _outer(CountOuterSize())
, _inner(QRect({}, st::reactionCornerSize))
, _overlayFull(
	QRect(0, 0, _inner.width(), _inner.width()).marginsAdded(
		st::reactionCornerShadow
	).size())
, _uniqueLimit(std::move(uniqueLimitValue))
, _buttonShowTimer([=] { showButtonDelayed(); })
, _buttonUpdate(std::move(buttonUpdate)) {
	static_assert(!(kFramesCount % kDivider));

	_inner.translate(QRect({}, _outer).center() - _inner.center());

	const auto ratio = style::DevicePixelRatio();
	_cacheBg = QImage(
		_outer.width() * kDivider * ratio,
		_outer.height() * kFramesCount / kDivider * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_cacheBg.setDevicePixelRatio(ratio);
	_cacheBg.fill(Qt::transparent);
	_cacheParts = QImage(
		_outer.width() * kDivider * kCacheColumsCount * ratio,
		_outer.height() * kFramesCount / kDivider * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_cacheParts.setDevicePixelRatio(ratio);
	_cacheParts.fill(Qt::transparent);
	_overlayCacheParts = QImage(
		_overlayFull.width() * kDivider * kOverlayCacheColumsCount * ratio,
		_overlayFull.height() * kFramesCount / kDivider * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_overlayCacheParts.setDevicePixelRatio(ratio);
	_overlayMaskScaled = QImage(
		_overlayFull * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_overlayMaskScaled.setDevicePixelRatio(ratio);
	_overlayShadowScaled = QImage(
		_overlayFull * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_overlayShadowScaled.setDevicePixelRatio(ratio);
	_shadowBuffer = QImage(
		_outer * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_shadowBuffer.setDevicePixelRatio(ratio);
	_expandedBuffer = QImage(
		_outer.width() * ratio,
		(_outer.height() + st::reactionCornerAddedHeightMax) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_expandedBuffer.setDevicePixelRatio(ratio);

	if (wheelEventsTarget) {
		stealWheelEvents(wheelEventsTarget);
	}

	_uniqueLimit.changes(
	) | rpl::start_with_next([=] {
		applyListFilters();
	}, _lifetime);

	_createChooseCallback = [=](QString emoji) {
		return [=] {
			if (auto chosen = lookupChosen(emoji)) {
				updateButton({});
				_chosen.fire(std::move(chosen));
			}
		};
	};
}

Manager::Chosen Manager::lookupChosen(const QString &emoji) const {
	auto result = Chosen{
		.context = _buttonContext,
		.emoji = emoji,
	};
	const auto button = _button.get();
	const auto i = ranges::find(_icons, emoji, &ReactionIcons::emoji);
	if (i == end(_icons) || !button) {
		return result;
	}
	const auto &icon = *i;
	if (const auto &appear = icon->appear; appear && appear->animating()) {
		result.icon = CreateIcon(
			icon->appearAnimation->activeMediaView().get(),
			appear->width(),
			appear->frameIndex());
	} else if (const auto &select = icon->select) {
		result.icon = CreateIcon(
			icon->selectAnimation->activeMediaView().get(),
			select->width(),
			select->frameIndex());
	}
	const auto index = (i - begin(_icons));
	const auto between = st::reactionCornerSkip;
	const auto oneHeight = (st::reactionCornerSize.height() + between);
	const auto expanded = (_icons.size() > 1);
	const auto skip = (expanded ? st::reactionExpandedSkip : 0);
	const auto scroll = button->scroll();
	const auto local = skip + index * oneHeight - scroll;
	const auto geometry = button->geometry();
	const auto top = button->expandUp()
		? (geometry.height() - local - _outer.height())
		: local;
	const auto rect = QRect(geometry.topLeft() + QPoint(0, top), _outer);
	const auto imageSize = int(base::SafeRound(
		st::reactionCornerImage * kHoverScale));
	result.geometry = QRect(
		rect.x() + (rect.width() - imageSize) / 2,
		rect.y() + (rect.height() - imageSize) / 2,
		imageSize,
		imageSize);
	return result;
}

void Manager::applyListFilters() {
	const auto limit = _uniqueLimit.current();
	const auto applyUniqueLimit = _buttonContext
		&& (limit > 0)
		&& (_buttonAlreadyNotMineCount >= limit);
	auto icons = std::vector<not_null<ReactionIcons*>>();
	icons.reserve(_list.size());
	auto showPremiumLock = (ReactionIcons*)nullptr;
	auto favoriteIndex = -1;
	for (auto &icon : _list) {
		const auto &emoji = icon.emoji;
		const auto add = applyUniqueLimit
			? _buttonAlreadyList.contains(emoji)
			: (!_filter || _filter->contains(emoji));
		if (add) {
			if (icon.premium
				&& !_allowSendingPremium
				&& !_buttonAlreadyList.contains(emoji)) {
				if (_premiumPossible) {
					showPremiumLock = &icon;
				} else {
					clearStateForHidden(icon);
				}
			} else {
				icon.premiumLock = false;
				if (emoji == _favorite) {
					favoriteIndex = int(icons.size());
				}
				icons.push_back(&icon);
			}
		} else {
			clearStateForHidden(icon);
		}
	}
	if (showPremiumLock) {
		showPremiumLock->premiumLock = true;
		icons.push_back(showPremiumLock);
	}
	if (favoriteIndex > 0) {
		const auto first = begin(icons);
		std::rotate(first, first + favoriteIndex, first + favoriteIndex + 1);
	}
	if (_icons == icons) {
		return;
	}
	const auto selected = _selectedIcon;
	setSelectedIcon(-1);
	_icons = std::move(icons);
	setSelectedIcon((selected < _icons.size()) ? selected : -1);
	resolveMainReactionIcon();
}

void Manager::stealWheelEvents(not_null<QWidget*> target) {
	base::install_event_filter(target, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::Wheel
			|| !consumeWheelEvent(static_cast<QWheelEvent*>(e.get()))) {
			return base::EventFilterResult::Continue;
		}
		Ui::SendSynteticMouseEvent(target, QEvent::MouseMove, Qt::NoButton);
		return base::EventFilterResult::Cancel;
	});
}

Manager::~Manager() = default;

void Manager::updateButton(ButtonParameters parameters) {
	if (parameters.cursorLeft && _menu) {
		return;
	}
	const auto contextChanged = (_buttonContext != parameters.context);
	if (contextChanged) {
		setSelectedIcon(-1);
		if (_button) {
			_button->applyState(ButtonState::Hidden);
			_buttonHiding.push_back(std::move(_button));
		}
		_buttonShowTimer.cancel();
		_scheduledParameters = std::nullopt;
	}
	_buttonContext = parameters.context;
	parameters.reactionsCount = _icons.size();
	if (!_buttonContext || !parameters.reactionsCount) {
		return;
	} else if (_button) {
		_button->applyParameters(parameters);
		if (_button->geometry().height() == _outer.height()) {
			clearAppearAnimations();
		}
		return;
	} else if (parameters.outside) {
		_buttonShowTimer.cancel();
		_scheduledParameters = std::nullopt;
		return;
	}
	const auto globalPositionChanged = _scheduledParameters
		&& (_scheduledParameters->globalPointer != parameters.globalPointer);
	const auto positionChanged = _scheduledParameters
		&& (_scheduledParameters->pointer != parameters.pointer);
	_scheduledParameters = parameters;
	if ((_buttonShowTimer.isActive() && positionChanged)
		|| globalPositionChanged) {
		_buttonShowTimer.callOnce(kButtonShowDelay);
	}
}

void Manager::showButtonDelayed() {
	clearAppearAnimations();
	_button = std::make_unique<Button>(
		_buttonUpdate,
		*_scheduledParameters,
		[=]{ updateButton({}); });
}

void Manager::applyList(
		const std::vector<Data::Reaction> &list,
		const QString &favorite,
		bool premiumPossible) {
	const auto possibleChanged = (_premiumPossible != premiumPossible);
	_premiumPossible = premiumPossible;
	const auto proj = [](const auto &obj) {
		return std::tie(
			obj.emoji,
			obj.appearAnimation,
			obj.selectAnimation,
			obj.premium);
	};
	const auto favoriteChanged = (_favorite != favorite);
	if (favoriteChanged) {
		_favorite = favorite;
	}
	if (ranges::equal(_list, list, ranges::equal_to(), proj, proj)) {
		if (favoriteChanged || possibleChanged) {
			applyListFilters();
		}
		return;
	}
	const auto selected = _selectedIcon;
	setSelectedIcon(-1);
	_icons.clear();
	_list.clear();
	for (const auto &reaction : list) {
		_list.push_back({
			.emoji = reaction.emoji,
			.appearAnimation = reaction.appearAnimation,
			.selectAnimation = reaction.selectAnimation,
			.premium = reaction.premium,
		});
	}
	applyListFilters();
	setSelectedIcon((selected < _icons.size()) ? selected : -1);
}

void Manager::updateAllowedSublist(AllowedSublist filter) {
	if (_filter == filter) {
		return;
	}
	_filter = std::move(filter);
	applyListFilters();
}

void Manager::updateAllowSendingPremium(bool allow) {
	if (_allowSendingPremium == allow) {
		return;
	}
	_allowSendingPremium = allow;
	applyListFilters();
}

const Manager::AllowedSublist &Manager::allowedSublist() const {
	return _filter;
}

void Manager::updateUniqueLimit(not_null<HistoryItem*> item) {
	if (item->fullId() != _buttonContext) {
		return;
	}
	const auto &all = item->reactions();
	const auto my = item->chosenReaction();
	auto list = base::flat_set<QString>();
	list.reserve(all.size());
	auto myIsUnique = false;
	for (const auto &[emoji, count] : all) {
		list.emplace(emoji);
		if (count == 1 && emoji == my) {
			myIsUnique = true;
		}
	}
	const auto notMineCount = int(list.size()) - (myIsUnique ? 1 : 0);

	auto changed = false;
	if (_buttonAlreadyList != list) {
		_buttonAlreadyList = std::move(list);
		changed = true;
	}
	if (_buttonAlreadyNotMineCount != notMineCount) {
		_buttonAlreadyNotMineCount = notMineCount;
		changed = true;
	}
	if (changed) {
		applyListFilters();
	}
}

void Manager::resolveMainReactionIcon() {
	if (_icons.empty()) {
		_mainReactionMedia = nullptr;
		_mainReactionLifetime.destroy();
		return;
	}
	const auto main = _icons.front()->selectAnimation;
	_icons.front()->appearAnimated = true;
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

void Manager::setMainReactionIcon() {
	_mainReactionLifetime.destroy();
	ranges::fill(_validBg, false);
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

QMargins Manager::innerMargins() const {
	return {
		_inner.x(),
		_inner.y(),
		_outer.width() - _inner.x() - _inner.width(),
		_outer.height() - _inner.y() - _inner.height(),
	};
}

QRect Manager::buttonInner() const {
	return buttonInner(_button.get());
}

QRect Manager::buttonInner(not_null<Button*> button) const {
	return button->geometry().marginsRemoved(innerMargins());
}

bool Manager::checkIconLoaded(ReactionDocument &entry) const {
	if (!entry.media) {
		return true;
	} else if (!entry.media->loaded()) {
		return false;
	}
	const auto size = (entry.media == _mainReactionMedia)
		? MainReactionSize()
		: CornerImageSize(1.);
	entry.icon = _iconFactory(entry.media.get(), size);
	entry.media = nullptr;
	return true;
}

void Manager::updateCurrentButton() const {
	if (const auto button = _button.get()) {
		_buttonUpdate(button->geometry());
	}
}

void Manager::loadIcons() {
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
	for (const auto &icon : _icons) {
		if (!icon->appear) {
			icon->appear = load(icon->appearAnimation);
		}
		if (!icon->select) {
			icon->select = load(icon->selectAnimation);
		}
		if (!icon->appear || !icon->select) {
			all = false;
		}
	}
	if (all && !_icons.empty()) {
		auto &data = _icons.front()->appearAnimation->owner().reactions();
		for (const auto &icon : _icons) {
			data.preloadAnimationsFor(icon->emoji);
		}
	}
}

void Manager::checkIcons() {
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

void Manager::removeStaleButtons() {
	_buttonHiding.erase(
		ranges::remove_if(_buttonHiding, &Button::isHidden),
		end(_buttonHiding));
}

void Manager::paint(Painter &p, const PaintContext &context) {
	removeStaleButtons();
	for (const auto &button : _buttonHiding) {
		paintButton(p, context, button.get());
	}
	if (const auto current = _button.get()) {
		paintButton(p, context, current);
	}

	for (const auto &[id, effect] : _collectedEffects) {
		const auto offset = effect.effectOffset;
		p.translate(offset);
		_activeEffectAreas[id] = effect.effectPaint(p).translated(offset);
		p.translate(-offset);
	}
	_collectedEffects.clear();
}

ClickHandlerPtr Manager::computeButtonLink(QPoint position) const {
	if (_icons.empty()) {
		setSelectedIcon(-1);
		return nullptr;
	}
	const auto inner = buttonInner();
	const auto top = _button->expandUp()
		? (inner.y() + inner.height() - position.y())
		: (position.y() - inner.y());
	const auto shifted = top + _button->scroll();
	const auto between = st::reactionCornerSkip;
	const auto oneHeight = (st::reactionCornerSize.height() + between);
	const auto index = std::clamp(
		int(base::SafeRound(shifted + between / 2.)) / oneHeight,
		0,
		int(_icons.size() - 1));
	auto &result = _icons[index]->link;
	if (!result) {
		result = resolveButtonLink(*_icons[index]);
	}
	setSelectedIcon(index);
	return result;
}

void Manager::setSelectedIcon(int index) const {
	const auto setSelected = [&](int index, bool selected) {
		if (index < 0 || index >= _icons.size()) {
			return;
		}
		const auto &icon = _icons[index];
		if (icon->selected == selected) {
			return;
		}
		icon->selected = selected;
		icon->selectedScale.start(
			[=] { updateCurrentButton(); },
			selected ? 1. : kHoverScale,
			selected ? kHoverScale : 1.,
			kHoverScaleDuration,
			anim::sineInOut);
		if (selected) {
			const auto skipAnimation = icon->selectAnimated
				|| !icon->appearAnimated
				|| (icon->select && icon->select->animating())
				|| (icon->appear && icon->appear->animating());
			const auto select = skipAnimation ? nullptr : icon->select.get();
			if (select && !icon->selectAnimated) {
				icon->selectAnimated = true;
				select->animate(
					crl::guard(this, [=] { updateCurrentButton(); }),
					0,
					select->framesCount() - 1);
			}
		}
	};
	if (_selectedIcon != index) {
		setSelected(_selectedIcon, false);
		_selectedIcon = index;
	}
	setSelected(index, true);
}

ClickHandlerPtr Manager::resolveButtonLink(
		const ReactionIcons &reaction) const {
	const auto emoji = reaction.emoji;
	const auto i = _reactionsLinks.find(emoji);
	if (i != end(_reactionsLinks)) {
		return i->second;
	}
	auto handler = std::make_shared<LambdaClickHandler>(
		crl::guard(this, _createChooseCallback(emoji)));
	handler->setProperty(kSendReactionEmojiProperty, emoji);
	return _reactionsLinks.emplace(emoji, std::move(handler)).first->second;
}

TextState Manager::buttonTextState(QPoint position) const {
	if (overCurrentButton(position)) {
		auto result = TextState(nullptr, computeButtonLink(position));
		result.itemId = _buttonContext;
		return result;
	} else {
		setSelectedIcon(-1);
	}
	return {};
}

bool Manager::overCurrentButton(QPoint position) const {
	if (!_button) {
		return false;
	}
	return _button && buttonInner().contains(position);
}

void Manager::remove(FullMsgId context) {
	_activeEffectAreas.remove(context);
	if (_buttonContext == context) {
		_buttonContext = {};
		_button = nullptr;
	}
}

bool Manager::consumeWheelEvent(not_null<QWheelEvent*> e) {
	return _button && _button->consumeWheelEvent(e);
}

void Manager::paintButton(
		Painter &p,
		const PaintContext &context,
		not_null<Button*> button) {
	const auto geometry = button->geometry();
	if (!context.clip.intersects(geometry)) {
		return;
	}
	const auto scale = button->currentScale();
	const auto scaleMin = Button::ScaleForState(ButtonState::Hidden);
	const auto scaleMax = Button::ScaleForState(ButtonState::Active);
	const auto progress = (scale - scaleMin) / (scaleMax - scaleMin);
	const auto frame = int(base::SafeRound(progress * (kFramesCount - 1)));
	const auto useScale = scaleMin
		+ (frame / float64(kFramesCount - 1)) * (scaleMax - scaleMin);
	paintButton(p, context, button, frame, useScale);
}

void Manager::paintButton(
		Painter &p,
		const PaintContext &context,
		not_null<Button*> button,
		int frameIndex,
		float64 scale) {
	const auto opacity = button->currentOpacity();
	if (opacity == 0.) {
		return;
	}

	const auto geometry = button->geometry();
	const auto position = geometry.topLeft();
	const auto size = geometry.size();
	const auto expanded = (size.height() - _outer.height());
	if (opacity != 1.) {
		p.setOpacity(opacity);
	}
	auto layeredPainter = std::optional<Painter>();
	if (expanded) {
		_expandedBuffer.fill(Qt::transparent);
	}
	const auto q = expanded ? &layeredPainter.emplace(&_expandedBuffer) : &p;
	const auto shadow = context.st->shadowFg()->c;
	const auto background = context.st->windowBg()->c;
	setShadowColor(shadow);
	setBackgroundColor(background);
	if (expanded) {
		q->fillRect(QRect(QPoint(), size), context.st->windowBg());
	} else {
		const auto source = validateFrame(
			frameIndex,
			scale,
			background,
			shadow);
		p.drawImage(position, _cacheBg, source);
	}

	const auto current = (button == _button.get());
	const auto expandRatio = expanded
		? std::clamp(
			float64(expanded) / (button->expandedHeight() - _outer.height()),
			0.,
			1.)
		: 0.;
	const auto expandedSkip = int(base::SafeRound(
		expandRatio * st::reactionExpandedSkip));
	const auto mainEmojiPosition = !expanded
		? position
		: button->expandUp()
		? QPoint(0, expanded - expandedSkip)
		: QPoint(0, expandedSkip);
	const auto source = validateEmoji(frameIndex, scale);
	if (expanded
		|| (current && !onlyMainEmojiVisible())
		|| (_icons.size() == 1 && _icons.front()->premiumLock)) {
		const auto origin = expanded ? QPoint() : position;
		const auto scroll = button->expandAnimationScroll(expandRatio);
		const auto opacity = button->expandAnimationOpacity(expandRatio);
		if (opacity != 1.) {
			q->setOpacity(opacity);
		}
		paintAllEmoji(*q, button, scroll, scale, origin, mainEmojiPosition);
		if (opacity != 1.) {
			q->setOpacity(1.);
		}
		if (current && expanded) {
			_showingAll = true;
		}
		if (expanded) {
			paintInnerGradients(*q, background, button, scroll, expandRatio);
		}
		if (opacity != 1.) {
			const auto appearShift = st::reactionMainAppearShift * opacity;
			const auto appearPosition = !expanded
				? position
				: button->expandUp()
				? QPoint(0, expanded - appearShift)
				: QPoint(0, appearShift);
			q->setOpacity(1. - opacity);
			q->drawImage(appearPosition, _cacheParts, source);
			q->setOpacity(1.);
		}
	} else {
		p.drawImage(mainEmojiPosition, _cacheParts, source);
	}
	if (current && !expanded) {
		clearAppearAnimations();
	}

	if (expanded) {
		overlayExpandedBorder(*q, size, expandRatio, scale, shadow);
		layeredPainter.reset();
		p.drawImage(
			geometry,
			_expandedBuffer,
			QRect(QPoint(), size * style::DevicePixelRatio()));
	}
	if (opacity != 1.) {
		p.setOpacity(1.);
	}
}

void Manager::paintInnerGradients(
		Painter &p,
		const QColor &background,
		not_null<Button*> button,
		int scroll,
		float64 expandRatio) {
	const auto endScroll = button->scrollMax() - scroll;
	const auto size = st::reactionGradientSize;
	const auto ensureGradient = [&](QImage &gradient, bool top) {
		if (!gradient.isNull()) {
			return;
		}
		gradient = Images::GenerateShadow(
			size,
			top ? 255 : 0,
			top ? 0 : 255,
			background);
	};
	ensureGradient(_topGradient, true);
	ensureGradient(_bottomGradient, false);
	const auto paintGradient = [&](QImage &gradient, int scrolled, int top) {
		if (scrolled <= 0) {
			return;
		}
		const auto opacity = (expandRatio * scrolled)
			/ st::reactionGradientFadeSize;
		p.setOpacity(opacity);
		p.drawImage(
			QRect(0, top, _outer.width(), size),
			gradient,
			QRect(QPoint(), gradient.size()));
	};
	const auto up = button->expandUp();
	const auto start = st::reactionGradientStart;
	paintGradient(_topGradient, up ? endScroll : scroll, start);
	const auto bottomStart = button->geometry().height() - start - size;
	paintGradient(_bottomGradient, up ? scroll : endScroll, bottomStart);
	p.setOpacity(1.);
}

Manager::OverlayImage Manager::validateOverlayMask(
		int frameIndex,
		QSize innerSize,
		float64 radius,
		float64 scale) {
	const auto ratio = style::DevicePixelRatio();
	const auto cached = (scale == 1.);
	const auto full = cached
		? overlayCacheRect(frameIndex, kOverlayMaskCacheIndex)
		: QRect(QPoint(), _overlayFull * ratio);

	const auto maskSize = QSize(
		_overlayFull.width(),
		_overlayFull.height() + innerSize.height() - innerSize.width());

	const auto result = OverlayImage{
		.cache = cached ? &_overlayCacheParts : &_overlayMaskScaled,
		.source = QRect(full.topLeft(), maskSize * ratio),
	};
	if (cached && _validOverlayMask[frameIndex]) {
		return result;
	}

	auto p = QPainter(result.cache.get());
	const auto position = full.topLeft() / ratio;
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(QRect(position, maskSize), Qt::transparent);

	auto hq = PainterHighQualityEnabler(p);
	const auto inner = QRect(position + _inner.topLeft(), innerSize);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	if (scale != 1.) {
		const auto center = inner.center();
		p.save();
		p.translate(center);
		p.scale(scale, scale);
		p.translate(-center);
	}
	p.drawRoundedRect(inner, radius, radius);
	if (scale != 1.) {
		p.restore();
	}

	if (cached) {
		_validOverlayMask[frameIndex] = true;
	}
	return result;
}

Manager::OverlayImage Manager::validateOverlayShadow(
		int frameIndex,
		QSize innerSize,
		float64 radius,
		float64 scale,
		const QColor &shadow,
		const OverlayImage &mask) {
	const auto ratio = style::DevicePixelRatio();
	const auto cached = (scale == 1.);
	const auto full = cached
		? overlayCacheRect(frameIndex, kOverlayShadowCacheIndex)
		: QRect(QPoint(), _overlayFull * ratio);

	const auto maskSize = QSize(
		_overlayFull.width(),
		_overlayFull.height() + innerSize.height() - innerSize.width());

	const auto result = OverlayImage{
		.cache = cached ? &_overlayCacheParts : &_overlayShadowScaled,
		.source = QRect(full.topLeft(), maskSize * ratio),
	};
	if (cached && _validOverlayShadow[frameIndex]) {
		return result;
	}

	const auto position = full.topLeft() / ratio;

	_overlayShadowScaled.fill(Qt::transparent);
	const auto inner = QRect(_inner.topLeft(), innerSize);
	const auto add = style::ConvertScale(2.5);
	const auto shift = style::ConvertScale(0.5);
	const auto extended = QRectF(inner).marginsAdded({ add, add, add, add });
	{
		auto p = QPainter(&_overlayShadowScaled);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(shadow);
		if (scale != 1.) {
			const auto center = inner.center();
			p.translate(center);
			p.scale(scale, scale);
			p.translate(-center);
		}
		p.drawRoundedRect(extended.translated(0, shift), radius, radius);
		p.end();
	}

	_overlayShadowScaled = Images::Blur(std::move(_overlayShadowScaled));

	auto q = Painter(result.cache);
	if (result.cache != &_overlayShadowScaled) {
		q.setCompositionMode(QPainter::CompositionMode_Source);
		q.drawImage(
			QRect(position, maskSize),
			_overlayShadowScaled,
			QRect(QPoint(), maskSize * ratio));
	}
	q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	q.drawImage(QRect(position, maskSize), *mask.cache, mask.source);

	if (cached) {
		_validOverlayShadow[frameIndex] = true;
	}
	return result;
}

void Manager::overlayExpandedBorder(
		Painter &p,
		QSize size,
		float64 expandRatio,
		float64 scale,
		const QColor &shadow) {
	const auto radiusMin = _inner.height() / 2.;
	const auto radiusMax = _inner.width() / 2.;
	const auto progress = expandRatio;
	const auto frame = int(base::SafeRound(progress * (kFramesCount - 1)));
	const auto radius = radiusMin
		+ (frame / float64(kFramesCount - 1)) * (radiusMax - radiusMin);
	const auto innerSize = QSize(_inner.width(), int(std::ceil(radius * 2)));

	const auto overlayMask = validateOverlayMask(
		frame,
		innerSize,
		radius,
		scale);
	const auto overlayShadow = validateOverlayShadow(
		frame,
		innerSize,
		radius,
		scale,
		shadow,
		overlayMask);

	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	paintLongImage(
		p,
		QRect(QPoint(), size),
		*overlayMask.cache,
		overlayMask.source);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	paintLongImage(
		p,
		QRect(QPoint(), size),
		*overlayShadow.cache,
		overlayShadow.source);
}

bool Manager::onlyMainEmojiVisible() const {
	if (_icons.empty()) {
		return true;
	}
	const auto &icon = _icons.front();
	if (icon->selected
		|| icon->selectedScale.animating()
		|| (icon->select && icon->select->animating())) {
		return false;
	}
	icon->selectAnimated = false;
	return true;
}

void Manager::clearAppearAnimations() {
	if (!_showingAll) {
		return;
	}
	_showingAll = false;
	auto main = true;
	for (auto &icon : _icons) {
		if (!main) {
			if (icon->selected) {
				setSelectedIcon(-1);
			}
			icon->selectedScale.stop();
			if (const auto select = icon->select.get()) {
				select->jumpTo(0, nullptr);
			}
			icon->selectAnimated = false;
		}
		if (icon->appearAnimated != main) {
			if (const auto appear = icon->appear.get()) {
				appear->jumpTo(0, nullptr);
			}
			icon->appearAnimated = main;
		}
		main = false;
	}
}

void Manager::paintLongImage(
		QPainter &p,
		QRect geometry,
		const QImage &image,
		QRect source) {
	const auto factor = style::DevicePixelRatio();
	const auto sourceHeight = (source.height() / factor);
	const auto part = (sourceHeight / 2) - 1;
	const auto fill = geometry.height() - 2 * part;
	const auto half = part * factor;
	const auto top = source.height() - half;
	p.drawImage(
		geometry.topLeft(),
		image,
		QRect(source.x(), source.y(), source.width(), half));
	p.drawImage(
		QRect(
			geometry.topLeft() + QPoint(0, part),
			QSize(source.width() / factor, fill)),
		image,
		QRect(
			source.x(),
			source.y() + half,
			source.width(),
			top - half));
	p.drawImage(
		geometry.topLeft() + QPoint(0, part + fill),
		image,
		QRect(source.x(), source.y() + top, source.width(), half));
}

void Manager::paintAllEmoji(
		Painter &p,
		not_null<Button*> button,
		int scroll,
		float64 scale,
		QPoint position,
		QPoint mainEmojiPosition) {
	const auto current = (button == _button.get());

	const auto clip = QRect(
		position,
		button->geometry().size()).marginsRemoved(innerMargins());
	const auto skip = st::reactionAppearStartSkip;
	const auto animationRect = clip.marginsRemoved({ 0, skip, 0, skip });

	PainterHighQualityEnabler hq(p);
	const auto between = st::reactionCornerSkip;
	const auto oneHeight = st::reactionCornerSize.height() + between;
	const auto finalSize = CornerImageSize(1.);
	const auto hoveredSize = int(base::SafeRound(finalSize * kHoverScale));
	const auto basicTargetForScale = [&](int size, float64 scale) {
		const auto remove = size * (1. - scale) / 2.;
		return QRectF(QRect(
			_inner.x() + (_inner.width() - size) / 2,
			_inner.y() + (_inner.height() - size) / 2,
			size,
			size
		)).marginsRemoved({ remove, remove, remove, remove });
	};
	const auto basicTarget = basicTargetForScale(finalSize, scale);
	const auto countTarget = [&](const ReactionIcons &icon) {
		const auto selectScale = icon.selectedScale.value(
			icon.selected ? kHoverScale : 1.);
		if (selectScale == 1.) {
			return basicTarget;
		}
		const auto finalScale = scale * selectScale;
		return (finalScale <= 1.)
			? basicTargetForScale(finalSize, finalScale)
			: basicTargetForScale(hoveredSize, finalScale / kHoverScale);
	};
	const auto expandUp = button->expandUp();
	const auto shift = QPoint(0, oneHeight * (expandUp ? -1 : 1));
	auto emojiPosition = mainEmojiPosition
		+ QPoint(0, scroll * (expandUp ? 1 : -1));
	const auto update = crl::guard(this, [=] {
		updateCurrentButton();
	});
	for (const auto &icon : _icons) {
		const auto target = countTarget(*icon).translated(emojiPosition);
		emojiPosition += shift;

		const auto paintFrame = [&](not_null<Lottie::Icon*> animation) {
			const auto size = int(std::floor(target.width() + 0.01));
			const auto frame = animation->frame({ size, size }, update);
			p.drawImage(target, frame.image);
		};

		if (!target.intersects(clip)) {
			if (current) {
				clearStateForHidden(*icon);
			}
		} else if (icon->premiumLock) {
			paintPremiumIcon(p, emojiPosition - shift, target);
		} else {
			const auto appear = icon->appear.get();
			if (current
				&& appear
				&& !icon->appearAnimated
				&& target.intersects(animationRect)) {
				icon->appearAnimated = true;
				appear->animate(update, 0, appear->framesCount() - 1);
			}
			if (appear && appear->animating()) {
				paintFrame(appear);
			} else if (const auto select = icon->select.get()) {
				paintFrame(select);
			}
		}
		if (current) {
			clearStateForSelectFinished(*icon);
		}
	}
}

void Manager::paintPremiumIcon(
		QPainter &p,
		QPoint position,
		QRectF target) const {
	const auto finalSize = CornerImageSize(1.);
	const auto to = QRect(
		_inner.x() + (_inner.width() - finalSize) / 2,
		_inner.y() + (_inner.height() - finalSize) / 2,
		finalSize,
		finalSize).translated(position);
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

void Manager::clearStateForHidden(ReactionIcons &icon) {
	if (const auto appear = icon.appear.get()) {
		appear->jumpTo(0, nullptr);
	}
	if (icon.selected) {
		setSelectedIcon(-1);
	}
	icon.appearAnimated = false;
	icon.selectAnimated = false;
	if (const auto select = icon.select.get()) {
		select->jumpTo(0, nullptr);
	}
	icon.selectedScale.stop();
}

void Manager::clearStateForSelectFinished(ReactionIcons &icon) {
	if (icon.selectAnimated
		&& !icon.select->animating()
		&& !icon.selected) {
		icon.selectAnimated = false;
	}
}

void Manager::setShadowColor(const QColor &shadow) {
	if (_shadow == shadow) {
		return;
	}
	_shadow = shadow;
	ranges::fill(_validBg, false);
	ranges::fill(_validShadow, false);
	ranges::fill(_validOverlayShadow, false);
}

QRect Manager::cacheRect(int frameIndex, int columnIndex) const {
	const auto ratio = style::DevicePixelRatio();
	const auto origin = QPoint(
		_outer.width() * (kDivider * columnIndex + (frameIndex % kDivider)),
		_outer.height() * (frameIndex / kDivider));
	return QRect(ratio * origin, ratio * _outer);
}

QRect Manager::overlayCacheRect(int frameIndex, int columnIndex) const {
	const auto ratio = style::DevicePixelRatio();
	const auto size = _overlayFull;
	const auto origin = QPoint(
		size.width() * (kDivider * columnIndex + (frameIndex % kDivider)),
		size.height() * (frameIndex / kDivider));
	return QRect(ratio * origin, ratio * size);
}

QRect Manager::validateShadow(
		int frameIndex,
		float64 scale,
		const QColor &shadow) {
	const auto result = cacheRect(frameIndex, kShadowCacheIndex);
	if (_validShadow[frameIndex]) {
		return result;
	}

	_shadowBuffer.fill(Qt::transparent);
	auto p = QPainter(&_shadowBuffer);
	auto hq = PainterHighQualityEnabler(p);
	const auto center = _inner.center();
	const auto add = style::ConvertScale(2.5);
	const auto shift = style::ConvertScale(0.5);
	const auto big = QRectF(_inner).marginsAdded({ add, add, add, add });
	const auto radius = big.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(shadow);
	if (scale != 1.) {
		p.translate(center);
		p.scale(scale, scale);
		p.translate(-center);
	}
	p.drawRoundedRect(big.translated(0, shift), radius, radius);
	p.end();
	_shadowBuffer = Images::Blur(std::move(_shadowBuffer));

	auto q = QPainter(&_cacheParts);
	q.setCompositionMode(QPainter::CompositionMode_Source);
	q.drawImage(result.topLeft() / style::DevicePixelRatio(), _shadowBuffer);

	_validShadow[frameIndex] = true;
	return result;
}

QRect Manager::validateEmoji(int frameIndex, float64 scale) {
	const auto result = cacheRect(frameIndex, kEmojiCacheIndex);
	if (_validEmoji[frameIndex]) {
		return result;
	}

	auto p = QPainter(&_cacheParts);
	const auto ratio = style::DevicePixelRatio();
	const auto position = result.topLeft() / ratio;
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(QRect(position, result.size() / ratio), Qt::transparent);
	if (_mainReactionImage.isNull()
		&& _mainReactionIcon) {
		_mainReactionImage = base::take(_mainReactionIcon)->frame();
	}
	if (!_mainReactionImage.isNull()) {
		const auto size = CornerImageSize(scale);
		const auto inner = _inner.translated(position);
		const auto target = QRect(
			inner.x() + (inner.width() - size) / 2,
			inner.y() + (inner.height() - size) / 2,
			size,
			size);

		p.drawImage(target, _mainReactionImage.scaled(
			target.size() * ratio,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation));
	}

	_validEmoji[frameIndex] = true;
	return result;
}

void Manager::setBackgroundColor(const QColor &background) {
	if (_background == background) {
		return;
	}
	_background = background;
	_topGradient = QImage();
	_bottomGradient = QImage();
	ranges::fill(_validBg, false);
}

QRect Manager::validateFrame(
		int frameIndex,
		float64 scale,
		const QColor &background,
		const QColor &shadow) {
	const auto result = cacheRect(frameIndex, kBgCacheIndex);
	if (_validBg[frameIndex]) {
		return result;
	}

	const auto shadowSource = validateShadow(frameIndex, scale, shadow);
	const auto position = result.topLeft() / style::DevicePixelRatio();
	auto p = QPainter(&_cacheBg);
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.drawImage(position, _cacheParts, shadowSource);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	auto hq = PainterHighQualityEnabler(p);
	const auto inner = _inner.translated(position);
	const auto radius = inner.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(background);
	if (scale != 1.) {
		const auto center = inner.center();
		p.save();
		p.translate(center);
		p.scale(scale, scale);
		p.translate(-center);
	}
	p.drawRoundedRect(inner, radius, radius);
	if (scale != 1.) {
		p.restore();
	}

	_validBg[frameIndex] = true;
	return result;
}

std::optional<QRect> Manager::lookupEffectArea(FullMsgId itemId) const {
	const auto i = _activeEffectAreas.find(itemId);
	return (i != end(_activeEffectAreas))
		? i->second
		: std::optional<QRect>();
}

void Manager::startEffectsCollection() {
	_collectedEffects.clear();
	_currentReactionInfo = {};
}

auto Manager::currentReactionPaintInfo()
-> not_null<Ui::ReactionPaintInfo*> {
	return &_currentReactionInfo;
}

void Manager::recordCurrentReactionEffect(FullMsgId itemId, QPoint origin) {
	if (_currentReactionInfo.effectPaint) {
		_currentReactionInfo.effectOffset += origin
			+ _currentReactionInfo.position;
		_collectedEffects[itemId] = base::take(_currentReactionInfo);
	} else if (!_collectedEffects.empty()) {
		_collectedEffects.remove(itemId);
	}
}

bool Manager::showContextMenu(
		QWidget *parent,
		QContextMenuEvent *e,
		const QString &favorite) {
	if (_icons.empty() || _selectedIcon < 0) {
		return false;
	}
	const auto lookupSelectedEmoji = [&] {
		const auto i = ranges::find(_icons, true, &ReactionIcons::selected);
		return (i != end(_icons)) ? (*i)->emoji : QString();
	};
	if (!favorite.isEmpty() && lookupSelectedEmoji() == favorite) {
		return true;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto callback = [=] {
		if (const auto emoji = lookupSelectedEmoji(); !emoji.isEmpty()) {
			_faveRequests.fire_copy(emoji);
		}
	};
	_menu->addAction(
		tr::lng_context_set_as_quick(tr::now),
		callback,
		&st::menuIconFave);
	_menu->popup(e->globalPos());
	return true;
}

rpl::producer<QString> Manager::faveRequests() const {
	return _faveRequests.events();
}

void SetupManagerList(
		not_null<Manager*> manager,
		not_null<Main::Session*> session,
		rpl::producer<Manager::AllowedSublist> filter) {
	const auto reactions = &session->data().reactions();
	rpl::single(rpl::empty) | rpl::then(
		reactions->updates()
	) | rpl::start_with_next([=] {
		manager->applyList(
			reactions->list(Data::Reactions::Type::Active),
			reactions->favorite(),
			session->premiumPossible());
	}, manager->lifetime());

	std::move(
		filter
	) | rpl::start_with_next([=](Manager::AllowedSublist &&list) {
		manager->updateAllowedSublist(std::move(list));
	}, manager->lifetime());

	manager->faveRequests(
	) | rpl::start_with_next([=](const QString &emoji) {
		reactions->setFavorite(emoji);
		manager->updateButton({});
	}, manager->lifetime());

	Data::AmPremiumValue(
		session
	) | rpl::start_with_next([=](bool premium) {
		manager->updateAllowSendingPremium(premium);
	}, manager->lifetime());
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

std::shared_ptr<Lottie::Icon> DefaultIconFactory(
		not_null<Data::DocumentMedia*> media,
		int size) {
	return CreateIcon(media, size, 0);
}

} // namespace HistoryView
