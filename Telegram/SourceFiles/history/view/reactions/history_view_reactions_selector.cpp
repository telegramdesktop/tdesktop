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
#include "ui/text/text_custom_emoji.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/painter.h"
#include "history/history_item.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "main/main_session.h"
#include "chat_helpers/emoji_list_widget.h"
#include "chat_helpers/stickers_list_footer.h"
#include "window/window_session_controller.h"
#include "boxes/premium_preview_box.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kExpandDuration = crl::time(300);
constexpr auto kScaleDuration = crl::time(120);
constexpr auto kFullDuration = kExpandDuration + kScaleDuration;
constexpr auto kExpandDelay = crl::time(40);
constexpr auto kDefaultColumns = 8;
constexpr auto kMinNonTransparentColumns = 7;

class StripEmoji final : public Ui::Text::CustomEmoji {
public:
	StripEmoji(
		std::unique_ptr<Ui::Text::CustomEmoji> wrapped,
		not_null<Strip*> strip,
		QPoint shift,
		int index);

	int width() override;
	QString entityData() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	const std::unique_ptr<Ui::Text::CustomEmoji> _wrapped;
	const not_null<Strip*> _strip;
	const QPoint _shift;
	const int _index = 0;
	bool _switched = false;

};

StripEmoji::StripEmoji(
	std::unique_ptr<Ui::Text::CustomEmoji> wrapped,
	not_null<Strip*> strip,
	QPoint shift,
	int index)
: _wrapped(std::move(wrapped))
, _strip(strip)
, _shift(shift)
, _index(index) {
}

int StripEmoji::width() {
	return _wrapped->width();
}

QString StripEmoji::entityData() {
	return _wrapped->entityData();
}

void StripEmoji::paint(QPainter &p, const Context &context) {
	if (_switched) {
		_wrapped->paint(p, context);
	} else if (_wrapped->readyInDefaultState()
		&& _strip->inDefaultState(_index)) {
		_switched = true;
		_wrapped->paint(p, context);
	} else {
		_strip->paintOne(p, _index, context.position + _shift, 1.);
	}
}

void StripEmoji::unload() {
	_wrapped->unload();
	_switched = true;
}

bool StripEmoji::ready() {
	return _wrapped->ready();
}

bool StripEmoji::readyInDefaultState() {
	return _wrapped->readyInDefaultState();
}

} // namespace

Selector::Selector(
	not_null<QWidget*> parent,
	const style::EmojiPan &st,
	std::shared_ptr<ChatHelpers::Show> show,
	const Data::PossibleItemReactionsRef &reactions,
	IconFactory iconFactory,
	Fn<void(bool fast)> close,
	bool child)
: Selector(
	parent,
	st,
	std::move(show),
	reactions,
	(reactions.customAllowed
		? ChatHelpers::EmojiListMode::FullReactions
		: ChatHelpers::EmojiListMode::RecentReactions),
	{},
	iconFactory,
	close,
	child) {
}

Selector::Selector(
	not_null<QWidget*> parent,
	const style::EmojiPan &st,
	std::shared_ptr<ChatHelpers::Show> show,
	ChatHelpers::EmojiListMode mode,
	std::vector<DocumentId> recent,
	Fn<void(bool fast)> close,
	bool child)
: Selector(
	parent,
	st,
	std::move(show),
	{ .customAllowed = true },
	mode,
	std::move(recent),
	nullptr,
	close,
	child) {
}

Selector::Selector(
	not_null<QWidget*> parent,
	const style::EmojiPan &st,
	std::shared_ptr<ChatHelpers::Show> show,
	const Data::PossibleItemReactionsRef &reactions,
	ChatHelpers::EmojiListMode mode,
	std::vector<DocumentId> recent,
	IconFactory iconFactory,
	Fn<void(bool fast)> close,
	bool child)
