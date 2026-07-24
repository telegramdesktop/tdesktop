/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/reactions_settings_box.h"

#include "chat_helpers/emoji_list_widget.h"
#include "chat_helpers/tabbed_selector.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/view/reactions/history_view_reactions_strip.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/animations.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/effects/round_area_with_shadow.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/animated_icon.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace {

constexpr auto kCenterSizeMultiplier = 1.6;
constexpr auto kMaxLandingWait = 3 * crl::time(1000);

using Strip = HistoryView::Reactions::Strip;

[[nodiscard]] int PlainIconSize() {
	return Data::FrameSizeFromTag(Data::CustomEmojiSizeTag::Large)
		/ style::DevicePixelRatio();
}

[[nodiscard]] int CenterIconSize() {
	const auto rough = int(base::SafeRound(
		st::reactStripImage * kCenterSizeMultiplier));
	return rough + (((rough - PlainIconSize()) % 2) ? 1 : 0);
}

[[nodiscard]] float64 PlainIconScale() {
	return PlainIconSize() / float64(CenterIconSize());
}

[[nodiscard]] float64 CenterIconMultiplier() {
	return CenterIconSize() / float64(st::reactStripImage);
}

class StripPreview final : public Ui::RpWidget {
public:
	explicit StripPreview(QWidget *parent);

	void setList(std::vector<Data::Reaction> list, int selectedCount);
	void setHiddenIndex(int index);
	[[nodiscard]] int slotCount() const;
	[[nodiscard]] QRect slotGeometry(int index) const;
	[[nodiscard]] QRect plainIconGeometry(int index) const;
	[[nodiscard]] rpl::producer<int> clicks() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] static int MaxSlotCount();
	[[nodiscard]] QRect islandRect() const;
	[[nodiscard]] QRect innerRect() const;
	[[nodiscard]] int indexAt(QPoint position) const;
	void validateIslandCache();
	void validatePillCache();

	const style::EmojiPan &_st;
	Ui::RoundAreaWithShadow _cachedRound;
	Strip _strip;
	std::unique_ptr<Ui::ChatTheme> _theme;
	std::vector<Data::Reaction> _list;
	base::flat_set<not_null<DocumentData*>> _centered;
	QImage _islandCache;
	QImage _pillCache;
	int _selectedCount = 0;
	int _columns = 1;
	int _hiddenIndex = -1;
	int _pressed = -1;
	rpl::event_stream<int> _clicks;

};

StripPreview::StripPreview(QWidget *parent)
: Ui::RpWidget(parent)
, _st(st::reactPanelEmojiPan)
, _cachedRound(
	QSize(2 * st::reactStripSkip + st::reactStripSize, st::reactStripHeight),
	st::reactionCornerShadow,
	st::reactStripHeight)
, _strip(
	_st,
	QRect(0, 0, st::reactStripSize, st::reactStripSize),
	CenterIconSize(),
	crl::guard(this, [=] { update(); }),
	[=](not_null<Data::DocumentMedia*> media, int) {
		return HistoryView::Reactions::DefaultIconFactory(
			media,
			(_centered.contains(media->owner())
				? CenterIconSize()
				: PlainIconSize()));
	})
, _theme(Window::Theme::DefaultChatThemeOn(lifetime())) {
	setMouseTracking(true);

	style::PaletteChanged(
	) | rpl::on_next([=] {
		_pillCache = QImage();
		_islandCache = QImage();
		update();
	}, lifetime());

	_theme->repaintBackgroundRequests(
	) | rpl::on_next([=] {
		_islandCache = QImage();
		update();
	}, lifetime());
}

int StripPreview::MaxSlotCount() {
	const auto max = st::menuWithIcons.widthMax
		+ st::reactStripExtend.left()
		+ st::reactStripExtend.right();
	return ((max - 2 * st::reactStripSkip) / st::reactStripSize) - 1;
}

void StripPreview::setList(
		std::vector<Data::Reaction> list,
		int selectedCount) {
	_list = std::move(list);
	_selectedCount = selectedCount;
	auto pointers = std::vector<not_null<const Data::Reaction*>>();
	pointers.reserve(_list.size());
	_centered.clear();
	for (auto &reaction : _list) {
		const auto center = reaction.centerIcon;
		if (center && center != reaction.selectAnimation) {
			reaction.appearAnimation = center;
			reaction.selectAnimation = center;
			_centered.emplace(center);
		} else {
			reaction.centerIcon = nullptr;
		}
		pointers.push_back(&reaction);
	}
	_strip.applyList(pointers, Strip::AddedButton::None);
	update();
}

