/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/url_auth_box.h"

#include "apiwrap.h"
#include "boxes/url_auth_box_content.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_user.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/emoji_config.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"
#include "styles/style_premium.h"
#include "styles/style_window.h"

namespace UrlAuthBox {
namespace {

using AnotherSessionFactory = Fn<not_null<Main::Session*>()>;
using OnUserChangedCallback = Fn<void(Fn<void()>)>;

struct SwitchAccountResult {
	Ui::RpWidget *widget = nullptr;
	AnotherSessionFactory anotherSession;
	OnUserChangedCallback setOnUserChanged;
	Fn<void(UserId)> updateUserIdHint;
};

[[nodiscard]] std::shared_ptr<Ui::DynamicImage> MakeMatchCodeImage(
		not_null<Main::Session*> session,
		const QString &code) {
	auto emojiLength = 0;
	const auto emoji = Ui::Emoji::Find(code, &emojiLength);
	if (!emoji || emojiLength != code.size()) {
		return nullptr;
	}
	const auto makeFor = [&](not_null<Main::Session*> source) {
		if (const auto sticker = source->emojiStickersPack()
				.stickerForEmoji(emoji)) {
			return Ui::MakeEmojiThumbnail(
				&source->data(),
				Data::SerializeCustomEmojiId(sticker.document),
				nullptr,
				nullptr,
				1);
		}
		return std::shared_ptr<Ui::DynamicImage>();
	};
	if (session->isTestMode()) {
		for (const auto &account : Core::App().domain().orderedAccounts()) {
			if (!account->sessionExists()
				|| account->session().isTestMode()) {
				continue;
			}
			if (const auto image = makeFor(&account->session())) {
				return image;
			}
		}
	}
	return makeFor(session);
}

[[nodiscard]] SwitchAccountResult AddAccountsMenu(
		not_null<Ui::RpWidget*> parent,
		UserId userIdHint = UserId()) {
	const auto session = &Core::App().domain().active().session();
	const auto widget = Ui::CreateChild<SwitchableUserpicButton>(
		parent,
		st::restoreUserpicIcon.photoSize + st::lineWidth * 8);
	struct State {
		base::unique_qptr<Ui::PopupMenu> menu;
		UserData *currentUser = nullptr;
		Fn<void()> onUserChanged;
	};
	const auto state = widget->lifetime().make_state<State>();

	const auto isCurrentTest = session->isTestMode();
	const auto findHintedUser = [&]() -> UserData* {
		if (!userIdHint) {
			return session->user().get();
		}
		for (const auto &account : Core::App().domain().orderedAccounts()) {
			if (!account->sessionExists()
				|| (account->session().isTestMode() != isCurrentTest)) {
				continue;
			}
			if (account->session().userId() == userIdHint) {
				return account->session().user();
			}
		}
		return session->user().get();
	};

	state->currentUser = findHintedUser();
	const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
		parent,
		state->currentUser,
		st::restoreUserpicIcon);
	widget->setUserpic(userpic);
	const auto filtered = [=] {
		auto result = std::vector<not_null<Main::Session*>>();
		for (const auto &account : Core::App().domain().orderedAccounts()) {
			if (!account->sessionExists()
				|| (account->session().user() == state->currentUser)
				|| (account->session().isTestMode() != isCurrentTest)) {
				continue;
			}
			result.push_back(&account->session());
		}
		return result;
	};
	const auto isSingle = filtered().empty();
	widget->setExpanded(!isSingle);
	widget->setAttribute(Qt::WA_TransparentForMouseEvents, isSingle);
	widget->setClickedCallback([=] {
		const auto &st = st::popupMenuWithIcons;
		state->menu = base::make_unique_q<Ui::PopupMenu>(widget, st);
		for (const auto &anotherSession : filtered()) {
			const auto user = anotherSession->user();
			const auto action = new QAction(user->name(), state->menu);
			QObject::connect(action, &QAction::triggered, [=] {
				state->currentUser = user;
				const auto newUserpic = Ui::CreateChild<Ui::UserpicButton>(
					parent,
					user,
					st::restoreUserpicIcon);
				widget->setUserpic(newUserpic);
				if (state->onUserChanged) {
					state->onUserChanged();
				}
			});
			auto owned = base::make_unique_q<Ui::Menu::Action>(
				state->menu->menu(),
				state->menu->menu()->st(),
				action,
				nullptr,
				nullptr);
			const auto menuUserpic = Ui::CreateChild<Ui::UserpicButton>(
				owned.get(),
				user,
				st::lockSetupEmailUserpicSmall);
			menuUserpic->setAttribute(Qt::WA_TransparentForMouseEvents);
			menuUserpic->move(st.menu.itemIconPosition);
			state->menu->addAction(std::move(owned));
		}

		state->menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		state->menu->popup(
			widget->mapToGlobal(
				QPoint(
					widget->width() + st.shadow.extend.right(),
					widget->height())));
	});
	return {
		widget,
		[=] { return &state->currentUser->session(); },
		[=](Fn<void()> callback) { state->onUserChanged = callback; },
		[=](UserId newUserIdHint) {
			const auto isCurrentTest = session->isTestMode();
			for (const auto &acc : Core::App().domain().orderedAccounts()) {
				if (!acc->sessionExists()
					|| (acc->session().isTestMode() != isCurrentTest)) {
					continue;
				}
				if (acc->session().userId() == newUserIdHint) {
					state->currentUser = acc->session().user();
					const auto next = Ui::CreateChild<Ui::UserpicButton>(
						parent,
						state->currentUser,
						st::restoreUserpicIcon);
					widget->setUserpic(next);
					break;
				}
			}
		},
	};
}

} // namespace

