/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_notifications_type.h"

#include "api/api_ringtones.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "boxes/ringtones_box.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_mute.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using Notify = Data::DefaultNotify;

struct Factory : AbstractSectionFactory {
	explicit Factory(Notify type) : type(type) {
	}

	object_ptr<AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller
	) const final override {
		return object_ptr<NotificationsType>(parent, controller, type);
	}

	const Notify type = {};
};

class AddExceptionBoxController final
	: public ChatsListBoxController
	, public base::has_weak_ptr {
public:
	AddExceptionBoxController(
		not_null<Main::Session*> session,
		Notify type,
		Fn<void(not_null<PeerData*>)> done);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

private:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;

	const not_null<Main::Session*> _session;
	const Notify _type;
	const Fn<void(not_null<PeerData*>)> _done;

	base::unique_qptr<Ui::PopupMenu> _menu;
	PeerData *_lastClickedPeer = nullptr;

	rpl::lifetime _lifetime;

};

class ExceptionsController final : public PeerListController {
public:
	ExceptionsController(
		not_null<Window::SessionController*> window,
		Notify type);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	void rowRightActionClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	void bringToTop(not_null<PeerData*> peer);

	[[nodiscard]] rpl::producer<int> countValue() const;

private:
	void refreshRows();
	bool appendRow(not_null<PeerData*> peer);
	std::unique_ptr<PeerListRow> createRow(not_null<PeerData*> peer) const;
	void refreshStatus(not_null<PeerListRow*> row) const;

	void sort();

	const not_null<Window::SessionController*> _window;
	const Notify _type;

	base::unique_qptr<Ui::PopupMenu> _menu;

	base::flat_map<not_null<PeerData*>, int> _topOrdered;
	int _topOrder = 0;

	rpl::variable<int> _count;

	rpl::lifetime _lifetime;

};

AddExceptionBoxController::AddExceptionBoxController(
	not_null<Main::Session*> session,
	Notify type,
	Fn<void(not_null<PeerData*>)> done)
: ChatsListBoxController(session)
, _session(session)
, _type(type)
, _done(std::move(done)) {
}

Main::Session &AddExceptionBoxController::session() const {
	return *_session;
}

void AddExceptionBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(tr::lng_notification_exceptions_add());

	_session->changes().peerUpdates(
		Data::PeerUpdate::Flag::Notifications
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return update.peer == _lastClickedPeer;
	}) | rpl::start_with_next([=] {
		if (const auto onstack = _done) {
			onstack(_lastClickedPeer);
		}
	}, _lifetime);
}

void AddExceptionBoxController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListShowRowMenu(row, true);
}

base::unique_qptr<Ui::PopupMenu> AddExceptionBoxController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);

	MuteMenu::FillMuteMenu(
		result.get(),
		peer->owner().history(peer),
		delegate()->peerListUiShow());

	// First clear _menu value, so that we don't check row positions yet.
	base::take(_menu);

	// Here unique_qptr is used like a shared pointer, where
	// not the last destroyed pointer destroys the object, but the first.
	_menu = base::unique_qptr<Ui::PopupMenu>(result.get());
	_menu->setDestroyedCallback(crl::guard(this, [=] {
		_lastClickedPeer = nullptr;
	}));
	_lastClickedPeer = peer;

	return result;
}

auto AddExceptionBoxController::createRow(not_null<History*> history)
-> std::unique_ptr<AddExceptionBoxController::Row> {
	if (Data::DefaultNotifyType(history->peer) != _type
		|| history->peer->isSelf()
		|| history->peer->isRepliesChat()) {
		return nullptr;
	}
	return std::make_unique<Row>(history);
}

ExceptionsController::ExceptionsController(
	not_null<Window::SessionController*> window,
	Notify type)
: _window(window)
, _type(type) {
}

Main::Session &ExceptionsController::session() const {
	return _window->session();
}

