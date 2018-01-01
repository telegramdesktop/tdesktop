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
#include "info/info_top_bar.h"

#include <rpl/never.h>
#include <rpl/merge.h>
#include "styles/style_info.h"
#include "lang/lang_keys.h"
#include "info/info_wrap_widget.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_values.h"
#include "storage/storage_shared_media.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "mainwidget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/search_field_controller.h"
#include "window/window_peer_menu.h"

namespace Info {

TopBar::TopBar(
	QWidget *parent,
	const style::InfoTopBar &st,
	SelectedItems &&selectedItems)
: RpWidget(parent)
, _st(st)
, _selectedItems(Section::MediaType::kCount) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	setSelectedItems(std::move(selectedItems));
	updateControlsVisibility(anim::type::instant);
}

template <typename Callback>
void TopBar::registerUpdateControlCallback(
		QObject *guard,
		Callback &&callback) {
	_updateControlCallbacks[guard] =[
		weak = make_weak(guard),
		callback = std::forward<Callback>(callback)
	](anim::type animated) {
		if (!weak) {
			return false;
		}
		callback(animated);
		return true;
	};
}

template <typename Widget, typename IsVisible>
void TopBar::registerToggleControlCallback(
		Widget *widget,
		IsVisible &&callback) {
	registerUpdateControlCallback(widget, [
		widget,
		isVisible = std::forward<IsVisible>(callback)
	](anim::type animated) {
		widget->toggle(isVisible(), animated);
	});
}

void TopBar::setTitle(rpl::producer<QString> &&title) {
	if (_title) {
		delete _title;
	}
	_title = Ui::CreateChild<Ui::FadeWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(this, std::move(title), _st.title),
		st::infoTopBarScale);
	_title->setDuration(st::infoTopBarDuration);
	_title->toggle(!selectionMode(), anim::type::instant);
	registerToggleControlCallback(_title.data(), [=] {
		return !selectionMode() && !searchMode();
	});

	if (_back) {
		_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	updateControlsGeometry(width());
}

void TopBar::enableBackButton() {
	if (_back) {
		return;
	}
	_back = Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		this,
		object_ptr<Ui::IconButton>(this, _st.back),
		st::infoTopBarScale);
	_back->setDuration(st::infoTopBarDuration);
	_back->toggle(!selectionMode(), anim::type::instant);
	_back->entity()->clicks()
		| rpl::start_to_stream(_backClicks, _back->lifetime());
	registerToggleControlCallback(_back.data(), [=] {
		return !selectionMode();
	});

	if (_title) {
		_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	updateControlsGeometry(width());
}

void TopBar::createSearchView(
		not_null<Ui::SearchFieldController*> controller,
		rpl::producer<bool> &&shown,
		bool startsFocused) {
	setSearchField(
		controller->createField(this, _st.searchRow.field),
		std::move(shown),
		startsFocused);
}

bool TopBar::focusSearchField() {
	if (_searchField && _searchField->isVisible()) {
		_searchField->setFocus();
		return true;
	}
	return false;
}

Ui::FadeWrap<Ui::RpWidget> *TopBar::pushButton(
		base::unique_qptr<Ui::RpWidget> button) {
	auto wrapped = base::make_unique_q<Ui::FadeWrap<Ui::RpWidget>>(
		this,
		object_ptr<Ui::RpWidget>::fromRaw(button.release()),
		st::infoTopBarScale);
	auto weak = wrapped.get();
	_buttons.push_back(std::move(wrapped));
	weak->setDuration(st::infoTopBarDuration);
	registerToggleControlCallback(weak, [=] {
		return !selectionMode()
			&& !_searchModeEnabled;
	});
	weak->toggle(
		!selectionMode() && !_searchModeEnabled,
		anim::type::instant);
	weak->widthValue(
	) | rpl::start_with_next([this] {
		updateControlsGeometry(width());
	}, lifetime());
	return weak;
}

void TopBar::setSearchField(
		base::unique_qptr<Ui::InputField> field,
		rpl::producer<bool> &&shown,
		bool startsFocused) {
	Expects(field != nullptr);
	createSearchView(field.release(), std::move(shown), startsFocused);
}

void TopBar::clearSearchField() {
	_searchView = nullptr;
}

