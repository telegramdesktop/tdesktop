/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/stories/info_stories_widget.h"

#include "data/data_peer.h"
#include "data/data_stories.h"
#include "info/stories/info_stories_inner_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "media/stories/media_stories_post.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/shadow.h"
#include "lang/lang_keys.h"
#include "ui/ui_utility.h"
#include "styles/style_basic.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QScrollBar>

namespace Info::Stories {

namespace {

void PaintIslandOutline(
		QPainter &p,
		const QRectF &island,
		float64 radius,
		const style::color &bg) {
	const auto light = (bg->c.lightness() >= 128);
	const auto width = light ? (0.6 * st::lineWidth) : double(st::lineWidth);
	if (light) {
		p.setPen(QPen(QColor(0, 0, 0, 20), width));
	} else {
		auto grad = QLinearGradient(0, island.top(), 0, island.bottom());
		grad.setColorAt(0., QColor(255, 255, 255, 40));
		grad.setColorAt(0.5, QColor(255, 255, 255, 0));
		grad.setColorAt(1., QColor(255, 255, 255, 20));
		p.setPen(QPen(QBrush(grad), width));
	}
	p.setBrush(Qt::NoBrush);
	const auto half = 0.5 * width;
	const auto stroke = light
		? island.marginsAdded(QMarginsF(half, half, half, half))
		: island.adjusted(half, half, -half, -half);
	const auto strokeRadius = light ? (radius + half) : (radius - half);
	p.drawRoundedRect(stroke, strokeRadius, strokeRadius);
}

} // namespace

int ArchiveId() {
	return Data::kStoriesAlbumIdArchive;
}

Memento::Memento(not_null<Controller*> controller)
: ContentMemento(Tag{
	controller->storiesPeer(),
	controller->storiesAlbumId(),
	controller->storiesAddToAlbumId() })
, _media(controller) {
}

Memento::Memento(not_null<PeerData*> peer, int albumId, int addingToAlbumId)
: ContentMemento(Tag{ peer, albumId, addingToAlbumId })
, _media(peer, 0, Media::Type::PhotoVideo) {
}

Memento::~Memento() = default;

Section Memento::section() const {
	return Section(Section::Type::Stories);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, _myProfile);
	result->setInternalState(geometry, this);
	return result;
}

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller,
	bool myProfile)
: ContentWidget(parent, controller)
, _albumId(controller->key().storiesAlbumId())
, _inner(UseClassicProfileScroll()
	? setupFlexibleInnerWidget(
		object_ptr<InnerWidget>(
			this,
			controller,
			_albumId.value(),
			controller->key().storiesAddToAlbumId(),
			myProfile),
		_flexibleScroll)
	: setInnerWidget(
		object_ptr<InnerWidget>(
			this,
			controller,
			_albumId.value(),
			controller->key().storiesAddToAlbumId(),
			myProfile)))
, _pinnedToTop(_inner->createPinnedToTop(this)) {
	const auto classic = UseClassicProfileScroll();
	const auto flexible = _pinnedToTop
		&& _pinnedToTop->minimumHeight()
		&& _inner->hasFlexibleTopBar();
	if (classic) {
		_inner->move(0, 0);
	}

	_emptyAlbumShown = _inner->albumEmptyValue();
	_inner->albumIdChanges() | rpl::on_next([=](int id) {
		controller->showSection(
			Make(controller->storiesPeer(), id),
			Window::SectionShow::Way::Backward);
	}, _inner->lifetime());
	_inner->setScrollHeightValue(scrollHeightValue());
	_inner->scrollToRequests(
	) | rpl::on_next([this](Ui::ScrollToRequest request) {
		const auto reserve = innerTopReserve();
		if (request.ymin < 0) {
			scrollTopRestore(
				std::min(scrollTopSave(), request.ymax + reserve));
		} else {
			scrollTo({
				request.ymin + reserve,
				(request.ymax < 0) ? -1 : (request.ymax + reserve),
			});
		}
	}, lifetime());

	if (!classic && flexible) {
		setupFlexibleRegularScroll(_inner, _pinnedToTop.get());
	} else if (_pinnedToTop) {
		_inner->widthValue(
		) | rpl::on_next([=](int w) {
			_pinnedToTop->resizeToWidth(w);
			setScrollTopSkip(_pinnedToTop->height());
		}, _pinnedToTop->lifetime());

		_pinnedToTop->heightValue(
		) | rpl::on_next([=](int h) {
			setScrollTopSkip(h);
		}, _pinnedToTop->lifetime());
	}

	if (classic && flexible) {
		_flexibleScrollHelper = std::make_unique<FlexibleScrollHelper>(
			scroll(),
			_inner,
			_pinnedToTop.get(),
			[=](QMargins margins) {
				ContentWidget::setPaintPadding(std::move(margins));
			},
			[=](rpl::producer<not_null<QEvent*>> &&events) {
				ContentWidget::setViewport(std::move(events));
			},
			_flexibleScroll);
	}

	rpl::combine(
		_albumId.value(),
		_emptyAlbumShown.value()
	) | rpl::on_next([=] {
		refreshBottom();
	}, _inner->lifetime());

	rpl::combine(
		scroll()->scrollTopValue(),
		_inner->heightValue(),
		heightValue()
	) | rpl::on_next([=](int top, int contentHeight, int viewHeight) {
		const auto max = std::max(contentHeight - viewHeight, 0);
		const auto closeToBottom
			= (top + st::storiesBottomButtonRevealAfter >= max);
		if (_bottomNear == closeToBottom) {
			return;
		}
		_bottomNear = closeToBottom;
		if (const auto wrap = _pinnedToBottom.data()) {
			wrap->toggle(_shown && closeToBottom, anim::type::normal);
		}
	}, lifetime());

	_inner->backRequest() | rpl::on_next([=] {
		checkBeforeClose([=] { controller->showBackFromStack(); });
	}, _inner->lifetime());
}

