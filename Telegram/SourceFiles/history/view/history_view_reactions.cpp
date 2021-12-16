/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_reactions.h"

#include "history/view/history_view_message.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history_message.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/image/image_prepare.h"
#include "data/data_message_reactions.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "core/click_handler_types.h"
#include "main/main_session.h"
#include "styles/style_chat.h"
#include "styles/palette.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kItemsPerRow = 5;
constexpr auto kToggleDuration = crl::time(80);
constexpr auto kActivateDuration = crl::time(150);
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

} // namespace

InlineList::InlineList(Data &&data)
: _data(std::move(data))
, _reactions(st::msgMinWidth / 2) {
	layout();
}

void InlineList::update(Data &&data, int availableWidth) {
	_data = std::move(data);
	layout();
	if (width() > 0) {
		resizeGetHeight(std::min(maxWidth(), availableWidth));
	}
}

void InlineList::updateSkipBlock(int width, int height) {
	_reactions.updateSkipBlock(width, height);
}

void InlineList::removeSkipBlock() {
	_reactions.removeSkipBlock();
}

void InlineList::layout() {
	layoutReactionsText();
	initDimensions();
}

void InlineList::layoutReactionsText() {
	if (_data.reactions.empty()) {
		_reactions.clear();
		return;
	}
	auto sorted = ranges::view::all(
		_data.reactions
	) | ranges::view::transform([](const auto &pair) {
		return std::make_pair(pair.first, pair.second);
	}) | ranges::to_vector;
	ranges::sort(sorted, std::greater<>(), &std::pair<QString, int>::second);

	auto text = TextWithEntities();
	for (const auto &[string, count] : sorted) {
		if (!text.text.isEmpty()) {
			text.append(" - ");
		}
		const auto chosen = (_data.chosenReaction == string);
		text.append(string);
		if (_data.chosenReaction == string) {
			text.append(Ui::Text::Bold(QString::number(count)));
		} else {
			text.append(QString::number(count));
		}
	}

	_reactions.setMarkedText(
		st::msgDateTextStyle,
		text,
		Ui::NameTextOptions());
}

QSize InlineList::countOptimalSize() {
	return QSize(_reactions.maxWidth(), _reactions.minHeight());
}

QSize InlineList::countCurrentSize(int newWidth) {
	if (newWidth >= maxWidth()) {
		return optimalSize();
	}
	return { newWidth, _reactions.countHeight(newWidth) };
}

void InlineList::paint(
		Painter &p,
		const Ui::ChatStyle *st,
		int outerWidth,
		const QRect &clip) const {
	_reactions.draw(p, 0, 0, outerWidth);
}

InlineListData InlineListDataFromMessage(not_null<Message*> message) {
	auto result = InlineListData();

	const auto item = message->message();
	result.reactions = item->reactions();
	result.chosenReaction = item->chosenReaction();
	return result;
}

Button::Button(
	Fn<void(QRect)> update,
	ButtonParameters parameters)
: _update(std::move(update)) {
	const auto initial = QRect(QPoint(), CountOuterSize());
	_geometry = initial.translated(parameters.center - initial.center());
	_outbg = parameters.outbg;
	applyState(parameters.active ? State::Active : State::Shown);
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

void Button::applyParameters(ButtonParameters parameters) {
	const auto geometry = _geometry.translated(
		parameters.center - _geometry.center());
	if (_outbg != parameters.outbg) {
		_outbg = parameters.outbg;
		_update(_geometry);
	}
	if (_geometry != geometry) {
		if (!_geometry.isNull()) {
			_update(_geometry);
		}
		_geometry = geometry;
		_update(_geometry);
	}
	applyState(parameters.active ? State::Active : State::Shown);
}

void Button::applyState(State state) {
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
	case State::Hidden: return 0.7;
	case State::Shown: return 1.;
	case State::Active: return 1.4;
	}
	Unexpected("State in ReactionButton::ScaleForState.");
}

float64 Button::OpacityForScale(float64 scale) {
	return (scale >= 1.)
		? 1.
		: ((scale - ScaleForState(State::Hidden))
			/ (ScaleForState(State::Shown) - ScaleForState(State::Hidden)));
}

float64 Button::currentScale() const {
	return _scaleAnimation.value(ScaleForState(_state));
}

Selector::Selector(
	QWidget *parent,
	const std::vector<Data::Reaction> &list)
