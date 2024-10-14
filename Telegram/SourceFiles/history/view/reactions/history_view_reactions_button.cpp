/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/reactions/history_view_reactions_button.h"

#include "history/view/history_view_cursor_state.h"
#include "history/history_item.h"
#include "history/history.h"
#include "ui/chat/chat_style.h"
#include "ui/widgets/popup_menu.h"
#include "ui/ui_utility.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h"
#include "data/data_peer.h"
#include "data/data_message_reactions.h"
#include "lang/lang_keys.h"
#include "core/click_handler_types.h"
#include "main/main_session.h"
#include "base/event_filter.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kToggleDuration = crl::time(120);
constexpr auto kActivateDuration = crl::time(150);
constexpr auto kExpandDuration = crl::time(300);
constexpr auto kCollapseDuration = crl::time(250);
constexpr auto kButtonShowDelay = crl::time(300);
constexpr auto kButtonExpandDelay = crl::time(25);
constexpr auto kButtonHideDelay = crl::time(300);
constexpr auto kButtonExpandedHideDelay = crl::time(0);
constexpr auto kMaxReactionsScrollAtOnce = 2;
constexpr auto kRefreshListDelay = crl::time(100);

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

} // namespace

Button::Button(
	Fn<void(QRect)> update,
	ButtonParameters parameters,
	Fn<void()> hide)
: _update(std::move(update))
, _finalScale(ScaleForState(_state))
, _collapsed(QPoint(), CountOuterSize())
, _finalHeight(_collapsed.height())
, _expandTimer([=] { applyState(State::Inside, _update); })
, _hideTimer(hide) {
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
	Fn<void(QRect)> buttonUpdate,
	IconFactory iconFactory)
: _outer(CountOuterSize())
, _inner(QRect({}, st::reactionCornerSize))
, _strip(
	st::reactPanelEmojiPan,
	_inner,
	st::reactionCornerImage,
	crl::guard(this, [=] { updateCurrentButton(); }),
	std::move(iconFactory))
, _cachedRound(
	st::reactionCornerSize,
	st::reactionCornerShadow,
	_inner.width())
, _buttonShowTimer([=] { showButtonDelayed(); })
, _buttonUpdate(std::move(buttonUpdate)) {
	_inner.translate(QRect({}, _outer).center() - _inner.center());

	_expandedBuffer = _cachedRound.PrepareImage(QSize(
		_outer.width(),
		_outer.height() + st::reactionCornerAddedHeightMax));
	if (wheelEventsTarget) {
		stealWheelEvents(wheelEventsTarget);
	}

	_createChooseCallback = [=](ReactionId id) {
		return [=] {
			if (auto chosen = lookupChosen(id)) {
				updateButton({});
				_chosen.fire(std::move(chosen));
			}
		};
	};
}

Manager::~Manager() = default;

