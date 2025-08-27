/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/section_widget.h"

#include "mainwidget.h"
#include "mainwindow.h"
#include "ui/ui_utility.h"
#include "ui/chat/chat_theme.h"
#include "ui/painter.h"
#include "boxes/premium_preview_box.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_cloud_themes.h"
#include "data/data_message_reactions.h"
#include "data/data_peer_values.h"
#include "history/history.h"
#include "history/history_item.h"
#include "settings/settings_premium.h"
#include "main/main_session.h"
#include "window/section_memento.h"
#include "window/window_slide_animation.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"

#include <rpl/range.h>

namespace Window {
namespace {

[[nodiscard]] rpl::producer<QString> PeerThemeEmojiValue(
		not_null<PeerData*> peer) {
	return peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::ChatThemeEmoji
	) | rpl::map([=] {
		return peer->themeEmoji();
	});
}

struct ResolvedPaper {
	Data::WallPaper paper;
	std::shared_ptr<Data::DocumentMedia> media;
};

[[nodiscard]] rpl::producer<const Data::WallPaper*> PeerWallPaperMapped(
		not_null<PeerData*> peer) {
	return peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::ChatWallPaper
	) | rpl::map([=]() -> rpl::producer<const Data::WallPaper*> {
		return WallPaperResolved(&peer->owner(), peer->wallPaper());
	}) | rpl::flatten_latest();
}

[[nodiscard]] rpl::producer<std::optional<ResolvedPaper>> PeerWallPaperValue(
		not_null<PeerData*> peer) {
	return PeerWallPaperMapped(
		peer
	) | rpl::map([=](const Data::WallPaper *paper)
	-> rpl::producer<std::optional<ResolvedPaper>> {
		const auto single = [](std::optional<ResolvedPaper> value) {
			return rpl::single(std::move(value));
		};
		if (!paper) {
			return single({});
		}
		const auto document = paper->document();
		auto value = ResolvedPaper{
			*paper,
			document ? document->createMediaView() : nullptr,
		};
		if (!value.media || value.media->loaded(true)) {
			return single(std::move(value));
		}
		paper->loadDocument();
		return single(
			value
		) | rpl::then(document->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return value.media->loaded(true);
		}) | rpl::take(1) | rpl::map_to(
			std::optional<ResolvedPaper>(value)
		));
	}) | rpl::flatten_latest();
}

[[nodiscard]] auto MaybeChatThemeDataValueFromPeer(
	not_null<PeerData*> peer)
-> rpl::producer<std::optional<Data::CloudTheme>> {
	return PeerThemeEmojiValue(
		peer
	) | rpl::map([=](const QString &emoji)
	-> rpl::producer<std::optional<Data::CloudTheme>> {
		return peer->owner().cloudThemes().themeForEmojiValue(emoji);
	}) | rpl::flatten_latest();
}

[[nodiscard]] rpl::producer<> DebouncedPaletteValue() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		struct State {
			base::has_weak_ptr guard;
			bool scheduled = false;
		};
		const auto state = lifetime.make_state<State>();

		consumer.put_next_copy(rpl::empty);
		style::PaletteChanged(
		) | rpl::start_with_next([=] {
			if (state->scheduled) {
				return;
			}
			state->scheduled = true;
			Ui::PostponeCall(&state->guard, [=] {
				state->scheduled = false;
				consumer.put_next_copy(rpl::empty);
			});
		}, lifetime);

		return lifetime;
	};
}

struct ResolvedTheme {
	std::optional<Data::CloudTheme> theme;
	std::optional<ResolvedPaper> paper;
	bool dark = false;
};

[[nodiscard]] auto MaybeCloudThemeValueFromPeer(
	not_null<PeerData*> peer)
-> rpl::producer<ResolvedTheme> {
	return rpl::combine(
		MaybeChatThemeDataValueFromPeer(peer),
		PeerWallPaperValue(peer),
		Theme::IsThemeDarkValue() | rpl::distinct_until_changed()
	) | rpl::map([](
			std::optional<Data::CloudTheme> theme,
			std::optional<ResolvedPaper> paper,
			bool night) -> rpl::producer<ResolvedTheme> {
		if (theme || !paper) {
			return rpl::single<ResolvedTheme>({
				std::move(theme),
				std::move(paper),
				night,
			});
		}
		return DebouncedPaletteValue(
		) | rpl::map([=] {
			return ResolvedTheme{
				.paper = paper,
				.dark = night,
			};
		});
	}) | rpl::flatten_latest();
}

} // namespace

