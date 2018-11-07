/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_privacy_security.h"

#include "settings/settings_common.h"
#include "settings/settings_privacy_controllers.h"
#include "boxes/peer_list_box.h"
#include "boxes/edit_privacy_box.h"
#include "boxes/passcode_box.h"
#include "boxes/autolock_box.h"
#include "boxes/sessions_box.h"
#include "boxes/confirm_box.h"
#include "boxes/self_destruction_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/labels.h"
#include "calls/calls_instance.h"
#include "core/core_cloud_password.h"
#include "core/update_checker.h"
#include "info/profile/info_profile_button.h"
#include "platform/platform_specific.h"
#include "lang/lang_keys.h"
#include "data/data_session.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

rpl::producer<> PasscodeChanges() {
	return rpl::single(
		rpl::empty_value()
	) | rpl::then(base::ObservableViewer(
		Global::RefLocalPasscodeChanged()
	));
}

QString PrivacyBase(ApiWrap::Privacy::Option option) {
	const auto key = [&] {
		using Option = ApiWrap::Privacy::Option;
		switch (option) {
		case Option::Everyone: return lng_edit_privacy_everyone;
		case Option::Contacts: return lng_edit_privacy_contacts;
		case Option::Nobody: return lng_edit_privacy_nobody;
		}
		Unexpected("Value in Privacy::Option.");
	}();
	return lang(key);
}

void SetupPrivacy(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container, st::settingsPrivacySkip);
	AddSubsectionTitle(container, lng_settings_privacy_title);

	AddButton(
		container,
		lng_settings_blocked_users,
		st::settingsButton
	)->addClickHandler([] {
		const auto initBox = [](not_null<PeerListBox*> box) {
			box->addButton(langFactory(lng_close), [=] {
				box->closeBox();
			});
			box->addLeftButton(langFactory(lng_blocked_list_add), [] {
				BlockedBoxController::BlockNewUser();
			});
		};
		Ui::show(Box<PeerListBox>(
			std::make_unique<BlockedBoxController>(),
			initBox));
	});

	using Privacy = ApiWrap::Privacy;
	const auto PrivacyString = [](Privacy::Key key) {
		Auth().api().reloadPrivacy(key);
		return Auth().api().privacyValue(
			key
		) | rpl::map([](const Privacy &value) {
			auto add = QStringList();
			if (const auto never = value.never.size()) {
				add.push_back("-" + QString::number(never));
			}
			if (const auto always = value.always.size()) {
				add.push_back("+" + QString::number(always));
			}
			if (!add.isEmpty()) {
				return PrivacyBase(value.option) + " (" + add.join(", ") + ")";
			} else {
				return PrivacyBase(value.option);
			}
		});
	};
	const auto add = [&](LangKey label, Privacy::Key key, auto controller) {
		const auto shower = Ui::AttachAsChild(container, rpl::lifetime());
		AddButtonWithLabel(
			container,
			label,
			PrivacyString(key),
			st::settingsButton
		)->addClickHandler([=] {
			*shower = Auth().api().privacyValue(
				key
			) | rpl::take(
				1
			) | rpl::start_with_next([=](const Privacy &value) {
				Ui::show(Box<EditPrivacyBox>(
					controller(),
					value));
			});
		});
	};
	add(
		lng_settings_last_seen,
		Privacy::Key::LastSeen,
		[] { return std::make_unique<LastSeenPrivacyController>(); });
	add(
		lng_settings_calls,
		Privacy::Key::Calls,
		[] { return std::make_unique<CallsPrivacyController>(); });
	add(
		lng_settings_groups_invite,
		Privacy::Key::Invites,
		[] { return std::make_unique<GroupsInvitePrivacyController>(); });

	AddSkip(container, st::settingsPrivacySecurityPadding);
	AddDividerText(
		container,
		Lang::Viewer(lng_settings_group_privacy_about));
}

