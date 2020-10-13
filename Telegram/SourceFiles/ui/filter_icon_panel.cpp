/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/filter_icon_panel.h"

#include "ui/widgets/shadow.h"
#include "ui/image/image_prepare.h"
#include "ui/effects/panel_animation.h"
#include "ui/ui_utility.h"
#include "ui/filter_icons.h"
#include "ui/cached_round_corners.h"
#include "lang/lang_keys.h"
#include "core/application.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"

namespace Ui {
namespace {

constexpr auto kHideTimeoutMs = crl::time(300);
constexpr auto kIconsPerRow = 6;

constexpr auto kIcons = std::array{
	FilterIcon::Cat,
	FilterIcon::Crown,
	FilterIcon::Favorite,
	FilterIcon::Flower,
	FilterIcon::Game,
	FilterIcon::Home,
	FilterIcon::Love,
	FilterIcon::Mask,
	FilterIcon::Party,
	FilterIcon::Sport,
	FilterIcon::Study,
	FilterIcon::Trade,
	FilterIcon::Travel,
	FilterIcon::Work,

	FilterIcon::All,
	FilterIcon::Unread,
	FilterIcon::Unmuted,
	FilterIcon::Bots,
	FilterIcon::Channels,
	FilterIcon::Groups,
	FilterIcon::Private,
	FilterIcon::Custom,
	FilterIcon::Setup,
};

} // namespace

FilterIconPanel::FilterIconPanel(QWidget *parent)
: RpWidget(parent)
, _inner(Ui::CreateChild<Ui::RpWidget>(this)) {
	setup();
}

FilterIconPanel::~FilterIconPanel() {
	hideFast();
}

rpl::producer<FilterIcon> FilterIconPanel::chosen() const {
	return _chosen.events();
}

void FilterIconPanel::setup() {
	setupInner();
	resize(_inner->rect().marginsAdded(innerPadding()).size());
	_inner->move(innerRect().topLeft());

	_hideTimer.setCallback([=] { hideByTimerOrLeave(); });

	macWindowDeactivateEvents(
	) | rpl::filter([=] {
		return !isHidden();
	}) | rpl::start_with_next([=] {
		hideAnimated();
	}, lifetime());

	setAttribute(Qt::WA_OpaquePaintEvent, false);

	hideChildren();
	hide();
}

void FilterIconPanel::setupInner() {
	const auto count = kIcons.size();
	const auto rows = (count / kIconsPerRow)
		+ ((count % kIconsPerRow) ? 1 : 0);
	const auto single = st::windowFilterIconSingle;
	const auto size = QSize(
		single.width() * kIconsPerRow,
		single.height() * rows);
	const auto full = QRect(QPoint(), size).marginsAdded(
		st::windowFilterIconPadding).size();
	_inner->resize(full);

	_inner->paintRequest(
		) | rpl::start_with_next([=](QRect clip) {
		auto p = Painter(_inner);
		Ui::FillRoundRect(
			p,
			_inner->rect(),
			st::emojiPanBg,
			ImageRoundRadius::Small);
		p.setFont(st::emojiPanHeaderFont);
		p.setPen(st::emojiPanHeaderFg);
		p.drawTextLeft(
			st::windowFilterIconHeaderPosition.x(),
			st::windowFilterIconHeaderPosition.y(),
			_inner->width(),
			tr::lng_filters_icon_header(tr::now));

		const auto selected = (_pressed >= 0) ? _pressed : _selected;
		for (auto i = 0; i != kIcons.size(); ++i) {
			const auto rect = countRect(i);
			if (!rect.intersects(clip)) {
				continue;
			}
			if (i == selected) {
				Ui::FillRoundRect(
					p,
					rect,
					st::emojiPanHover,
					Ui::StickerHoverCorners);
			}
			const auto icon = LookupFilterIcon(kIcons[i]).normal;
			icon->paintInCenter(p, rect, st::emojiIconFg->c);
		}
	}, _inner->lifetime());

	_inner->setMouseTracking(true);
	_inner->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		switch (e->type()) {
		case QEvent::Leave: setSelected(-1); break;
		case QEvent::MouseMove:
			mouseMove(static_cast<QMouseEvent*>(e.get())->pos());
			break;
		case QEvent::MouseButtonPress:
			mousePress(static_cast<QMouseEvent*>(e.get())->button());
			break;
		case QEvent::MouseButtonRelease:
			mouseRelease(static_cast<QMouseEvent*>(e.get())->button());
			break;
		}
	}, _inner->lifetime());
}