: RpWidget(parent)
, _st(st)
, _show(std::move(show))
, _reactions(reactions)
, _recent(std::move(recent))
, _listMode(mode)
, _jumpedToPremium([=] { close(false); })
, _cachedRound(
	QSize(2 * st::reactStripSkip + st::reactStripSize, st::reactStripHeight),
	st::reactionCornerShadow,
	st::reactStripHeight)
, _strip(iconFactory
	? std::make_unique<Strip>(
		_st,
		QRect(0, 0, st::reactStripSize, st::reactStripSize),
		st::reactStripImage,
		crl::guard(this, [=] { update(_inner); }),
		std::move(iconFactory))
	: nullptr)
, _size(st::reactStripSize)
, _skipx(countSkipLeft())
, _skipy((st::reactStripHeight - st::reactStripSize) / 2) {
	setMouseTracking(true);

	_useTransparency = child || Ui::Platform::TranslucentWindowsSupported();
}

bool Selector::useTransparency() const {
	return _useTransparency;
}

int Selector::recentCount() const {
	return int(_strip ? _reactions.recent.size() : _recent.size());
}

int Selector::countSkipLeft() const {
	const auto addedToMax = _reactions.customAllowed
		|| _reactions.morePremiumAvailable;
	const auto max = recentCount() + (addedToMax ? 1 : 0);
	return std::max(
		(st::reactStripMinWidth - (max * _size)) / 2,
		st::reactStripSkip);
}

int Selector::countWidth(int desiredWidth, int maxWidth) {
	const auto addedToMax = _reactions.customAllowed
		|| _reactions.morePremiumAvailable;
	const auto max = recentCount() + (addedToMax ? 1 : 0);
	const auto desiredColumns = std::max(
		(desiredWidth - 2 * _skipx + _size - 1) / _size,
		kMinNonTransparentColumns);
	const auto possibleColumns = std::min(
		desiredColumns,
		(maxWidth - 2 * _skipx) / _size);
	_columns = _strip ? std::min(possibleColumns, max) : kDefaultColumns;
	_small = (possibleColumns - _columns > 1);
	_recentRows = (recentCount()
		+ (_reactions.morePremiumAvailable ? 1 : 0)
		+ _columns - 1) / _columns;
	const auto added = (_columns < max || _reactions.customAllowed)
		? Strip::AddedButton::Expand
		: _reactions.morePremiumAvailable
		? Strip::AddedButton::Premium
		: Strip::AddedButton::None;
	if (_strip) {
		const auto &real = _reactions.recent;
		auto list = std::vector<not_null<const Data::Reaction*>>();
		list.reserve(_columns);
		if (const auto cut = max - _columns) {
			const auto from = begin(real);
			const auto till = end(real) - (cut + (addedToMax ? 0 : 1));
			for (auto i = from; i != till; ++i) {
				list.push_back(&*i);
			}
		} else {
			for (const auto &reaction : real) {
				list.push_back(&reaction);
			}
		}
		_strip->applyList(list, added);
		_strip->clearAppearAnimations(false);
	}
	return std::max(2 * _skipx + _columns * _size, desiredWidth);
}

QMargins Selector::marginsForShadow() const {
	const auto line = st::lineWidth;
	return useTransparency()
		? st::reactionCornerShadow
		: QMargins(line, line, line, line);
}

int Selector::extendTopForCategories() const {
	return _reactions.customAllowed ? _st.footer : 0;
}

int Selector::minimalHeight() const {
	return _skipy
		+ (_recentRows * _size)
		+ st::emojiPanRadius
		+ _st.padding.bottom();
}

void Selector::setSpecialExpandTopSkip(int skip) {
	_specialExpandTopSkip = skip;
}

