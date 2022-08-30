/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/section_widget.h"

#include "mainwidget.h"
#include "ui/ui_utility.h"
#include "ui/chat/chat_theme.h"
#include "ui/toasts/common_toasts.h"
#include "boxes/premium_preview_box.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_document.h"
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

struct ResolvedTheme {
	std::optional<Data::CloudTheme> theme;
	bool dark = false;
};

[[nodiscard]] auto MaybeCloudThemeValueFromPeer(
	not_null<PeerData*> peer)
-> rpl::producer<ResolvedTheme> {
	return rpl::combine(
		MaybeChatThemeDataValueFromPeer(peer),
		Theme::IsThemeDarkValue() | rpl::distinct_until_changed()
	) | rpl::map([](std::optional<Data::CloudTheme> theme, bool night) {
		return ResolvedTheme{ std::move(theme), night };
	});
}

} // namespace

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
		auto weak = Ui::MakeWeak(this);
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
	if (_showAnimation) return;

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
	Painter p(widget);

	const auto &background = theme->background();
	if (background.colorForFill) {
		p.fillRect(clip, *background.colorForFill);
		return;
	}
	const auto &gradient = background.gradientForFill;
	const auto fill = QSize(widget->width(), controller->content()->height());
	auto fromy = controller->content()->backgroundFromY();
	auto state = theme->backgroundState(fill);
	const auto paintCache = [&](const Ui::CachedBackground &cache) {
		const auto to = QRect(
			QPoint(cache.x, fromy + cache.y),
			cache.pixmap.size() / cIntRetinaFactor());
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
		if (state.shown < 1. && !gradient.isNull()) {
			paintCache(state.was);
			p.setOpacity(state.shown);
		}
		paintCache(state.now);
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
		const auto w = tiled.width() / cRetinaFactor();
		const auto h = tiled.height() / cRetinaFactor();
		const auto sx = qFloor(left / w);
		const auto sy = qFloor((top - fromy) / h);
		const auto cx = qCeil(right / w);
		const auto cy = qCeil((bottom - fromy) / h);
		for (auto i = sx; i < cx; ++i) {
			for (auto j = sy; j < cy; ++j) {
				p.drawImage(QPointF(i * w, fromy + j * h), tiled);
			}
		}
	} else {
		const auto hq = PainterHighQualityEnabler(p);
		const auto rects = Ui::ComputeChatBackgroundRects(
			fill,
			prepared.size());
		auto to = rects.to;
		to.moveTop(to.top() + fromy);
		p.drawImage(to, prepared, rects.from);
	}
}

void SectionWidget::paintEvent(QPaintEvent *e) {
	if (_showAnimation) {
		Painter p(this);
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

	setInnerFocus();
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
		return resolved.theme
			? controller->cachedChatThemeValue(
				*resolved.theme,
				(resolved.dark
					? Data::CloudThemeType::Dark
					: Data::CloudThemeType::Light))
			: rpl::single(controller->defaultChatTheme());
	}) | rpl::flatten_latest(
	) | rpl::distinct_until_changed();

	return rpl::combine(
		std::move(cloud),
		controller->peerThemeOverrideValue()
	) | rpl::map([=](
			std::shared_ptr<Ui::ChatTheme> &&cloud,
			PeerThemeOverride &&overriden) {
		return (overriden.peer == peer.get())
			? std::move(overriden.theme)
			: std::move(cloud);
	});
}

bool ShowSendPremiumError(
		not_null<SessionController*> controller,
		not_null<DocumentData*> document) {
	if (!document->isPremiumSticker()
		|| document->session().premium()) {
		return false;
	}
	ShowStickerPreviewBox(controller, document);
	return true;
}

[[nodiscard]] auto ExtractDisabledReactions(
	not_null<PeerData*> peer,
	const std::vector<Data::Reaction> &list)
-> base::flat_map<Data::ReactionId, ReactionDisableType> {
	auto result = base::flat_map<Data::ReactionId, ReactionDisableType>();
	const auto type = peer->isBroadcast()
		? ReactionDisableType::Channel
		: ReactionDisableType::Group;
	const auto &allowed = Data::PeerAllowedReactions(peer);
	if (!allowed.some.empty()) {
		for (const auto &reaction : list) {
			if (reaction.premium
				&& !ranges::contains(allowed.some, reaction.id)) {
				result.emplace(reaction.id, type);
			}
		}
	}
	return result;
}

bool ShowReactPremiumError(
		not_null<SessionController*> controller,
		not_null<HistoryItem*> item,
		const Data::ReactionId &id) {
	if (controller->session().premium()
		|| ranges::contains(item->chosenReactions(), id)) {
		return false;
	}
	const auto &list = controller->session().data().reactions().list(
		Data::Reactions::Type::Active);
	const auto i = ranges::find(list, id, &Data::Reaction::id);
	if (i == end(list) || !i->premium) {
		if (!id.custom()) {
			return false;
		}
	}
	ShowPremiumPreviewBox(
		controller,
		PremiumPreview::Reactions,
		ExtractDisabledReactions(item->history()->peer, list));
	return true;
}

void ShowPremiumPromoBox(
		not_null<SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto &list = controller->session().data().reactions().list(
		Data::Reactions::Type::Active);
	ShowPremiumPreviewBox(
		controller,
		PremiumPreview::Reactions,
		ExtractDisabledReactions(item->history()->peer, list));
}

} // namespace Window
