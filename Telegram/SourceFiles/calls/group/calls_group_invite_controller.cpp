/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_invite_controller.h"

#include "api/api_chat_participants.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_menu.h"
#include "calls/calls_call.h"
#include "boxes/peer_lists_box.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_group_call.h"
#include "info/profile/info_profile_icon.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
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

class ConfInviteRow final : public PeerListRow {
public:
	using PeerListRow::PeerListRow;

	void setAlreadyIn(bool alreadyIn);
	void setVideo(bool video);

	int elementsCount() const override;
	QRect elementGeometry(int element, int outerWidth) const override;
	bool elementDisabled(int element) const override;
	bool elementOnlySelect(int element) const override;
	void elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) override;
	void elementsStopLastRipple() override;
	void elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) override;

private:
	std::unique_ptr<Ui::RippleAnimation> _videoRipple;
	std::unique_ptr<Ui::RippleAnimation> _audioRipple;
	bool _alreadyIn = false;
	bool _video = false;

};

class ConfInviteController final : public ContactsBoxController {
public:
	ConfInviteController(
		not_null<Main::Session*> session,
		base::flat_set<not_null<UserData*>> alreadyIn,
		Fn<void()> shareLink);

	[[nodiscard]] rpl::producer<bool> hasSelectedValue() const;
	[[nodiscard]] std::vector<InviteRequest> requests(
		const std::vector<not_null<PeerData*>> &peers) const;

protected:
	void prepareViewHook() override;

	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;

	void rowClicked(not_null<PeerListRow*> row) override;
	void rowElementClicked(not_null<PeerListRow*> row, int element) override;

private:
	[[nodiscard]] int fullCount() const;
	void toggleRowSelected(not_null<PeerListRow*> row, bool video);

	const base::flat_set<not_null<UserData*>> _alreadyIn;
	const Fn<void()> _shareLink;
	rpl::variable<bool> _hasSelected;
	base::flat_set<not_null<UserData*>> _withVideo;
	bool _lastSelectWithVideo = false;

};

void ConfInviteRow::setAlreadyIn(bool alreadyIn) {
	_alreadyIn = alreadyIn;
	setDisabledState(alreadyIn ? State::DisabledChecked : State::Active);
}

void ConfInviteRow::setVideo(bool video) {
	_video = video;
}

int ConfInviteRow::elementsCount() const {
	return _alreadyIn ? 0 : 2;
}

QRect ConfInviteRow::elementGeometry(int element, int outerWidth) const {
	if (_alreadyIn || (element != 1 && element != 2)) {
		return QRect();
	}
	const auto &st = (element == 1)
		? st::confcallInviteVideo
		: st::confcallInviteAudio;
	const auto size = QSize(st.width, st.height);
	const auto margins = (element == 1)
		? st::confcallInviteVideoMargins
		: st::confcallInviteAudioMargins;
	const auto right = margins.right();
	const auto top = margins.top();
	const auto side = (element == 1)
		? outerWidth
		: elementGeometry(1, outerWidth).x();
	const auto left = side - right - size.width();
	return QRect(QPoint(left, top), size);
}

bool ConfInviteRow::elementDisabled(int element) const {
	return _alreadyIn
		|| (checked()
			&& ((_video && element == 1) || (!_video && element == 2)));
}

bool ConfInviteRow::elementOnlySelect(int element) const {
	return false;
}

void ConfInviteRow::elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) {
	if (_alreadyIn || (element != 1 && element != 2)) {
		return;
	}
	auto &ripple = (element == 1) ? _videoRipple : _audioRipple;
	const auto &st = (element == 1)
		? st::confcallInviteVideo
		: st::confcallInviteAudio;
	if (!ripple) {
		auto mask = Ui::RippleAnimation::EllipseMask(QSize(
				st.rippleAreaSize,
				st.rippleAreaSize));
		ripple = std::make_unique<Ui::RippleAnimation>(
			st.ripple,
			std::move(mask),
			std::move(updateCallback));
	}
	ripple->add(point - st.rippleAreaPosition);
}

void ConfInviteRow::elementsStopLastRipple() {
	if (_videoRipple) {
		_videoRipple->lastStop();
	}
	if (_audioRipple) {
		_audioRipple->lastStop();
	}
}

void ConfInviteRow::elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) {
	if (_alreadyIn) {
		return;
	}
	const auto paintElement = [&](int element) {
		const auto &st = (element == 1)
			? st::confcallInviteVideo
			: st::confcallInviteAudio;
		auto &ripple = (element == 1) ? _videoRipple : _audioRipple;
		const auto active = checked() && ((element == 1) ? _video : !_video);
		const auto geometry = elementGeometry(element, outerWidth);
		if (ripple) {
			ripple->paint(
				p,
				geometry.x() + st.rippleAreaPosition.x(),
				geometry.y() + st.rippleAreaPosition.y(),
				outerWidth);
			if (ripple->empty()) {
				ripple.reset();
			}
		}
		const auto selected = (element == selectedElement);
		const auto &icon = active
			? (element == 1
				? st::confcallInviteVideoActive
				: st::confcallInviteAudioActive)
			: (selected ? st.iconOver : st.icon);
		icon.paintInCenter(p, geometry);
	};
	paintElement(1);
	paintElement(2);
}

