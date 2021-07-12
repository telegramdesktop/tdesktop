/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_privacy_security.h"

#include "api/api_authorizations.h"
#include "api/api_self_destruct.h"
#include "api/api_sensitive_content.h"
#include "api/api_global_privacy.h"
#include "settings/settings_common.h"
#include "settings/settings_privacy_controllers.h"
#include "base/timer_rpl.h"
#include "boxes/peer_list_box.h"
#include "boxes/edit_privacy_box.h"
#include "boxes/passcode_box.h"
#include "boxes/auto_lock_box.h"
#include "boxes/sessions_box.h"
#include "boxes/confirm_box.h"
#include "boxes/self_destruction_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/layers/generic_box.h"
#include "calls/calls_instance.h"
#include "core/core_cloud_password.h"
#include "core/update_checker.h"
#include "base/platform/base_platform_last_input.h"
#include "lang/lang_keys.h"
#include "data/data_session.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "facades.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QtGui/QGuiApplication>

namespace Settings {
namespace {

constexpr auto kUpdateTimeout = 60 * crl::time(1000);

using Privacy = ApiWrap::Privacy;

QString PrivacyBase(Privacy::Key key, Privacy::Option option) {
	using Key = Privacy::Key;
	using Option = Privacy::Option;
	switch (key) {
	case Key::CallsPeer2Peer:
		switch (option) {
		case Option::Everyone:
			return tr::lng_edit_privacy_calls_p2p_everyone(tr::now);
		case Option::Contacts:
			return tr::lng_edit_privacy_calls_p2p_contacts(tr::now);
		case Option::Nobody:
			return tr::lng_edit_privacy_calls_p2p_nobody(tr::now);
		}
		Unexpected("Value in Privacy::Option.");
	default:
		switch (option) {
		case Option::Everyone: return tr::lng_edit_privacy_everyone(tr::now);
		case Option::Contacts: return tr::lng_edit_privacy_contacts(tr::now);
		case Option::Nobody: return tr::lng_edit_privacy_nobody(tr::now);
		}
		Unexpected("Value in Privacy::Option.");
	}
}

rpl::producer<QString> PrivacyString(
		not_null<::Main::Session*> session,
		Privacy::Key key) {
	session->api().reloadPrivacy(key);
	return session->api().privacyValue(
		key
	) | rpl::map([=](const Privacy &value) {
		auto add = QStringList();
		if (const auto never = ExceptionUsersCount(value.never)) {
			add.push_back("-" + QString::number(never));
		}
		if (const auto always = ExceptionUsersCount(value.always)) {
			add.push_back("+" + QString::number(always));
		}
		if (!add.isEmpty()) {
			return PrivacyBase(key, value.option)
				+ " (" + add.join(", ") + ")";
		} else {
			return PrivacyBase(key, value.option);
		}
	});
}

rpl::producer<int> BlockedPeersCount(not_null<::Main::Session*> session) {
	return session->api().blockedPeersSlice(
	) | rpl::map([=](const ApiWrap::BlockedPeersSlice &data) {
		return data.total;
	});
}

void SetupPrivacy(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger) {
	AddSkip(container, st::settingsPrivacySkip);
	AddSubsectionTitle(container, tr::lng_settings_privacy_title());

	const auto session = &controller->session();
	auto count = rpl::combine(
		BlockedPeersCount(session),
		tr::lng_settings_no_blocked_users()
	) | rpl::map([](int count, const QString &none) {
		return count ? QString::number(count) : none;
	});
	const auto blockedPeers = AddButtonWithLabel(
		container,
		tr::lng_settings_blocked_users(),
		std::move(count),
		st::settingsButton);
	blockedPeers->addClickHandler([=] {
		const auto initBox = [=](not_null<PeerListBox*> box) {
			box->addButton(tr::lng_close(), [=] {
				box->closeBox();
			});
			box->addLeftButton(tr::lng_blocked_list_add(), [=] {
				BlockedBoxController::BlockNewPeer(controller);
			});
		};
		controller->show(Box<PeerListBox>(
			std::make_unique<BlockedBoxController>(controller),
			initBox));
	});
	std::move(
		updateTrigger
	) | rpl::start_with_next([=] {
		session->api().reloadBlockedPeers();
	}, blockedPeers->lifetime());

	using Key = Privacy::Key;
	const auto add = [&](
			rpl::producer<QString> label,
			Key key,
			auto controllerFactory) {
		AddPrivacyButton(
			controller,
			container,
			std::move(label),
			key,
			controllerFactory);
	};
	add(
		tr::lng_settings_phone_number_privacy(),
		Key::PhoneNumber,
		[] { return std::make_unique<PhoneNumberPrivacyController>(); });
	add(
		tr::lng_settings_last_seen(),
		Key::LastSeen,
		[=] { return std::make_unique<LastSeenPrivacyController>(session); });
	add(
		tr::lng_settings_forwards_privacy(),
		Key::Forwards,
		[=] { return std::make_unique<ForwardsPrivacyController>(
			controller); });
	add(
		tr::lng_settings_profile_photo_privacy(),
		Key::ProfilePhoto,
		[] { return std::make_unique<ProfilePhotoPrivacyController>(); });
	add(
		tr::lng_settings_calls(),
		Key::Calls,
		[] { return std::make_unique<CallsPrivacyController>(); });
	add(
		tr::lng_settings_groups_invite(),
		Key::Invites,
		[] { return std::make_unique<GroupsInvitePrivacyController>(); });

	session->api().reloadPrivacy(ApiWrap::Privacy::Key::AddedByPhone);

	AddSkip(container, st::settingsPrivacySecurityPadding);
	AddDividerText(container, tr::lng_settings_group_privacy_about());
}

void SetupArchiveAndMute(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	using namespace rpl::mappers;

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();

	AddSkip(inner);
	AddSubsectionTitle(inner, tr::lng_settings_new_unknown());

	const auto session = &controller->session();

	const auto privacy = &session->api().globalPrivacy();
	privacy->reload();
	AddButton(
		inner,
		tr::lng_settings_auto_archive(),
		st::settingsButton
	)->toggleOn(
		privacy->archiveAndMute()
	)->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return toggled != privacy->archiveAndMuteCurrent();
	}) | rpl::start_with_next([=](bool toggled) {
		privacy->update(toggled);
	}, container->lifetime());

