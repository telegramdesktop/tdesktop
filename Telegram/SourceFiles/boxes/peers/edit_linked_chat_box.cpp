/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_linked_chat_box.h"

#include "lang/lang_keys.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "settings/settings_common.h" // AddButton.
#include "data/data_changes.h"
#include "ui/widgets/labels.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text/text_utilities.h" // Ui::Text::RichLangValue
#include "boxes/peer_list_box.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/add_contact_box.h"
#include "apiwrap.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kEnableSearchRowsCount = 10;

class Controller : public PeerListController, public base::has_weak_ptr {
public:
	Controller(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		ChannelData *chat,
		const std::vector<not_null<PeerData*>> &chats,
		Fn<void(ChannelData*)> callback,
		Fn<void(not_null<PeerData*>)> showHistoryCallback);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	int contentWidth() const override;

private:
	void choose(not_null<ChannelData*> chat);
	void choose(not_null<ChatData*> chat);

	not_null<Window::SessionNavigation*> _navigation;
	not_null<ChannelData*> _channel;
	ChannelData *_chat = nullptr;
	std::vector<not_null<PeerData*>> _chats;
	Fn<void(ChannelData*)> _callback;
	Fn<void(not_null<PeerData*>)> _showHistoryCallback;

	ChannelData *_waitForFull = nullptr;

	rpl::event_stream<not_null<PeerData*>> _showHistoryRequest;
};

Controller::Controller(
	not_null<Window::SessionNavigation*> navigation,
	not_null<ChannelData*> channel,
	ChannelData *chat,
	const std::vector<not_null<PeerData*>> &chats,
	Fn<void(ChannelData*)> callback,
	Fn<void(not_null<PeerData*>)> showHistoryCallback)
: _navigation(navigation)
, _channel(channel)
, _chat(chat)
, _chats(std::move(chats))
, _callback(std::move(callback))
, _showHistoryCallback(std::move(showHistoryCallback)) {
	channel->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::FullInfo
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return (update.peer == _waitForFull);
	}) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		choose(std::exchange(_waitForFull, nullptr));
	}, lifetime());
}

Main::Session &Controller::session() const {
	return _channel->session();
}

int Controller::contentWidth() const {
	return st::boxWidth;
}

void Controller::prepare() {
	const auto appendRow = [&](not_null<PeerData*> chat) {
		if (delegate()->peerListFindRow(chat->id.value)) {
			return;
		}
		auto row = std::make_unique<PeerListRow>(chat);
		const auto username = chat->username();
		row->setCustomStatus(!username.isEmpty()
			? ('@' + username)
			: (chat->isChannel() && !chat->isMegagroup())
			? tr::lng_manage_linked_channel_private_status(tr::now)
			: tr::lng_manage_discussion_group_private_status(tr::now));
		delegate()->peerListAppendRow(std::move(row));
	};
	if (_chat) {
		appendRow(_chat);
	} else {
		for (const auto chat : _chats) {
			appendRow(chat);
		}
		if (_chats.size() >= kEnableSearchRowsCount) {
			delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
		}
	}
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	if (_chat != nullptr) {
		_showHistoryCallback(_chat);
		return;
	}
	const auto peer = row->peer();
	if (const auto channel = peer->asChannel()) {
		if (channel->wasFullUpdated()) {
			choose(channel);
			return;
		}
		_waitForFull = channel;
		channel->updateFull();
	} else if (const auto chat = peer->asChat()) {
		choose(chat);
	}
}

void Controller::choose(not_null<ChannelData*> chat) {
	if (chat->isForum()) {
		ShowForumForDiscussionError(_navigation);
		return;
	}
	auto text = tr::lng_manage_discussion_group_sure(
		tr::now,
		lt_group,
		Ui::Text::Bold(chat->name()),
		lt_channel,
		Ui::Text::Bold(_channel->name()),
		Ui::Text::WithEntities);
	if (!_channel->isPublic()) {
		text.append(
			"\n\n" + tr::lng_manage_linked_channel_private(tr::now));
	}
	if (!chat->isPublic()) {
		text.append(
			"\n\n" + tr::lng_manage_discussion_group_private(tr::now));
		if (chat->hiddenPreHistory()) {
			text.append("\n\n");
			text.append(tr::lng_manage_discussion_group_warning(
				tr::now,
				Ui::Text::RichLangValue));
		}
	}
	const auto sure = [=](Fn<void()> &&close) {
		close();
		const auto onstack = _callback;
		onstack(chat);
	};
	delegate()->peerListUiShow()->showBox(Ui::MakeConfirmBox({
		.text = text,
		.confirmed = sure,
		.confirmText = tr::lng_manage_discussion_group_link(tr::now),
	}));
}

