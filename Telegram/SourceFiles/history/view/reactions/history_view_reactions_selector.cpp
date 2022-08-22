/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/reactions/history_view_reactions_selector.h"

#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "history/history_item.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "main/main_session.h"
#include "chat_helpers/emoji_list_widget.h"
#include "chat_helpers/stickers_list_footer.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"

namespace HistoryView::Reactions {
namespace {

class ShiftedEmoji final : public Ui::Text::CustomEmoji {
public:
	ShiftedEmoji(
		not_null<Data::CustomEmojiManager*> manager,
		DocumentId id,
		Fn<void()> repaint,
		QPoint shift);

	QString entityData() override;
	void paint(
		QPainter &p,
		int x,
		int y,
		crl::time now,
		const QColor &preview,
		bool paused) override;
	void unload() override;

private:
	std::unique_ptr<Ui::Text::CustomEmoji> _real;
	QPoint _shift;

};

ShiftedEmoji::ShiftedEmoji(
	not_null<Data::CustomEmojiManager*> manager,
	DocumentId id,
	Fn<void()> repaint,
	QPoint shift)
: _real(manager->create(
	id,
	std::move(repaint),
	Data::CustomEmojiManager::SizeTag::ReactionFake))
, _shift(shift) {
}

QString ShiftedEmoji::entityData() {
	return _real->entityData();
}

void ShiftedEmoji::paint(
		QPainter &p,
		int x,
		int y,
		crl::time now,
		const QColor &preview,
		bool paused) {
	_real->paint(p, x + _shift.x(), y + _shift.y(), now, preview, paused);
}

void ShiftedEmoji::unload() {
	_real->unload();
}

} // namespace

Selector::Selector(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> parentController,
	Data::PossibleItemReactions &&reactions,
	IconFactory iconFactory)
: RpWidget(parent)
, _parentController(parentController.get())
, _reactions(std::move(reactions))
, _cachedRound(
	QSize(st::reactStripSkip * 2 + st::reactStripSize, st::reactStripHeight),
	st::reactionCornerShadow,
	st::reactStripHeight)
, _strip(
	QRect(0, 0, st::reactStripSize, st::reactStripSize),
	st::reactStripImage,
	crl::guard(this, [=] { update(_inner); }),
	std::move(iconFactory))
, _size(st::reactStripSize)
, _skipx(st::reactStripSkip)
, _skipy((st::reactStripHeight - st::reactStripSize) / 2)
, _skipBottom(st::reactStripHeight - st::reactStripSize - _skipy) {
	setMouseTracking(true);
}

int Selector::countWidth(int desiredWidth, int maxWidth) {
	const auto addedToMax = _reactions.customAllowed
		|| _reactions.morePremiumAvailable;
	const auto max = int(_reactions.recent.size()) + (addedToMax ? 1 : 0);
	const auto possibleColumns = std::min(
		(desiredWidth - 2 * _skipx + _size - 1) / _size,
		(maxWidth - 2 * _skipx) / _size);
	_columns = std::min(possibleColumns, max);
	_small = (possibleColumns - _columns > 1);
	_recentRows = (_strip.count() + _columns - 1) / _columns;
	const auto added = (_columns < max || _reactions.customAllowed)
		? Strip::AddedButton::Expand
		: _reactions.morePremiumAvailable
		? Strip::AddedButton::Premium
		: Strip::AddedButton::None;
	if (const auto cut = max - _columns) {
		_strip.applyList(ranges::make_subrange(
			begin(_reactions.recent),
			end(_reactions.recent) - (cut + (addedToMax ? 0 : 1))
		) | ranges::to_vector, added);
	} else {
		_strip.applyList(_reactions.recent, added);
	}
	_strip.clearAppearAnimations(false);
	return std::max(2 * _skipx + _columns * _size, desiredWidth);
}

QMargins Selector::extentsForShadow() const {
	return st::reactionCornerShadow;
}

int Selector::extendTopForCategories() const {
	return _reactions.customAllowed ? st::reactPanelEmojiPan.footer : 0;
}

int Selector::desiredHeight() const {
	return _reactions.customAllowed
		? st::emojiPanMaxHeight
		: (_skipy + _recentRows * _size + _skipBottom);
}

void Selector::setSpecialExpandTopSkip(int skip) {
	_specialExpandTopSkip = skip;
}

void Selector::initGeometry(int innerTop) {
	const auto extents = extentsForShadow();
	const auto parent = parentWidget()->rect();
	const auto innerWidth = 2 * _skipx + _columns * _size;
	const auto innerHeight = st::reactStripHeight;
	const auto width = innerWidth + extents.left() + extents.right();
	const auto height = innerHeight + extents.top() + extents.bottom();
	const auto left = style::RightToLeft() ? 0 : (parent.width() - width);
	_collapsedTopSkip = extendTopForCategories() + _specialExpandTopSkip;
	const auto top = innerTop - extents.top() - _collapsedTopSkip;
	const auto add = st::reactStripBubble.height() - extents.bottom();
	_outer = QRect(0, _collapsedTopSkip, width, height);
	_outerWithBubble = _outer.marginsAdded({ 0, 0, 0, add });
	setGeometry(_outerWithBubble.marginsAdded(
		{ 0, _collapsedTopSkip, 0, 0 }
	).translated(left, top));
	_inner = _outer.marginsRemoved(extents);
}

void Selector::updateShowState(
		float64 progress,
		float64 opacity,
		bool appearing,
		bool toggling) {
	if (_appearing && !appearing && !_paintBuffer.isNull()) {
		paintBackgroundToBuffer();
	}
	_appearing = appearing;
	_toggling = toggling;
	_appearProgress = progress;
	_appearOpacity = opacity;
	if (_appearing && isHidden()) {
		show();
		raise();
	} else if (_toggling && !isHidden()) {
		hide();
	}
	if (!_appearing && !_low) {
		_low = true;
		lower();
	}
	update();
}

void Selector::paintAppearing(QPainter &p) {
	p.setOpacity(_appearOpacity);

	const auto factor = style::DevicePixelRatio();
	if (_paintBuffer.size() != _outerWithBubble.size() * factor) {
		_paintBuffer = _cachedRound.PrepareImage(_outerWithBubble.size());
	}
	_paintBuffer.fill(st::defaultPopupMenu.menu.itemBg->c);
	auto q = QPainter(&_paintBuffer);
	const auto extents = extentsForShadow();
	const auto appearedWidth = anim::interpolate(
		_skipx * 2 + _size,
		_inner.width(),
		_appearProgress);
	const auto fullWidth = _inner.x() + appearedWidth + extents.right();
	const auto size = QSize(fullWidth, _outer.height());

	q.translate(_inner.topLeft() - QPoint(0, _collapsedTopSkip));
	_strip.paint(
		q,
		{ _skipx, _skipy },
		{ _size, 0 },
		{ 0, 0, appearedWidth, _inner.height() },
		1.,
		false);

	_cachedRound.setBackgroundColor(st::defaultPopupMenu.menu.itemBg->c);
	_cachedRound.setShadowColor(st::shadowFg->c);
	q.translate(QPoint(0, _collapsedTopSkip) - _inner.topLeft());
	const auto radius = st::reactStripHeight / 2;
	_cachedRound.overlayExpandedBorder(q, size, _appearProgress, radius, 1.);
	q.setCompositionMode(QPainter::CompositionMode_Source);
	q.fillRect(
		QRect{ 0, size.height(), width(), height() - size.height() },
		Qt::transparent);
	q.setCompositionMode(QPainter::CompositionMode_SourceOver);
	paintBubble(q, appearedWidth);
	q.end();

	p.drawImage(
		_outer.topLeft(),
		_paintBuffer,
		QRect(QPoint(), QSize(fullWidth, height()) * factor));
}

void Selector::paintBackgroundToBuffer() {
	const auto factor = style::DevicePixelRatio();
	if (_paintBuffer.size() != _outerWithBubble.size() * factor) {
		_paintBuffer = _cachedRound.PrepareImage(_outerWithBubble.size());
	}
	_paintBuffer.fill(Qt::transparent);

	auto p = QPainter(&_paintBuffer);
	const auto radius = _inner.height() / 2.;
	const auto frame = _cachedRound.validateFrame(0, 1., radius);
	const auto outer = _outer.translated(0, -_collapsedTopSkip);
	_cachedRound.FillWithImage(p, outer, frame);
	paintBubble(p, _inner.width());
}

void Selector::paintCollapsed(QPainter &p) {
	if (_paintBuffer.isNull()) {
		paintBackgroundToBuffer();
	}
	p.drawImage(_outer.topLeft(), _paintBuffer);
	_strip.paint(
		p,
		_inner.topLeft() + QPoint(_skipx, _skipy),
		{ _size, 0 },
		_inner,
		1.,
		false);
}

void Selector::paintExpanding(QPainter &p, float64 progress) {
	paintExpandingBg(p, progress);
	paintStripWithoutExpand(p);
	paintFadingExpandIcon(p, progress);
}

void Selector::paintExpandingBg(QPainter &p, float64 progress) {
	constexpr auto kFramesCount = Ui::RoundAreaWithShadow::kFramesCount;
	const auto frame = int(base::SafeRound(progress * (kFramesCount - 1)));
	const auto radiusStart = st::reactStripHeight / 2.;
	const auto radiusEnd = st::roundRadiusSmall;
	const auto radius = radiusStart + progress * (radiusEnd - radiusStart);
	const auto extents = extentsForShadow();
	const auto expanding = anim::easeOutCirc(1., progress);
	const auto expandUp = anim::interpolate(0, _collapsedTopSkip, expanding);
	const auto expandDown = anim::interpolate(
		0,
		(height() - _outer.y() - _outer.height()),
		expanding);
	const auto outer = _outer.marginsAdded({ 0, expandUp, 0, expandDown });
	const auto pattern = _cachedRound.validateFrame(frame, 1., radius);
	const auto fill = _cachedRound.FillWithImage(p, outer, pattern);
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::defaultPopupMenu.menu.itemBg);
	}
}

