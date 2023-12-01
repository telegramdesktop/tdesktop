/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/choose_peer_box.h"

#include "apiwrap.h" // ApiWrap::botCommonGroups / requestBotCommonGroups.
#include "boxes/add_contact_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/premium_limits_box.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item_reply_markup.h"
#include "info/profile/info_profile_icon.h"
#include "lang/lang_keys.h"
#include "main/main_session.h" // Session::api().
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

namespace {

class ChoosePeerBoxController final
	: public ChatsListBoxController
	, public base::has_weak_ptr {
public:
	ChoosePeerBoxController(
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> bot,
		RequestPeerQuery query,
		Fn<void(not_null<PeerData*>)> callback);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;

	QString savedMessagesChatStatus() const override {
		return tr::lng_saved_forward_here(tr::now);
	}

private:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	QString emptyBoxText() const override;

	void prepareRestrictions();

	const not_null<Window::SessionNavigation*> _navigation;
	not_null<UserData*> _bot;
	RequestPeerQuery _query;
	base::flat_set<not_null<PeerData*>> _commonGroups;
	Fn<void(not_null<PeerData*>)> _callback;

};

using RightsMap = std::vector<std::pair<ChatAdminRight, tr::phrase<>>>;

[[nodiscard]] RightsMap GroupRights() {
	using Flag = ChatAdminRight;
	return {
		{ Flag::ChangeInfo, tr::lng_request_group_change_info },
		{
			Flag::DeleteMessages,
			tr::lng_request_group_delete_messages },
		{ Flag::BanUsers, tr::lng_request_group_ban_users },
		{ Flag::InviteByLinkOrAdd, tr::lng_request_group_invite },
		{ Flag::PinMessages, tr::lng_request_group_pin_messages },
		{ Flag::ManageTopics, tr::lng_request_group_manage_topics },
		{
			Flag::ManageCall,
			tr::lng_request_group_manage_video_chats },
		{ Flag::Anonymous, tr::lng_request_group_anonymous },
		{ Flag::AddAdmins, tr::lng_request_group_add_admins },
	};
}

[[nodiscard]] RightsMap BroadcastRights() {
	using Flag = ChatAdminRight;
	return {
		{ Flag::ChangeInfo, tr::lng_request_channel_change_info },
		{
			Flag::PostMessages,
			tr::lng_request_channel_post_messages },
		{
			Flag::EditMessages,
			tr::lng_request_channel_edit_messages },
		{
			Flag::DeleteMessages,
			tr::lng_request_channel_delete_messages },
		{
			Flag::InviteByLinkOrAdd,
			tr::lng_request_channel_add_subscribers },
		{
			Flag::ManageCall,
			tr::lng_request_channel_manage_livestreams },
		{ Flag::AddAdmins, tr::lng_request_channel_add_admins },
	};
}

[[nodiscard]] QString RightsText(
		ChatAdminRights rights,
		const RightsMap &phrases) {
	auto list = QStringList();
	for (const auto &[flag, phrase] : phrases) {
		if (rights & flag) {
			list.push_back(phrase(tr::now));
		}
	}
	const auto count = list.size();
	if (!count) {
		return QString();
	}
	const auto last = list.back();
	return (count > 1)
		? tr::lng_request_peer_rights_and(
			tr::now,
			lt_rights,
			list.mid(0, count - 1).join(", "),
			lt_last,
			last)
		: last;
}

[[nodiscard]] QString GroupRightsText(ChatAdminRights rights) {
	return RightsText(rights, GroupRights());
}

[[nodiscard]] QString BroadcastRightsText(ChatAdminRights rights) {
	return RightsText(rights, BroadcastRights());
}

[[nodiscard]] QStringList RestrictionsList(RequestPeerQuery query) {
	using Type = RequestPeerQuery::Type;
	using Restriction = RequestPeerQuery::Restriction;
	auto result = QStringList();
	const auto addRestriction = [&](
			Restriction value,
			tr::phrase<> yes,
			tr::phrase<> no) {
		if (value == Restriction::Yes) {
			result.push_back(yes(tr::now));
		} else if (value == Restriction::No) {
			result.push_back(no(tr::now));
		}
	};
	const auto addRights = [&](const QString &rights) {
		if (!rights.isEmpty()) {
			result.push_back(
				tr::lng_request_peer_rights(tr::now, lt_rights, rights));
		}
	};
	switch (query.type) {
	case Type::User:
		if (query.userIsBot != Restriction::Yes) {
			addRestriction(
				query.userIsPremium,
				tr::lng_request_user_premium_yes,
				tr::lng_request_user_premium_no);
		}
		break;
	case Type::Group:
		addRestriction(
			query.hasUsername,
			tr::lng_request_group_public_yes,
			tr::lng_request_group_public_no);
		addRestriction(
			query.groupIsForum,
			tr::lng_request_group_topics_yes,
			tr::lng_request_group_topics_no);
		if (query.amCreator) {
			result.push_back(tr::lng_request_group_am_owner(tr::now));
		} else {
			addRights(GroupRightsText(query.myRights));
		}
		break;
	case Type::Broadcast:
		addRestriction(
			query.hasUsername,
			tr::lng_request_channel_public_yes,
			tr::lng_request_channel_public_no);
		if (query.amCreator) {
			result.push_back(tr::lng_request_channel_am_owner(tr::now));
		} else {
			addRights(BroadcastRightsText(query.myRights));
		}
		break;
	}
	return result;
}

object_ptr<Ui::BoxContent> MakeConfirmBox(
		not_null<UserData*> bot,
		not_null<PeerData*> peer,
		RequestPeerQuery query,
		Fn<void()> confirmed) {
	const auto name = peer->name();
	const auto botName = bot->name();
	auto text = tr::lng_request_peer_confirm(
		tr::now,
		lt_chat,
		Ui::Text::Bold(name),
		lt_bot,
		Ui::Text::Bold(botName),
		Ui::Text::WithEntities);
	if (!peer->isUser()) {
		const auto rights = peer->isBroadcast()
			? BroadcastRightsText(query.botRights)
			: GroupRightsText(query.botRights);
		if (!rights.isEmpty()) {
			text.append('\n').append('\n').append(
				tr::lng_request_peer_confirm_rights(
					tr::now,
					lt_bot,
					Ui::Text::Bold(botName),
					lt_chat,
					Ui::Text::Bold(name),
					lt_rights,
					TextWithEntities{ rights },
					Ui::Text::WithEntities));
		} else if (!peer->isBroadcast() && query.isBotParticipant) {
			const auto common = bot->session().api().botCommonGroups(bot);
			if (!common || !ranges::contains(*common, peer)) {
				text.append('\n').append('\n').append(
					tr::lng_request_peer_confirm_add(
						tr::now,
						lt_bot,
						Ui::Text::Bold(botName),
						lt_chat,
						Ui::Text::Bold(name),
						Ui::Text::WithEntities));
			}
		}
	}
	return Ui::MakeConfirmBox({
		.text = std::move(text),
		.confirmed = [=](Fn<void()> close) { confirmed(); close(); },
		.confirmText = tr::lng_request_peer_confirm_send(tr::now),
	});
}

object_ptr<Ui::BoxContent> CreatePeerByQueryBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> bot,
		RequestPeerQuery query,
		Fn<void(not_null<PeerData*>)> done) {
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	auto callback = [=](not_null<PeerData*> peer) {
		done(peer);
		if (const auto strong = weak->data()) {
			strong->closeBox();
		}
	};
	auto result = Box<GroupInfoBox>(
		navigation,
		bot,
		query,
		std::move(callback));
	*weak = result.data();
	return result;
}