void Widget::setInnerFocus() {
	_inner->setFocus();
}

void Widget::setIsStackBottom(bool isStackBottom) {
	ContentWidget::setIsStackBottom(isStackBottom);
	_inner->setIsStackBottom(isStackBottom);
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto storiesMemento = dynamic_cast<Memento*>(memento.get())) {
		const auto myId = controller()->key().storiesAlbumId();
		const auto hisId = storiesMemento->storiesAlbumId();
		constexpr auto kArchive = Data::kStoriesAlbumIdArchive;
		if (myId == hisId) {
			restoreState(storiesMemento);
			return true;
		} else if (myId != kArchive && hisId != kArchive) {
			_albumId = hisId;
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
		setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(controller());
	result->setMyProfile(_inner->myProfile());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
	scrollTopRestore(memento->scrollTop());
}

void Widget::refreshBottom() {
	const auto albumId = _albumId.current();
	const auto peer = controller()->storiesPeer();
	const auto withButton = (albumId == Data::kStoriesAlbumIdArchive)
		? false
		: (albumId == Data::kStoriesAlbumIdSaved)
		? (_inner->myProfile() && ::Media::Stories::CanPostStory(peer))
		: (peer->canEditStories() && !_emptyAlbumShown.current());
	const auto wasBottom = _pinnedToBottom ? _pinnedToBottom->height() : 0;
	delete _pinnedToBottom.data();
	if (!withButton) {
		_hasPinnedToBottom = false;
	} else {
		setupBottomButton(wasBottom);
	}

	if (_pinnedToBottom) {
		const auto processHeight = [=] {
			_pinnedToBottom->moveToLeft(
				_pinnedToBottom->x(),
				height() - _pinnedToBottom->height());
		};

		_inner->sizeValue(
		) | rpl::on_next([=](const QSize &s) {
			_pinnedToBottom->resizeToWidth(s.width());
		}, _pinnedToBottom->lifetime());

		rpl::combine(
			_pinnedToBottom->heightValue(),
			heightValue()
		) | rpl::on_next(processHeight, _pinnedToBottom->lifetime());
	}
}

void Widget::setupBottomButton(int wasBottomHeight) {
	_pinnedToBottom = Ui::CreateChild<Ui::SlideWrap<Ui::RpWidget>>(
		this,
		object_ptr<Ui::RpWidget>(this));
	const auto wrap = _pinnedToBottom.data();
	wrap->toggle(false, anim::type::instant);

	const auto bottom = wrap->entity();
	bottom->show();

	const auto button = Ui::CreateChild<Ui::RoundButton>(
		bottom,
		rpl::single(QString()),
		st::storiesBottomButton);
	button->setFullRadius(true);
	const auto saved = (_albumId.current() == Data::kStoriesAlbumIdSaved);
	button->setText((saved
		? tr::lng_story_add_button()
		: tr::lng_stories_album_add_button()
	) | rpl::map([saved](const QString &text) {
		return Ui::Text::IconEmoji(
			saved ? &st::storiesAddPostIcon : &st::collectionAddIcon
		).append(text);
	}));
	button->show();
	_hasPinnedToBottom = true;

	const auto postOrEdit = [=] {
		if (_albumId.current() == Data::kStoriesAlbumIdSaved) {
			::Media::Stories::PostStory(
				controller()->parentController(),
				controller()->storiesPeer());
		} else if (const auto id = _albumId.current()) {
			_inner->editAlbumStories(id);
		} else {
			refreshBottom();
		}
	};
	button->setClickedCallback(postOrEdit);

	const auto halo = st::storiesBottomButtonHalo;
	const auto shadowExtend = Ui::BoxShadow::ExtendFor(
		st::storiesBottomButtonShadow);
	const auto pad = shadowExtend.top();
	const auto bottomMargin = st::storiesBottomButtonBottom;
	const auto bottomPadding = shadowExtend.bottom() + bottomMargin;

	rpl::combine(
		bottom->widthValue(),
		button->widthValue()
	) | rpl::on_next([=](int outerWidth, int innerWidth) {
		button->moveToLeft(
			(outerWidth - innerWidth) / 2,
			pad + halo);
	}, button->lifetime());

	button->heightValue() | rpl::on_next([=](int height) {
		bottom->resize(
			bottom->width(),
			pad + height + 2 * halo + bottomPadding);
	}, button->lifetime());

	const auto shadow = bottom->lifetime().make_state<Ui::BoxShadow>(
		st::storiesBottomButtonShadow);
	bottom->paintRequest(
	) | rpl::on_next([=] {
		const auto inner = button->geometry();
		if (inner.isEmpty()) {
			return;
		}
		const auto outer = inner.marginsAdded({ halo, halo, halo, halo });
		const auto radius = outer.height() / 2.;

		auto p = QPainter(bottom);
		shadow->paint(p, outer, int(radius));
		{
			auto hq = PainterHighQualityEnabler(p);
			p.setBrush(st::windowBg);
			p.setPen(Qt::NoPen);
			p.drawRoundedRect(outer, radius, radius);
		}
		PaintIslandOutline(p, QRectF(outer), radius, st::windowBg);
	}, bottom->lifetime());

	bottom->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseMove) {
			const auto me = static_cast<QMouseEvent*>(e.get());
			const auto outer = button->geometry().marginsAdded(
				{ halo, halo, halo, halo });
			bottom->setCursor(outer.contains(me->pos())
				? style::cur_pointer
				: style::cur_default);
		} else if (type == QEvent::MouseButtonPress) {
			const auto me = static_cast<QMouseEvent*>(e.get());
			const auto outer = button->geometry().marginsAdded(
				{ halo, halo, halo, halo });
			if (me->button() == Qt::LeftButton && outer.contains(me->pos())) {
				postOrEdit();
			}
		} else if (type == QEvent::Wheel) {
			scroll()->viewportEvent(e.get());
		}
	}, bottom->lifetime());
	bottom->setMouseTracking(true);

	if (_shown) {
		wrap->toggle(
			_shown && _bottomNear,
			wasBottomHeight ? anim::type::instant : anim::type::normal);
	}
}

