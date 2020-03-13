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
#include "data/data_peer.h"
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
#include "settings/settings_common.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"

namespace {

constexpr auto kRefreshSuggestedTimeout = 7200 * crl::time(1000);
constexpr auto kFiltersLimit = 10;
constexpr auto kMaxFilterTitleLength = 20;

using namespace Settings;

using Flag = Data::ChatFilter::Flag;
using Flags = Data::ChatFilter::Flags;
using ExceptionPeersRef = const base::flat_set<not_null<History*>> &;
using ExceptionPeersGetter = ExceptionPeersRef(Data::ChatFilter::*)() const;

constexpr auto kAllTypes = {
	Flag::Contacts,
	Flag::NonContacts,
	Flag::Groups,
	Flag::Channels,
	Flag::Bots,
	Flag::NoMuted,
	Flag::NoArchived,
	Flag::NoRead
};

class FilterChatsPreview final : public Ui::RpWidget {
public:
	FilterChatsPreview(
		not_null<QWidget*> parent,
		Flags flags,
		const base::flat_set<not_null<History*>> &peers);

	[[nodiscard]] rpl::producer<Flag> flagRemoved() const;
	[[nodiscard]] rpl::producer<not_null<History*>> peerRemoved() const;

	int resizeGetHeight(int newWidth) override;

private:
	using Button = base::unique_qptr<Ui::IconButton>;
	struct FlagButton {
		Flag flag = Flag();
		Button button;
	};
	struct PeerButton {
		not_null<History*> history;
		Button button;
	};

	void paintEvent(QPaintEvent *e) override;

	void setup(
		Flags flags,
		const base::flat_set<not_null<History*>> &peers);
	void refresh();
	void removeFlag(Flag flag);
	void removePeer(not_null<History*> history);
	void paintFlagIcon(QPainter &p, int left, int top, Flag flag) const;

	std::vector<FlagButton> _removeFlag;
	std::vector<PeerButton> _removePeer;

	rpl::event_stream<Flag> _flagRemoved;
	rpl::event_stream<not_null<History*>> _peerRemoved;

};

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

FilterChatsPreview::FilterChatsPreview(
	not_null<QWidget*> parent,
	Flags flags,
	const base::flat_set<not_null<History*>> &peers)
: RpWidget(parent) {
	setup(flags, peers);
}

void FilterChatsPreview::setup(
		Flags flags,
		const base::flat_set<not_null<History*>> &peers) {
	const auto makeButton = [&](Fn<void()> handler) {
		auto result = base::make_unique_q<Ui::IconButton>(
			this,
			st::windowFilterSmallRemove);
		result->setClickedCallback(std::move(handler));
		return result;
	};
	for (const auto flag : kAllTypes) {
		if (flags & flag) {
			_removeFlag.push_back({
				flag,
				makeButton([=] { removeFlag(flag); }) });
		}
	}
	for (const auto history : peers) {
		_removePeer.push_back({
			history,
			makeButton([=] { removePeer(history); }) });
	}
	refresh();
}

void FilterChatsPreview::refresh() {
	resizeToWidth(width());
}

int FilterChatsPreview::resizeGetHeight(int newWidth) {
	const auto right = st::windowFilterSmallRemoveRight;
	const auto add = (st::windowFilterSmallItem.height
		- st::windowFilterSmallRemove.height) / 2;
	auto top = 0;
	const auto moveNextButton = [&](not_null<Ui::IconButton*> button) {
		button->moveToRight(right, top + add, newWidth);
		top += st::windowFilterSmallItem.height;
	};
	for (const auto &[flag, button] : _removeFlag) {
		moveNextButton(button.get());
	}
	for (const auto &[history, button] : _removePeer) {
		moveNextButton(button.get());
	}
	return top;
}

[[nodiscard]] QString TypeName(Flag flag) {
	switch (flag) {
	case Flag::Contacts: return tr::lng_filters_type_contacts(tr::now);
	case Flag::NonContacts:
		return tr::lng_filters_type_non_contacts(tr::now);
	case Flag::Groups: return tr::lng_filters_type_groups(tr::now);
	case Flag::Channels: return tr::lng_filters_type_channels(tr::now);
	case Flag::Bots: return tr::lng_filters_type_bots(tr::now);
	case Flag::NoMuted: return tr::lng_filters_type_no_muted(tr::now);
	case Flag::NoArchived: return tr::lng_filters_type_no_archived(tr::now);
	case Flag::NoRead: return tr::lng_filters_type_no_read(tr::now);
	}
	Unexpected("Flag in TypeName.");
}

void FilterChatsPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto top = 0;
	const auto &st = st::windowFilterSmallItem;
	const auto iconLeft = st.photoPosition.x();
	const auto iconTop = st.photoPosition.y();
	const auto nameLeft = st.namePosition.x();
	p.setFont(st::windowFilterSmallItem.nameStyle.font);
	const auto nameTop = st.namePosition.y();
	for (const auto &[flag, button] : _removeFlag) {
		paintFlagIcon(p, iconLeft, top + iconTop, flag);

		p.setPen(st::contactsNameFg);
		p.drawTextLeft(nameLeft, top + nameTop, width(), TypeName(flag));
		top += st.height;
	}
	for (const auto &[history, button] : _removePeer) {
		history->peer->paintUserpicLeft(
			p,
			iconLeft,
			top + iconTop,
			width(),
			st.photoSize);
		history->peer->nameText().drawLeftElided(
			p,
			nameLeft,
			top + nameTop,
			button->x() - nameLeft,
			width());
		top += st.height;
	}
}

