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
#include "data/business/data_business_chatbots.h"
#include "data/data_authorization.h"
#include "data/data_session.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/business/settings_chatbots.h"
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

bool OpenAuthSettings(
		not_null<Main::Session*> session,
		Fn<not_null<Window::SessionController*>()> findController,
		ClickHandlerPtr handler) {
	if (const auto controller = findController()) {
		const auto url = dynamic_cast<UrlClickHandler*>(handler.get());
		const auto chatbots = url
			&& url->originalUrl() == u"internal:chatbots"_q;
		if (chatbots) {
			session->data().chatbots().preload();
		} else {
			session->api().authorizations().reload();
		}
		controller->showSettings(chatbots
			? Settings::ChatbotsId()
			: Settings::SessionsId());
		return false;
	}
	return true;
}

void ShowAuthToast(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		Fn<not_null<Window::SessionController*>()> findController,
		const std::vector<Data::UnreviewedAuth> &list,
		bool confirmed,
		bool failed) {
	const auto bot = ranges::all_of(list, [](const auto &auth) {
		return auth.botId != 0;
	});
	const auto mixed = !bot && ranges::any_of(list, [](const auto &auth) {
		return auth.botId != 0;
	});
	if (confirmed) {
		const auto message = bot
			? tr::lng_unconfirmed_auth_confirmed_bot_message
			: tr::lng_unconfirmed_auth_confirmed_message;
		auto text = message(
			tr::now,
			lt_link,
			tr::link(
				bot
					? tr::lng_chat_automation_title(tr::now)
					: tr::lng_settings_sessions_title(tr::now),
				bot ? u"internal:chatbots"_q : u"internal:sessions"_q),
			tr::rich);
		if (mixed) {
			text.append(u"\n"_q).append(
				tr::lng_unconfirmed_auth_confirmed_bot_message(
					tr::now,
					lt_link,
					tr::link(
						tr::lng_chat_automation_title(tr::now),
						u"internal:chatbots"_q),
					tr::rich));
		}
		if (failed) {
			text.append(u"\n\n"_q).append(
				tr::lng_unconfirmed_auth_review_failed(tr::now));
		}
		auto filter = [=](
				ClickHandlerPtr handler,
				Qt::MouseButton button) {
			return OpenAuthSettings(session, findController, handler);
		};
		Ui::Toast::Show(parent->window(), Ui::Toast::Config{
			.title = ((bot || mixed) && list.size() > 1)
				? tr::lng_unconfirmed_auth_connections_allowed(tr::now)
				: bot
				? tr::lng_unconfirmed_auth_confirmed_bot(tr::now)
				: tr::lng_unconfirmed_auth_confirmed(tr::now),
			.text = std::move(text),
			.filter = std::move(filter),
			.duration = crl::time(5000),
		});
	} else {
		auto messageText = QString();
		if (list.size() == 1) {
			const auto message = bot
				? tr::lng_unconfirmed_auth_denied_single_bot
				: tr::lng_unconfirmed_auth_denied_single;
			messageText = message(
				tr::now,
				lt_country,
				FormatAuthInfo(list.front()));
		} else {
			for (const auto &auth : list) {
				if (!messageText.isEmpty()) {
					messageText += u"\n"_q;
				}
				const auto message = auth.botId
					? tr::lng_unconfirmed_auth_denied_single_bot
					: tr::lng_unconfirmed_auth_denied_single;
				messageText += message(
					tr::now,
					lt_country,
					FormatAuthInfo(auth));
			}
			if (mixed) {
				messageText += u"\n\n"_q
					+ tr::lng_unconfirmed_auth_denied_warning_bot(tr::now);
			}
		}
		if (failed) {
			messageText += u"\n\n"_q
				+ tr::lng_unconfirmed_auth_review_failed(tr::now);
		}
		const auto controller = findController();
		const auto count = float64(list.size());
		controller->show(Box(
			ShowAuthDeniedBox,
			count,
			messageText,
			bot,
			mixed));
	}
}

void ShowReviewResult(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		Fn<not_null<Window::SessionController*>()> findController,
		const std::vector<Data::UnreviewedAuth> &current,
		const std::vector<Data::UnreviewedAuth> &success,
		bool confirmed) {
	const auto &pending = session->api().authorizations().unreviewed();
	const auto stillPending = [&](const Data::UnreviewedAuth &auth) {
		return ranges::any_of(pending, [&](const auto &now) {
			return now.hash == auth.hash
				&& now.botId == auth.botId
				&& (!auth.botId || now.date <= auth.date);
		});
	};
	const auto failed = ranges::any_of(current, stillPending);
	if (!success.empty()) {
		ShowAuthToast(
			parent,
			session,
			findController,
			success,
			confirmed,
			failed);
	}
	if (success.empty() && failed) {
		Ui::Toast::Show(parent->window(), Ui::Toast::Config{
			.text = tr::lng_unconfirmed_auth_review_failed(
				tr::now,
				tr::marked),
		});
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
		[=](bool confirmed, Fn<void()> finish) {
			const auto current = authorizations->unreviewed();
			const auto done = crl::guard(parent, [=](
					std::vector<Data::UnreviewedAuth> success) {
				finish();
				ShowReviewResult(
					parent,
					session,
					findController,
					current,
					success,
					confirmed);
			});
			authorizations->review(current, confirmed, done);
		},
		args.context.childListShown(),
		authorizations->reviewingValue());
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