void Selector::paintStripWithoutExpand(QPainter &p) {
	_strip.paint(
		p,
		_inner.topLeft() + QPoint(_skipx, _skipy),
		{ _size, 0 },
		_inner.marginsRemoved({ 0, 0, _skipx + _size, 0 }),
		1.,
		false);
}

void Selector::paintFadingExpandIcon(QPainter &p, float64 progress) {
	if (progress >= 1.) {
		return;
	}
	p.setOpacity(1. - progress);
	const auto sub = anim::interpolate(0, _size / 3, progress);
	const auto expandIconPosition = _inner.topLeft()
		+ QPoint(_inner.width() - _size - _skipx, _skipy);
	const auto expandIconRect = QRect(
		expandIconPosition,
		QSize(_size, _size)
	).marginsRemoved({ sub, sub, sub, sub });
	p.drawImage(expandIconRect, _expandIconCache);
}

void Selector::paintExpanded(QPainter &p) {
	if (!_expandFinished) {
		finishExpand();
	}
	p.drawImage(0, 0, _paintBuffer);
	paintStripWithoutExpand(p);
}

void Selector::finishExpand() {
	Expects(!_expandFinished);

	_expandFinished = true;
	auto q = QPainter(&_paintBuffer);
	q.setCompositionMode(QPainter::CompositionMode_Source);
	const auto pattern = _cachedRound.validateFrame(
		kFramesCount - 1,
		1.,
		st::roundRadiusSmall);
	const auto fill = _cachedRound.FillWithImage(q, rect(), pattern);
	if (!fill.isEmpty()) {
		q.fillRect(fill, st::defaultPopupMenu.menu.itemBg);
	}
	if (_footer) {
		_footer->show();
	}
	_scroll->show();
}