rpl::producer<const Data::WallPaper*> WallPaperResolved(
		not_null<Data::Session*> owner,
		const Data::WallPaper *paper) {
	const auto id = paper ? paper->emojiId() : QString();
	if (id.isEmpty()) {
		return rpl::single(paper);
	}
	const auto themes = &owner->cloudThemes();
	auto fromThemes = [=](bool force)
	-> rpl::producer<const Data::WallPaper*> {
		if (themes->chatThemes().empty() && !force) {
			return nullptr;
		}
		return Window::Theme::IsNightModeValue(
		) | rpl::map([=](bool dark) -> const Data::WallPaper* {
			const auto &list = themes->chatThemes();
			const auto i = ranges::find(
				list,
				id,
				&Data::CloudTheme::emoticon);
			if (i != end(list)) {
				using Type = Data::CloudThemeType;
				const auto type = dark ? Type::Dark : Type::Light;
				const auto j = i->settings.find(type);
				if (j != end(i->settings) && j->second.paper) {
					return &*j->second.paper;
				}
			}
			return nullptr;
		});
	};
	if (auto result = fromThemes(false)) {
		return result;
	}
	themes->refreshChatThemes();
	return rpl::single<const Data::WallPaper*>(
		nullptr
	) | rpl::then(themes->chatThemesUpdated(
	) | rpl::take(1) | rpl::map([=] {
		return fromThemes(true);
	}) | rpl::flatten_latest());
}

AbstractSectionWidget::AbstractSectionWidget(
	QWidget *parent,
	not_null<SessionController*> controller,
	rpl::producer<PeerData*> peerForBackground)
: RpWidget(parent)
, _controller(controller) {
	std::move(
		peerForBackground
	) | rpl::map([=](PeerData *peer) -> rpl::producer<> {
		if (!peer) {
			return rpl::single(rpl::empty) | rpl::then(
				controller->defaultChatTheme()->repaintBackgroundRequests()
			);
		}
		return ChatThemeValueFromPeer(
			controller,
			peer
		) | rpl::map([](const std::shared_ptr<Ui::ChatTheme> &theme) {
			return rpl::single(rpl::empty) | rpl::then(
				theme->repaintBackgroundRequests()
			);
		}) | rpl::flatten_latest();
	}) | rpl::flatten_latest() | rpl::start_with_next([=] {
		update();
	}, lifetime());
}

Main::Session &AbstractSectionWidget::session() const {
	return _controller->session();
}

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	rpl::producer<PeerData*> peerForBackground)
: AbstractSectionWidget(parent, controller, std::move(peerForBackground)) {
}

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peerForBackground)
: AbstractSectionWidget(
	parent,
	controller,
	rpl::single(peerForBackground.get())) {
}

void SectionWidget::setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta) {
	_topDelta = topDelta;
	bool willBeResized = (size() != newGeometry.size());
	if (geometry() != newGeometry) {
		auto weak = base::make_weak(this);
		setGeometry(newGeometry);
		if (!weak) {
			return;
		}
	}
	if (!willBeResized) {
		resizeEvent(nullptr);
	}
	_topDelta = 0;
}

void SectionWidget::showAnimated(
		SlideDirection direction,
		const SectionSlideParams &params) {
	validateSubsectionTabs();
	if (_showAnimation) {
		return;
	}

	showChildren();
	auto myContentCache = grabForShowAnimation(params);
	hideChildren();
	showAnimatedHook(params);

	_showAnimation = std::make_unique<SlideAnimation>();
	_showAnimation->setDirection(direction);
	_showAnimation->setRepaintCallback([this] { update(); });
	_showAnimation->setFinishedCallback([this] { showFinished(); });
	_showAnimation->setPixmaps(
		params.oldContentCache,
		myContentCache);
	_showAnimation->setTopBarShadow(params.withTopBarShadow);
	_showAnimation->setWithFade(params.withFade);
	_showAnimation->setTopSkip(params.topSkip);
	_showAnimation->setTopBarMask(params.topMask);
	_showAnimation->start();

	show();
}

