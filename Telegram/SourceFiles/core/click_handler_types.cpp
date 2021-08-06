/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/click_handler_types.h"

#include "lang/lang_keys.h"
#include "core/application.h"
#include "core/local_url_handlers.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "boxes/confirm_box.h"
#include "base/qthelp_regex.h"
#include "storage/storage_account.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "window/window_session_controller.h"
#include "facades.h"
#include "app.h"

#include <QtGui/QGuiApplication>

namespace {

void SearchByHashtag(ClickContext context, const QString &tag) {
	const auto my = context.other.value<ClickHandlerContext>();
	const auto controller = my.sessionWindow.get();
	if (!controller) {
		return;
	}
	if (controller->openedFolder().current()) {
		controller->closeFolder();
	}

	controller->widget()->ui_hideSettingsAndLayer(anim::type::normal);
	Core::App().hideMediaView();

	auto &data = controller->session().data();
	const auto inPeer = my.peer
		? my.peer
		: my.itemId
		? data.message(my.itemId)->history()->peer.get()
		: nullptr;
	controller->content()->searchMessages(
		tag + ' ',
		(inPeer && !inPeer->isUser())
			? data.history(inPeer).get()
			: Dialogs::Key());
}

} // namespace

bool UrlRequiresConfirmation(const QUrl &url) {
	using namespace qthelp;

	return !regex_match(
		"(^|\\.)(telegram\\.(org|me|dog)|t\\.me|telegra\\.ph|telesco\\.pe)$",
		url.host(),
		RegExOption::CaseInsensitive);
}

void HiddenUrlClickHandler::Open(QString url, QVariant context) {
	url = Core::TryConvertUrlToLocal(url);
	if (Core::InternalPassportLink(url)) {
		return;
	}

	const auto open = [=] {
		UrlClickHandler::Open(url, context);
	};
	if (url.startsWith(qstr("tg://"), Qt::CaseInsensitive)
		|| url.startsWith(qstr("internal:"), Qt::CaseInsensitive)) {
		open();
	} else {
		const auto parsedUrl = QUrl::fromUserInput(url);
		if (UrlRequiresConfirmation(parsedUrl)
			&& QGuiApplication::keyboardModifiers() != Qt::ControlModifier) {
			Core::App().hideMediaView();
			const auto displayed = parsedUrl.isValid()
				? parsedUrl.toDisplayString()
				: url;
			const auto displayUrl = !IsSuspicious(displayed)
				? displayed
				: parsedUrl.isValid()
				? QString::fromUtf8(parsedUrl.toEncoded())
				: ShowEncoded(displayed);
			Ui::show(
				Box<ConfirmBox>(
					(tr::lng_open_this_link(tr::now)
						+ qsl("\n\n")
						+ displayUrl),
					tr::lng_open_link(tr::now),
					[=] { Ui::hideLayer(); open(); }),
				Ui::LayerOption::KeepOther);
		} else {
			open();
		}
	}
}

void BotGameUrlClickHandler::onClick(ClickContext context) const {
	const auto url = Core::TryConvertUrlToLocal(this->url());
	if (Core::InternalPassportLink(url)) {
		return;
	}

	const auto open = [=] {
		UrlClickHandler::Open(url, context.other);
	};
	if (url.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		open();
	} else if (!_bot
		|| _bot->isVerified()
		|| _bot->session().local().isBotTrustedOpenGame(_bot->id)) {
		open();
	} else {
		const auto callback = [=, bot = _bot] {
			Ui::hideLayer();
			bot->session().local().markBotTrustedOpenGame(bot->id);
			open();
		};
		Ui::show(Box<ConfirmBox>(
			tr::lng_allow_bot_pass(tr::now, lt_bot_name, _bot->name),
			tr::lng_allow_bot(tr::now),
			callback));
	}
}

auto HiddenUrlClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::CustomUrl, url() };
}

QString MentionClickHandler::copyToClipboardContextItemText() const {
	return tr::lng_context_copy_mention(tr::now);
}

void MentionClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		if (const auto m = App::main()) { // multi good
			using Info = Window::SessionNavigation::PeerByLinkInfo;
			m->controller()->showPeerByLink(Info{
				.usernameOrId = _tag.mid(1),
				.messageId = ShowAtProfileMsgId
			});
		}
	}
}

auto MentionClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::Mention };
}

void MentionNameClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		if (auto user = _session->data().userLoaded(_userId)) {
			Ui::showPeerProfile(user);
		}
	}
}

auto MentionNameClickHandler::getTextEntity() const -> TextEntity {
	const auto data = QString::number(_userId.bare)
		+ '.'
		+ QString::number(_accessHash);
	return { EntityType::MentionName, data };
}

QString MentionNameClickHandler::tooltip() const {
	if (const auto user = _session->data().userLoaded(_userId)) {
		const auto name = user->name;
		if (name != _text) {
			return name;
		}
	}
	return QString();
}

QString HashtagClickHandler::copyToClipboardContextItemText() const {
	return tr::lng_context_copy_hashtag(tr::now);
}

void HashtagClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		SearchByHashtag(context, _tag);
	}
}

auto HashtagClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::Hashtag };
}

QString CashtagClickHandler::copyToClipboardContextItemText() const {
	return tr::lng_context_copy_hashtag(tr::now);
}

void CashtagClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		SearchByHashtag(context, _tag);
	}
}

auto CashtagClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::Cashtag };
}

void BotCommandClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button != Qt::LeftButton && button != Qt::MiddleButton) {
		return;
	}
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto delegate = my.elementDelegate ? my.elementDelegate() : nullptr) {
		delegate->elementSendBotCommand(_cmd, my.itemId);
	} else if (const auto controller = my.sessionWindow.get()) {
		auto &data = controller->session().data();
		const auto peer = my.peer
			? my.peer
			: my.itemId
			? data.message(my.itemId)->history()->peer.get()
			: nullptr;
		// Can't find context.
		if (!peer) {
			return;
		}
		controller->widget()->ui_hideSettingsAndLayer(anim::type::normal);
		Core::App().hideMediaView();
		controller->content()->sendBotCommand({
			.peer = peer,
			.command = _cmd,
			.context = my.itemId,
			.replyTo = 0,
		});
	}
}

auto BotCommandClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::BotCommand };
}
