/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_folders.h"

#include "boxes/filters/edit_filter_box.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_peer.h"
#include "data/data_chat_filters.h"
#include "history/history.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/text/text_utilities.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "ui/filter_icons.h"
#include "settings/settings_common.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "app.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"

namespace Settings {
namespace {

constexpr auto kFiltersLimit = 10;

using Flag = Data::ChatFilter::Flag;
using Flags = Data::ChatFilter::Flags;

class FilterRowButton final : public Ui::RippleButton {
public:
	FilterRowButton(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		const Data::ChatFilter &filter);
	FilterRowButton(
		not_null<QWidget*> parent,
		const Data::ChatFilter &filter,
		const QString &description);

	void setRemoved(bool removed);
	void updateData(const Data::ChatFilter &filter);
	void updateCount(const Data::ChatFilter &filter);

	[[nodiscard]] rpl::producer<> removeRequests() const;
	[[nodiscard]] rpl::producer<> restoreRequests() const;
	[[nodiscard]] rpl::producer<> addRequests() const;

private:
	enum class State {
		Suggested,
		Removed,
		Normal,
	};

	FilterRowButton(
		not_null<QWidget*> parent,
		Main::Session *session,
		const Data::ChatFilter &filter,
		const QString &description,
		State state);

	void paintEvent(QPaintEvent *e) override;

	void setup(const Data::ChatFilter &filter, const QString &status);
	void setState(State state, bool force = false);
	void updateButtonsVisibility();

	Main::Session *_session = nullptr;

	Ui::IconButton _remove;
	Ui::RoundButton _restore;
	Ui::RoundButton _add;

	Ui::Text::String _title;
	QString _status;
	Ui::FilterIcon _icon = Ui::FilterIcon();

	State _state = State::Normal;

};

struct FilterRow {
	not_null<FilterRowButton*> button;
	Data::ChatFilter filter;
	bool removed = false;
	bool added = false;
	bool postponedCountUpdate = false;
};

[[nodiscard]] int CountFilterChats(
		not_null<Main::Session*> session,
		const Data::ChatFilter &filter) {
	auto result = 0;
	const auto addList = [&](not_null<Dialogs::MainList*> list) {
		for (const auto &entry : list->indexed()->all()) {
			if (const auto history = entry->history()) {
				if (filter.contains(history)) {
					++result;
				}
			}
		}
	};
	addList(session->data().chatsList());
	const auto folderId = Data::Folder::kId;
	if (const auto folder = session->data().folderLoaded(folderId)) {
		addList(folder->chatsList());
	}
	return result;
}

[[nodiscard]] int ComputeCount(
		not_null<Main::Session*> session,
		const Data::ChatFilter &filter,
		bool check = false) {
	const auto &list = session->data().chatsFilters().list();
	const auto id = filter.id();
	const auto i = ranges::find(list, id, &Data::ChatFilter::id);
	if (i != end(list)
		&& (!check
			|| (i->flags() == filter.flags()
				&& i->always() == filter.always()
				&& i->never() == filter.never()))) {
		const auto chats = session->data().chatsFilters().chatsList(id);
		return chats->indexed()->size();
	}
	return CountFilterChats(session, filter);
}

[[nodiscard]] QString ComputeCountString(
		not_null<Main::Session*> session,
		const Data::ChatFilter &filter,
		bool check = false) {
	const auto count = ComputeCount(session, filter, check);
	return count
		? tr::lng_filters_chats_count(tr::now, lt_count_short, count)
		: tr::lng_filters_no_chats(tr::now);
}

FilterRowButton::FilterRowButton(
	not_null<QWidget*> parent,
	not_null<Main::Session*> session,
	const Data::ChatFilter &filter)
: FilterRowButton(
	parent,
	session,
	filter,
	ComputeCountString(session, filter),
	State::Normal) {
}

FilterRowButton::FilterRowButton(
	not_null<QWidget*> parent,
	const Data::ChatFilter &filter,
	const QString &description)
: FilterRowButton(parent, nullptr, filter, description, State::Suggested) {
}

FilterRowButton::FilterRowButton(
	not_null<QWidget*> parent,
	Main::Session *session,
	const Data::ChatFilter &filter,
	const QString &status,
	State state)
: RippleButton(parent, st::defaultRippleAnimation)
, _session(session)
, _remove(this, st::filtersRemove)
, _restore(this, tr::lng_filters_restore(), st::stickersUndoRemove)
, _add(this, tr::lng_filters_recommended_add(), st::stickersTrendingAdd)
, _state(state) {
	setup(filter, status);
}

void FilterRowButton::setRemoved(bool removed) {
	setState(removed ? State::Removed : State::Normal);
}

void FilterRowButton::updateData(const Data::ChatFilter &filter) {
	Expects(_session != nullptr);

	_title.setText(st::contactsNameStyle, filter.title());
	_icon = Ui::ComputeFilterIcon(filter);
	updateCount(filter);
}

void FilterRowButton::updateCount(const Data::ChatFilter &filter) {
	_status = ComputeCountString(_session, filter, true);
	update();
}

void FilterRowButton::setState(State state, bool force) {
	if (!force && _state == state) {
		return;
	}
	_state = state;
	setPointerCursor(_state == State::Normal);
	setDisabled(_state != State::Normal);
	updateButtonsVisibility();
	update();
}

void FilterRowButton::setup(
		const Data::ChatFilter &filter,
		const QString &status) {
	resize(width(), st::defaultPeerListItem.height);

	_title.setText(st::contactsNameStyle, filter.title());
	_status = status;
	_icon = Ui::ComputeFilterIcon(filter);

	setState(_state, true);

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto right = st::contactsPadding.right()
			+ st::contactsCheckPosition.x();
		const auto width = size.width();
		const auto height = size.height();
		_restore.moveToRight(right, (height - _restore.height()) / 2, width);
		_add.moveToRight(right, (height - _add.height()) / 2, width);
		const auto skipped = right - st::stickersRemoveSkip;
		_remove.moveToRight(skipped, (height - _remove.height()) / 2, width);
	}, lifetime());
}