std::shared_ptr<SectionMemento> SectionWidget::createMemento() {
	return nullptr;
}

void SectionWidget::showFast() {
	validateSubsectionTabs();
	show();
	showFinished();
}

QPixmap SectionWidget::grabForShowAnimation(
		const SectionSlideParams &params) {
	return Ui::GrabWidget(this);
}

void SectionWidget::PaintBackground(
		not_null<Window::SessionController*> controller,
		not_null<Ui::ChatTheme*> theme,
		not_null<QWidget*> widget,
		QRect clip) {
	PaintBackground(
		theme,
		widget,
		controller->content()->height(),
		controller->content()->backgroundFromY(),
		clip);
}

void SectionWidget::PaintBackground(
		not_null<Ui::ChatTheme*> theme,
		not_null<QWidget*> widget,
		int fillHeight,
		int fromy,
		QRect clip) {
	auto p = QPainter(widget);
	if (fromy) {
		p.translate(0, fromy);
		clip = clip.translated(0, -fromy);
	}
	PaintBackground(p, theme, QSize(widget->width(), fillHeight), clip);
}

void SectionWidget::PaintBackground(
		QPainter &p,
		not_null<Ui::ChatTheme*> theme,
		QSize fill,
		QRect clip) {
	const auto &background = theme->background();
	if (background.colorForFill) {
		p.fillRect(clip, *background.colorForFill);
		return;
	}
	const auto &gradient = background.gradientForFill;
	auto state = theme->backgroundState(fill);
	const auto paintCache = [&](const Ui::CachedBackground &cache) {
		const auto to = QRect(
			QPoint(cache.x, cache.y),
			cache.pixmap.size() / style::DevicePixelRatio());
		if (cache.waitingForNegativePattern) {
			// While we wait for pattern being loaded we paint just gradient.
			// But in case of negative patter opacity we just fill-black.
			p.fillRect(to, Qt::black);
		} else if (cache.area == fill) {
			p.drawPixmap(to, cache.pixmap);
		} else {
			const auto sx = fill.width() / float64(cache.area.width());
			const auto sy = fill.height() / float64(cache.area.height());
			const auto round = [](float64 value) -> int {
				return (value >= 0.)
					? int(std::ceil(value))
					: int(std::floor(value));
			};
			const auto sto = QPoint(round(to.x() * sx), round(to.y() * sy));
			p.drawPixmap(
				sto.x(),
				sto.y(),
				round((to.x() + to.width()) * sx) - sto.x(),
				round((to.y() + to.height()) * sy) - sto.y(),
				cache.pixmap);
		}
	};
	const auto hasNow = !state.now.pixmap.isNull();
	const auto goodNow = hasNow && (state.now.area == fill);
	const auto useCache = goodNow || !gradient.isNull();
	if (useCache) {
		const auto fade = (state.shown < 1. && !gradient.isNull());
		if (fade) {
			paintCache(state.was);
			p.setOpacity(state.shown);
		}
		paintCache(state.now);
		if (fade) {
			p.setOpacity(1.);
		}
		return;
	}
	const auto &prepared = background.prepared;
	if (prepared.isNull()) {
		return;
	} else if (background.isPattern) {
		const auto w = prepared.width() * fill.height() / prepared.height();
		const auto cx = qCeil(fill.width() / float64(w));
		const auto cols = (cx / 2) * 2 + 1;
		const auto xshift = (fill.width() - w * cols) / 2;
		for (auto i = 0; i != cols; ++i) {
			p.drawImage(
				QRect(xshift + i * w, 0, w, fill.height()),
				prepared,
				QRect(QPoint(), prepared.size()));
		}
	} else if (background.tile) {
		const auto &tiled = background.preparedForTiled;
		const auto left = clip.left();
		const auto top = clip.top();
		const auto right = clip.left() + clip.width();
		const auto bottom = clip.top() + clip.height();
		const auto w = tiled.width() / float64(style::DevicePixelRatio());
		const auto h = tiled.height() / float64(style::DevicePixelRatio());
		const auto sx = qFloor(left / w);
		const auto sy = qFloor(top / h);
		const auto cx = qCeil(right / w);
		const auto cy = qCeil(bottom / h);
		for (auto i = sx; i < cx; ++i) {
			for (auto j = sy; j < cy; ++j) {
				p.drawImage(QPointF(i * w, j * h), tiled);
			}
		}
	} else {
		const auto hq = PainterHighQualityEnabler(p);
		const auto rects = Ui::ComputeChatBackgroundRects(
			fill,
			prepared.size());
		p.drawImage(rects.to, prepared, rects.from);
	}
}

