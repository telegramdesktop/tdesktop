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
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "boxes/peer_list_box.h"
#include "boxes/confirm_box.h"
#include "boxes/add_contact_box.h"
#include "apiwrap.h"
#include "facades.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace {

constexpr auto kEnableSearchRowsCount = 10;

class Controller : public PeerListController, public base::has_weak_ptr {
public:
	Controller(
		not_null<ChannelData*> channel,
		ChannelData *chat,
		const std::vector<not_null<PeerData*>> &chats,
		Fn<void(ChannelData*)> callback);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	int contentWidth() const override;

private:
	void choose(not_null<ChannelData*> chat);
	void choose(not_null<ChatData*> chat);

	not_null<ChannelData*> _channel;
	ChannelData *_chat = nullptr;
	std::vector<not_null<PeerData*>> _chats;
	Fn<void(ChannelData*)> _callback;

	ChannelData *_waitForFull = nullptr;

};

Controller::Controller(
	not_null<ChannelData*> channel,
	ChannelData *chat,
	const std::vector<not_null<PeerData*>> &chats,
	Fn<void(ChannelData*)> callback)
: _channel(channel)
, _chat(chat)
, _chats(std::move(chats))
, _callback(std::move(callback)) {
	base::ObservableViewer(
		channel->session().api().fullPeerUpdated()
	) | rpl::start_with_next([=](PeerData *peer) {
		if (peer == _waitForFull) {
			choose(std::exchange(_waitForFull, nullptr));
		}
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
		const auto username = chat->userName();
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
		Ui::showPeerHistory(_chat, ShowAtUnreadMsgId);
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
	auto text = tr::lng_manage_discussion_group_sure(
		tr::now,
		lt_group,
		Ui::Text::Bold(chat->name),
		lt_channel,
		Ui::Text::Bold(_channel->name),
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
	Ui::show(
		Box<ConfirmBox>(
			text,
			tr::lng_manage_discussion_group_link(tr::now),
			sure),
		Ui::LayerOption::KeepOther);
}

void Controller::choose(not_null<ChatData*> chat) {
	auto text = tr::lng_manage_discussion_group_sure(
		tr::now,
		lt_group,
		Ui::Text::Bold(chat->name),
		lt_channel,
		Ui::Text::Bold(_channel->name),
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
	Ui::show(
		Box<ConfirmBox>(
			text,
			tr::lng_manage_discussion_group_link(tr::now),
			sure),
		Ui::LayerOption::KeepOther);
}

object_ptr<Ui::RpWidget> SetupAbout(
		not_null<QWidget*> parent,
		not_null<ChannelData*> channel,
		ChannelData *chat) {
	auto about = object_ptr<Ui::FlatLabel>(
		parent,
		QString(),
		st::linkedChatAbout);
	about->setMarkedText([&] {
		if (!channel->isBroadcast()) {
			return tr::lng_manage_linked_channel_about(
				tr::now,
				lt_channel,
				Ui::Text::Bold(chat->name),
				Ui::Text::WithEntities);
		} else if (chat != nullptr) {
			return tr::lng_manage_discussion_group_about_chosen(
				tr::now,
				lt_group,
				Ui::Text::Bold(chat->name),
				Ui::Text::WithEntities);
		}
		return tr::lng_manage_discussion_group_about(
			tr::now,
			Ui::Text::WithEntities);
	}());
	return about;
}

object_ptr<Ui::RpWidget> SetupFooter(
		not_null<QWidget*> parent,
		not_null<ChannelData*> channel) {
	return object_ptr<Ui::FlatLabel>(
		parent,
		(channel->isBroadcast()
			? tr::lng_manage_discussion_group_posted
			: tr::lng_manage_linked_channel_posted)(),
		st::linkedChatAbout);
}

object_ptr<Ui::RpWidget> SetupCreateGroup(
		not_null<QWidget*> parent,
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		Fn<void(ChannelData*)> callback) {
	Expects(channel->isBroadcast());

	auto result = object_ptr<Ui::SettingsButton>(
		parent,
		tr::lng_manage_discussion_group_create(
		) | Ui::Text::ToUpper(),
		st::infoCreateLinkedChatButton);
	result->addClickHandler([=] {
		const auto guarded = crl::guard(parent, callback);
		Ui::show(
			Box<GroupInfoBox>(
				navigation,
				GroupInfoBox::Type::Megagroup,
				channel->name + " Chat",
				guarded),
			Ui::LayerOption::KeepOther);
	});
	return result;
}

object_ptr<Ui::RpWidget> SetupUnlink(
		not_null<QWidget*> parent,
		not_null<ChannelData*> channel,
		Fn<void(ChannelData*)> callback) {
	auto result = object_ptr<Ui::SettingsButton>(
		parent,
		(channel->isBroadcast()
			? tr::lng_manage_discussion_group_unlink
			: tr::lng_manage_linked_channel_unlink)() | Ui::Text::ToUpper(),
		st::infoUnlinkChatButton);
	result->addClickHandler([=] {
		callback(nullptr);
	});
	return result;
}

object_ptr<Ui::BoxContent> EditLinkedChatBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		ChannelData *chat,
		std::vector<not_null<PeerData*>> &&chats,
		bool canEdit,
		Fn<void(ChannelData*)> callback) {
	Expects((channel->isBroadcast() && canEdit) || (chat != nullptr));

	const auto init = [=](not_null<PeerListBox*> box) {
		auto above = object_ptr<Ui::VerticalLayout>(box);
		above->add(
			SetupAbout(above, channel, chat),
			st::linkedChatAboutPadding);
		if (!chat) {
			above->add(SetupCreateGroup(
				above,
				navigation,
				channel,
				callback));
		}
		box->peerListSetAboveWidget(std::move(above));

		auto below = object_ptr<Ui::VerticalLayout>(box);
		if (chat && canEdit) {
			below->add(SetupUnlink(below, channel, callback));
		}
		below->add(
			SetupFooter(below, channel),
			st::linkedChatAboutPadding);
		box->peerListSetBelowWidget(std::move(below));

		box->setTitle(channel->isBroadcast()
			? tr::lng_manage_discussion_group()
			: tr::lng_manage_linked_channel());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	};
	auto controller = std::make_unique<Controller>(
		channel,
		chat,
		std::move(chats),
		std::move(callback));
	return Box<PeerListBox>(std::move(controller), init);
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