[[nodiscard]] bool FilterPeerByQuery(
		not_null<PeerData*> peer,
		RequestPeerQuery query,
		const base::flat_set<not_null<PeerData*>> &commonGroups) {
	using Type = RequestPeerQuery::Type;
	using Restriction = RequestPeerQuery::Restriction;
	const auto checkRestriction = [](Restriction restriction, bool value) {
		return (restriction == Restriction::Any)
			|| ((restriction == Restriction::Yes) == value);
	};
	const auto checkRights = [](
			ChatAdminRights wanted,
			bool creator,
			ChatAdminRights rights) {
		return creator || ((rights & wanted) == wanted);
	};
	switch (query.type) {
	case Type::User: {
		const auto user = peer->asUser();
		return user
			&& checkRestriction(query.userIsBot, user->isBot())
			&& checkRestriction(query.userIsPremium, user->isPremium());
	}
	case Type::Group: {
		const auto chat = peer->asChat();
		const auto megagroup = peer->asMegagroup();
		return (chat || megagroup)
			&& (!query.amCreator
				|| (chat ? chat->amCreator() : megagroup->amCreator()))
			&& checkRestriction(query.groupIsForum, peer->isForum())
			&& checkRestriction(
				query.hasUsername,
				megagroup && megagroup->hasUsername())
			&& checkRights(
				query.myRights,
				chat ? chat->amCreator() : megagroup->amCreator(),
				chat ? chat->adminRights() : megagroup->adminRights())
			&& (!query.isBotParticipant
				|| query.myRights
				|| commonGroups.contains(peer)
				|| (chat
					? chat->canAddMembers()
					: megagroup->canAddMembers()));
	}
	case Type::Broadcast: {
		const auto broadcast = peer->asBroadcast();
		return broadcast
			&& (!query.amCreator || broadcast->amCreator())
			&& checkRestriction(query.hasUsername, broadcast->hasUsername())
			&& checkRights(
				query.myRights,
				broadcast->amCreator(),
				broadcast->adminRights());
	}
	}
	Unexpected("Type in FilterPeerByQuery.");
}