void ExceptionsController::prepare() {
	refreshRows();

	session().data().notifySettings().exceptionsUpdates(
	) | rpl::filter(rpl::mappers::_1 == _type) | rpl::start_with_next([=] {
		refreshRows();
	}, lifetime());

	session().changes().peerUpdates(
		Data::PeerUpdate::Flag::Notifications
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		const auto peer = update.peer;
		if (const auto row = delegate()->peerListFindRow(peer->id.value)) {
			if (peer->notify().muteUntil().has_value()) {
				refreshStatus(row);
			} else {
				delegate()->peerListRemoveRow(row);
				delegate()->peerListRefreshRows();
				_count = delegate()->peerListFullRowsCount();
			}
		}
	}, _lifetime);
}

void ExceptionsController::loadMoreRows() {
}

void ExceptionsController::bringToTop(not_null<PeerData*> peer) {
	_topOrdered[peer] = ++_topOrder;
	if (delegate()->peerListFindRow(peer->id.value)) {
		sort();
	}
}

rpl::producer<int> ExceptionsController::countValue() const {
	return _count.value();
}

void ExceptionsController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListShowRowMenu(row, true);
}

void ExceptionsController::rowRightActionClicked(
		not_null<PeerListRow*> row) {
	session().data().notifySettings().resetToDefault(row->peer());
}

void ExceptionsController::refreshRows() {
	auto seen = base::flat_set<not_null<PeerData*>>();
	const auto &list = session().data().notifySettings().exceptions(_type);
	auto removed = false, added = false;
	auto already = delegate()->peerListFullRowsCount();
	seen.reserve(std::min(int(list.size()), already));
	for (auto i = 0; i != already;) {
		const auto row = delegate()->peerListRowAt(i);
		if (list.contains(row->peer())) {
			seen.emplace(row->peer());
			++i;
		} else {
			delegate()->peerListRemoveRow(row);
			--already;
			removed = true;
		}
	}
	for (const auto &peer : list) {
		if (!seen.contains(peer)) {
			appendRow(peer);
			added = true;
		}
	}
	if (added || removed) {
		if (added) {
			sort();
		}
		delegate()->peerListRefreshRows();
		_count = delegate()->peerListFullRowsCount();
	}
}

base::unique_qptr<Ui::PopupMenu> ExceptionsController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);

	result->addAction(
		(peer->isUser()
			? tr::lng_context_view_profile
			: peer->isBroadcast()
			? tr::lng_context_view_channel
			: tr::lng_context_view_group)(tr::now),
		crl::guard(_window, [window = _window.get(), peer] {
			window->showPeerInfo(peer);
		}),
		(peer->isUser() ? &st::menuIconProfile : &st::menuIconInfo));
	result->addSeparator();

	MuteMenu::FillMuteMenu(
		result.get(),
		peer->owner().history(peer),
		_window->uiShow());

	// First clear _menu value, so that we don't check row positions yet.
	base::take(_menu);

	// Here unique_qptr is used like a shared pointer, where
	// not the last destroyed pointer destroys the object, but the first.
	_menu = base::unique_qptr<Ui::PopupMenu>(result.get());

	return result;
}

bool ExceptionsController::appendRow(not_null<PeerData*> peer) {
	delegate()->peerListAppendRow(createRow(peer));
	return true;
}

std::unique_ptr<PeerListRow> ExceptionsController::createRow(
		not_null<PeerData*> peer) const {
	auto row = std::make_unique<PeerListRowWithLink>(peer);
	row->setActionLink(tr::lng_notification_exceptions_remove(tr::now));
	refreshStatus(row.get());
	return row;
}

void ExceptionsController::refreshStatus(not_null<PeerListRow*> row) const {
	const auto peer = row->peer();
	const auto status = peer->owner().notifySettings().isMuted(peer)
		? tr::lng_notification_exceptions_muted(tr::now)
		: tr::lng_notification_exceptions_unmuted(tr::now);
	row->setCustomStatus(status);
}

void ExceptionsController::sort() {
	auto keys = base::flat_map<PeerListRowId, QString>();
	keys.reserve(delegate()->peerListFullRowsCount());
	const auto length = QString::number(_topOrder).size();
	const auto key = [&](const PeerListRow &row) {
		const auto id = row.id();
		const auto i = keys.find(id);
		if (i != end(keys)) {
			return i->second;
		}
		const auto peer = row.peer();
		const auto top = _topOrdered.find(peer);
		if (top != end(_topOrdered)) {
			const auto order = _topOrder - top->second;
			return keys.emplace(
				id,
				u"0%1"_q.arg(order, length, 10, QChar('0'))).first->second;
		}
		const auto history = peer->owner().history(peer);
		return keys.emplace(
			id,
			'1' + history->chatListNameSortKey()).first->second;
	};
	const auto predicate = [&](const PeerListRow &a, const PeerListRow &b) {
		return (key(a).compare(key(b)) < 0);
	};
	delegate()->peerListSortRows(predicate);
}