void Selector::initGeometry(int innerTop) {
	const auto margins = marginsForShadow();
	const auto parent = parentWidget()->rect();
	const auto innerWidth = 2 * _skipx + _columns * _size;
	const auto innerHeight = st::reactStripHeight;
	const auto width = _useTransparency
		? (innerWidth + margins.left() + margins.right())
		: parent.width();
	const auto height = innerHeight + margins.top() + margins.bottom();
	const auto left = style::RightToLeft() ? 0 : (parent.width() - width);
	_collapsedTopSkip = _useTransparency
		? (extendTopForCategories() + _specialExpandTopSkip)
		: 0;
	const auto top = innerTop - margins.top() - _collapsedTopSkip;
	const auto add = _st.icons.stripBubble.height() - margins.bottom();
	_outer = QRect(0, _collapsedTopSkip, width, height);
	_outerWithBubble = _outer.marginsAdded({ 0, 0, 0, add });
	setGeometry(_outerWithBubble.marginsAdded(
		{ 0, _collapsedTopSkip, 0, 0 }
	).translated(left, top));
	_inner = _outer.marginsRemoved(margins);

	if (!_strip) {
		expand();
	}
}

void Selector::beforeDestroy() {
	if (_list) {
		_list->beforeHiding();
	}
}

rpl::producer<> Selector::escapes() const {
	return _escapes.events();
}

