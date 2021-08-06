/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_invite_controller.h"

#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_menu.h"
#include "boxes/peer_lists_box.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_group_call.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "styles/style_calls.h"

namespace Calls::Group {
namespace {

[[nodiscard]] object_ptr<Ui::RpWidget> CreateSectionSubtitle(
		QWidget *parent,
		rpl::producer<QString> text) {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		parent,
		st::searchedBarHeight);

	const auto raw = result.data();
	raw->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(raw);
		p.fillRect(clip, st::groupCallMembersBgOver);
	}, raw->lifetime());

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		std::move(text),
		st::groupCallBoxLabel);
	raw->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto padding = st::groupCallInviteDividerPadding;
		const auto available = width - padding.left() - padding.right();
		label->resizeToNaturalWidth(available);
		label->moveToLeft(padding.left(), padding.top(), width);
	}, label->lifetime());

	return result;
}

} // namespace

InviteController::InviteController(
	not_null<PeerData*> peer,
	base::flat_set<not_null<UserData*>> alreadyIn)
: ParticipantsBoxController(CreateTag{}, nullptr, peer, Role::Members)
, _peer(peer)
, _alreadyIn(std::move(alreadyIn)) {
	SubscribeToMigration(
		_peer,
		lifetime(),
		[=](not_null<ChannelData*> channel) { _peer = channel; });
}

void InviteController::prepare() {
	delegate()->peerListSetHideEmpty(true);
	ParticipantsBoxController::prepare();
	delegate()->peerListSetAboveWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_group_call_invite_members()));
	delegate()->peerListSetAboveSearchWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_group_call_invite_members()));
}

void InviteController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

base::unique_qptr<Ui::PopupMenu> InviteController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	return nullptr;
}

void InviteController::itemDeselectedHook(not_null<PeerData*> peer) {
}

bool InviteController::hasRowFor(not_null<PeerData*> peer) const {
	return (delegate()->peerListFindRow(peer->id.value) != nullptr);
}

bool InviteController::isAlreadyIn(not_null<UserData*> user) const {
	return _alreadyIn.contains(user);
}

std::unique_ptr<PeerListRow> InviteController::createRow(
		not_null<PeerData*> participant) const {
	const auto user = participant->asUser();
	if (!user
		|| user->isSelf()
		|| user->isBot()
		|| user->isInaccessible()) {
		return nullptr;
	}
	auto result = std::make_unique<PeerListRow>(user);
	_rowAdded.fire_copy(user);
	_inGroup.emplace(user);
	if (isAlreadyIn(user)) {
		result->setDisabledState(PeerListRow::State::DisabledChecked);
	}
	return result;
}

auto InviteController::peersWithRows() const
-> not_null<const base::flat_set<not_null<UserData*>>*> {
	return &_inGroup;
}

rpl::producer<not_null<UserData*>> InviteController::rowAdded() const {
	return _rowAdded.events();
}

InviteContactsController::InviteContactsController(
	not_null<PeerData*> peer,
	base::flat_set<not_null<UserData*>> alreadyIn,
	not_null<const base::flat_set<not_null<UserData*>>*> inGroup,
	rpl::producer<not_null<UserData*>> discoveredInGroup)
: AddParticipantsBoxController(peer, std::move(alreadyIn))
, _inGroup(inGroup)
, _discoveredInGroup(std::move(discoveredInGroup)) {
}

void InviteContactsController::prepareViewHook() {
	AddParticipantsBoxController::prepareViewHook();

	delegate()->peerListSetAboveWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_contacts_header()));
	delegate()->peerListSetAboveSearchWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_group_call_invite_search_results()));

	std::move(
		_discoveredInGroup
	) | rpl::start_with_next([=](not_null<UserData*> user) {
		if (auto row = delegate()->peerListFindRow(user->id.value)) {
			delegate()->peerListRemoveRow(row);
		}
	}, _lifetime);
}

std::unique_ptr<PeerListRow> InviteContactsController::createRow(
		not_null<UserData*> user) {
	return _inGroup->contains(user)
		? nullptr
		: AddParticipantsBoxController::createRow(user);
}