ChoosePeerBoxController::ChoosePeerBoxController(
	not_null<Window::SessionNavigation*> navigation,
	not_null<UserData*> bot,
	RequestPeerQuery query,
	Fn<void(not_null<PeerData*>)> callback)
: ChatsListBoxController(&navigation->session())
, _navigation(navigation)
, _bot(bot)
, _query(query)
, _callback(std::move(callback)) {
	if (const auto list = _bot->session().api().botCommonGroups(_bot)) {
		_commonGroups = { begin(*list), end(*list) };
	}
}

Main::Session &ChoosePeerBoxController::session() const {
	return _navigation->session();
}

void ChoosePeerBoxController::prepareRestrictions() {
	auto above = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto raw = above.data();
	auto rows = RestrictionsList(_query);
	if (!rows.empty()) {
		Ui::AddSubsectionTitle(
			raw,
			tr::lng_request_peer_requirements(),
			{ 0, st::membersMarginTop, 0, 0 });
		const auto skip = st::defaultSubsectionTitlePadding.left();
		auto separator = QString::fromUtf8("\n\xE2\x80\xA2 ");
		raw->add(
			object_ptr<Ui::FlatLabel>(
				raw,
				separator + rows.join(separator),
				st::requestPeerRestriction),
			{ skip, 0, skip, st::membersMarginTop });
		Ui::AddDivider(raw);
	}
	const auto make = [&](tr::phrase<> text, const style::icon &st) {
		auto button = raw->add(
			object_ptr<Ui::SettingsButton>(
				raw,
				text(),
				st::inviteViaLinkButton),
			{ 0, st::membersMarginTop, 0, 0 });
		const auto icon = Ui::CreateChild<Info::Profile::FloatingIcon>(
			button,
			st,
			QPoint());
		button->heightValue(
		) | rpl::start_with_next([=](int height) {
			icon->moveToLeft(
				st::choosePeerCreateIconLeft,
				(height - st::inviteViaLinkIcon.height()) / 2);
		}, icon->lifetime());

		button->setClickedCallback([=] {
			_navigation->parentController()->show(
				CreatePeerByQueryBox(_navigation, _bot, _query, _callback));
		});

		button->events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return (e->type() == QEvent::Enter);
		}) | rpl::start_with_next([=] {
			delegate()->peerListMouseLeftGeometry();
		}, button->lifetime());
		return button;
	};
	if (_query.type == RequestPeerQuery::Type::Group) {
		make(tr::lng_request_group_create, st::choosePeerGroupIcon);
	} else if (_query.type == RequestPeerQuery::Type::Broadcast) {
		make(tr::lng_request_channel_create, st::choosePeerChannelIcon);
	}

	if (raw->count() > 0) {
		delegate()->peerListSetAboveWidget(std::move(above));
	}
}