void Selector::updateShowState(
		float64 progress,
		float64 opacity,
		bool appearing,
		bool toggling) {
	if (_useTransparency
		&& _appearing
		&& !appearing
		&& !_paintBuffer.isNull()) {
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

int Selector::countAppearedWidth(float64 progress) const {
	return anim::interpolate(_skipx * 2 + _size, _inner.width(), progress);
}

void Selector::paintAppearing(QPainter &p) {
	Expects(_strip != nullptr);

	p.setOpacity(_appearOpacity);

	const auto factor = style::DevicePixelRatio();
	if (_paintBuffer.size() != _outerWithBubble.size() * factor) {
		_paintBuffer = _cachedRound.PrepareImage(_outerWithBubble.size());
	}
	_paintBuffer.fill(_st.bg->c);
	auto q = QPainter(&_paintBuffer);
	const auto margins = marginsForShadow();
	const auto appearedWidth = countAppearedWidth(_appearProgress);
	const auto fullWidth = _inner.x() + appearedWidth + margins.right();
	const auto size = QSize(fullWidth, _outer.height());

	q.translate(_inner.topLeft() - QPoint(0, _collapsedTopSkip));
	_strip->paint(
		q,
		{ _skipx, _skipy },
		{ _size, 0 },
		{ 0, 0, appearedWidth, _inner.height() },
		1.,
		false);

	_cachedRound.setBackgroundColor(_st.bg->c);
	_cachedRound.setShadowColor(st::shadowFg->c);
	q.translate(QPoint(0, _collapsedTopSkip) - _inner.topLeft());
	const auto radius = st::reactStripHeight / 2;
	_cachedRound.overlayExpandedBorder(
		q,
		size,
		_appearProgress,
		radius,
		radius,
		1.);
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
	if (!_useTransparency) {
		return;
	}
	const auto factor = style::DevicePixelRatio();
	if (_paintBuffer.size() != _outerWithBubble.size() * factor) {
		_paintBuffer = _cachedRound.PrepareImage(_outerWithBubble.size());
	}
	_paintBuffer.fill(Qt::transparent);

	_cachedRound.setBackgroundColor(_st.bg->c);
	_cachedRound.setShadowColor(st::shadowFg->c);

	auto p = QPainter(&_paintBuffer);
	const auto radius = _inner.height() / 2.;
	const auto frame = _cachedRound.validateFrame(0, 1., radius);
	const auto outer = _outer.translated(0, -_collapsedTopSkip);
	_cachedRound.FillWithImage(p, outer, frame);
	paintBubble(p, _inner.width());
}

void Selector::paintCollapsed(QPainter &p) {
	Expects(_strip != nullptr);

	if (_useTransparency) {
		if (_paintBuffer.isNull()) {
			paintBackgroundToBuffer();
		}
		p.drawImage(_outer.topLeft(), _paintBuffer);
	} else {
		p.fillRect(_inner, _st.bg);
	}
	_strip->paint(
		p,
		_inner.topLeft() + QPoint(_skipx, _skipy),
		{ _size, 0 },
		_inner,
		1.,
		false);
}

void Selector::paintExpanding(Painter &p, float64 progress) {
	const auto rects = updateExpandingRects(progress);
	paintExpandingBg(p, rects);
	progress /= kFullDuration;
	if (_footer) {
		_footer->paintExpanding(
			p,
			rects.categories,
			rects.radius,
			RectPart::BottomRight);
	}
	_list->paintExpanding(
		p,
		rects.list.marginsRemoved(_st.margin),
		rects.finalBottom,
		rects.expanding,
		progress,
		RectPart::TopRight);
	paintFadingExpandIcon(p, progress);
}

Selector::ExpandingRects Selector::updateExpandingRects(float64 progress) {
	progress = (progress >= kExpandDuration)
		? 1.
		: (progress / kExpandDuration);
	constexpr auto kFramesCount = Ui::RoundAreaWithShadow::kFramesCount;
	const auto frame = int(base::SafeRound(progress * (kFramesCount - 1)));
	const auto radiusStart = st::reactStripHeight / 2.;
	const auto radiusEnd = st::emojiPanRadius;
	const auto radius = _reactions.customAllowed
		? (radiusStart + progress * (radiusEnd - radiusStart))
		: radiusStart;
	const auto margins = marginsForShadow();
	const auto expanding = anim::easeOutCirc(1., progress);
	const auto expandUp = anim::interpolate(0, _collapsedTopSkip, expanding);
	const auto expandDown = anim::interpolate(
		0,
		(height() - _outer.y() - _outer.height()),
		expanding);
	const auto outer = _outer.marginsAdded({ 0, expandUp, 0, expandDown });
	const auto categories = anim::interpolate(
		0,
		extendTopForCategories(),
		expanding);
	const auto inner = outer.marginsRemoved(margins);
	_shadowTop = inner.y() + categories;
	_shadowSkip = (_useTransparency && categories < radius)
		? int(base::SafeRound(
			radius - sqrt(categories * (2 * radius - categories))))
		: 0;
	return {
		.categories = QRect(inner.x(), inner.y(), inner.width(), categories),
		.list = inner.marginsRemoved({ 0, categories, 0, 0 }),
		.radius = radius,
		.expanding = expanding,
		.finalBottom = height() - margins.bottom(),
		.frame = frame,
		.outer = outer,
	};
}

void Selector::paintExpandingBg(QPainter &p, const ExpandingRects &rects) {
	if (_useTransparency) {
		const auto pattern = _cachedRound.validateFrame(
			rects.frame,
			1.,
			rects.radius);
		const auto fill = _cachedRound.FillWithImage(p, rects.outer, pattern);
		if (!fill.isEmpty()) {
			p.fillRect(fill, _st.bg);
		}
	} else {
		paintNonTransparentExpandRect(p, rects.outer - marginsForShadow());
	}
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
	p.setOpacity(1.);
}

void Selector::paintNonTransparentExpandRect(
		QPainter &p,
		const QRect &inner) const {
	p.fillRect(inner, _st.bg);
	p.fillRect(
		inner.x(),
		inner.y() + inner.height(),
		inner.width(),
		st::lineWidth,
		st::defaultPopupMenu.shadow.fallback);
}

void Selector::paintExpanded(QPainter &p) {
	if (!_expandFinished) {
		finishExpand();
	}
	if (_useTransparency) {
		p.drawImage(0, 0, _paintBuffer);
	} else {
		paintNonTransparentExpandRect(p, rect() - marginsForShadow());
	}
}

void Selector::finishExpand() {
	Expects(!_expandFinished);

	_expandFinished = true;
	updateExpandingRects(kExpandDuration);
	if (_useTransparency) {
		auto q = QPainter(&_paintBuffer);
		q.setCompositionMode(QPainter::CompositionMode_Source);
		const auto pattern = _cachedRound.validateFrame(
			kFramesCount - 1,
			1.,
			st::emojiPanRadius);
		const auto fill = _cachedRound.FillWithImage(q, rect(), pattern);
		if (!fill.isEmpty()) {
			q.fillRect(fill, _st.bg);
		}
	}
	if (_footer) {
		_footer->show();
	}
	_scroll->show();
	_list->afterShown();
	_show->session().api().updateCustomEmoji();
}

void Selector::paintBubble(QPainter &p, int innerWidth) {
	const auto &bubble = _st.icons.stripBubble;
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
	auto p = Painter(this);
	if (_strip && _appearing && _useTransparency) {
		paintAppearing(p);
	} else if (_strip && !_expanded) {
		paintCollapsed(p);
	} else if (const auto progress = _expanding.value(kFullDuration)
		; progress < kFullDuration) {
		paintExpanding(p, progress);
	} else {
		paintExpanded(p);
	}
}

void Selector::mouseMoveEvent(QMouseEvent *e) {
	if (!_strip) {
		return;
	}
	setSelected(lookupSelectedIndex(e->pos()));
}

int Selector::lookupSelectedIndex(QPoint position) const {
	const auto p = position - _inner.topLeft() - QPoint(_skipx, _skipy);
	const auto max = _strip->count();
	const auto index = p.x() / _size;
	if (p.x() >= 0 && p.y() >= 0 && p.y() < _inner.height() && index < max) {
		return index;
	}
	return -1;
}

void Selector::setSelected(int index) {
	Expects(_strip != nullptr);

	if (index >= 0 && _expandScheduled) {
		return;
	}
	_strip->setSelected(index);
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
	if (!_strip) {
		return;
	}
	setSelected(-1);
}

void Selector::mousePressEvent(QMouseEvent *e) {
	if (!_strip) {
		return;
	}
	_pressed = lookupSelectedIndex(e->pos());
}

void Selector::mouseReleaseEvent(QMouseEvent *e) {
	if (!_strip || _pressed != lookupSelectedIndex(e->pos())) {
		return;
	}
	_pressed = -1;
	const auto selected = _strip->selected();
	if (selected == Strip::AddedButton::Premium) {
		_premiumPromoChosen.fire({});
	} else if (selected == Strip::AddedButton::Expand) {
		expand();
	} else if (const auto id = std::get_if<Data::ReactionId>(&selected)) {
		if (!id->empty()) {
			_chosen.fire(lookupChosen(*id));
		}
	}
}

ChosenReaction Selector::lookupChosen(const Data::ReactionId &id) const {
	Expects(_strip != nullptr);

	auto result = ChosenReaction{
		.id = id,
	};
	const auto index = _strip->fillChosenIconGetIndex(result);
	if (result.icon.isNull()) {
		return result;
	}
	const auto rect = QRect(_skipx + index * _size, _skipy, _size, _size);
	const auto imageSize = _strip->computeOverSize();
	result.globalGeometry = mapToGlobal(QRect(
		_inner.x() + rect.x() + (rect.width() - imageSize) / 2,
		_inner.y() + rect.y() + (rect.height() - imageSize) / 2,
		imageSize,
		imageSize));
	return result;
}

void Selector::preloadAllRecentsAnimations() {
	const auto preload = [&](DocumentData *document) {
		const auto view = document
			? document->activeMediaView()
			: nullptr;
		if (view) {
			view->checkStickerLarge();
		}
	};
	for (const auto &reaction : _reactions.recent) {
		if (!reaction.id.custom()) {
			preload(reaction.centerIcon);
		}
		preload(reaction.aroundAnimation);
	}
}

void Selector::expand() {
	if (_expandScheduled) {
		return;
	}
	_expandScheduled = true;
	_willExpand.fire({});
	preloadAllRecentsAnimations();
	const auto parent = parentWidget()->geometry();
	const auto margins = marginsForShadow();
	const auto heightLimit = _reactions.customAllowed
		? st::emojiPanMaxHeight
		: minimalHeight();
	const auto willBeHeight = std::min(
		parent.height() - y(),
		margins.top() + heightLimit + margins.bottom());
	const auto additionalBottom = willBeHeight - height();
	const auto additional = _specialExpandTopSkip + additionalBottom;
	if (additionalBottom < 0 || additional <= 0) {
		return;
	} else if (additionalBottom > 0) {
		resize(width(), height() + additionalBottom);
		raise();
	}

	createList();
	cacheExpandIcon();

	[[maybe_unused]] const auto grabbed = Ui::GrabWidget(_scroll);
	_list->prepareExpanding();
	setSelected(-1);

	base::call_delayed(kExpandDelay, this, [this] {
		const auto full = kExpandDuration + kScaleDuration;
		_expanded = true;
		_paintBuffer = _cachedRound.PrepareImage(size());
		_expanding.start([=] { update(); }, 0., full, full);
	});
}

void Selector::cacheExpandIcon() {
	if (!_strip) {
		return;
	}
	_expandIconCache = _cachedRound.PrepareImage({ _size, _size });
	_expandIconCache.fill(Qt::transparent);
	auto q = QPainter(&_expandIconCache);
	_strip->paintOne(q, _strip->count() - 1, { 0, 0 }, 1.);
}

void Selector::createList() {
	using namespace ChatHelpers;
	auto recent = _recent;
	auto defaultReactionIds = base::flat_map<DocumentId, QString>();
	if (_strip) {
		recent.reserve(recentCount());
		auto index = 0;
		const auto inStrip = _strip->count();
		for (const auto &reaction : _reactions.recent) {
			if (const auto id = reaction.id.custom()) {
				recent.push_back(id);
			} else {
				recent.push_back(reaction.selectAnimation->id);
				defaultReactionIds.emplace(recent.back(), reaction.id.emoji());
			}
			if (index + 1 < inStrip) {
				_defaultReactionInStripMap.emplace(recent.back(), index++);
			}
		};
	}
	const auto manager = &_show->session().data().customEmojiManager();
	_stripPaintOneShift = [&] {
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
		return areaPosition + innerPosition + customPosition;
	}();
	_defaultReactionShift = QPoint(
		(_size - st::reactStripImage) / 2,
		(_size - st::reactStripImage) / 2
	) - _stripPaintOneShift;
	auto factory = [=](DocumentId id, Fn<void()> repaint)
	-> std::unique_ptr<Ui::Text::CustomEmoji> {
		const auto tag = Data::CustomEmojiManager::SizeTag::Large;
		const auto sizeOverride = st::reactStripImage;
		const auto isDefaultReaction = defaultReactionIds.contains(id);
		auto result = isDefaultReaction
			? std::make_unique<Ui::Text::ShiftedEmoji>(
				manager->create(id, std::move(repaint), tag, sizeOverride),
				_defaultReactionShift)
			: manager->create(id, std::move(repaint), tag);
		const auto i = _defaultReactionInStripMap.find(id);
		if (i != end(_defaultReactionInStripMap)) {
			Assert(_strip != nullptr);
			return std::make_unique<StripEmoji>(
				std::move(result),
				_strip.get(),
				-_stripPaintOneShift,
				i->second);
		}
		return result;
	};
	_scroll = Ui::CreateChild<Ui::ScrollArea>(this, st::reactPanelScroll);
	_scroll->hide();

	const auto st = lifetime().make_state<style::EmojiPan>(_st);
	st->padding.setTop(_skipy);
	if (!_reactions.customAllowed) {
		st->bg = st::transparent;
	}
	_list = _scroll->setOwnedWidget(
		object_ptr<EmojiListWidget>(_scroll, EmojiListDescriptor{
			.show = _show,
			.mode = _listMode,
			.paused = [] { return false; },
			.customRecentList = std::move(recent),
			.customRecentFactory = std::move(factory),
			.st = st,
		})
	).data();

	_list->escapes() | rpl::start_to_stream(_escapes, _list->lifetime());

	_list->customChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		const auto id = DocumentId{ data.document->id };
		const auto i = defaultReactionIds.find(id);
		const auto reactionId = (i != end(defaultReactionIds))
			? Data::ReactionId{ i->second }
			: Data::ReactionId{ id };
		_chosen.fire({
			.id = reactionId,
			.icon = data.messageSendingFrom.frame,
			.globalGeometry = data.messageSendingFrom.globalStartGeometry,
		});
	}, _list->lifetime());

	_list->jumpedToPremium(
	) | rpl::start_with_next(_jumpedToPremium, _list->lifetime());

	const auto inner = rect().marginsRemoved(marginsForShadow());
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
		_shadowTop = _outer.y();
		_shadowSkip = _useTransparency ? (st::reactStripHeight / 2) : 0;
		_shadow = Ui::CreateChild<Ui::PlainShadow>(this);
		rpl::combine(
			_shadowTop.value(),
			_shadowSkip.value()
		) | rpl::start_with_next([=](int top, int skip) {
			_shadow->setGeometry(
				inner.x() + skip,
				top,
				inner.width() - 2 * skip,
				st::lineWidth);
		}, _shadow->lifetime());
		_shadow->show();
	}
	const auto geometry = inner.marginsRemoved(_st.margin);
	_list->move(0, 0);
	_list->resizeToWidth(geometry.width());
	_list->refreshEmoji();
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
		_shadow->update();
	}, _list->lifetime());

	_scroll->setGeometry(inner.marginsRemoved({
		_st.margin.left(),
		_footer ? _footer->height() : 0,
		0,
		0,
	}));
	_list->setMinimalHeight(geometry.width(), _scroll->height());

	updateVisibleTopBottom();
}