void StripPreview::setHiddenIndex(int index) {
	if (_hiddenIndex != index) {
		_hiddenIndex = index;
		update();
	}
}

int StripPreview::slotCount() const {
	return _columns;
}

QRect StripPreview::islandRect() const {
	const auto &margin = st::localStorageIslandMargin;
	return QRect(
		margin.left(),
		margin.top(),
		width() - margin.left() - margin.right(),
		height() - margin.top() - margin.bottom());
}

QRect StripPreview::innerRect() const {
	const auto island = islandRect();
	const auto innerWidth = 2 * st::reactStripSkip
		+ _columns * st::reactStripSize;
	return QRect(
		island.x() + (island.width() - innerWidth) / 2,
		island.y()
			+ st::settingsReactionsIslandPadding.top()
			+ st::reactionCornerShadow.top(),
		innerWidth,
		st::reactStripHeight);
}

QRect StripPreview::slotGeometry(int index) const {
	const auto inner = innerRect();
	return QRect(
		inner.x() + st::reactStripSkip + index * st::reactStripSize,
		inner.y() + (st::reactStripHeight - st::reactStripSize) / 2,
		st::reactStripSize,
		st::reactStripSize);
}

QRect StripPreview::plainIconGeometry(int index) const {
	const auto full = CenterIconSize();
	const auto size = PlainIconSize();
	const auto shift = (st::reactStripSize - full) / 2 + (full - size) / 2;
	return QRect(
		slotGeometry(index).topLeft() + QPoint(shift, shift),
		QSize(size, size));
}

rpl::producer<int> StripPreview::clicks() const {
	return _clicks.events();
}

int StripPreview::resizeGetHeight(int newWidth) {
	const auto &margin = st::localStorageIslandMargin;
	const auto &padding = st::settingsReactionsIslandPadding;
	const auto available = newWidth
		- margin.left()
		- margin.right()
		- padding.left()
		- padding.right()
		- 2 * st::reactStripSkip;
	_columns = std::max(
		std::min(available / st::reactStripSize, MaxSlotCount()),
		1);
	return margin.top()
		+ padding.top()
		+ st::reactionCornerShadow.top()
		+ st::reactStripHeight
		+ st::reactionCornerShadow.bottom()
		+ _st.icons.stripBubble.height()
		+ padding.bottom()
		+ margin.bottom();
}

int StripPreview::indexAt(QPoint position) const {
	const auto inner = innerRect();
	const auto count = std::min(int(_list.size()), _columns);
	const auto left = inner.x() + st::reactStripSkip;
	const auto rect = QRect(
		left,
		inner.y(),
		count * st::reactStripSize,
		inner.height());
	return rect.contains(position)
		? ((position.x() - left) / st::reactStripSize)
		: -1;
}

void StripPreview::validatePillCache() {
	const auto inner = innerRect();
	const auto outer = inner.marginsAdded(st::reactionCornerShadow);
	const auto ratio = style::DevicePixelRatio();
	if (_pillCache.size() == outer.size() * ratio) {
		return;
	}
	_pillCache = _cachedRound.PrepareImage(outer.size());
	_pillCache.fill(Qt::transparent);
	auto q = QPainter(&_pillCache);
	_cachedRound.setBackgroundColor(_st.bg->c);
	_cachedRound.setShadowColor(st::shadowFg->c);
	const auto radius = inner.height() / 2.;
	const auto frame = _cachedRound.validateFrame(0, 1., radius);
	const auto fill = _cachedRound.FillWithImage(
		q,
		QRect(QPoint(), outer.size()),
		frame);
	if (!fill.isEmpty()) {
		q.fillRect(fill, _st.bg);
	}
}