[[nodiscard]] rpl::producer<QString> Title(Notify type) {
	switch (type) {
	case Notify::User: return tr::lng_notification_title_private_chats();
	case Notify::Group: return tr::lng_notification_title_groups();
	case Notify::Broadcast: return tr::lng_notification_title_channels();
	}
	Unexpected("Type in Title.");
}

void SetupChecks(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		Notify type) {
	AddSubsectionTitle(container, Title(type));

	const auto session = &controller->session();
	const auto settings = &session->data().notifySettings();

	const auto enabled = container->add(
		CreateButton(
			container,
			tr::lng_notification_enable(),
			st::settingsButton,
			{ &st::menuIconNotifications }));
	enabled->toggleOn(
		NotificationsEnabledForTypeValue(session, type),
		true);

	enabled->setAcceptBoth();
	MuteMenu::SetupMuteMenu(
		enabled,
		enabled->clicks(
		) | rpl::filter([=](Qt::MouseButton button) {
			if (button == Qt::RightButton) {
				return true;
			} else if (settings->isMuted(type)) {
				settings->defaultUpdate(type, { .unmute = true });
				return false;
			} else {
				return true;
			}
		}) | rpl::to_empty,
		[=] { return MuteMenu::DefaultDescriptor(session, type); },
		controller->uiShow());

	const auto soundWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	soundWrap->toggleOn(enabled->toggledValue());
	soundWrap->finishAnimating();

	const auto soundInner = soundWrap->entity();
	const auto soundValue = [=] {
		const auto sound = settings->defaultSettings(type).sound();
		return !sound || !sound->none;
	};
	const auto sound = soundInner->add(
		CreateButton(
			soundInner,
			tr::lng_notification_sound(),
			st::settingsButton,
			{ &st::menuIconUnmute }));
	sound->toggleOn(rpl::single(
		soundValue()
	) | rpl::then(settings->defaultUpdates(
		type
	) | rpl::map([=] { return soundValue(); })));

	const auto toneWrap = soundInner->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	toneWrap->toggleOn(sound->toggledValue());
	toneWrap->finishAnimating();

	const auto toneInner = toneWrap->entity();
	const auto toneLabel = toneInner->lifetime(
	).make_state<rpl::event_stream<QString>>();
	const auto toneValue = [=] {
		const auto sound = settings->defaultSettings(type).sound();
		return sound.value_or(Data::NotifySound());
	};
	const auto label = [=] {
		const auto now = toneValue();
		return !now.id
			? tr::lng_ringtones_box_default(tr::now)
			: ExtractRingtoneName(session->data().document(now.id));
	};
	settings->defaultUpdates(
		Notify::User
	) | rpl::start_with_next([=] {
		toneLabel->fire(label());
	}, toneInner->lifetime());
	session->api().ringtones().listUpdates(
	) | rpl::start_with_next([=] {
		toneLabel->fire(label());
	}, toneInner->lifetime());

	const auto tone = AddButtonWithLabel(
		toneInner,
		tr::lng_notification_tone(),
		toneLabel->events_starting_with(label()),
		st::settingsButton,
		{ &st::menuIconSoundOn });

	enabled->toggledValue(
	) | rpl::filter([=](bool value) {
		return (value != NotificationsEnabledForType(session, type));
	}) | rpl::start_with_next([=](bool value) {
		settings->defaultUpdate(type, Data::MuteValue{
			.unmute = value,
			.forever = !value,
		});
	}, sound->lifetime());

	sound->toggledValue(
	) | rpl::filter([=](bool enabled) {
		const auto sound = settings->defaultSettings(type).sound();
		return (!sound || !sound->none) != enabled;
	}) | rpl::start_with_next([=](bool enabled) {
		const auto value = Data::NotifySound{ .none = !enabled };
		settings->defaultUpdate(type, {}, {}, value);
	}, sound->lifetime());

	tone->setClickedCallback([=] {
		controller->show(Box(RingtonesBox, session, toneValue(), [=](
				Data::NotifySound sound) {
			settings->defaultUpdate(type, {}, {}, sound);
		}));
	});
}

