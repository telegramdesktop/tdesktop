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
#include "styles/style_chat.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kItemsPerRow = 5;
constexpr auto kToggleDuration = crl::time(80);
constexpr auto kActivateDuration = crl::time(150);
constexpr auto kExpandDuration = crl::time(150);
constexpr auto kInCacheIndex = 0;
constexpr auto kOutCacheIndex = 1;
constexpr auto kShadowCacheIndex = 0;
constexpr auto kEmojiCacheIndex = 1;
constexpr auto kMaskCacheIndex = 2;
constexpr auto kCacheColumsCount = 3;

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

void CopyImagePart(QImage &to, const QImage &from, QRect source) {
	Expects(to.size() == source.size());
	Expects(QRect(QPoint(), from.size()).contains(source));
	Expects(to.format() == from.format());
	Expects(to.bytesPerLine() == to.width() * 4);

	const auto perPixel = 4;
	const auto fromPerLine = from.bytesPerLine();
	const auto toPerLine = to.bytesPerLine();
	auto toBytes = reinterpret_cast<char*>(to.bits());
	auto fromBytes = reinterpret_cast<const char*>(from.bits())
		+ (source.y() * fromPerLine)
		+ (source.x() * perPixel);
	for (auto y = 0, height = source.height(); y != height; ++y) {
		memcpy(toBytes, fromBytes, toPerLine);
		toBytes += toPerLine;
		fromBytes += fromPerLine;
	}
}

} // namespace

Button::Button(
	Fn<void(QRect)> update,
	ButtonParameters parameters)
: _update(std::move(update))
, _collapsed(QPoint(), CountOuterSize())
, _finalHeight(_collapsed.height()) {
	applyParameters(parameters, nullptr);
}

Button::~Button() = default;

bool Button::outbg() const {
	return _outbg;
}

bool Button::isHidden() const {
	return (_state == State::Hidden) && !_scaleAnimation.animating();
}

QRect Button::geometry() const {
	return _geometry;
}

bool Button::expandUp() const {
	return (_expandDirection == ExpandDirection::Up);
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
	const auto state = inside
		? State::Inside
		: active
		? State::Active
		: State::Shown;
	applyState(state, update);
	if (_outbg != parameters.outbg) {
		_outbg = parameters.outbg;
		if (update) {
			update(_geometry);
		}
	}
}

void Button::updateExpandDirection(const ButtonParameters &parameters) {
	const auto maxAddedHeight = (parameters.reactionsCount - 1)
		* (st::reactionCornerSize.height() + st::reactionCornerSkip);
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
	return std::max(
		((scale - ScaleForState(State::Hidden))
			/ (ScaleForState(State::Shown) - ScaleForState(State::Hidden))),
		1.);
}

float64 Button::currentScale() const {
	return _scaleAnimation.value(ScaleForState(_state));
}

