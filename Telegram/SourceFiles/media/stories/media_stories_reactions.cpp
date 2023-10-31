/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_reactions.h"

#include "base/event_filter.h"
#include "base/unixtime.h"
#include "boxes/premium_preview_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_message_reactions.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/view/media/history_view_custom_emoji.h"
#include "history/view/media/history_view_media_unwrapped.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "history/view/history_view_element.h"
#include "history/history_item_reply_markup.h"
#include "history/history_item.h"
#include "history/history.h"
#include "main/main_session.h"
#include "media/stories/media_stories_controller.h"
#include "lang/lang_tag.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/emoji_fly_animation.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/text/text_isolated_emoji.h"
#include "ui/widgets/popup_menu.h"
#include "ui/animated_icon.h"
#include "ui/painter.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"
#include "styles/style_media_view.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Media::Stories {
namespace {

constexpr auto kReactionScaleOutTarget = 0.7;
constexpr auto kReactionScaleOutDuration = crl::time(1000);
constexpr auto kMessageReactionScaleOutDuration = crl::time(400);
constexpr auto kSuggestedBubbleSize = 1.0;
constexpr auto kSuggestedTailBigSize = 0.264;
constexpr auto kSuggestedTailBigOffset = 0.464;
constexpr auto kSuggestedTailSmallSize = 0.110;
constexpr auto kSuggestedTailSmallOffset = 0.697;
constexpr auto kSuggestedTailBigRotation = -42.29;
constexpr auto kSuggestedTailSmallRotation = -40.87;
constexpr auto kSuggestedReactionSize = 0.7;
constexpr auto kSuggestedWithCountSize = 0.55;
constexpr auto kStoppingFadeDuration = crl::time(150);

class ReactionView final
	: public Ui::RpWidget
	, public SuggestedReactionView
	, public HistoryView::DefaultElementDelegate {
public:
	ReactionView(
		QWidget *parent,
		not_null<Main::Session*> session,
		const Data::SuggestedReaction &reaction);

	void setAreaGeometry(QRect geometry) override;
	void updateCount(int count) override;
	void playEffect() override;

private:
	using Element = HistoryView::Element;

	struct Stopping {
		std::unique_ptr<Ui::ReactionFlyAnimation> effect;
		Ui::Animations::Simple animation;
	};

	not_null<HistoryView::ElementDelegate*> delegate();
	HistoryView::Context elementContext() override;
	bool elementAnimationsPaused() override;
	bool elementShownUnread(not_null<const Element*> view) override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;

	void paintEvent(QPaintEvent *e) override;

	void setupCustomChatStylePalette();
	void cacheBackground();
	void paintEffectFrame(
		QPainter &p,
		not_null<Ui::ReactionFlyAnimation*> effect,
		crl::time now);
	void updateEffectGeometry();
	void createEffectCanvas();
	void stopEffect();

	Data::SuggestedReaction _data;
	std::unique_ptr<Ui::ChatStyle> _chatStyle;
	std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	AdminLog::OwnedItem _fake;
	QImage _background;
	QString _countShort;
	Ui::Text::String _counter;
	Ui::Animations::Simple _counterAnimation;
	QRectF _bubbleGeometry;
	int _size = 0;
	int _mediaLeft = 0;
	int _mediaTop = 0;
	int _mediaWidth = 0;
	int _mediaHeight = 0;
	float64 _bubble = 0;
	float64 _bigOffset = 0;
	float64 _bigSize = 0;
	float64 _smallOffset = 0;
	float64 _smallSize = 0;

	std::unique_ptr<Ui::RpWidget> _effectCanvas;
	std::unique_ptr<Ui::ReactionFlyAnimation> _effect;
	std::vector<Stopping> _effectStopping;
	QRect _effectTarget;

};

[[nodiscard]] AdminLog::OwnedItem GenerateFakeItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history) {
	Expects(history->peer->isUser());

	const auto flags = MessageFlag::FakeHistoryItem
		| MessageFlag::HasFromId;
	const auto replyTo = FullReplyTo();
	const auto viaBotId = UserId();
	const auto groupedId = uint64();
	const auto item = history->makeMessage(
		history->nextNonHistoryEntryId(),
		flags,
		replyTo,
		viaBotId,
		base::unixtime::now(),
		peerToUser(history->peer->id),
		QString(),
		TextWithEntities(),
		MTP_messageMediaEmpty(),
		HistoryMessageMarkupData(),
		groupedId);
	return AdminLog::OwnedItem(delegate, item);
}

ReactionView::ReactionView(
	QWidget *parent,
	not_null<Main::Session*> session,
	const Data::SuggestedReaction &reaction)
