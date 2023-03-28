/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_chat_filters.h"

#include "apiwrap.h"
#include "boxes/peer_list_box.h"
#include "core/application.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toasts/common_toasts.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Api {
namespace {

enum class ToggleAction {
	Adding,
	Removing,
};

enum class HeaderType {
	AddingFilter,
	AddingChats,
	AllAdded,
	Removing,
};

struct HeaderDescriptor {
	base::required<HeaderType> type;
	base::required<QString> title;
	int badge = 0;
};

class ToggleChatsController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	ToggleChatsController(
		not_null<Window::SessionController*> window,
		ToggleAction action,
		const QString &slug,
		FilterId filterId,
		const QString &title,
		std::vector<not_null<PeerData*>> chats);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	[[nodiscard]] auto selectedValue() const
		-> rpl::producer<base::flat_set<not_null<PeerData*>>>;

private:
	void setupAboveWidget();

	const not_null<Window::SessionController*> _window;

	ToggleAction _action = ToggleAction::Adding;
	QString _slug;
	FilterId _filterId = 0;
	QString _filterTitle;
	std::vector<not_null<PeerData*>> _chats;
	rpl::variable<base::flat_set<not_null<PeerData*>>> _selected;

	base::unique_qptr<Ui::PopupMenu> _menu;

	rpl::lifetime _lifetime;

};

[[nodiscard]] rpl::producer<QString> TitleText(HeaderType type) {
	// langs
	switch (type) {
	case HeaderType::AddingFilter:
		return rpl::single(u"Add Folder"_q);
	case HeaderType::AddingChats:
		return rpl::single(u"Add Chats to Folder"_q);
	case HeaderType::AllAdded:
		return rpl::single(u"Folder Already Added"_q);
	case HeaderType::Removing:
		return rpl::single(u"Remove Folder"_q);
	}
	Unexpected("HeaderType in TitleText.");
}

void FillHeader(
		not_null<Ui::VerticalLayout*> container,
		HeaderDescriptor descriptor) {
	// langs
	const auto description = (descriptor.type == HeaderType::AddingFilter)
		? (u"Do you want to add a new chat folder "_q
			+ descriptor.title
			+ u" and join its groups and channels?"_q)
		: (descriptor.type == HeaderType::AddingChats)
		? (u"Do you want to join "_q
			+ QString::number(descriptor.badge)
			+ u" chats and add them to your folder "_q
			+ descriptor.title + '?')
		: (descriptor.type == HeaderType::AllAdded)
		? (u"You have already added the folder "_q
			+ descriptor.title
			+ u" and all its chats."_q)
		: (u"Do you want to quit the chats you joined "
			"when adding the folder "_q
			+ descriptor.title + '?');
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			description,
			st::boxDividerLabel),
		st::boxRowPadding);
}

void ImportInvite(
		base::weak_ptr<Window::SessionController> weak,
		const QString &slug,
		const base::flat_set<not_null<PeerData*>> &peers) {
	Expects(!peers.empty());

	const auto peer = peers.front();
	const auto api = &peer->session().api();
	const auto callback = [=](const MTPUpdates &result) {
		api->applyUpdates(result);
	};
	const auto error = [=](const MTP::Error &error) {
		if (const auto strong = weak.get()) {
			Ui::ShowMultilineToast({
				.parentOverride = Window::Show(strong).toastParent(),
				.text = { error.description() },
			});
		}
	};
	auto inputs = peers | ranges::views::transform([](auto peer) {
		return MTPInputPeer(peer->input);
	}) | ranges::to<QVector>();
	api->request(MTPcommunities_JoinCommunityInvite(
		MTP_string(slug),
		MTP_vector<MTPInputPeer>(std::move(inputs))
	)).done(callback).fail(error).send();
}

ToggleChatsController::ToggleChatsController(
		not_null<Window::SessionController*> window,
		ToggleAction action,
		const QString &slug,
		FilterId filterId,
		const QString &title,
		std::vector<not_null<PeerData*>> chats)
: _window(window)
, _action(action)
, _slug(slug)
, _filterId(filterId)
, _filterTitle(title)
, _chats(std::move(chats)) {
}

void ToggleChatsController::prepare() {
	setupAboveWidget();
	auto selected = base::flat_set<not_null<PeerData*>>();
	for (const auto &peer : _chats) {
		auto row = std::make_unique<PeerListRow>(peer);
		const auto raw = row.get();
		delegate()->peerListAppendRow(std::move(row));
		delegate()->peerListSetRowChecked(raw, true);
		selected.emplace(peer);
	}
	delegate()->peerListRefreshRows();
	_selected = std::move(selected);
}

void ToggleChatsController::rowClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	const auto checked = row->checked();
	auto selected = _selected.current();
	delegate()->peerListSetRowChecked(row, !checked);
	if (checked) {
		selected.remove(peer);
	} else {
		selected.emplace(peer);
	}
	_selected = std::move(selected);
}

void ToggleChatsController::setupAboveWidget() {
	using namespace Settings;

	auto wrap = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = wrap.data();

	const auto type = !_filterId
		? HeaderType::AddingFilter
		: (_action == ToggleAction::Adding)
		? HeaderType::AddingChats
		: HeaderType::Removing;
	delegate()->peerListSetTitle(TitleText(type));
	FillHeader(container, {
		.type = type,
		.title = _filterTitle,
		.badge = (type == HeaderType::AddingChats) ? int(_chats.size()) : 0,
	});

	delegate()->peerListSetAboveWidget(std::move(wrap));
}

Main::Session &ToggleChatsController::session() const {
	return _window->session();
}

auto ToggleChatsController::selectedValue() const
-> rpl::producer<base::flat_set<not_null<PeerData*>>> {
	return _selected.value();
}

