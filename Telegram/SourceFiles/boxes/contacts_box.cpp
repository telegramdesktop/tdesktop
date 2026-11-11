/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/contacts_box.h"

#include "apiwrap.h"
#include "base/qt/qt_key_modifiers.h"
#include "boxes/peer_list_controllers.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h" // HighlightArgs.
#include "ui/boxes/confirm_box.h"
#include "ui/effects/round_checkbox.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace {

class SelectableContactRow final : public PeerListRow {
public:
	SelectableContactRow(
		not_null<UserData*> user,
		Fn<bool()> selecting,
		Fn<void(not_null<PeerListRow*>)> repaint);

	void setSelected(bool selected, anim::type animated);

	void paintUserpicOverlay(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth) override;

private:
	const Fn<bool()> _selecting;
	Ui::RoundCheckbox _check;

};

SelectableContactRow::SelectableContactRow(
	not_null<UserData*> user,
	Fn<bool()> selecting,
	Fn<void(not_null<PeerListRow*>)> repaint)
: PeerListRow(user)
, _selecting(std::move(selecting))
, _check(st::defaultPeerListCheck, [=] { repaint(this); }) {
	_check.setDisplayInactive(true);
}

void SelectableContactRow::setSelected(
		bool selected,
		anim::type animated) {
	_check.setChecked(selected, animated);
}

void SelectableContactRow::paintUserpicOverlay(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int outerWidth) {
	if (!_selecting()) {
		return;
	}
	const auto shift = st.photoSize
		+ st.checkbox.selectWidth
		- st::defaultPeerListCheck.size;
	_check.paint(p, x + shift, y + shift, outerWidth);
}

class ContactsListController final
	: public ContactsBoxController
	, public base::has_weak_ptr {
public:
	explicit ContactsListController(
		not_null<Window::SessionController*> window);

	[[nodiscard]] rpl::producer<not_null<PeerData*>> wheelClicks() const;
	[[nodiscard]] rpl::producer<int> selectedCountValue() const;

	void rowClicked(not_null<PeerListRow*> row) override;
	void rowMiddleClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	bool rowDragSelectStart(not_null<PeerListRow*> row) override;
	void rowDragSelectUpdate(
		const std::vector<not_null<PeerListRow*>> &rows) override;
	void rowDragSelectFinish() override;

	void clearSelection();
	void deleteSelected();

protected:
	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;

private:
	struct Press {
		PeerListRowId id = 0;
		bool select = false;
		bool dragged = false;
		bool held = true;
		base::flat_set<PeerListRowId> initial;
	};

	[[nodiscard]] bool selecting() const;
	[[nodiscard]] static bool Selectable(not_null<PeerListRow*> row);
	void toggleSelected(not_null<PeerListRow*> row);
	void applySelected(base::flat_set<PeerListRowId> ids);
	void removeRows(const std::vector<not_null<UserData*>> &users);

	const not_null<Window::SessionController*> _window;
	base::flat_set<PeerListRowId> _selected;
	std::optional<Press> _press;
	rpl::variable<int> _selectedCount = 0;
	rpl::event_stream<not_null<PeerData*>> _wheelClicks;

};

ContactsListController::ContactsListController(
	not_null<Window::SessionController*> window)
: ContactsBoxController(&window->session())
, _window(window) {
}

rpl::producer<not_null<PeerData*>> ContactsListController::wheelClicks(
) const {
	return _wheelClicks.events();
}

rpl::producer<int> ContactsListController::selectedCountValue() const {
	return _selectedCount.value();
}

bool ContactsListController::selecting() const {
	return !_selected.empty();
}

bool ContactsListController::Selectable(not_null<PeerListRow*> row) {
	const auto user = row->peer()->asUser();
	return user && user->isContact();
}