: RpWidget(parent)
, _data(reaction)
, _chatStyle(std::make_unique<Ui::ChatStyle>(session->colorIndicesValue()))
, _pathGradient(
	std::make_unique<Ui::PathShiftGradient>(
		st::shadowFg,
		st::shadowFg,
		[=] { update(); }))
, _fake(
	GenerateFakeItem(
		delegate(),
		session->data().history(PeerData::kServiceNotificationsId))) {
	style::PaletteChanged() | rpl::start_with_next([=] {
		_background = QImage();
	}, lifetime());

	const auto view = _fake.get();
	const auto entityData = [&] {
		const auto &id = _data.reaction;
		const auto reactions = &session->data().reactions();
		reactions->preloadAnimationsFor(id);
		if (const auto customId = id.custom()) {
			return Data::SerializeCustomEmojiId(customId);
		}
		const auto type = Data::Reactions::Type::All;
		const auto &list = reactions->list(type);
		const auto i = ranges::find(list, id, &Data::Reaction::id);
		return (i != end(list))
			? Data::SerializeCustomEmojiId(i->selectAnimation->id)
			: QString();
	}();

	const auto emoji = Ui::Text::OnlyCustomEmoji{
		{ { { entityData } } }
	};
	view->overrideMedia(std::make_unique<HistoryView::UnwrappedMedia>(
		view,
		std::make_unique<HistoryView::CustomEmoji>(view, emoji)));
	view->initDimensions();

	_mediaLeft = st::msgMargin.left();
	_mediaTop = st::msgMargin.top();
	_mediaWidth = _mediaHeight = view->resizeGetHeight(st::windowMinWidth)
		- _mediaTop
		- st::msgMargin.bottom();

	session->data().viewRepaintRequest(
	) | rpl::start_with_next([=](not_null<const Element*> element) {
		if (element == view) {
			update();
		}
	}, lifetime());

	_data.count = 0;
	updateCount(reaction.count);
	_counterAnimation.stop();

	setupCustomChatStylePalette();
	setAttribute(Qt::WA_TransparentForMouseEvents);
	show();
}

void ReactionView::setupCustomChatStylePalette() {
	const auto color = uchar(_data.dark ? 255 : 0);
	_chatStyle->historyTextInFg().set(color, color, color, 255);
	_chatStyle->applyCustomPalette(_chatStyle.get());
}

void ReactionView::setAreaGeometry(QRect geometry) {
	_size = std::min(geometry.width(), geometry.height());
	_bubble = _size * kSuggestedBubbleSize;
	_bigOffset = _bubble * kSuggestedTailBigOffset;
	_bigSize = _bubble * kSuggestedTailBigSize;
	_smallOffset = _bubble * kSuggestedTailSmallOffset;
	_smallSize = _bubble * kSuggestedTailSmallSize;
	const auto add = int(base::SafeRound(_smallOffset + _smallSize))
		- (_size / 2);
	setGeometry(geometry.marginsAdded({ add, add, add, add }));
	const auto sub = int(base::SafeRound(
		(1. - kSuggestedReactionSize) * _size / 2));
	_effectTarget = geometry.marginsRemoved({ sub, sub, sub, sub });
	updateEffectGeometry();
}

void ReactionView::updateCount(int count) {
	if (_data.count == count) {
		return;
	}
	_data.count = count;
	const auto countShort = count
		? Lang::FormatCountToShort(count).string
		: QString();
	if (_countShort == countShort) {
		return;
	}
	const auto was = !_countShort.isEmpty();
	_countShort = countShort;
	const auto now = !_countShort.isEmpty();

	if (!_countShort.isEmpty()) {
		_counter = { st::storiesLikeCountStyle, _countShort };
	}
	if (now != was) {
		_counterAnimation.start(
			[=] { update(); },
			was ? 1. : 0.,
			was ? 0. : 1.,
			st::fadeWrapDuration);
	}
	update();
}

void ReactionView::playEffect() {
	const auto exists = (_effectCanvas != nullptr);
	if (exists) {
		stopEffect();
	} else {
		createEffectCanvas();
	}
	const auto reactions = &_fake->history()->owner().reactions();
	const auto scaleDown = _bubbleGeometry.width() / float64(_mediaWidth);
	auto args = Ui::ReactionFlyAnimationArgs{
		.id = _data.reaction,
		.miniCopyMultiplier = std::min(1., scaleDown),
		.effectOnly = true,
	};
	_effect = std::make_unique<Ui::ReactionFlyAnimation>(
		reactions,
		std::move(args),
		[=] { _effectCanvas->update(); },
		_size / 2,
		Data::CustomEmojiSizeTag::Isolated);
	if (exists) {
		_effectStopping.back().animation.start([=] {
			_effectCanvas->update();
		}, 1., 0., kStoppingFadeDuration);
	}
}

