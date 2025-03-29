/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_invite_controller.h"

#include "api/api_chat_participants.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_menu.h"
#include "boxes/peer_lists_box.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_group_call.h"
#include "info/profile/info_profile_icon.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "ui/text/text_utilities.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "styles/style_boxes.h" // membersMarginTop
#include "styles/style_calls.h"
#include "styles/style_dialogs.h" // searchedBarHeight

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

class ConfInviteController final : public ContactsBoxController {
public:
	ConfInviteController(
		not_null<Main::Session*> session,
		base::flat_set<not_null<UserData*>> alreadyIn,
		Fn<void()> shareLink);

	[[nodiscard]] rpl::producer<bool> hasSelectedValue() const;

protected:
	void prepareViewHook() override;

	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;

	void rowClicked(not_null<PeerListRow*> row) override;

private:
	[[nodiscard]] int fullCount() const;

	base::flat_set<not_null<UserData*>> _alreadyIn;
	const Fn<void()> _shareLink;
	rpl::variable<bool> _hasSelected;

};

ConfInviteController::ConfInviteController(
	not_null<Main::Session*> session,
	base::flat_set<not_null<UserData*>> alreadyIn,
	Fn<void()> shareLink)
: ContactsBoxController(session)
, _alreadyIn(std::move(alreadyIn))
, _shareLink(std::move(shareLink)) {
	_alreadyIn.remove(session->user());
}

rpl::producer<bool> ConfInviteController::hasSelectedValue() const {
	return _hasSelected.value();
}

std::unique_ptr<PeerListRow> ConfInviteController::createRow(
		not_null<UserData*> user) {
	if (user->isSelf()
		|| user->isBot()
		|| user->isServiceUser()
		|| user->isInaccessible()) {
		return nullptr;
	}
	auto result = ContactsBoxController::createRow(user);
	if (_alreadyIn.contains(user)) {
		result->setDisabledState(PeerListRow::State::DisabledChecked);
	}
	return result;
}

int ConfInviteController::fullCount() const {
	return _alreadyIn.size() + delegate()->peerListSelectedRowsCount();
}

void ConfInviteController::rowClicked(not_null<PeerListRow*> row) {
	auto count = fullCount();
	auto limit = Data::kMaxConferenceMembers;
	if (count < limit || row->checked()) {
		delegate()->peerListSetRowChecked(row, !row->checked());
		_hasSelected = (delegate()->peerListSelectedRowsCount() > 0);
	} else {
		delegate()->peerListUiShow()->showToast(
			tr::lng_group_call_invite_limit(tr::now));
	}
}

void ConfInviteController::prepareViewHook() {
	auto button = object_ptr<Ui::PaddingWrap<Ui::SettingsButton>>(
		nullptr,
		object_ptr<Ui::SettingsButton>(
			nullptr,
			tr::lng_profile_add_via_link(),
			st::groupCallInviteLink),
		style::margins(0, st::membersMarginTop, 0, 0));

	const auto icon = Ui::CreateChild<Info::Profile::FloatingIcon>(
		button->entity(),
		st::groupCallInviteLinkIcon,
		QPoint());
	button->entity()->heightValue(
	) | rpl::start_with_next([=](int height) {
		icon->moveToLeft(
			st::groupCallInviteLinkIconPosition.x(),
			(height - st::groupCallInviteLinkIcon.height()) / 2);
	}, icon->lifetime());

	button->entity()->setClickedCallback(_shareLink);
	button->entity()->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::start_with_next([=] {
		delegate()->peerListMouseLeftGeometry();
	}, button->lifetime());
	delegate()->peerListSetAboveWidget(std::move(button));
}

