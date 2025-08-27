/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/reactions/history_view_reactions_selector.h"

#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_utilities.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/integration.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "history/view/media/history_view_sticker.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "chat_helpers/emoji_list_widget.h"
#include "chat_helpers/stickers_list_footer.h"
#include "chat_helpers/stickers_list_widget.h"
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

UnifiedFactoryOwner::UnifiedFactoryOwner(
	not_null<Main::Session*> session,
	const std::vector<Data::Reaction> &reactions,
	Strip *strip)
: _session(session)
, _strip(strip) {
	auto index = 0;
	const auto inStrip = _strip ? _strip->count() : 0;
	_unifiedIdsList.reserve(reactions.size());
	for (const auto &reaction : reactions) {
		_unifiedIdsList.push_back(reaction.selectAnimation->id);

		const auto unifiedId = _unifiedIdsList.back();
		if (unifiedId != reaction.id.custom()) {
			_defaultReactionIds.emplace(unifiedId, reaction.id);
		}
		if (index + 1 < inStrip) {
			_defaultReactionInStripMap.emplace(unifiedId, index++);
		}
	}

	_stripPaintOneShift = [&] {
		// See EmojiListWidget custom emoji position resolving.
		const auto size = st::reactStripSize;
		const auto area = st::emojiPanArea;
		const auto areaPosition = QPoint(
			(size - area.width()) / 2,
			(size - area.height()) / 2);
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
		(st::reactStripSize - st::reactStripImage) / 2,
		(st::reactStripSize - st::reactStripImage) / 2
	) - _stripPaintOneShift;
}

Data::ReactionId UnifiedFactoryOwner::lookupReactionId(
		DocumentId unifiedId) const {
	const auto i = _defaultReactionIds.find(unifiedId);
	return (i != end(_defaultReactionIds))
		? i->second
		: Data::ReactionId{ unifiedId };
}

UnifiedFactoryOwner::RecentFactory UnifiedFactoryOwner::factory() {
	return [=](DocumentId id, Fn<void()> repaint)
	-> std::unique_ptr<Ui::Text::CustomEmoji> {
		const auto tag = Data::CustomEmojiManager::SizeTag::Large;
		const auto sizeOverride = st::reactStripImage;
		const auto i = _defaultReactionIds.find(id);
		const auto isDefaultReaction = (i != end(_defaultReactionIds))
			&& !i->second.custom();
		const auto manager = &_session->data().customEmojiManager();
		auto result = isDefaultReaction
			? std::make_unique<Ui::Text::ShiftedEmoji>(
				manager->create(id, std::move(repaint), tag, sizeOverride),
				_defaultReactionShift)
			: manager->create(id, std::move(repaint), tag);
		const auto j = _defaultReactionInStripMap.find(id);
		if (j != end(_defaultReactionInStripMap)) {
			Assert(_strip != nullptr);
			return std::make_unique<StripEmoji>(
				std::move(result),
				_strip,
				-_stripPaintOneShift,
				j->second);
		}
		return result;
	};
}

Selector::Selector(
	not_null<QWidget*> parent,
	const style::EmojiPan &st,
	std::shared_ptr<ChatHelpers::Show> show,
	const Data::PossibleItemReactionsRef &reactions,
	TextWithEntities about,
	Fn<void(bool fast)> close,
	IconFactory iconFactory,
	Fn<bool()> paused,
	bool child)
: Selector(
	parent,
	st,
	std::move(show),
	reactions,
	(reactions.customAllowed
		? ChatHelpers::EmojiListMode::FullReactions
		: reactions.stickers.empty()
		? ChatHelpers::EmojiListMode::RecentReactions
		: ChatHelpers::EmojiListMode::MessageEffects),
	{},
	std::move(about),
	std::move(iconFactory),
	std::move(paused),
	std::move(close),
	child) {
}

#if 0 // not ready
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
	{},
	nullptr,
	close,
	child) {
}
#endif

