/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_top_bar.h"

#include <rpl/never.h>
#include <rpl/merge.h>
#include "lang/lang_keys.h"
#include "lang/lang_numbers_animation.h"
#include "info/info_wrap_widget.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_values.h"
#include "storage/storage_shared_media.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/search_field_controller.h"
#include "window/window_peer_menu.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "styles/style_info.h"

namespace Info {

TopBar::TopBar(
	QWidget *parent,
	not_null<Window::SessionNavigation*> navigation,
	const style::InfoTopBar &st,
	SelectedItems &&selectedItems)
: RpWidget(parent)
, _navigation(navigation)
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
		weak = Ui::MakeWeak(guard),
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
	_back->entity()->clicks(
	) | rpl::to_empty
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

void TopBar::forceButtonVisibility(
		Ui::FadeWrap<Ui::RpWidget> *button,
		rpl::producer<bool> shown) {
	_updateControlCallbacks.erase(button);
	button->toggleOn(std::move(shown));
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
	search->addClickHandler([=] { showSearch(); });
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

void TopBar::showSearch() {
	_searchModeEnabled = true;
	updateControlsVisibility(anim::type::normal);
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

	auto highlight = _a_highlight.value(_highlight ? 1. : 0.);
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
	) | rpl::to_empty
	| rpl::start_to_stream(
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
	return ranges::all_of(_selectedItems.list, &SelectedItem::canDelete);
}

Ui::StringWithNumbers TopBar::generateSelectedText() const {
	using Type = Storage::SharedMediaType;
	const auto phrase = [&] {
		switch (_selectedItems.type) {
		case Type::Photo: return tr::lng_media_selected_photo;
		case Type::Video: return tr::lng_media_selected_video;
		case Type::File: return tr::lng_media_selected_file;
		case Type::MusicFile: return tr::lng_media_selected_song;
		case Type::Link: return tr::lng_media_selected_link;
		case Type::RoundVoiceFile: return tr::lng_media_selected_audio;
		}
		Unexpected("Type in TopBar::generateSelectedText()");
	}();
	return phrase(
		tr::now,
		lt_count,
		_selectedItems.list.size(),
		Ui::StringWithNumbers::FromString);
}

bool TopBar::selectionMode() const {
	return !_selectedItems.list.empty();
}

bool TopBar::searchMode() const {
	return _searchModeAvailable && _searchModeEnabled;
}

MessageIdsList TopBar::collectItems() const {
	return ranges::views::all(
		_selectedItems.list
	) | ranges::views::transform([](auto &&item) {
		return item.msgId;
	}) | ranges::views::filter([&](FullMsgId msgId) {
		return _navigation->session().data().message(msgId) != nullptr;
	}) | ranges::to_vector;
}

void TopBar::performForward() {
	auto items = collectItems();
	if (items.empty()) {
		_cancelSelectionClicks.fire({});
		return;
	}
	Window::ShowForwardMessagesBox(
		_navigation,
		std::move(items),
		[weak = Ui::MakeWeak(this)] {
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
		auto box = Box<DeleteMessagesBox>(
			&_navigation->session(),
			std::move(items));
		box->setDeleteConfirmedCallback([weak = Ui::MakeWeak(this)] {
			if (weak) {
				weak->_cancelSelectionClicks.fire({});
			}
		});
		_navigation->parentController()->show(std::move(box));
	}
}

rpl::producer<QString> TitleValue(
		const Section &section,
		Key key,
		bool isStackBottom) {
	const auto peer = key.peer();

	switch (section.type()) {
	case Section::Type::Profile:
		if (const auto user = peer->asUser()) {
			return (user->isBot() && !user->isSupport())
				? tr::lng_info_bot_title()
				: tr::lng_info_user_title();
		} else if (const auto channel = peer->asChannel()) {
			return channel->isMegagroup()
				? tr::lng_info_group_title()
				: tr::lng_info_channel_title();
		} else if (peer->isChat()) {
			return tr::lng_info_group_title();
		}
		Unexpected("Bad peer type in Info::TitleValue()");

	case Section::Type::Media:
		if (peer->sharedMediaInfo() && isStackBottom) {
			return tr::lng_profile_shared_media();
		}
		switch (section.mediaType()) {
		case Section::MediaType::Photo:
			return tr::lng_media_type_photos();
		case Section::MediaType::Video:
			return tr::lng_media_type_videos();
		case Section::MediaType::MusicFile:
			return tr::lng_media_type_songs();
		case Section::MediaType::File:
			return tr::lng_media_type_files();
		case Section::MediaType::RoundVoiceFile:
			return tr::lng_media_type_audios();
		case Section::MediaType::Link:
			return tr::lng_media_type_links();
		case Section::MediaType::RoundFile:
			return tr::lng_media_type_rounds();
		}
		Unexpected("Bad media type in Info::TitleValue()");

	case Section::Type::CommonGroups:
		return tr::lng_profile_common_groups_section();

	case Section::Type::Members:
		if (const auto channel = peer->asChannel()) {
			return channel->isMegagroup()
				? tr::lng_profile_participants_section()
				: tr::lng_profile_subscribers_section();
		}
		return tr::lng_profile_participants_section();

	case Section::Type::Settings:
		switch (section.settingsType()) {
		case Section::SettingsType::Main:
			return tr::lng_menu_settings();
		case Section::SettingsType::Information:
			return tr::lng_settings_section_info();
		case Section::SettingsType::Notifications:
			return tr::lng_settings_section_notify();
		case Section::SettingsType::PrivacySecurity:
			return tr::lng_settings_section_privacy();
		case Section::SettingsType::Advanced:
			return tr::lng_settings_advanced();
		case Section::SettingsType::Chat:
			return tr::lng_settings_section_chat_settings();
		case Section::SettingsType::Folders:
			return tr::lng_filters_title();
		case Section::SettingsType::Calls:
			return tr::lng_settings_section_call_settings();
		}
		Unexpected("Bad settings type in Info::TitleValue()");

	case Section::Type::PollResults:
		return key.poll()->quiz()
			? tr::lng_polls_quiz_results_title()
			: tr::lng_polls_poll_results_title();
	}
	Unexpected("Bad section type in Info::TitleValue()");
}

} // namespace Info