void ReactionView::paintEffectFrame(
		QPainter &p,
		not_null<Ui::ReactionFlyAnimation*> effect,
		crl::time now) {
	effect->paintGetArea(
		p,
		QPoint(),
		_effectTarget.translated(-_effectCanvas->pos()),
		_data.dark ? Qt::white : Qt::black,
		QRect(),
		now);
}

void ReactionView::createEffectCanvas() {
	_effectCanvas = std::make_unique<Ui::RpWidget>(parentWidget());
	const auto raw = _effectCanvas.get();
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);
	raw->show();
	raw->paintRequest() | rpl::start_with_next([=] {
		if (!_effect || _effect->finished()) {
			crl::on_main(_effectCanvas.get(), [=] {
				_effect = nullptr;
				_effectStopping.clear();
				_effectCanvas = nullptr;
			});
			return;
		}
		const auto now = crl::now();
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		_effectStopping.erase(ranges::remove_if(_effectStopping, [&](
				const Stopping &stopping) {
			if (!stopping.animation.animating()
				|| stopping.effect->finished()) {
				return true;
			}
			p.setOpacity(stopping.animation.value(0.));
			paintEffectFrame(p, stopping.effect.get(), now);
			return false;
		}), end(_effectStopping));
		paintEffectFrame(p, _effect.get(), now);
	}, raw->lifetime());
	updateEffectGeometry();
}

void ReactionView::stopEffect() {
	_effectStopping.push_back({ .effect = std::move(_effect) });
	_effectStopping.back().animation.start([=] {
		_effectCanvas->update();
	}, 1., 0., kStoppingFadeDuration);
}

void ReactionView::updateEffectGeometry() {
	if (!_effectCanvas) {
		return;
	}
	const auto center = geometry().center();
	_effectCanvas->setGeometry(
		center.x() - _size,
		center.y() - _size,
		_size * 2,
		_size * 3);
}

not_null<HistoryView::ElementDelegate*> ReactionView::delegate() {
	return static_cast<HistoryView::ElementDelegate*>(this);
}

void ReactionView::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	if (!_size) {
		return;
	} else if (_background.size() != size() * style::DevicePixelRatio()) {
		cacheBackground();
	}
	p.drawImage(0, 0, _background);

	const auto counted = _counterAnimation.value(_countShort.isEmpty()
		? 0.
		: 1.);
	const auto scale = kSuggestedReactionSize
		+ (kSuggestedWithCountSize - kSuggestedReactionSize) * counted;
	const auto counterSkip = (kSuggestedReactionSize - scale) * _mediaHeight / 2;

	auto hq = PainterHighQualityEnabler(p);
	p.translate(_bubbleGeometry.center());
	p.scale(
		scale * _bubbleGeometry.width() / _mediaWidth,
		scale * _bubbleGeometry.height() / _mediaHeight);
	p.rotate(_data.area.rotation);
	p.translate(
		-(_mediaLeft + (_mediaWidth / 2)),
		-(_mediaTop + (_mediaHeight / 2) + counterSkip));

	auto context = Ui::ChatPaintContext{
		.st = _chatStyle.get(),
		.viewport = rect(),
		.clip = rect(),
		.now = crl::now(),
	};
	_fake->draw(p, context);

	if (counted > 0.) {
		p.setPen(_data.dark ? Qt::white : Qt::black);
		const auto countTop = _mediaTop + _mediaHeight;
		if (counted < 1.) {
			const auto center = QPoint(
				_mediaLeft + (_mediaWidth / 2),
				countTop + st::storiesLikeCountStyle.font->height / 2);
			p.translate(center);
			p.scale(counted, counted);
			p.translate(-center);
		}
		_counter.draw(p, _mediaLeft, countTop, _mediaWidth, style::al_top);
	}
}

