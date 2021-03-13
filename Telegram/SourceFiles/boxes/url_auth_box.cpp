/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/url_auth_box.h"

#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "core/click_handler_types.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "app.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

void UrlAuthBox::Activate(
		not_null<const HistoryItem*> message,
		int row,
		int column) {
	const auto itemId = message->fullId();
	const auto button = HistoryMessageMarkupButton::Get(
		&message->history()->owner(),
		itemId,
		row,
		column);
	if (button->requestId || !IsServerMsgId(itemId.msg)) {
		return;
	}
	const auto session = &message->history()->session();
	const auto inputPeer = message->history()->peer->input;
	const auto buttonId = button->buttonId;
	const auto url = QString::fromUtf8(button->data);

	using Flag = MTPmessages_RequestUrlAuth::Flag;
	button->requestId = session->api().request(MTPmessages_RequestUrlAuth(
		MTP_flags(Flag::f_peer | Flag::f_msg_id | Flag::f_button_id),
		inputPeer,
		MTP_int(itemId.msg),
		MTP_int(buttonId),
		MTPstring() // #TODO auth url
	)).done([=](const MTPUrlAuthResult &result) {
		const auto button = HistoryMessageMarkupButton::Get(
			&session->data(),
			itemId,
			row,
			column);
		if (!button) {
			return;
		}

		button->requestId = 0;
		result.match([&](const MTPDurlAuthResultAccepted &data) {
			UrlClickHandler::Open(qs(data.vurl()));
		}, [&](const MTPDurlAuthResultDefault &data) {
			HiddenUrlClickHandler::Open(url);
		}, [&](const MTPDurlAuthResultRequest &data) {
			if (const auto item = session->data().message(itemId)) {
				Request(data, item, row, column);
			}
		});
	}).fail([=](const MTP::Error &error) {
		const auto button = HistoryMessageMarkupButton::Get(
			&session->data(),
			itemId,
			row,
			column);
		if (!button) return;

		button->requestId = 0;
		HiddenUrlClickHandler::Open(url);
	}).send();
}

void UrlAuthBox::Activate(
		not_null<Main::Session*> session,
		const QString &url,
		QVariant context) {
	context = QVariant::fromValue([&] {
		auto result = context.value<ClickHandlerContext>();
		result.skipBotAutoLogin = true;
		return result;
	}());

	using Flag = MTPmessages_RequestUrlAuth::Flag;
	session->api().request(MTPmessages_RequestUrlAuth(
		MTP_flags(Flag::f_url),
		MTPInputPeer(),
		MTPint(), // msg_id
		MTPint(), // button_id
		MTP_string(url)
	)).done([=](const MTPUrlAuthResult &result) {
		result.match([&](const MTPDurlAuthResultAccepted &data) {
			UrlClickHandler::Open(qs(data.vurl()), context);
		}, [&](const MTPDurlAuthResultDefault &data) {
			HiddenUrlClickHandler::Open(url, context);
		}, [&](const MTPDurlAuthResultRequest &data) {
			Request(data, session, url, context);
		});
	}).fail([=](const MTP::Error &error) {
		HiddenUrlClickHandler::Open(url, context);
	}).send();
}

void UrlAuthBox::Request(
		const MTPDurlAuthResultRequest &request,
		not_null<const HistoryItem*> message,
		int row,
		int column) {
	const auto itemId = message->fullId();
	const auto button = HistoryMessageMarkupButton::Get(
		&message->history()->owner(),
		itemId,
		row,
		column);
	if (button->requestId || !IsServerMsgId(itemId.msg)) {
		return;
	}
	const auto session = &message->history()->session();
	const auto inputPeer = message->history()->peer->input;
	const auto buttonId = button->buttonId;
	const auto url = QString::fromUtf8(button->data);

	const auto bot = request.is_request_write_access()
		? session->data().processUser(request.vbot()).get()
		: nullptr;
	const auto box = std::make_shared<QPointer<Ui::BoxContent>>();
	const auto finishWithUrl = [=](const QString &url) {
		if (*box) {
			(*box)->closeBox();
		}
		UrlClickHandler::Open(url);
	};
	const auto callback = [=](Result result) {
		if (result == Result::None) {
			finishWithUrl(url);
		} else if (const auto msg = session->data().message(itemId)) {
			const auto allowWrite = (result == Result::AuthAndAllowWrite);
			using Flag = MTPmessages_AcceptUrlAuth::Flag;
			const auto flags = (allowWrite ? Flag::f_write_allowed : Flag(0))
				| (Flag::f_peer | Flag::f_msg_id | Flag::f_button_id);
			session->api().request(MTPmessages_AcceptUrlAuth(
				MTP_flags(flags),
				inputPeer,
				MTP_int(itemId.msg),
				MTP_int(buttonId),
				MTPstring() // #TODO auth url
			)).done([=](const MTPUrlAuthResult &result) {
				const auto to = result.match(
				[&](const MTPDurlAuthResultAccepted &data) {
					return qs(data.vurl());
				}, [&](const MTPDurlAuthResultDefault &data) {
					return url;
				}, [&](const MTPDurlAuthResultRequest &data) {
					LOG(("API Error: "
						"got urlAuthResultRequest after acceptUrlAuth."));
					return url;
				});
				finishWithUrl(to);
			}).fail([=](const MTP::Error &error) {
				finishWithUrl(url);
			}).send();
		}
	};
	*box = Ui::show(
		Box<UrlAuthBox>(session, url, qs(request.vdomain()), bot, callback),
		Ui::LayerOption::KeepOther);
}