void FilterChatsPreview::paintFlagIcon(
		QPainter &p,
		int left,
		int top,
		Flag flag) const {
	const auto &color = [&]() -> const style::color& {
		switch (flag) {
		case Flag::Contacts: return st::historyPeer4UserpicBg;
		case Flag::NonContacts: return st::historyPeer7UserpicBg;
		case Flag::Groups: return st::historyPeer2UserpicBg;
		case Flag::Channels: return st::historyPeer1UserpicBg;
		case Flag::Bots: return st::historyPeer6UserpicBg;
		case Flag::NoMuted: return st::historyPeer6UserpicBg;
		case Flag::NoArchived: return st::historyPeer4UserpicBg;
		case Flag::NoRead: return st::historyPeer7UserpicBg;
		}
		Unexpected("Flag in color paintFlagIcon.");
	}();
	const auto &icon = [&]() -> const style::icon& {
		switch (flag) {
		case Flag::Contacts: return st::windowFilterTypeContacts;
		case Flag::NonContacts: return st::windowFilterTypeNonContacts;
		case Flag::Groups: return st::windowFilterTypeGroups;
		case Flag::Channels: return st::windowFilterTypeChannels;
		case Flag::Bots: return st::windowFilterTypeBots;
		case Flag::NoMuted: return st::windowFilterTypeNoMuted;
		case Flag::NoArchived: return st::windowFilterTypeNoArchived;
		case Flag::NoRead: return st::windowFilterTypeNoRead;
		}
		Unexpected("Flag in icon paintFlagIcon.");
	}();
	const auto size = st::windowFilterSmallItem.photoSize;
	const auto rect = QRect(left, top, size, size);
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(color->b);
	p.setPen(Qt::NoPen);
	p.drawEllipse(rect);
	icon.paintInCenter(p, rect);
}

void FilterChatsPreview::removeFlag(Flag flag) {
	const auto i = ranges::find(_removeFlag, flag, &FlagButton::flag);
	Assert(i != end(_removeFlag));
	_removeFlag.erase(i);
	refresh();
	_flagRemoved.fire_copy(flag);
}

void FilterChatsPreview::removePeer(not_null<History*> history) {
	const auto i = ranges::find(_removePeer, history, &PeerButton::history);
	Assert(i != end(_removePeer));
	_removePeer.erase(i);
	refresh();
	_peerRemoved.fire_copy(history);
}

rpl::producer<Flag> FilterChatsPreview::flagRemoved() const {
	return _flagRemoved.events();
}