void ReactionView::cacheBackground() {
	const auto ratio = style::DevicePixelRatio();
	_background = QImage(
		size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_background.setDevicePixelRatio(ratio);
	_background.fill(Qt::transparent);

	const auto paintShape = [&](QColor color) {
		auto p = QPainter(&_background);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(color);
		_bubbleGeometry = QRectF(
			(width() - _bubble) / 2.,
			(height() - _bubble) / 2.,
			_bubble,
			_bubble);
		p.drawEllipse(_bubbleGeometry);

		const auto center = QPointF(width() / 2., height() / 2.);
		p.translate(center);

		auto previous = 0.;
		const auto rotate = [&](float64 initial) {
			if (_data.flipped) {
				initial = 180 - initial;
			}
			auto rotation = _data.area.rotation - initial;
			while (rotation < 0) {
				rotation += 360;
			}
			while (rotation >= 360) {
				rotation -= 360;
			}
			const auto delta = rotation - previous;
			previous = rotation;
			p.rotate(delta);
		};
		const auto paintTailPart = [&](float64 offset, float64 size) {
			const auto part = QRectF(-size / 2., -size / 2., size, size);
			p.drawEllipse(part.translated(offset, 0));
		};
		rotate(kSuggestedTailBigRotation);
		paintTailPart(_bigOffset, _bigSize);
		rotate(kSuggestedTailSmallRotation);
		paintTailPart(_smallOffset, _smallSize);
	};
	const auto dark = QColor(0, 0, 0, 128);
	if (!_data.dark) {
		paintShape(dark);
		_background = Images::Blur(std::move(_background), true);
	}
	paintShape(_data.dark ? dark : QColor(255, 255, 255));
}

[[nodiscard]] Data::ReactionId HeartReactionId() {
	return { QString() + QChar(10084) };
}

[[nodiscard]] Data::PossibleItemReactionsRef LookupPossibleReactions(
		not_null<Main::Session*> session) {
	auto result = Data::PossibleItemReactionsRef();
	const auto reactions = &session->data().reactions();
	const auto &full = reactions->list(Data::Reactions::Type::Active);
	const auto &top = reactions->list(Data::Reactions::Type::Top);
	const auto &recent = reactions->list(Data::Reactions::Type::Recent);
	const auto premiumPossible = session->premiumPossible();
	auto added = base::flat_set<Data::ReactionId>();
	result.recent.reserve(full.size());
	for (const auto &reaction : ranges::views::concat(top, recent, full)) {
		if (premiumPossible || !reaction.id.custom()) {
			if (added.emplace(reaction.id).second) {
				result.recent.push_back(&reaction);
			}
		}
	}
	result.customAllowed = premiumPossible;
	const auto i = ranges::find(
		result.recent,
		reactions->favoriteId(),
		&Data::Reaction::id);
	if (i != end(result.recent) && i != begin(result.recent)) {
		std::rotate(begin(result.recent), i, i + 1);
	}
	return result;
}

HistoryView::Context ReactionView::elementContext() {
	return HistoryView::Context::ContactPreview;
}

bool ReactionView::elementAnimationsPaused() {
	return false;
}

bool ReactionView::elementShownUnread(
		not_null<const Element*> view) {
	return false;
}

auto ReactionView::elementPathShiftGradient()
-> not_null<Ui::PathShiftGradient*> {
	return _pathGradient.get();
}

} // namespace

class Reactions::Panel final {
public:
	explicit Panel(not_null<Controller*> controller);
	~Panel();

	[[nodiscard]] rpl::producer<bool> expandedValue() const {
		return _expanded.value();
	}
	[[nodiscard]] rpl::producer<bool> shownValue() const {
		return _shown.value();
	}

	[[nodiscard]] rpl::producer<Chosen> chosen() const;

	void show(Mode mode);
	void hide(Mode mode);
	void hideIfCollapsed(Mode mode);
	void collapse(Mode mode);

	void attachToReactionButton(not_null<Ui::RpWidget*> button);

private:
	struct Hiding;

	void create();
	void updateShowState();
	void fadeOutSelector();
	void startAnimation();

	const not_null<Controller*> _controller;

	std::unique_ptr<Ui::RpWidget> _parent;
	std::unique_ptr<HistoryView::Reactions::Selector> _selector;
	std::vector<std::unique_ptr<Hiding>> _hiding;
	rpl::event_stream<Chosen> _chosen;
	Ui::Animations::Simple _showing;
	rpl::variable<float64> _shownValue;
	rpl::variable<bool> _expanded;
	rpl::variable<Mode> _mode;
	rpl::variable<bool> _shown = false;

};

struct Reactions::Panel::Hiding {
	explicit Hiding(not_null<QWidget*> parent) : widget(parent) {
	}

	Ui::RpWidget widget;
	Ui::Animations::Simple animation;
	QImage frame;
};

Reactions::Panel::Panel(not_null<Controller*> controller)
: _controller(controller) {
}

Reactions::Panel::~Panel() = default;

auto Reactions::Panel::chosen() const -> rpl::producer<Chosen> {
	return _chosen.events();
}

void Reactions::Panel::show(Mode mode) {
	const auto was = _mode.current();
	if (_shown.current() && was == mode) {
		return;
	} else if (_shown.current()) {
		hide(was);
	}
	_mode = mode;
	create();
	if (!_selector) {
		return;
	}
	const auto duration = st::defaultPanelAnimation.heightDuration
		* st::defaultPopupMenu.showDuration;
	_shown = true;
	_showing.start([=] { updateShowState(); }, 0., 1., duration);
	updateShowState();
	_parent->show();
}

void Reactions::Panel::hide(Mode mode) {
	if (!_selector || _mode.current() != mode) {
		return;
	}
	_selector->beforeDestroy();
	if (!anim::Disabled()) {
		fadeOutSelector();
	}
	_shown = false;
	_expanded = false;
	_showing.stop();
	_selector = nullptr;
	_parent = nullptr;
}