not_null<Ui::SlideWrap<Ui::PlainShadow>*> AddSeparator(
		not_null<Ui::VerticalLayout*> container) {
	return container->add(
		object_ptr<Ui::SlideWrap<Ui::PlainShadow>>(
			container,
			object_ptr<Ui::PlainShadow>(container),
			st::settingsSeparatorPadding));
}

void SetupLocalPasscode(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container);
	AddSubsectionTitle(container, lng_settings_passcode_title);

	auto has = PasscodeChanges(
	) | rpl::map([] {
		return Global::LocalPasscode();
	});
	auto text = rpl::combine(
		Lang::Viewer(lng_passcode_change),
		Lang::Viewer(lng_passcode_turn_on),
		base::duplicate(has),
		[](const QString &change, const QString &create, bool has) {
			return has ? change : create;
		});
	container->add(
		object_ptr<Button>(
			container,
			std::move(text),
			st::settingsButton)
	)->addClickHandler([] {
		Ui::show(Box<PasscodeBox>(false));
	});

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();
	inner->add(
		object_ptr<Button>(
			inner,
			Lang::Viewer(lng_settings_passcode_disable),
			st::settingsButton)
	)->addClickHandler([] {
		Ui::show(Box<PasscodeBox>(true));
	});

	const auto label = psIdleSupported()
		? lng_passcode_autolock_away
		: lng_passcode_autolock_inactive;
	auto value = PasscodeChanges(
	) | rpl::map([] {
		const auto autolock = Global::AutoLock();
		return (autolock % 3600)
			? lng_passcode_autolock_minutes(lt_count, autolock / 60)
			: lng_passcode_autolock_hours(lt_count, autolock / 3600);
	});

	AddButtonWithLabel(
		inner,
		label,
		std::move(value),
		st::settingsButton
	)->addClickHandler([] {
		Ui::show(Box<AutoLockBox>());
	});

	wrap->toggleOn(base::duplicate(has));

	AddSkip(container);
}

bool CheckEditCloudPassword() {
	const auto current = Auth().api().passwordStateCurrent();
	Assert(current.has_value());
	if (!current->unknownAlgorithm
		&& current->newPassword
		&& current->newSecureSecret) {
		return true;
	}
	auto box = std::make_shared<QPointer<BoxContent>>();
	const auto callback = [=] {
		Core::UpdateApplication();
		if (*box) (*box)->closeBox();
	};
	*box = Ui::show(Box<ConfirmBox>(
		lang(lng_passport_app_out_of_date),
		lang(lng_menu_update),
		callback));
	return false;
}

void EditCloudPassword() {
	const auto current = Auth().api().passwordStateCurrent();
	Assert(current.has_value());

	const auto box = Ui::show(Box<PasscodeBox>(
		current->request,
		current->newPassword,
		current->hasRecovery,
		current->notEmptyPassport,
		current->hint,
		current->newSecureSecret));
	rpl::merge(
		box->newPasswordSet() | rpl::map([] { return rpl::empty_value(); }),
		box->passwordReloadNeeded()
	) | rpl::start_with_next([=] {
		Auth().api().reloadPasswordState();
	}, box->lifetime());
}

void RemoveCloudPassword() {
	const auto current = Auth().api().passwordStateCurrent();
	Assert(current.has_value());

	if (!current->request) {
		Auth().api().clearUnconfirmedPassword();
		return;
	}
	const auto box = Ui::show(Box<PasscodeBox>(
		current->request,
		current->newPassword,
		current->hasRecovery,
		current->notEmptyPassport,
		current->hint,
		current->newSecureSecret,
		true));
	rpl::merge(
		box->newPasswordSet(
		) | rpl::map([] { return rpl::empty_value(); }),
		box->passwordReloadNeeded()
	) | rpl::start_with_next([=] {
		Auth().api().reloadPasswordState();
	}, box->lifetime());
}

