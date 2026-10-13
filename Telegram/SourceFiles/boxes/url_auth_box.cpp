/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/url_auth_box.h"

#include "apiwrap.h"
#include "base/call_delayed.h"
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
#include "main/session/session_show.h"
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
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "wallet/wallet_ton_connect_login.h"

#include "styles/style_userpic_button.h"
#include "styles/style_window.h"

namespace UrlAuthBox {
namespace {

using AnotherSessionFactory = Fn<not_null<Main::Session*>()>;
using OnUserChangedCallback = Fn<void(Fn<void(not_null<Main::Session*>)>)>;

constexpr auto kHintedRequestTimeout = 3 * crl::time(1000);

struct SwitchAccountResult {
	Ui::RpWidget *widget = nullptr;
	AnotherSessionFactory anotherSession;
	OnUserChangedCallback setOnUserChanged;
	Fn<void(Fn<void()>)> setOnUserLost;
	Fn<void(UserId)> updateUserIdHint;
};

[[nodiscard]] not_null<Main::Session*> HintedSession(
		not_null<Main::Session*> session,
		UserId userIdHint) {
	if (!userIdHint) {
		return session;
	}
	for (const auto &account : Core::App().domain().orderedAccounts()) {
		if (account->sessionExists()
			&& (account->session().isTestMode() == session->isTestMode())
			&& (account->session().userId() == userIdHint)) {
			return &account->session();
		}
	}
	return session;
}

[[nodiscard]] MTPmessages_RequestUrlAuth UrlAuthRequest(const QString &url) {
	using Flag = MTPmessages_RequestUrlAuth::Flag;
	return MTPmessages_RequestUrlAuth(
		MTP_flags(Flag::f_url),
		MTPInputPeer(),
		MTPint(), // msg_id
		MTPint(), // button_id
		MTP_string(url),
		MTPstring()); // in_app_origin
}

[[nodiscard]] QStringList MatchCodes(const MTPDurlAuthResultRequest &data) {
	auto result = QStringList();
	if (const auto codes = data.vmatch_codes()) {
		for (const auto &code : codes->v) {
			result.push_back(qs(code));
		}
	}
	return result;
}

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
	const auto widget = Ui::CreateChild<SwitchableUserpicButton>(parent);
	struct State {
		base::unique_qptr<Ui::PopupMenu> menu;
		UserData *currentUser = nullptr;
		Fn<void(not_null<Main::Session*>)> onUserChanged;
		Fn<void()> onUserLost;
		rpl::lifetime currentLifetime;
	};
	const auto state = widget->lifetime().make_state<State>();

	const auto isCurrentTest = session->isTestMode();

	const auto watch = [=](
			not_null<Main::Account*> account,
			rpl::lifetime &lifetime) {
		account->sessionChanges(
		) | rpl::filter(
			!rpl::mappers::_1
		) | rpl::on_next([=] {
			state->menu = nullptr;
			if (const auto onUserLost = state->onUserLost) {
				onUserLost();
			}
		}, lifetime);
	};
	watch(&session->account(), widget->lifetime());
	const auto setCurrentUser = [=](not_null<UserData*> user) {
		state->currentUser = user;
		widget->setUserpic(Ui::CreateChild<Ui::UserpicButton>(
			parent,
			user,
			st::restoreUserpicIcon));
		state->currentLifetime.destroy();
		if (&user->session() != session) {
			watch(&user->session().account(), state->currentLifetime);
		}
	};
	setCurrentUser(HintedSession(session, userIdHint)->user());
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
				if (state->onUserChanged) {
					state->onUserChanged(anotherSession);
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
					widget->width()
						+ Ui::BoxShadow::ExtendFor(st.shadow).right(),
					widget->height())));
	});
	return {
		widget,
		[=] { return &state->currentUser->session(); },
		[=](Fn<void(not_null<Main::Session*>)> callback) {
			state->onUserChanged = std::move(callback);
		},
		[=](Fn<void()> callback) {
			state->onUserLost = std::move(callback);
		},
		[=](UserId newUserIdHint) {
			const auto isCurrentTest = session->isTestMode();
			for (const auto &acc : Core::App().domain().orderedAccounts()) {
				if (!acc->sessionExists()
					|| (acc->session().isTestMode() != isCurrentTest)) {
					continue;
				}
				if (acc->session().userId() == newUserIdHint) {
					if (state->currentUser != acc->session().user()) {
						setCurrentUser(acc->session().user());
					}
					break;
				}
			}
		},
	};
}