object_ptr<Ui::BoxContent> PrepareInviteBox(
		not_null<GroupCall*> call,
		Fn<void(TextWithEntities&&)> showToast) {
	const auto real = call->lookupReal();
	if (!real) {
		return nullptr;
	}
	const auto peer = call->peer();
	auto alreadyIn = peer->owner().invitedToCallUsers(real->id());
	for (const auto &participant : real->participants()) {
		if (const auto user = participant.peer->asUser()) {
			alreadyIn.emplace(user);
		}
	}
	alreadyIn.emplace(peer->session().user());
	auto controller = std::make_unique<InviteController>(peer, alreadyIn);
	controller->setStyleOverrides(
		&st::groupCallInviteMembersList,
		&st::groupCallMultiSelect);

	auto contactsController = std::make_unique<InviteContactsController>(
		peer,
		std::move(alreadyIn),
		controller->peersWithRows(),
		controller->rowAdded());
	contactsController->setStyleOverrides(
		&st::groupCallInviteMembersList,
		&st::groupCallMultiSelect);

	const auto weak = base::make_weak(call.get());
	const auto invite = [=](const std::vector<not_null<UserData*>> &users) {
		const auto call = weak.get();
		if (!call) {
			return;
		}
		const auto result = call->inviteUsers(users);
		if (const auto user = std::get_if<not_null<UserData*>>(&result)) {
			showToast(tr::lng_group_call_invite_done_user(
				tr::now,
				lt_user,
				Ui::Text::Bold((*user)->firstName),
				Ui::Text::WithEntities));
		} else if (const auto count = std::get_if<int>(&result)) {
			if (*count > 0) {
				showToast(tr::lng_group_call_invite_done_many(
					tr::now,
					lt_count,
					*count,
					Ui::Text::RichLangValue));
			}
		} else {
			Unexpected("Result in GroupCall::inviteUsers.");
		}
	};
	const auto inviteWithAdd = [=](
			const std::vector<not_null<UserData*>> &users,
			const std::vector<not_null<UserData*>> &nonMembers,
			Fn<void()> finish) {
		peer->session().api().addChatParticipants(
			peer,
			nonMembers,
			[=](bool) { invite(users); finish(); });
	};
	const auto inviteWithConfirmation = [=](
			not_null<PeerListsBox*> parentBox,
			const std::vector<not_null<UserData*>> &users,
			const std::vector<not_null<UserData*>> &nonMembers,
			Fn<void()> finish) {
		if (nonMembers.empty()) {
			invite(users);
			finish();
			return;
		}
		const auto name = peer->name;
		const auto text = (nonMembers.size() == 1)
			? tr::lng_group_call_add_to_group_one(
				tr::now,
				lt_user,
				nonMembers.front()->shortName(),
				lt_group,
				name)
			: (nonMembers.size() < users.size())
			? tr::lng_group_call_add_to_group_some(tr::now, lt_group, name)
			: tr::lng_group_call_add_to_group_all(tr::now, lt_group, name);
		const auto shared = std::make_shared<QPointer<Ui::GenericBox>>();
		const auto finishWithConfirm = [=] {
			if (*shared) {
				(*shared)->closeBox();
			}
			finish();
		};
		const auto done = [=] {
			inviteWithAdd(users, nonMembers, finishWithConfirm);
		};
		auto box = ConfirmBox({
			.text = { text },
			.button = tr::lng_participant_invite(),
			.callback = done,
		});
		*shared = box.data();
		parentBox->getDelegate()->showBox(
			std::move(box),
			Ui::LayerOption::KeepOther,
			anim::type::normal);
	};
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListsBox*> box) {
		box->setTitle(tr::lng_group_call_invite_title());
		box->addButton(tr::lng_group_call_invite_button(), [=] {
			const auto rows = box->collectSelectedRows();

			const auto users = ranges::views::all(
				rows
			) | ranges::views::transform([](not_null<PeerData*> peer) {
				return not_null<UserData*>(peer->asUser());
			}) | ranges::to_vector;

			const auto nonMembers = ranges::views::all(
				users
			) | ranges::views::filter([&](not_null<UserData*> user) {
				return !controller->hasRowFor(user);
			}) | ranges::to_vector;

			const auto finish = [box = Ui::MakeWeak(box)]() {
				if (box) {
					box->closeBox();
				}
			};
			inviteWithConfirmation(box, users, nonMembers, finish);
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};

	auto controllers = std::vector<std::unique_ptr<PeerListController>>();
	controllers.push_back(std::move(controller));
	controllers.push_back(std::move(contactsController));
	return Box<PeerListsBox>(std::move(controllers), initBox);
}

} // namespace Calls::Group