	AddSkip(inner);
	AddDividerText(inner, tr::lng_settings_auto_archive_about());

	using namespace rpl::mappers;
	wrap->toggleOn(rpl::single(
		false
	) | rpl::then(
		session->api().globalPrivacy().showArchiveAndMute(
		) | rpl::filter(_1) | rpl::take(1)
	));
}

void SetupLocalPasscode(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_passcode_title());

	auto has = rpl::single(
		rpl::empty_value()
	) | rpl::then(
		controller->session().domain().local().localPasscodeChanged()
	) | rpl::map([=] {
		return controller->session().domain().local().hasLocalPasscode();
	});
	auto text = rpl::combine(
		tr::lng_passcode_change(),
		tr::lng_passcode_turn_on(),
		base::duplicate(has),
		[](const QString &change, const QString &create, bool has) {
			return has ? change : create;
		});
	container->add(
		object_ptr<Button>(
			container,
			std::move(text),
			st::settingsButton)
	)->addClickHandler([=] {
		controller->show(Box<PasscodeBox>(&controller->session(), false));
	});

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();
	inner->add(
		object_ptr<Button>(
			inner,
			tr::lng_settings_passcode_disable(),
			st::settingsButton)
	)->addClickHandler([=] {
		controller->show(Box<PasscodeBox>(&controller->session(), true));
	});

	const auto autoLockBoxClosing =
		container->lifetime().make_state<rpl::event_stream<>>();
	const auto label = base::Platform::LastUserInputTimeSupported()
		? tr::lng_passcode_autolock_away
		: tr::lng_passcode_autolock_inactive;
	auto value = autoLockBoxClosing->events_starting_with(
		rpl::empty_value()
	) | rpl::map([] {
		const auto autolock = Core::App().settings().autoLock();
		return (autolock % 3600)
			? tr::lng_passcode_autolock_minutes(
				tr::now,
				lt_count,
				autolock / 60)
			: tr::lng_passcode_autolock_hours(
				tr::now,
				lt_count,
				autolock / 3600);
	});

	AddButtonWithLabel(
		inner,
		label(),
		std::move(value),
		st::settingsButton
	)->addClickHandler([=] {
		const auto box = controller->show(
			Box<AutoLockBox>(&controller->session()));
		box->boxClosing(
		) | rpl::start_to_stream(*autoLockBoxClosing, box->lifetime());
	});

	wrap->toggleOn(base::duplicate(has));

	AddSkip(container);
}