// The server put a web login token into this url for the current user.
[[nodiscard]] QVariant AcceptedUrlContext(QVariant context) {
	auto result = context.value<ClickHandlerContext>();
	result.keepWebAuthTokens = true;
	return QVariant::fromValue(result);
}

} // namespace

void RequestButton(
	std::shared_ptr<Ui::Show> show,
	const MTPDurlAuthResultRequest &request,
	not_null<const HistoryItem*> message,
	Api::BotButtonLookup lookup);
void RequestUrl(
	std::shared_ptr<Ui::Show> show,
	const MTPDurlAuthResultRequest &request,
	not_null<Main::Session*> session,
	const QString &url,
	QVariant context);

void ActivateButton(
		std::shared_ptr<Ui::Show> show,
		not_null<const HistoryItem*> message,
		Api::BotButtonLookup lookup) {
	const auto itemId = message->fullId();
	const auto button = lookup();
	if (!button || button->requestId || !message->isRegular()) {
		return;
	}
	const auto session = &message->history()->session();
	const auto inputPeer = message->history()->peer->input();
	const auto buttonId = button->buttonId;
	const auto url = QString::fromUtf8(button->data);
	const auto context = QVariant::fromValue(ClickHandlerContext{
		.peer = message->history()->peer,
	});

	using Flag = MTPmessages_RequestUrlAuth::Flag;
	button->requestId = session->api().request(MTPmessages_RequestUrlAuth(
		MTP_flags(Flag::f_peer | Flag::f_msg_id | Flag::f_button_id),
		inputPeer,
		MTP_int(itemId.msg),
		MTP_int(buttonId),
		MTPstring(), // #TODO auth url
		MTPstring() // in_app_origin
	)).done([=](const MTPUrlAuthResult &result) {
		const auto button = lookup();
		if (!button) {
			return;
		}

		button->requestId = 0;
		result.match([&](const MTPDurlAuthResultAccepted &data) {
			if (const auto url = data.vurl()) {
				UrlClickHandler::Open(qs(url->v), AcceptedUrlContext({}));
			}
		}, [&](const MTPDurlAuthResultDefault &data) {
			HiddenUrlClickHandler::Open(url, context);
		}, [&](const MTPDurlAuthResultRequest &data) {
			if (const auto item = session->data().message(itemId)) {
				RequestButton(show, data, item, lookup);
			}
		});
	}).fail([=] {
		const auto button = lookup();
		if (!button) {
			return;
		}

		button->requestId = 0;
		HiddenUrlClickHandler::Open(url, context);
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

	session->api().request(UrlAuthRequest(url)).done([=](
			const MTPUrlAuthResult &result) {
		result.match([&](const MTPDurlAuthResultAccepted &data) {
			UrlClickHandler::Open(
				qs(data.vurl().value_or_empty()),
				AcceptedUrlContext(context));
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
		Api::BotButtonLookup lookup) {
	const auto itemId = message->fullId();
	const auto button = lookup();
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
			UrlClickHandler::Open(
				url,
				accepted ? AcceptedUrlContext({}) : QVariant());
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
					: MTP_string(result.matchCode),
				MTPInputTonConnectOauthSession() // tonconnect_session
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
	const auto displayName = request.is_is_app()
		? (request.vverified_app_name()
			? qs(*request.vverified_app_name())
			: tr::lng_url_auth_unverified_app(tr::now))
		: qs(request.vdomain());
	*box = show->show(
		Box(
			Show,
			url,
			displayName,
			session->user()->name(),
			bot ? bot->firstName : QString(),
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
		QPointer<Ui::VerticalLayout> walletContainer;
		base::weak_ptr<Wallet::TonConnectLoginSection> wallet;
		rpl::variable<bool> walletBusy;
		rpl::variable<bool> reloading;
		struct Prefetched {
			base::weak_ptr<Main::Session> session;
			std::optional<Wallet::TonConnectLoginRequest> login;
			UserData *bot = nullptr;
			QStringList matchCodes;
		};
		std::optional<Prefetched> prefetched;
		QString firstMatchCode;
		bool boxAnswered = false;
		bool lost = false;
		bool matchCodeSent = false;
		int switchSeq = 0;
	};
	const auto requestWrite = request.is_request_write_access();
	const auto login = Wallet::ParseTonConnectLogin(request);
	const auto bot = (requestWrite || login)
		? session->data().processUser(request.vbot()).get()
		: nullptr;
	const auto requestPhone = request.is_request_phone_number();
	const auto matchCodesFirst = request.is_match_codes_first();
	const auto isApp = request.is_is_app();
	const auto domain = isApp
		? (request.vverified_app_name()
			? qs(*request.vverified_app_name())
			: tr::lng_url_auth_unverified_app(tr::now))
		: qs(request.vdomain());
	const auto userIdHint = request.vuser_id_hint()
		? peerToUser(peerFromUser(*request.vuser_id_hint()))
		: UserId();
	const auto matchCodes = MatchCodes(request);
	const auto state = std::make_shared<State>();
	const auto finishWithUrl = [=](const QString &to, bool accepted) {
		if (state->box) {
			state->box->closeBox();
		}

		if ((to.isEmpty() && accepted) || (to == url)) {
		} else {
			UrlClickHandler::Open(
				to,
				accepted ? AcceptedUrlContext(context) : context);
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
	const auto sendAccept = [=](
			Result result,
			std::optional<MTPInputTonConnectOauthSession> tonconnect) {
		const auto sharePhone = result.sharePhone;
		const auto withWallet = tonconnect.has_value();
		using Flag = MTPmessages_AcceptUrlAuth::Flag;
		const auto flags = Flag::f_url
			| (result.allowWrite ? Flag::f_write_allowed : Flag(0))
			| (sharePhone ? Flag::f_share_phone_number : Flag(0))
			| (result.matchCode.isEmpty() ? Flag(0) : Flag::f_match_code)
			| (withWallet ? Flag::f_tonconnect_session : Flag(0));
		resolveSession()->api().request(MTPmessages_AcceptUrlAuth(
			MTP_flags(flags),
			MTPInputPeer(),
			MTPint(), // msg_id
			MTPint(), // button_id
			MTP_string(url),
			result.matchCode.isEmpty()
				? MTPstring()
				: MTP_string(result.matchCode),
			tonconnect.value_or(
				MTPInputTonConnectOauthSession()) // tonconnect_session
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
			const auto domainWrapped = isApp
				? tr::bold(domain)
				: tr::link(domain);
			show->showToast(Ui::Toast::Config{
				.title = tr::lng_url_auth_phone_toast_good_title(tr::now),
				.text = ((requestPhone && !sharePhone)
					? tr::lng_url_auth_phone_toast_good_no_phone
					: tr::lng_url_auth_phone_toast_good)(
						tr::now,
						lt_domain,
						domainWrapped,
						tr::marked),
				.duration = crl::time(4000),
			});
		}).fail([=](const MTP::Error &error) {
			if (withWallet) {
				LOG(("Wallet Error: messages.acceptUrlAuth "
					"with a wallet grant failed: %1").arg(error.type()));
			}
			const auto domainWrapped = isApp
				? tr::bold(domain)
				: tr::link(domain);
			show->showToast(Ui::Toast::Config{
				.title = tr::lng_url_auth_phone_toast_bad_title(tr::now),
				.text = tr::lng_url_auth_phone_toast_bad(
					tr::now,
					lt_domain,
					domainWrapped,
					tr::marked),
				.duration = crl::time(4000),
			});
			finishWithUrl(url, false);
		}).send();
	};
	const auto sendRequest = [=](Result result) {
		if (state->lost) {
			return;
		} else if (!result.auth) {
			requestDecline();
			finishWithUrl(url, false);
			return;
		}
		const auto wallet = state->wallet.get();
		if (wallet && wallet->wanted()) {
			state->boxAnswered = false;
			wallet->confirm([=](
					const MTPInputTonConnectOauthSession &tonconnect) {
				state->boxAnswered = true;
				sendAccept(result, tonconnect);
			});
			return;
		}
		sendAccept(result, std::nullopt);
	};
	const auto browser = qs(request.vbrowser().value_or("Unknown browser"));
	const auto device = qs(request.vplatform().value_or("Unknown platform"));
	const auto ip = qs(request.vip().value_or("Unknown IP"));
	const auto region = qs(request.vregion().value_or("Unknown region"));
	const auto showAuthBox = [=] {
		state->box = show->show(Box([=](not_null<Ui::GenericBox*> box) {
			const auto accountResult = box->lifetime().make_state<
				SwitchAccountResult>(nullptr);
			const auto prefetched = state->prefetched;
			const auto prefetchedSession = prefetched
				? prefetched->session.get()
				: nullptr;
			const auto matchCodesShared = box->lifetime().make_state<
				rpl::variable<QStringList>>(prefetchedSession
					? prefetched->matchCodes
					: matchCodes);
			const auto reloadLifetime = box->lifetime().make_state<
				rpl::lifetime>();
			const auto buildWallet = [=](
					not_null<Main::Session*> owner,
					std::optional<Wallet::TonConnectLoginRequest> walletLogin,
					UserData *walletBot) {
				if (!state->walletContainer) {
					return;
				}
				state->walletContainer->clear();
				state->wallet = nullptr;
				state->walletBusy = false;
				if (!walletLogin || !walletBot || resolveSession() != owner) {
					return;
				}
				state->wallet = Wallet::AddTonConnectLoginSection(
					state->walletContainer,
					{
						.session = owner,
						.show = Main::MakeSessionShow(box->uiShow(), owner),
						.request = *walletLogin,
						.botName = Info::Profile::NameValue(walletBot),
						.busyChanged = [=](bool busy) {
							state->walletBusy = busy;
						},
					});
			};
			const auto reloadForSession = [=](not_null<Main::Session*> target) {
				if (state->walletBusy.current()) {
					return;
				}
				const auto generation = ++state->switchSeq;
				state->reloading = true;
				reloadLifetime->destroy();
				target->account().sessionChanges(
				) | rpl::filter(
					!rpl::mappers::_1
				) | rpl::on_next([=] {
					if (generation == state->switchSeq) {
						state->reloading = false;
					}
				}, *reloadLifetime);
				target->api().request(
					UrlAuthRequest(url)
				).done(crl::guard(box, [=](const MTPUrlAuthResult &result) {
					if (generation != state->switchSeq) {
						return;
					}
					state->reloading = false;
					result.match([&](const MTPDurlAuthResultRequest &data) {
						accountResult->updateUserIdHint(target->userId());
						const auto newUserId = data.vuser_id_hint()
							? peerToUser(peerFromUser(*data.vuser_id_hint()))
							: UserId();
						accountResult->updateUserIdHint(newUserId);
						*matchCodesShared = MatchCodes(data);
						const auto switched
							= Wallet::ParseTonConnectLogin(data);
						buildWallet(
							target,
							switched,
							(switched
								? target->data().processUser(data.vbot()).get()
								: nullptr));
					}, [](const auto &) {});
				})).fail([=](const MTP::Error &error) {
					if ((generation != state->switchSeq) || !state->box) {
						return;
					}
					state->reloading = false;
					if (error.type() == u"URL_EXPIRED"_q) {
						state->boxAnswered = true;
						state->box->closeBox();
						show->showToast(
							tr::lng_url_auth_phone_toast_bad_expired(tr::now));
					} else {
						show->showToast(error.type());
					}
				}).send();
			};
			const auto callback = [=](Result result) {
				if (state->reloading.current()) {
					return;
				} else if (const auto wallet = state->wallet.get()) {
					if (wallet->busy()) {
						return;
					}
				}
				state->boxAnswered = true;
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
								rpl::single(
									tr::bold(isApp
										? domain
										: capitalized(domain))),
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
			auto walletOwned = object_ptr<Ui::VerticalLayout>(
				box->verticalLayout());
			state->walletContainer = walletOwned.data();
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
				requestWrite ? Info::Profile::NameValue(bot) : nullptr,
				browser,
				device,
				ip,
				region,
				(matchCodesFirst
					? (rpl::single(QStringList()) | rpl::type_erased)
					: matchCodesShared->value()),
				isApp,
				std::move(walletOwned),
				rpl::combine(
					state->walletBusy.value(),
					state->reloading.value(),
					rpl::mappers::_1 || rpl::mappers::_2));

			*accountResult = AddAccountsMenu(
				box->verticalLayout(),
				prefetchedSession ? prefetchedSession->userId() : userIdHint);
			box->verticalLayout()->widthValue(
			) | rpl::on_next([=, w = (*accountResult).widget] {
				w->moveToRight(
					SwitchableUserpicButton::RightSkip(),
					SwitchableUserpicButton::TopSkip());
			}, (*accountResult).widget->lifetime());
			state->anotherSession = (*accountResult).anotherSession;
			(*accountResult).setOnUserChanged(reloadForSession);
			(*accountResult).setOnUserLost([=] {
				state->lost = true;
				buildWallet(session, std::nullopt, nullptr);
				state->boxAnswered = true;
				box->uiShow()->hideLayer();
			});
			if (prefetchedSession && resolveSession() == prefetchedSession) {
				buildWallet(
					prefetchedSession,
					prefetched->login,
					prefetched->bot);
			} else {
				buildWallet(session, login, bot);
				if (login && resolveSession() != session) {
					reloadForSession(resolveSession());
				}
			}
		}));
		if (const auto strong = state->box.get()) {
			strong->boxClosing() | rpl::on_next([=] {
				if (!state->boxAnswered) {
					requestDecline();
				}
			}, strong->lifetime());
		} else {
			// Closed inside show(), so boxClosing() has already passed.
			requestDecline();
		}
	};
	const auto showAuthBoxPrepared = [=] {
		if (!login) {
			showAuthBox();
			return;
		}
		const auto hinted = HintedSession(session, userIdHint);
		if (hinted == session) {
			Wallet::PreloadTonConnectLoginWallet(session, showAuthBox);
			return;
		}
		const auto weak = base::make_weak(session);
		const auto finished = std::make_shared<bool>();
		const auto finish = [=](std::optional<State::Prefetched> data) {
			if (std::exchange(*finished, true)) {
				return;
			}
			const auto strong = weak.get();
			if (!strong) {
				return;
			}
			const auto owner = data ? data->session.get() : nullptr;
			state->prefetched = std::move(data);
			if (!owner) {
				Wallet::PreloadTonConnectLoginWallet(
					strong,
					crl::guard(strong, showAuthBox));
				return;
			}
			Wallet::PreloadTonConnectLoginWallet(
				owner,
				crl::guard(strong, showAuthBox),
				[=] {
					state->prefetched = std::nullopt;
					if (const auto origin = weak.get()) {
						Wallet::PreloadTonConnectLoginWallet(
							origin,
							crl::guard(origin, showAuthBox));
					}
				});
		};
		hinted->api().request(
			UrlAuthRequest(url)
		).done([=](const MTPUrlAuthResult &result) {
			result.match([&](const MTPDurlAuthResultRequest &data) {
				const auto hintedLogin = Wallet::ParseTonConnectLogin(data);
				finish(State::Prefetched{
					.session = base::make_weak(hinted),
					.login = hintedLogin,
					.bot = (hintedLogin
						? hinted->data().processUser(data.vbot()).get()
						: nullptr),
					.matchCodes = MatchCodes(data),
				});
			}, [&](const auto &) {
				finish(std::nullopt);
			});
		}).fail([=](const MTP::Error &error) {
			if (error.type() != u"URL_EXPIRED"_q) {
				finish(std::nullopt);
			} else if (!std::exchange(*finished, true)) {
				show->showToast(
					tr::lng_url_auth_phone_toast_bad_expired(tr::now));
			}
		}).send();
		base::call_delayed(kHintedRequestTimeout, [=] {
			finish(std::nullopt);
		});
	};
	if (!matchCodesFirst || matchCodes.isEmpty()) {
		showAuthBoxPrepared();
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
					state->matchCodeSent = true;
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
						showAuthBoxPrepared();
					}).fail([=](const MTP::Error &error) {
						show->showToast((error.type() == u"URL_EXPIRED"_q)
							? tr::lng_url_auth_phone_toast_bad_expired(
								tr::now)
							: error.type());
					}).send();
				},
				isApp);
		}),
		Ui::LayerOption::KeepOther);
	if (const auto strong = matchCodesBox.get()) {
		strong->boxClosing() | rpl::on_next([=] {
			if (!state->matchCodeSent) {
				requestDecline();
			}
		}, strong->lifetime());
	} else {
		// Closed inside show(), so boxClosing() has already passed.
		requestDecline();
	}
}

} // namespace UrlAuthBox