Manager::Manager(Fn<void(QRect)> buttonUpdate)
: _outer(CountOuterSize())
, _inner(QRectF({}, st::reactionCornerSize))
, _innerActive(QRect({}, CountMaxSizeWithMargins({})))
, _buttonUpdate(std::move(buttonUpdate))
, _buttonLink(std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
	if (_buttonContext && !_list.empty()) {
		_chosen.fire({
			.context = _buttonContext,
			.emoji = _list.front().emoji,
		});
	}
}))) {
	_inner.translate(QRectF({}, _outer).center() - _inner.center());
	_innerActive.translate(
		QRect({}, _outer).center() - _innerActive.center());

	const auto ratio = style::DevicePixelRatio();
	_cacheInOut = QImage(
		_outer.width() * 2 * ratio,
		_outer.height() * kFramesCount * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_cacheInOut.setDevicePixelRatio(ratio);
	_cacheInOut.fill(Qt::transparent);
	_cacheParts = QImage(
		_outer.width() * kCacheColumsCount * ratio,
		_outer.height() * kFramesCount * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_cacheParts.setDevicePixelRatio(ratio);
	_cacheParts.fill(Qt::transparent);
	_cacheForPattern = QImage(
		_outer * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_cacheForPattern.setDevicePixelRatio(ratio);
	_shadowBuffer = QImage(
		_outer * ratio,
		QImage::Format_ARGB32_Premultiplied);
}

Manager::~Manager() = default;

void Manager::updateButton(ButtonParameters parameters) {
	if (_button && _buttonContext != parameters.context) {
		_button->applyState(ButtonState::Hidden);
		_buttonHiding.push_back(std::move(_button));
	}
	_buttonContext = parameters.context;
	parameters.reactionsCount = _list.size();
	if (!_buttonContext || _list.empty()) {
		return;
	} else if (!_button) {
		_button = std::make_unique<Button>(_buttonUpdate, parameters);
	} else {
		_button->applyParameters(parameters);
	}
}

void Manager::applyList(std::vector<Data::Reaction> list) {
	constexpr auto proj = &Data::Reaction::emoji;
	if (ranges::equal(_list, list, ranges::equal_to{}, proj, proj)) {
		return;
	}
	_list = std::move(list);
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

TextState Manager::buttonTextState(QPoint position) const {
	if (overCurrentButton(position)) {
		auto result = TextState(nullptr, _buttonLink);
		result.itemId = _buttonContext;
		return result;
	}
	return {};
}

bool Manager::overCurrentButton(QPoint position) const {
	if (!_button) {
		return false;
	}
	const auto geometry = _button->geometry();
	return geometry.marginsRemoved(st::reactionCornerShadow).contains(position);
}

void Manager::remove(FullMsgId context) {
	if (_buttonContext == context) {
		_buttonContext = {};
		_button = nullptr;
	}
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
	const auto outbg = button->outbg();
	const auto patterned = outbg
		&& context.bubblesPattern
		&& !context.viewport.isEmpty()
		&& !context.bubblesPattern->pixmap.size().isEmpty();
	const auto shadow = context.st->shadowFg()->c;
	if (opacity != 1.) {
		p.setOpacity(opacity);
	}
	if (patterned) {
		p.drawImage(
			position,
			_cacheParts,
			validateShadow(frameIndex, scale, shadow));

		validateCacheForPattern(frameIndex, scale, geometry, context);
		p.drawImage(geometry, _cacheForPattern);
	} else {
		const auto &stm = context.st->messageStyle(outbg, false);
		const auto background = stm.msgBg->c;
		const auto source = validateFrame(
			outbg,
			frameIndex,
			scale,
			stm.msgBg->c,
			shadow);
		if (size.height() > _outer.height()) {
			const auto factor = style::DevicePixelRatio();
			const auto part = (source.height() / factor) / 2 - 1;
			const auto fill = size.height() - 2 * part;
			const auto half = part * factor;
			const auto top = source.height() - half;
			p.drawImage(
				position,
				_cacheInOut,
				QRect(source.x(), source.y(), source.width(), half));
			p.drawImage(
				QRect(
					position + QPoint(0, part),
					QSize(source.width() / factor, fill)),
				_cacheInOut,
				QRect(
					source.x(),
					source.y() + half,
					source.width(),
					top - half));
			p.drawImage(
				position + QPoint(0, part + fill),
				_cacheInOut,
				QRect(source.x(), source.y() + top, source.width(), half));
		} else {
			p.drawImage(position, _cacheInOut, source);
		}
	}
	const auto mainEmojiPosition = position + (button->expandUp()
		? QPoint(0, size.height() - _outer.height())
		: QPoint());
	p.drawImage(
		mainEmojiPosition,
		_cacheParts,
		validateEmoji(frameIndex, scale));
	if (opacity != 1.) {
		p.setOpacity(1.);
	}
}

void Manager::validateCacheForPattern(
		int frameIndex,
		float64 scale,
		const QRect &geometry,
		const PaintContext &context) {
	CopyImagePart(
		_cacheForPattern,
		_cacheParts,
		validateMask(frameIndex, scale));
	auto q = QPainter(&_cacheForPattern);
	q.setCompositionMode(QPainter::CompositionMode_SourceIn);
	Ui::PaintPatternBubblePart(
		q,
		context.viewport.translated(-geometry.topLeft()),
		context.bubblesPattern->pixmap,
		QRect(QPoint(), _outer));
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
		bool outbg,
		int frameIndex,
		float64 scale,
		const QColor &background,
		const QColor &shadow) {
	applyPatternedShadow(shadow);
	auto &valid = outbg ? _validOut : _validIn;
	auto &color = outbg ? _backgroundOut : _backgroundIn;
	if (color != background) {
		color = background;
		ranges::fill(valid, false);
	}

	const auto columnIndex = outbg ? kOutCacheIndex : kInCacheIndex;
	const auto result = cacheRect(frameIndex, columnIndex);
	if (valid[frameIndex]) {
		return result;
	}

	const auto shadowSource = validateShadow(frameIndex, scale, shadow);
	//const auto emojiSource = validateEmoji(frameIndex, scale);
	const auto position = result.topLeft() / style::DevicePixelRatio();
	auto p = QPainter(&_cacheInOut);
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
	//p.restore();

	//p.drawImage(position, _cacheParts, emojiSource);

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