void RequestButton(
	std::shared_ptr<Ui::Show> show,
	const MTPDurlAuthResultRequest &request,
	not_null<const HistoryItem*> message,
	int row,
	int column);
void RequestUrl(
	std::shared_ptr<Ui::Show> show,
	const MTPDurlAuthResultRequest &request,
	not_null<Main::Session*> session,
	const QString &url,
	QVariant context);

void ActivateButton(
		std::shared_ptr<Ui::Show> show,
		not_null<const HistoryItem*> message,
		int row,
		int column) {
	const auto itemId = message->fullId();
	const auto button = HistoryMessageMarkupButton::Get(
		&message->history()->owner(),
		itemId,
		row,
		column);
	if (button->requestId || !message->isRegular()) {
		return;
	}
	const auto session = &message->history()->session();
	const auto inputPeer = message->history()->peer->input();
	const auto buttonId = button->buttonId;
	const auto url = QString::fromUtf8(button->data);

	using Flag = MTPmessages_RequestUrlAuth::Flag;
	button->requestId = session->api().request(MTPmessages_RequestUrlAuth(
		MTP_flags(Flag::f_peer | Flag::f_msg_id | Flag::f_button_id),
		inputPeer,
		MTP_int(itemId.msg),
		MTP_int(buttonId),
		MTPstring(), // #TODO auth url
		MTPstring() // in_app_origin
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
			if (const auto url = data.vurl()) {
				UrlClickHandler::Open(qs(url->v));
			}
		}, [&](const MTPDurlAuthResultDefault &data) {
			HiddenUrlClickHandler::Open(url);
		}, [&](const MTPDurlAuthResultRequest &data) {
			if (const auto item = session->data().message(itemId)) {
				RequestButton(show, data, item, row, column);
			}
		});
	}).fail([=] {
		const auto button = HistoryMessageMarkupButton::Get(
			&session->data(),
			itemId,
			row,
			column);
		if (!button) {
			return;
		}

		button->requestId = 0;
		HiddenUrlClickHandler::Open(url);
	}).send();
}

void ActivateUrl(
		std::shared_ptr<Ui::Show> show,
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
		MTP_string(url),
		MTPstring() // in_app_origin
	)).done([=](const MTPUrlAuthResult &result) {
		result.match([&](const MTPDurlAuthResultAccepted &data) {
			UrlClickHandler::Open(qs(data.vurl().value_or_empty()), context);
		}, [&](const MTPDurlAuthResultDefault &data) {
			HiddenUrlClickHandler::Open(url, context);
		}, [&](const MTPDurlAuthResultRequest &data) {
			RequestUrl(show, data, session, url, context);
		});
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"URL_EXPIRED"_q) {
			show->showToast(
				tr::lng_url_auth_phone_toast_bad_expired(tr::now));
		} else {
			HiddenUrlClickHandler::Open(url, context);
		}
	}).send();
}

