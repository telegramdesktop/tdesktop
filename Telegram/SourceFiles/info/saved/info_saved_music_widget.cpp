/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/saved/info_saved_music_widget.h"

#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_session.h"
#include "info/media/info_media_list_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "lang/lang_keys.h"
#include "ui/ui_utility.h"
#include "styles/style_credits.h" // giftListAbout
#include "styles/style_info.h"
#include "styles/style_layers.h"

namespace Info::Saved {

class MusicInner final : public Ui::RpWidget {
public:
	MusicInner(QWidget *parent, not_null<Controller*> controller);
	~MusicInner();

	bool showInternal(not_null<MusicMemento*> memento);
	void setIsStackBottom(bool isStackBottom) {
		_isStackBottom = isStackBottom;
	}

	void saveState(not_null<MusicMemento*> memento);
	void restoreState(not_null<MusicMemento*> memento);

	void setScrollHeightValue(rpl::producer<int> value);

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;
	rpl::producer<SelectedItems> selectedListValue() const;
	void selectionAction(SelectionAction action);

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	int recountHeight();
	void refreshHeight();
	void refreshEmpty();

	void setupList();
	void setupEmpty();

	const not_null<Controller*> _controller;
	const not_null<PeerData*> _peer;

	base::unique_qptr<Ui::PopupMenu> _menu;

	object_ptr<Media::ListWidget> _list = { nullptr };
	object_ptr<Ui::RpWidget> _empty = { nullptr };
	int _lastNonLoadingHeight = 0;
	bool _emptyLoading = false;