[[nodiscard]] TextWithEntities ComposeInviteResultToast(
		const GroupCall::InviteResult &result) {
	auto text = TextWithEntities();
	const auto invited = int(result.invited.size());
	const auto restricted = int(result.privacyRestricted.size());
	if (invited == 1) {
		text.append(tr::lng_confcall_invite_done_user(
			tr::now,
			lt_user,
			Ui::Text::Bold(result.invited.front()->shortName()),
			Ui::Text::RichLangValue));
	} else if (invited > 1) {
		text.append(tr::lng_confcall_invite_done_many(
			tr::now,
			lt_count,
			invited,
			Ui::Text::RichLangValue));
	}
	if (invited && restricted) {
		text.append(u"\n\n"_q);
	}
	if (restricted == 1) {
		text.append(tr::lng_confcall_invite_fail_user(
			tr::now,
			lt_user,
			Ui::Text::Bold(result.privacyRestricted.front()->shortName()),
			Ui::Text::RichLangValue));
	} else if (restricted > 1) {
		text.append(tr::lng_confcall_invite_fail_many(
			tr::now,
			lt_count,
			restricted,
			Ui::Text::RichLangValue));
	}
	return text;
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
		Fn<void(TextWithEntities&&)> showToast,
		Fn<void(Fn<void(bool)> finished)> shareConferenceLink) {
	const auto real = call->lookupReal();
	if (!real) {
		return nullptr;
	}
	const auto peer = call->peer();
	const auto weak = base::make_weak(call);
	auto alreadyIn = peer->owner().invitedToCallUsers(real->id());
	for (const auto &participant : real->participants()) {
		if (const auto user = participant.peer->asUser()) {
			alreadyIn.emplace(user);
		}
	}
	alreadyIn.emplace(peer->session().user());
	if (call->conference()) {
		const auto close = std::make_shared<Fn<void()>>();
		const auto shareLink = [=] {
			Assert(shareConferenceLink != nullptr);
			shareConferenceLink([=](bool ok) { if (ok) (*close)(); });
		};
		auto controller = std::make_unique<ConfInviteController>(
			&real->session(),
			alreadyIn,
			shareLink);
		const auto raw = controller.get();
		raw->setStyleOverrides(
			&st::groupCallInviteMembersList,
			&st::groupCallMultiSelect);
		auto initBox = [=](not_null<PeerListBox*> box) {
			box->setTitle(tr::lng_group_call_invite_conf());
			raw->hasSelectedValue() | rpl::start_with_next([=](bool has) {
				box->clearButtons();
				if (has) {
					box->addButton(tr::lng_group_call_invite_button(), [=] {
						const auto call = weak.get();
						if (!call) {
							return;
						}
						auto peers = box->collectSelectedRows();
						auto users = ranges::views::all(
							peers
						) | ranges::views::transform([](not_null<PeerData*> peer) {
							return not_null(peer->asUser());
						}) | ranges::to_vector;
						const auto done = [=](GroupCall::InviteResult result) {
							(*close)();
							showToast({ ComposeInviteResultToast(result) });
						};
						call->inviteUsers(users, done);
					});
				}
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			}, box->lifetime());
			*close = crl::guard(box, [=] { box->closeBox(); });
		};
		return Box<PeerListBox>(std::move(controller), initBox);
	}

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

	const auto invite = [=](const std::vector<not_null<UserData*>> &users) {
		const auto call = weak.get();
		if (!call) {
			return;
		}
		call->inviteUsers(users, [=](GroupCall::InviteResult result) {
			if (result.invited.size() == 1) {
				showToast(tr::lng_group_call_invite_done_user(
					tr::now,
					lt_user,
					Ui::Text::Bold(result.invited.front()->firstName),
					Ui::Text::WithEntities));
			} else if (result.invited.size() > 1) {
				showToast(tr::lng_group_call_invite_done_many(
					tr::now,
					lt_count,
					result.invited.size(),
					Ui::Text::RichLangValue));
			} else {
				Unexpected("Result in GroupCall::inviteUsers.");
			}
		});
	};
	const auto inviteWithAdd = [=](
			std::shared_ptr<Ui::Show> show,
			const std::vector<not_null<UserData*>> &users,
			const std::vector<not_null<UserData*>> &nonMembers,
			Fn<void()> finish) {
		peer->session().api().chatParticipants().add(
			show,
			peer,
			nonMembers,
			true,
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
		const auto name = peer->name();
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
			const auto show = (*shared) ? (*shared)->uiShow() : nullptr;
			inviteWithAdd(show, users, nonMembers, finishWithConfirm);
		};
		auto box = ConfirmBox({
			.text = text,
			.confirmed = done,
			.confirmText = tr::lng_participant_invite(),
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