void RequestButton(
		std::shared_ptr<Ui::Show> show,
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
	if (!button || button->requestId || !message->isRegular()) {
		return;
	}
	const auto session = &message->history()->session();
	const auto inputPeer = message->history()->peer->input();
	const auto buttonId = button->buttonId;
	const auto url = QString::fromUtf8(button->data);

	const auto bot = request.is_request_write_access()
		? session->data().processUser(request.vbot()).get()
		: nullptr;
	const auto box = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	const auto finishWithUrl = [=](const QString &url, bool accepted) {
		if (*box) {
			(*box)->closeBox();
		}
		if (url.isEmpty() && accepted) {
			show->showToast(tr::lng_passport_success(tr::now));
		} else {
			UrlClickHandler::Open(url);
		}
	};
	const auto callback = [=](Result result) {
		if (!result.auth) {
			session->api().request(MTPmessages_DeclineUrlAuth(
				MTP_string(url)
			)).send();
			finishWithUrl(url, false);
		} else if (session->data().message(itemId)) {
			using Flag = MTPmessages_AcceptUrlAuth::Flag;
			const auto flags = Flag(0)
				| (result.allowWrite ? Flag::f_write_allowed : Flag(0))
				| (result.sharePhone ? Flag::f_share_phone_number : Flag(0))
				| (result.matchCode.isEmpty() ? Flag(0) : Flag::f_match_code)
				| (Flag::f_peer | Flag::f_msg_id | Flag::f_button_id);
			session->api().request(MTPmessages_AcceptUrlAuth(
				MTP_flags(flags),
				inputPeer,
				MTP_int(itemId.msg),
				MTP_int(buttonId),
				MTPstring(),
				result.matchCode.isEmpty()
					? MTPstring()
					: MTP_string(result.matchCode)
			)).done([=](const MTPUrlAuthResult &result) {
				const auto accepted = result.match(
				[](const MTPDurlAuthResultAccepted &data) {
					return true;
				}, [](const auto &) {
					return false;
				});
				const auto to = result.match(
				[&](const MTPDurlAuthResultAccepted &data) {
					return qs(data.vurl().value_or_empty());
				}, [&](const MTPDurlAuthResultDefault &data) {
					return url;
				}, [&](const MTPDurlAuthResultRequest &data) {
					LOG(("API Error: "
						"got urlAuthResultRequest after acceptUrlAuth."));
					return url;
				});
				finishWithUrl(to, accepted);
			}).fail([=] {
				finishWithUrl(url, false);
			}).send();
		}
	};
	*box = show->show(
		Box(
			Show,
			url,
			qs(request.vdomain()),
			session->user()->name(),
			bot->firstName,
			callback),
		Ui::LayerOption::KeepOther);
}