[[nodiscard]] void AlreadyFilterBox(
		not_null<Ui::GenericBox*> box,
		const QString &title) {
	// langs
	box->setTitle(TitleText(HeaderType::AllAdded));

	FillHeader(box->verticalLayout(), {
		.type = HeaderType::AllAdded,
		.title = title,
	});

	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
}

void ProcessFilterInvite(
		base::weak_ptr<Window::SessionController> weak,
		const QString &slug,
		FilterId filterId,
		const QString &title,
		std::vector<not_null<PeerData*>> peers) {
	const auto strong = weak.get();
	if (!strong) {
		return;
	}
	Core::App().hideMediaView();
	if (peers.empty()) {
		if (filterId) {
			strong->show(Box(AlreadyFilterBox, title));
		} else {
			Ui::ShowMultilineToast({
				.parentOverride = Window::Show(strong).toastParent(),
				.text = { tr::lng_group_invite_bad_link(tr::now) },
			});
		}
		return;
	}
	auto controller = std::make_unique<ToggleChatsController>(
		strong,
		ToggleAction::Adding,
		slug,
		filterId,
		title,
		std::move(peers));
	const auto raw = controller.get();
	auto initBox = [=](not_null<Ui::BoxContent*> box) {
		box->setStyle(st::filterInviteBox);
		raw->selectedValue(
		) | rpl::start_with_next([=](
				base::flat_set<not_null<PeerData*>> &&peers) {
			const auto count = int(peers.size());

			box->clearButtons();
			auto button = object_ptr<Ui::RoundButton>(
				box,
				rpl::single(count
					? u"Add %1 Chats"_q.arg(count)
					: u"Don't add chats"_q),
				st::defaultActiveButton);
			const auto raw = button.data();

			box->widthValue(
			) | rpl::start_with_next([=](int width) {
				const auto &padding = st::filterInviteBox.buttonPadding;
				raw->resizeToWidth(width
					- padding.left()
					- padding.right());
				raw->moveToLeft(padding.left(), padding.top());
			}, raw->lifetime());

			raw->setClickedCallback([=] {
				if (!count) {
					box->closeBox();
				//} else if (count + alreadyInFilter() >= ...) {
					// #TODO filters
				} else {
					ImportInvite(weak, slug, peers);
				}
			});

			box->addButton(std::move(button));
		}, box->lifetime());
	};
	strong->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)));
}

void ProcessFilterInvite(
		base::weak_ptr<Window::SessionController> weak,
		const QString &slug,
		FilterId filterId,
		std::vector<not_null<PeerData*>> peers) {
	const auto strong = weak.get();
	if (!strong) {
		return;
	}
	Core::App().hideMediaView();
	const auto &list = strong->session().data().chatsFilters().list();
	const auto it = ranges::find(list, filterId, &Data::ChatFilter::id);
	if (it == end(list)) {
		Ui::ShowMultilineToast({
			.parentOverride = Window::Show(strong).toastParent(),
			.text = { u"Filter not found :shrug:"_q },
		});
		return;
	}
	ProcessFilterInvite(weak, slug, filterId, it->title(), std::move(peers));
}

} // namespace

void SaveNewFilterPinned(
		not_null<Main::Session*> session,
		FilterId filterId) {
	const auto &order = session->data().pinnedChatsOrder(filterId);
	auto &filters = session->data().chatsFilters();
	const auto &filter = filters.applyUpdatedPinned(filterId, order);
	session->api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
		MTP_int(filterId),
		filter.tl()
	)).send();
}

void CheckFilterInvite(
		not_null<Window::SessionController*> controller,
		const QString &slug) {
	const auto session = &controller->session();
	const auto weak = base::make_weak(controller);
	session->api().checkFilterInvite(slug, [=](
			const MTPcommunities_CommunityInvite &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		auto title = QString();
		auto filterId = FilterId();
		auto peers = std::vector<not_null<PeerData*>>();
		auto already = std::vector<not_null<PeerData*>>();
		auto &owner = strong->session().data();
		result.match([&](const auto &data) {
			owner.processUsers(data.vusers());
			owner.processChats(data.vchats());
		});
		const auto parseList = [&](const MTPVector<MTPPeer> &list) {
			auto result = std::vector<not_null<PeerData*>>();
			result.reserve(list.v.size());
			for (const auto &peer : list.v) {
				result.push_back(owner.peer(peerFromMTP(peer)));
			}
			return result;
		};
		result.match([&](const MTPDcommunities_communityInvite &data) {
			title = qs(data.vtitle());
			peers = parseList(data.vpeers());
		}, [&](const MTPDcommunities_communityInviteAlready &data) {
			filterId = data.vfilter_id().v;
			peers = parseList(data.vmissing_peers());
			already = parseList(data.valready_peers());
		});

		const auto &filters = owner.chatsFilters();
		const auto notLoaded = filterId
			&& !ranges::contains(
				owner.chatsFilters().list(),
				filterId,
				&Data::ChatFilter::id);
		if (notLoaded) {
			const auto lifetime = std::make_shared<rpl::lifetime>();
			owner.chatsFilters().changed(
			) | rpl::start_with_next([=] {
				lifetime->destroy();
				ProcessFilterInvite(weak, slug, filterId, std::move(peers));
			}, *lifetime);
			owner.chatsFilters().reload();
		} else if (filterId) {
			ProcessFilterInvite(weak, slug, filterId, std::move(peers));
		} else {
			ProcessFilterInvite(weak, slug, filterId, title, std::move(peers));
		}
	}, [=](const MTP::Error &error) {
		if (error.code() != 400) {
			return;
		}
		ProcessFilterInvite(weak, slug, FilterId(), QString(), {});
	});
}

} // namespace Api