void Selector::paintBubble(QPainter &p, int innerWidth) {
	const auto &bubble = st::reactStripBubble;
	const auto bubbleRight = std::min(
		st::reactStripBubbleRight,
		(innerWidth - bubble.width()) / 2);
	bubble.paint(
		p,
		_inner.x() + innerWidth - bubbleRight - bubble.width(),
		_inner.y() + _inner.height() - _collapsedTopSkip,
		width());
}

void Selector::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (_appearing) {
		paintAppearing(p);
	} else if (!_expanded) {
		paintCollapsed(p);
	} else if (const auto progress = _expanding.value(1.); progress < 1.) {
		paintExpanding(p, progress);
	} else {
		paintExpanded(p);
	}
}

void Selector::mouseMoveEvent(QMouseEvent *e) {
	setSelected(lookupSelectedIndex(e->pos()));
}

int Selector::lookupSelectedIndex(QPoint position) const {
	const auto p = position - _inner.topLeft();
	const auto max = _strip.count();
	const auto index = p.x() / _size;
	if (p.x() >= 0 && p.y() >= 0 && p.y() < _inner.height() && index < max) {
		return index;
	}
	return -1;
}

void Selector::setSelected(int index) {
	_strip.setSelected(index);
	const auto over = (index >= 0);
	if (_over != over) {
		_over = over;
		setCursor(over ? style::cur_pointer : style::cur_default);
		if (over) {
			Ui::Integration::Instance().registerLeaveSubscription(this);
		} else {
			Ui::Integration::Instance().unregisterLeaveSubscription(this);
		}
	}
}

