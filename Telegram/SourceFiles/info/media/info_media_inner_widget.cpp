/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "info/media/info_media_inner_widget.h"

#include "boxes/abstract_box.h"
#include "info/media/info_media_list_widget.h"
#include "info/media/info_media_buttons.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_icon.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_info.h"
#include "lang/lang_keys.h"

namespace Info {
namespace Media {
namespace {

using Type = InnerWidget::Type;

base::optional<int> TypeToTabIndex(Type type) {
	switch (type) {
	case Type::Photo: return 0;
	case Type::Video: return 1;
	case Type::File: return 2;
	}
	return base::none;
}

Type TabIndexToType(int index) {
	switch (index) {
	case 0: return Type::Photo;
	case 1: return Type::Video;
	case 2: return Type::File;
	}
	Unexpected("Index in Info::Media::TabIndexToType()");
}

} // namespace

InnerWidget::InnerWidget(
	QWidget *parent,
	rpl::producer<Wrap> &&wrap,
	not_null<Window::Controller*> controller,
	not_null<PeerData*> peer,
	Type type)
: RpWidget(parent) {
	_list = setupList(controller, peer, type);
	setupOtherTypes(std::move(wrap));
}

void InnerWidget::setupOtherTypes(rpl::producer<Wrap> &&wrap) {
	std::move(wrap)
		| rpl::start_with_next([this](Wrap value) {
			if (value == Wrap::Side
				&& TypeToTabIndex(type())) {
				createOtherTypes();
			} else {
				_otherTabs = nullptr;
				_otherTypes.destroy();
				refreshHeight();
			}
		}, lifetime());
}

void InnerWidget::createOtherTypes() {
	_otherTabsShadow.create(this);
	_otherTabsShadow->show();

	_otherTabs = nullptr;
	_otherTypes.create(this);
	_otherTypes->show();

	createTypeButtons();
	_otherTypes->add(object_ptr<BoxContentDivider>(_otherTypes));
	createTabs();

	_otherTypes->heightValue()
		| rpl::start_with_next(
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
	auto addMediaButton = [&](
			Type type,
			const style::icon &icon) {
		auto result = AddButton(
			content,
			controller(),
			peer(),
			type,
			tracker);
		object_ptr<Profile::FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};
	auto addCommonGroupsButton = [&](
			not_null<UserData*> user,
			const style::icon &icon) {
		auto result = AddCommonGroupsButton(
			content,
			controller(),
			user,
			tracker);
		object_ptr<Profile::FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};

	addMediaButton(Type::MusicFile, st::infoIconMediaAudio);
	addMediaButton(Type::Link, st::infoIconMediaLink);
	if (auto user = peer()->asUser()) {
		addCommonGroupsButton(user, st::infoIconMediaGroup);
	}
	addMediaButton(Type::VoiceFile, st::infoIconMediaVoice);
	addMediaButton(Type::RoundFile, st::infoIconMediaRound);

	content->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		st::infoProfileSkip));
	wrap->toggleOn(tracker.atLeastOneShownValue());
	wrap->finishAnimating();
}

void InnerWidget::createTabs() {
	_otherTabs = _otherTypes->add(object_ptr<Ui::SettingsSlider>(
		this,
		st::infoTabs));
	auto sections = QStringList();
	sections.push_back(lang(lng_media_type_photos).toUpper());
	sections.push_back(lang(lng_media_type_videos).toUpper());
	sections.push_back(lang(lng_media_type_files).toUpper());
	_otherTabs->setSections(sections);
	_otherTabs->setActiveSection(*TypeToTabIndex(type()));
	_otherTabs->finishAnimating();

	_otherTabs->sectionActivated()
		| rpl::map([](int index) { return TabIndexToType(index); })
		| rpl::start_with_next(
			[this](Type type) {
				if (_list->type() != type) {
					switchToTab(Memento(peer()->id, type));
				}
			},
			_otherTabs->lifetime());
}

not_null<PeerData*> InnerWidget::peer() const {
	return _list->peer();
}

Type InnerWidget::type() const {
	return _list->type();
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

bool InnerWidget::showInternal(not_null<Memento*> memento) {
	if (memento->peerId() != peer()->id) {
		return false;
	}
	auto mementoType = memento->section().mediaType();
	if (mementoType == type()) {
		restoreState(memento);
		return true;
	} else if (_otherTypes) {
		if (TypeToTabIndex(mementoType)) {
			switchToTab(std::move(*memento));
			return true;
		}
	}
	return false;
}

void InnerWidget::switchToTab(Memento &&memento) {
	auto type = memento.section().mediaType();
	_list = setupList(controller(), peer(), type);
	restoreState(&memento);
	_list->show();
	_list->resizeToWidth(width());
	refreshHeight();
	if (_otherTypes) {
		_otherTabsShadow->raise();
		_otherTypes->raise();
		_otherTabs->setActiveSection(*TypeToTabIndex(type));
	}
}

not_null<Window::Controller*> InnerWidget::controller() const {
	return _list->controller();
}

object_ptr<ListWidget> InnerWidget::setupList(
		not_null<Window::Controller*> controller,
		not_null<PeerData*> peer,
		Type type) {
	auto result = object_ptr<ListWidget>(
		this,
		controller,
		peer,
		type);
	result->heightValue()
		| rpl::start_with_next(
			[this] { refreshHeight(); },
			result->lifetime());
	using namespace rpl::mappers;
	result->scrollToRequests()
		| rpl::map([widget = result.data()](int to) {
			return widget->y() + to;
		})
		| rpl::start_to_stream(
			_scrollToRequests,
			result->lifetime());
	return result;
}

void InnerWidget::saveState(not_null<Memento*> memento) {
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
}

int InnerWidget::resizeGetHeight(int newWidth) {
	_inResize = true;
	auto guard = gsl::finally([this] { _inResize = false; });

	if (_otherTypes) {
		_otherTypes->resizeToWidth(newWidth);
		_otherTabsShadow->resizeToWidth(newWidth);
	}
	_list->resizeToWidth(newWidth);
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
		_otherTabsShadow->moveToLeft(0, top);
	}
	if (_list) {
		_list->moveToLeft(0, top);
		top += _list->heightNoMargins();
	}
	return top;
}

} // namespace Media
} // namespace Info
