/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_top_bar.h"

#include "dialogs/ui/dialogs_stories_list.h"
#include "lang/lang_keys.h"
#include "info/info_wrap_widget.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_values.h"
#include "storage/storage_shared_media.h"
#include "boxes/delete_messages_box.h"
#include "boxes/peer_list_controllers.h"
#include "main/main_session.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/search_field_controller.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "styles/style_dialogs.h"
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
	if (_st.radius) {
		_roundRect.emplace(_st.radius, _st.bg);
	}
	setAttribute(Qt::WA_OpaquePaintEvent, !_roundRect);
	setSelectedItems(std::move(selectedItems));
	updateControlsVisibility(anim::type::instant);
}

template <typename Callback>
void TopBar::registerUpdateControlCallback(
		QObject *guard,
		Callback &&callback) {
	_updateControlCallbacks[guard] =[
		weak = base::make_weak(guard),
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

void TopBar::setTitle(TitleDescriptor descriptor) {
	if (_title) {
		delete _title;
	}
	if (_subtitle) {
		delete _subtitle;
	}
	const auto withSubtitle = !!descriptor.subtitle;
	if (withSubtitle) {
		_subtitle = Ui::CreateChild<Ui::FadeWrap<Ui::FlatLabel>>(
			this,
			object_ptr<Ui::FlatLabel>(
				this,
				std::move(descriptor.subtitle),
				_st.subtitle),
			st::infoTopBarScale);
		_subtitle->setDuration(st::infoTopBarDuration);
		_subtitle->toggle(
			!selectionMode() && !storiesTitle(),
			anim::type::instant);
		registerToggleControlCallback(_subtitle.data(), [=] {
			return !selectionMode() && !storiesTitle() && !searchMode();
		});
	}
	_title = Ui::CreateChild<Ui::FadeWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			std::move(descriptor.title),
			withSubtitle ? _st.titleWithSubtitle : _st.title),
		st::infoTopBarScale);
	_title->setDuration(st::infoTopBarDuration);
	_title->toggle(
		!selectionMode() && !storiesTitle(),
		anim::type::instant);
	registerToggleControlCallback(_title.data(), [=] {
		return !selectionMode() && !storiesTitle() && !searchMode();
	});

	if (_back) {
		_title->setAttribute(Qt::WA_TransparentForMouseEvents);
		if (_subtitle) {
			_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
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
	if (_subtitle) {
		_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	if (_storiesWrap) {
		_storiesWrap->raise();
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

void TopBar::checkBeforeCloseByEscape(Fn<void()> close) {
	if (_searchModeEnabled) {
		if (_searchField && !_searchField->empty()) {
			_searchField->setText({});
		} else {
			_searchModeEnabled = false;
			updateControlsVisibility(anim::type::normal);
		}
	} else {
		close();
	}
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

	cancel->addClickHandler([=] {
		if (!field->getLastText().isEmpty()) {
			field->setText(QString());
		} else {
			_searchModeEnabled = false;
			updateControlsVisibility(anim::type::normal);
		}
	});

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
	updateStoriesGeometry(newWidth);
}

void TopBar::updateDefaultControlsGeometry(int newWidth) {
	auto right = 0;
	for (auto &button : _buttons) {
		if (!button) {
			continue;
		}
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
		const auto x = _back
			? _st.back.width
			: _subtitle
			? _st.titleWithSubtitlePosition.x()
			: _st.titlePosition.x();
		const auto y = _subtitle
			? _st.titleWithSubtitlePosition.y()
			: _st.titlePosition.y();
		_title->moveToLeft(x, y, newWidth);
		if (_subtitle) {
			_subtitle->moveToLeft(
				_back ? _st.back.width : _st.subtitlePosition.x(),
				_st.subtitlePosition.y(),
				newWidth);
		}
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
	if (_canToggleStoryPin) {
		_toggleStoryInProfile->moveToRight(right, 0, newWidth);
		right += _toggleStoryInProfile->width();
		_toggleStoryPin->moveToRight(right, 0, newWidth);
		right += _toggleStoryPin->width();
	}
	if (_canForward) {
		_forward->moveToRight(right, 0, newWidth);
		right += _forward->width();
	}

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

void TopBar::updateStoriesGeometry(int newWidth) {
	if (!_stories) {
		return;
	}

	auto right = 0;
	for (auto &button : _buttons) {
		if (!button) {
			continue;
		}
		button->moveToRight(right, 0, newWidth);
		right += button->width();
	}
	const auto &small = st::dialogsStories;
	const auto wrapLeft = (_back ? _st.back.width : 0);
	const auto left = _back
		? 0
		: (_st.titlePosition.x() - small.left - small.photoLeft);
	const auto height = small.photo + 2 * small.photoTop;
	const auto top = _st.titlePosition.y()
		+ (_st.title.style.font->height - height) / 2;
	_stories->setLayoutConstraints({ left, top }, style::al_left);
	_storiesWrap->move(wrapLeft, 0);
}

void TopBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	auto highlight = _a_highlight.value(_highlight ? 1. : 0.);
	if (_highlight && !_a_highlight.animating()) {
		_highlight = false;
		startHighlightAnimation();
	}
	if (!_roundRect) {
		const auto brush = anim::brush(_st.bg, _st.highlightBg, highlight);
		p.fillRect(e->rect(), brush);
	} else if (highlight > 0.) {
		p.setPen(Qt::NoPen);
		p.setBrush(anim::brush(_st.bg, _st.highlightBg, highlight));
		p.drawRoundedRect(
			rect() + style::margins(0, 0, 0, _st.radius * 2),
			_st.radius,
			_st.radius);
	} else {
		_roundRect->paintSomeRounded(
			p,
			rect(),
			RectPart::TopLeft | RectPart::TopRight);
	}
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

void TopBar::setStories(rpl::producer<Dialogs::Stories::Content> content) {
	_storiesLifetime.destroy();
	delete _storiesWrap.data();
	if (content) {
		using namespace Dialogs::Stories;

		auto last = std::move(
			content
		) | rpl::start_spawning(_storiesLifetime);

		_storiesWrap = _storiesLifetime.make_state<
			Ui::FadeWrap<Ui::AbstractButton>
		>(this, object_ptr<Ui::AbstractButton>(this), st::infoTopBarScale);
		registerToggleControlCallback(
			_storiesWrap.data(),
			[this] { return _storiesCount > 0; });
		_storiesWrap->toggle(false, anim::type::instant);
		_storiesWrap->setDuration(st::infoTopBarDuration);

		const auto button = _storiesWrap->entity();
		const auto stories = Ui::CreateChild<List>(
			button,
			st::dialogsStoriesListInfo,
			rpl::duplicate(
				last
			) | rpl::filter([](const Content &content) {
				return !content.elements.empty();
			}));
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			button,
			QString(),
			_st.title);
		stories->setAttribute(Qt::WA_TransparentForMouseEvents);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		stories->geometryValue(
		) | rpl::start_with_next([=](QRect geometry) {
			const auto skip = _st.title.style.font->spacew;
			label->move(
				geometry.x() + geometry.width() + skip,
				_st.titlePosition.y());
		}, label->lifetime());
		rpl::combine(
			_storiesWrap->positionValue(),
			label->geometryValue()
		) | rpl::start_with_next([=] {
			button->resize(
				label->x() + label->width() + _st.titlePosition.x(),
				_st.height);
		}, button->lifetime());

		_stories = stories;
		_stories->clicks(
		) | rpl::start_to_stream(_storyClicks, _stories->lifetime());

		button->setClickedCallback([=] {
			_storyClicks.fire({});
		});

		rpl::duplicate(
			last
		) | rpl::start_with_next([=](const Content &content) {
			const auto count = content.total;
			if (_storiesCount != count) {
				const auto was = (_storiesCount > 0);
				_storiesCount = count;
				const auto now = (_storiesCount > 0);
				if (was != now) {
					updateControlsVisibility(anim::type::normal);
				}
				if (now) {
					label->setText(
						tr::lng_contacts_stories_status(
							tr::now,
							lt_count,
							_storiesCount));
				}
				updateControlsGeometry(width());
			}
		}, _storiesLifetime);

		_storiesLifetime.add([weak = base::make_weak(label)] {
			delete weak.get();
		});
	} else {
		_storiesCount = 0;
	}
	updateControlsVisibility(anim::type::instant);
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
	_canForward = false;
	return std::move(_selectedItems);
}

rpl::producer<SelectionAction> TopBar::selectionActionRequests() const {
	return _selectionActionRequests.events();
}

void TopBar::updateSelectionState() {
	Expects(_selectionText
		&& _delete
		&& _forward
		&& _toggleStoryInProfile
		&& _toggleStoryPin);

	_canDelete = computeCanDelete();
	_canForward = computeCanForward();
	_canUnpinStories = computeCanUnpinStories();
	_canToggleStoryPin = computeCanToggleStoryPin();
	_allStoriesInProfile = computeAllStoriesInProfile();
	_selectionText->entity()->setValue(generateSelectedText());
	_delete->toggle(_canDelete, anim::type::instant);
	_forward->toggle(_canForward, anim::type::instant);
	_toggleStoryInProfile->toggle(_canToggleStoryPin, anim::type::instant);
	_toggleStoryInProfile->entity()->setIconOverride(
		(_allStoriesInProfile
			? &_st.storiesArchive.icon
			: &_st.storiesSave.icon),
		(_allStoriesInProfile
			? &_st.storiesArchive.iconOver
			: &_st.storiesSave.iconOver));
	_toggleStoryPin->toggle(_canToggleStoryPin, anim::type::instant);
	_toggleStoryPin->entity()->setIconOverride(
		_canUnpinStories ? &_st.storiesUnpin.icon : nullptr,
		_canUnpinStories ? &_st.storiesUnpin.iconOver : nullptr);

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
	_canForward = computeCanForward();
	_canUnpinStories = computeCanUnpinStories();
	_canToggleStoryPin = computeCanToggleStoryPin();
	_allStoriesInProfile = computeAllStoriesInProfile();
	_cancelSelection = wrap(Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		this,
		object_ptr<Ui::IconButton>(this, _st.mediaCancel),
		st::infoTopBarScale));
	_cancelSelection->setDuration(st::infoTopBarDuration);
	_cancelSelection->entity()->clicks(
	) | rpl::map_to(
		SelectionAction::Clear
	) | rpl::start_to_stream(
		_selectionActionRequests,
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
	_selectionText->naturalWidthValue(
	) | rpl::skip(1) | rpl::start_with_next([=] {
		updateSelectionControlsGeometry(width());
	}, _selectionText->lifetime());

	_forward = wrap(Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		this,
		object_ptr<Ui::IconButton>(this, _st.mediaForward),
		st::infoTopBarScale));
	registerToggleControlCallback(
		_forward.data(),
		[this] { return selectionMode() && _canForward; });
	_forward->setDuration(st::infoTopBarDuration);
	_forward->entity()->clicks(
	) | rpl::map_to(
		SelectionAction::Forward
	) | rpl::start_to_stream(
		_selectionActionRequests,
		_cancelSelection->lifetime());
	_forward->entity()->setVisible(_canForward);

	_delete = wrap(Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
		this,
		object_ptr<Ui::IconButton>(this, _st.mediaDelete),
		st::infoTopBarScale));
	registerToggleControlCallback(
		_delete.data(),
		[this] { return selectionMode() && _canDelete; });
	_delete->setDuration(st::infoTopBarDuration);
	_delete->entity()->clicks(
	) | rpl::map_to(
		SelectionAction::Delete
	) | rpl::start_to_stream(
		_selectionActionRequests,
		_cancelSelection->lifetime());
	_delete->entity()->setVisible(_canDelete);

	_toggleStoryInProfile = wrap(
		Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
			this,
			object_ptr<Ui::IconButton>(
				this,
				_allStoriesInProfile ? _st.storiesArchive : _st.storiesSave),
			st::infoTopBarScale));
	registerToggleControlCallback(
		_toggleStoryInProfile.data(),
		[this] { return selectionMode() && _canToggleStoryPin; });
	_toggleStoryInProfile->setDuration(st::infoTopBarDuration);
	_toggleStoryInProfile->entity()->clicks(
	) | rpl::map([=] {
		return _allStoriesInProfile
			? SelectionAction::ToggleStoryToArchive
			: SelectionAction::ToggleStoryToProfile;
	}) | rpl::start_to_stream(
		_selectionActionRequests,
		_cancelSelection->lifetime());
	_toggleStoryInProfile->entity()->setVisible(_canToggleStoryPin);

	_toggleStoryPin = wrap(
		Ui::CreateChild<Ui::FadeWrap<Ui::IconButton>>(
			this,
			object_ptr<Ui::IconButton>(
				this,
				_st.storiesPin),
			st::infoTopBarScale));
	if (_canUnpinStories) {
		_toggleStoryPin->entity()->setIconOverride(
			_canUnpinStories ? &_st.storiesUnpin.icon : nullptr,
			_canUnpinStories ? &_st.storiesUnpin.iconOver : nullptr);
	}
	registerToggleControlCallback(
		_toggleStoryPin.data(),
		[this] { return selectionMode() && _canToggleStoryPin; });
	_toggleStoryPin->setDuration(st::infoTopBarDuration);
	_toggleStoryPin->entity()->clicks(
	) | rpl::map_to(
		SelectionAction::ToggleStoryPin
	) | rpl::start_to_stream(
		_selectionActionRequests,
		_cancelSelection->lifetime());
	_toggleStoryPin->entity()->setVisible(_canToggleStoryPin);

	updateControlsGeometry(width());
}

bool TopBar::computeCanDelete() const {
	return ranges::all_of(_selectedItems.list, &SelectedItem::canDelete);
}

bool TopBar::computeCanForward() const {
	return ranges::all_of(_selectedItems.list, &SelectedItem::canForward);
}

bool TopBar::computeCanUnpinStories() const {
	return ranges::any_of(_selectedItems.list, &SelectedItem::canUnpinStory);
}

bool TopBar::computeCanToggleStoryPin() const {
	return ranges::all_of(
		_selectedItems.list,
		&SelectedItem::canToggleStoryPin);
}

bool TopBar::computeAllStoriesInProfile() const {
	return ranges::all_of(
		_selectedItems.list,
		&SelectedItem::storyInProfile);
}

Ui::StringWithNumbers TopBar::generateSelectedText() const {
	return _selectedItems.title(_selectedItems.list.size());
}

bool TopBar::selectionMode() const {
	return !_selectedItems.list.empty();
}

bool TopBar::storiesTitle() const {
	return _storiesCount > 0;
}

bool TopBar::searchMode() const {
	return _searchModeAvailable && _searchModeEnabled;
}

void TopBar::performForward() {
	_selectionActionRequests.fire(SelectionAction::Forward);
}

void TopBar::performDelete() {
	_selectionActionRequests.fire(SelectionAction::Delete);
}

} // namespace Info