void ChoosePeerBoxController::prepareViewHook() {
	delegate()->peerListSetTitle([&] {
		using Type = RequestPeerQuery::Type;
		using Restriction = RequestPeerQuery::Restriction;
		switch (_query.type) {
		case Type::User: return (_query.userIsBot == Restriction::Yes)
			? tr::lng_request_bot_title()
			: tr::lng_request_user_title();
		case Type::Group: return tr::lng_request_group_title();
		case Type::Broadcast: return tr::lng_request_channel_title();
		}
		Unexpected("Type in RequestPeerQuery.");
	}());
	prepareRestrictions();
}

void ChoosePeerBoxController::rowClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	const auto done = [callback = _callback, peer] {
		const auto onstack = callback;
		onstack(peer);
	};
	if (const auto user = peer->asUser()) {
		done();
	} else {
		delegate()->peerListUiShow()->showBox(
			MakeConfirmBox(_bot, peer, _query, done));
	}
}

auto ChoosePeerBoxController::createRow(not_null<History*> history)
-> std::unique_ptr<Row> {
	return FilterPeerByQuery(history->peer, _query, _commonGroups)
		? std::make_unique<Row>(history)
		: nullptr;
}

QString ChoosePeerBoxController::emptyBoxText() const {
	using Type = RequestPeerQuery::Type;
	using Restriction = RequestPeerQuery::Restriction;

	const auto result = [](tr::phrase<> title, tr::phrase<> text) {
		return title(tr::now) + "\n\n" + text(tr::now);
	};
	switch (_query.type) {
	case Type::User: return (_query.userIsBot == Restriction::Yes)
		? result(tr::lng_request_bot_no, tr::lng_request_bot_no_about)
		: result(tr::lng_request_user_no, tr::lng_request_user_no_about);
	case Type::Group:
		return result(
			tr::lng_request_group_no,
			tr::lng_request_group_no_about);
	case Type::Broadcast:
		return result(
			tr::lng_request_channel_no,
			tr::lng_request_channel_no_about);
	}
	Unexpected("Type in ChoosePeerBoxController::emptyBoxText.");
}

} // namespace

void ShowChoosePeerBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> bot,
		RequestPeerQuery query,
		Fn<void(not_null<PeerData*>)> chosen) {
	const auto needCommonGroups = query.isBotParticipant
		&& (query.type == RequestPeerQuery::Type::Group)
		&& !query.myRights;
	if (needCommonGroups && !bot->session().api().botCommonGroups(bot)) {
		const auto weak = base::make_weak(navigation);
		bot->session().api().requestBotCommonGroups(bot, [=] {
			if (const auto strong = weak.get()) {
				ShowChoosePeerBox(strong, bot, query, chosen);
			}
		});
		return;
	}
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_cancel(), [box] {
			box->closeBox();
		});
	};
	auto callback = [=, done = std::move(chosen)](not_null<PeerData*> peer) {
		done(peer);
		if (const auto strong = weak->data()) {
			strong->closeBox();
		}
	};
	*weak = navigation->parentController()->show(Box<PeerListBox>(
		std::make_unique<ChoosePeerBoxController>(
			navigation,
			bot,
			query,
			std::move(callback)),
		std::move(initBox)));
}