void Widget::enableBackButton() {
	_inner->enableBackButton();
}

void Widget::showFinished() {
	_shown = true;
	if (const auto bottom = _pinnedToBottom.data()) {
		bottom->toggle(_shown && _bottomNear, anim::type::normal);
	}
	_inner->showFinished();
}

rpl::producer<SelectedItems> Widget::selectedListValue() const {
	return _inner->selectedListValue();
}

void Widget::selectionAction(SelectionAction action) {
	_inner->selectionAction(action);
}

rpl::producer<QString> Widget::title() {
	const auto peer = controller()->key().storiesPeer();
	return (controller()->key().storiesAlbumId() == ArchiveId())
		? tr::lng_stories_archive_title()
		: (peer && peer->isSelf())
		? tr::lng_menu_my_profile()
		: tr::lng_stories_my_title();
}

rpl::producer<bool> Widget::desiredBottomShadowVisibility() {
	return _hasPinnedToBottom.value();
}

std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer, int albumId) {
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(
			1,
			std::make_shared<Memento>(peer, albumId, 0)));
}

std::shared_ptr<Info::Memento> MakeMyProfile(not_null<PeerData*> peer) {
	const auto memento = std::make_shared<Memento>(peer, 0, 0);
	memento->setMyProfile(true);
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(1, memento));
}

} // namespace Info::Stories

