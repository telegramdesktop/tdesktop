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
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "lang/lang_keys.h"
#include "ui/ui_utility.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QScrollBar>

namespace Info::Stories {

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
	auto result = object_ptr<Widget>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller)
: ContentWidget(parent, controller)
, _albumId(controller->key().storiesAlbumId())
, _inner(
	setupFlexibleInnerWidget(
		object_ptr<InnerWidget>(
			this,
			controller,
			_albumId.value(),
			controller->key().storiesAddToAlbumId()),
		_flexibleScroll))
, _pinnedToTop(_inner->createPinnedToTop(this)) {
	_emptyAlbumShown = _inner->albumEmptyValue();
	_inner->albumIdChanges() | rpl::on_next([=](int id) {
		controller->showSection(
			Make(controller->storiesPeer(), id),
			Window::SectionShow::Way::Backward);
	}, _inner->lifetime());
	_inner->setScrollHeightValue(scrollHeightValue());
	_inner->scrollToRequests(
	) | rpl::on_next([this](Ui::ScrollToRequest request) {
		if (request.ymin < 0) {
			scrollTopRestore(
				qMin(scrollTopSave(), request.ymax));
		} else {
			scrollTo(request);
		}
	}, lifetime());

	if (_pinnedToTop) {
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

	if (_pinnedToTop
		&& _pinnedToTop->minimumHeight()
		&& _inner->hasFlexibleTopBar()) {
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
	const auto withButton = (albumId != Data::kStoriesAlbumIdSaved)
		&& (albumId != Data::kStoriesAlbumIdArchive)
		&& controller()->storiesPeer()->canEditStories()
		&& !_emptyAlbumShown.current();
	const auto wasBottom = _pinnedToBottom ? _pinnedToBottom->height() : 0;
	delete _pinnedToBottom.data();
	if (!withButton) {
		setScrollBottomSkip(0);
		_hasPinnedToBottom = false;
	} else {
		setupBottomButton(wasBottom);
	}

	if (_pinnedToBottom) {
		const auto processHeight = [=] {
			setScrollBottomSkip(_pinnedToBottom->height());
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
		st::collectionEditBox.button);
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	button->setText(tr::lng_stories_album_add_button(
	) | rpl::map([](const QString &text) {
		return Ui::Text::IconEmoji(&st::collectionAddIcon).append(text);
	}));
	button->show();
	_hasPinnedToBottom = true;

	button->setClickedCallback([=] {
		if (const auto id = _albumId.current()) {
			_inner->editAlbumStories(id);
		} else {
			refreshBottom();
		}
	});

	const auto buttonTop = st::boxRadius;
	bottom->widthValue() | rpl::on_next([=](int width) {
		const auto normal = width - 2 * buttonTop;
		button->resizeToWidth(normal);
		const auto buttonLeft = (width - normal) / 2;
		button->moveToLeft(buttonLeft, buttonTop);
	}, button->lifetime());

	button->heightValue() | rpl::on_next([=](int height) {
		bottom->resize(bottom->width(), st::boxRadius + height);
	}, button->lifetime());

	if (_shown) {
		wrap->toggle(
			true,
			wasBottomHeight ? anim::type::instant : anim::type::normal);
	}
}

void Widget::showFinished() {
	_shown = true;
	if (const auto bottom = _pinnedToBottom.data()) {
		bottom->toggle(true, anim::type::normal);
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

} // namespace Info::Stories