void TopBar::createSearchView(
		not_null<Ui::InputField*> field,
		rpl::producer<bool> &&shown,
		bool startsFocused) {
	_searchView = base::make_unique_q<Ui::FixedHeightWidget>(
		this,
		_st.searchRow.height);
	auto wrap = _searchView.get();
	registerUpdateControlCallback(wrap, [=](anim::type) {
		wrap->setVisible(!selectionMode() && _searchModeAvailable);
	});

	_searchField = field;
	auto fieldWrap = Ui::CreateChild<Ui::FadeWrap<Ui::InputField>>(
		wrap,
		object_ptr<Ui::InputField>::fromRaw(field),
		st::infoTopBarScale);
	fieldWrap->setDuration(st::infoTopBarDuration);

	auto focusLifetime = field->lifetime().make_state<rpl::lifetime>();
	registerUpdateControlCallback(fieldWrap, [=](anim::type animated) {
		auto fieldShown = !selectionMode() && searchMode();
		if (!fieldShown && field->hasFocus()) {
			setFocus();
		}
		fieldWrap->toggle(fieldShown, animated);
		if (fieldShown) {
			*focusLifetime = field->shownValue()
				| rpl::filter([](bool shown) { return shown; })
				| rpl::take(1)
				| rpl::start_with_next([=] { field->setFocus(); });
		} else {
			focusLifetime->destroy();
		}
	});

	auto button = base::make_unique_q<Ui::IconButton>(this, _st.search);
	auto search = button.get();
	search->addClickHandler([=] {
		_searchModeEnabled = true;
		updateControlsVisibility(anim::type::normal);
	});
	auto searchWrap = pushButton(std::move(button));
	registerToggleControlCallback(searchWrap, [=] {
		return !selectionMode()
			&& _searchModeAvailable
			&& !_searchModeEnabled;
	});

	auto cancel = Ui::CreateChild<Ui::CrossButton>(
		wrap,
		_st.searchRow.fieldCancel);
	registerToggleControlCallback(cancel, [=] {
		return !selectionMode() && searchMode();
	});

	auto cancelSearch = [=] {
		if (!field->getLastText().isEmpty()) {
			field->setText(QString());
		} else {
			_searchModeEnabled = false;
			updateControlsVisibility(anim::type::normal);
		}
	};

	cancel->addClickHandler(cancelSearch);
	field->connect(field, &Ui::InputField::cancelled, cancelSearch);

	wrap->widthValue(
	) | rpl::start_with_next([=](int newWidth) {
		auto availableWidth = newWidth
			- _st.searchRow.fieldCancelSkip;
		fieldWrap->resizeToWidth(availableWidth);
		fieldWrap->moveToLeft(
			_st.searchRow.padding.left(),
			_st.searchRow.padding.top());
		cancel->moveToRight(0, 0);
	}, wrap->lifetime());

	widthValue(
	) | rpl::start_with_next([=](int newWidth) {
		auto left = _back
			? _st.back.width
			: _st.titlePosition.x();
		wrap->setGeometryToLeft(
			left,
			0,
			newWidth - left,
			wrap->height(),
			newWidth);
	}, wrap->lifetime());

	field->alive(
	) | rpl::start_with_done([=] {
		field->setParent(nullptr);
		removeButton(search);
		clearSearchField();
	}, _searchView->lifetime());

	_searchModeEnabled = !field->getLastText().isEmpty() || startsFocused;
	updateControlsVisibility(anim::type::instant);

	std::move(
		shown
	) | rpl::start_with_next([=](bool visible) {
		auto alreadyInSearch = !field->getLastText().isEmpty();
		_searchModeAvailable = visible || alreadyInSearch;
		updateControlsVisibility(anim::type::instant);
	}, wrap->lifetime());
}

void TopBar::removeButton(not_null<Ui::RpWidget*> button) {
	_buttons.erase(
		std::remove(_buttons.begin(), _buttons.end(), button),
		_buttons.end());
}

int TopBar::resizeGetHeight(int newWidth) {
	updateControlsGeometry(newWidth);
	return _st.height;
}

void TopBar::updateControlsGeometry(int newWidth) {
	updateDefaultControlsGeometry(newWidth);
	updateSelectionControlsGeometry(newWidth);
}