void SetupCloudPassword(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	using namespace rpl::mappers;
	using State = Core::CloudPasswordState;

	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_password_title());

	const auto session = &controller->session();
	auto has = rpl::single(
		false
	) | rpl::then(controller->session().api().passwordState(
	) | rpl::map([](const State &state) {
		return state.request
			|| state.unknownAlgorithm
			|| !state.unconfirmedPattern.isEmpty();
	})) | rpl::distinct_until_changed();
	auto pattern = session->api().passwordState(
	) | rpl::map([](const State &state) {
		return state.unconfirmedPattern;
	});
	auto confirmation = rpl::single(
		tr::lng_profile_loading(tr::now)
	) | rpl::then(rpl::duplicate(
		pattern
	) | rpl::filter([](const QString &pattern) {
		return !pattern.isEmpty();
	}) | rpl::map([](const QString &pattern) {
		return tr::lng_cloud_password_waiting_code(tr::now, lt_email, pattern);
	}));
	auto unconfirmed = rpl::duplicate(
		pattern
	) | rpl::map([](const QString &pattern) {
		return !pattern.isEmpty();
	});
	auto noconfirmed = rpl::single(
		true
	) | rpl::then(rpl::duplicate(
		unconfirmed
	));
	const auto label = container->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				base::duplicate(confirmation),
				st::settingsCloudPasswordLabel),
			QMargins(
				st::settingsButton.padding.left(),
				st::settingsButton.padding.top(),
				st::settingsButton.padding.right(),
				(st::settingsButton.height
					- st::settingsCloudPasswordLabel.style.font->height
					+ st::settingsButton.padding.bottom()))));
	label->toggleOn(base::duplicate(noconfirmed))->setDuration(0);

	std::move(
		confirmation
	) | rpl::start_with_next([=] {
		container->resizeToWidth(container->width());
	}, label->lifetime());

	auto text = rpl::combine(
		tr::lng_cloud_password_set(),
		tr::lng_cloud_password_edit(),
		base::duplicate(has)
	) | rpl::map([](const QString &set, const QString &edit, bool has) {
		return has ? edit : set;
	});
	const auto change = container->add(
		object_ptr<Ui::SlideWrap<Button>>(
			container,
			object_ptr<Button>(
				container,
				std::move(text),
				st::settingsButton)));
	change->toggleOn(rpl::duplicate(
		noconfirmed
	) | rpl::map(
		!_1
	))->setDuration(0);
	change->entity()->addClickHandler([=] {
		if (CheckEditCloudPassword(session)) {
			controller->show(EditCloudPasswordBox(session));
		} else {
			controller->show(CloudPasswordAppOutdatedBox());
		}
	});

	const auto confirm = container->add(
		object_ptr<Ui::SlideWrap<Button>>(
			container,
			object_ptr<Button>(
				container,
				tr::lng_cloud_password_confirm(),
				st::settingsButton)));
	confirm->toggleOn(rpl::single(
		false
	) | rpl::then(rpl::duplicate(
		unconfirmed
	)))->setDuration(0);
	confirm->entity()->addClickHandler([=] {
		const auto state = session->api().passwordStateCurrent();
		if (!state) {
			return;
		}
		auto validation = ConfirmRecoveryEmail(
			&controller->session(),
			state->unconfirmedPattern);

		std::move(
			validation.reloadRequests
		) | rpl::start_with_next([=] {
			session->api().reloadPasswordState();
		}, validation.box->lifetime());

		std::move(
			validation.cancelRequests
		) | rpl::start_with_next([=] {
			session->api().clearUnconfirmedPassword();
		}, validation.box->lifetime());

		controller->show(std::move(validation.box));
	});

	const auto remove = [=] {
		if (CheckEditCloudPassword(session)) {
			RemoveCloudPassword(controller);
		} else {
			controller->show(CloudPasswordAppOutdatedBox());
		}
	};
	const auto disable = container->add(
		object_ptr<Ui::SlideWrap<Button>>(
			container,
			object_ptr<Button>(
				container,
				tr::lng_settings_password_disable(),
				st::settingsButton)));
	disable->toggleOn(rpl::combine(
		rpl::duplicate(has),
		rpl::duplicate(noconfirmed),
		_1 && !_2));
	disable->entity()->addClickHandler(remove);

	const auto abort = container->add(
		object_ptr<Ui::SlideWrap<Button>>(
			container,
			object_ptr<Button>(
				container,
				tr::lng_settings_password_abort(),
				st::settingsAttentionButton)));
	abort->toggleOn(rpl::combine(
		rpl::duplicate(has),
		rpl::duplicate(noconfirmed),
		_1 && _2));
	abort->entity()->addClickHandler(remove);

	const auto reloadOnActivation = [=](Qt::ApplicationState state) {
		if (label->toggled() && state == Qt::ApplicationActive) {
			controller->session().api().reloadPasswordState();
		}
	};
	QObject::connect(
		static_cast<QGuiApplication*>(QCoreApplication::instance()),
		&QGuiApplication::applicationStateChanged,
		label,
		reloadOnActivation);

	session->api().reloadPasswordState();

	AddSkip(container);
	AddDivider(container);
}