bool AdjustMenuGeometryForSelector(
		not_null<Ui::PopupMenu*> menu,
		QPoint desiredPosition,
		not_null<Selector*> selector) {
	const auto useTransparency = selector->useTransparency();
	const auto extend = useTransparency
		? st::reactStripExtend
		: QMargins(0, st::lineWidth + st::reactStripHeight, 0, 0);
	const auto added = extend.left() + extend.right();
	const auto desiredWidth = menu->menu()->width() + added;
	const auto maxWidth = menu->st().menu.widthMax + added;
	const auto width = selector->countWidth(desiredWidth, maxWidth);
	const auto margins = selector->marginsForShadow();
	const auto categoriesTop = selector->useTransparency()
		? selector->extendTopForCategories()
		: 0;
	menu->setForceWidth(width - added);
	const auto height = menu->height();
	const auto fullTop = margins.top() + categoriesTop + extend.top();
	const auto minimalHeight = margins.top()
		+ selector->minimalHeight()
		+ margins.bottom();
	const auto willBeHeightWithoutBottomPadding = fullTop
		+ height
		- menu->st().shadow.extend.top();
	const auto additionalPaddingBottom
		= (willBeHeightWithoutBottomPadding >= minimalHeight
			? 0
			: useTransparency
			? (minimalHeight - willBeHeightWithoutBottomPadding)
			: 0);
	menu->setAdditionalMenuPadding(QMargins(
		margins.left() + extend.left(),
		fullTop,
		margins.right() + extend.right(),
		additionalPaddingBottom
	), QMargins(
		margins.left(),
		margins.top(),
		margins.right(),
		std::min(additionalPaddingBottom, margins.bottom())
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
		margins.left() + extend.left(),
		fullTop + additionalPaddingBottom,
		margins.right() + extend.right(),
		0
	), QMargins(
		margins.left(),
		margins.top(),
		margins.right(),
		0
	));
	selector->setSpecialExpandTopSkip(additionalPaddingBottom);
	return menu->prepareGeometryFor(desiredPosition);
}