: _dropdown(parent) {
	_dropdown.setAutoHiding(false);

	const auto content = _dropdown.setOwnedWidget(
		object_ptr<Ui::RpWidget>(&_dropdown));

	const auto count = int(list.size());
	const auto single = st::reactionPopupImage;
	const auto padding = st::reactionPopupPadding;
	const auto width = padding.left() + single + padding.right();
	const auto height = padding.top() + single + padding.bottom();
	const auto rows = (count + kItemsPerRow - 1) / kItemsPerRow;
	const auto columns = (int(list.size()) + rows - 1) / rows;
	const auto inner = QRect(0, 0, columns * width, rows * height);
	const auto outer = inner.marginsAdded(padding);
	content->resize(outer.size());

	_elements.reserve(list.size());
	auto x = padding.left();
	auto y = padding.top();
	auto row = -1;
	auto perrow = 0;
	while (_elements.size() != list.size()) {
		if (!perrow) {
			++row;
			perrow = (list.size() - _elements.size()) / (rows - row);
			x = (outer.width() - perrow * width) / 2;
		}
		auto &reaction = list[_elements.size()];
		_elements.push_back({
			.emoji = reaction.emoji,
			.geometry = QRect(x, y + row * height, width, height),
		});
		x += width;
		--perrow;
	}

	struct State {
		int selected = -1;
		int pressed = -1;
	};
	const auto state = content->lifetime().make_state<State>();
	content->setMouseTracking(true);
	content->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseMove) {
			const auto position = static_cast<QMouseEvent*>(e.get())->pos();
			const auto i = ranges::find_if(_elements, [&](const Element &e) {
				return e.geometry.contains(position);
			});
			const auto selected = (i != end(_elements))
				? int(i - begin(_elements))
				: -1;
			if (state->selected != selected) {
				state->selected = selected;
				content->update();
			}
		} else if (type == QEvent::MouseButtonPress) {
			state->pressed = state->selected;
			content->update();
		} else if (type == QEvent::MouseButtonRelease) {
			const auto pressed = std::exchange(state->pressed, -1);
			if (pressed >= 0) {
				content->update();
				if (pressed == state->selected) {
					_chosen.fire_copy(_elements[pressed].emoji);
				}
			}
		}
	}, content->lifetime());

	content->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(content);
		const auto radius = st::roundRadiusSmall;
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setBrush(st::emojiPanBg);
			p.setPen(Qt::NoPen);
			p.drawRoundedRect(content->rect(), radius, radius);
		}
		auto index = 0;
		const auto activeIndex = (state->pressed >= 0)
			? state->pressed
			: state->selected;
		const auto size = Ui::Emoji::GetSizeNormal();
		for (const auto &element : _elements) {
			const auto active = (index++ == activeIndex);
			if (active) {
				auto hq = PainterHighQualityEnabler(p);
				p.setBrush(st::windowBgOver);
				p.setPen(Qt::NoPen);
				p.drawRoundedRect(element.geometry, radius, radius);
			}
			if (const auto emoji = Ui::Emoji::Find(element.emoji)) {
				Ui::Emoji::Draw(
					p,
					emoji,
					size,
					element.geometry.x() + (width - size) / 2,
					element.geometry.y() + (height - size) / 2);
			}
		}
	}, content->lifetime());

	_dropdown.resizeToContent();
}

void Selector::showAround(QRect area) {
	const auto parent = _dropdown.parentWidget();
	const auto left = std::min(
		std::max(area.x() + (area.width() - _dropdown.width()) / 2, 0),
		parent->width() - _dropdown.width());
	_fromTop = (area.y() >= _dropdown.height());
	_fromLeft = (area.center().x() - left
		<= left + _dropdown.width() - area.center().x());
	const auto top = _fromTop
		? (area.y() - _dropdown.height())
		: (area.y() + area.height());
	_dropdown.move(left, top);
}

void Selector::toggle(bool shown, anim::type animated) {
	if (animated == anim::type::normal) {
		if (shown) {
			using Origin = Ui::PanelAnimation::Origin;
			_dropdown.showAnimated(_fromTop
				? (_fromLeft ? Origin::BottomLeft : Origin::BottomRight)
				: (_fromLeft ? Origin::TopLeft : Origin::TopRight));
		} else {
			_dropdown.hideAnimated();
		}
	} else if (shown) {
		_dropdown.showFast();
	} else {
		_dropdown.hideFast();
	}
}

[[nodiscard]] rpl::producer<QString> Selector::chosen() const {
	return _chosen.events();
}

[[nodiscard]] rpl::lifetime &Selector::lifetime() {
	return _dropdown.lifetime();
}