Selector::Selector(
	not_null<QWidget*> parent,
	const style::EmojiPan &st,
	std::shared_ptr<ChatHelpers::Show> show,
	const Data::PossibleItemReactionsRef &reactions,
	ChatHelpers::EmojiListMode mode,
	std::vector<DocumentId> recent,
	TextWithEntities about,
	IconFactory iconFactory,
	Fn<bool()> paused,
	Fn<void(bool fast)> close,
	bool child)
: RpWidget(parent)
, _st(st)
, _show(std::move(show))
, _reactions(reactions)
, _recent(std::move(recent))
, _listMode(mode)
, _paused(std::move(paused))
, _jumpedToPremium([=] { close(false); })
, _cachedRound(
	QSize(2 * st::reactStripSkip + st::reactStripSize, st::reactStripHeight),
	st::reactionCornerShadow,
	st::reactStripHeight)
, _strip(std::make_unique<Strip>(
	_st,
	QRect(0, 0, st::reactStripSize, st::reactStripSize),
	st::reactStripImage,
	crl::guard(this, [=] { update(_inner); }),
	std::move(iconFactory)))
, _about(about.empty()
	? nullptr
	: std::make_unique<Ui::FlatLabel>(
		this,
		rpl::single(about),
		_st.about))
, _size(st::reactStripSize)
, _skipx(countSkipLeft())
, _skipy((st::reactStripHeight - st::reactStripSize) / 2) {
	setMouseTracking(true);

	if (_about) {
		_about->setClickHandlerFilter([=](const auto &...) {
			_escapes.fire({});
			return true;
		});
	}

	_useTransparency = child || Ui::Platform::TranslucentWindowsSupported();
}

Selector::~Selector() = default;

bool Selector::useTransparency() const {
	return _useTransparency;
}

int Selector::recentCount() const {
	return int(_strip ? _reactions.recent.size() : _recent.size());
}

int Selector::countSkipLeft() const {
	const auto addedToMax = _reactions.customAllowed;
	const auto max = recentCount() + (addedToMax ? 1 : 0);
	return std::max(
		(st::reactStripMinWidth - (max * _size)) / 2,
		st::reactStripSkip);
}

