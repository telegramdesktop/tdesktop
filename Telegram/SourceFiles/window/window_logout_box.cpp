#include "window/window_logout_box.h"

#include "core/application.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtproto_dc_options.h"
#include "settings/detailed_settings_button.h"
#include "settings/sections/settings_local_passcode.h"
#include "settings/sections/settings_local_storage.h"
#include "settings/sections/settings_main.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Window {

namespace {

void ShowConfirmation(
		not_null<Controller*> window,
		Main::Account *account,
		base::weak_qptr<Ui::GenericBox> options) {
	const auto session = account && account->sessionExists()
		? &account->session()
		: nullptr;
	const auto callback = [
		options,
		weakAccount = base::make_weak(account),
		weakSession = base::make_weak(session),
		locked = !account,
		hadSession = (session != nullptr)
	](Fn<void()> close) {
		const auto account = weakAccount.get();
		const auto allowed = locked || (account && (!hadSession || weakSession));
		if (options) {
			options->closeBox();
		}
		if (close) {
			close();
		}
		if (allowed) {
			Core::App().logoutWithChecks(account);
		}
	};
	window->show(Ui::MakeConfirmBox({
		.text = LogoutConfirmationText(account),
		.confirmed = callback,
		.confirmText = tr::lng_settings_logout(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

} // namespace

void ShowLogoutConfirmation(
		not_null<Controller*> window,
		Main::Account *account) {
	ShowConfirmation(window, account, {});
}

void ShowLogoutOptions(
		not_null<Controller*> window,
		not_null<Main::Session*> session) {
	window->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_settings_logout());
		box->setWidth(st::boxWideWidth);

		const auto content = box->verticalLayout();
		Ui::AddSubsectionTitle(content, tr::lng_logout_alternatives());

		const auto addRow = [&](
				rpl::producer<QString> title,
				rpl::producer<QString> description,
				const style::icon &icon,
				Fn<void()> action) {
			const auto row = content->add(object_ptr<DetailedSettingsButton>(
				content,
				std::move(title),
				std::move(description),
				Settings::IconDescriptor{ &icon },
				std::nullopt,
				st::logoutOptionsButtonStyle));
			row->setClickedCallback(crl::guard(session, [
				box,
				action = std::move(action)
			] {
				const auto run = action;
				box->closeBox();
				run();
			}));
		};
		const auto &domain = session->domain();
		if (domain.accounts().size() < domain.maxAccounts()) {
			addRow(
				tr::lng_menu_add_account(),
				tr::lng_logout_add_account_about(),
				st::logoutIconAddAccount,
				[=] {
					auto &domain = session->domain();
					if (domain.accounts().size() < domain.maxAccounts()) {
						Core::App().setActivePrimaryWindow(window);
						domain.addActivated(MTP::Environment{});
					}
				});
		}
		const auto openSettings = [=](Settings::Type section) {
			window->invokeForSessionController(
				&session->account(),
				nullptr,
				[=](not_null<SessionController*> controller) {
					controller->showSettings(section);
				});
		};
		if (!domain.local().hasLocalPasscode()) {
			addRow(
				tr::lng_logout_set_passcode(),
				tr::lng_logout_set_passcode_about(),
				st::logoutIconSetPasscode,
				[=] { openSettings(Settings::LocalPasscodeCreateId()); });
		}
		addRow(
			tr::lng_logout_clear_cache(),
			tr::lng_logout_clear_cache_about(),
			st::logoutIconClearCache,
			[=] { openSettings(Settings::LocalStorageId()); });
		addRow(
			tr::lng_logout_contact_support(),
			tr::lng_logout_contact_support_about(),
			st::logoutIconContactSupport,
			[=] {
				window->invokeForSessionController(
					&session->account(),
					nullptr,
					Settings::OpenAskQuestionConfirm);
			});

		box->addButton(
			tr::lng_settings_logout(),
			crl::guard(session, [=] {
				ShowConfirmation(window, &session->account(), box.get());
			}),
			st::attentionBoxButton);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}), Ui::LayerOption::CloseOther);
}

} // namespace Window