std::unique_ptr<PeerListRow> ContactsListController::createRow(
		not_null<UserData*> user) {
	if (user->isSelf()) {
		return nullptr;
	}
	auto result = std::make_unique<SelectableContactRow>(
		user,
		[=] { return selecting(); },
		[=](not_null<PeerListRow*> row) {
			delegate()->peerListUpdateRow(row);
		});
	if (_selected.contains(result->id())) {
		result->setSelected(true, anim::type::instant);
	}
	return result;
}

void ContactsListController::rowClicked(not_null<PeerListRow*> row) {
	const auto press = base::take(_press);
	if (press && press->id == row->id() && press->dragged) {
		return;
	} else if (selecting() || base::IsCtrlPressed()) {
		toggleSelected(row);
	} else {
		ContactsBoxController::rowClicked(row);
	}
}

void ContactsListController::rowMiddleClicked(not_null<PeerListRow*> row) {
	_wheelClicks.fire(row->peer());
}

base::unique_qptr<Ui::PopupMenu> ContactsListController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	if (selecting() || !Selectable(row)) {
		return nullptr;
	}
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	result->addAction(
		tr::lng_context_select_msg(tr::now),
		crl::guard(this, [=] { toggleSelected(row); }),
		&st::menuIconSelect);
	return result;
}

bool ContactsListController::rowDragSelectStart(
		not_null<PeerListRow*> row) {
	_press = std::nullopt;
	if (!Selectable(row) || !(selecting() || base::IsCtrlPressed())) {
		return false;
	}
	_press = Press{
		.id = row->id(),
		.select = !_selected.contains(row->id()),
		.initial = _selected,
	};
	return true;
}

void ContactsListController::rowDragSelectUpdate(
		const std::vector<not_null<PeerListRow*>> &rows) {
	if (!_press || !_press->held) {
		return;
	} else if (!_press->dragged && rows.size() < 2) {
		return;
	}
	_press->dragged = true;
	auto ids = _press->initial;
	for (const auto &row : rows) {
		if (!Selectable(row)) {
			continue;
		} else if (_press->select) {
			ids.emplace(row->id());
		} else {
			ids.remove(row->id());
		}
	}
	applySelected(std::move(ids));
}

void ContactsListController::rowDragSelectFinish() {
	if (_press) {
		_press->held = false;
	}
}

void ContactsListController::toggleSelected(not_null<PeerListRow*> row) {
	if (!Selectable(row)) {
		return;
	}
	auto ids = _selected;
	if (!ids.remove(row->id())) {
		ids.emplace(row->id());
	}
	applySelected(std::move(ids));
}

void ContactsListController::applySelected(
		base::flat_set<PeerListRowId> ids) {
	const auto wasSelecting = selecting();
	const auto animated = ids.empty()
		? anim::type::instant
		: anim::type::normal;
	const auto set = [&](PeerListRowId id, bool selected) {
		if (const auto row = delegate()->peerListFindRow(id)) {
			static_cast<SelectableContactRow*>(row)->setSelected(
				selected,
				animated);
		}
	};
	for (const auto id : _selected) {
		if (!ids.contains(id)) {
			set(id, false);
		}
	}
	for (const auto id : ids) {
		if (!_selected.contains(id)) {
			set(id, true);
		}
	}
	_selected = std::move(ids);
	if (wasSelecting != selecting()) {
		delegate()->peerListRefreshRows();
	}
	_selectedCount = int(_selected.size());
}

void ContactsListController::clearSelection() {
	_press = std::nullopt;
	applySelected({});
}

