/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_inner_widget.h"

#include <rpl/flatten_latest.h>
#include "boxes/abstract_box.h"
#include "info/media/info_media_list_widget.h"
#include "info/media/info_media_buttons.h"
#include "info/media/info_media_empty_widget.h"
#include "info/profile/info_profile_icon.h"
#include "info/info_controller.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/search_field_controller.h"
#include "styles/style_info.h"
#include "lang/lang_keys.h"

namespace Info {
namespace Media {

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _empty(this) {
	_empty->heightValue(
	) | rpl::start_with_next(
		[this] { refreshHeight(); },
		_empty->lifetime());
	_list = setupList();
}

// Allows showing additional shared media links and tabs.
// Used for shared media in Saved Messages.
void InnerWidget::setupOtherTypes() {
	if (_controller->key().peer()->sharedMediaInfo() && _isStackBottom) {
		createOtherTypes();
	} else {
		_otherTypes.destroy();
		refreshHeight();
	}
}

void InnerWidget::createOtherTypes() {
	_otherTypes.create(this);
	_otherTypes->show();

	createTypeButtons();
	_otherTypes->add(object_ptr<Ui::BoxContentDivider>(_otherTypes));

	_otherTypes->resizeToWidth(width());
	_otherTypes->heightValue(
	) | rpl::start_with_next(
		[this] { refreshHeight(); },
		_otherTypes->lifetime());
}

void InnerWidget::createTypeButtons() {
	auto wrap = _otherTypes->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_otherTypes,
		object_ptr<Ui::VerticalLayout>(_otherTypes)));
	auto content = wrap->entity();
	content->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		st::infoProfileSkip));

	auto tracker = Ui::MultiSlideTracker();
	const auto peer = _controller->key().peer();
	const auto topic = _controller->key().topic();
	const auto sublist = _controller->key().sublist();
	const auto topicRootId = topic ? topic->rootId() : MsgId();
	const auto monoforumPeerId = sublist
		? sublist->sublistPeer()->id
		: PeerId();
	const auto migrated = _controller->migrated();
	const auto addMediaButton = [&](
			Type buttonType,
			const style::icon &icon) {
		if (buttonType == type()) {
			return;
		}
		auto result = AddButton(
			content,
			_controller,
			peer,
			topicRootId,
			monoforumPeerId,
			migrated,
			buttonType,
			tracker);
		object_ptr<Profile::FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition)->show();
	};

	addMediaButton(Type::Photo, st::infoIconMediaPhoto);
	addMediaButton(Type::Video, st::infoIconMediaVideo);
	addMediaButton(Type::File, st::infoIconMediaFile);
	addMediaButton(Type::MusicFile, st::infoIconMediaAudio);
	addMediaButton(Type::Link, st::infoIconMediaLink);
	addMediaButton(Type::RoundVoiceFile, st::infoIconMediaVoice);
	addMediaButton(Type::GIF, st::infoIconMediaGif);

	content->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		st::infoProfileSkip));
	wrap->toggleOn(tracker.atLeastOneShownValue());
	wrap->finishAnimating();
}

Type InnerWidget::type() const {
	return _controller->section().mediaType();
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

bool InnerWidget::showInternal(not_null<Memento*> memento) {
	if (!_controller->validateMementoPeer(memento)) {
		return false;
	}
	auto mementoType = memento->section().mediaType();
	if (mementoType == type()) {
		restoreState(memento);
		return true;
	}
	return false;
}

object_ptr<ListWidget> InnerWidget::setupList() {
	auto result = object_ptr<ListWidget>(this, _controller);
	result->heightValue(
	) | rpl::start_with_next(
		[this] { refreshHeight(); },
		result->lifetime());
	using namespace rpl::mappers;
	result->scrollToRequests(
	) | rpl::map([widget = result.data()](int to) {
		return Ui::ScrollToRequest {
			widget->y() + to,
			-1
		};
	}) | rpl::start_to_stream(
		_scrollToRequests,
		result->lifetime());
	_selectedLists.fire(result->selectedListValue());
	_listTops.fire(result->topValue());
	_empty->setType(_controller->section().mediaType());
	_controller->mediaSourceQueryValue(
	) | rpl::start_with_next([this](const QString &query) {
		_empty->setSearchQuery(query);
	}, result->lifetime());
	return result;
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	_list->saveState(memento);
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_list->restoreState(memento);
}

rpl::producer<SelectedItems> InnerWidget::selectedListValue() const {
	return _selectedLists.events_starting_with(
		_list->selectedListValue()
	) | rpl::flatten_latest();
}

void InnerWidget::selectionAction(SelectionAction action) {
	_list->selectionAction(action);
}

InnerWidget::~InnerWidget() = default;

int InnerWidget::resizeGetHeight(int newWidth) {
	_inResize = true;
	auto guard = gsl::finally([this] { _inResize = false; });

	if (_otherTypes) {
		_otherTypes->resizeToWidth(newWidth);
	}
	_list->resizeToWidth(newWidth);
	_empty->resizeToWidth(newWidth);
	return recountHeight();
}

void InnerWidget::refreshHeight() {
	if (_inResize) {
		return;
	}
	resize(width(), recountHeight());
}

int InnerWidget::recountHeight() {
	auto top = 0;
	if (_otherTypes) {
		_otherTypes->moveToLeft(0, top);
		top += _otherTypes->heightNoMargins() - st::lineWidth;
	}
	auto listHeight = 0;
	if (_list) {
		_list->moveToLeft(0, top);
		listHeight = _list->heightNoMargins();
		top += listHeight;
	}
	if (listHeight > 0) {
		_empty->hide();
	} else {
		_empty->show();
		_empty->moveToLeft(0, top);
		top += _empty->heightNoMargins();
	}
	return top;
}

void InnerWidget::setScrollHeightValue(rpl::producer<int> value) {
	using namespace rpl::mappers;
	_empty->setFullHeight(rpl::combine(
		std::move(value),
		_listTops.events_starting_with(
			_list->topValue()
		) | rpl::flatten_latest(),
		_1 - _2));
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

} // namespace Media
} // namespace Info