void SetupExceptions(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> window,
		Notify type) {
	const auto add = AddButton(
		container,
		tr::lng_notification_exceptions_add(),
		st::settingsButtonActive,
		{ &st::menuIconInviteSettings });

	auto controller = std::make_unique<ExceptionsController>(window, type);
	controller->setStyleOverrides(&st::settingsBlockedList);
	const auto content = container->add(
		object_ptr<PeerListContent>(container, controller.get()));

	struct State {
		std::unique_ptr<ExceptionsController> controller;
		std::unique_ptr<PeerListContentDelegateSimple> delegate;
	};
	const auto state = content->lifetime().make_state<State>();
	state->controller = std::move(controller);
	state->delegate = std::make_unique<PeerListContentDelegateSimple>();

	state->delegate->setContent(content);
	state->controller->setDelegate(state->delegate.get());

	add->setClickedCallback([=] {
		const auto box = std::make_shared<QPointer<Ui::BoxContent>>();
		const auto done = [=](not_null<PeerData*> peer) {
			state->controller->bringToTop(peer);
			if (*box) {
				(*box)->closeBox();
			}
		};
		auto controller = std::make_unique<AddExceptionBoxController>(
			&window->session(),
			type,
			crl::guard(content, done));
		auto initBox = [=](not_null<PeerListBox*> box) {
			box->addButton(tr::lng_cancel(), [box] { box->closeBox(); });
		};
		*box = window->show(
			Box<PeerListBox>(std::move(controller), std::move(initBox)));
	});

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			CreateButton(
				container,
				tr::lng_notification_exceptions_clear(),
				st::settingsAttentionButtonWithIcon,
				{ &st::menuIconDeleteAttention })));
	wrap->entity()->setClickedCallback([=] {
		const auto clear = [=](Fn<void()> close) {
			window->session().data().notifySettings().clearExceptions(type);
			close();
		};
		window->show(Ui::MakeConfirmBox({
			.text = tr::lng_notification_exceptions_clear_sure(),
			.confirmed = clear,
			.confirmText = tr::lng_notification_exceptions_clear_button(),
			.confirmStyle = &st::attentionBoxButton,
			.title = tr::lng_notification_exceptions_clear(),
		}));
	});
	wrap->toggleOn(
		state->controller->countValue() | rpl::map(rpl::mappers::_1 > 1),
		anim::type::instant);
}

} // namespace

NotificationsType::NotificationsType(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Notify type)
: AbstractSection(parent)
, _type(type) {
	setupContent(controller);
}

rpl::producer<QString> NotificationsType::title() {
	switch (_type) {
	case Notify::User: return tr::lng_notification_private_chats();
	case Notify::Group: return tr::lng_notification_groups();
	case Notify::Broadcast: return tr::lng_notification_channels();
	}
	Unexpected("Type in NotificationsType.");
}

Type NotificationsType::Id(Notify type) {
	return std::make_shared<Factory>(type);
}

void NotificationsType::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(this);

	AddSkip(container, st::settingsPrivacySkip);
	SetupChecks(container, controller, _type);

	AddSkip(container);
	AddDivider(container);
	AddSkip(container);

	SetupExceptions(container, controller, _type);

	Ui::ResizeFitChild(this, container);
}

bool NotificationsEnabledForType(
		not_null<Main::Session*> session,
		Notify type) {
	const auto settings = &session->data().notifySettings();
	const auto until = settings->defaultSettings(type).muteUntil();
	return until && (*until <= base::unixtime::now());
}

rpl::producer<bool> NotificationsEnabledForTypeValue(
		not_null<Main::Session*> session,
		Notify type) {
	const auto settings = &session->data().notifySettings();
	return rpl::single(
		rpl::empty
	) | rpl::then(
		settings->defaultUpdates(type)
	) | rpl::map([=] {
		return NotificationsEnabledForType(session, type);
	});
}

} // namespace Settings