void Reactions::Panel::hideIfCollapsed(Mode mode) {
	if (!_expanded.current() && _mode.current() == mode) {
		hide(mode);
	}
}

void Reactions::Panel::collapse(Mode mode) {
	if (_expanded.current() && _mode.current() == mode) {
		hide(mode);
		show(mode);
	}
}

void Reactions::Panel::attachToReactionButton(
		not_null<Ui::RpWidget*> button) {
	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu && !button->isHidden()) {
			show(Reactions::Mode::Reaction);
			return base::EventFilterResult::Cancel;
		} else if (e->type() == QEvent::Hide) {
			hide(Reactions::Mode::Reaction);
		}
		return base::EventFilterResult::Continue;
	});
}

void Reactions::Panel::create() {
	auto reactions = LookupPossibleReactions(
		&_controller->uiShow()->session());
	if (reactions.recent.empty() && !reactions.morePremiumAvailable) {
		return;
	}
	_parent = std::make_unique<Ui::RpWidget>(_controller->wrap().get());
	_parent->show();

	const auto mode = _mode.current();

	_parent->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			const auto event = static_cast<QMouseEvent*>(e.get());
			if (event->button() == Qt::LeftButton) {
				if (!_selector
					|| !_selector->geometry().contains(event->pos())) {
					if (mode == Mode::Message) {
						collapse(mode);
					} else {
						hide(mode);
					}
				}
			}
		}
	}, _parent->lifetime());

	_selector = std::make_unique<HistoryView::Reactions::Selector>(
		_parent.get(),
		st::storiesReactionsPan,
		_controller->uiShow(),
		std::move(reactions),
		_controller->cachedReactionIconFactory().createMethod(),
		[=](bool fast) { hide(mode); });

	_selector->chosen(
	) | rpl::start_with_next([=](
			HistoryView::Reactions::ChosenReaction reaction) {
		_chosen.fire({ .reaction = reaction, .mode = mode });
		hide(mode);
	}, _selector->lifetime());

	_selector->premiumPromoChosen() | rpl::start_with_next([=] {
		hide(mode);
		ShowPremiumPreviewBox(
			_controller->uiShow(),
			PremiumPreview::InfiniteReactions);
	}, _selector->lifetime());

	const auto desiredWidth = st::storiesReactionsWidth;
	const auto maxWidth = desiredWidth * 2;
	const auto width = _selector->countWidth(desiredWidth, maxWidth);
	const auto margins = _selector->marginsForShadow();
	const auto categoriesTop = _selector->extendTopForCategories();
	const auto full = margins.left() + width + margins.right();

	_shownValue = 0.;
	rpl::combine(
		_controller->layoutValue(),
		_shownValue.value()
	) | rpl::start_with_next([=](const Layout &layout, float64 shown) {
		const auto story = _controller->story();
		const auto viewsReactionsMode = story && story->peer()->isChannel();
		const auto width = margins.left()
			+ _selector->countAppearedWidth(shown)
			+ margins.right();
		const auto height = layout.reactions.height();
		const auto shift = (width / 2);
		const auto right = (mode == Mode::Message)
			? (layout.reactions.x() + layout.reactions.width() / 2 + shift)
			: viewsReactionsMode
			? (layout.content.x() + layout.content.width())
			: (layout.controlsBottomPosition.x()
				+ layout.controlsWidth
				- st::storiesLikeReactionsPosition.x());
		const auto top = (mode == Mode::Message)
			? layout.reactions.y()
			: (layout.controlsBottomPosition.y()
				- height
				- st::storiesLikeReactionsPosition.y());
		_parent->setGeometry(QRect((right - width), top, full, height));
		const auto innerTop = height
			- st::storiesReactionsBottomSkip
			- st::reactStripHeight;
		const auto maxAdded = innerTop - margins.top() - categoriesTop;
		const auto added = std::min(maxAdded, st::storiesReactionsAddedTop);
		_selector->setSpecialExpandTopSkip(added);
		_selector->initGeometry(innerTop);
	}, _selector->lifetime());

	_selector->willExpand(
	) | rpl::start_with_next([=] {
		_expanded = true;
	}, _selector->lifetime());

	_selector->escapes() | rpl::start_with_next([=] {
		if (mode == Mode::Message) {
			collapse(mode);
		} else {
			hide(mode);
		}
	}, _selector->lifetime());
}

