/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/manage_filters_box.h"

#include "data/data_session.h"
#include "data/data_chat_filters.h"
#include "data/data_folder.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/text/text_utilities.h"
#include "settings/settings_common.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

namespace {

constexpr auto kRefreshSuggestedTimeout = 7200 * crl::time(1000);
constexpr auto kFiltersLimit = 10;

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
		const Data::ChatFilter &filter,
		const QString &description,
		State state);

	void paintEvent(QPaintEvent *e) override;

	void setup(const Data::ChatFilter &filter, const QString &status);
	void setState(State state, bool force = false);
	void updateButtonsVisibility();

	Ui::IconButton _remove;
	Ui::RoundButton _restore;
	Ui::RoundButton _add;

	Ui::Text::String _title;
	QString _status;

	State _state = State::Normal;

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
		const Data::ChatFilter &filter) {
	const auto &list = session->data().chatsFilters().list();
	const auto id = filter.id();
	if (ranges::contains(list, id, &Data::ChatFilter::id)) {
		const auto chats = session->data().chatsFilters().chatsList(id);
		return chats->indexed()->size();
	}
	return CountFilterChats(session, filter);
}

[[nodiscard]] QString ComputeCountString(
		not_null<Main::Session*> session,
		const Data::ChatFilter &filter) {
	const auto count = ComputeCount(session, filter);
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
	filter,
	ComputeCountString(session, filter),
	State::Normal) {
}

FilterRowButton::FilterRowButton(
	not_null<QWidget*> parent,
	const Data::ChatFilter &filter,
	const QString &description)
: FilterRowButton(parent, filter, description, State::Suggested) {
}

FilterRowButton::FilterRowButton(
	not_null<QWidget*> parent,
	const Data::ChatFilter &filter,
	const QString &status,
	State state)
: RippleButton(parent, st::defaultRippleAnimation)
, _remove(this, st::filtersRemove)
, _restore(this, tr::lng_filters_restore(), st::stickersUndoRemove)
, _add(this, tr::lng_filters_recommended_add(), st::stickersTrendingAdd)
, _state(state) {
	setup(filter, status);
}

void FilterRowButton::setRemoved(bool removed) {
	setState(removed ? State::Removed : State::Normal);
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
	return _remove.clicks() | rpl::map([] { return rpl::empty_value(); });
}

rpl::producer<> FilterRowButton::restoreRequests() const {
	return _restore.clicks() | rpl::map([] { return rpl::empty_value(); });
}

rpl::producer<> FilterRowButton::addRequests() const {
	return _add.clicks() | rpl::map([] { return rpl::empty_value(); });
}

void FilterRowButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	if (_state == State::Normal) {
		if (isOver() || isDown()) {
			p.fillRect(e->rect(), st::windowBgOver);
		}
		RippleButton::paintRipple(p, 0, 0);
	} else if (_state == State::Removed) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	}

	const auto left = st::settingsSubsectionTitlePadding.left();
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
}

} // namespace

ManageFiltersPrepare::ManageFiltersPrepare(
	not_null<Window::SessionController*> window)
: _window(window)
, _api(&_window->session().api()) {
}

ManageFiltersPrepare::~ManageFiltersPrepare() {
	if (_requestId) {
		_api->request(_requestId).cancel();
	}
}

void ManageFiltersPrepare::showBox() {
	if (_requestId) {
		return;
	}
	if (_suggestedLastReceived > 0
		&& crl::now() - _suggestedLastReceived < kRefreshSuggestedTimeout) {
		showBoxWithSuggested();
		return;
	}
	_requestId = _api->request(MTPmessages_GetSuggestedDialogFilters(
	)).done([=](const MTPVector<MTPDialogFilterSuggested> &data) {
		_requestId = 0;
		_suggestedLastReceived = crl::now();

		const auto owner = &_api->session().data();
		_suggested = ranges::view::all(
			data.v
		) | ranges::view::transform([&](const MTPDialogFilterSuggested &f) {
			return f.match([&](const MTPDdialogFilterSuggested &data) {
				return Suggested{
					Data::ChatFilter::FromTL(data.vfilter(), owner),
					qs(data.vdescription())
				};
			});
		}) | ranges::to_vector;

		showBoxWithSuggested();
	}).fail([=](const RPCError &error) {
		_requestId = 0;
		_suggestedLastReceived = crl::now() + kRefreshSuggestedTimeout / 2;

		showBoxWithSuggested();
	}).send();
}

void ManageFiltersPrepare::showBoxWithSuggested() {
	_window->window().show(Box(CreateBox, _window, _suggested));
}