void TopBar::updateDefaultControlsGeometry(int newWidth) {
	auto right = 0;
	for (auto &button : _buttons) {
		if (!button) continue;
		button->moveToRight(right, 0, newWidth);
		right += button->width();
	}
	if (_back) {
		_back->setGeometryToLeft(
			0,
			0,
			newWidth - right,
			_back->height(),
			newWidth);
	}
	if (_title) {
		_title->moveToLeft(
			_back ? _st.back.width : _st.titlePosition.x(),
			_st.titlePosition.y(),
			newWidth);
	}
}

void TopBar::updateSelectionControlsGeometry(int newWidth) {
	if (!_selectionText) {
		return;
	}

	auto right = _st.mediaActionsSkip;
	if (_canDelete) {
		_delete->moveToRight(right, 0, newWidth);
		right += _delete->width();
	}
	_forward->moveToRight(right, 0, newWidth);
	right += _forward->width();

	auto left = 0;
	_cancelSelection->moveToLeft(left, 0);
	left += _cancelSelection->width();

	const auto top = 0;
	const auto availableWidth = newWidth - left - right;
	_selectionText->resizeToNaturalWidth(availableWidth);
	_selectionText->moveToLeft(
		left,
		top,
		newWidth);
}

void TopBar::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto highlight = _a_highlight.current(ms, _highlight ? 1. : 0.);
	if (_highlight && !_a_highlight.animating()) {
		_highlight = false;
		startHighlightAnimation();
	}
	auto brush = anim::brush(_st.bg, _st.highlightBg, highlight);
	p.fillRect(e->rect(), brush);
}

void TopBar::highlight() {
	_highlight = true;
	startHighlightAnimation();
}

void TopBar::startHighlightAnimation() {
	_a_highlight.start(
		[this] { update(); },
		_highlight ? 0. : 1.,
		_highlight ? 1. : 0.,
		_st.highlightDuration);
}

void TopBar::updateControlsVisibility(anim::type animated) {
	for (auto i = _updateControlCallbacks.begin(); i != _updateControlCallbacks.end();) {
		auto &&[widget, callback] = *i;
		if (!callback(animated)) {
			i = _updateControlCallbacks.erase(i);
		} else {
			++i;
		}
	}
}

void TopBar::setSelectedItems(SelectedItems &&items) {
	auto wasSelectionMode = selectionMode();
	_selectedItems = std::move(items);
	if (selectionMode()) {
		if (_selectionText) {
			updateSelectionState();
			if (!wasSelectionMode) {
				_selectionText->entity()->finishAnimating();
			}
		} else {
			createSelectionControls();
		}
	}
	updateControlsVisibility(anim::type::normal);
}

SelectedItems TopBar::takeSelectedItems() {
	_canDelete = false;
	return std::move(_selectedItems);
}

rpl::producer<> TopBar::cancelSelectionRequests() const {
	return _cancelSelectionClicks.events();
}

void TopBar::updateSelectionState() {
	Expects(_selectionText && _delete);

	_canDelete = computeCanDelete();
	_selectionText->entity()->setValue(generateSelectedText());
	_delete->toggle(_canDelete, anim::type::instant);

	updateSelectionControlsGeometry(width());
}

void TopBar::createSelectionControls() {
	auto wrap = [&](auto created) {
		registerToggleControlCallback(
			created,
			[this] { return selectionMode(); });
		created->toggle(false, anim::type::instant);
		return created;
	};
	_canDelete = computeCanDelete();
	_cancelSelection = wrap(Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		this,
		object_ptr<Ui::IconButton>(this, _st.mediaCancel),
		st::infoTopBarScale));
	_cancelSelection->setDuration(st::infoTopBarDuration);
	_cancelSelection->entity()->clicks(
	) | rpl::start_to_stream(
		_cancelSelectionClicks,
		_cancelSelection->lifetime());
	_selectionText = wrap(Ui::CreateChild<Ui::FadeWrap<Ui::LabelWithNumbers>>(
		this,
		object_ptr<Ui::LabelWithNumbers>(
			this,
			_st.title,
			_st.titlePosition.y(),
			generateSelectedText()),
		st::infoTopBarScale));
	_selectionText->setDuration(st::infoTopBarDuration);
	_selectionText->entity()->resize(0, _st.height);
	_forward = wrap(Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		this,
		object_ptr<Ui::IconButton>(this, _st.mediaForward),
		st::infoTopBarScale));
	_forward->setDuration(st::infoTopBarDuration);
	_forward->entity()->addClickHandler([this] { performForward(); });
	_delete = wrap(Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		this,
		object_ptr<Ui::IconButton>(this, _st.mediaDelete),
		st::infoTopBarScale));
	registerToggleControlCallback(
		_delete.data(),
		[this] { return selectionMode() && _canDelete; });
	_delete->setDuration(st::infoTopBarDuration);
	_delete->entity()->addClickHandler([this] { performDelete(); });
	_delete->entity()->setVisible(_canDelete);

	updateControlsGeometry(width());
}