void Selector::leaveEventHook(QEvent *e) {
	setSelected(-1);
}

void Selector::mousePressEvent(QMouseEvent *e) {
	_pressed = lookupSelectedIndex(e->pos());
}

void Selector::mouseReleaseEvent(QMouseEvent *e) {
	if (_pressed != lookupSelectedIndex(e->pos())) {
		return;
	}
	_pressed = -1;
	const auto selected = _strip.selected();
	if (selected == Strip::AddedButton::Premium) {
		_premiumPromoChosen.fire({});
	} else if (selected == Strip::AddedButton::Expand) {
		expand();
	} else if (const auto id = std::get_if<Data::ReactionId>(&selected)) {
		if (!id->empty()) {
			_chosen.fire({ .id = *id });
		}
	}
}

void Selector::expand() {
	const auto parent = parentWidget()->geometry();
	const auto additionalBottom = parent.height() - y() - height();
	const auto additional = _specialExpandTopSkip + additionalBottom;
	const auto strong = _parentController.get();
	if (additionalBottom < 0 || additional <= 0 || !strong) {
		return;
	} else if (additionalBottom > 0) {
		resize(width(), height() + additionalBottom);
		raise();
	}

	createList(strong);
	cacheExpandIcon();

	_paintBuffer = _cachedRound.PrepareImage(size());
	_expanded = true;
	_expanding.start([=] { update(); }, 0., 1., st::slideDuration);
}

void Selector::cacheExpandIcon() {
	_expandIconCache = _cachedRound.PrepareImage({ _size, _size });
	_expandIconCache.fill(Qt::transparent);
	auto q = QPainter(&_expandIconCache);
	const auto count = _strip.count();
	_strip.paint(
		q,
		QPoint(-(count - 1) * _size, 0),
		{ _size, 0 },
		QRect(-(count - 1) * _size, 0, count * _size, _size),
		1.,
		false);
}