void SetupCloudPassword(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, lng_settings_password_title);

	using State = Core::CloudPasswordState;

	auto has = rpl::single(
		false
	) | rpl::then(Auth().api().passwordState(
	) | rpl::map([](const State &state) {
		return state.request
			|| state.unknownAlgorithm
			|| !state.unconfirmedPattern.isEmpty();
	})) | rpl::distinct_until_changed();
	auto pattern = Auth().api().passwordState(
	) | rpl::map([](const State &state) {
		return state.unconfirmedPattern;
	});
	auto confirmation = rpl::single(
		lang(lng_profile_loading)
	) | rpl::then(base::duplicate(
		pattern
	) | rpl::filter([](const QString &pattern) {
		return !pattern.isEmpty();
	}) | rpl::map([](const QString &pattern) {
		return lng_cloud_password_waiting(lt_email, pattern);
	}));
	auto unconfirmed = rpl::single(
		true
	) | rpl::then(base::duplicate(
		pattern
	) | rpl::map([](const QString &pattern) {
		return !pattern.isEmpty();
	}));
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
	label->toggleOn(base::duplicate(unconfirmed))->setDuration(0);

	std::move(
		confirmation
	) | rpl::start_with_next([=] {
		container->resizeToWidth(container->width());
	}, label->lifetime());

	auto text = rpl::combine(
		Lang::Viewer(lng_cloud_password_set),
		Lang::Viewer(lng_cloud_password_edit),
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
	change->toggleOn(std::move(
		unconfirmed
	) | rpl::map([](bool unconfirmed) {
		return !unconfirmed;
	}))->setDuration(0);
	change->entity()->addClickHandler([] {
		if (CheckEditCloudPassword()) {
			EditCloudPassword();
		}
	});

	const auto disable = container->add(
		object_ptr<Ui::SlideWrap<Button>>(
			container,
			object_ptr<Button>(
				container,
				Lang::Viewer(lng_settings_password_disable),
				st::settingsButton)));
	disable->toggleOn(base::duplicate(has));
	disable->entity()->addClickHandler([] {
		if (CheckEditCloudPassword()) {
			RemoveCloudPassword();
		}
	});

	const auto reloadOnActivation = [=](Qt::ApplicationState state) {
		if (label->toggled() && state == Qt::ApplicationActive) {
			Auth().api().reloadPasswordState();
		}
	};
	QObject::connect(
		qApp,
		&QApplication::applicationStateChanged,
		label,
		reloadOnActivation);

	Auth().api().reloadPasswordState();

	AddSkip(container);
}

void SetupSelfDestruction(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, lng_settings_destroy_title);

	Auth().api().reloadSelfDestruct();
	const auto label = [] {
		return Auth().api().selfDestructValue(
		) | rpl::map(
			SelfDestructionBox::DaysLabel
		);
	};

	AddButtonWithLabel(
		container,
		lng_settings_destroy_if,
		label(),
		st::settingsButton
	)->addClickHandler([] {
		Ui::show(Box<SelfDestructionBox>(Auth().api().selfDestructValue()));
	});

	AddSkip(container);
}

void SetupSessionsList(not_null<Ui::VerticalLayout*> container) {
	AddSkip(container);
	AddSubsectionTitle(container, lng_settings_sessions_title);

	AddButton(
		container,
		lng_settings_show_sessions,
		st::settingsButton
	)->addClickHandler([] {
		Ui::show(Box<SessionsBox>());
	});
	AddSkip(container, st::settingsPrivacySecurityPadding);
	AddDividerText(
		container,
		Lang::Viewer(lng_settings_sessions_about));
}

} // namespace

PrivacySecurity::PrivacySecurity(QWidget *parent, not_null<UserData*> self)
: Section(parent)
, _self(self) {
	setupContent();
}

void PrivacySecurity::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupPrivacy(content);
	SetupSessionsList(content);
	SetupLocalPasscode(content);
	SetupCloudPassword(content);
	SetupSelfDestruction(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