rpl::producer<not_null<History*>> FilterChatsPreview::peerRemoved() const {
	return _peerRemoved.events();
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
	_window->window().show(Box(SetupBox, _window, _suggested));
}

void ManageFiltersPrepare::SetupBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		const std::vector<Suggested> &suggestions) {
	box->setTitle(tr::lng_filters_title());

	struct FilterRow {
		not_null<FilterRowButton*> button;
		Data::ChatFilter filter;
		bool removed = false;
		bool added = false;
	};

	const auto session = &window->session();
	const auto content = box->verticalLayout();
	AddSubsectionTitle(content, tr::lng_filters_subtitle());

	const auto rows = box->lifetime().make_state<std::vector<FilterRow>>();
	const auto find = [=](not_null<FilterRowButton*> button) {
		const auto i = ranges::find(*rows, button, &FilterRow::button);
		Assert(i != end(*rows));
		return &*i;
	};
	const auto countNonRemoved = [=] {
	};
	const auto showLimitReached = [=] {
		const auto removed = ranges::count_if(*rows, &FilterRow::removed);
		if (rows->size() < kFiltersLimit + removed) {
			return false;
		}
		window->window().showToast(tr::lng_filters_limit(tr::now));
		return true;
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
			window->window().show(Box(
				EditBox,
				window,
				found->filter,
				crl::guard(button, doneCallback)));
		});
		rows->push_back({ button, filter });
	};
	const auto &list = session->data().chatsFilters().list();
	for (const auto &filter : list) {
		addFilter(filter);
	}

	AddButton(
		content,
		tr::lng_filters_create() | Ui::Text::ToUpper(),
		st::settingsUpdate
	)->setClickedCallback([=] {
		if (showLimitReached()) {
			return;
		}
		const auto doneCallback = [=](const Data::ChatFilter &result) {
			addFilter(result);
		};
		window->window().show(Box(
			EditBox,
			window,
			Data::ChatFilter(),
			crl::guard(box, doneCallback)));
	});
	AddSkip(content);
	const auto emptyAbout = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_filters_about(),
				st::boxDividerLabel),
			st::settingsDividerLabelPadding)
	)->setDuration(0);
	const auto nonEmptyAbout = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content))
	)->setDuration(0);
	const auto aboutRows = nonEmptyAbout->entity();
	AddDividerText(aboutRows, tr::lng_filters_about());
	AddSkip(aboutRows);
	AddSubsectionTitle(aboutRows, tr::lng_filters_recommended());

	const auto changed = box->lifetime().make_state<bool>();
	const auto suggested = box->lifetime().make_state<rpl::variable<int>>();
	for (const auto &suggestion : suggestions) {
		const auto &filter = suggestion.filter;
		if (ranges::contains(list, filter)) {
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

	using namespace rpl::mappers;
	emptyAbout->toggleOn(suggested->value() | rpl::map(_1 == 0));
	nonEmptyAbout->toggleOn(suggested->value() | rpl::map(_1 > 0));

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

		using Requests = std::vector<MTPmessages_UpdateDialogFilter>;
		auto addRequests = Requests(), removeRequests = Requests();
		auto &realFilters = session->data().chatsFilters();
		const auto &list = realFilters.list();
		auto order = QVector<MTPint>();
		for (const auto &row : *rows) {
			const auto id = row.filter.id();
			const auto removed = row.removed;
			const auto i = ranges::find(list, id, &Data::ChatFilter::id);
			if (removed && i == end(list)) {
				continue;
			} else if (!removed && i != end(list) && *i == row.filter) {
				order.push_back(MTP_int(id));
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
				removeRequests.push_back(request);
			} else {
				addRequests.push_back(request);
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
		auto &&requests = ranges::view::concat(removeRequests, addRequests);
		for (auto &request : requests) {
			previousId = session->api().request(
				std::move(request)
			).afterRequest(previousId).send();
		}
		if (!order.isEmpty() && !addRequests.empty()) {
			realFilters.apply(
				MTP_updateDialogFilterOrder(MTP_vector(order)));
			session->api().request(MTPmessages_UpdateDialogFiltersOrder(
				MTP_vector(order)
			)).afterRequest(previousId).send();
		}
		box->closeBox();
	};
	box->boxClosing() | rpl::start_with_next(save, box->lifetime());
	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });
}

