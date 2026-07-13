/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/community_box.h"

#include "api/api_communities.h"
#include "apiwrap.h"
#include "boxes/peer_list_box.h"
#include "boxes/peers/add_to_community_box.h"
#include "boxes/peers/community_pending_requests_box.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_community.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_community_chats_list.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "history/history.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace {

[[nodiscard]] std::unique_ptr<PeerListRow> MakeCommunityChatRow(
		not_null<PeerData*> peer) {
	auto row = std::make_unique<PeerListRow>(peer);
	const auto channel = peer->asChannel();
	if (channel && channel->membersCountKnown()) {
		row->setCustomStatus((channel->isBroadcast()
			? tr::lng_chat_status_subscribers
			: tr::lng_chat_status_members)(
				tr::now,
				lt_count_decimal,
				channel->membersCount()));
	}
	return row;
}

class ChatsController final : public PeerListController {
public:
	ChatsController(
		not_null<Main::Session*> session,
		rpl::producer<std::vector<not_null<PeerData*>>> chats,
		Fn<void(not_null<PeerData*>)> callback,
		Fn<void(not_null<PeerListRow*>)> remove = nullptr);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

	[[nodiscard]] rpl::producer<int> countValue() const {
		return _count.value();
	}

private:
	const not_null<Main::Session*> _session;
	rpl::producer<std::vector<not_null<PeerData*>>> _chats;
	Fn<void(not_null<PeerData*>)> _callback;
	Fn<void(not_null<PeerListRow*>)> _remove;
	rpl::variable<int> _count = 0;

};

ChatsController::ChatsController(
	not_null<Main::Session*> session,
	rpl::producer<std::vector<not_null<PeerData*>>> chats,
	Fn<void(not_null<PeerData*>)> callback,
	Fn<void(not_null<PeerListRow*>)> remove)
: _session(session)
, _chats(std::move(chats))
, _callback(std::move(callback))
, _remove(std::move(remove)) {
}

Main::Session &ChatsController::session() const {
	return *_session;
}

void ChatsController::prepare() {
	std::move(
		_chats
	) | rpl::on_next([=](const std::vector<not_null<PeerData*>> &list) {
		while (delegate()->peerListFullRowsCount() > 0) {
			delegate()->peerListRemoveRow(
				delegate()->peerListRowAt(
					delegate()->peerListFullRowsCount() - 1));
		}
		for (const auto &peer : list) {
			delegate()->peerListAppendRow(MakeCommunityChatRow(peer));
		}
		delegate()->peerListRefreshRows();
		_count = int(list.size());
	}, lifetime());
}

void ChatsController::rowClicked(not_null<PeerListRow*> row) {
	_callback(row->peer());
}

base::unique_qptr<Ui::PopupMenu> ChatsController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	if (!_remove) {
		return nullptr;
	}
	const auto peer = row->peer();
	const auto channel = peer->asChannel();
	const auto broadcast = channel && channel->isBroadcast();
	const auto user = peer->asUser();
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	const auto addAction = Ui::Menu::CreateAddActionCallback(result);
	addAction({
		.text = (user
			? tr::lng_community_chat_view_bot
			: broadcast
			? tr::lng_community_chat_view_channel
			: tr::lng_community_chat_view_group)(tr::now),
		.handler = [=] { _callback(peer); },
		.icon = &st::menuIconShowInChat,
	});
	addAction({
		.text = (user
			? tr::lng_community_chat_remove_bot
			: broadcast
			? tr::lng_community_chat_remove_channel
			: tr::lng_community_chat_remove_group)(tr::now),
		.handler = [=] { _remove(row); },
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	return result;
}

} // namespace

void ShowCommunityChatJoinConfirm(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Data::CommunityInfo*> community,
		not_null<PeerData*> peer) {
	const auto channel = peer->asChannel();
	const auto joinable = channel
		&& (!community->isHidden(peer) || channel->amCreator());
	if (!joinable) {
		show->showToast(tr::lng_community_hidden_not_accessible(tr::now));
		return;
	}
	const auto join = [=](Fn<void()> close) {
		show->session().api().joinChannel(channel);
		close();
	};
	show->show(Ui::MakeConfirmBox({
		.text = tr::lng_community_join_sure(
			tr::now,
			lt_group,
			tr::bold(peer->name()),
			tr::marked),
		.confirmed = join,
		.confirmText = (channel->isMegagroup()
			? tr::lng_profile_join_group(tr::now)
			: tr::lng_profile_join_channel(tr::now)),
	}));
}