AttachSelectorResult MakeJustSelectorMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		QPoint desiredPosition,
		ChatHelpers::EmojiListMode mode,
		std::vector<DocumentId> recent,
		Fn<void(ChosenReaction)> chosen) {
	const auto selector = Ui::CreateChild<Selector>(
		menu.get(),
		st::reactPanelEmojiPan,
		controller->uiShow(),
		mode,
		std::move(recent),
		[=](bool fast) { menu->hideMenu(fast); },
		false); // child
	if (!AdjustMenuGeometryForSelector(menu, desiredPosition, selector)) {
		return AttachSelectorResult::Failed;
	}
	if (mode != ChatHelpers::EmojiListMode::RecentReactions) {
		Ui::Platform::FixPopupMenuNativeEmojiPopup(menu);
	}
	const auto selectorInnerTop = menu->preparedPadding().top()
		- st::reactStripExtend.top();
	menu->animatePhaseValue(
	) | rpl::start_with_next([=](Ui::PopupMenu::AnimatePhase phase) {
		if (phase == Ui::PopupMenu::AnimatePhase::StartHide) {
			selector->beforeDestroy();
		}
	}, selector->lifetime());
	selector->initGeometry(selectorInnerTop);
	selector->show();

	selector->chosen() | rpl::start_with_next([=](ChosenReaction reaction) {
		menu->hideMenu();
		chosen(std::move(reaction));
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

	const auto weak = base::make_weak(controller);
	controller->enableGifPauseReason(
		Window::GifPauseReason::MediaPreview);
	QObject::connect(menu.get(), &QObject::destroyed, [weak] {
		if (const auto strong = weak.get()) {
			strong->disableGifPauseReason(
				Window::GifPauseReason::MediaPreview);
		}
	});

	return AttachSelectorResult::Attached;
}

AttachSelectorResult AttachSelectorToMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		QPoint desiredPosition,
		not_null<HistoryItem*> item,
		Fn<void(ChosenReaction)> chosen,
		Fn<void(FullMsgId)> showPremiumPromo,
		IconFactory iconFactory) {
	const auto result = AttachSelectorToMenu(
		menu,
		desiredPosition,
		st::reactPanelEmojiPan,
		controller->uiShow(),
		Data::LookupPossibleReactions(item),
		std::move(iconFactory));
	if (!result) {
		return result.error();
	}
	const auto selector = *result;
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

	const auto weak = base::make_weak(controller);
	controller->enableGifPauseReason(
		Window::GifPauseReason::MediaPreview);
	QObject::connect(menu.get(), &QObject::destroyed, [weak] {
		if (const auto strong = weak.get()) {
			strong->disableGifPauseReason(
				Window::GifPauseReason::MediaPreview);
		}
	});

	return AttachSelectorResult::Attached;
}

auto AttachSelectorToMenu(
	not_null<Ui::PopupMenu*> menu,
	QPoint desiredPosition,
	const style::EmojiPan &st,
	std::shared_ptr<ChatHelpers::Show> show,
	const Data::PossibleItemReactionsRef &reactions,
	IconFactory iconFactory)
-> base::expected<not_null<Selector*>, AttachSelectorResult> {
	if (reactions.recent.empty() && !reactions.morePremiumAvailable) {
		return base::make_unexpected(AttachSelectorResult::Skipped);
	}
	const auto withSearch = reactions.customAllowed;
	const auto selector = Ui::CreateChild<Selector>(
		menu.get(),
		st,
		std::move(show),
		std::move(reactions),
		std::move(iconFactory),
		[=](bool fast) { menu->hideMenu(fast); },
		false); // child
	if (!AdjustMenuGeometryForSelector(menu, desiredPosition, selector)) {
		return base::make_unexpected(AttachSelectorResult::Failed);
	}
	if (withSearch) {
		Ui::Platform::FixPopupMenuNativeEmojiPopup(menu);
	}
	const auto selectorInnerTop = selector->useTransparency()
		? (menu->preparedPadding().top() - st::reactStripExtend.top())
		: st::lineWidth;
	menu->animatePhaseValue(
	) | rpl::start_with_next([=](Ui::PopupMenu::AnimatePhase phase) {
		if (phase == Ui::PopupMenu::AnimatePhase::StartHide) {
			selector->beforeDestroy();
		}
	}, selector->lifetime());
	selector->initGeometry(selectorInnerTop);
	selector->show();

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

	return selector;
}

} // namespace HistoryView::Reactions