void Controller::choose(not_null<ChatData*> chat) {
	auto text = tr::lng_manage_discussion_group_sure(
		tr::now,
		lt_group,
		Ui::Text::Bold(chat->name()),
		lt_channel,
		Ui::Text::Bold(_channel->name()),
		Ui::Text::WithEntities);
	if (!_channel->isPublic()) {
		text.append("\n\n" + tr::lng_manage_linked_channel_private(tr::now));
	}
	text.append("\n\n" + tr::lng_manage_discussion_group_private(tr::now));
	text.append("\n\n");
	text.append(tr::lng_manage_discussion_group_warning(
		tr::now,
		Ui::Text::RichLangValue));
	const auto sure = [=](Fn<void()> &&close) {
		close();
		const auto done = [=](not_null<ChannelData*> chat) {
			const auto onstack = _callback;
			onstack(chat);
		};
		chat->session().api().migrateChat(chat, crl::guard(this, done));
	};
	delegate()->peerListUiShow()->showBox(Ui::MakeConfirmBox({
		.text = text,
		.confirmed = sure,
		.confirmText = tr::lng_manage_discussion_group_link(tr::now),
	}));
}

[[nodiscard]] rpl::producer<TextWithEntities> About(
		not_null<ChannelData*> channel,
		ChannelData *chat) {
	if (!channel->isBroadcast()) {
		return tr::lng_manage_linked_channel_about(
			lt_channel,
			rpl::single(Ui::Text::Bold(chat->name())),
			Ui::Text::WithEntities);
	} else if (chat != nullptr) {
		return tr::lng_manage_discussion_group_about_chosen(
			lt_group,
			rpl::single(Ui::Text::Bold(chat->name())),
			Ui::Text::WithEntities);
	}
	return tr::lng_manage_discussion_group_about(Ui::Text::WithEntities);
}

[[nodiscard]] object_ptr<Ui::BoxContent> EditLinkedChatBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		ChannelData *chat,
		std::vector<not_null<PeerData*>> &&chats,
		bool canEdit,
		Fn<void(ChannelData*)> callback) {
	Expects((channel->isBroadcast() && canEdit) || (chat != nullptr));

	class ListBox final : public PeerListBox {
	public:
		ListBox(
			QWidget *parent,
			std::unique_ptr<PeerListController> controller,
			Fn<void(not_null<ListBox*>)> init)
		: PeerListBox(
			parent,
			std::move(controller),
			[=](not_null<PeerListBox*>) { init(this); }) {
		}

		void showFinished() override {
			_showFinished.fire({});
		}

		rpl::producer<> showFinishes() const {
			return _showFinished.events();
		}

	private:
		rpl::event_stream<> _showFinished;

	};

	const auto init = [=](not_null<ListBox*> box) {
		auto above = object_ptr<Ui::VerticalLayout>(box);
		Settings::AddDividerTextWithLottie(above, {
			.lottie = u"discussion"_q,
			.showFinished = box->showFinishes(),
			.about = About(channel, chat),
		});
		if (!chat) {
			Assert(channel->isBroadcast());

			Ui::AddSkip(above);
			Settings::AddButtonWithIcon(
				above,
				tr::lng_manage_discussion_group_create(),
				st::infoCreateLinkedChatButton,
				{ &st::menuIconGroupCreate }
			)->addClickHandler([=, parent = above.data()] {
				const auto guarded = crl::guard(parent, callback);
				navigation->uiShow()->showBox(Box<GroupInfoBox>(
					navigation,
					GroupInfoBox::Type::Megagroup,
					channel->name() + " Chat",
					guarded));
			});
		}
		box->peerListSetAboveWidget(std::move(above));

		auto below = object_ptr<Ui::VerticalLayout>(box);
		if (chat && canEdit) {
			Settings::AddButtonWithIcon(
				below,
				(channel->isBroadcast()
					? tr::lng_manage_discussion_group_unlink
					: tr::lng_manage_linked_channel_unlink)(),
				st::infoUnlinkChatButton,
				{ &st::menuIconRemove }
			)->addClickHandler([=] { callback(nullptr); });
			Ui::AddSkip(below);
		}
		Ui::AddDividerText(
			below,
			(channel->isBroadcast()
				? tr::lng_manage_discussion_group_posted
				: tr::lng_manage_linked_channel_posted)());
		box->peerListSetBelowWidget(std::move(below));

		box->setTitle(channel->isBroadcast()
			? tr::lng_manage_discussion_group()
			: tr::lng_manage_linked_channel());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	};
	auto showHistoryCallback = [=](not_null<PeerData*> peer) {
		navigation->showPeerHistory(
			peer,
			Window::SectionShow::Way::ClearStack,
			ShowAtUnreadMsgId);
	};
	auto controller = std::make_unique<Controller>(
		navigation,
		channel,
		chat,
		std::move(chats),
		std::move(callback),
		std::move(showHistoryCallback));
	return Box<ListBox>(std::move(controller), init);
}

} // namespace

object_ptr<Ui::BoxContent> EditLinkedChatBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		std::vector<not_null<PeerData*>> &&chats,
		Fn<void(ChannelData*)> callback) {
	return EditLinkedChatBox(
		navigation,
		channel,
		nullptr,
		std::move(chats),
		true,
		callback);
}

object_ptr<Ui::BoxContent> EditLinkedChatBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		not_null<ChannelData*> chat,
		bool canEdit,
		Fn<void(ChannelData*)> callback) {
	return EditLinkedChatBox(
		navigation,
		channel,
		chat,
		{},
		canEdit,
		callback);
}

void ShowForumForDiscussionError(
		not_null<Window::SessionNavigation*> navigation) {
	navigation->showToast(
		tr::lng_forum_topics_no_discussion(
			tr::now,
			Ui::Text::RichLangValue));
}