void OpenCommunityLinkedPeer(
		not_null<Window::SessionController*> controller,
		not_null<Data::CommunityInfo*> community,
		not_null<PeerData*> peer) {
	if (peer->isUser() && !community->isHidden(peer)) {
		controller->showPeerHistory(
			peer,
			Window::SectionShow::Way::ClearStack,
			ShowAtUnreadMsgId);
	} else {
		ShowCommunityChatJoinConfirm(controller->uiShow(), community, peer);
	}
}

void SetupCommunityContent(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<ChannelData*> community,
		std::shared_ptr<Main::SessionShow> show) {
	const auto info = community->communityInfo();
	if (!info) {
		return;
	}

	Ui::AddSkip(container);
	const auto toggle = Settings::AddButtonWithIcon(
		container,
		tr::lng_community_show_as_one(),
		st::settingsButtonNoIcon);
	toggle->toggleOn(Data::PeerFlagValue(
		community.get(),
		ChannelDataFlag::CommunityCollapsed));
	toggle->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		const auto flags = community->flags();
		return toggled != ((flags & ChannelDataFlag::CommunityCollapsed)
			!= 0);
	}) | rpl::on_next([=](bool toggled) {
		community->session().api().communities().toggleCollapsedInDialogs(
			community,
			toggled);
	}, toggle->lifetime());
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_community_show_as_one_about());

	if (community->canManageLinkedPeers()) {
		const auto wrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		const auto inner = wrap->entity();
		Ui::AddSkip(inner);
		auto count = Info::Profile::PendingRequestsCountValue(
			community
		) | rpl::start_spawning(wrap->lifetime());
		const auto button = Settings::AddButtonWithLabel(
			inner,
			tr::lng_community_requests_title(),
			rpl::duplicate(count) | rpl::map([](int c) {
				return QString::number(c);
			}),
			st::settingsButton,
			{});
		const auto icon = Dialogs::CreateRequestsBubbleIcon(button);
		icon->raise();
		button->sizeValue(
		) | rpl::on_next([=](QSize size) {
			icon->moveToLeft(
				st::settingsButton.iconLeft,
				(size.height() - icon->height()) / 2,
				size.width());
		}, icon->lifetime());
		button->addClickHandler([=] {
			ShowCommunityPendingRequestsBox(controller, community);
		});
		wrap->toggleOn(
			std::move(count) | rpl::map(rpl::mappers::_1 > 0),
			anim::type::instant);
		wrap->finishAnimating();
	}

	const auto addDialogsSection = [&](
			rpl::producer<QString> title,
			Dialogs::CommunityChatsKind kind) {
		const auto wrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		const auto inner = wrap->entity();
		Ui::AddSkip(inner);
		Ui::AddSubsectionTitle(inner, std::move(title));
		const auto list = inner->add(object_ptr<Dialogs::CommunityChatsList>(
			inner,
			controller,
			info,
			kind));
		list->chatChosen(
		) | rpl::on_next([=](not_null<History*> history) {
			controller->showPeerHistory(
				history,
				Window::SectionShow::Way::ClearStack,
				ShowAtUnreadMsgId);
		}, list->lifetime());
		wrap->toggleOn(
			list->countValue() | rpl::map(rpl::mappers::_1 > 0),
			anim::type::instant);
		wrap->finishAnimating();
	};

	auto requestable = info->linkedPeersValue(
	) | rpl::map([=] {
		auto result = std::vector<not_null<PeerData*>>();
		for (const auto &linked : info->linkedPeers()) {
			if (Data::CommunityChatJoined(linked.peer)) {
				continue;
			} else if (!linked.peer->isUser()
				&& Data::IsCommunityChatViewable(linked)) {
				continue;
			}
			result.push_back(linked.peer);
		}
		return result;
	}) | rpl::start_spawning(container->lifetime());

	const auto openChat = [=](not_null<PeerData*> peer) {
		OpenCommunityLinkedPeer(controller, info, peer);
	};

	const auto addPeerListSection = [&](
			rpl::producer<QString> title,
			rpl::producer<std::vector<not_null<PeerData*>>> list) {
		const auto wrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		const auto inner = wrap->entity();
		Ui::AddSkip(inner);
		Ui::AddSubsectionTitle(inner, std::move(title));

		class Delegate final : public PeerListContentDelegateSimple {
		public:
			explicit Delegate(std::shared_ptr<Main::SessionShow> show)
			: _show(std::move(show)) {
			}

			std::shared_ptr<Main::SessionShow> peerListUiShow() override {
				return _show;
			}

		private:
			const std::shared_ptr<Main::SessionShow> _show;

		};
		const auto delegate = inner->lifetime().make_state<Delegate>(show);
		const auto chatsController = inner->lifetime().make_state<
			ChatsController
		>(&community->session(), std::move(list), openChat);
		chatsController->setStyleOverrides(&st::communityInfoRequestableList);
		const auto content = inner->add(object_ptr<PeerListContent>(
			inner,
			chatsController));
		delegate->setContent(content);
		chatsController->setDelegate(delegate);

		wrap->toggleOn(
			chatsController->countValue() | rpl::map(rpl::mappers::_1 > 0),
			anim::type::instant);
		wrap->finishAnimating();
	};

	addDialogsSection(
		tr::lng_community_chats_joined(),
		Dialogs::CommunityChatsKind::Joined);
	addDialogsSection(
		tr::lng_community_chats_viewable(),
		Dialogs::CommunityChatsKind::Viewable);
	addPeerListSection(
		tr::lng_community_chats_requestable(),
		rpl::duplicate(requestable));

	if (!community->wasFullUpdated()) {
		community->session().api().requestFullPeer(community);
	}
}