void SetupSensitiveContent(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger) {
	using namespace rpl::mappers;

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();

	AddSkip(inner);
	AddSubsectionTitle(inner, tr::lng_settings_sensitive_title());

	const auto session = &controller->session();

	std::move(
		updateTrigger
	) | rpl::start_with_next([=] {
		session->api().sensitiveContent().reload();
	}, container->lifetime());
	AddButton(
		inner,
		tr::lng_settings_sensitive_disable_filtering(),
		st::settingsButton
	)->toggleOn(
		session->api().sensitiveContent().enabled()
	)->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return toggled != session->api().sensitiveContent().enabledCurrent();
	}) | rpl::start_with_next([=](bool toggled) {
		session->api().sensitiveContent().update(toggled);
	}, container->lifetime());

	AddSkip(inner);
	AddDividerText(inner, tr::lng_settings_sensitive_about());

	wrap->toggleOn(session->api().sensitiveContent().canChange());
}

void SetupSelfDestruction(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger) {
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_destroy_title());

	const auto session = &controller->session();

	std::move(
		updateTrigger
	) | rpl::start_with_next([=] {
		session->api().selfDestruct().reload();
	}, container->lifetime());
	const auto label = [&] {
		return session->api().selfDestruct().days(
		) | rpl::map(
			SelfDestructionBox::DaysLabel
		);
	};

	AddButtonWithLabel(
		container,
		tr::lng_settings_destroy_if(),
		label(),
		st::settingsButton
	)->addClickHandler([=] {
		controller->show(Box<SelfDestructionBox>(
			session,
			session->api().selfDestruct().days()));
	});

	AddSkip(container);
}

void ClearPaymentInfoBoxBuilder(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	box->setTitle(tr::lng_clear_payment_info_title());

	const auto checkboxPadding = style::margins(
		st::boxRowPadding.left(),
		st::boxRowPadding.left(),
		st::boxRowPadding.right(),
		st::boxRowPadding.bottom());
	const auto label = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_clear_payment_info_sure(),
		st::boxLabel));
	const auto shipping = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::lng_clear_payment_info_shipping(tr::now),
			true,
			st::defaultBoxCheckbox),
		checkboxPadding);
	const auto payment = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::lng_clear_payment_info_payment(tr::now),
			true,
			st::defaultBoxCheckbox),
		checkboxPadding);

	using Flags = MTPpayments_ClearSavedInfo::Flags;
	const auto flags = box->lifetime().make_state<Flags>();

	box->addButton(tr::lng_clear_payment_info_clear(), [=] {
		using Flag = Flags::Enum;
		*flags = (shipping->checked() ? Flag::f_info : Flag(0))
			| (payment->checked() ? Flag::f_credentials : Flag(0));
		delete label;
		delete shipping;
		delete payment;
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_clear_payment_info_confirm(),
			st::boxLabel));
		box->clearButtons();
		box->addButton(tr::lng_clear_payment_info_clear(), [=] {
			session->api().request(MTPpayments_ClearSavedInfo(
				MTP_flags(*flags)
			)).send();
			box->closeBox();
		}, st::attentionBoxButton);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}, st::attentionBoxButton);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

auto ClearPaymentInfoBox(not_null<Main::Session*> session) {
	return Box(ClearPaymentInfoBoxBuilder, session);
}

void SetupBotsAndWebsites(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_security_bots());

	const auto session = &controller->session();
	AddButton(
		container,
		tr::lng_settings_clear_payment_info(),
		st::settingsButton
	)->addClickHandler([=] {
		controller->show(ClearPaymentInfoBox(session));
	});

	AddSkip(container);
}

void SetupSessionsList(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> updateTrigger) {
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_sessions_title());

	std::move(
		updateTrigger
	) | rpl::start_with_next([=] {
		controller->session().api().authorizations().reload();
	}, container->lifetime());

	auto count = controller->session().api().authorizations().totalChanges(
	) | rpl::map([](int count) {
		return count ? QString::number(count) : QString();
	});

	AddButtonWithLabel(
		container,
		tr::lng_settings_show_sessions(),
		std::move(count),
		st::settingsButton
	)->addClickHandler([=] {
		controller->show(Box<SessionsBox>(&controller->session()));
	});
	AddSkip(container, st::settingsPrivacySecurityPadding);
	AddDividerText(container, tr::lng_settings_sessions_about());
}

} // namespace

