/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/select_future_owner_box.h"

#include "api/api_chat_participants.h"
#include "api/api_cloud_password.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "boxes/filters/edit_filter_chats_list.h"
#include "boxes/passcode_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peer_lists_box.h"
#include "boxes/peers/replace_boost_box.h" // CreateUserpicsTransfer.
#include "core/application.h"
#include "core/core_cloud_password.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "dialogs/ui/chat_search_empty.h"
#include "boxes/peers/channel_ownership_transfer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace {

enum class ParticipantType {
	Admins,
	Members
};

class FutureOwnerController : public PeerListController {
public:
	void rowClicked(not_null<PeerListRow*> row) override;
	void itemDeselectedHook(not_null<PeerData*> peer) override;

	void setOnRowClicked(Fn<void()> callback);
	rpl::producer<> itemDeselected() const;

private:
	Fn<void()> _onRowClicked;
	rpl::event_stream<> _itemDeselected;

};

void FutureOwnerController::setOnRowClicked(Fn<void()> callback) {
	_onRowClicked = callback;
}

void FutureOwnerController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(
		row,
		!delegate()->peerListIsRowChecked(row));
	for (auto i = 0; i < delegate()->peerListFullRowsCount(); ++i) {
		auto r = delegate()->peerListRowAt(i);
		if (r != row) {
			delegate()->peerListSetRowChecked(r, false);
		}
	}
	if (_onRowClicked) {
		_onRowClicked();
	}
}

void FutureOwnerController::itemDeselectedHook(not_null<PeerData*> peer) {
	_itemDeselected.fire({});
}

rpl::producer<> FutureOwnerController::itemDeselected() const {
	return _itemDeselected.events();
}

class ParticipantsController : public FutureOwnerController {
public:
	ParticipantsController(
		not_null<ChannelData*> channel,
		ParticipantType type);

	Main::Session &session() const override;
	void prepare() override;
	void loadMoreRows() override;

private:
	const not_null<ChannelData*> _channel;
	const ParticipantType _type;
	MTP::Sender _api;

	mtpRequestId _loadRequestId = 0;
	int _offset = 0;
	bool _allLoaded = false;

};

ParticipantsController::ParticipantsController(
	not_null<ChannelData*> channel,
	ParticipantType type)
: _channel(channel)
, _type(type)
, _api(&channel->session().mtp()) {
}

Main::Session &ParticipantsController::session() const {
	return _channel->session();
}

void ParticipantsController::prepare() {
	loadMoreRows();
}

void ParticipantsController::loadMoreRows() {
	if (_loadRequestId || _allLoaded) {
		return;
	}

	const auto perPage = (_offset > 0) ? 200 : 50;
	const auto participantsHash = uint64(0);
	const auto filter = (_type == ParticipantType::Admins)
		? MTP_channelParticipantsAdmins()
		: MTP_channelParticipantsRecent();

	_loadRequestId = _api.request(MTPchannels_GetParticipants(
		_channel->inputChannel(),
		filter,
		MTP_int(_offset),
		MTP_int(perPage),
		MTP_long(participantsHash)
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		_loadRequestId = 0;
		auto added = false;

		result.match([&](const MTPDchannels_channelParticipants &data) {
			const auto &[availableCount, list] = Api::ChatParticipants::Parse(
				_channel,
				data);
			for (const auto &participant : list) {
				if (const auto user = _channel->owner().userLoaded(
						participant.userId())) {
					if (delegate()->peerListFindRow(user->id.value)) {
						continue;
					}
					if (user->isBot()) {
						continue;
					}
					using Type = Api::ChatParticipant::Type;
					if ((participant.type() == Type::Creator)
						|| (_type == ParticipantType::Members
							&& (participant.type() == Type::Admin))) {
						continue;
					}
					auto row = std::make_unique<PeerListRow>(user);
					const auto promotedSince = participant.promotedSince();
					row->setCustomStatus(
						(promotedSince
							? tr::lng_select_next_owner_box_status_promoted
							: tr::lng_select_next_owner_box_status_joined)(
							tr::now,
							lt_date,
							Ui::FormatDateTime(
								base::unixtime::parse(promotedSince
									? promotedSince
									: participant.memberSince()))));
					delegate()->peerListAppendRow(std::move(row));
					added = true;
				}
			}
			if (const auto size = list.size()) {
				_offset += size;
			} else {
				_allLoaded = true;
			}
		}, [](const MTPDchannels_channelParticipantsNotModified &) {
		});

		if (!added && _offset > 0) {
			_allLoaded = true;
		}
		delegate()->peerListRefreshRows();
	}).fail([=] {
		_loadRequestId = 0;
	}).send();
}

class LegacyParticipantsController : public FutureOwnerController {
public:
	LegacyParticipantsController(
		not_null<ChatData*> chat,
		ParticipantType type);

	Main::Session &session() const override;
	void prepare() override;
	void loadMoreRows() override;

private:
	const not_null<ChatData*> _chat;
	const ParticipantType _type;

};