ConfInviteController::ConfInviteController(
	not_null<Main::Session*> session,
	base::flat_set<not_null<UserData*>> alreadyIn,
	Fn<void()> shareLink)
: ContactsBoxController(session)
, _alreadyIn(std::move(alreadyIn))
, _shareLink(std::move(shareLink)) {
}

rpl::producer<bool> ConfInviteController::hasSelectedValue() const {
	return _hasSelected.value();
}

std::vector<InviteRequest> ConfInviteController::requests(
		const std::vector<not_null<PeerData*>> &peers) const {
	auto result = std::vector<InviteRequest>();
	result.reserve(peers.size());
	for (const auto &peer : peers) {
		if (const auto user = peer->asUser()) {
			result.push_back({ user, _withVideo.contains(user) });
		}
	}
	return result;
}

std::unique_ptr<PeerListRow> ConfInviteController::createRow(
		not_null<UserData*> user) {
	if (user->isSelf()
		|| user->isBot()
		|| user->isServiceUser()
		|| user->isInaccessible()) {
		return nullptr;
	}
	auto result = std::make_unique<ConfInviteRow>(user);
	if (_alreadyIn.contains(user)) {
		result->setAlreadyIn(true);
	}
	if (_withVideo.contains(user)) {
		result->setVideo(true);
	}
	return result;
}

int ConfInviteController::fullCount() const {
	return _alreadyIn.size() + delegate()->peerListSelectedRowsCount();
}

void ConfInviteController::rowClicked(not_null<PeerListRow*> row) {
	toggleRowSelected(row, _lastSelectWithVideo);
}

void ConfInviteController::rowElementClicked(
		not_null<PeerListRow*> row,
		int element) {
	if (row->checked()) {
		static_cast<ConfInviteRow*>(row.get())->setVideo(element == 1);
		_lastSelectWithVideo = (element == 1);
	} else if (element == 1) {
		toggleRowSelected(row, true);
	} else if (element == 2) {
		toggleRowSelected(row, false);
	}
}

void ConfInviteController::toggleRowSelected(
		not_null<PeerListRow*> row,
		bool video) {
	auto count = fullCount();
	auto limit = Data::kMaxConferenceMembers;
	if (count < limit || row->checked()) {
		const auto real = static_cast<ConfInviteRow*>(row.get());
		if (!row->checked()) {
			real->setVideo(video);
			_lastSelectWithVideo = video;
		}
		const auto user = row->peer()->asUser();
		if (!row->checked() && video) {
			_withVideo.emplace(user);
		} else {
			_withVideo.remove(user);
		}
		delegate()->peerListSetRowChecked(row, !row->checked());

		// row may have been destroyed here, from search.
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
	const auto conference = call->conference();
	const auto weak = base::make_weak(call);
	const auto &invited = peer->owner().invitedToCallUsers(real->id());
	auto alreadyIn = base::flat_set<not_null<UserData*>>();
	alreadyIn.reserve(invited.size() + real->participants().size() + 1);
	alreadyIn.emplace(peer->session().user());
	for (const auto &participant : real->participants()) {
		if (const auto user = participant.peer->asUser()) {
			alreadyIn.emplace(user);
		}
	}
	for (const auto &[user, calling] : invited) {
		if (!conference || calling) {
			alreadyIn.emplace(user);
		}
	}
	if (conference) {
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
					box->addButton(tr::lng_group_call_confcall_add(), [=] {
						const auto call = weak.get();
						if (!call) {
							return;
						}
						const auto done = [=](InviteResult result) {
							(*close)();
							showToast({ ComposeInviteResultToast(result) });
						};
						call->inviteUsers(
							raw->requests(box->collectSelectedRows()),
							done);
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
		auto requests = ranges::views::all(
			users
		) | ranges::views::transform([](not_null<UserData*> user) {
			return InviteRequest{ user };
		}) | ranges::to_vector;
		call->inviteUsers(std::move(requests), [=](InviteResult result) {
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

object_ptr<Ui::BoxContent> PrepareInviteBox(
		not_null<Call*> call,
		Fn<void(std::vector<InviteRequest>)> inviteUsers,
		Fn<void()> shareLink) {
	const auto user = call->user();
	const auto weak = base::make_weak(call);
	auto alreadyIn = base::flat_set<not_null<UserData*>>{ user };
	auto controller = std::make_unique<ConfInviteController>(
		&user->session(),
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
					inviteUsers(raw->requests(box->collectSelectedRows()));
				});
			}
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		}, box->lifetime());
	};
	return Box<PeerListBox>(std::move(controller), initBox);
}

} // namespace Calls::Group
