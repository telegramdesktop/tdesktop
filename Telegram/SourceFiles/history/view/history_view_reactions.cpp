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
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "data/data_message_reactions.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "core/click_handler_types.h"
#include "main/main_session.h"
#include "styles/style_chat.h"
#include "styles/palette.h"

namespace HistoryView {
namespace {

constexpr auto kItemsPerRow = 5;

} // namespace

Reactions::Reactions(Data &&data)
: _data(std::move(data))
, _reactions(st::msgMinWidth / 2) {
	layout();
}

void Reactions::update(Data &&data, int availableWidth) {
	_data = std::move(data);
	layout();
	if (width() > 0) {
		resizeGetHeight(std::min(maxWidth(), availableWidth));
	}
}

void Reactions::updateSkipBlock(int width, int height) {
	_reactions.updateSkipBlock(width, height);
}

void Reactions::removeSkipBlock() {
	_reactions.removeSkipBlock();
}

void Reactions::layout() {
	layoutReactionsText();
	initDimensions();
}

void Reactions::layoutReactionsText() {
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

QSize Reactions::countOptimalSize() {
	return QSize(_reactions.maxWidth(), _reactions.minHeight());
}

QSize Reactions::countCurrentSize(int newWidth) {
	if (newWidth >= maxWidth()) {
		return optimalSize();
	}
	return { newWidth, _reactions.countHeight(newWidth) };
}

void Reactions::paint(
		Painter &p,
		const Ui::ChatStyle *st,
		int outerWidth,
		const QRect &clip) const {
	_reactions.draw(p, 0, 0, outerWidth);
}

Reactions::Data ReactionsDataFromMessage(not_null<Message*> message) {
	auto result = Reactions::Data();

	const auto item = message->message();
	result.reactions = item->reactions();
	result.chosenReaction = item->chosenReaction();
	return result;
}

ReactButton::ReactButton(
	Fn<void()> update,
	Fn<void()> react,
	QRect bubble)
: _update(std::move(update))
, _handler(std::make_shared<LambdaClickHandler>(react)) {
	updateGeometry(bubble);
}

void ReactButton::updateGeometry(QRect bubble) {
	const auto topLeft = bubble.topLeft()
		+ QPoint(bubble.width(), bubble.height())
		+ QPoint(st::reactionCornerOut.x(), st::reactionCornerOut.y())
		- QPoint(
			st::reactionCornerSize.width(),
			st::reactionCornerSize.height());
	_geometry = QRect(topLeft, st::reactionCornerSize);
	_imagePosition = _geometry.topLeft() + QPoint(
		(_geometry.width() - st::reactionCornerImage) / 2,
		(_geometry.height() - st::reactionCornerImage) / 2);
}

int ReactButton::bottomOutsideMargin(int fullHeight) const {
	return _geometry.y() + _geometry.height() - fullHeight;
}

std::optional<PointState> ReactButton::pointState(QPoint point) const {
	if (!_geometry.contains(point)) {
		return std::nullopt;
	}
	return PointState::Inside;
}

std::optional<TextState> ReactButton::textState(
		QPoint point,
		const StateRequest &request) const {
	if (!_geometry.contains(point)) {
		return std::nullopt;
	}
	auto result = TextState(nullptr, _handler);
	result.reactionArea = _geometry;
	return result;
}

void ReactButton::paint(Painter &p, const PaintContext &context) {
	const auto shown = _shownAnimation.value(_shown ? 1. : 0.);
	if (shown == 0.) {
		return;
	}
	p.setOpacity(shown);
	p.setBrush(context.messageStyle()->msgBg);
	p.setPen(st::shadowFg);
	const auto radius = _geometry.height() / 2;
	p.drawRoundedRect(_geometry, radius, radius);
	if (!_image.isNull()) {
		p.drawImage(_imagePosition, _image);
	}
	p.setOpacity(1.);
}

void ReactButton::toggle(bool shown) {
	if (_shown == shown) {
		return;
	}
	_shown = shown;
	_shownAnimation.start(_update, _shown ? 0. : 1., _shown ? 1. : 0., 120);
}

bool ReactButton::isHidden() const {
	return !_shown && !_shownAnimation.animating();
}

void ReactButton::show(not_null<const Data::Reaction*> reaction) {
	if (_media && _media->owner()  == reaction->staticIcon) {
		return;
	}
	_handler->setProperty(kReactionIdProperty, reaction->emoji);
	_media = reaction->staticIcon->createMediaView();
	const auto setImage = [=](not_null<Image*> image) {
		const auto size = st::reactionCornerImage;
		_image = Images::prepare(
			image->original(),
			size * style::DevicePixelRatio(),
			size * style::DevicePixelRatio(),
			Images::Option::Smooth | Images::Option::TransparentBackground,
			size,
			size);
		_image.setDevicePixelRatio(style::DevicePixelRatio());
	};
	if (const auto image = _media->getStickerLarge()) {
		setImage(image);
	} else {
		reaction->staticIcon->session().downloaderTaskFinished(
		) | rpl::map([=] {
			return _media->getStickerLarge();
		}) | rpl::filter_nullptr() | rpl::take(
			1
		) | rpl::start_with_next([=](not_null<Image*> image) {
			setImage(image);
			_update();
		}, _downloadTaskLifetime);
	}
}

ReactionsMenu::ReactionsMenu(
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

void ReactionsMenu::showAround(QRect area) {
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

void ReactionsMenu::toggle(bool shown, anim::type animated) {
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

[[nodiscard]] rpl::producer<QString> ReactionsMenu::chosen() const {
	return _chosen.events();
}

[[nodiscard]] rpl::lifetime &ReactionsMenu::lifetime() {
	return _dropdown.lifetime();
}

ReactionsMenuManager::ReactionsMenuManager(QWidget *parent)
: _parent(parent) {
}

ReactionsMenuManager::~ReactionsMenuManager() = default;

void ReactionsMenuManager::showReactionsMenu(
		FullMsgId context,
		QRect globalReactionArea,
		const std::vector<Data::Reaction> &list) {
	if (globalReactionArea.isEmpty()) {
		context = FullMsgId();
	}
	const auto listsEqual = ranges::equal(
		_list,
		list,
		ranges::equal_to(),
		&Data::Reaction::emoji,
		&Data::Reaction::emoji);
	const auto changed = (_context != context || !listsEqual);
	if (_menu && changed) {
		_menu->toggle(false, anim::type::normal);
		_hiding.push_back(std::move(_menu));
	}
	_context = context;
	_list = list;
	if (list.size() < 2 || !context || (!changed && !_menu)) {
		return;
	} else if (!_menu) {
		_menu = std::make_unique<ReactionsMenu>(_parent, list);
		_menu->chosen(
		) | rpl::start_with_next([=](QString emoji) {
			_menu->toggle(false, anim::type::normal);
			_hiding.push_back(std::move(_menu));
			_chosen.fire({ context, std::move(emoji) });
		}, _menu->lifetime());
	}
	const auto area = QRect(
		_parent->mapFromGlobal(globalReactionArea.topLeft()),
		globalReactionArea.size());
	_menu->showAround(area);
	_menu->toggle(true, anim::type::normal);
}

void ReactionsMenuManager::hideAll(anim::type animated) {
	if (animated == anim::type::instant) {
		_hiding.clear();
		_menu = nullptr;
	} else if (_menu) {
		_menu->toggle(false, anim::type::normal);
		_hiding.push_back(std::move(_menu));
	}
}

} // namespace HistoryView