	bool _inResize = false;
	bool _isStackBottom = false;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<rpl::producer<SelectedItems>> _selectedLists;
	rpl::event_stream<rpl::producer<int>> _listTops;
	rpl::variable<int> _topHeight;
	rpl::variable<bool> _albumEmpty;

};

MusicInner::MusicInner(QWidget *parent, not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _peer(controller->key().musicPeer()) {
	setupList();
	setupEmpty();
}

MusicInner::~MusicInner() = default;

void MusicInner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

bool MusicInner::showInternal(not_null<MusicMemento*> memento) {
	if (memento->section().type() == Section::Type::SavedMusic) {
		restoreState(memento);
		return true;
	}
	return false;
}

void MusicInner::setupList() {
	Expects(!_list);

	_list = object_ptr<Media::ListWidget>(this, _controller);
	const auto raw = _list.data();

	using namespace rpl::mappers;
	raw->scrollToRequests(
	) | rpl::map([=](int to) {
		return Ui::ScrollToRequest {
			raw->y() + to,
			-1
		};
	}) | rpl::start_to_stream(_scrollToRequests, raw->lifetime());
	_selectedLists.fire(raw->selectedListValue());
	_listTops.fire(raw->topValue());

	raw->show();
}

void MusicInner::setupEmpty() {
	_list->resizeToWidth(width());

	const auto savedMusic = &_controller->session().data().savedMusic();
	rpl::combine(
		rpl::single(
			rpl::empty
		) | rpl::then(
			savedMusic->changed() | rpl::filter(
				rpl::mappers::_1 == _peer->id
			) | rpl::to_empty
		),
		_list->heightValue()
	) | rpl::start_with_next([=](auto, int listHeight) {
		const auto padding = st::infoMediaMargin;
		if (const auto raw = _empty.release()) {
			raw->hide();
			raw->deleteLater();
		}
		_emptyLoading = false;
		if (listHeight <= padding.bottom() + padding.top()) {
			refreshEmpty();
		} else {
			_albumEmpty = false;
		}
		refreshHeight();
	}, _list->lifetime());
}

void MusicInner::refreshEmpty() {
	const auto savedMusic = &_controller->session().data().savedMusic();
	const auto knownEmpty = savedMusic->countKnown(_peer->id);
	_empty = object_ptr<Ui::FlatLabel>(
		this,
		(!knownEmpty
			? tr::lng_contacts_loading(Ui::Text::WithEntities)
			: rpl::single(
				tr::lng_media_song_empty(tr::now, Ui::Text::WithEntities))),
		st::giftListAbout);
	_empty->show();
	_emptyLoading = !knownEmpty;
	resizeToWidth(width());
}

void MusicInner::saveState(not_null<MusicMemento*> memento) {
	_list->saveState(&memento->media());
}

void MusicInner::restoreState(not_null<MusicMemento*> memento) {
	_list->restoreState(&memento->media());
}

rpl::producer<SelectedItems> MusicInner::selectedListValue() const {
	return _selectedLists.events_starting_with(
		_list->selectedListValue()
	) | rpl::flatten_latest();
}

void MusicInner::selectionAction(SelectionAction action) {
	_list->selectionAction(action);
}

int MusicInner::resizeGetHeight(int newWidth) {
	if (!newWidth) {
		return 0;
	}
	_inResize = true;
	auto guard = gsl::finally([this] { _inResize = false; });

	if (_list) {
		_list->resizeToWidth(newWidth);
	}
	if (const auto empty = _empty.get()) {
		const auto margin = st::giftListAboutMargin;
		empty->resizeToWidth(newWidth - margin.left() - margin.right());
	}

	return recountHeight();
}

void MusicInner::refreshHeight() {
	if (_inResize) {
		return;
	}
	resize(width(), recountHeight());
}

int MusicInner::recountHeight() {
	auto top = 0;
	auto listHeight = 0;
	if (_list) {
		_list->moveToLeft(0, top);
		listHeight = _list->heightNoMargins();
		top += listHeight;
	}
	if (const auto empty = _empty.get()) {
		const auto margin = st::giftListAboutMargin;
		empty->moveToLeft(margin.left(), top + margin.top());
		top += margin.top() + empty->height() + margin.bottom();
	}
	if (_emptyLoading) {
		top = std::max(top, _lastNonLoadingHeight);
	} else {
		_lastNonLoadingHeight = top;
	}
	return top;
}

void MusicInner::setScrollHeightValue(rpl::producer<int> value) {
}

rpl::producer<Ui::ScrollToRequest> MusicInner::scrollToRequests() const {
	return _scrollToRequests.events();
}

MusicMemento::MusicMemento(not_null<Controller*> controller)
: ContentMemento(MusicTag{ controller->musicPeer() })
, _media(controller) {
}

MusicMemento::MusicMemento(not_null<PeerData*> peer)
: ContentMemento(MusicTag{ peer })
, _media(peer, 0, Media::Type::MusicFile) {
}

MusicMemento::~MusicMemento() = default;

Section MusicMemento::section() const {
	return Section(Section::Type::SavedMusic);
}

object_ptr<ContentWidget> MusicMemento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<MusicWidget>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

MusicWidget::MusicWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<MusicInner>(this, controller));
	_inner->setScrollHeightValue(scrollHeightValue());
	_inner->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		scrollTo(request);
	}, _inner->lifetime());
}

void MusicWidget::setIsStackBottom(bool isStackBottom) {
	ContentWidget::setIsStackBottom(isStackBottom);
	_inner->setIsStackBottom(isStackBottom);
}

bool MusicWidget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	return true;
}

void MusicWidget::setInternalState(
		const QRect &geometry,
		not_null<MusicMemento*> memento) {
		setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> MusicWidget::doCreateMemento() {
	auto result = std::make_shared<MusicMemento>(controller());
	saveState(result.get());
	return result;
}

void MusicWidget::saveState(not_null<MusicMemento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void MusicWidget::restoreState(not_null<MusicMemento*> memento) {
	_inner->restoreState(memento);
	scrollTopRestore(memento->scrollTop());
}

rpl::producer<SelectedItems> MusicWidget::selectedListValue() const {
	return _inner->selectedListValue();
}

void MusicWidget::selectionAction(SelectionAction action) {
	_inner->selectionAction(action);
}

rpl::producer<QString> MusicWidget::title() {
	return controller()->key().musicPeer()->isSelf()
		? tr::lng_media_saved_music_your()
		: tr::lng_media_saved_music_title();
}

std::shared_ptr<Info::Memento> MakeMusic(not_null<PeerData*> peer) {
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(
			1,
			std::make_shared<MusicMemento>(peer)));
}

} // namespace Info::Saved