LegacyParticipantsController::LegacyParticipantsController(
	not_null<ChatData*> chat,
	ParticipantType type)
: _chat(chat)
, _type(type) {
}

Main::Session &LegacyParticipantsController::session() const {
	return _chat->session();
}

void LegacyParticipantsController::prepare() {
	if (_chat->noParticipantInfo()) {
		_chat->updateFullForced();
	}
	const auto &source = (_type == ParticipantType::Admins)
		? _chat->admins
		: _chat->participants;
	for (const auto &user : source) {
		if (user->isBot()) {
			continue;
		}
		if (user->id == peerFromUser(_chat->creator)) {
			continue;
		}
		if (_type == ParticipantType::Members
			&& _chat->admins.contains(user)) {
			continue;
		}
		delegate()->peerListAppendRow(
			std::make_unique<PeerListRow>(user));
	}
	delegate()->peerListRefreshRows();
}

void LegacyParticipantsController::loadMoreRows() {
}

} // namespace

void SelectFutureOwnerbox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		not_null<UserData*> user) {
	const auto content = box->verticalLayout();
	const auto channel = peer->asChannel();
	const auto chat = peer->asChat();
	const auto isGroup = peer->isMegagroup() || peer->isChat();
	const auto isLegacy = (chat != nullptr);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	content->add(
		CreateUserpicsTransfer(
			content,
			rpl::single(std::vector<not_null<PeerData*>>{
				user->session().user(),
				peer,
			}),
			user,
			UserpicsTransferType::ChannelFutureOwner),
		st::boxRowPadding);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			isGroup
				? tr::lng_leave_next_owner_box_title_group()
				: tr::lng_leave_next_owner_box_title(),
			box->getDelegate()->style().title),
		st::boxRowPadding);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	const auto adminsCount = [&] {
		if (channel) {
			return channel->adminsCount();
		} else if (chat) {
			return int(chat->admins.size()) + 1;
		}
		return 0;
	}();
	const auto adminsAreEqual = (adminsCount <= 1);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			(isLegacy
				? (adminsAreEqual
					? tr::lng_leave_next_owner_box_about_legacy
					: tr::lng_leave_next_owner_box_about_admin_legacy)
				: (adminsAreEqual
					? tr::lng_leave_next_owner_box_about
					: tr::lng_leave_next_owner_box_about_admin))(
					lt_user,
					Info::Profile::NameValue(user) | rpl::map(tr::marked),
					lt_chat,
					Info::Profile::NameValue(peer) | rpl::map(tr::marked),
					tr::rich),
			st::boxLabel),
		st::boxRowPadding);
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	const auto select = content->add(
		object_ptr<Ui::RoundButton>(
			content,
			!adminsAreEqual
				? tr::lng_select_next_owner_box()
				: tr::lng_select_next_owner_box_admin(),
			st::defaultLightButton),
		st::boxRowPadding,
		style::al_justify);
	Ui::AddSkip(content);
	const auto cancel = content->add(
		object_ptr<Ui::RoundButton>(
			content,
			tr::lng_cancel(),
			st::defaultLightButton),
		st::boxRowPadding,
		style::al_justify);
	cancel->setClickedCallback([=] {
		box->closeBox();
	});
	Ui::AddSkip(content);
	const auto leave = content->add(
		object_ptr<Ui::RoundButton>(
			content,
			isGroup
				? tr::lng_profile_leave_group()
				: tr::lng_profile_leave_channel(),
			st::attentionBoxButton),
		st::boxRowPadding,
		style::al_justify);
	leave->setClickedCallback([=, revoke = false] {
		peer->session().api().deleteConversation(peer, revoke);
		box->closeBox();
	});
	select->setClickedCallback([=] {
		const auto window = Core::App().findWindow(box);
		const auto sessionController = window
			? window->sessionController()
			: nullptr;
		if (!sessionController) {
			return;
		}

		using Pair = std::pair<
			std::unique_ptr<FutureOwnerController>,
			std::unique_ptr<FutureOwnerController>>;
		auto makeControllers = [&]() -> Pair {
			if (channel) {
				return {
					std::make_unique<ParticipantsController>(
						channel,
						ParticipantType::Admins),
					std::make_unique<ParticipantsController>(
						channel,
						ParticipantType::Members),
				};
			} else {
				return {
					std::make_unique<LegacyParticipantsController>(
						chat,
						ParticipantType::Admins),
					std::make_unique<LegacyParticipantsController>(
						chat,
						ParticipantType::Members),
				};
			}
		};
		auto [adminsOwned, membersOwned] = makeControllers();
		const auto admins = adminsOwned.get();
		const auto members = membersOwned.get();

		auto initBox = [=](not_null<PeerListsBox*> selectBox) {
			struct State {
				base::unique_qptr<Dialogs::SearchEmpty> noLists;
				rpl::event_stream<> selectionChanges;
			};
			const auto state = selectBox->lifetime().make_state<State>();
			const auto uncheckOtherList = [=](
					not_null<PeerListController*> otherController) {
				auto delegate = otherController->delegate();
				const auto full = delegate->peerListFullRowsCount();
				for (auto i = 0; i < full; ++i) {
					delegate->peerListSetRowChecked(
						delegate->peerListRowAt(i),
						false);
				}
				state->selectionChanges.fire({});
			};
			admins->setOnRowClicked([=] { uncheckOtherList(members); });
			members->setOnRowClicked([=] { uncheckOtherList(admins); });
			selectBox->setTitle(!adminsAreEqual
				? tr::lng_select_next_owner_box_title()
				: tr::lng_select_next_owner_box_title_admin());
			const auto searchEnabled = PeerListSearchMode::Enabled;
			admins->delegate()->peerListSetSearchMode(searchEnabled);
			members->delegate()->peerListSetSearchMode(searchEnabled);
			rpl::merge(
				admins->itemDeselected(),
				members->itemDeselected()
			) | rpl::on_next([=] {
				state->selectionChanges.fire({});
			}, selectBox->lifetime());
			const auto separatorAdmins = selectBox->addSeparatorBefore(
				0,
				CreatePeerListSectionSubtitle(
					selectBox,
					!isGroup
						? tr::lng_select_next_owner_box_sub_admins()
						: tr::lng_select_next_owner_box_sub_admins_group()));
			const auto separatorMembers = selectBox->addSeparatorBefore(
				1,
				CreatePeerListSectionSubtitle(
					selectBox,
					!isGroup
						? tr::lng_select_next_owner_box_sub_members()
						: tr::lng_select_next_owner_box_sub_members_group()));
			rpl::combine(
				separatorAdmins->heightValue(),
				separatorMembers->heightValue()
			) | rpl::map(
				(rpl::mappers::_1 + rpl::mappers::_2) > 0
			) | rpl::distinct_until_changed() | rpl::on_next([=](bool has) {
				qDebug() << "has" << has;
				if (has) {
					state->noLists = nullptr;
					return;
				}
				using namespace Dialogs;
				state->noLists = base::make_unique_q<SearchEmpty>(
					selectBox,
					SearchEmpty::Icon::NoResults,
					(adminsAreEqual
						? tr::lng_select_next_owner_box_empty_list
						: tr::lng_select_next_owner_box_empty_list_admin)(
							tr::rich));
				state->noLists->show();
				state->noLists->raise();
				selectBox->sizeValue(
				) | rpl::filter_size() | rpl::on_next([=](QSize s) {
					state->noLists->setMinimalHeight(s.height() / 3);
					state->noLists->resizeToWidth(s.width() / 3 * 2);
					state->noLists->moveToLeft(
						(s.width() - state->noLists->width()) / 2,
						(s.height() - state->noLists->height()) / 2);
				}, state->noLists->lifetime());
				crl::on_main(state->noLists.get(), [=] {
					state->noLists->animate();
				});
			}, selectBox->lifetime());
			{
				const auto &st = st::futureOwnerBoxSelect;
				selectBox->setStyle(st);
				auto button = object_ptr<Ui::RoundButton>(
					selectBox,
					rpl::conditional(
						state->selectionChanges.events(
						) | rpl::map([=] {
							return !selectBox->collectSelectedRows().empty();
						}),
						tr::lng_select_next_owner_box_confirm(),
						tr::lng_close()),
					st::defaultActiveButton);
				button->setTextTransform(
					Ui::RoundButton::TextTransform::NoTransform);
				const auto raw = button.data();
				rpl::combine(
					state->selectionChanges.events() | rpl::map_to(0),
					selectBox->widthValue()
				) | rpl::on_next([=](int, int width) {
					raw->resizeToWidth(width
						- st.buttonPadding.left()
						- st.buttonPadding.right());
				}, selectBox->lifetime());
				button->setFullRadius(true);
				button->setClickedCallback([=] {
					const auto selected = selectBox->collectSelectedRows();
					if (selected.empty()) {
						return selectBox->closeBox();
					}
					if (const auto user = selected.front()->asUser()) {
						auto &lifetime = selectBox->lifetime();
						lifetime.make_state<ChannelOwnershipTransfer>(
							peer,
							user,
							selectBox->uiShow(),
							[=](std::shared_ptr<Ui::Show> show) {
								const auto revoke = false;
								peer->session().api().deleteConversation(
									peer,
									revoke);
								show->hideLayer();
							})->start();
					}
				});
				selectBox->addButton(std::move(button));
				state->selectionChanges.fire({});
			}
		};

		auto controllers = std::vector<std::unique_ptr<PeerListController>>();
		controllers.reserve(2);
		controllers.push_back(std::move(adminsOwned));
		controllers.push_back(std::move(membersOwned));
		box->uiShow()->showBox(
			Box<PeerListsBox>(std::move(controllers), initBox));
	});
	for (const auto &b : { select, cancel, leave }) {
		b->setFullRadius(true);
		b->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	}
	box->setStyle(st::futureOwnerBox);
}