/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/add_to_community_box.h"

#include "api/api_communities.h"
#include "apiwrap.h"
#include "boxes/peer_list_box.h"
#include "boxes/peers/manage_community_box.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_community.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace {

class Controller final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	Controller(
		not_null<PeerData*> peer,
		Fn<void(not_null<ChannelData*>)> callback);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	[[nodiscard]] rpl::producer<int> countValue() const {
		return _count.value();
	}

private:
	void appendRow(not_null<ChannelData*> community);
	void updateStatus(not_null<PeerListRow*> row);

	const not_null<PeerData*> _peer;
	const Fn<void(not_null<ChannelData*>)> _callback;
	rpl::variable<int> _count = 0;

};

Controller::Controller(
	not_null<PeerData*> peer,
	Fn<void(not_null<ChannelData*>)> callback)
: _peer(peer)
, _callback(std::move(callback)) {
}

Main::Session &Controller::session() const {
	return _peer->session();
}

void Controller::prepare() {
	session().changes().peerUpdates(
		Data::PeerUpdate::Flag::FullInfo
	) | rpl::on_next([=](const Data::PeerUpdate &update) {
		if (const auto row = delegate()->peerListFindRow(
				update.peer->id.value)) {
			updateStatus(row);
			delegate()->peerListUpdateRow(row);
		}
	}, lifetime());

	session().api().communities().requestJoinedCommunities(crl::guard(
		this,
		[=](const std::vector<not_null<ChannelData*>> &list) {
			for (const auto &community : list) {
				appendRow(community);
			}
			delegate()->peerListRefreshRows();
			_count = delegate()->peerListFullRowsCount();
		}));
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	if (const auto community = row->peer()->asChannel()) {
		_callback(community);
	}
}

void Controller::appendRow(not_null<ChannelData*> community) {
	if (delegate()->peerListFindRow(community->id.value)) {
		return;
	}
	auto row = std::make_unique<PeerListRow>(community);
	updateStatus(row.get());
	delegate()->peerListAppendRow(std::move(row));
	if (!community->wasFullUpdated()) {
		session().api().requestFullPeer(community);
	}
}

void Controller::updateStatus(not_null<PeerListRow*> row) {
	const auto community = row->peer()->asChannel();
	const auto info = community ? community->communityInfo() : nullptr;
	const auto count = info ? int(info->linkedPeers().size()) : 0;
	row->setCustomStatus(count
		? tr::lng_community_chats(tr::now, lt_count, count)
		: tr::lng_community_status(tr::now));
}

void CreateCommunityBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer) {
	box->setTitle(tr::lng_community_create_title());

	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_community_create_name()));
	box->setFocusCallback([=] { field->setFocusFast(); });

	const auto hidden = box->addRow(object_ptr<Ui::Checkbox>(
		box,
		tr::lng_community_create_hidden(tr::now),
		false,
		st::defaultBoxCheckbox));

	const auto creating = box->lifetime().make_state<bool>(false);
	const auto submit = [=] {
		const auto title = field->getLastText().trimmed();
		if (title.isEmpty()) {
			field->showError();
			return;
		} else if (*creating) {
			return;
		}
		*creating = true;
		const auto show = navigation->uiShow();
		navigation->session().api().communities().create(
			title,
			QString(),
			peer,
			hidden->checked(),
			crl::guard(box, [=](not_null<ChannelData*> community) {
				show->hideLayer();
				show->showToast(tr::lng_community_created(tr::now));
				ShowManageCommunityBox(navigation, community);
			}),
			crl::guard(box, [=](const QString &error) {
				*creating = false;
				show->showToast(error.isEmpty()
					? Lang::Hard::ServerError()
					: error);
			}));
	};
	field->submits(
	) | rpl::on_next(submit, field->lifetime());

	box->addButton(tr::lng_create_group_create(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void ChooseVisibilityBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(bool visible)> done) {
	box->setTitle(tr::lng_community_visibility_title());

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(1);
	const auto addOption = [&](
			int value,
			const QString &label,
			rpl::producer<QString> about) {
		box->addSkip(st::editPeerHistoryVisibilityTopSkip);
		const auto wrap = box->addRow(object_ptr<Ui::VerticalLayout>(box));
		wrap->add(object_ptr<Ui::Radiobutton>(
			box,
			group,
			value,
			label,
			st::defaultBoxCheckbox));
		wrap->add(
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(about),
				st::editPeerPrivacyLabel),
			st::editPeerPreHistoryLabelMargins);
		const auto button = Ui::CreateChild<Ui::AbstractButton>(wrap);
		wrap->sizeValue(
		) | rpl::on_next([=](const QSize &s) {
			button->resize(s);
		}, button->lifetime());
		button->setClickedCallback([=] { group->setValue(value); });
	};
	addOption(
		1,
		tr::lng_community_visibility_visible(tr::now),
		tr::lng_community_visibility_visible_about());
	addOption(
		0,
		tr::lng_community_visibility_hidden(tr::now),
		tr::lng_community_visibility_hidden_about());

	box->addSkip(st::editPeerHistoryVisibilityTopSkip);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_community_visibility_note(),
			st::editPeerPrivacyLabel),
		st::editPeerPreHistoryLabelMargins);

	box->addButton(tr::lng_community_add_to(), [=] {
		done(group->current() == 1);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

void ShowAddPeerToCommunity(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> community,
		not_null<PeerData*> peer) {
	const auto show = navigation->uiShow();
	const auto add = [=](bool visible) {
		const auto sure = [=](Fn<void()> &&close) {
			close();
			peer->session().api().communities().addPeerLink(
				community,
				peer,
				visible,
				[=] {
					show->hideLayer();
					show->showToast(peer->isUser()
						? tr::lng_community_add_done_bot(tr::now)
						: peer->isBroadcast()
						? tr::lng_community_add_done_channel(tr::now)
						: tr::lng_community_add_done(tr::now));
				},
				[=](const QString &error) {
					if (error == Api::kCommunityRequestCreated.utf16()) {
						show->hideLayer();
						show->showToast(
							tr::lng_community_request_sent(tr::now));
					} else if (error == Api::kCommunityPeersTooMuch.utf16()) {
						show->showToast(
							Api::CommunityPeersLimitToast(peer));
					} else {
						show->showToast(error.isEmpty()
							? Lang::Hard::ServerError()
							: error);
					}
				});
		};
		// A community admin adds chats directly; everyone else can only
		// suggest a chat, which a community admin must then approve.
		if (community->canManageLinkedPeers()) {
			sure([] {});
		} else {
			show->showBox(Ui::MakeConfirmBox({
				.text = (peer->isUser()
					? tr::lng_community_add_confirm_bot()
					: tr::lng_community_add_confirm()),
				.confirmed = sure,
				.confirmText = tr::lng_community_add_confirm_add(),
				.title = tr::lng_community_add_to(),
			}));
		}
	};
	show->showBox(Box(ChooseVisibilityBox, add));
}

void ShowAddToCommunityBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer) {
	const auto choose = [=](not_null<ChannelData*> community) {
		ShowAddPeerToCommunity(navigation, community, peer);
	};
	auto controller = std::make_unique<Controller>(peer, choose);
	const auto raw = controller.get();
	const auto init = [=](not_null<PeerListBox*> box) {
		box->setTitle(tr::lng_community_title());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });

		auto above = object_ptr<Ui::VerticalLayout>(box);
		Ui::AddDividerText(above, peer->isUser()
			? tr::lng_community_add_about_bot()
			: tr::lng_community_add_about());
		Ui::AddSkip(above);
		Settings::AddButtonWithIcon(
			above,
			tr::lng_community_create(),
			st::infoCreateDiscussionLinkButton,
			{ &st::menuBlueIconGroupCreate }
		)->addClickHandler([=] {
			navigation->uiShow()->showBox(
				Box(CreateCommunityBox, navigation, peer));
		});
		const auto wrap = above->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				above,
				object_ptr<Ui::VerticalLayout>(above)));
		const auto inner = wrap->entity();
		Ui::AddSkip(inner);
		Ui::AddDivider(inner);
		Ui::AddSkip(inner);
		Ui::AddSubsectionTitle(inner, tr::lng_community_existing());
		wrap->toggleOn(
			raw->countValue() | rpl::map(rpl::mappers::_1 > 0),
			anim::type::instant);
		wrap->finishAnimating();
		box->peerListSetAboveWidget(std::move(above));
	};
	navigation->uiShow()->showBox(Box<PeerListBox>(
		std::move(controller),
		init));
}
