/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/click_handler_types.h"

#include "lang/lang_keys.h"
#include "chat_helpers/bot_command.h"
#include "core/application.h"
#include "core/local_url_handlers.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/popup_menu.h"
#include "base/qthelp_regex.h"
#include "base/qt/qt_key_modifiers.h"
#include "storage/storage_account.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"
#include "inline_bots/bot_attach_web_view.h"
#include "data/data_game.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"
#include "styles/style_calls.h" // groupCallBoxLabel
#include "styles/style_layers.h"

namespace {

[[nodiscard]] TextWithEntities BoldDomainInUrl(const QString &url) {
	auto result = TextWithEntities{ .text = url };

	if (const auto parsedUrl = QUrl(url); parsedUrl.isValid()) {
		if (const auto host = parsedUrl.host(); !host.isEmpty()) {
			if (const auto hostPos = url.indexOf(host); hostPos != -1) {
				auto boldEntity = EntityInText(
					EntityType::Bold,
					hostPos,
					host.length());

				if (host.startsWith("www.")) {
					boldEntity = EntityInText(
						EntityType::Bold,
						hostPos + 4,
						host.length() - 4);
				}

				result.entities.push_back(boldEntity);
			}
		}
	}
	return result;
}

// Possible context owners: media viewer, profile, history widget.

void SearchByHashtag(ClickContext context, const QString &tag) {
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto delegate = my.elementDelegate
		? my.elementDelegate()
		: nullptr) {
		delegate->elementSearchInList(tag, my.itemId);
		return;
	}
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
		"(^|\\.)("
		"telegram\\.(org|me|dog)"
		"|t\\.me"
		"|te\\.?legra\\.ph"
		"|graph\\.org"
		"|fragment\\.com"
		"|telesco\\.pe"
		")$",
		url.host(),
		RegExOption::CaseInsensitive);
}

QString HiddenUrlClickHandler::copyToClipboardText() const {
	return url().startsWith(u"internal:url:"_q)
		? url().mid(u"internal:url:"_q.size())
		: url();
}

QString HiddenUrlClickHandler::copyToClipboardContextItemText() const {
	return url().isEmpty()
		? QString()
		: !url().startsWith(u"internal:"_q)
		? UrlClickHandler::copyToClipboardContextItemText()
		: url().startsWith(u"internal:url:"_q)
		? UrlClickHandler::copyToClipboardContextItemText()
		: QString();
}

QString HiddenUrlClickHandler::dragText() const {
	const auto result = HiddenUrlClickHandler::copyToClipboardText();
	return result.startsWith(u"internal:"_q) ? QString() : result;
}