void FilterRowButton::updateButtonsVisibility() {
	_remove.setVisible(_state == State::Normal);
	_restore.setVisible(_state == State::Removed);
	_add.setVisible(_state == State::Suggested);
}

rpl::producer<> FilterRowButton::removeRequests() const {
	return _remove.clicks() | rpl::to_empty;
}

rpl::producer<> FilterRowButton::restoreRequests() const {
	return _restore.clicks() | rpl::to_empty;
}

rpl::producer<> FilterRowButton::addRequests() const {
	return _add.clicks() | rpl::to_empty;
}

void FilterRowButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto over = isOver() || isDown();
	if (_state == State::Normal) {
		if (over) {
			p.fillRect(e->rect(), st::windowBgOver);
		}
		RippleButton::paintRipple(p, 0, 0);
	} else if (_state == State::Removed) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	}

	const auto left = (_state == State::Suggested)
		? st::settingsSubsectionTitlePadding.left()
		: st::settingsFilterIconSkip;
	const auto buttonsLeft = std::min(
		_add.x(),
		std::min(_remove.x(), _restore.x()));
	const auto availableWidth = buttonsLeft - left;

	p.setPen(st::contactsNameFg);
	_title.drawLeftElided(
		p,
		left,
		st::contactsPadding.top() + st::contactsNameTop,
		availableWidth,
		width());

	p.setFont(st::contactsStatusFont);
	p.setPen(st::contactsStatusFg);
	p.drawTextLeft(
		left,
		st::contactsPadding.top() + st::contactsStatusTop,
		width(),
		_status);

	if (_state != State::Suggested) {
		const auto icon = Ui::LookupFilterIcon(_icon).normal;
		icon->paint(
			p,
			st::settingsFilterIconLeft,
			(height() - icon->height()) / 2,
			width(),
			(over ? st::menuIconFgOver : st::menuIconFg)->c);
	}
}