void Selector::createList(not_null<Window::SessionController*> controller) {
	using namespace ChatHelpers;
	auto recent = std::vector<DocumentId>();
	auto defaultReactionIds = base::flat_map<DocumentId, QString>();
	recent.reserve(_reactions.recent.size());
	for (const auto &reaction : _reactions.recent) {
		if (const auto id = reaction->id.custom()) {
			recent.push_back(id);
		} else {
			recent.push_back(reaction->selectAnimation->id);
			defaultReactionIds.emplace(recent.back(), reaction->id.emoji());
		}
	};
	const auto manager = &controller->session().data().customEmojiManager();
	const auto shift = [&] {
		// See EmojiListWidget custom emoji position resolving.
		const auto area = st::emojiPanArea;
		const auto areaPosition = QPoint(
			(_size - area.width()) / 2,
			(_size - area.height()) / 2);
		const auto esize = Ui::Emoji::GetSizeLarge() / style::DevicePixelRatio();
		const auto innerPosition = QPoint(
			(area.width() - esize) / 2,
			(area.height() - esize) / 2);
		const auto customSize = Ui::Text::AdjustCustomEmojiSize(esize);
		const auto customSkip = (esize - customSize) / 2;
		const auto customPosition = QPoint(customSkip, customSkip);
		return QPoint(
			(_size - st::reactStripImage) / 2,
			(_size - st::reactStripImage) / 2
		) - areaPosition - innerPosition - customPosition;
	}();
	auto factory = [=](DocumentId id, Fn<void()> repaint) {
		return defaultReactionIds.contains(id)
			? std::make_unique<ShiftedEmoji>(
				manager,
				id,
				std::move(repaint),
				shift)
			: nullptr;
	};
	_scroll = Ui::CreateChild<Ui::ScrollArea>(this, st::reactPanelScroll);
	_scroll->hide();

	const auto st = lifetime().make_state<style::EmojiPan>(
		st::reactPanelEmojiPan);
	st->padding.setTop(_skipy);
	_list = _scroll->setOwnedWidget(
		object_ptr<EmojiListWidget>(_scroll, EmojiListDescriptor{
			.session = &controller->session(),
			.mode = (_reactions.customAllowed
				? EmojiListMode::FullReactions
				: EmojiListMode::RecentReactions),
			.controller = controller,
			.paused = [] { return false; },
			.customRecentList = std::move(recent),
			.customRecentFactory = std::move(factory),
			.st = st,
		})
	).data();

	_list->customChosen(
	) | rpl::start_with_next([=](const TabbedSelector::FileChosen &chosen) {
		const auto id = DocumentId{ chosen.document->id };
		const auto i = defaultReactionIds.find(id);
		if (i != end(defaultReactionIds)) {
			_chosen.fire({ .id = { i->second } });
		} else {
			_chosen.fire({ .id = { id } });
		}
	}, _list->lifetime());

	const auto inner = rect().marginsRemoved(extentsForShadow());
	const auto footer = _reactions.customAllowed
		? _list->createFooter().data()
		: nullptr;
	if ((_footer = static_cast<StickersListFooter*>(footer))) {
		_footer->setParent(this);
		_footer->hide();
		_footer->setGeometry(
			inner.x(),
			inner.y(),
			inner.width(),
			_footer->height());
		const auto shadow = Ui::CreateChild<Ui::PlainShadow>(this);
		_footer->geometryValue() | rpl::start_with_next([=](QRect geometry) {
			shadow->setGeometry(
				geometry.x(),
				geometry.y() + geometry.height(),
				geometry.width(),
				st::lineWidth);
		}, shadow->lifetime());
		shadow->show();
	}
	const auto geometry = inner.marginsRemoved(
		st::reactPanelEmojiPan.margin);
	_list->move(0, 0);
	_list->refreshEmoji();
	_list->resizeToWidth(geometry.width());
	_list->show();

	const auto updateVisibleTopBottom = [=] {
		const auto scrollTop = _scroll->scrollTop();
		const auto scrollBottom = scrollTop + _scroll->height();
		_list->setVisibleTopBottom(scrollTop, scrollBottom);
	};
	_scroll->scrollTopChanges(
	) | rpl::start_with_next(updateVisibleTopBottom, _list->lifetime());

	_list->scrollToRequests(
	) | rpl::start_with_next([=](int y) {
		_scroll->scrollToY(y);
	}, _list->lifetime());

	_scroll->setGeometry(inner.marginsRemoved({
		st::reactPanelEmojiPan.margin.left(),
		_footer ? _footer->height() : 0,
		0,
		0,
	}));

	updateVisibleTopBottom();
}