void SetupChatsPreview(
		not_null<Ui::VerticalLayout*> content,
		not_null<Data::ChatFilter*> data,
		Flags flags,
		ExceptionPeersGetter peers) {
	const auto preview = content->add(object_ptr<FilterChatsPreview>(
		content,
		data->flags() & flags,
		(data->*peers)()));

	preview->flagRemoved(
	) | rpl::start_with_next([=](Flag flag) {
		*data = Data::ChatFilter(
			data->id(),
			data->title(),
			(data->flags() & ~flag),
			data->always(),
			data->never());
	}, preview->lifetime());

	preview->peerRemoved(
	) | rpl::start_with_next([=](not_null<History*> history) {
		auto always = data->always();
		auto never = data->never();
		always.remove(history);
		never.remove(history);
		*data = Data::ChatFilter(
			data->id(),
			data->title(),
			data->flags(),
			std::move(always),
			std::move(never));
	}, preview->lifetime());
}

void ManageFiltersPrepare::EditBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		const Data::ChatFilter &filter,
		Fn<void(const Data::ChatFilter &)> doneCallback) {
	const auto creating = filter.title().isEmpty();
	box->setTitle(creating ? tr::lng_filters_new() : tr::lng_filters_edit());

	const auto content = box->verticalLayout();
	const auto name = content->add(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_filters_new_name(),
			filter.title()),
		st::markdownLinkFieldPadding);
	name->setMaxLength(kMaxFilterTitleLength);

	const auto data = box->lifetime().make_state<Data::ChatFilter>(filter);

	constexpr auto kTypes = Flag::Contacts
		| Flag::NonContacts
		| Flag::Groups
		| Flag::Channels
		| Flag::Bots;
	constexpr auto kExcludeTypes = Flag::NoMuted
		| Flag::NoArchived
		| Flag::NoRead;

	box->setFocusCallback([=] {
		name->setFocusFast();
	});

	AddSkip(content);
	AddDivider(content);
	AddSkip(content);
	AddSubsectionTitle(content, tr::lng_filters_include());

	SetupChatsPreview(
		content,
		data,
		kTypes,
		&Data::ChatFilter::always);

	AddButton(
		content,
		tr::lng_filters_add_chats() | Ui::Text::ToUpper(),
		st::settingsUpdate
	)->setClickedCallback([=] {
	});

	AddSkip(content);
	AddDividerText(content, tr::lng_filters_include_about());
	AddSkip(content);

	AddSubsectionTitle(content, tr::lng_filters_exclude());

	SetupChatsPreview(
		content,
		data,
		kExcludeTypes,
		&Data::ChatFilter::never);

	AddButton(
		content,
		tr::lng_filters_add_chats() | Ui::Text::ToUpper(),
		st::settingsUpdate
	)->setClickedCallback([=] {
	});
	AddSkip(content);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_filters_exclude_about(),
			st::boxDividerLabel),
		st::settingsDividerLabelPadding);

	const auto save = [=] {
		const auto title = name->getLastText().trimmed();
		if (title.isEmpty()) {
			name->showError();
			return;
		} else if (!(data->flags() & kTypes) && data->always().empty()) {
			window->window().showToast(tr::lng_filters_empty(tr::now));
			return;
		} else if ((data->flags() == (kTypes | Flag::NoArchived))
			&& data->always().empty()
			&& data->never().empty()) {
			window->window().showToast(tr::lng_filters_default(tr::now));
			return;
		}
		const auto result = Data::ChatFilter(
			data->id(),
			title,
			data->flags(),
			data->always(),
			data->never());
		box->closeBox();

		doneCallback(result);
	};
	box->addButton(
		creating ? tr::lng_filters_create_button() : tr::lng_settings_save(),
		save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}