bool TopBar::computeCanDelete() const {
	return ranges::find_if(
		_selectedItems.list,
		[](const SelectedItem &item) { return !item.canDelete; }
	) == _selectedItems.list.end();
}

Ui::StringWithNumbers TopBar::generateSelectedText() const {
	using Data = Ui::StringWithNumbers;
	using Type = Storage::SharedMediaType;
	auto phrase = [&] {
		switch (_selectedItems.type) {
		case Type::Photo: return lng_media_selected_photo__generic<Data>;
		case Type::Video: return lng_media_selected_video__generic<Data>;
		case Type::File: return lng_media_selected_file__generic<Data>;
		case Type::MusicFile: return lng_media_selected_song__generic<Data>;
		case Type::Link: return lng_media_selected_link__generic<Data>;
		case Type::VoiceFile: return lng_media_selected_audio__generic<Data>;
//		case Type::RoundFile: return lng_media_selected_round__generic<Data>;
		}
		Unexpected("Type in TopBar::generateSelectedText()");
	}();
	return phrase(lt_count, _selectedItems.list.size());
}

bool TopBar::selectionMode() const {
	return !_selectedItems.list.empty();
}

bool TopBar::searchMode() const {
	return _searchModeAvailable && _searchModeEnabled;
}

MessageIdsList TopBar::collectItems() const {
	return ranges::view::all(
		_selectedItems.list
	) | ranges::view::transform([](auto &&item) {
		return item.msgId;
	}) | ranges::view::filter([](const FullMsgId &msgId) {
		return App::histItemById(msgId) != nullptr;
	}) | ranges::to_vector;
}

void TopBar::performForward() {
	auto items = collectItems();
	if (items.empty()) {
		_cancelSelectionClicks.fire({});
		return;
	}
	Window::ShowForwardMessagesBox(std::move(items), [weak = make_weak(this)]{
		if (weak) {
			weak->_cancelSelectionClicks.fire({});
		}
	});
}

void TopBar::performDelete() {
	auto items = collectItems();
	if (items.empty()) {
		_cancelSelectionClicks.fire({});
	} else {
		Ui::show(Box<DeleteMessagesBox>(std::move(items)));
	}
}

rpl::producer<QString> TitleValue(
		const Section &section,
		not_null<PeerData*> peer,
		bool isStackBottom) {
	return Lang::Viewer([&] {
		switch (section.type()) {
		case Section::Type::Profile:
			if (auto user = peer->asUser()) {
				return user->botInfo
					? lng_info_bot_title
					: lng_info_user_title;
			} else if (auto channel = peer->asChannel()) {
				return channel->isMegagroup()
					? lng_info_group_title
					: lng_info_channel_title;
			} else if (peer->isChat()) {
				return lng_info_group_title;
			}
			Unexpected("Bad peer type in Info::TitleValue()");

		case Section::Type::Media:
			if (peer->isSelf() && isStackBottom) {
				return lng_profile_shared_media;
			}
			switch (section.mediaType()) {
			case Section::MediaType::Photo:
				return lng_media_type_photos;
			case Section::MediaType::Video:
				return lng_media_type_videos;
			case Section::MediaType::MusicFile:
				return lng_media_type_songs;
			case Section::MediaType::File:
				return lng_media_type_files;
			case Section::MediaType::VoiceFile:
				return lng_media_type_audios;
			case Section::MediaType::Link:
				return lng_media_type_links;
			case Section::MediaType::RoundFile:
				return lng_media_type_rounds;
			}
			Unexpected("Bad media type in Info::TitleValue()");

		case Section::Type::CommonGroups:
			return lng_profile_common_groups_section;

		case Section::Type::Members:
			return lng_profile_participants_section;

		}
		Unexpected("Bad section type in Info::TitleValue()");
	}());
}

} // namespace Info
