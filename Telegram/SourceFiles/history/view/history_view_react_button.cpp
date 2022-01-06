/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_react_button.h"

#include "history/view/history_view_cursor_state.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "data/data_message_reactions.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "base/event_filter.h"
#include "styles/style_chat.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kToggleDuration = crl::time(120);
constexpr auto kActivateDuration = crl::time(150);
constexpr auto kExpandDuration = crl::time(300);
constexpr auto kCollapseDuration = crl::time(250);
constexpr auto kHoverScaleDuration = crl::time(120);
constexpr auto kBgCacheIndex = 0;
constexpr auto kShadowCacheIndex = 0;
constexpr auto kEmojiCacheIndex = 1;
constexpr auto kMaskCacheIndex = 2;
constexpr auto kCacheColumsCount = 3;
constexpr auto kButtonShowDelay = crl::time(300);
constexpr auto kButtonExpandDelay = crl::time(25);
constexpr auto kButtonHideDelay = crl::time(300);
constexpr auto kButtonExpandedHideDelay = crl::time(0);
constexpr auto kSizeForDownscale = 96;
constexpr auto kHoverScale = 1.24;

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
		int startFrame,
		int size) {
	Expects(media->loaded());

	return std::make_shared<Lottie::Icon>(Lottie::IconDescriptor{
		.path = media->owner()->filepath(true),
		.json = media->bytes(),
		.sizeOverride = QSize(size, size),
		.frame = startFrame,
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
	const auto shift = delta.y() * (expandUp() ? 1 : -1);
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
		* (st::reactionCornerSize.height() + st::reactionCornerSkip);
	_expandedInnerHeight = _collapsed.height() + maxAddedHeight;
	const auto addedHeight = std::min(
		maxAddedHeight,
		st::reactionCornerAddedHeightMax);
	_expandedHeight = _collapsed.height() + addedHeight;
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
	case State::Hidden: return 0.5;
	case State::Shown: return 0.7;
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
	Fn<void(QRect)> buttonUpdate)
: _outer(CountOuterSize())
, _inner(QRect({}, st::reactionCornerSize))
, _buttonShowTimer([=] { showButtonDelayed(); })
, _buttonUpdate(std::move(buttonUpdate)) {
	_inner.translate(QRect({}, _outer).center() - _inner.center());

	const auto ratio = style::DevicePixelRatio();
	_cacheBg = QImage(
		_outer.width() * ratio,
		_outer.height() * kFramesCount * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_cacheBg.setDevicePixelRatio(ratio);
	_cacheBg.fill(Qt::transparent);
	_cacheParts = QImage(
		_outer.width() * kCacheColumsCount * ratio,
		_outer.height() * kFramesCount * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_cacheParts.setDevicePixelRatio(ratio);
	_cacheParts.fill(Qt::transparent);
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

	_createChooseCallback = [=](QString emoji) {
		return [=] {
			if (const auto context = _buttonContext) {
				updateButton({});
				_chosen.fire({
					.context = context,
					.emoji = emoji,
				});
			}
		};
	};
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
	parameters.reactionsCount = _list.size();
	if (!_buttonContext || _list.empty()) {
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

void Manager::applyList(std::vector<Data::Reaction> list) {
	constexpr auto predicate = [](
			const Data::Reaction &a,
			const Data::Reaction &b) {
		return (a.emoji == b.emoji)
			&& (a.appearAnimation == b.appearAnimation)
			&& (a.selectAnimation == b.selectAnimation);
	};
	if (ranges::equal(_list, list, predicate)) {
		return;
	}
	_list = std::move(list);
	_links = std::vector<ClickHandlerPtr>(_list.size());
	if (_list.empty()) {
		_mainReactionMedia = nullptr;
		_mainReactionLifetime.destroy();
		setSelectedIcon(-1);
		_icons.clear();
		return;
	}
	const auto main = _list.front().selectAnimation;
	if (_mainReactionMedia
		&& _mainReactionMedia->owner() == main) {
		if (!_mainReactionLifetime) {
			loadIcons();
		}
		return;
	}
	_mainReactionLifetime.destroy();
	_mainReactionMedia = main->createMediaView();
	_mainReactionMedia->checkStickerLarge();
	if (_mainReactionMedia->loaded()) {
		setMainReactionIcon();
	} else {
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
		if (icon->frameIndex() == icon->framesCount() - 1
			&& icon->width() == MainReactionSize()) {
			_mainReactionImage = i->second.icon->frame();
			return;
		}
	}
	_mainReactionImage = CreateIcon(
		_mainReactionMedia.get(),
		-1,
		MainReactionSize())->frame();
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
	entry.icon = CreateIcon(entry.media.get(), entry.startFrame, size);
	entry.media = nullptr;
	return true;
}

void Manager::updateCurrentButton() const {
	if (const auto button = _button.get()) {
		_buttonUpdate(button->geometry());
	}
}

void Manager::loadIcons() {
	const auto load = [&](not_null<DocumentData*> document, int frame) {
		if (const auto i = _loadCache.find(document); i != end(_loadCache)) {
			return i->second.icon;
		}
		auto &entry = _loadCache.emplace(document).first->second;
		entry.media = document->createMediaView();
		entry.media->checkStickerLarge();
		entry.startFrame = frame;
		if (!checkIconLoaded(entry) && !_loadCacheLifetime) {
			document->session().downloaderTaskFinished(
			) | rpl::start_with_next([=] {
				checkIcons();
			}, _loadCacheLifetime);
		}
		return entry.icon;
	};
	// #TODO reactions rebuild list better
	_icons.clear();
	auto main = true;
	for (const auto &reaction : _list) {
		_icons.push_back({
			.appear = load(reaction.appearAnimation, main ? -1 : 0),
			.select = load(reaction.selectAnimation, 0),
			.appearAnimated = main,
		});
		main = false;
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

void Manager::paintButtons(Painter &p, const PaintContext &context) {
	removeStaleButtons();
	for (const auto &button : _buttonHiding) {
		paintButton(p, context, button.get());
	}
	if (const auto current = _button.get()) {
		paintButton(p, context, current);
	}
}

ClickHandlerPtr Manager::computeButtonLink(QPoint position) const {
	if (_list.empty()) {
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
		int(_list.size() - 1));
	auto &result = _links[index];
	if (!result) {
		result = resolveButtonLink(_list[index]);
	}
	setSelectedIcon(index);
	return result;
}

void Manager::setSelectedIcon(int index) const {
	const auto setSelected = [&](int index, bool selected) {
		if (index < 0 || index >= _icons.size()) {
			return;
		}
		auto &icon = _icons[index];
		if (icon.selected == selected) {
			return;
		}
		icon.selected = selected;
		icon.selectedScale.start(
			[=] { updateCurrentButton(); },
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
				select->animate(
					[=] { updateCurrentButton(); },
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
		const Data::Reaction &reaction) const {
	const auto emoji = reaction.emoji;
	const auto i = _reactionsLinks.find(emoji);
	if (i != end(_reactionsLinks)) {
		return i->second;
	}
	return _reactionsLinks.emplace(
		emoji,
		std::make_shared<LambdaClickHandler>(
			crl::guard(this, _createChooseCallback(emoji)))
	).first->second;
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
	const auto mainEmojiPosition = !expanded
		? position
		: button->expandUp()
		? QPoint(0, expanded)
		: QPoint();
	if (expanded || (current && !onlyMainEmojiVisible())) {
		const auto origin = expanded ? QPoint() : position;
		paintAllEmoji(*q, button, scale, origin, mainEmojiPosition);
		if (current && expanded) {
			_showingAll = true;
		}
	} else {
		const auto source = validateEmoji(frameIndex, scale);
		p.drawImage(mainEmojiPosition, _cacheParts, source);
	}
	if (current && !expanded) {
		clearAppearAnimations();
	}

	if (expanded) {
		const auto expandRatio = float64(expanded)
			/ (button->expandedHeight() - _outer.height());
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

void Manager::overlayExpandedBorder(
		Painter &p,
		QSize size,
		float64 expandRatio,
		float64 scale,
		const QColor &shadow) {
	const auto maxSide = _inner.width();
	const auto radius = expandRatio * (maxSide / 2.)
		+ (1. - expandRatio) * st::reactionCornerRadius;
	const auto minHeight = int(std::ceil(radius * 2)) + 1;

	const auto maskSize = QRect(0, 0, maxSide, minHeight).marginsAdded(
		st::reactionCornerShadow
	).size();
	const auto ratio = style::DevicePixelRatio();
	auto mask = QImage(
		maskSize * ratio,
		QImage::Format_ARGB32_Premultiplied);
	mask.setDevicePixelRatio(ratio);
	mask.fill(Qt::transparent);
	{
		auto q = Painter(&mask);
		auto hq = PainterHighQualityEnabler(q);
		const auto inner = QRect(_inner.x(), _inner.y(), maxSide, minHeight);
		const auto center = inner.center();
		q.setPen(Qt::NoPen);
		q.setBrush(Qt::white);
		q.save();
		q.translate(center);
		q.scale(scale, scale);
		q.translate(-center);
		q.drawRoundedRect(inner, radius, radius);
		q.restore();
	}

	auto shadowMask = QImage(
		maskSize * ratio,
		QImage::Format_ARGB32_Premultiplied);
	shadowMask.setDevicePixelRatio(ratio);
	shadowMask.fill(Qt::transparent);
	{
		auto q = Painter(&shadowMask);
		auto hq = PainterHighQualityEnabler(q);
		const auto inner = QRect(_inner.x(), _inner.y(), maxSide, minHeight);
		const auto center = inner.center();
		const auto add = style::ConvertScale(2.5);
		const auto shift = style::ConvertScale(0.5);
		const auto extended = QRectF(inner).marginsAdded({ add, add, add, add });
		q.setPen(Qt::NoPen);
		q.setBrush(shadow);
		q.translate(center);
		q.scale(scale, scale);
		q.translate(-center);
		q.drawRoundedRect(extended.translated(0, shift), radius, radius);
	}
	shadowMask = Images::prepareBlur(std::move(shadowMask));
	{
		auto q = Painter(&shadowMask);
		q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
		q.drawImage(0, 0, mask);
	}

	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	paintLongImage(
		p,
		QRect(QPoint(), size),
		mask,
		QRect(QPoint(), mask.size()));
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	paintLongImage(
		p,
		QRect(QPoint(), size),
		shadowMask,
		QRect(QPoint(), shadowMask.size()));
}

bool Manager::onlyMainEmojiVisible() const {
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

void Manager::clearAppearAnimations() {
	if (!_showingAll) {
		return;
	}
	_showingAll = false;
	auto main = true;
	for (auto &icon : _icons) {
		if (!main) {
			if (icon.selected) {
				setSelectedIcon(-1);
			}
			icon.selectedScale.stop();
			if (const auto select = icon.select.get()) {
				select->jumpTo(0, nullptr);
			}
			icon.selectAnimated = false;
		}
		if (icon.appearAnimated != main) {
			if (const auto appear = icon.appear.get()) {
				appear->jumpTo(
					main ? (appear->framesCount() - 1) : 0,
					nullptr);
			}
			icon.appearAnimated = main;
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
		float64 scale,
		QPoint position,
		QPoint mainEmojiPosition) {
	const auto current = (button == _button.get());

	const auto clip = QRect(
		position,
		button->geometry().size()).marginsRemoved(innerMargins());
	const auto skip = st::reactionAppearStartSkip;
	const auto animationRect = clip.marginsRemoved({ 0, skip, 0, skip });

	auto hq = std::optional<PainterHighQualityEnabler>();
	if (scale != 1. && scale != kHoverScale) {
		hq.emplace(p);
	}
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
		+ QPoint(0, button->scroll() * (expandUp ? 1 : -1));
	const auto update = [=] {
		updateCurrentButton();
	};
	for (auto &icon : _icons) {
		const auto target = countTarget(icon).translated(emojiPosition);
		emojiPosition += shift;

		if (!target.intersects(clip)) {
			if (current) {
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
		} else if (icon.select && icon.select->animating()) {
			const auto size = int(base::SafeRound(target.width()));
			const auto frame = icon.select->frame({ size, size }, update);
			p.drawImage(target, frame.image);
		} else if (const auto appear = icon.appear.get()) {
			if (current
				&& !icon.appearAnimated
				&& target.intersects(animationRect)) {
				icon.appearAnimated = true;
				appear->animate(update, 0, appear->framesCount() - 1);
			}
			const auto size = int(base::SafeRound(target.width()));
			const auto frame = appear->frame({ size, size }, update);
			p.drawImage(target, frame.image);
		}
		if (current
			&& icon.selectAnimated
			&& !icon.select->animating()
			&& !icon.selected) {
			icon.selectAnimated = false;
		}
	}
}

void Manager::applyPatternedShadow(const QColor &shadow) {
	if (_shadow == shadow) {
		return;
	}
	_shadow = shadow;
	ranges::fill(_validBg, false);
	ranges::fill(_validShadow, false);
}

QRect Manager::cacheRect(int frameIndex, int columnIndex) const {
	const auto ratio = style::DevicePixelRatio();
	const auto origin = QPoint(
		_outer.width() * columnIndex,
		_outer.height() * frameIndex);
	return QRect(ratio * origin, ratio * _outer);
}

QRect Manager::validateShadow(
		int frameIndex,
		float64 scale,
		const QColor &shadow) {
	applyPatternedShadow(shadow);
	const auto result = cacheRect(frameIndex, kShadowCacheIndex);
	if (_validShadow[frameIndex]) {
		return result;
	}

	_shadowBuffer.fill(Qt::transparent);
	auto p = QPainter(&_shadowBuffer);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = st::reactionCornerRadius;
	const auto center = _inner.center();
	const auto add = style::ConvertScale(2.5);
	const auto shift = style::ConvertScale(0.5);
	const auto extended = QRectF(_inner).marginsAdded({add, add, add, add});
	p.setPen(Qt::NoPen);
	p.setBrush(shadow);
	p.translate(center);
	p.scale(scale, scale);
	p.translate(-center);
	p.drawRoundedRect(extended.translated(0, shift), radius, radius);
	p.end();
	_shadowBuffer = Images::prepareBlur(std::move(_shadowBuffer));

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

QRect Manager::validateFrame(
		int frameIndex,
		float64 scale,
		const QColor &background,
		const QColor &shadow) {
	applyPatternedShadow(shadow);
	if (_background != background) {
		_background = background;
		ranges::fill(_validBg, false);
	}

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
	const auto radius = st::reactionCornerRadius;
	const auto center = inner.center();
	p.setPen(Qt::NoPen);
	p.setBrush(background);
	p.save();
	p.translate(center);
	p.scale(scale, scale);
	p.translate(-center);
	p.drawRoundedRect(inner, radius, radius);
	p.restore();
	p.end();
	_validBg[frameIndex] = true;
	return result;
}

} // namespace HistoryView