int Selector::countWidth(int desiredWidth, int maxWidth) {
	const auto addedToMax = _reactions.customAllowed;
	const auto max = recentCount() + (addedToMax ? 1 : 0);
	const auto desiredColumns = std::max(
		(desiredWidth - 2 * _skipx + _size - 1) / _size,
		kMinNonTransparentColumns);
	const auto possibleColumns = std::min(
		desiredColumns,
		(maxWidth - 2 * _skipx) / _size);
	_columns = _strip ? std::min(possibleColumns, max) : kDefaultColumns;
	_small = (possibleColumns - _columns > 1);
	_recentRows = (recentCount() + _columns - 1) / _columns;
	const auto added = (_columns < max || _reactions.customAllowed)
		? Strip::AddedButton::Expand
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

int Selector::effectPreviewHeight() const {
	if (_listMode != ChatHelpers::EmojiListMode::MessageEffects) {
		return 0;
	}
	return st::previewMenu.shadow.extend.top()
		+ HistoryView::Sticker::MessageEffectSize().height()
		+ st::effectPreviewSend.height
		+ st::previewMenu.shadow.extend.bottom();
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

int Selector::extendTopForCategoriesAndAbout(int width) const {
	if (_about) {
		const auto padding = _st.aboutPadding;
		const auto available = width - padding.left() - padding.right();
		const auto countAboutHeight = [&](int width) {
			_about->resizeToWidth(width);
			return _about->height();
		};
		const auto desired = Ui::FindNiceTooltipWidth(
			std::min(available, _st.about.minWidth * 2),
			available,
			countAboutHeight);

		_about->resizeToWidth(desired);
		_aboutExtend = padding.top() + _about->height() + padding.bottom();
	} else {
		_aboutExtend = 0;
	}
	return std::max(extendTopForCategories(), _aboutExtend);
}

int Selector::opaqueExtendTopAbout(int width) const {
	if (_about) {
		const auto padding = _st.aboutPadding;
		const auto available = width - padding.left() - padding.right();
		const auto countAboutHeight = [&](int width) {
			_about->resizeToWidth(width);
			return _about->height();
		};
		const auto desired = Ui::FindNiceTooltipWidth(
			std::min(available, _st.about.minWidth * 2),
			available,
			countAboutHeight);

		_about->resizeToWidth(desired);
		_aboutExtend = padding.top() + _about->height() + padding.bottom();
	} else {
		_aboutExtend = 0;
	}
	return _aboutExtend;
}

void Selector::setOpaqueHeightExpand(int expand, Fn<void(int)> apply) {
	_opaqueHeightExpand = expand;
	_opaqueApplyHeightExpand = std::move(apply);
}

int Selector::minimalHeight(int fullWidth) const {
	auto inner = _recentRows * _size;
	if (const auto stickers = int(_reactions.stickers.size())) {
		// See StickersListWidget.
		const auto listWidth = fullWidth
			- marginsForShadow().left()
			- marginsForShadow().right()
			- _st.margin.left()
			- _st.margin.right();
		const auto availableWidth = listWidth
			- (st::stickerPanPadding - _st.margin.left());
		const auto min = st::stickerEffectWidthMin;
		if (const auto columns = availableWidth / min) {
			const auto rows = (stickers + columns - 1) / columns;
			const auto singleWidth = availableWidth / columns;
			const auto singleHeight = singleWidth;
			const auto stickersHeight = rows * singleHeight;
			inner += _st.header + stickersHeight;
		}
	}
	if (_listMode == ChatHelpers::EmojiListMode::MessageEffects) {
		inner += _st.searchMargin.top()
			+ _st.search.height
			+ _st.searchMargin.bottom();
	}
	return _skipy
		+ std::min(inner, st::emojiPanMinHeight)
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
	const auto forAbout = width - margins.left() - margins.right();
	_collapsedTopSkip = _useTransparency
		? (extendTopForCategoriesAndAbout(forAbout) + _specialExpandTopSkip)
		: opaqueExtendTopAbout(forAbout);
	_topAddOnExpand = _collapsedTopSkip - _aboutExtend;
	const auto height = margins.top()
		+ _aboutExtend
		+ innerHeight
		+ margins.bottom();
	const auto left = style::RightToLeft() ? 0 : (parent.width() - width);
	const auto top = innerTop
		- margins.top()
		- (_useTransparency ? _collapsedTopSkip : 0);
	const auto add = _useTransparency
		? (_st.icons.stripBubble.height() - margins.bottom())
		: 0;
	_outer = QRect(0, _collapsedTopSkip - _aboutExtend, width, height);
	_outerWithBubble = _outer.marginsAdded({ 0, 0, 0, add });
	setGeometry(_outerWithBubble.marginsAdded(
		{ 0, _outer.y(), 0, 0}
	).translated(left, top));
	_inner = _outer.marginsRemoved(
		margins + QMargins{ 0, _aboutExtend, 0, 0 });
	if (_about) {
		_about->move(
			_inner.x() + (_inner.width() - _about->width()) / 2,
			_outer.y() + margins.top() + _st.aboutPadding.top());
		_aboutCache = Ui::GrabWidgetToImage(_about.get());
	}

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
		if (_about && _about->isHidden()) {
			_about->show();
		}
	} else if (_useTransparency
		&& !_appearing
		&& appearing
		&& _about) {
		_about->hide();
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

	q.translate(_inner.topLeft() - QPoint(0, _outer.y()));
	_strip->paint(
		q,
		{ _skipx, _skipy },
		{ _size, 0 },
		{ 0, 0, appearedWidth, _inner.height() },
		1.,
		false);

	_cachedRound.setBackgroundColor(_st.bg->c);
	_cachedRound.setShadowColor(st::shadowFg->c);
	q.translate(QPoint(0, _outer.y()) - _inner.topLeft());
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

	const auto aboutRight = _inner.x() + appearedWidth;
	if (_about && _about->isHidden() && aboutRight > _about->x()) {
		const auto aboutWidth = aboutRight - _about->x();
		p.drawImage(
			_about->geometry().topLeft(),
			_aboutCache,
			QRect(QPoint(), QSize(aboutWidth, _about->height()) * factor));
	}
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
	const auto outer = _outer.translated(0, -_outer.y());
	const auto fill = _cachedRound.FillWithImage(p, outer, frame);
	if (!fill.isEmpty()) {
		p.fillRect(fill, _st.bg);
	}
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
		p.fillRect(_outer.marginsRemoved(marginsForShadow()), _st.bg);
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
	if (_about && !_aboutCache.isNull()) {
		p.setClipping(false);
		p.setOpacity((1. - progress) * (1. - progress));
		const auto y = _about->y() - _outer.y() + rects.outer.y();
		p.drawImage(_about->x(), y, _aboutCache);
		p.setOpacity(1.);
	}
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
	const auto expandUp = anim::interpolate(0, _topAddOnExpand, expanding);
	const auto expandDown = anim::interpolate(
		0,
		(height() - _outer.y() - _outer.height()),
		expanding);
	const auto outer = _outer.marginsAdded({ 0, expandUp, 0, expandDown });
	const auto inner = outer.marginsRemoved(margins
		+ QMargins{
			0,
			anim::interpolate(_aboutExtend, 0, expanding),
			0,
			0 });
	const auto list = outer.marginsRemoved(margins
		+ QMargins{
			0,
			anim::interpolate(
				_aboutExtend,
				extendTopForCategories(),
				expanding),
			0,
			0 });
	_shadowTop = list.y();
	const auto categories = list.y() - inner.y();
	_shadowSkip = (_useTransparency && categories < radius)
		? int(base::SafeRound(
			radius - sqrt(categories * (2 * radius - categories))))
		: 0;

	if (!_useTransparency && _opaqueApplyHeightExpand) {
		Ui::PostponeCall(this, [=] {
			_opaqueApplyHeightExpand(y() + outer.y() + outer.height());
		});
	}

	return {
		.categories = QRect(inner.x(), inner.y(), inner.width(), categories),
		.list = list,
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
		_inner.y() + _inner.height() - _outer.y(),
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
	if (selected == Strip::AddedButton::Expand) {
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
		: minimalHeight(width());
	const auto opaqueAdded = _useTransparency ? 0 : _opaqueHeightExpand;
	const auto willBeHeight = std::min(
		parent.height() - y() + opaqueAdded,
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

	[[maybe_unused]] const auto grabbed = Ui::GrabWidget(_scroll); // clazy:exclude=unused-non-trivial-variable
	_list->prepareExpanding();
	setSelected(-1);

	base::call_delayed(kExpandDelay, this, [this] {
		const auto full = kExpandDuration + kScaleDuration;
		if (_about) {
			_about->hide();
		}
		_expanded = true;
		_paintBuffer = _cachedRound.PrepareImage(size());
		_expanding.start([=] {
			update();
		}, 0., full, full);
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
	_unifiedFactoryOwner = std::make_unique<UnifiedFactoryOwner>(
		&_show->session(),
		_strip ? _reactions.recent : std::vector<Data::Reaction>(),
		_strip.get());
	_scroll = Ui::CreateChild<Ui::ScrollArea>(this, !_useTransparency
		? st::emojiScroll
		: _reactions.customAllowed
		? st::reactPanelScroll
		: st::reactPanelScrollRounded);
	_scroll->hide();

	const auto effects = !_reactions.stickers.empty();
	const auto st = lifetime().make_state<style::EmojiPan>(_st);
	st->padding.setTop(_skipy);
	if (!_reactions.customAllowed) {
		st->bg = st::transparent;
	}
	auto lists = _scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(_scroll));
	auto recentList = _strip
		? _unifiedFactoryOwner->unifiedIdsList()
		: _recent;
	auto freeEffects = base::flat_set<DocumentId>();
	if (effects) {
		auto free = base::flat_set<Data::ReactionId>();
		free.reserve(_reactions.recent.size());
		for (const auto &reaction : _reactions.recent) {
			if (!reaction.premium) {
				free.emplace(reaction.id);
			}
		}
		for (const auto &id : recentList) {
			const auto reactionId = _strip
				? _unifiedFactoryOwner->lookupReactionId(id)
				: Data::ReactionId{ id };
			if (free.contains(reactionId)) {
				freeEffects.insert(id);
			}
		}
	}
	_list = lists->add(
		object_ptr<EmojiListWidget>(lists, EmojiListDescriptor{
			.show = _show,
			.mode = _listMode,
			.paused = _paused ? _paused : [] { return false; },
			.customRecentList = DocumentListToRecent(recentList),
			.customRecentFactory = _unifiedFactoryOwner->factory(),
			.freeEffects = std::move(freeEffects),
			.st = st,
		}));
	if (!_reactions.stickers.empty()) {
		auto descriptors = ranges::views::all(
			_reactions.stickers
		) | ranges::view::transform([](const Data::Reaction &reaction) {
			return ChatHelpers::StickerCustomRecentDescriptor{
				reaction.selectAnimation,
				reaction.title
			};
		}) | ranges::to_vector;
		_stickers = lists->add(
			object_ptr<StickersListWidget>(
				lists,
				StickersListDescriptor{
					.show = _show,
					.mode = StickersListMode::MessageEffects,
					.paused = _paused ? _paused : [] { return false; },
					.customRecentList = std::move(descriptors),
					.st = st,
				}));
	}

	_list->escapes() | rpl::start_to_stream(_escapes, _list->lifetime());

	rpl::merge(
		_list->customChosen(),
		(_stickers
			? _stickers->chosen()
			: rpl::never<ChatHelpers::FileChosen>())
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		_chosen.fire({
			.id = _unifiedFactoryOwner->lookupReactionId(data.document->id),
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
	lists->move(0, 0);
	lists->resizeToWidth(geometry.width());
	_list->refreshEmoji();
	lists->show();

	const auto updateVisibleTopBottom = [=] {
		const auto scrollTop = _scroll->scrollTop();
		const auto scrollBottom = scrollTop + _scroll->height();
		lists->setVisibleTopBottom(scrollTop, scrollBottom);
	};
	_scroll->scrollTopChanges(
	) | rpl::start_with_next(updateVisibleTopBottom, lists->lifetime());

	_list->scrollToRequests(
	) | rpl::start_with_next([=](int y) {
		_scroll->scrollToY(y);
		if (_shadow) {
			_shadow->update();
		}
	}, _list->lifetime());

	_scroll->setGeometry(inner.marginsRemoved({
		_st.margin.left(),
		_footer ? _footer->height() : 0,
		0,
		0,
	}));
	if (_stickers) {
		_list->setMinimalHeight(geometry.width(), 0);
		_stickers->setMinimalHeight(geometry.width(), 0);

		_list->searchQueries(
		) | rpl::start_with_next([=](std::vector<QString> &&query) {
			_stickers->applySearchQuery(std::move(query));
		}, _stickers->lifetime());

		rpl::combine(
			_list->heightValue(),
			_stickers->heightValue()
		) | rpl::start_with_next([=] {
			InvokeQueued(lists, updateVisibleTopBottom);
		}, _stickers->lifetime());

		rpl::combine(
			_list->recentShownCount(),
			_stickers->recentShownCount()
		) | rpl::start_with_next([=](int emoji, int stickers) {
			_showEmptySearch = !emoji && !stickers;
			_scroll->update();
		}, _scroll->lifetime());

		_scroll->paintRequest() | rpl::filter([=] {
			return _showEmptySearch;
		}) | rpl::start_with_next([=] {
			auto p = QPainter(_scroll);
			p.setPen(st::windowSubTextFg);
			p.setFont(st::normalFont);
			p.drawText(
				_scroll->rect(),
				tr::lng_effect_none(tr::now),
				style::al_center);
		}, _scroll->lifetime());
	} else {
		_list->setMinimalHeight(geometry.width(), _scroll->height());
	}

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
	const auto categoriesAboutTop = selector->useTransparency()
		? selector->extendTopForCategoriesAndAbout(width)
		: selector->opaqueExtendTopAbout(width);
	menu->setForceWidth(width - added);
	const auto height = menu->height();
	const auto fullTop = margins.top() + categoriesAboutTop + extend.top();
	const auto minimalHeight = std::max(
		margins.top() + selector->minimalHeight(width) + margins.bottom(),
		selector->effectPreviewHeight());
	const auto willBeHeightWithoutBottomPadding = fullTop
		+ height
		- menu->st().shadow.extend.top();
	const auto additionalPaddingBottom
		= (willBeHeightWithoutBottomPadding >= minimalHeight
			? 0
			: (minimalHeight - willBeHeightWithoutBottomPadding));
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
	const auto expandDown = (origin == Ui::PanelAnimation::Origin::TopLeft)
		|| (origin == Ui::PanelAnimation::Origin::TopRight);
	if (!useTransparency) {
		const auto expandBy = additionalPaddingBottom;
		selector->setOpaqueHeightExpand(expandBy, [=](int bottom) {
			const auto add = bottom - menu->height();
			if (add > 0) {
				const auto updated = menu->geometry().marginsAdded({
					0, expandDown ? 0 : add, 0, expandDown ? add : 0 });
				menu->setFixedSize(updated.size());
				menu->setGeometry(updated);
			}
		});
		menu->setAdditionalMenuPadding(QMargins(
			margins.left() + extend.left(),
			fullTop,
			margins.right() + extend.right(),
			0
		), QMargins(
			margins.left(),
			margins.top(),
			margins.right(),
			0
		));
		return menu->prepareGeometryFor(desiredPosition);
	} else if (!additionalPaddingBottom || expandDown) {
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

#if 0 // not ready
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
#endif

AttachSelectorResult AttachSelectorToMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		QPoint desiredPosition,
		not_null<HistoryItem*> item,
		Fn<void(ChosenReaction)> chosen,
		TextWithEntities about,
		IconFactory iconFactory) {
	const auto result = AttachSelectorToMenu(
		menu,
		desiredPosition,
		st::reactPanelEmojiPan,
		controller->uiShow(),
		Data::LookupPossibleReactions(item, true),
		std::move(about),
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

	selector->escapes() | rpl::start_with_next([=] {
		menu->hideMenu();
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
	TextWithEntities about,
	IconFactory iconFactory,
	Fn<bool()> paused)
-> base::expected<not_null<Selector*>, AttachSelectorResult> {
	if (reactions.recent.empty()) {
		return base::make_unexpected(AttachSelectorResult::Skipped);
	}
	const auto withSearch = reactions.customAllowed;
	const auto selector = Ui::CreateChild<Selector>(
		menu.get(),
		st,
		std::move(show),
		std::move(reactions),
		std::move(about),
		[=](bool fast) { menu->hideMenu(fast); },
		std::move(iconFactory),
		std::move(paused),
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

TextWithEntities ItemReactionsAbout(not_null<HistoryItem*> item) {
	return !item->reactionsAreTags()
		? TextWithEntities()
		: item->history()->session().premium()
		? TextWithEntities{ tr::lng_add_tag_about(tr::now) }
		: tr::lng_subscribe_tag_about(
			tr::now,
			lt_link,
			Ui::Text::Link(
				tr::lng_subscribe_tag_link(tr::now),
				u"internal:about_tags"_q),
			Ui::Text::WithEntities);
}

} // namespace HistoryView::Reactions