void Reactions::Panel::fadeOutSelector() {
	const auto wrap = _controller->wrap().get();
	const auto geometry = Ui::MapFrom(
		wrap,
		_parent.get(),
		_selector->geometry());
	_hiding.push_back(std::make_unique<Hiding>(wrap));
	const auto raw = _hiding.back().get();
	raw->frame = Ui::GrabWidgetToImage(_selector.get());
	raw->widget.setGeometry(geometry);
	raw->widget.show();
	raw->widget.paintRequest(
	) | rpl::start_with_next([=] {
		if (const auto opacity = raw->animation.value(0.)) {
			auto p = QPainter(&raw->widget);
			p.setOpacity(opacity);
			p.drawImage(0, 0, raw->frame);
		}
	}, raw->widget.lifetime());
	Ui::PostponeCall(&raw->widget, [=] {
		raw->animation.start([=] {
			if (raw->animation.animating()) {
				raw->widget.update();
			} else {
				const auto i = ranges::find(
					_hiding,
					raw,
					&std::unique_ptr<Hiding>::get);
				if (i != end(_hiding)) {
					_hiding.erase(i);
				}
			}
		}, 1., 0., st::slideWrapDuration);
	});
}

void Reactions::Panel::updateShowState() {
	const auto progress = _showing.value(_shown.current() ? 1. : 0.);
	const auto opacity = 1.;
	const auto appearing = _showing.animating();
	const auto toggling = false;
	_shownValue = progress;
	_selector->updateShowState(progress, opacity, appearing, toggling);
}

Reactions::Reactions(not_null<Controller*> controller)
: _controller(controller)
, _panel(std::make_unique<Panel>(_controller)) {
	_panel->chosen() | rpl::start_with_next([=](Chosen &&chosen) {
		animateAndProcess(std::move(chosen));
	}, _lifetime);
}

Reactions::~Reactions() = default;

rpl::producer<bool> Reactions::activeValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_panel->expandedValue(),
		_panel->shownValue(),
		_1 || _2);
}

auto Reactions::chosen() const -> rpl::producer<Chosen> {
	return _chosen.events();
}

auto Reactions::makeSuggestedReactionWidget(
	const Data::SuggestedReaction &reaction)
-> std::unique_ptr<SuggestedReactionView> {
	return std::make_unique<ReactionView>(
		_controller->wrap(),
		&_controller->uiShow()->session(),
		reaction);
}

void Reactions::setReplyFieldState(
		rpl::producer<bool> focused,
		rpl::producer<bool> hasSendText) {
	std::move(
		focused
	) | rpl::start_with_next([=](bool focused) {
		_replyFocused = focused;
		if (!_replyFocused) {
			_panel->hideIfCollapsed(Reactions::Mode::Message);
		} else if (!_hasSendText) {
			_panel->show(Reactions::Mode::Message);
		}
	}, _lifetime);

	std::move(
		hasSendText
	) | rpl::start_with_next([=](bool has) {
		_hasSendText = has;
		if (_replyFocused) {
			if (_hasSendText) {
				_panel->hide(Reactions::Mode::Message);
			} else {
				_panel->show(Reactions::Mode::Message);
			}
		}
	}, _lifetime);
}

void Reactions::attachToReactionButton(not_null<Ui::RpWidget*> button) {
	_panel->attachToReactionButton(button);
}

void Reactions::setReactionIconWidget(Ui::RpWidget *widget) {
	if (_likeIconWidget != widget) {
		assignLikedId({});
		_likeIconWidget = widget;
		_reactionAnimation = nullptr;
	}
}

auto Reactions::attachToMenu(
	not_null<Ui::PopupMenu*> menu,
	QPoint desiredPosition)
-> AttachStripResult {
	using namespace HistoryView::Reactions;

	const auto story = _controller->story();
	if (!story || story->peer()->isSelf()) {
		return AttachStripResult::Skipped;
	}

	const auto show = _controller->uiShow();
	const auto result = AttachSelectorToMenu(
		menu,
		desiredPosition,
		st::storiesReactionsPan,
		show,
		LookupPossibleReactions(&show->session()),
		_controller->cachedReactionIconFactory().createMethod());
	if (!result) {
		return result.error();
	}
	const auto selector = *result;

	selector->chosen() | rpl::start_with_next([=](ChosenReaction reaction) {
		menu->hideMenu();
		animateAndProcess({ reaction, ReactionsMode::Reaction });
	}, selector->lifetime());

	return AttachSelectorResult::Attached;
}

Data::ReactionId Reactions::liked() const {
	return _liked.current();
}

rpl::producer<Data::ReactionId> Reactions::likedValue() const {
	return _liked.value();
}

void Reactions::showLikeFrom(Data::Story *story) {
	setLikedIdFrom(story);

	if (!story) {
		_likeFromLifetime.destroy();
		return;
	}
	_likeFromLifetime = story->session().changes().storyUpdates(
		story,
		Data::StoryUpdate::Flag::Reaction
	) | rpl::start_with_next([=](const Data::StoryUpdate &update) {
		setLikedIdFrom(update.story);
	});
}