void StripPreview::validateIslandCache() {
	const auto island = islandRect();
	const auto ratio = style::DevicePixelRatio();
	if (_islandCache.size() == island.size() * ratio) {
		return;
	}
	auto background = QImage(
		island.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	background.setDevicePixelRatio(ratio);
	{
		auto q = QPainter(&background);
		Window::SectionWidget::PaintBackground(
			q,
			_theme.get(),
			QSize(island.width(), window()->height()),
			QRect(QPoint(), island.size()));
	}
	_islandCache = QImage(
		island.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_islandCache.setDevicePixelRatio(ratio);
	_islandCache.fill(Qt::transparent);
	auto q = QPainter(&_islandCache);
	auto hq = PainterHighQualityEnabler(q);
	q.setPen(Qt::NoPen);
	q.setBrush(QBrush(background));
	q.drawRoundedRect(
		QRect(QPoint(), island.size()),
		st::localStorageIslandRadius,
		st::localStorageIslandRadius);
}

void StripPreview::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), st::windowBgOver);

	const auto island = islandRect();
	validateIslandCache();
	p.drawImage(island.topLeft(), _islandCache);

	validatePillCache();
	const auto inner = innerRect();
	const auto outer = inner.marginsAdded(st::reactionCornerShadow);
	p.drawImage(outer.topLeft(), _pillCache);

	const auto &bubble = _st.icons.stripBubble;
	const auto bubbleRight = std::min(
		st::reactStripBubbleRight,
		(inner.width() - bubble.width()) / 2);
	bubble.paint(
		p,
		inner.x() + inner.width() - bubbleRight - bubble.width(),
		inner.y() + inner.height(),
		width());

	auto hq = PainterHighQualityEnabler(p);
	const auto count = std::min(int(_list.size()), _columns);
	auto position = QPoint(
		inner.x() + st::reactStripSkip,
		inner.y() + (st::reactStripHeight - st::reactStripSize) / 2);
	for (auto i = 0; i != count; ++i) {
		if (i != _hiddenIndex) {
			p.setOpacity((i < _selectedCount)
				? 1.
				: st::settingsReactionsFillerOpacity);
			_strip.paintOne(
				p,
				i,
				position,
				(_list[i].centerIcon ? 1. : PlainIconScale()));
		}
		position += QPoint(st::reactStripSize, 0);
	}
	p.setOpacity(1.);
}

void StripPreview::mouseMoveEvent(QMouseEvent *e) {
	const auto index = indexAt(e->pos());
	_strip.setSelected(index);
	setCursor((index >= 0) ? style::cur_pointer : style::cur_default);
}

void StripPreview::mousePressEvent(QMouseEvent *e) {
	_pressed = indexAt(e->pos());
}

void StripPreview::mouseReleaseEvent(QMouseEvent *e) {
	const auto index = indexAt(e->pos());
	if (index >= 0 && index == _pressed) {
		_clicks.fire_copy(index);
	}
	_pressed = -1;
}

void StripPreview::leaveEventHook(QEvent *e) {
	_strip.setSelected(-1);
}

not_null<Ui::RpWidget*> AddReactionIconWrap(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QPoint> iconPositionValue,
		int iconSize,
		Fn<void(not_null<QWidget*>, QPainter&)> paintCallback,
		rpl::producer<> &&destroys,
		not_null<rpl::lifetime*> stateLifetime) {
	struct State {
		base::unique_qptr<Ui::RpWidget> widget;
		Ui::Animations::Simple finalAnimation;
	};

	const auto state = stateLifetime->make_state<State>();
	state->widget = base::make_unique_q<Ui::RpWidget>(parent);

	const auto widget = state->widget.get();
	widget->resize(iconSize, iconSize);
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);

	std::move(
		iconPositionValue
	) | rpl::on_next([=](const QPoint &point) {
		widget->moveToLeft(point.x(), point.y());
	}, widget->lifetime());

	const auto update = crl::guard(widget, [=] { widget->update(); });

	widget->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(widget);

		if (state->finalAnimation.animating()) {
			const auto progress = 1. - state->finalAnimation.value(0.);
			const auto size = widget->size();
			const auto scaledSize = size * progress;
			const auto scaledCenter = QPoint(
				(size.width() - scaledSize.width()) / 2.,
				(size.height() - scaledSize.height()) / 2.);
			p.setOpacity(progress);
			p.translate(scaledCenter);
			p.scale(progress, progress);
		}

		paintCallback(widget, p);
	}, widget->lifetime());

	std::move(
		destroys
	) | rpl::take(1) | rpl::on_next([=, from = 0., to = 1.] {
		state->finalAnimation.start(
			[=](float64 value) {
				update();
				if (value == to) {
					stateLifetime->destroy();
				}
			},
			from,
			to,
			st::defaultPopupMenu.showDuration);
	}, widget->lifetime());

	widget->raise();
	widget->show();

	return widget;
}

} // namespace

