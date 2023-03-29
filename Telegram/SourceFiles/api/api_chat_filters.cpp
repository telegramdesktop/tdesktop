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
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/filter_link_header.h"
#include "ui/text/text_utilities.h"
#include "ui/toasts/common_toasts.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"
#include "styles/style_filter_icons.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Api {
namespace {

enum class ToggleAction {
	Adding,
	Removing,
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

	void setAddedTopHeight(int addedTopHeight);

private:
	void setupAboveWidget();
	void setupBelowWidget();

	const not_null<Window::SessionController*> _window;
	Ui::RpWidget *_addedTopWidget = nullptr;

	ToggleAction _action = ToggleAction::Adding;
	QString _slug;
	FilterId _filterId = 0;
	QString _filterTitle;
	std::vector<not_null<PeerData*>> _chats;
	rpl::variable<base::flat_set<not_null<PeerData*>>> _selected;

	base::unique_qptr<Ui::PopupMenu> _menu;

	rpl::lifetime _lifetime;

};

[[nodiscard]] tr::phrase<> TitleText(Ui::FilterLinkHeaderType type) {
	using Type = Ui::FilterLinkHeaderType;
	switch (type) {
	case Type::AddingFilter: return tr::lng_filters_by_link_title;
	case Type::AddingChats: return tr::lng_filters_by_link_more;
	case Type::AllAdded: return tr::lng_filters_by_link_already;
	case Type::Removing: return tr::lng_filters_by_link_remove;
	}
	Unexpected("Ui::FilterLinkHeaderType in TitleText.");
}

[[nodiscard]] TextWithEntities AboutText(
		Ui::FilterLinkHeaderType type,
		const QString &title) {
	using Type = Ui::FilterLinkHeaderType;
	const auto phrase = (type == Type::AddingFilter)
		? tr::lng_filters_by_link_sure
		: (type == Type::AddingChats)
		? tr::lng_filters_by_link_more_sure
		: (type == Type::AllAdded)
		? tr::lng_filters_by_link_already_about
		: tr::lng_filters_by_link_remove_sure;
	auto boldTitle = Ui::Text::Bold(title);
	return (type == Type::AddingFilter)
		? tr::lng_filters_by_link_sure(
			tr::now,
			lt_folder,
			std::move(boldTitle),
			Ui::Text::WithEntities)
		: (type == Type::AddingChats)
		? tr::lng_filters_by_link_more_sure(
			tr::now,
			lt_folder,
			std::move(boldTitle),
			Ui::Text::WithEntities)
		: (type == Type::AllAdded)
		? tr::lng_filters_by_link_already_about(
			tr::now,
			lt_folder,
			std::move(boldTitle),
			Ui::Text::WithEntities)
		: tr::lng_filters_by_link_remove_sure(
			tr::now,
			lt_folder,
			std::move(boldTitle),
			Ui::Text::WithEntities);
}

void InitFilterLinkHeader(
		not_null<PeerListBox*> box,
		Fn<void(int)> setAddedTopHeight,
		Ui::FilterLinkHeaderType type,
		const QString &title,
		rpl::producer<int> count) {
	auto header = Ui::MakeFilterLinkHeader(box, {
		.type = type,
		.title = TitleText(type)(tr::now),
		.about = AboutText(type, title),
		.folderTitle = title,
		.folderIcon = &st::foldersCustomActive,
		.badge = (type == Ui::FilterLinkHeaderType::AddingChats
			? std::move(count)
			: rpl::single(0)),
	});
	const auto widget = header.widget;
	widget->resizeToWidth(st::boxWideWidth);
	Ui::SendPendingMoveResizeEvents(widget);

	const auto min = widget->minimumHeight(), max = widget->maximumHeight();
	widget->resize(st::boxWideWidth, max);

	box->setAddedTopScrollSkip(max);
	std::move(
		header.wheelEvents
	) | rpl::start_with_next([=](not_null<QWheelEvent*> e) {
		box->sendScrollViewportEvent(e);
	}, widget->lifetime());

	struct State {
		bool processing = false;
		int addedTopHeight = 0;
	};
	const auto state = widget->lifetime().make_state<State>();

	box->scrolls(
	) | rpl::filter([=] {
		return !state->processing;
	}) | rpl::start_with_next([=] {
		state->processing = true;
		const auto guard = gsl::finally([&] { state->processing = false; });

		const auto top = box->scrollTop();
		const auto height = box->scrollHeight();
		const auto headerHeight = std::max(max - top, min);
		const auto addedTopHeight = max - headerHeight;
		widget->resize(widget->width(), headerHeight);
		if (state->addedTopHeight < addedTopHeight) {
			setAddedTopHeight(addedTopHeight);
			box->setAddedTopScrollSkip(headerHeight);
		} else {
			box->setAddedTopScrollSkip(headerHeight);
			setAddedTopHeight(addedTopHeight);
		}
		state->addedTopHeight = addedTopHeight;
		box->peerListRefreshRows();
	}, widget->lifetime());

	box->setNoContentMargin(true);
}

void ImportInvite(
		base::weak_ptr<Window::SessionController> weak,
		const QString &slug,
		const base::flat_set<not_null<PeerData*>> &peers,
		Fn<void()> done,
		Fn<void()> fail) {
	Expects(!peers.empty());

	const auto peer = peers.front();
	const auto api = &peer->session().api();
	const auto callback = [=](const MTPUpdates &result) {
		api->applyUpdates(result);
		done();
	};
	const auto error = [=](const MTP::Error &error) {
		if (const auto strong = weak.get()) {
			Ui::ShowMultilineToast({
				.parentOverride = Window::Show(strong).toastParent(),
				.text = { error.type() },
			});
		}
		fail();
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
	setStyleOverrides(&st::filterLinkChatsList);
}

void ToggleChatsController::prepare() {
	setupAboveWidget();
	setupBelowWidget();
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

	_addedTopWidget = container->add(object_ptr<Ui::RpWidget>(container));
	AddDivider(container);
	AddSubsectionTitle(
		container,
		tr::lng_filters_by_link_join(
			lt_count,
			rpl::single(float64(_chats.size()))),
		st::filterLinkSubsectionTitlePadding);

	delegate()->peerListSetAboveWidget(std::move(wrap));
}

void ToggleChatsController::setupBelowWidget() {
	delegate()->peerListSetBelowWidget(
		object_ptr<Ui::DividerLabel>(
			(QWidget*)nullptr,
			object_ptr<Ui::FlatLabel>(
				(QWidget*)nullptr,
				tr::lng_filters_by_link_about(tr::now),
				st::boxDividerLabel),
			st::settingsDividerLabelPadding));
}

Main::Session &ToggleChatsController::session() const {
	return _window->session();
}

auto ToggleChatsController::selectedValue() const
-> rpl::producer<base::flat_set<not_null<PeerData*>>> {
	return _selected.value();
}

void ToggleChatsController::setAddedTopHeight(int addedTopHeight) {
	Expects(addedTopHeight >= 0);

	_addedTopWidget->resize(_addedTopWidget->width(), addedTopHeight);
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
	if (peers.empty() && !filterId) {
		Ui::ShowMultilineToast({
			.parentOverride = Window::Show(strong).toastParent(),
			.text = { tr::lng_group_invite_bad_link(tr::now) },
		});
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
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->setStyle(st::filterInviteBox);

		using Type = Ui::FilterLinkHeaderType;
		const auto type = !filterId
			? Type::AddingFilter
			: Type::AddingChats;
		auto badge = raw->selectedValue(
		) | rpl::map([=](const base::flat_set<not_null<PeerData*>> &peers) {
			return int(peers.size());
		});
		InitFilterLinkHeader(box, [=](int addedTopHeight) {
			raw->setAddedTopHeight(addedTopHeight);
		}, type, title, rpl::duplicate(badge));

		auto owned = Ui::FilterLinkProcessButton(
			box,
			type,
			title,
			std::move(badge));

		const auto button = owned.data();
		box->widthValue(
		) | rpl::start_with_next([=](int width) {
			const auto &padding = st::filterInviteBox.buttonPadding;
			button->resizeToWidth(width
				- padding.left()
				- padding.right());
			button->moveToLeft(padding.left(), padding.top());
		}, button->lifetime());

		box->addButton(std::move(owned));

		struct State {
			bool importing = false;
		};
		const auto state = box->lifetime().make_state<State>();

		raw->selectedValue(
		) | rpl::start_with_next([=](
				base::flat_set<not_null<PeerData*>> &&peers) {
			button->setClickedCallback([=] {
				if (peers.empty()) {
					box->closeBox();
				//} else if (count + alreadyInFilter() >= ...) {
					// #TODO filters
				} else if (!state->importing) {
					state->importing = true;
					ImportInvite(weak, slug, peers, crl::guard(box, [=] {
						box->closeBox();
					}), crl::guard(box, [=] {
						state->importing = false;
					}));
				}
			});
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