[[nodiscard]] Fn<void()> SetupFoldersContent(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	auto &lifetime = container->lifetime();

	const auto session = &controller->session();
	AddSkip(container, st::settingsSectionSkip);
	AddSubsectionTitle(container, tr::lng_filters_subtitle());

	const auto rows = lifetime.make_state<std::vector<FilterRow>>();
	const auto rowsCount = lifetime.make_state<rpl::variable<int>>();
	const auto find = [=](not_null<FilterRowButton*> button) {
		const auto i = ranges::find(*rows, button, &FilterRow::button);
		Assert(i != end(*rows));
		return &*i;
	};
	const auto showLimitReached = [=] {
		const auto removed = ranges::count_if(*rows, &FilterRow::removed);
		if (rows->size() < kFiltersLimit + removed) {
			return false;
		}
		controller->window().showToast(tr::lng_filters_limit(tr::now));
		return true;
	};
	const auto wrap = container->add(object_ptr<Ui::VerticalLayout>(
		container));
	const auto addFilter = [=](const Data::ChatFilter &filter) {
		const auto button = wrap->add(
			object_ptr<FilterRowButton>(wrap, session, filter));
		button->removeRequests(
		) | rpl::start_with_next([=] {
			button->setRemoved(true);
			find(button)->removed = true;
		}, button->lifetime());
		button->restoreRequests(
		) | rpl::start_with_next([=] {
			if (showLimitReached()) {
				return;
			}
			button->setRemoved(false);
			find(button)->removed = false;
		}, button->lifetime());
		button->setClickedCallback([=] {
			const auto found = find(button);
			if (found->removed) {
				return;
			}
			const auto doneCallback = [=](const Data::ChatFilter &result) {
				find(button)->filter = result;
				button->updateData(result);
			};
			controller->window().show(Box(
				EditFilterBox,
				controller,
				found->filter,
				crl::guard(button, doneCallback)));
		});
		rows->push_back({ button, filter });
		*rowsCount = rows->size();

		const auto filters = &controller->session().data().chatsFilters();
		const auto id = filter.id();
		if (ranges::contains(filters->list(), id, &Data::ChatFilter::id)) {
			filters->chatsList(id)->fullSize().changes(
			) | rpl::start_with_next([=] {
				const auto found = find(button);
				if (found->postponedCountUpdate) {
					return;
				}
				found->postponedCountUpdate = true;
				Ui::PostponeCall(button, [=] {
					const auto &list = filters->list();
					const auto i = ranges::find(
						list,
						id,
						&Data::ChatFilter::id);
					if (i == end(list)) {
						return;
					}
					const auto found = find(button);
					const auto &now = found->filter;
					if ((i->flags() != now.flags())
						|| (i->always() != now.always())
						|| (i->never() != now.never())) {
						return;
					}
					button->updateCount(now);
					found->postponedCountUpdate = false;
				});
			}, button->lifetime());
		}

		wrap->resizeToWidth(container->width());
	};
	const auto &list = session->data().chatsFilters().list();
	for (const auto &filter : list) {
		addFilter(filter);
	}

	AddButton(
		container,
		tr::lng_filters_create() | Ui::Text::ToUpper(),
		st::settingsUpdate
	)->setClickedCallback([=] {
		if (showLimitReached()) {
			return;
		}
		const auto doneCallback = [=](const Data::ChatFilter &result) {
			addFilter(result);
		};
		controller->window().show(Box(
			EditFilterBox,
			controller,
			Data::ChatFilter(),
			crl::guard(container, doneCallback)));
	});
	AddSkip(container);
	const auto emptyAbout = container->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_filters_about(),
				st::boxDividerLabel),
			st::settingsDividerLabelPadding)
	)->setDuration(0);
	const auto nonEmptyAbout = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container))
	)->setDuration(0);
	const auto aboutRows = nonEmptyAbout->entity();
	AddDividerText(aboutRows, tr::lng_filters_about());
	AddSkip(aboutRows);
	AddSubsectionTitle(aboutRows, tr::lng_filters_recommended());

	const auto suggested = lifetime.make_state<rpl::variable<int>>();
	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		session->data().chatsFilters().suggestedUpdated()
	) | rpl::map([=] {
		return session->data().chatsFilters().suggestedFilters();
	}) | rpl::filter([=](const std::vector<Data::SuggestedFilter> &list) {
		return !list.empty();
	}) | rpl::take(
		1
	) | rpl::start_with_next([=](
			const std::vector<Data::SuggestedFilter> &suggestions) {
		for (const auto &suggestion : suggestions) {
			const auto &filter = suggestion.filter;
			if (ranges::contains(*rows, filter, &FilterRow::filter)) {
				continue;
			}
			*suggested = suggested->current() + 1;
			const auto button = aboutRows->add(object_ptr<FilterRowButton>(
				aboutRows,
				filter,
				suggestion.description));
			button->addRequests(
				) | rpl::start_with_next([=] {
				if (showLimitReached()) {
					return;
				}
				addFilter(filter);
				*suggested = suggested->current() - 1;
				delete button;
			}, button->lifetime());
		}
		aboutRows->resizeToWidth(container->width());
		AddSkip(aboutRows, st::settingsSectionSkip);
	}, aboutRows->lifetime());

	using namespace rpl::mappers;
	auto showSuggestions = rpl::combine(
		suggested->value(),
		rowsCount->value()
	) | rpl::map(_1 > 0 && _2 < kFiltersLimit);
	emptyAbout->toggleOn(rpl::duplicate(
		showSuggestions
	) | rpl::map(!_1));
	nonEmptyAbout->toggleOn(std::move(showSuggestions));

	const auto prepareGoodIdsForNewFilters = [=] {
		const auto &list = session->data().chatsFilters().list();

		auto localId = 1;
		const auto chooseNextId = [&] {
			++localId;
			while (ranges::contains(list, localId, &Data::ChatFilter::id)) {
				++localId;
			}
			return localId;
		};
		auto result = base::flat_map<not_null<FilterRowButton*>, FilterId>();
		for (auto &row : *rows) {
			const auto id = row.filter.id();
			if (row.removed) {
				continue;
			} else if (!ranges::contains(list, id, &Data::ChatFilter::id)) {
				result.emplace(row.button, chooseNextId());
			}
		}
		return result;
	};

	return [=] {
		auto ids = prepareGoodIdsForNewFilters();

		using Requests = std::vector<MTPmessages_UpdateDialogFilter>;
		auto addRequests = Requests(), removeRequests = Requests();
		auto &realFilters = session->data().chatsFilters();
		const auto &list = realFilters.list();
		auto order = std::vector<FilterId>();
		order.reserve(rows->size());
		for (const auto &row : *rows) {
			const auto id = row.filter.id();
			const auto removed = row.removed;
			const auto i = ranges::find(list, id, &Data::ChatFilter::id);
			if (removed && i == end(list)) {
				continue;
			} else if (!removed && i != end(list) && *i == row.filter) {
				order.push_back(id);
				continue;
			}
			const auto newId = ids.take(row.button).value_or(id);
			const auto tl = removed
				? MTPDialogFilter()
				: row.filter.tl(newId);
			const auto request = MTPmessages_UpdateDialogFilter(
				MTP_flags(removed
					? MTPmessages_UpdateDialogFilter::Flag(0)
					: MTPmessages_UpdateDialogFilter::Flag::f_filter),
				MTP_int(newId),
				tl);
			if (removed) {
				removeRequests.push_back(request);
			} else {
				addRequests.push_back(request);
				order.push_back(newId);
			}
			realFilters.apply(MTP_updateDialogFilter(
				MTP_flags(removed
					? MTPDupdateDialogFilter::Flag(0)
					: MTPDupdateDialogFilter::Flag::f_filter),
				MTP_int(newId),
				tl));
		}
		auto previousId = mtpRequestId(0);
		auto &&requests = ranges::views::concat(removeRequests, addRequests);
		for (auto &request : requests) {
			previousId = session->api().request(
				std::move(request)
			).afterRequest(previousId).send();
		}
		if (!order.empty() && !addRequests.empty()) {
			realFilters.saveOrder(order, previousId);
		}
	};
}

} // namespace

Folders::Folders(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

Folders::~Folders() {
	if (!App::quitting()) {
		_save();
	}
}

void Folders::setupContent(not_null<Window::SessionController*> controller) {
	controller->session().data().chatsFilters().requestSuggested();

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	_save = SetupFoldersContent(controller, content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