bool AdjustMenuGeometryForSelector(
		not_null<Ui::PopupMenu*> menu,
		QPoint desiredPosition,
		not_null<Selector*> selector) {
	const auto extend = st::reactStripExtend;
	const auto added = extend.left() + extend.right();
	const auto desiredWidth = menu->menu()->width() + added;
	const auto maxWidth = menu->st().menu.widthMax + added;
	const auto width = selector->countWidth(desiredWidth, maxWidth);
	const auto extents = selector->extentsForShadow();
	const auto categoriesTop = selector->extendTopForCategories();
	menu->setForceWidth(width - added);
	const auto height = menu->height();
	const auto fullTop = extents.top() + categoriesTop + extend.top();
	const auto minimalHeight = extents.top()
		+ std::min(
			selector->desiredHeight(),
			categoriesTop + st::emojiPanMinHeight / 2)
		+ extents.bottom();
	const auto willBeHeightWithoutBottomPadding = fullTop
		+ height
		- menu->st().shadow.extend.top();
	const auto additionalPaddingBottom
		= (willBeHeightWithoutBottomPadding < minimalHeight
			? (minimalHeight - willBeHeightWithoutBottomPadding)
			: 0);
	menu->setAdditionalMenuPadding(QMargins(
		extents.left() + extend.left(),
		fullTop,
		extents.right() + extend.right(),
		additionalPaddingBottom
	), QMargins(
		extents.left(),
		extents.top(),
		extents.right(),
		std::min(additionalPaddingBottom, extents.bottom())
	));
	if (!menu->prepareGeometryFor(desiredPosition)) {
		return false;
	}
	const auto origin = menu->preparedOrigin();
	if (!additionalPaddingBottom
		|| origin == Ui::PanelAnimation::Origin::TopLeft
		|| origin == Ui::PanelAnimation::Origin::TopRight) {
		return true;
	}
	menu->setAdditionalMenuPadding(QMargins(
		extents.left() + extend.left(),
		fullTop + additionalPaddingBottom,
		extents.right() + extend.right(),
		0
	), QMargins(
		extents.left(),
		extents.top(),
		extents.right(),
		0
	));
	selector->setSpecialExpandTopSkip(additionalPaddingBottom);
	return menu->prepareGeometryFor(desiredPosition);
}

AttachSelectorResult AttachSelectorToMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		QPoint desiredPosition,
		not_null<HistoryItem*> item,
		Fn<void(ChosenReaction)> chosen,
		Fn<void(FullMsgId)> showPremiumPromo,
		IconFactory iconFactory) {
	auto reactions = Data::LookupPossibleReactions(item);
	if (reactions.recent.empty() && !reactions.morePremiumAvailable) {
		return AttachSelectorResult::Skipped;
	}
	const auto selector = Ui::CreateChild<Selector>(
		menu.get(),
		controller,
		std::move(reactions),
		std::move(iconFactory));
	if (!AdjustMenuGeometryForSelector(menu, desiredPosition, selector)) {
		return AttachSelectorResult::Failed;
	}
	const auto selectorInnerTop = menu->preparedPadding().top()
		- st::reactStripExtend.top();
	selector->initGeometry(selectorInnerTop);
	selector->show();

	const auto itemId = item->fullId();

	selector->chosen() | rpl::start_with_next([=](ChosenReaction reaction) {
		menu->hideMenu();
		reaction.context = itemId;
		chosen(std::move(reaction));
	}, selector->lifetime());

	selector->premiumPromoChosen() | rpl::start_with_next([=] {
		menu->hideMenu();
		showPremiumPromo(itemId);
	}, selector->lifetime());

	const auto correctTop = selector->y();
	menu->showStateValue(
	) | rpl::start_with_next([=](Ui::PopupMenu::ShowState state) {
		const auto origin = menu->preparedOrigin();
		using Origin = Ui::PanelAnimation::Origin;
		if (origin == Origin::BottomLeft || origin == Origin::BottomRight) {
			const auto add = state.appearing
				? (menu->rect().marginsRemoved(
					menu->preparedPadding()
				).height() - state.appearingHeight)
				: 0;
			selector->move(selector->x(), correctTop + add);
		}
		selector->updateShowState(
			state.widthProgress * state.heightProgress,
			state.opacity,
			state.appearing,
			state.toggling);
	}, selector->lifetime());

	return AttachSelectorResult::Attached;
}

} // namespace HistoryView::Reactions