void UrlAuthBox::Request(
		const MTPDurlAuthResultRequest &request,
		not_null<Main::Session*> session,
		const QString &url,
		QVariant context) {
	const auto bot = request.is_request_write_access()
		? session->data().processUser(request.vbot()).get()
		: nullptr;
	const auto box = std::make_shared<QPointer<Ui::BoxContent>>();
	const auto finishWithUrl = [=](const QString &url) {
		if (*box) {
			(*box)->closeBox();
		}
		UrlClickHandler::Open(url, context);
	};
	const auto callback = [=](Result result) {
		if (result == Result::None) {
			finishWithUrl(url);
		} else {
			const auto allowWrite = (result == Result::AuthAndAllowWrite);
			using Flag = MTPmessages_AcceptUrlAuth::Flag;
			const auto flags = (allowWrite ? Flag::f_write_allowed : Flag(0))
				| Flag::f_url;
			session->api().request(MTPmessages_AcceptUrlAuth(
				MTP_flags(flags),
				MTPInputPeer(),
				MTPint(), // msg_id
				MTPint(), // button_id
				MTP_string(url)
			)).done([=](const MTPUrlAuthResult &result) {
				const auto to = result.match(
				[&](const MTPDurlAuthResultAccepted &data) {
					return qs(data.vurl());
				}, [&](const MTPDurlAuthResultDefault &data) {
					return url;
				}, [&](const MTPDurlAuthResultRequest &data) {
					LOG(("API Error: "
						"got urlAuthResultRequest after acceptUrlAuth."));
					return url;
				});
				finishWithUrl(to);
			}).fail([=](const MTP::Error &error) {
				finishWithUrl(url);
			}).send();
		}
	};
	*box = Ui::show(
		Box<UrlAuthBox>(session, url, qs(request.vdomain()), bot, callback),
		Ui::LayerOption::KeepOther);
}

UrlAuthBox::UrlAuthBox(
	QWidget*,
	not_null<Main::Session*> session,
	const QString &url,
	const QString &domain,
	UserData *bot,
	Fn<void(Result)> callback)
: _content(setupContent(session, url, domain, bot, std::move(callback))) {
}

void UrlAuthBox::prepare() {
	setDimensionsToContent(st::boxWidth, _content);
	addButton(tr::lng_open_link(), [=] { _callback(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });
}

not_null<Ui::RpWidget*> UrlAuthBox::setupContent(
		not_null<Main::Session*> session,
		const QString &url,
		const QString &domain,
		UserData *bot,
		Fn<void(Result)> callback) {
	const auto result = Ui::CreateChild<Ui::VerticalLayout>(this);
	result->add(
		object_ptr<Ui::FlatLabel>(
			result,
			tr::lng_url_auth_open_confirm(tr::now, lt_link, url),
			st::boxLabel),
		st::boxPadding);
	const auto addCheckbox = [&](const QString &text) {
		const auto checkbox = result->add(
			object_ptr<Ui::Checkbox>(
				result,
				QString(),
				true,
				st::urlAuthCheckbox),
			style::margins(
				st::boxPadding.left(),
				st::boxPadding.bottom(),
				st::boxPadding.right(),
				st::boxPadding.bottom()));
		checkbox->setAllowTextLines();
		checkbox->setText(text, true);
		return checkbox;
	};
	const auto auth = addCheckbox(
		tr::lng_url_auth_login_option(
			tr::now,
			lt_domain,
			textcmdStartSemibold() + domain + textcmdStopSemibold(),
			lt_user,
			(textcmdStartSemibold()
				+ session->user()->name
				+ textcmdStopSemibold())));
	const auto allow = bot
		? addCheckbox(tr::lng_url_auth_allow_messages(
			tr::now,
			lt_bot,
			textcmdStartSemibold() + bot->firstName + textcmdStopSemibold()))
		: nullptr;
	if (allow) {
		rpl::single(
			auth->checked()
		) | rpl::then(
			auth->checkedChanges()
		) | rpl::start_with_next([=](bool checked) {
			if (!checked) {
				allow->setChecked(false);
			}
			allow->setDisabled(!checked);
		}, auth->lifetime());
	}
	_callback = [=, callback = std::move(callback)]() {
		const auto authed = auth->checked();
		const auto allowed = (authed && allow && allow->checked());
		const auto onstack = callback;
		onstack(allowed
			? Result::AuthAndAllowWrite
			: authed
			? Result::Auth
			: Result::None);
	};
	return result;
}