Manager::Manager(QWidget *selectorParent, Fn<void(QRect)> buttonUpdate)
: _outer(CountOuterSize())
, _inner(QRectF(
	(_outer.width() - st::reactionCornerSize.width()) / 2.,
	(_outer.height() - st::reactionCornerSize.height()) / 2.,
	st::reactionCornerSize.width(),
	st::reactionCornerSize.height()))
, _buttonUpdate(std::move(buttonUpdate))
, _buttonLink(std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
	if (_buttonContext && !_list.empty()) {
		_chosen.fire({
			.context = _buttonContext,
			.emoji = _list.front().emoji,
		});
	}
})))
, _selectorParent(selectorParent) {
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
	_shadowBuffer = QImage(
		_outer * ratio,
		QImage::Format_ARGB32_Premultiplied);
}

Manager::~Manager() = default;

void Manager::showButton(ButtonParameters parameters) {
	if (_button && _buttonContext != parameters.context) {
		_button->applyState(ButtonState::Hidden);
		_buttonHiding.push_back(std::move(_button));
	}
	_buttonContext = parameters.context;
	if (!_buttonContext || _list.size() < 2) {
		return;
	}
	if (!_button) {
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
	if (_list.size() < 2) {
		hideSelectors(anim::type::normal);
	}
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
	if (const auto current = _button.get()) {
		const auto geometry = current->geometry();
		if (geometry.contains(position)) {
			const auto maxInner = QRect({}, CountMaxSizeWithMargins({}));
			const auto shift = geometry.center() - maxInner.center();
			if (maxInner.translated(shift).contains(position)) {
				auto result = TextState(nullptr, _buttonLink);
				result.itemId = _buttonContext;
				return result;
			}
		}
	}
	return {};
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
		// #TODO reactions
	} else {
		const auto &stm = context.st->messageStyle(outbg, false);
		const auto background = stm.msgBg->c;
		const auto source = validateFrame(
			outbg,
			frameIndex,
			scale,
			stm.msgBg->c,
			shadow);
		p.drawImage(position, _cacheInOut, source);
	}
	if (opacity != 1.) {
		p.setOpacity(1.);
	}
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
	const auto radius = _inner.height() / 2;
	const auto center = _inner.center();
	const auto add = style::ConvertScale(1.5);
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
	const auto emojiSource = validateEmoji(frameIndex, scale);
	const auto position = result.topLeft() / style::DevicePixelRatio();
	auto p = QPainter(&_cacheInOut);
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.drawImage(position, _cacheParts, shadowSource);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	auto hq = PainterHighQualityEnabler(p);
	const auto inner = _inner.translated(position);
	const auto radius = inner.height() / 2;
	const auto center = inner.center();
	p.setPen(Qt::NoPen);
	p.setBrush(background);
	p.save();
	p.translate(center);
	p.scale(scale, scale);
	p.translate(-center);
	p.drawRoundedRect(inner, radius, radius);
	p.restore();

	p.drawImage(position, _cacheParts, emojiSource);

	p.end();
	valid[frameIndex] = true;
	return result;
}

void Manager::showSelector(Fn<QPoint(QPoint)> mapToGlobal) {
	if (!_button) {
		showSelector({}, {});
	} else {
		const auto geometry = _button->geometry();
		showSelector(
			_buttonContext,
			{ mapToGlobal(geometry.topLeft()), geometry.size() });
	}
}

void Manager::showSelector(FullMsgId context, QRect globalButtonArea) {
	if (globalButtonArea.isEmpty()) {
		context = FullMsgId();
	}
	const auto changed = (_selectorContext != context);
	if (_selector && changed) {
		_selector->toggle(false, anim::type::normal);
		_selectorHiding.push_back(std::move(_selector));
	}
	_selectorContext = context;
	if (_list.size() < 2 || !context || (!changed && !_selector)) {
		return;
	} else if (!_selector) {
		_selector = std::make_unique<Selector>(_selectorParent, _list);
		_selector->chosen(
		) | rpl::start_with_next([=](QString emoji) {
			_selector->toggle(false, anim::type::normal);
			_selectorHiding.push_back(std::move(_selector));
			_chosen.fire({ context, std::move(emoji) });
		}, _selector->lifetime());
	}
	const auto area = QRect(
		_selectorParent->mapFromGlobal(globalButtonArea.topLeft()),
		globalButtonArea.size());
	_selector->showAround(area);
	_selector->toggle(true, anim::type::normal);
}

void Manager::hideSelectors(anim::type animated) {
	if (animated == anim::type::instant) {
		_selectorHiding.clear();
		_selector = nullptr;
	} else if (_selector) {
		_selector->toggle(false, anim::type::normal);
		_selectorHiding.push_back(std::move(_selector));
	}
}

} // namespace HistoryView