void AddReactionAnimatedIcon(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QPoint> iconPositionValue,
		int iconSize,
		const Data::Reaction &reaction,
		rpl::producer<> &&selects,
		rpl::producer<> &&destroys,
		not_null<rpl::lifetime*> stateLifetime) {
	struct State {
		struct Entry {
			std::shared_ptr<Data::DocumentMedia> media;
			std::shared_ptr<Ui::AnimatedIcon> icon;
		};
		Entry appear;
		Entry select;
		bool appearAnimated = false;
		rpl::lifetime loadingLifetime;
	};
	const auto state = stateLifetime->make_state<State>();

	state->appear.media = reaction.appearAnimation->createMediaView();
	state->select.media = reaction.selectAnimation->createMediaView();
	state->appear.media->checkStickerLarge();
	state->select.media->checkStickerLarge();
	rpl::single() | rpl::then(
		reaction.appearAnimation->session().downloaderTaskFinished()
	) | rpl::on_next([=] {
		const auto check = [&](State::Entry &entry) {
			if (!entry.media) {
				return true;
			} else if (!entry.media->loaded()) {
				return false;
			}
			entry.icon = HistoryView::Reactions::DefaultIconFactory(
				entry.media.get(),
				iconSize);
			entry.media = nullptr;
			return true;
		};
		if (check(state->select) && check(state->appear)) {
			state->select.icon->setCustomEndFrame(1);
			state->select.icon->animate([] {});
			state->loadingLifetime.destroy();
		}
	}, state->loadingLifetime);

	const auto paintCallback = [=](not_null<QWidget*> widget, QPainter &p) {
		const auto paintFrame = [&](not_null<Ui::AnimatedIcon*> animation) {
			const auto frame = animation->frame(st::windowFg->c);
			p.drawImage(
				QRect(
					(widget->width() - iconSize) / 2,
					(widget->height() - iconSize) / 2,
					iconSize,
					iconSize),
				frame);
		};

		const auto appear = state->appear.icon.get();
		if (appear && !state->appearAnimated) {
			state->appearAnimated = true;
			appear->animate(crl::guard(widget, [=] { widget->update(); }));
		}
		if (appear && appear->animating()) {
			paintFrame(appear);
			if (appear->frameIndex() == appear->framesCount() - 1) {
				if (const auto select = state->select.icon.get()) {
					select->setCustomEndFrame(select->framesCount() - 1);
				}
			}
		} else if (const auto select = state->select.icon.get()) {
			paintFrame(select);
		}
	};
	const auto widget = AddReactionIconWrap(
		parent,
		std::move(iconPositionValue),
		iconSize,
		paintCallback,
		std::move(destroys),
		stateLifetime);

	std::move(
		selects
	) | rpl::on_next([=] {
		const auto select = state->select.icon.get();
		if (select && !select->animating()) {
			select->animate(crl::guard(widget, [=] { widget->update(); }));
		}
	}, widget->lifetime());
}

void AddReactionCustomIcon(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QPoint> iconPositionValue,
		int iconSize,
		not_null<Window::SessionController*> controller,
		DocumentId customId,
		rpl::producer<> &&destroys,
		not_null<rpl::lifetime*> stateLifetime) {
	struct State {
		std::unique_ptr<Ui::Text::CustomEmoji> custom;
		Fn<void()> repaint;
	};
	const auto state = stateLifetime->make_state<State>();
	static constexpr auto tag = Data::CustomEmojiManager::SizeTag::Normal;
	state->custom = controller->session().data().customEmojiManager().create(
		customId,
		[=] { state->repaint(); },
		tag);

	const auto paintCallback = [=](not_null<QWidget*> widget, QPainter &p) {
		const auto ratio = style::DevicePixelRatio();
		const auto size = Data::FrameSizeFromTag(tag) / ratio;
		state->custom->paint(p, {
			.textColor = st::windowFg->c,
			.now = crl::now(),
			.position = QPoint(
				(widget->width() - size) / 2,
				(widget->height() - size) / 2),
			.paused = controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer),
		});
	};
	const auto widget = AddReactionIconWrap(
		parent,
		std::move(iconPositionValue),
		iconSize,
		paintCallback,
		std::move(destroys),
		stateLifetime);
	state->repaint = crl::guard(widget, [=] { widget->update(); });
}

void ReactionsSettingsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {

	struct State {
		std::vector<Data::ReactionId> extras;
		std::vector<Data::ReactionId> composedIds;
		int composedSelected = 0;
		base::flat_map<DocumentId, Data::ReactionId> docToReaction;
		base::flat_map<Data::ReactionId, DocumentId> reactionToDoc;
		StripPreview *preview = nullptr;
		std::unique_ptr<Ui::ReactionFlyAnimation> fly;
		Ui::RpWidget *flyLayer = nullptr;
		QRect flyArea;
		QRect flyRepaintArea;
		crl::time flyFinishedAt = 0;
		int flyIndex = -1;
		bool flyCustom = false;
		bool flyLanded = false;
		Fn<void()> refresh;
	};

	const auto session = &controller->session();
	const auto reactions = &session->data().reactions();
	const auto premium = session->premium();
	const auto state = box->lifetime().make_state<State>();
	state->extras = session->settings().extraFavoriteReactions();

	const auto pinnedToTop = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));
	pinnedToTop->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		QPainter(pinnedToTop).fillRect(clip, st::windowBgOver);
	}, pinnedToTop->lifetime());
	state->preview = pinnedToTop->add(object_ptr<StripPreview>(pinnedToTop));
	pinnedToTop->add(
		object_ptr<Ui::FlatLabel>(
			pinnedToTop,
			tr::lng_settings_chat_reactions_subtitle(),
			st::settingsReactionsAboutLabel),
		st::settingsReactionsAboutPadding,
		style::al_top);

	box->setStyle(st::reactionsSettingsBox);
	box->setTitle(tr::lng_settings_chat_reactions_title());
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });

	for (const auto &r : reactions->list(Data::Reactions::Type::Active)) {
		const auto docId = r.selectAnimation->id;
		state->docToReaction.emplace(docId, r.id);
		state->reactionToDoc.emplace(r.id, docId);
		reactions->preloadAnimationsFor(r.id);
	}

	const auto allowed = [=](const Data::ReactionId &id) {
		return id && !id.paid() && (premium || !id.custom());
	};
	const auto selectedIds = [=] {
		auto result = std::vector<Data::ReactionId>();
		const auto favorite = reactions->favoriteId();
		if (allowed(favorite)) {
			result.push_back(favorite);
		}
		for (const auto &id : state->extras) {
			if (allowed(id) && !ranges::contains(result, id)) {
				result.push_back(id);
			}
		}
		return result;
	};
	const auto resolve = [=](
			std::vector<Data::Reaction> &to,
			const Data::ReactionId &id) {
		if (ranges::contains(to, id, &Data::Reaction::id)) {
			return false;
		}
		const auto &active = reactions->list(Data::Reactions::Type::Active);
		const auto i = ranges::find(active, id, &Data::Reaction::id);
		if (i != end(active)) {
			to.push_back(*i);
			return true;
		} else if (id.custom()) {
			if (const auto temp = reactions->lookupTemporary(id)) {
				to.push_back(*temp);
				return true;
			}
		}
		return false;
	};

	const auto container = pinnedToTop;
	using TabbedSelector = ChatHelpers::TabbedSelector;
	const auto island = container->add(
		object_ptr<Ui::VerticalLayout>(container),
		st::localStorageIslandMargin);
	island->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(island);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxBg);
		p.drawRoundedRect(
			island->rect(),
			st::localStorageIslandRadius,
			st::localStorageIslandRadius);
	}, island->lifetime());
	const auto selector = island->add(
		object_ptr<TabbedSelector>(
			island,
			controller->uiShow(),
			Window::GifPauseReason::Layer,
			(premium
				? TabbedSelector::Mode::FullReactions
				: TabbedSelector::Mode::RecentReactions)),
		QMargins(
			st::boxRadius,
			st::boxRadius,
			st::boxRadius,
			st::boxRadius));
	if (premium) {
		selector->setAllowEmojiWithoutPremium(false);
	}
	selector->setRoundRadius(0);
	selector->resize(
		st::boxWideWidth
			- st::localStorageIslandMargin.left()
			- st::localStorageIslandMargin.right()
			- 2 * st::boxRadius,
		st::emojiPanMinHeight);
	Ui::AddSkip(container, st::localStorageIslandMargin.top());

	rpl::combine(
		box->getDelegate()->contentHeightMaxValue(),
		state->preview->heightValue()
	) | rpl::on_next([=](int contentHeight, int) {
		const auto outside = container->height() - selector->height();
		selector->resize(
			selector->width(),
			std::clamp(
				contentHeight - outside,
				st::emojiPanMinHeight / 2,
				st::emojiPanMinHeight));
	}, box->lifetime());

	{
		auto recentIds = std::vector<DocumentId>();
		const auto pushDoc = [&](const Data::ReactionId &id) {
			const auto customId = id.custom();
			const auto docId = customId ? customId : [&] {
				const auto i = state->reactionToDoc.find(id);
				return (i != end(state->reactionToDoc))
					? i->second
					: DocumentId();
			}();
			if (docId && !ranges::contains(recentIds, docId)) {
				recentIds.push_back(docId);
			}
		};
		for (const auto &id : selectedIds()) {
			pushDoc(id);
		}
		for (const auto &r : reactions->list(Data::Reactions::Type::Active)) {
			if (allowed(r.id)) {
				pushDoc(r.id);
			}
		}
		selector->provideRecentEmoji(
			ChatHelpers::DocumentListToRecent(recentIds));
	}

	state->refresh = [=] {
		const auto selected = selectedIds();
		auto list = std::vector<Data::Reaction>();
		auto count = 0;
		for (const auto &id : selected) {
			if (resolve(list, id)) {
				++count;
			}
		}
		const auto slots = state->preview->slotCount();
		const auto &top = reactions->list(Data::Reactions::Type::Top);
		const auto &recent = reactions->list(Data::Reactions::Type::Recent);
		const auto &active = reactions->list(Data::Reactions::Type::Active);
		for (const auto &reaction
			: ranges::views::concat(top, recent, active)) {
			if (int(list.size()) >= slots) {
				break;
			} else if (allowed(reaction.id)) {
				resolve(list, reaction.id);
			}
		}
		state->composedSelected = count;
		state->composedIds = list
			| ranges::views::transform(&Data::Reaction::id)
			| ranges::to_vector;
		state->preview->setList(std::move(list), count);

		auto marked = base::flat_set<DocumentId>();
		for (const auto &id : selected) {
			if (const auto customId = id.custom()) {
				marked.emplace(customId);
			} else {
				const auto i = state->reactionToDoc.find(id);
				if (i != end(state->reactionToDoc)) {
					marked.emplace(i->second);
				}
			}
		}
		selector->setMarkedCustomIds(marked);
	};

	for (const auto &id : selectedIds()) {
		const auto customId = id.custom();
		if (!customId || reactions->lookupTemporary(id)) {
			continue;
		}
		session->data().customEmojiManager().resolve(
			customId
		) | rpl::on_next_error([=](not_null<DocumentData*>) {
			state->refresh();
		}, [] {}, box->lifetime());
	}

	const auto finishFly = [=] {
		state->fly = nullptr;
		state->flyArea = QRect();
		state->flyRepaintArea = QRect();
		state->flyFinishedAt = 0;
		state->flyLanded = false;
		state->flyIndex = -1;
		if (state->flyLayer) {
			state->flyLayer->hide();
		}
		state->preview->setHiddenIndex(-1);
	};
	const auto startFly = [=](
			Data::ReactionId id,
			Ui::MessageSendingAnimationFrom from,
			int index) {
		if (from.frame.isNull() || index < 0) {
			return;
		}
		if (!state->flyLayer) {
			const auto layer = Ui::CreateChild<Ui::RpWidget>(box.get());
			state->flyLayer = layer;
			layer->setAttribute(Qt::WA_TransparentForMouseEvents);
			box->sizeValue(
			) | rpl::on_next([=](QSize size) {
				layer->setGeometry(QRect(QPoint(), size));
			}, layer->lifetime());
			layer->paintRequest(
			) | rpl::on_next([=](QRect clip) {
				if (!state->fly) {
					return;
				}
				auto p = QPainter(layer);
				const auto preview = state->preview;
				const auto index = state->flyIndex;
				const auto target = state->flyCustom
					? Ui::MapFrom(
						layer,
						preview,
						preview->plainIconGeometry(index))
					: [&] {
						const auto slot = Ui::MapFrom(
							layer,
							preview,
							preview->slotGeometry(index));
						const auto size = st::reactStripImage;
						return QRect(
							slot.topLeft() + QPoint(
								(slot.width() - size) / 2,
								(slot.height() - size) / 2),
							QSize(size, size));
					}();
				const auto flying = state->fly->flying();
				const auto area = state->fly->paintGetArea(
					p,
					QPoint(),
					target,
					st::windowFg->c,
					clip,
					crl::now());
				state->flyRepaintArea = area.united(state->flyArea);
				state->flyArea = area;
				if (flying && !state->fly->flying()) {
					state->preview->setHiddenIndex(-1);
				}
				if (state->fly->finished() && !state->flyLanded) {
					const auto now = crl::now();
					if (!state->flyFinishedAt) {
						state->flyFinishedAt = now;
					}
					if (state->fly->centerInDefaultState()
						|| (now - state->flyFinishedAt
							>= kMaxLandingWait)) {
						state->flyLanded = true;
					} else {
						layer->update(state->flyRepaintArea);
					}
				}
				if (state->flyLanded) {
					crl::on_main(layer, [=] {
						if (state->flyLanded) {
							finishFly();
						}
					});
				}
			}, layer->lifetime());
		}
		state->flyIndex = index;
		state->flyCustom = (id.custom() != 0);
		state->preview->setHiddenIndex(index);
		state->flyLayer->raise();
		state->flyLayer->show();
		state->fly = std::make_unique<Ui::ReactionFlyAnimation>(
			reactions,
			Ui::ReactionFlyAnimationArgs{
				.id = id,
				.flyIcon = from.frame,
				.flyFrom = state->flyLayer->mapFromGlobal(
					from.globalStartGeometry),
				.flyUp = st::settingsReactionsFlyUp,
				.centerSizeMultiplier = CenterIconMultiplier(),
				.flyKeepSize = true,
			},
			[=] {
				const auto layer = state->flyLayer;
				layer->update(state->flyRepaintArea.isEmpty()
					? layer->rect()
					: state->flyRepaintArea);
			},
			(id.custom() ? PlainIconSize() : int(st::reactStripImage)),
			Data::CustomEmojiSizeTag::Large);
	};

	const auto toggle = [=](
			Data::ReactionId id,
			std::optional<Ui::MessageSendingAnimationFrom> from) {
		if (!allowed(id)) {
			return;
		}
		finishFly();
		const auto favorite = reactions->favoriteId();
		auto &extras = state->extras;
		const auto i = ranges::find(extras, id);
		if (id == favorite) {
			const auto j = ranges::find_if(extras, [&](
					const Data::ReactionId &extra) {
				return allowed(extra) && (extra != favorite);
			});
			if (j == end(extras)) {
				return;
			}
			const auto promoted = *j;
			extras.erase(j);
			extras.erase(ranges::remove(extras, id), end(extras));
			reactions->setFavorite(promoted);
		} else if (i != end(extras)) {
			extras.erase(i);
		} else if (favorite.empty()) {
			reactions->setFavorite(id);
		} else if (state->composedSelected < state->preview->slotCount()) {
			extras.push_back(id);
			if (from) {
				startFly(id, *from, state->composedSelected);
			}
		} else {
			return;
		}
		session->settings().setExtraFavoriteReactions(extras);
		session->saveSettingsDelayed();
		state->refresh();
	};

	selector->customEmojiChosen(
	) | rpl::on_next([=](ChatHelpers::FileChosen data) {
		const auto docId = data.document->id;
		const auto i = state->docToReaction.find(docId);
		const auto id = (i != end(state->docToReaction))
			? i->second
			: Data::ReactionId{ docId };
		toggle(id, data.messageSendingFrom);
	}, selector->lifetime());

	state->preview->clicks(
	) | rpl::on_next([=](int index) {
		if (index >= 0 && index < int(state->composedIds.size())) {
			toggle(state->composedIds[index], std::nullopt);
		}
	}, state->preview->lifetime());

	reactions->favoriteUpdates(
	) | rpl::on_next([=] {
		state->refresh();
	}, box->lifetime());

	state->preview->widthValue(
	) | rpl::filter(rpl::mappers::_1 > 0) | rpl::on_next([=] {
		state->refresh();
	}, box->lifetime());
}