void FilterIconPanel::setSelected(int selected) {
	if (_selected == selected) {
		return;
	}
	const auto was = (_selected >= 0);
	updateRect(_selected);
	_selected = selected;
	updateRect(_selected);
	const auto now = (_selected >= 0);
	if (was != now) {
		_inner->setCursor(now ? style::cur_pointer : style::cur_default);
	}
}

void FilterIconPanel::setPressed(int pressed) {
	if (_pressed == pressed) {
		return;
	}
	updateRect(_pressed);
	_pressed = pressed;
	updateRect(_pressed);
}

QRect FilterIconPanel::countRect(int index) const {
	Expects(index >= 0);

	const auto row = index / kIconsPerRow;
	const auto column = index % kIconsPerRow;
	const auto single = st::windowFilterIconSingle;
	const auto rect = QRect(
		QPoint(column * single.width(), row * single.height()),
		single);
	const auto padding = st::windowFilterIconPadding;
	return rect.translated(padding.left(), padding.top());
}

void FilterIconPanel::updateRect(int index) {
	if (index < 0) {
		return;
	}
	_inner->update(countRect(index));
}

void FilterIconPanel::mouseMove(QPoint position) {
	const auto padding = st::windowFilterIconPadding;
	if (!_inner->rect().marginsRemoved(padding).contains(position)) {
		setSelected(-1);
	} else {
		const auto point = position - QPoint(padding.left(), padding.top());
		const auto column = point.x() / st::windowFilterIconSingle.width();
		const auto row = point.y() / st::windowFilterIconSingle.height();
		const auto index = row * kIconsPerRow + column;
		setSelected(index < kIcons.size() ? index : -1);
	}
}

void FilterIconPanel::mousePress(Qt::MouseButton button) {
	if (button != Qt::LeftButton) {
		return;
	}
	setPressed(_selected);
}

void FilterIconPanel::mouseRelease(Qt::MouseButton button) {
	if (button != Qt::LeftButton) {
		return;
	}
	const auto pressed = _pressed;
	setPressed(-1);
	if (pressed == _selected && pressed >= 0) {
		Assert(pressed < kIcons.size());
		_chosen.fire_copy(kIcons[pressed]);
	}
}

void FilterIconPanel::paintEvent(QPaintEvent *e) {
	Painter p(this);

	// This call can finish _a_show animation and destroy _showAnimation.
	const auto opacityAnimating = _a_opacity.animating();

	const auto showAnimating = _a_show.animating();
	if (_showAnimation && !showAnimating) {
		_showAnimation.reset();
		if (!opacityAnimating) {
			showChildren();
		}
	}

	if (showAnimating) {
		Assert(_showAnimation != nullptr);
		if (auto opacity = _a_opacity.value(_hiding ? 0. : 1.)) {
			_showAnimation->paintFrame(
				p,
				0,
				0,
				width(),
				_a_show.value(1.),
				opacity);
		}
	} else if (opacityAnimating) {
		p.setOpacity(_a_opacity.value(_hiding ? 0. : 1.));
		p.drawPixmap(0, 0, _cache);
	} else if (_hiding || isHidden()) {
		hideFinished();
	} else {
		if (!_cache.isNull()) _cache = QPixmap();
		Ui::Shadow::paint(
			p,
			innerRect(),
			width(),
			st::emojiPanAnimation.shadow);
	}
}

void FilterIconPanel::enterEventHook(QEvent *e) {
	Core::App().registerLeaveSubscription(this);
	showAnimated();
}

void FilterIconPanel::leaveEventHook(QEvent *e) {
	Core::App().unregisterLeaveSubscription(this);
	if (_a_show.animating() || _a_opacity.animating()) {
		hideAnimated();
	} else {
		_hideTimer.callOnce(kHideTimeoutMs);
	}
	return TWidget::leaveEventHook(e);
}

void FilterIconPanel::otherEnter() {
	showAnimated();
}