void ContactsListController::deleteSelected() {
	if (_window->showFrozenError()) {
		return;
	}
	auto users = std::vector<not_null<UserData*>>();
	users.reserve(_selected.size());
	for (const auto id : _selected) {
		if (const auto row = delegate()->peerListFindRow(id)) {
			if (const auto user = row->peer()->asUser()) {
				users.push_back(user);
			}
		}
	}
	if (users.empty()) {
		return;
	}
	const auto count = int(users.size());
	const auto text = (count == 1)
		? tr::lng_sure_delete_contact(
			tr::now,
			lt_contact,
			users.front()->name())
		: tr::lng_sure_delete_contacts(tr::now, lt_count, count);
	const auto confirmed = crl::guard(this, [=](Fn<void()> close) {
		close();
		clearSelection();
		auto inputs = QVector<MTPInputUser>();
		inputs.reserve(users.size());
		for (const auto &user : users) {
			inputs.push_back(user->inputUser());
		}
		const auto api = &session().api();
		api->request(MTPcontacts_DeleteContacts(
			MTP_vector<MTPInputUser>(std::move(inputs))
		)).done(crl::guard(this, [=](const MTPUpdates &result) {
			removeRows(users);
			api->applyUpdates(result);
		})).send();
	});
	delegate()->peerListUiShow()->showBox(Ui::MakeConfirmBox({
		.text = text,
		.confirmed = confirmed,
		.confirmText = tr::lng_box_delete(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void ContactsListController::removeRows(
		const std::vector<not_null<UserData*>> &users) {
	for (const auto &user : users) {
		if (const auto row = delegate()->peerListFindRow(user->id.value)) {
			delegate()->peerListRemoveRow(row);
		}
	}
	delegate()->peerListRefreshRows();
}

} // namespace

object_ptr<Ui::BoxContent> PrepareContactsBox(
		not_null<Window::SessionController*> window) {
	using Mode = ContactsBoxController::SortMode;
	auto controller = std::make_unique<ContactsListController>(window);
	controller->setStyleOverrides(&st::contactsWithStories);
	controller->setStoriesShown(true);
	controller->setSectionHeadersShown(true);
	const auto raw = controller.get();
	auto init = [=](not_null<PeerListBox*> box) {
		struct State {
			QPointer<::Ui::IconButton> toggleSort;
			rpl::variable<Mode> mode = Mode::Online;
			::Ui::Animations::Simple scrollAnimation;
		};

		const auto state = box->lifetime().make_state<State>();
		const auto refreshSortIcon = [=] {
			const auto alphabet = (state->mode.current() == Mode::Alphabet);
			state->toggleSort->setIconOverride(
				alphabet ? &st::contactsSortOnlineIcon : nullptr,
				alphabet ? &st::contactsSortOnlineIconOver : nullptr);
		};
		const auto toggleSort = [=] {
			const auto online = (state->mode.current() == Mode::Online);
			const auto mode = online ? Mode::Alphabet : Mode::Online;
			state->mode = mode;
			raw->setSortMode(mode);
			refreshSortIcon();
		};
		const auto addSortButton = [=] {
			state->toggleSort = box->addTopButton(
				st::contactsSortButton,
				toggleSort);
			refreshSortIcon();
		};
		raw->selectedCountValue(
		) | rpl::map(
			rpl::mappers::_1 > 0
		) | rpl::distinct_until_changed(
		) | rpl::on_next([=](bool selecting) {
			box->clearButtons();
			if (selecting) {
				box->setTitle(tr::lng_contacts_selected(
					lt_count,
					raw->selectedCountValue() | tr::to_count()));
				box->addButton(
					tr::lng_box_delete(),
					[=] { raw->deleteSelected(); },
					st::attentionBoxButton);
				box->addButton(
					tr::lng_cancel(),
					[=] { raw->clearSelection(); });
			} else {
				box->setTitle(tr::lng_contacts_header());
				box->addButton(tr::lng_close(), [=] { box->closeBox(); });
				box->addLeftButton(
					tr::lng_profile_add_contact(),
					[=] { window->showAddContact(); });
			}
			addSortButton();
		}, box->lifetime());
		raw->setSortMode(Mode::Online);

		raw->wheelClicks() | rpl::on_next([=](not_null<PeerData*> p) {
			window->showInNewWindow(p);
		}, box->lifetime());

		raw->setShowFinishedCallback([=] {
			window->checkHighlightControl(
				u"contacts/sort"_q,
				state->toggleSort,
				{ .rippleShape = true });
		});
	};
	return Box<PeerListBox>(std::move(controller), std::move(init));
}