void SectionWidget::paintEvent(QPaintEvent *e) {
	if (_showAnimation) {
		auto p = QPainter(this);
		_showAnimation->paintContents(p);
	}
}

bool SectionWidget::animatingShow() const {
	return (_showAnimation != nullptr);
}

void SectionWidget::showFinished() {
	_showAnimation.reset();
	if (isHidden()) return;

	showChildren();
	showFinishedHook();

	if (isAncestorOf(window()->focusWidget())) {
		setInnerFocus();
	} else {
		controller()->widget()->setInnerFocus();
	}
}

rpl::producer<int> SectionWidget::desiredHeight() const {
	return rpl::single(height());
}

SectionWidget::~SectionWidget() = default;

auto ChatThemeValueFromPeer(
	not_null<SessionController*> controller,
	not_null<PeerData*> peer)
-> rpl::producer<std::shared_ptr<Ui::ChatTheme>> {
	auto cloud = MaybeCloudThemeValueFromPeer(
		peer
	) | rpl::map([=](ResolvedTheme resolved)
	-> rpl::producer<std::shared_ptr<Ui::ChatTheme>> {
		if (!resolved.theme && !resolved.paper) {
			return rpl::single(controller->defaultChatTheme());
		}
		const auto theme = resolved.theme.value_or(Data::CloudTheme());
		const auto paper = resolved.paper
			? resolved.paper->paper
			: Data::WallPaper(0);
		const auto type = resolved.dark
			? Data::CloudThemeType::Dark
			: Data::CloudThemeType::Light;
		if (paper.document()
			&& resolved.paper->media
			&& !resolved.paper->media->loaded()
			&& !controller->chatThemeAlreadyCached(theme, paper, type)) {
			return rpl::single(controller->defaultChatTheme());
		}
		return controller->cachedChatThemeValue(theme, paper, type);
	}) | rpl::flatten_latest(
	) | rpl::distinct_until_changed();

	return rpl::combine(
		std::move(cloud),
		controller->peerThemeOverrideValue()
	) | rpl::map([=](
			std::shared_ptr<Ui::ChatTheme> &&cloud,
			PeerThemeOverride &&overriden) {
		return (overriden.peer == peer.get()
			&& Ui::Emoji::Find(peer->themeEmoji()) != overriden.emoji)
			? std::move(overriden.theme)
			: std::move(cloud);
	});
}

bool ShowSendPremiumError(
		not_null<SessionController*> controller,
		not_null<DocumentData*> document) {
	return ShowSendPremiumError(controller->uiShow(), document);
}

bool ShowSendPremiumError(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document) {
	if (!document->isPremiumSticker()
		|| document->session().premium()) {
		return false;
	}
	ShowStickerPreviewBox(std::move(show), document);
	return true;
}

bool ShowReactPremiumError(
		not_null<SessionController*> controller,
		not_null<HistoryItem*> item,
		const Data::ReactionId &id) {
	if (item->reactionsAreTags()) {
		if (controller->session().premium()) {
			return false;
		}
		ShowPremiumPreviewBox(controller, PremiumFeature::TagsForMessages);
		return true;
	} else if (controller->session().premium()
		|| ranges::contains(item->chosenReactions(), id)
		|| item->history()->peer->isBroadcast()) {
		return false;
	} else if (!id.custom()) {
		return false;
	}
	ShowPremiumPreviewBox(controller, PremiumFeature::InfiniteReactions);
	return true;
}

} // namespace Window