void FilterIconPanel::otherLeave() {
	if (_a_opacity.animating()) {
		hideByTimerOrLeave();
	} else {
		_hideTimer.callOnce(0);
	}
}

void FilterIconPanel::hideFast() {
	if (isHidden()) return;

	_hideTimer.cancel();
	_hiding = false;
	_a_opacity.stop();
	hideFinished();
}

void FilterIconPanel::opacityAnimationCallback() {
	update();
	if (!_a_opacity.animating()) {
		if (_hiding) {
			_hiding = false;
			hideFinished();
		} else if (!_a_show.animating()) {
			showChildren();
		}
	}
}

void FilterIconPanel::hideByTimerOrLeave() {
	if (isHidden()) {
		return;
	}

	hideAnimated();
}

void FilterIconPanel::prepareCacheFor(bool hiding) {
	if (_a_opacity.animating()) {
		_hiding = hiding;
		return;
	}

	auto showAnimation = base::take(_a_show);
	auto showAnimationData = base::take(_showAnimation);
	_hiding = false;
	showChildren();

	_cache = Ui::GrabWidget(this);

	_a_show = base::take(showAnimation);
	_showAnimation = base::take(showAnimationData);
	_hiding = hiding;
	if (_a_show.animating()) {
		hideChildren();
	}
}

void FilterIconPanel::startOpacityAnimation(bool hiding) {
	prepareCacheFor(hiding);
	hideChildren();
	_a_opacity.start(
		[=] { opacityAnimationCallback(); },
		_hiding ? 1. : 0.,
		_hiding ? 0. : 1.,
		st::emojiPanDuration);
}

void FilterIconPanel::startShowAnimation() {
	if (!_a_show.animating()) {
		auto image = grabForAnimation();

		_showAnimation = std::make_unique<Ui::PanelAnimation>(st::emojiPanAnimation, Ui::PanelAnimation::Origin::TopRight);
		auto inner = rect().marginsRemoved(st::emojiPanMargins);
		_showAnimation->setFinalImage(std::move(image), QRect(inner.topLeft() * cIntRetinaFactor(), inner.size() * cIntRetinaFactor()));
		_showAnimation->setCornerMasks(Images::CornersMask(ImageRoundRadius::Small));
		_showAnimation->start();
	}
	hideChildren();
	_a_show.start([this] { update(); }, 0., 1., st::emojiPanShowDuration);
}

QImage FilterIconPanel::grabForAnimation() {
	auto cache = base::take(_cache);
	auto opacityAnimation = base::take(_a_opacity);
	auto showAnimationData = base::take(_showAnimation);
	auto showAnimation = base::take(_a_show);

	showChildren();
	Ui::SendPendingMoveResizeEvents(this);

	auto result = QImage(
		size() * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	if (_inner) {
		QPainter p(&result);
		Ui::RenderWidget(p, _inner, _inner->pos());
	}

	_a_show = base::take(showAnimation);
	_showAnimation = base::take(showAnimationData);
	_a_opacity = base::take(opacityAnimation);
	_cache = base::take(_cache);

	return result;
}

void FilterIconPanel::hideAnimated() {
	if (isHidden() || _hiding) {
		return;
	}

	_hideTimer.cancel();
	startOpacityAnimation(true);
}

void FilterIconPanel::toggleAnimated() {
	if (isHidden() || _hiding) {
		showAnimated();
	} else {
		hideAnimated();
	}
}

void FilterIconPanel::hideFinished() {
	hide();
	_a_show.stop();
	_showAnimation.reset();
	_cache = QPixmap();
	_hiding = false;
}

void FilterIconPanel::showAnimated() {
	_hideTimer.cancel();
	showStarted();
}

void FilterIconPanel::showStarted() {
	if (isHidden()) {
		raise();
		show();
		startShowAnimation();
	} else if (_hiding) {
		startOpacityAnimation(false);
	}
}

bool FilterIconPanel::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		otherEnter();
	} else if (e->type() == QEvent::Leave) {
		otherLeave();
	}
	return false;
}

style::margins FilterIconPanel::innerPadding() const {
	return st::emojiPanMargins;
}

QRect FilterIconPanel::innerRect() const {
	return rect().marginsRemoved(innerPadding());
}

} // namespace Ui