void RequestUrl(
		std::shared_ptr<Ui::Show> show,
		const MTPDurlAuthResultRequest &request,
		not_null<Main::Session*> session,
		const QString &url,
		QVariant context) {
	struct State {
		base::weak_qptr<Ui::BoxContent> box;
		AnotherSessionFactory anotherSession = nullptr;
		QString firstMatchCode;
		rpl::lifetime boxDeclineLifetime;
		rpl::lifetime matchCodesBoxDeclineLifetime;
	};
	const auto bot = request.is_request_write_access()
		? session->data().processUser(request.vbot()).get()
		: nullptr;
	const auto requestPhone = request.is_request_phone_number();
	const auto matchCodesFirst = request.is_match_codes_first();
	const auto domain = qs(request.vdomain());
	const auto userIdHint = request.vuser_id_hint()
		? peerToUser(peerFromUser(*request.vuser_id_hint()))
		: UserId();
	const auto matchCodes = [&] {
		auto result = QStringList();
		if (const auto codes = request.vmatch_codes()) {
			for (const auto &code : codes->v) {
				result.push_back(qs(code));
			}
		}
		return result;
	}();
	const auto state = std::make_shared<State>();
	const auto finishWithUrl = [=](const QString &to, bool accepted) {
		if (state->box) {
			state->box->closeBox();
		}

		if ((to.isEmpty() && accepted) || (to == url)) {
		} else {
			UrlClickHandler::Open(to, context);
		}
	};
	const auto resolveSession = [=] {
		return state->anotherSession ? state->anotherSession() : session;
	};
	const auto requestDecline = [=] {
		resolveSession()->api().request(MTPmessages_DeclineUrlAuth(
			MTP_string(url)
		)).send();
	};
	const auto sendRequest = [=](Result result) {
		if (!result.auth) {
			requestDecline();
			finishWithUrl(url, false);
		} else {
			const auto sharePhone = result.sharePhone;
			using Flag = MTPmessages_AcceptUrlAuth::Flag;
			const auto flags = Flag::f_url
				| (result.allowWrite ? Flag::f_write_allowed : Flag(0))
				| (sharePhone ? Flag::f_share_phone_number : Flag(0))
				| (result.matchCode.isEmpty() ? Flag(0) : Flag::f_match_code);
			resolveSession()->api().request(MTPmessages_AcceptUrlAuth(
				MTP_flags(flags),
				MTPInputPeer(),
				MTPint(), // msg_id
				MTPint(), // button_id
				MTP_string(url),
				result.matchCode.isEmpty()
					? MTPstring()
					: MTP_string(result.matchCode)
			)).done([=](const MTPUrlAuthResult &result) {
				const auto accepted = result.match(
				[](const MTPDurlAuthResultAccepted &data) {
					return true;
				}, [](const auto &) {
					return false;
				});
				const auto to = result.match(
				[&](const MTPDurlAuthResultAccepted &data) {
					return qs(data.vurl().value_or_empty());
				}, [&](const MTPDurlAuthResultDefault &data) {
					return url;
				}, [&](const MTPDurlAuthResultRequest &data) {
					LOG(("API Error: "
						"got urlAuthResultRequest after acceptUrlAuth."));
					return url;
				});
				finishWithUrl(to, accepted);
				show->showToast(Ui::Toast::Config{
					.title = tr::lng_url_auth_phone_toast_good_title(tr::now),
					.text = ((requestPhone && !sharePhone)
						? tr::lng_url_auth_phone_toast_good_no_phone
						: tr::lng_url_auth_phone_toast_good)(
							tr::now,
							lt_domain,
							tr::link(domain),
							tr::marked),
					.duration = crl::time(4000),
				});
			}).fail([=] {
				show->showToast(Ui::Toast::Config{
					.title = tr::lng_url_auth_phone_toast_bad_title(tr::now),
					.text = tr::lng_url_auth_phone_toast_bad(
						tr::now,
						lt_domain,
						tr::link(domain),
						tr::marked),
					.duration = crl::time(4000),
				});
				finishWithUrl(url, false);
			}).send();
		}
	};
	const auto browser = qs(request.vbrowser().value_or("Unknown browser"));
	const auto device = qs(request.vplatform().value_or("Unknown platform"));
	const auto ip = qs(request.vip().value_or("Unknown IP"));
	const auto region = qs(request.vregion().value_or("Unknown region"));
	const auto showAuthBox = [=] {
		state->box = show->show(Box([=](not_null<Ui::GenericBox*> box) {
			const auto accountResult = box->lifetime().make_state<
				SwitchAccountResult>(nullptr);
			const auto matchCodesShared = box->lifetime().make_state<
				rpl::variable<QStringList>>(matchCodes);
			const auto reloadRequest = [=] {
				using Flag = MTPmessages_RequestUrlAuth::Flag;
				const auto currentSession = resolveSession();
				currentSession->api().request(MTPmessages_RequestUrlAuth(
					MTP_flags(Flag::f_url),
					MTPInputPeer(),
					MTPint(), // msg_id
					MTPint(), // button_id
					MTP_string(url),
					MTPstring() // in_app_origin
				)).done(crl::guard(box, [=](const MTPUrlAuthResult &result) {
					result.match([&](const MTPDurlAuthResultRequest &data) {
						const auto newUserId = data.vuser_id_hint()
							? peerToUser(peerFromUser(*data.vuser_id_hint()))
							: UserId();
						accountResult->updateUserIdHint(newUserId);
						auto newCodes = QStringList();
						if (const auto codes = data.vmatch_codes()) {
							for (const auto &code : codes->v) {
								newCodes.push_back(qs(code));
							}
						}
						*matchCodesShared = newCodes;
					}, [](const auto &) {});
				})).send();
			};
			const auto callback = [=](Result result) {
				state->boxDeclineLifetime.destroy();
				if (result.matchCode.isEmpty()
					&& !state->firstMatchCode.isEmpty()) {
					result.matchCode = state->firstMatchCode;
				}
				if (!requestPhone) {
					return sendRequest(result);
				}
				box->uiShow()->show(Box([=](not_null<Ui::GenericBox*> box) {
					box->setTitle(tr::lng_url_auth_phone_sure_title());
					const auto confirm = [=](bool confirmed) {
						return [=](Fn<void()> close) {
							auto copy = result;
							copy.sharePhone = confirmed;
							sendRequest(copy);
							close();
						};
					};
					const auto currentSession = resolveSession();
					const auto capitalized = [=](const QString &v) {
						return v.left(1).toUpper() + v.mid(1).toLower();
					};
					using namespace Info::Profile;
					Ui::ConfirmBox(
						box,
						Ui::ConfirmBoxArgs{
							.text = tr::lng_url_auth_phone_sure_text(
								lt_domain,
								rpl::single(tr::bold(capitalized(domain))),
								lt_phone,
								PhoneValue(currentSession->user()),
								tr::rich),
							.confirmed = confirm(true),
							.cancelled = confirm(false),
							.confirmText = tr::lng_allow_bot(),
							.cancelText = tr::lng_url_auth_phone_sure_deny(),
						});
				}));
			};
			ShowDetails(
				box,
				url,
				domain,
				[=](QString code) -> std::shared_ptr<Ui::DynamicImage> {
					return MakeMatchCodeImage(resolveSession(), code);
				},
				callback,
				(bot
					? object_ptr<Ui::UserpicButton>(
						box->verticalLayout(),
						bot,
						st::defaultUserpicButton,
						Ui::PeerUserpicShape::Forum)
					: nullptr),
				bot ? Info::Profile::NameValue(bot) : nullptr,
				browser,
				device,
				ip,
				region,
				(matchCodesFirst
					? (rpl::single(QStringList()) | rpl::type_erased)
					: matchCodesShared->value()));

			*accountResult = AddAccountsMenu(
				box->verticalLayout(),
				userIdHint);
			box->verticalLayout()->widthValue(
			) | rpl::on_next([=, w = (*accountResult).widget] {
				w->moveToRight(st::lineWidth * 4, 0);
			}, (*accountResult).widget->lifetime());
			state->anotherSession = (*accountResult).anotherSession;
			(*accountResult).setOnUserChanged(reloadRequest);
		}));
		state->box->boxClosing() | rpl::on_next([=] {
			requestDecline();
		}, state->boxDeclineLifetime);
	};
	if (!matchCodesFirst || matchCodes.isEmpty()) {
		showAuthBox();
		return;
	}
	auto matchCodesBox = base::weak_qptr<Ui::BoxContent>();
	matchCodesBox = show->show(
		Box([=](not_null<Ui::GenericBox*> matchBox) {
			ShowMatchCodesBox(
				matchBox,
				[=](QString code) -> std::shared_ptr<Ui::DynamicImage> {
					return MakeMatchCodeImage(resolveSession(), code);
				},
				domain,
				matchCodes,
				[=](QString matchCode) {
					state->matchCodesBoxDeclineLifetime.destroy();
					resolveSession()->api().request(
						MTPmessages_CheckUrlAuthMatchCode(
							MTP_string(url),
							MTP_string(matchCode))
					).done([=](const MTPBool &result) {
						if (!mtpIsTrue(result)) {
							show->showToast(
								tr::lng_url_auth_phone_toast_bad_expired(
									tr::now));
							return;
						}
						state->firstMatchCode = std::move(matchCode);
						showAuthBox();
					}).fail([=](const MTP::Error &error) {
						show->showToast((error.type() == u"URL_EXPIRED"_q)
							? tr::lng_url_auth_phone_toast_bad_expired(
								tr::now)
							: error.type());
					}).send();
				});
		}),
		Ui::LayerOption::KeepOther);
	matchCodesBox->boxClosing() | rpl::on_next([=] {
		requestDecline();
	}, state->matchCodesBoxDeclineLifetime);
}

} // namespace UrlAuthBox
