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
#include "main/main_session.h"
#include "base/event_filter.h"
#include "styles/style_chat.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kToggleDuration = crl::time(80);
constexpr auto kActivateDuration = crl::time(150);
constexpr auto kExpandDuration = crl::time(150);
constexpr auto kInCacheIndex = 0;
constexpr auto kOutCacheIndex = 1;
constexpr auto kServiceCacheIndex = 2;
constexpr auto kShadowCacheIndex = 0;
constexpr auto kEmojiCacheIndex = 1;
constexpr auto kMaskCacheIndex = 2;
constexpr auto kCacheColumsCount = 3;
constexpr auto kButtonShowDelay = crl::time(300);
constexpr auto kButtonExpandDelay = crl::time(300);

[[nodiscard]] QPoint LocalPosition(not_null<QWheelEvent*> e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return e->position().toPoint();
#else // Qt >= 6.0
	return e->pos();
#endif // Qt >= 6.0
}

[[nodiscard]] QSize CountMaxSizeWithMargins(style::margins margins) {
	const auto extended = QRect(
		QPoint(),
		st::reactionCornerSize
	).marginsAdded(margins);
	const auto scale = Button::ScaleForState(ButtonState::Active);
	return QSize(
		int(base::SafeRound(extended.width() * scale)),
		int(base::SafeRound(extended.height() * scale)));
}

[[nodiscard]] QSize CountOuterSize() {
	return CountMaxSizeWithMargins(st::reactionCornerShadow);
}

} // namespace

Button::Button(
	Fn<void(QRect)> update,
	ButtonParameters parameters)
: _update(std::move(update))
, _collapsed(QPoint(), CountOuterSize())
, _finalHeight(_collapsed.height())
, _expandTimer([=] { applyState(State::Inside, _update); }) {
	applyParameters(parameters, nullptr);
}

Button::~Button() = default;

ButtonStyle Button::style() const {
	return _style;
}

bool Button::isHidden() const {
	return (_state == State::Hidden) && !_scaleAnimation.animating();
}

QRect Button::geometry() const {
	return _geometry;
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
	const auto state = (inside && !delayInside)
		? State::Inside
		: active
		? State::Active
		: State::Shown;
	applyState(state, update);
	if (_style != parameters.style) {
		_style = parameters.style;
		if (update) {
			update(_geometry);
		}
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
	}
	const auto finalHeight = (state == State::Inside)
		? _expandedHeight
		: _collapsed.height();
	if (_finalHeight != finalHeight) {
		_heightAnimation.start(
			[=] { updateGeometry(_update); },
			_finalHeight,
			finalHeight,
			kExpandDuration);
		_finalHeight = finalHeight;
	}
	updateGeometry(update);
	if (_state == state) {
		return;
	}
	const auto duration = (state == State::Hidden
		|| _state == State::Hidden)
		? kToggleDuration
		: kActivateDuration;
	_scaleAnimation.start(
		[=] { _update(_geometry); },
		ScaleForState(_state),
		ScaleForState(state),
		duration);
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
	return _scaleAnimation.value(ScaleForState(_state));
}

Manager::Manager(
	QWidget *wheelEventsTarget,
	Fn<void(QRect)> buttonUpdate)