void Reactions::hide() {
	_panel->hide(Reactions::Mode::Message);
	_panel->hide(Reactions::Mode::Reaction);
}

void Reactions::outsidePressed() {
	_panel->hide(Reactions::Mode::Reaction);
	_panel->collapse(Reactions::Mode::Message);
}

void Reactions::toggleLiked() {
	const auto liked = !_liked.current().empty();
	applyLike(liked ? Data::ReactionId() : HeartReactionId());
}

void Reactions::applyLike(Data::ReactionId id) {
	if (_liked.current() != id) {
		animateAndProcess({ { .id = id }, ReactionsMode::Reaction });
	}
}

void Reactions::ready() {
	if (const auto story = _controller->story()) {
		story->owner().reactions().preloadAnimationsFor(HeartReactionId());
	}
}

void Reactions::animateAndProcess(Chosen &&chosen) {
	const auto like = (chosen.mode == Mode::Reaction);
	const auto wrap = _controller->wrap();
	const auto target = like ? _likeIconWidget : wrap.get();
	const auto story = _controller->story();
	if (!story || !target) {
		return;
	}

	auto done = like
		? setLikedIdIconInit(&story->owner(), chosen.reaction.id)
		: Fn<void(Ui::ReactionFlyCenter)>();
	const auto scaleOutDuration = like
		? kReactionScaleOutDuration
		: kMessageReactionScaleOutDuration;
	const auto scaleOutTarget = like ? kReactionScaleOutTarget : 0.;

	if (!chosen.reaction.id.empty()) {
		startReactionAnimation({
			.id = chosen.reaction.id,
			.flyIcon = chosen.reaction.icon,
			.flyFrom = (chosen.reaction.globalGeometry.isEmpty()
				? QRect()
				: wrap->mapFromGlobal(chosen.reaction.globalGeometry)),
			.scaleOutDuration = scaleOutDuration,
			.scaleOutTarget = scaleOutTarget,
		}, target, std::move(done));
	}

	_chosen.fire(std::move(chosen));
}

void Reactions::assignLikedId(Data::ReactionId id) {
	invalidate_weak_ptrs(&_likeIconGuard);
	_likeIcon = nullptr;
	_liked = id;
}

Fn<void(Ui::ReactionFlyCenter)> Reactions::setLikedIdIconInit(
		not_null<Data::Session*> owner,
		Data::ReactionId id,
		bool force) {
	if (_liked.current() != id) {
		_likeIconMedia = nullptr;
	} else if (!force) {
		return nullptr;
	}
	assignLikedId(id);
	if (id.empty() || !_likeIconWidget) {
		return nullptr;
	}
	return crl::guard(&_likeIconGuard, [=](Ui::ReactionFlyCenter center) {
		if (!id.custom() && !center.icon && !_likeIconMedia) {
			waitForLikeIcon(owner, id);
		} else {
			initLikeIcon(owner, id, std::move(center));
		}
	});
}

void Reactions::initLikeIcon(
		not_null<Data::Session*> owner,
		Data::ReactionId id,
		Ui::ReactionFlyCenter center) {
	Expects(_likeIconWidget != nullptr);

	_likeIcon = std::make_unique<Ui::RpWidget>(_likeIconWidget);
	const auto icon = _likeIcon.get();
	icon->show();
	_likeIconWidget->sizeValue() | rpl::start_with_next([=](QSize size) {
		icon->setGeometry(QRect(QPoint(), size));
	}, icon->lifetime());

	if (!id.custom() && !center.icon) {
		return;
	}

	struct State {
		Ui::ReactionFlyCenter center;
		QImage cache;
	};
	const auto fly = icon->lifetime().make_state<State>(State{
		.center = std::move(center),
	});
	if (const auto customId = id.custom()) {
		auto withCorrectCallback = owner->customEmojiManager().create(
			customId,
			[=] { icon->update(); },
			Data::CustomEmojiSizeTag::Isolated);
		[[maybe_unused]] const auto load = withCorrectCallback->ready();
		fly->center.custom = std::move(withCorrectCallback);
		fly->center.icon = nullptr;
	} else {
		fly->center.icon->jumpToStart(nullptr);
		fly->center.custom = nullptr;
	}
	const auto paintNonCached = [=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);

		const auto size = fly->center.size;
		const auto target = QRect(
			(icon->width() - size) / 2,
			(icon->height() - size) / 2,
			size,
			size);
		const auto scale = fly->center.scale;
		if (scale < 1.) {
			const auto shift = QRectF(target).center();
			p.translate(shift);
			p.scale(scale, scale);
			p.translate(-shift);
		}
		const auto multiplier = fly->center.centerSizeMultiplier;
		const auto inner = int(base::SafeRound(size * multiplier));
		if (const auto icon = fly->center.icon.get()) {
			const auto rect = QRect(
				target.x() + (target.width() - inner) / 2,
				target.y() + (target.height() - inner) / 2,
				inner,
				inner);
			p.drawImage(rect, icon->frame(st::storiesComposeWhiteText->c));
		} else {
			const auto customSize = fly->center.customSize;
			const auto scaled = (inner != customSize);
			fly->center.custom->paint(p, {
				.textColor = st::storiesComposeWhiteText->c,
				.size = { customSize, customSize },
				.now = crl::now(),
				.scale = (scaled ? (inner / float64(customSize)) : 1.),
				.position = QPoint(
					target.x() + (target.width() - customSize) / 2,
					target.y() + (target.height() - customSize) / 2),
				.scaled = scaled,
			});
		}
	};
	icon->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(icon);
		if (!fly->cache.isNull()) {
			p.drawImage(0, 0, fly->cache);
		} else if (fly->center.icon
			|| fly->center.custom->readyInDefaultState()) {
			const auto ratio = style::DevicePixelRatio();
			fly->cache = QImage(
				icon->size() * ratio,
				QImage::Format_ARGB32_Premultiplied);
			fly->cache.setDevicePixelRatio(ratio);
			fly->cache.fill(Qt::transparent);
			auto q = QPainter(&fly->cache);
			paintNonCached(q);
			q.end();

			fly->center.icon = nullptr;
			fly->center.custom = nullptr;
			p.drawImage(0, 0, fly->cache);
		} else {
			paintNonCached(p);
		}
	}, icon->lifetime());
}