int ExceptionUsersCount(const std::vector<not_null<PeerData*>> &exceptions) {
	const auto add = [](int already, not_null<PeerData*> peer) {
		if (const auto chat = peer->asChat()) {
			return already + chat->count;
		} else if (const auto channel = peer->asChannel()) {
			return already + channel->membersCount();
		}
		return already + 1;
	};
	return ranges::accumulate(exceptions, 0, add);
}

bool CheckEditCloudPassword(not_null<::Main::Session*> session) {
	const auto current = session->api().passwordStateCurrent();
	Assert(current.has_value());

	if (!current->unknownAlgorithm
		&& !v::is_null(current->newPassword)
		&& !v::is_null(current->newSecureSecret)) {
		return true;
	}
	return false;
}

object_ptr<Ui::BoxContent> EditCloudPasswordBox(not_null<Main::Session*> session) {
	const auto current = session->api().passwordStateCurrent();
	Assert(current.has_value());

	auto result = Box<PasscodeBox>(
		session,
		PasscodeBox::CloudFields::From(*current));
	const auto box = result.data();

	rpl::merge(
		box->newPasswordSet() | rpl::to_empty,
		box->passwordReloadNeeded()
	) | rpl::start_with_next([=] {
		session->api().reloadPasswordState();
	}, box->lifetime());

	box->clearUnconfirmedPassword(
	) | rpl::start_with_next([=] {
		session->api().clearUnconfirmedPassword();
	}, box->lifetime());

	return result;
}

void RemoveCloudPassword(not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	const auto current = session->api().passwordStateCurrent();
	Assert(current.has_value());

	if (!current->request) {
		session->api().clearUnconfirmedPassword();
		return;
	}
	auto fields = PasscodeBox::CloudFields::From(*current);
	fields.turningOff = true;
	auto box = Box<PasscodeBox>(session, fields);

	rpl::merge(
		box->newPasswordSet() | rpl::to_empty,
		box->passwordReloadNeeded()
	) | rpl::start_with_next([=] {
		session->api().reloadPasswordState();
	}, box->lifetime());

	box->clearUnconfirmedPassword(
	) | rpl::start_with_next([=] {
		session->api().clearUnconfirmedPassword();
	}, box->lifetime());

	controller->show(std::move(box));
}

object_ptr<Ui::BoxContent> CloudPasswordAppOutdatedBox() {
	const auto callback = [=](Fn<void()> &&close) {
		Core::UpdateApplication();
		close();
	};
	return Box<ConfirmBox>(
		tr::lng_passport_app_out_of_date(tr::now),
		tr::lng_menu_update(tr::now),
		callback);
}

void AddPrivacyButton(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> label,
		Privacy::Key key,
		Fn<std::unique_ptr<EditPrivacyController>()> controllerFactory) {
	const auto shower = Ui::CreateChild<rpl::lifetime>(container.get());
	const auto session = &controller->session();
	AddButtonWithLabel(
		container,
		std::move(label),
		PrivacyString(session, key),
		st::settingsButton
	)->addClickHandler([=] {
		*shower = session->api().privacyValue(
			key
		) | rpl::take(
			1
		) | rpl::start_with_next([=](const Privacy &value) {
			controller->show(
				Box<EditPrivacyBox>(controller, controllerFactory(), value),
				Ui::LayerOption::KeepOther);
		});
	});
}

PrivacySecurity::PrivacySecurity(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

void PrivacySecurity::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	auto updateOnTick = rpl::single(
	) | rpl::then(base::timer_each(kUpdateTimeout));
	const auto trigger = [=] {
		return rpl::duplicate(updateOnTick);
	};

	SetupPrivacy(controller, content, trigger());
	SetupArchiveAndMute(controller, content);
	SetupSessionsList(controller, content, trigger());
	SetupLocalPasscode(controller, content);
	SetupCloudPassword(controller, content);
#if !defined OS_MAC_STORE && !defined OS_WIN_STORE
	SetupSensitiveContent(controller, content, trigger());
#else // !OS_MAC_STORE && !OS_WIN_STORE
	AddDivider(content);
#endif // !OS_MAC_STORE && !OS_WIN_STORE
	SetupSelfDestruction(controller, content, trigger());
	AddDivider(content);
	SetupBotsAndWebsites(controller, content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