: _outer(CountOuterSize())
, _inner(QRectF({}, st::reactionCornerSize))
, _innerActive(QRect({}, CountMaxSizeWithMargins({})))
, _buttonShowTimer([=] { showButtonDelayed(); })
, _buttonUpdate(std::move(buttonUpdate)) {
	_inner.translate(QRectF({}, _outer).center() - _inner.center());
	_innerActive.translate(
		QRect({}, _outer).center() - _innerActive.center());

	const auto ratio = style::DevicePixelRatio();
	_cacheInOutService = QImage(
		_outer.width() * 3 * ratio,
		_outer.height() * kFramesCount * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_cacheInOutService.setDevicePixelRatio(ratio);
	_cacheInOutService.fill(Qt::transparent);
	_cacheParts = QImage(
		_outer.width() * kCacheColumsCount * ratio,
		_outer.height() * kFramesCount * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_cacheParts.setDevicePixelRatio(ratio);
	_cacheParts.fill(Qt::transparent);
	_cacheForPattern = QImage(
		QSize(
			_outer.width(),
			_outer.height() + st::reactionCornerAddedHeightMax) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_cacheForPattern.setDevicePixelRatio(ratio);
	_shadowBuffer = QImage(
		_outer * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_shadowBuffer.setDevicePixelRatio(ratio);

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
		return (e->type() == QEvent::Wheel
			&& consumeWheelEvent(static_cast<QWheelEvent*>(e.get())))
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
}

Manager::~Manager() = default;

void Manager::updateButton(ButtonParameters parameters) {
	const auto contextChanged = (_buttonContext != parameters.context);
	if (contextChanged) {
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
	_button = std::make_unique<Button>(_buttonUpdate, *_scheduledParameters);
}

void Manager::applyList(std::vector<Data::Reaction> list) {
	constexpr auto proj = &Data::Reaction::emoji;
	if (ranges::equal(_list, list, ranges::equal_to{}, proj, proj)) {
		return;
	}
	_list = std::move(list);
	_links = std::vector<ClickHandlerPtr>(_list.size());
	if (_list.empty()) {
		_mainReactionMedia = nullptr;
		return;
	}
	const auto main = _list.front().staticIcon;
	if (_mainReactionMedia && _mainReactionMedia->owner() == main) {
		return;
	}
	_mainReactionMedia = main->createMediaView();
	if (const auto image = _mainReactionMedia->getStickerLarge()) {
		setMainReactionImage(image->original());
	} else {
		main->session().downloaderTaskFinished(
		) | rpl::map([=] {
			return _mainReactionMedia->getStickerLarge();
		}) | rpl::filter_nullptr() | rpl::take(
			1
		) | rpl::start_with_next([=](not_null<Image*> image) {
			setMainReactionImage(image->original());
		}, _mainReactionLifetime);
	}
}

void Manager::setMainReactionImage(QImage image) {
	_mainReactionImage = std::move(image);
	ranges::fill(_validIn, false);
	ranges::fill(_validOut, false);
	ranges::fill(_validEmoji, false);
	loadOtherReactions();
}

QMarginsF Manager::innerMargins() const {
	return {
		_inner.x(),
		_inner.y(),
		_outer.width() - _inner.x() - _inner.width(),
		_outer.height() - _inner.y() - _inner.height(),
	};
}

QRectF Manager::buttonInner() const {
	return buttonInner(_button.get());
}

QRectF Manager::buttonInner(not_null<Button*> button) const {
	return QRectF(button->geometry()).marginsRemoved(innerMargins());
}

void Manager::loadOtherReactions() {
	for (const auto &reaction : _list) {
		const auto icon = reaction.staticIcon;
		if (_otherReactions.contains(icon)) {
			continue;
		}
		auto &entry = _otherReactions.emplace(icon, OtherReactionImage{
			.media = icon->createMediaView(),
		}).first->second;
		if (const auto image = entry.media->getStickerLarge()) {
			entry.image = image->original();
			entry.media = nullptr;
		} else if (!_otherReactionsLifetime) {
			icon->session().downloaderTaskFinished(
			) | rpl::start_with_next([=] {
				checkOtherReactions();
			}, _otherReactionsLifetime);
		}
	}
}

void Manager::checkOtherReactions() {
	auto all = true;
	for (auto &[icon, entry] : _otherReactions) {
		if (entry.media) {
			if (const auto image = entry.media->getStickerLarge()) {
				entry.image = image->original();
				entry.media = nullptr;
			} else {
				all = false;
			}
		}
	}
	if (all) {
		_otherReactionsLifetime.destroy();
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
		return nullptr;
	}
	const auto inner = buttonInner();
	const auto top = _button->expandUp()
		? (inner.y() + inner.height() - position.y())
		: (position.y() - inner.y());
	const auto scroll = _button->scroll();
	const auto shifted = top + scroll * (_button->expandUp() ? 1 : -1);
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
	return result;
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
	const auto opacity = Button::OpacityForScale(scale);
	if (opacity == 0.) {
		return;
	}
	const auto geometry = button->geometry();
	const auto position = geometry.topLeft();
	const auto size = geometry.size();
	const auto style = button->style();
	const auto patterned = (style == ButtonStyle::Outgoing)
		&& context.bubblesPattern
		&& !context.viewport.isEmpty()
		&& !context.bubblesPattern->pixmap.size().isEmpty();
	const auto shadow = context.st->shadowFg()->c;
	if (opacity != 1.) {
		p.setOpacity(opacity);
	}
	if (patterned) {
		const auto source = validateShadow(frameIndex, scale, shadow);
		paintLongImage(p, geometry, _cacheParts, source);
		validateCacheForPattern(frameIndex, scale, geometry, context);
		p.drawImage(
			geometry,
			_cacheForPattern,
			QRect(QPoint(), geometry.size() * style::DevicePixelRatio()));
	} else {
		const auto background = (style == ButtonStyle::Service)
			? context.st->msgServiceFg()->c
			: context.st->messageStyle(
				(style == ButtonStyle::Outgoing),
				false
			).msgBg->c;
		const auto source = validateFrame(
			style,
			frameIndex,
			scale,
			background,
			shadow);
		paintLongImage(p, geometry, _cacheInOutService, source);
	}

	const auto mainEmojiPosition = position + (button->expandUp()
		? QPoint(0, size.height() - _outer.height())
		: QPoint());
	if (size.height() > _outer.height()) {
		p.save();
		paintAllEmoji(p, button, scale, mainEmojiPosition);
		p.restore();
	} else {
		const auto source = validateEmoji(frameIndex, scale);
		p.drawImage(mainEmojiPosition, _cacheParts, source);
	}

	if (opacity != 1.) {
		p.setOpacity(1.);
	}
}

void Manager::paintLongImage(
		QPainter &p,
		QRect geometry,
		const QImage &image,
		QRect source) {
	if (geometry.height() == _outer.height()) {
		p.drawImage(geometry.topLeft(), image, source);
		return;
	}
	const auto factor = style::DevicePixelRatio();
	const auto part = (source.height() / factor) / 2 - 1;
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
		QPoint mainEmojiPosition) {
	const auto clip = buttonInner(button);
	p.setClipRect(clip);

	auto hq = PainterHighQualityEnabler(p);
	const auto between = st::reactionCornerSkip;
	const auto oneHeight = st::reactionCornerSize.height() + between;
	const auto oneSize = st::reactionCornerImage * scale;
	const auto expandUp = button->expandUp();
	const auto shift = QPoint(0, oneHeight * (expandUp ? -1 : 1));
	auto emojiPosition = mainEmojiPosition
		+ QPoint(0, button->scroll() * (expandUp ? 1 : -1));
	for (const auto &reaction : _list) {
		const auto inner = QRectF(_inner).translated(emojiPosition);
		const auto target = QRectF(
			inner.x() + (inner.width() - oneSize) / 2,
			inner.y() + (inner.height() - oneSize) / 2,
			oneSize,
			oneSize);
		if (target.intersects(clip)) {
			const auto i = _otherReactions.find(reaction.staticIcon);
			if (i != end(_otherReactions) && !i->second.image.isNull()) {
				p.drawImage(target, i->second.image);
			}
		}
		emojiPosition += shift;
	}
}

void Manager::validateCacheForPattern(
		int frameIndex,
		float64 scale,
		const QRect &geometry,
		const PaintContext &context) {
	auto q = QPainter(&_cacheForPattern);

	q.setCompositionMode(QPainter::CompositionMode_Source);
	const auto source = validateMask(frameIndex, scale);
	paintLongImage(q, QRect(QPoint(), geometry.size()), _cacheParts, source);

	q.setCompositionMode(QPainter::CompositionMode_SourceIn);
	Ui::PaintPatternBubblePart(
		q,
		context.viewport.translated(-geometry.topLeft()),
		context.bubblesPattern->pixmap,
		QRect(QPoint(), geometry.size()));
}

void Manager::applyPatternedShadow(const QColor &shadow) {
	if (_shadow == shadow) {
		return;
	}
	_shadow = shadow;
	ranges::fill(_validIn, false);
	ranges::fill(_validOut, false);
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
	const auto add = style::ConvertScale(2.);
	const auto shift = style::ConvertScale(1.);
	const auto extended = _inner.marginsAdded({ add, add, add, add });
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
		const auto size = st::reactionCornerImage * scale;
		const auto inner = _inner.translated(position);
		const auto target = QRectF(
			inner.x() + (inner.width() - size) / 2,
			inner.y() + (inner.height() - size) / 2,
			size,
			size);

		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(target, _mainReactionImage);
	}

	_validEmoji[frameIndex] = true;
	return result;
}

QRect Manager::validateFrame(
		ButtonStyle style,
		int frameIndex,
		float64 scale,
		const QColor &background,
		const QColor &shadow) {
	applyPatternedShadow(shadow);
	auto &valid = (style == ButtonStyle::Service)
		? _validService
		: (style == ButtonStyle::Outgoing)
		? _validOut
		: _validIn;
	auto &color = (style == ButtonStyle::Service)
		? _backgroundService
		: (style == ButtonStyle::Outgoing)
		? _backgroundOut
		: _backgroundIn;
	if (color != background) {
		color = background;
		ranges::fill(valid, false);
	}

	const auto columnIndex = (style == ButtonStyle::Service)
		? kServiceCacheIndex
		: (style == ButtonStyle::Outgoing)
		? kOutCacheIndex
		: kInCacheIndex;
	const auto result = cacheRect(frameIndex, columnIndex);
	if (valid[frameIndex]) {
		return result;
	}

	const auto shadowSource = validateShadow(frameIndex, scale, shadow);
	const auto position = result.topLeft() / style::DevicePixelRatio();
	auto p = QPainter(&_cacheInOutService);
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
	valid[frameIndex] = true;
	return result;
}

QRect Manager::validateMask(int frameIndex, float64 scale) {
	const auto result = cacheRect(frameIndex, kMaskCacheIndex);
	if (_validMask[frameIndex]) {
		return result;
	}

	auto p = QPainter(&_cacheParts);
	auto hq = PainterHighQualityEnabler(p);
	const auto position = result.topLeft() / style::DevicePixelRatio();
	const auto inner = _inner.translated(position);
	const auto radius = st::reactionCornerRadius;
	const auto center = inner.center();
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	p.save();
	p.translate(center);
	p.scale(scale, scale);
	p.translate(-center);
	p.drawRoundedRect(inner, radius, radius);

	_validMask[frameIndex] = true;
	return result;
}

} // namespace HistoryView