void HiddenUrlClickHandler::Open(QString url, QVariant context) {
	url = Core::TryConvertUrlToLocal(url);
	if (Core::InternalPassportLink(url)) {
		return;
	}

	const auto open = [=] {
		UrlClickHandler::Open(url, context);
	};
	if (url.startsWith(u"tg://"_q, Qt::CaseInsensitive)
		|| url.startsWith(u"internal:"_q, Qt::CaseInsensitive)) {
		UrlClickHandler::Open(url, QVariant::fromValue([&] {
			auto result = context.value<ClickHandlerContext>();
			result.mayShowConfirmation = !base::IsCtrlPressed();
			return result;
		}()));
	} else {
		const auto parsedUrl = url.startsWith(u"tonsite://"_q)
			? QUrl(url)
			: QUrl::fromUserInput(url);
		if (UrlRequiresConfirmation(parsedUrl) && !base::IsCtrlPressed()) {
			const auto my = context.value<ClickHandlerContext>();
			if (!my.show) {
				Core::App().hideMediaView();
			}
			const auto displayed = parsedUrl.isValid()
				? parsedUrl.toDisplayString()
				: url;
			const auto displayUrl = !IsSuspicious(displayed)
				? displayed
				: parsedUrl.isValid()
				? QString::fromUtf8(parsedUrl.toEncoded())
				: ShowEncoded(displayed);
			const auto controller = my.sessionWindow.get();
			const auto use = controller
				? &controller->window()
				: Core::App().activeWindow();
			auto box = Box([=](not_null<Ui::GenericBox*> box) {
				Ui::ConfirmBox(box, {
					.text = (tr::lng_open_this_link(tr::now)),
					.confirmed = [=](Fn<void()> hide) { hide(); open(); },
					.confirmText = tr::lng_open_link(),
					.labelStyle = my.dark ? &st::groupCallBoxLabel : nullptr,
				});
				const auto &st = my.dark
					? st::groupCallBoxLabel
					: st::boxLabel;
				box->addSkip(st.style.lineHeight - st::boxPadding.bottom());
				const auto url = box->addRow(
					object_ptr<Ui::FlatLabel>(
						box,
						rpl::single(BoldDomainInUrl(displayUrl)),
						st));
				url->setContextMenuHook([=](
						Ui::FlatLabel::ContextMenuRequest request) {
					const auto copyContextText = [=] {
						TextUtilities::SetClipboardText(
							TextForMimeData::Simple(displayUrl));
					};
					if (request.fullSelection) {
						request.menu->addAction(
							tr::lng_context_copy_link(tr::now),
							copyContextText);
					} else if (request.uponSelection
						&& !request.fullSelection) {
						const auto selection = request.selection;
						const auto copySelectedText = [=] {
							TextUtilities::SetClipboardText(
								TextForMimeData::Simple(
									displayUrl.mid(
										selection.from,
										selection.to - selection.from)));
						};
						request.menu->addAction(
							tr::lng_context_copy_selected(tr::now),
							copySelectedText);
					} else if (request.selection.empty()) {
						request.menu->addAction(
							tr::lng_context_copy_link(tr::now),
							copyContextText);
					}
				});
				url->setSelectable(true);
				url->setContextCopyText(tr::lng_context_copy_link(tr::now));
			});
			if (my.show) {
				my.show->showBox(std::move(box));
			} else if (use) {
				use->show(std::move(box));
				use->activate();
			}
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
	const auto openLink = [=] {
		UrlClickHandler::Open(url, context.other);
	};
	const auto my = context.other.value<ClickHandlerContext>();
	const auto weakController = my.sessionWindow;
	const auto controller = weakController.get();
	const auto item = controller
		? controller->session().data().message(my.itemId)
		: nullptr;
	const auto media = item ? item->media() : nullptr;
	const auto game = media ? media->game() : nullptr;
	if (url.startsWith(u"tg://"_q, Qt::CaseInsensitive) || !_bot || !game) {
		openLink();
		return;
	}
	const auto bot = _bot;
	const auto title = game->title;
	const auto itemId = my.itemId;
	const auto openGame = [=] {
		bot->session().attachWebView().open({
			.bot = bot,
			.button = {.url = url.toUtf8() },
			.source = InlineBots::WebViewSourceGame{
				.messageId = itemId,
				.title = title,
			},
		});
	};
	if (_bot->isVerified()
		|| _bot->session().local().isPeerTrustedOpenGame(_bot->id)) {
		openGame();
	} else {
		if (const auto controller = my.sessionWindow.get()) {
			const auto callback = [=, bot = _bot](Fn<void()> close) {
				close();
				bot->session().local().markPeerTrustedOpenGame(bot->id);
				openGame();
			};
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_allow_bot_pass(
					tr::now,
					lt_bot_name,
					_bot->name()),
				.confirmed = callback,
				.confirmText = tr::lng_allow_bot(),
				.labelStyle = my.dark ? &st::groupCallBoxLabel : nullptr,
			}));
		}
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
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		const auto use = controller
			? controller
			: Core::App().activeWindow()
			? Core::App().activeWindow()->sessionController()
			: nullptr;
		if (use) {
			use->showPeerByLink(Window::PeerByLinkInfo{
				.usernameOrId = _tag.mid(1),
				.resolveType = Window::ResolveType::Mention,
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
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			if (auto user = _session->data().userLoaded(_userId)) {
				controller->showPeerInfo(user);
			}
		}
	}
}

auto MentionNameClickHandler::getTextEntity() const -> TextEntity {
	const auto data = TextUtilities::MentionNameDataFromFields({
		.selfId = _session->userId().bare,
		.userId = _userId.bare,
		.accessHash = _accessHash,
	});
	return { EntityType::MentionName, data };
}

QString MentionNameClickHandler::tooltip() const {
	if (const auto user = _session->data().userLoaded(_userId)) {
		const auto name = user->name();
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
	if (const auto delegate = my.elementDelegate
		? my.elementDelegate()
		: nullptr) {
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
		});
	}
}

auto BotCommandClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::BotCommand };
}

MonospaceClickHandler::MonospaceClickHandler(
	const QString &text,
	EntityType type)
: _text(text)
, _entity({ type }) {
}

void MonospaceClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button != Qt::LeftButton && button != Qt::MiddleButton) {
		return;
	}
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto controller = my.sessionWindow.get()) {
		controller->showToast(tr::lng_text_copied(tr::now));
	}
	TextUtilities::SetClipboardText(TextForMimeData::Simple(_text.trimmed()));
}

auto MonospaceClickHandler::getTextEntity() const -> TextEntity {
	return _entity;
}

QString MonospaceClickHandler::url() const {
	return _text;
}