void ManageFiltersPrepare::CreateBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		const std::vector<Suggested> &suggested) {
	struct FilterRow {
		not_null<FilterRowButton*> button;
		Data::ChatFilter filter;
		bool removed = false;
		bool added = false;
	};

	box->setTitle(tr::lng_filters_title());

	const auto session = &window->session();
	const auto content = box->verticalLayout();
	Settings::AddSubsectionTitle(content, tr::lng_filters_subtitle());

	const auto rows = box->lifetime().make_state<std::vector<FilterRow>>();
	const auto find = [=](not_null<FilterRowButton*> button) {
		const auto i = ranges::find(*rows, button, &FilterRow::button);
		Assert(i != end(*rows));
		return &*i;
	};
	const auto countNonRemoved = [=] {
		const auto removed = ranges::count_if(*rows, &FilterRow::removed);
		return rows->size() - removed;
	};
	const auto wrap = content->add(object_ptr<Ui::VerticalLayout>(content));
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
			if (countNonRemoved() < kFiltersLimit) {
				button->setRemoved(false);
				find(button)->removed = false;
			}
		}, button->lifetime());
		rows->push_back({ button, filter });
	};
	const auto &list = session->data().chatsFilters().list();
	for (const auto &filter : list) {
		addFilter(filter);
	}

	Settings::AddButton(
		content,
		tr::lng_filters_create() | Ui::Text::ToUpper(),
		st::settingsUpdate);
	Settings::AddSkip(content);
	if (suggested.empty()) {
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_filters_about(),
				st::boxDividerLabel),
			st::settingsDividerLabelPadding);
	} else {
		Settings::AddDividerText(content, tr::lng_filters_about());
		Settings::AddSkip(content);
		Settings::AddSubsectionTitle(content, tr::lng_filters_recommended());

		for (const auto &suggestion : suggested) {
			const auto filter = suggestion.filter;
			const auto already = [&] {
				for (const auto &entry : list) {
					if (entry.flags() == filter.flags()
						&& entry.always() == filter.always()
						&& entry.never() == filter.never()) {
						return true;
					}
				}
				return false;
			}();
			if (already) {
				continue;
			}
			const auto button = content->add(object_ptr<FilterRowButton>(
				content,
				filter,
				suggestion.description));
			button->addRequests(
			) | rpl::start_with_next([=] {
				addFilter(filter);
				delete button;
			}, button->lifetime());
		}
	}

	const auto prepareGoodIdsForNewFilters = [=] {
		const auto &list = session->data().chatsFilters().list();

		auto localId = 2;
		const auto chooseNextId = [&] {
			while (ranges::contains(list, localId, &Data::ChatFilter::id)) {
				++localId;
			}
			return localId;
		};
		auto result = base::flat_map<FilterId, FilterId>();
		for (auto &row : *rows) {
			const auto id = row.filter.id();
			if (row.removed) {
				continue;
			} else if (!ranges::contains(list, id, &Data::ChatFilter::id)) {
				result.emplace(row.filter.id(), chooseNextId());
			}
		}
		return result;
	};

	const auto save = [=] {
		auto ids = prepareGoodIdsForNewFilters();

		auto requests = std::deque<MTPmessages_UpdateDialogFilter>();
		auto &realFilters = session->data().chatsFilters();
		const auto &list = realFilters.list();
		auto order = QVector<MTPint>();
		for (const auto &row : *rows) {
			const auto id = row.filter.id();
			const auto removed = row.removed;
			if (removed
				&& !ranges::contains(list, id, &Data::ChatFilter::id)) {
				continue;
			}
			const auto newId = ids.take(id).value_or(id);
			const auto tl = removed ? MTPDialogFilter() : row.filter.tl();
			const auto request = MTPmessages_UpdateDialogFilter(
				MTP_flags(removed
					? MTPmessages_UpdateDialogFilter::Flag(0)
					: MTPmessages_UpdateDialogFilter::Flag::f_filter),
				MTP_int(newId),
				tl);
			if (removed) {
				requests.push_front(request);
			} else {
				requests.push_back(request);
				order.push_back(MTP_int(newId));
			}
			realFilters.apply(MTP_updateDialogFilter(
				MTP_flags(removed
					? MTPDupdateDialogFilter::Flag(0)
					: MTPDupdateDialogFilter::Flag::f_filter),
				MTP_int(newId),
				tl));
		}
		auto previousId = mtpRequestId(0);
		for (auto &request : requests) {
			previousId = session->api().request(
				std::move(request)
			).afterRequest(previousId).send();
		}
		if (!order.isEmpty()) {
			realFilters.apply(
				MTP_updateDialogFilterOrder(MTP_vector(order)));
			session->api().request(MTPmessages_UpdateDialogFiltersOrder(
				MTP_vector(order)
			)).afterRequest(previousId).send();
		}
		box->closeBox();
	};
	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}