void Reactions::waitForLikeIcon(
		not_null<Data::Session*> owner,
		Data::ReactionId id) {
	_likeIconWaitLifetime = rpl::single(
		rpl::empty
	) | rpl::then(
		owner->reactions().defaultUpdates()
	) | rpl::map([=]() -> rpl::producer<bool> {
		const auto &list = owner->reactions().list(
			Data::Reactions::Type::All);
		const auto i = ranges::find(list, id, &Data::Reaction::id);
		if (i == end(list)) {
			return rpl::single(false);
		}
		const auto document = i->centerIcon
			? not_null(i->centerIcon)
			: i->selectAnimation;
		_likeIconMedia = document->createMediaView();
		_likeIconMedia->checkStickerLarge();
		return rpl::single(
			rpl::empty
		) | rpl::then(
			document->session().downloaderTaskFinished()
		) | rpl::map([=] {
			return _likeIconMedia->loaded();
		});
	}) | rpl::flatten_latest(
	) | rpl::filter(
		rpl::mappers::_1
	) | rpl::take(1) | rpl::start_with_next([=] {
		setLikedId(owner, id, true);

		crl::on_main(&_likeIconGuard, [=] {
			_likeIconMedia = nullptr;
			_likeIconWaitLifetime.destroy();
		});
	});
}

void Reactions::setLikedIdFrom(Data::Story *story) {
	if (!story) {
		assignLikedId({});
	} else {
		setLikedId(&story->owner(), story->sentReactionId());
	}
}

void Reactions::setLikedId(
		not_null<Data::Session*> owner,
		Data::ReactionId id,
		bool force) {
	if (const auto done = setLikedIdIconInit(owner, id, force)) {
		const auto reactions = &owner->reactions();
		const auto colored = [] { return st::storiesComposeWhiteText->c; };
		const auto sizeTag = Data::CustomEmojiSizeTag::Isolated;
		done(Ui::EmojiFlyAnimation(_controller->wrap(), reactions, {
			.id = id,
			.scaleOutDuration = kReactionScaleOutDuration,
			.scaleOutTarget = kReactionScaleOutTarget,
		}, [] {}, colored, sizeTag).grabBadgeCenter());
	}
}

void Reactions::startReactionAnimation(
		Ui::ReactionFlyAnimationArgs args,
		not_null<QWidget*> target,
		Fn<void(Ui::ReactionFlyCenter)> done) {
	const auto wrap = _controller->wrap();
	const auto story = _controller->story();
	_reactionAnimation = std::make_unique<Ui::EmojiFlyAnimation>(
		wrap,
		&story->owner().reactions(),
		std::move(args),
		[=] { _reactionAnimation->repaint(); },
		[] { return st::storiesComposeWhiteText->c; },
		Data::CustomEmojiSizeTag::Isolated);
	const auto layer = _reactionAnimation->layer();
	wrap->paintRequest() | rpl::start_with_next([=] {
		if (!_reactionAnimation->paintBadgeFrame(target)) {
			InvokeQueued(layer, [=] {
				_reactionAnimation = nullptr;
				wrap->update();
			});
			if (done) {
				done(_reactionAnimation->grabBadgeCenter());
			}
		}
	}, layer->lifetime());
	wrap->update();
}

} // namespace Media::Stories