ChosenReaction Manager::lookupChosen(const ReactionId &id) const {
	auto result = ChosenReaction{
		.context = _buttonContext,
		.id = id,
	};
	const auto button = _button.get();
	if (!button) {
		return result;
	}
	const auto index = _strip.fillChosenIconGetIndex(result);
	if (result.icon.isNull()) {
		return result;
	}
	const auto between = st::reactionCornerSkip;
	const auto oneHeight = (st::reactionCornerSize.height() + between);
	const auto expanded = (_strip.count() > 1);
	const auto skip = (expanded ? st::reactionExpandedSkip : 0);
	const auto scroll = button->scroll();
	const auto local = skip + index * oneHeight - scroll;
	const auto geometry = button->geometry();
	const auto top = button->expandUp()
		? (geometry.height() - local - _outer.height())
		: local;
	const auto rect = QRect(geometry.topLeft() + QPoint(0, top), _outer);
	const auto imageSize = _strip.computeOverSize();
	result.localGeometry = QRect(
		rect.x() + (rect.width() - imageSize) / 2,
		rect.y() + (rect.height() - imageSize) / 2,
		imageSize,
		imageSize);
	return result;
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

void Manager::updateButton(ButtonParameters parameters) {
	if (parameters.cursorLeft && _menu) {
		return;
	}
	const auto contextChanged = (_buttonContext != parameters.context);
	if (contextChanged) {
		_strip.setSelected(-1);
		if (_button) {
			_button->applyState(ButtonState::Hidden);
			_buttonHiding.push_back(std::move(_button));
		}
		_buttonShowTimer.cancel();
		_scheduledParameters = std::nullopt;
	}
	_buttonContext = parameters.context;
	parameters.reactionsCount = _strip.count();
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

void Manager::applyList(const Data::PossibleItemReactionsRef &reactions) {
	using Button = Strip::AddedButton;
	_strip.applyList(
		reactions.recent,
		(/*reactions.customAllowed
			? Button::Expand
			: */Button::None));
	_tagsStrip = reactions.tags;
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

void Manager::updateCurrentButton() const {
	if (const auto button = _button.get()) {
		_buttonUpdate(button->geometry());
	}
}

void Manager::removeStaleButtons() {
	_buttonHiding.erase(
		ranges::remove_if(_buttonHiding, &Button::isHidden),
		end(_buttonHiding));
}

void Manager::paint(QPainter &p, const PaintContext &context) {
	removeStaleButtons();
	for (const auto &button : _buttonHiding) {
		paintButton(p, context, button.get());
	}
	if (const auto current = _button.get()) {
		if (context.gestureHorizontal.ratio) {
			current->applyState(ButtonState::Hidden);
			_buttonHiding.push_back(std::move(_button));
		}
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
	if (_strip.empty()) {
		_strip.setSelected(-1);
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
		int(_strip.count() - 1));
	_strip.setSelected(index);
	const auto selected = _strip.selected();
	if (selected == Strip::AddedButton::Expand) {
		if (!_expandLink) {
			_expandLink = std::make_shared<LambdaClickHandler>([=] {
				_expandChosen.fire_copy(_buttonContext);
			});
		}
		return _expandLink;
	}
	const auto id = std::get_if<ReactionId>(&selected);
	if (!id || id->empty()) {
		return nullptr;
	}
	auto &result = _links[*id];
	if (!result) {
		result = resolveButtonLink(*id);
	}
	return result;
}

ClickHandlerPtr Manager::resolveButtonLink(const ReactionId &id) const {
	const auto i = _reactionsLinks.find(id);
	if (i != end(_reactionsLinks)) {
		return i->second;
	}
	auto handler = std::make_shared<LambdaClickHandler>(
		crl::guard(this, _createChooseCallback(id)));
	handler->setProperty(
		kSendReactionEmojiProperty,
		QVariant::fromValue(id));
	return _reactionsLinks.emplace(id, std::move(handler)).first->second;
}

TextState Manager::buttonTextState(QPoint position) const {
	if (overCurrentButton(position)) {
		auto result = TextState(nullptr, computeButtonLink(position));
		result.itemId = _buttonContext;
		return result;
	} else {
		_strip.setSelected(-1);
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
		QPainter &p,
		const PaintContext &context,
		not_null<Button*> button) {
	const auto geometry = button->geometry();
	if (!context.clip.intersects(geometry)) {
		return;
	}
	constexpr auto kFramesCount = Ui::RoundAreaWithShadow::kFramesCount;
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
		QPainter &p,
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
	auto layeredPainter = std::optional<QPainter>();
	if (expanded) {
		_expandedBuffer.fill(Qt::transparent);
	}
	const auto q = expanded ? &layeredPainter.emplace(&_expandedBuffer) : &p;
	const auto shadow = context.st->shadowFg()->c;
	const auto background = context.st->windowBg()->c;
	_cachedRound.setShadowColor(shadow);
	_cachedRound.setBackgroundColor(background);
	if (expanded) {
		q->fillRect(QRect(QPoint(), size), context.st->windowBg());
	} else {
		const auto radius = _inner.height() / 2.;
		const auto frame = _cachedRound.validateFrame(
			frameIndex,
			scale,
			radius);
		p.drawImage(position, *frame.image, frame.rect);
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
	const auto mainEmojiPosition = _inner.topLeft() + (!expanded
		? position
		: button->expandUp()
		? QPoint(0, expanded - expandedSkip)
		: QPoint(0, expandedSkip));
	const auto mainEmoji = _strip.validateEmoji(frameIndex, scale);
	if (expanded
		|| (current && !_strip.onlyMainEmojiVisible())
		|| _strip.onlyAddedButton()) {
		const auto opacity = button->expandAnimationOpacity(expandRatio);
		if (opacity != 1.) {
			q->setOpacity(opacity);
		}
		const auto clip = QRect(
			expanded ? QPoint() : position,
			button->geometry().size()
		).marginsRemoved(innerMargins());
		const auto between = st::reactionCornerSkip;
		const auto oneHeight = st::reactionCornerSize.height() + between;
		const auto expandUp = button->expandUp();
		const auto shift = QPoint(0, oneHeight * (expandUp ? -1 : 1));
		const auto scroll = button->expandAnimationScroll(expandRatio);
		const auto startEmojiPosition = mainEmojiPosition
			+ QPoint(0, scroll * (expandUp ? 1 : -1));
		_strip.paint(*q, startEmojiPosition, shift, clip, scale, !current);
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
			q->drawImage(
				appearPosition + _inner.topLeft(),
				*mainEmoji.image,
				mainEmoji.rect);
			q->setOpacity(1.);
		}
	} else {
		p.drawImage(mainEmojiPosition, *mainEmoji.image, mainEmoji.rect);
	}
	if (current && !expanded) {
		clearAppearAnimations();
	}

	if (expanded) {
		const auto radiusMin = _inner.height() / 2.;
		const auto radiusMax = _inner.width() / 2.;
		_cachedRound.overlayExpandedBorder(
			*q,
			size,
			expandRatio,
			radiusMin,
			radiusMax,
			scale);
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
		QPainter &p,
		const QColor &background,
		not_null<Button*> button,
		int scroll,
		float64 expandRatio) {
	if (_gradientBackground != background) {
		_gradientBackground = background;
		_topGradient = _bottomGradient = QImage();
	}
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

void Manager::clearAppearAnimations() {
	if (!_showingAll) {
		return;
	}
	_showingAll = false;
	_strip.clearAppearAnimations();
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
		const ReactionId &favorite) {
	const auto selected = _strip.selected();
	const auto id = std::get_if<ReactionId>(&selected);
	if (!id || id->empty() || _tagsStrip) {
		return false;
	} else if (*id == favorite || id->paid()) {
		return true;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	_menu->addAction(
		tr::lng_context_set_as_quick(tr::now),
		[=, id = *id] { _faveRequests.fire_copy(id); },
		&st::menuIconFave);
	_menu->popup(e->globalPos());
	return true;
}

auto Manager::faveRequests() const -> rpl::producer<ReactionId> {
	return _faveRequests.events();
}

void SetupManagerList(
		not_null<Manager*> manager,
		rpl::producer<HistoryItem*> items) {
	struct State {
		PeerData *peer = nullptr;
		HistoryItem *item = nullptr;
		Main::Session *session = nullptr;
		rpl::lifetime sessionLifetime;
		rpl::lifetime peerLifetime;
		base::Timer timer;
	};
	const auto state = manager->lifetime().make_state<State>();

	std::move(
		items
	) | rpl::filter([=](HistoryItem *item) {
		return (item != state->item);
	}) | rpl::start_with_next([=](HistoryItem *item) {
		state->item = item;
		if (!item) {
			return;
		}
		const auto peer = item->history()->peer;
		const auto session = &peer->session();
		const auto peerChanged = (state->peer != peer);
		const auto sessionChanged = (state->session != session);
		const auto push = [=] {
			state->timer.cancel();
			if (const auto item = state->item) {
				manager->applyList(Data::LookupPossibleReactions(item));
			}
		};
		state->timer.setCallback(push);
		if (sessionChanged) {
			state->sessionLifetime.destroy();
			state->session = session;
			Data::AmPremiumValue(
				session
			) | rpl::skip(
				1
			) | rpl::start_with_next(push, state->sessionLifetime);

			session->changes().messageUpdates(
				Data::MessageUpdate::Flag::Destroyed
			) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
				if (update.item == state->item) {
					state->item = nullptr;
					state->timer.cancel();
				}
			}, state->sessionLifetime);

			session->data().itemDataChanges(
			) | rpl::filter([=](not_null<HistoryItem*> item) {
				return (item == state->item);
			}) | rpl::start_with_next(push, state->sessionLifetime);

			const auto &reactions = session->data().reactions();
			rpl::merge(
				reactions.topUpdates(),
				reactions.recentUpdates(),
				reactions.defaultUpdates(),
				reactions.favoriteUpdates(),
				reactions.myTagsUpdates(),
				reactions.tagsUpdates()
			) | rpl::start_with_next([=] {
				if (!state->timer.isActive()) {
					state->timer.callOnce(kRefreshListDelay);
				}
			}, state->sessionLifetime);
		}
		if (peerChanged) {
			state->peer = peer;
			state->peerLifetime = rpl::combine(
				Data::PeerAllowedReactionsValue(peer),
				Data::UniqueReactionsLimitValue(peer)
			) | rpl::start_with_next(push);
		} else {
			push();
		}
	}, manager->lifetime());

	manager->faveRequests(
	) | rpl::filter([=] {
		return (state->session != nullptr);
	}) | rpl::start_with_next([=](const Data::ReactionId &id) {
		state->session->data().reactions().setFavorite(id);
		manager->updateButton({});
	}, manager->lifetime());
}

} // namespace HistoryView