void SetupCommunityEditChatsList(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> community) {
	const auto info = community->communityInfo();
	if (!info) {
		return;
	}

	auto chats = info->linkedPeersValue(
	) | rpl::map([=] {
		auto result = std::vector<not_null<PeerData*>>();
		result.reserve(info->linkedPeers().size());
		for (const auto &linked : info->linkedPeers()) {
			result.push_back(linked.peer);
		}
		return result;
	}) | rpl::start_spawning(container->lifetime());

	const auto openChat = [=](not_null<PeerData*> peer) {
		navigation->parentController()->showPeerHistory(
			peer,
			Window::SectionShow::Way::ClearStack,
			ShowAtUnreadMsgId);
	};

	const auto delegate = container->lifetime().make_state<
		PeerListContentDelegateShow
	>(show);
	const auto removeChat = [=](not_null<PeerListRow*> row) {
		const auto peer = row->peer();
		const auto remove = [=](Fn<void()> close) {
			community->session().api().communities().removePeerLink(
				community,
				peer,
				crl::guard(container, [=] {
					if (const auto found = delegate->peerListFindRow(
							peer->id.value)) {
						delegate->peerListRemoveRow(found);
						delegate->peerListRefreshRows();
					}
				}),
				crl::guard(container, [=](const QString &error) {
					show->showToast(error.isEmpty()
						? Lang::Hard::ServerError()
						: error);
				}));
			close();
		};
		show->show(Ui::MakeConfirmBox({
			.text = tr::lng_community_remove_sure(
				tr::now,
				lt_group,
				tr::bold(peer->name()),
				tr::marked),
			.confirmed = remove,
			.confirmText = tr::lng_box_remove(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	};
	const auto controller = container->lifetime().make_state<
		ChatsController
	>(&community->session(), std::move(chats), openChat, removeChat);
	controller->setStyleOverrides(&st::peerListBox);

	const auto content = container->add(
		object_ptr<PeerListContent>(container, controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	auto button = object_ptr<Ui::PaddingWrap<Ui::SettingsButton>>(
		container,
		object_ptr<Ui::SettingsButton>(
			container,
			tr::lng_community_add_chat(),
			st::inviteViaLinkButton),
		style::margins());
	const auto raw = button->entity();
	const auto icon = Ui::CreateChild<Info::Profile::FloatingIcon>(
		raw,
		st::menuIconInviteSettings,
		QPoint());
	raw->heightValue(
	) | rpl::on_next([=](int height) {
		icon->moveToLeft(
			st::communityEditAddChatIconPosition.x(),
			(height - st::menuIconInviteSettings.height()) / 2);
	}, icon->lifetime());
	raw->setClickedCallback([=] {
		ShowChooseChatToAddBox(navigation, community);
	});
	raw->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::on_next([=] {
		delegate->peerListMouseLeftGeometry();
	}, raw->lifetime());
	delegate->peerListSetAboveWidget(std::move(button));
	delegate->peerListRefreshRows();

	if (!community->wasFullUpdated()) {
		community->session().api().requestFullPeer(community);
	}
}

not_null<Ui::RoundButton*> MakeCommunityAddChatButton(
		not_null<QWidget*> parent,
		Fn<void()> clicked) {
	const auto button = Ui::CreateChild<Ui::RoundButton>(
		parent,
		tr::lng_community_add_chat(),
		st::communityAddChatButton);
	button->setFullRadius(true);
	button->setClickedCallback(std::move(clicked));
	return button;
}

namespace {

class ChooseChatController final : public PeerListController {
public:
	ChooseChatController(
		not_null<Main::Session*> session,
		not_null<ChannelData*> community,
		Fn<void(not_null<PeerData*>)> callback);
	~ChooseChatController();

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

private:
	const not_null<Main::Session*> _session;
	const not_null<ChannelData*> _community;
	Fn<void(not_null<PeerData*>)> _callback;
	mtpRequestId _requestId = 0;
	mtpRequestId _botsRequestId = 0;

};

ChooseChatController::ChooseChatController(
	not_null<Main::Session*> session,
	not_null<ChannelData*> community,
	Fn<void(not_null<PeerData*>)> callback)
: _session(session)
, _community(community)
, _callback(std::move(callback)) {
}

ChooseChatController::~ChooseChatController() {
	if (_requestId) {
		_session->api().request(_requestId).cancel();
	}
	if (_botsRequestId) {
		_session->api().request(_botsRequestId).cancel();
	}
}

Main::Session &ChooseChatController::session() const {
	return *_session;
}

void ChooseChatController::prepare() {
	delegate()->peerListSetTitle(tr::lng_community_add_chat());
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	auto candidates = std::vector<not_null<ChannelData*>>();
	_session->data().enumerateGroups([&](not_null<PeerData*> peer) {
		const auto channel = peer->asChannel();
		if (channel
			&& channel->isMegagroup()
			&& channel->amCreator()
			&& !channel->isForbidden()
			&& !channel->isMonoforum()
			&& !channel->linkedCommunityId()) {
			candidates.push_back(channel);
		}
	});
	ranges::sort(candidates, [](
			not_null<ChannelData*> a,
			not_null<ChannelData*> b) {
		return a->name().compare(b->name(), Qt::CaseInsensitive) < 0;
	});
	for (const auto &channel : candidates) {
		if (delegate()->peerListFindRow(channel->id.value)) {
			continue;
		}
		delegate()->peerListAppendRow(MakeCommunityChatRow(channel));
	}
	delegate()->peerListRefreshRows();

	using Flag = MTPchannels_GetAdminedPublicChannels::Flag;
	_requestId = _session->api().request(
		MTPchannels_GetAdminedPublicChannels(
			MTP_flags(Flag::f_for_community_peer))
	).done([=](const MTPmessages_Chats &result) {
		_requestId = 0;

		_session->data().processChats(result.match([](const auto &data)
			-> const MTPVector<MTPChat> & {
			return data.vchats();
		}));
		const auto &chats = result.match([](const auto &data) {
			return data.vchats().v;
		});
		for (const auto &chat : chats) {
			const auto peer = _session->data().processChat(chat);
			const auto channel = peer->asChannel();
			if (channel && !delegate()->peerListFindRow(channel->id.value)) {
				delegate()->peerListAppendRow(MakeCommunityChatRow(channel));
			}
		}
		delegate()->peerListRefreshRows();
	}).send();

	_botsRequestId = _session->api().request(
		MTPbots_GetAdminedBots()
	).done([=](const MTPVector<MTPUser> &result) {
		_botsRequestId = 0;

		for (const auto &user : result.v) {
			const auto bot = _session->data().processUser(user);
			if (bot->isBot()
				&& !bot->linkedCommunityId()
				&& !delegate()->peerListFindRow(bot->id.value)) {
				delegate()->peerListAppendRow(MakeCommunityChatRow(bot));
			}
		}
		delegate()->peerListRefreshRows();
	}).send();
}

void ChooseChatController::rowClicked(not_null<PeerListRow*> row) {
	_callback(row->peer());
}

} // namespace

void BanFromCommunityWithWarning(
		std::shared_ptr<Ui::Show> show,
		not_null<ChannelData*> community,
		not_null<PeerData*> participant) {
	const auto session = &community->session();
	const auto ban = [=] {
		session->api().communities().toggleParticipantBanned(
			community,
			participant,
			false,
			[=] {
				show->showToast(tr::lng_community_ban_done(
					tr::now,
					lt_user,
					participant->shortName()));
			},
			[=](const QString &error) {
				show->showToast(error.isEmpty()
					? Lang::Hard::ServerError()
					: error);
			});
	};
	session->api().communities().requestParticipantJoinedChats(
		community,
		participant,
		[=](Api::CommunityParticipantJoinedChats chats) {
			if (chats.creatorChats.empty()) {
				ban();
				return;
			}
			const auto creatorChats = chats.creatorChats;
			show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(tr::lng_community_ban_warning_title());
				box->addRow(object_ptr<Ui::FlatLabel>(
					box,
					rpl::single(tr::lng_community_ban_warning(
						tr::now,
						lt_count,
						int(creatorChats.size()),
						lt_user,
						tr::bold(participant->shortName()),
						tr::marked)),
					st::boxLabel));

				const auto container = box->verticalLayout();
				Ui::AddSkip(container);

				class Delegate final : public PeerListContentDelegateSimple {
				public:
					explicit Delegate(std::shared_ptr<Main::SessionShow> show)
					: _show(std::move(show)) {
					}

					std::shared_ptr<Main::SessionShow> peerListUiShow(
							) override {
						return _show;
					}

				private:
					const std::shared_ptr<Main::SessionShow> _show;

				};
				const auto delegate
					= container->lifetime().make_state<Delegate>(
						Main::MakeSessionShow(show, session));
				const auto controller = container->lifetime().make_state<
					ChatsController
				>(
					session,
					rpl::single(creatorChats),
					[](not_null<PeerData*>) {});
				const auto content = container->add(
					object_ptr<PeerListContent>(container, controller));
				delegate->setContent(content);
				controller->setDelegate(delegate);

				box->addButton(tr::lng_community_ban_button(), [=] {
					box->closeBox();
					ban();
				}, st::attentionBoxButton);
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			}));
		});
}

void ShowCommunityAdminBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> community) {
	navigation->uiShow()->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_community_admin_title());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_community_admin_about(
				lt_community,
				rpl::single(tr::bold(community->name())),
				tr::marked),
			st::boxLabel));
		box->addButton(tr::lng_community_admin_continue(), [=] {
			box->closeBox();
		});
		box->addButton(tr::lng_community_admin_decline(), [=] {
			const auto sure = crl::guard(box, [=](Fn<void()> &&close) {
				close();
				const auto session = &community->session();
				const auto show = box->uiShow();
				session->api().request(MTPchannels_EditAdmin(
					MTP_flags(MTPchannels_EditAdmin::Flags(0)),
					community->inputChannel(),
					session->user()->inputUser(),
					AdminRightsToMTP(ChatAdminRightsInfo()),
					MTPstring()
				)).done([=](const MTPUpdates &result) {
					session->api().applyUpdates(result);
				}).fail([=](const MTP::Error &error) {
					show->showToast(error.type());
				}).send();
				box->closeBox();
			});
			box->uiShow()->showBox(Ui::MakeConfirmBox({
				.text = tr::lng_community_admin_dismiss_text(),
				.confirmed = sure,
				.confirmText = tr::lng_box_ok(),
				.title = tr::lng_community_admin_dismiss_title(),
			}));
		});
	}));
}

void ShowChooseChatToAddBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> community) {
	const auto choose = [=](not_null<PeerData*> peer) {
		ShowAddPeerToCommunity(navigation, community, peer);
	};
	auto controller = std::make_unique<ChooseChatController>(
		&community->session(),
		community,
		choose);
	const auto init = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	navigation->uiShow()->showBox(Box<PeerListBox>(
		std::move(controller),
		init));
}
