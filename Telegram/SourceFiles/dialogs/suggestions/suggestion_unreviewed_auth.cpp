/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/suggestions/suggestion.h"

#include "api/api_authorizations.h"
#include "apiwrap.h"
#include "boxes/unconfirmed_auth_denied_box.h"
#include "core/click_handler_types.h"
#include "data/data_authorization.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_active_sessions.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Dialogs::TopBarSuggestions {
namespace {

[[nodiscard]] QString FormatAuthInfo(const Data::UnreviewedAuth &auth) {
	const auto location = auth.location.isEmpty()
		? QString()
		: "\U0001F30D " + auth.location;
	const auto device = auth.device.isEmpty()
		? QString()
		: "\U0001F4F1 " + auth.device;

	if (!location.isEmpty() && !device.isEmpty()) {
		return location + " (" + device + ")";
	} else if (!location.isEmpty()) {
		return location;
	} else if (!device.isEmpty()) {
		return device;
	}
	return QString();
}

void ShowAuthToast(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		Fn<not_null<Window::SessionController*>()> findController,
		const std::vector<Data::UnreviewedAuth> &list,
		bool confirmed) {
	if (confirmed) {
		auto text = tr::lng_unconfirmed_auth_confirmed_message(
			tr::now,
			lt_link,
			tr::link(tr::lng_settings_sessions_title(tr::now)),
			tr::rich);
		auto filter = [=](
				ClickHandlerPtr handler,
				Qt::MouseButton button) {
			if (const auto controller = findController()) {
				session->api().authorizations().reload();
				controller->showSettings(Settings::SessionsId());
				return false;
			}
			return true;
		};
		Ui::Toast::Show(parent->window(), Ui::Toast::Config{
			.title = tr::lng_unconfirmed_auth_confirmed(tr::now),
			.text = std::move(text),
			.filter = std::move(filter),
			.duration = crl::time(5000),
		});
	} else {
		auto messageText = QString();
		if (list.size() == 1) {
			messageText = tr::lng_unconfirmed_auth_denied_single(
				tr::now,
				lt_country,
				FormatAuthInfo(list.front()));
		} else {
			auto authList = QString('\n');
			for (auto i = 0; i < std::min(int(list.size()), 10); ++i) {
				const auto info = FormatAuthInfo(list[i]);
				if (!info.isEmpty()) {
					authList += "• " + info + "\n";
				}
			}
			messageText = tr::lng_unconfirmed_auth_denied_multiple(
				tr::now,
				lt_country,
				authList);
		}
		const auto controller = findController();
		const auto count = float64(list.size());
		controller->show(Box(ShowAuthDeniedBox, count, messageText));
	}
}

bool Available(const Context &context) {
	return !context.session->api().authorizations().unreviewed().empty();
}

void Activate(ActivateArgs args) {
	const auto session = args.context.session.get();
	const auto parent = args.context.parent;
	const auto findController = args.context.findController;
	const auto authorizations = &session->api().authorizations();
	auto listProducer = rpl::single(
		authorizations->unreviewed()
	) | rpl::then(
		authorizations->unreviewedChanges() | rpl::map([=] {
			return authorizations->unreviewed();
		})
	);

	const auto wrap = CreateUnconfirmedAuthContent(
		parent,
		std::move(listProducer),
		[=](bool confirmed) {
			const auto current = authorizations->unreviewed();
			auto hashes = ranges::views::all(
				current
			) | ranges::views::transform([](const auto &auth) {
				return auth.hash;
			}) | ranges::to_vector;
			ShowAuthToast(parent, session, findController, current, confirmed);
			authorizations->review(hashes, confirmed);
		},
		args.context.childListShown());
	args.done(wrap, [wrap] { wrap->prepareCollapseSnapshot(); });
}

} // namespace

Spec MakeUnreviewedAuthSpec() {
	return {
		.priority = Priority::UnreviewedAuth,
		.available = Available,
		.activate = Activate,
	};
}

} // namespace Dialogs::TopBarSuggestions
