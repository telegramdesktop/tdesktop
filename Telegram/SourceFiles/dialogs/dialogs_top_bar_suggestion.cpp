/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_top_bar_suggestion.h"

#include "api/api_authorizations.h"
#include "api/api_credits.h"
#include "api/api_peer_photo.h"
#include "api/api_premium.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "boxes/star_gift_box.h" // ShowStarGiftBox.
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/components/promo_suggestions.h"
#include "data/data_birthday.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "history/view/history_view_group_call_bar.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_active_sessions.h"
#include "settings/settings_credits_graphics.h"
#include "settings/settings_premium.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/credits_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"

namespace Dialogs {
namespace {

[[nodiscard]] not_null<Window::SessionController*> FindSessionController(
		not_null<Ui::RpWidget*> widget) {
	const auto window = Core::App().findWindow(widget);
	Assert(window != nullptr);
	return window->sessionController();
}

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
		const std::vector<Data::UnreviewedAuth> &list,
		bool confirmed) {
	if (confirmed) {
		auto text = tr::lng_unconfirmed_auth_confirmed_message(
			tr::now,
			lt_link,
			Ui::Text::Link(tr::lng_settings_sessions_title(tr::now)),
			Ui::Text::RichLangValue);
		auto filter = [=](
				ClickHandlerPtr handler,
				Qt::MouseButton button) {
			if (const auto controller = FindSessionController(parent)) {
				session->api().authorizations().reload();
				controller->showSettings(Settings::Sessions::Id());
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
		if (const auto controller = FindSessionController(parent)) {
			const auto count = float64(list.size());
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(tr::lng_unconfirmed_auth_denied_title(
					lt_count,
					rpl::single(count)));
				Ui::InformBox(box, {
					.text = TextWithEntities()
						.append(messageText)
						.append('\n')
						.append(
							tr::lng_unconfirmed_auth_denied_warning(
								tr::now,
								Ui::Text::Bold)),
					.confirmText = tr::lng_archive_hint_button(tr::now),
				});
			}));
		}
	}
}

constexpr auto kSugSetBirthday = "BIRTHDAY_SETUP"_cs;
constexpr auto kSugBirthdayContacts = "BIRTHDAY_CONTACTS_TODAY"_cs;
constexpr auto kSugPremiumAnnual = "PREMIUM_ANNUAL"_cs;
constexpr auto kSugPremiumUpgrade = "PREMIUM_UPGRADE"_cs;
constexpr auto kSugPremiumRestore = "PREMIUM_RESTORE"_cs;
constexpr auto kSugPremiumGrace = "PREMIUM_GRACE"_cs;
constexpr auto kSugSetUserpic = "USERPIC_SETUP"_cs;
constexpr auto kSugLowCreditsSubs = "STARS_SUBSCRIPTION_LOW_BALANCE"_cs;


} // namespace

rpl::producer<Ui::SlideWrap<Ui::RpWidget>*> TopBarSuggestionValue(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		rpl::producer<bool> outerWrapToggleValue) {
	return [=, outerWrapToggleValue = rpl::duplicate(outerWrapToggleValue)](
			auto consumer) {
		auto lifetime = rpl::lifetime();

		struct Toggle {
			bool value = false;
			anim::type type;
		};

		struct State {
			TopBarSuggestionContent *content = nullptr;
			Ui::SlideWrap<Ui::VerticalLayout> *unconfirmedWarning = nullptr;
			base::unique_qptr<Ui::SlideWrap<Ui::RpWidget>> wrap;
			rpl::variable<int> leftPadding;
			rpl::variable<Toggle> desiredWrapToggle;
			rpl::variable<bool> outerWrapToggle;
			rpl::lifetime birthdayLifetime;
			rpl::lifetime premiumLifetime;
			rpl::lifetime userpicLifetime;
			rpl::lifetime giftsLifetime;
			rpl::lifetime creditsLifetime;
			std::unique_ptr<Api::CreditsHistory> creditsHistory;
		};

		const auto state = lifetime.make_state<State>();
		state->outerWrapToggle = rpl::duplicate(outerWrapToggleValue);
		state->leftPadding = rpl::variable<int>(
			rpl::single(st::dialogsTopBarLeftPadding));
		const auto ensureContent = [=] {
			if (!state->content) {
				state->content = Ui::CreateChild<TopBarSuggestionContent>(
					parent);
				rpl::combine(
					parent->widthValue(),
					state->content->desiredHeightValue()
				) | rpl::start_with_next([=](int width, int height) {
					state->content->resize(width, height);
				}, state->content->lifetime());
			}
		};
		const auto ensureWrap = [=](not_null<Ui::RpWidget*> child) {
			if (!state->wrap) {
				state->wrap
					= base::make_unique_q<Ui::SlideWrap<Ui::RpWidget>>(
						parent,
						object_ptr<Ui::RpWidget>::fromRaw(child));
				state->desiredWrapToggle.force_assign(
					Toggle{ false, anim::type::instant });
			}
		};

		const auto setLeftPaddingRelativeTo = [=](
				not_null<TopBarSuggestionContent*> content,
				not_null<Ui::RpWidget*> relativeTo) {
			content->setLeftPadding(state->leftPadding.value(
				) | rpl::map([w = relativeTo->width()](int padding) {
					return w + padding * 2;
				}));
		};

		const auto processCurrentSuggestion = [=](auto repeat) -> void {
			state->birthdayLifetime.destroy();
			state->premiumLifetime.destroy();
			state->userpicLifetime.destroy();
			state->giftsLifetime.destroy();
			state->creditsLifetime.destroy();

			if (!session->api().authorizations().unreviewed().empty()) {
				state->content = nullptr;
				state->wrap = nullptr;
				const auto &list
					= session->api().authorizations().unreviewed();
				const auto hashes = ranges::views::all(
					list
				) | ranges::views::transform([](const auto &auth) {
					return auth.hash;
				}) | ranges::to_vector;

				const auto content = CreateUnconfirmedAuthContent(
					parent,
					list,
					[=](bool confirmed) {
						ShowAuthToast(parent, session, list, confirmed);
						session->api().authorizations().review(
							hashes,
							confirmed);
					});
				ensureWrap(content);
				const auto wasUnconfirmedWarning = state->unconfirmedWarning;
				state->unconfirmedWarning = content;
				state->desiredWrapToggle.force_assign(Toggle{
					true,
					(state->unconfirmedWarning != wasUnconfirmedWarning)
						? anim::type::instant
						: anim::type::normal,
				});
				return;
			} else {
				if (state->unconfirmedWarning) {
					state->unconfirmedWarning = nullptr;
					state->wrap = nullptr;
				}
			}

			ensureContent();
			ensureWrap(state->content);
			const auto content = state->content;
			const auto wrap = state->wrap.get();
			using RightIcon = TopBarSuggestionContent::RightIcon;
			const auto promo = &session->promoSuggestions();
			if (const auto custom = promo->custom()) {
				content->setRightIcon(RightIcon::Close);
				content->setLeftPadding(state->leftPadding.value());
				content->setClickedCallback([=] {
					const auto controller = FindSessionController(parent);
					UrlClickHandler::Open(
						custom->url,
						QVariant::fromValue(ClickHandlerContext{
							.sessionWindow = base::make_weak(controller),
						}));
				});
				content->setHideCallback([=] {
					promo->dismiss(custom->suggestion);
					repeat(repeat);
				});

				content->setContent(
					custom->title,
					custom->description,
					Core::TextContext({ .session = session }));
				state->desiredWrapToggle.force_assign(
					Toggle{ true, anim::type::normal });
				return;
			} else if (session->premiumCanBuy()
				&& promo->current(kSugPremiumGrace.utf8())) {
				content->setRightIcon(RightIcon::Close);
				content->setLeftPadding(state->leftPadding.value());
				content->setClickedCallback([=] {
					const auto controller = FindSessionController(parent);
					UrlClickHandler::Open(
						u"https://t.me/premiumbot?start=status"_q,
						QVariant::fromValue(ClickHandlerContext{
							.sessionWindow = base::make_weak(controller),
						}));
				});
				content->setHideCallback([=] {
					promo->dismiss(kSugPremiumGrace.utf8());
					repeat(repeat);
				});
				content->setContent(
					tr::lng_dialogs_suggestions_premium_grace_title(
						tr::now,
						Ui::Text::Bold),
					tr::lng_dialogs_suggestions_premium_grace_about(
						tr::now,
						TextWithEntities::Simple));
				state->desiredWrapToggle.force_assign(
					Toggle{ true, anim::type::normal });
				return;
			} else if (session->premiumCanBuy()
				&& promo->current(kSugLowCreditsSubs.utf8())) {
				state->creditsHistory = std::make_unique<Api::CreditsHistory>(
					session->user(),
					false,
					false);
				const auto show = [=](
						const QString &peers,
						uint64 needed,
						uint64 whole) {
					if (whole > needed) {
						return;
					}
					content->setRightIcon(RightIcon::Close);
					content->setLeftPadding(state->leftPadding.value());
					content->setClickedCallback([=] {
						const auto controller = FindSessionController(parent);
						controller->uiShow()->show(Box(
							Settings::SmallBalanceBox,
							controller->uiShow(),
							needed,
							Settings::SmallBalanceSubscription{ peers },
							[=] {
								promo->dismiss(kSugLowCreditsSubs.utf8());
								repeat(repeat);
							}));
					});
					content->setHideCallback([=] {
						promo->dismiss(kSugLowCreditsSubs.utf8());
						repeat(repeat);
					});

					content->setContent(
						tr::lng_dialogs_suggestions_credits_sub_low_title(
							tr::now,
							lt_count,
							float64(needed - whole),
							lt_emoji,
							Ui::MakeCreditsIconEntity(),
							lt_channels,
							{ peers },
							Ui::Text::Bold),
						tr::lng_dialogs_suggestions_credits_sub_low_about(
							tr::now,
							TextWithEntities::Simple),
						Ui::MakeCreditsIconContext(
							content->contentTitleSt().font->height,
							1));
					state->desiredWrapToggle.force_assign(
						Toggle{ true, anim::type::normal });
				};
				session->credits().load();
				state->creditsLifetime.destroy();
				session->credits().balanceValue() | rpl::start_with_next([=] {
					state->creditsLifetime.destroy();
					state->creditsHistory->requestSubscriptions(
						Data::CreditsStatusSlice::OffsetToken(),
						[=](Data::CreditsStatusSlice slice) {
							state->creditsHistory = nullptr;
							auto peers = QStringList();
							auto credits = uint64(0);
							for (const auto &entry : slice.subscriptions) {
								if (entry.barePeerId) {
									const auto peer = session->data().peer(
										PeerId(entry.barePeerId));
									peers.append(peer->name());
									credits += entry.subscription.credits;
								}
							}
							show(
								peers.join(", "),
								credits,
								session->credits().balance().whole());
						},
						true);
				}, state->creditsLifetime);

				return;
			} else if (session->premiumCanBuy()
				&& promo->current(kSugBirthdayContacts.utf8())) {
				promo->requestContactBirthdays(crl::guard(content, [=] {
					const auto users = promo->knownBirthdaysToday().value_or(
						std::vector<UserId>());
					if (users.empty()) {
						repeat(repeat);
						return;
					}

					const auto controller = FindSessionController(parent);
					const auto isSingle = users.size() == 1;
					const auto first = session->data().user(users.front());
					content->setRightIcon(RightIcon::Close);
					content->setClickedCallback([=] {
						if (isSingle) {
							Ui::ShowStarGiftBox(controller, first);
						} else {
							Ui::ChooseStarGiftRecipient(controller);
						}
					});
					content->setHideCallback([=] {
						promo->dismiss(kSugBirthdayContacts.utf8());
						controller->showToast(
							tr::lng_dialogs_suggestions_birthday_contact_dismiss(
								tr::now));
						repeat(repeat);
					});
					auto title = isSingle
						? tr::lng_dialogs_suggestions_birthday_contact_title(
							tr::now,
							lt_text,
							{ first->shortName() },
							Ui::Text::RichLangValue)
						: tr::lng_dialogs_suggestions_birthday_contacts_title(
							tr::now,
							lt_count,
							users.size(),
							Ui::Text::RichLangValue);
					auto text = isSingle
						? tr::lng_dialogs_suggestions_birthday_contact_about(
							tr::now,
							TextWithEntities::Simple)
						: tr::lng_dialogs_suggestions_birthday_contacts_about(
							tr::now,
							TextWithEntities::Simple);
					content->setContent(std::move(title), std::move(text));
					state->giftsLifetime.destroy();
					if (!isSingle) {
						struct UserViews {
							std::vector<HistoryView::UserpicInRow> inRow;
							QImage userpics;
							base::unique_qptr<Ui::RpWidget> widget;
						};
						const auto s
							= state->giftsLifetime.template make_state<
								UserViews>();
						s->widget = base::make_unique_q<Ui::RpWidget>(
							content);
						const auto widget = s->widget.get();
						widget->setAttribute(
							Qt::WA_TransparentForMouseEvents);
						content->sizeValue() | rpl::filter_size(
						) | rpl::start_with_next([=](const QSize &size) {
							widget->resize(size);
							widget->show();
							widget->raise();
						}, widget->lifetime());
						for (const auto &id : users) {
							if (const auto user = session->data().user(id)) {
								s->inRow.push_back({ .peer = user });
							}
						}
						widget->paintRequest() | rpl::start_with_next([=] {
							auto p = QPainter(widget);
							const auto regenerate = [&] {
								if (s->userpics.isNull()) {
									return true;
								}
								for (auto &entry : s->inRow) {
									if (entry.uniqueKey
										!= entry.peer->userpicUniqueKey(
											entry.view)) {
										return true;
									}
								}
								return false;
							}();
							if (regenerate) {
								const auto &st = st::historyCommentsUserpics;
								HistoryView::GenerateUserpicsInRow(
									s->userpics,
									s->inRow,
									st,
									3);
								const auto v = int(users.size() * st.size
									- st.shift);
								content->setLeftPadding(
									state->leftPadding.value(
									) | rpl::map([v](int padding) {
										return padding * 2 + v;
									}));
							}
							p.drawImage(
								state->leftPadding.current(),
								(widget->height()
									- (s->userpics.height()
										/ style::DevicePixelRatio())) / 2,
								s->userpics);
						}, widget->lifetime());
					} else {
						using Ptr = base::unique_qptr<Ui::UserpicButton>;
						const auto ptr
							= state->giftsLifetime.template make_state<Ptr>(
								base::make_unique_q<Ui::UserpicButton>(
									content,
									first,
									st::uploadUserpicButton));
						const auto fake = ptr->get();
						fake->setAttribute(Qt::WA_TransparentForMouseEvents);
						rpl::combine(
							state->leftPadding.value(),
							content->sizeValue() | rpl::filter_size()
						) | rpl::start_with_next([=](int p, const QSize &s) {
							fake->raise();
							fake->show();
							fake->moveToLeft(
								p,
								(s.height() - fake->height()) / 2);
						}, fake->lifetime());
						setLeftPaddingRelativeTo(content, fake);
					}

					state->desiredWrapToggle.force_assign(
						Toggle{ true, anim::type::normal });
				}));
				return;
			} else if (promo->current(kSugSetBirthday.utf8())
				&& !Data::IsBirthdayToday(session->user()->birthday())) {
				content->setRightIcon(RightIcon::Close);
				content->setLeftPadding(state->leftPadding.value());
				content->setClickedCallback([=] {
					const auto controller = FindSessionController(parent);
					Core::App().openInternalUrl(
						u"internal:edit_birthday:add_privacy"_q,
						QVariant::fromValue(ClickHandlerContext{
							.sessionWindow = base::make_weak(controller),
						}));

					state->birthdayLifetime = Info::Profile::BirthdayValue(
						session->user()
					) | rpl::map(
						Data::IsBirthdayTodayValue
					) | rpl::flatten_latest(
					) | rpl::distinct_until_changed(
					) | rpl::start_with_next([=] {
						repeat(repeat);
					});
				});
				content->setHideCallback([=] {
					promo->dismiss(kSugSetBirthday.utf8());
					repeat(repeat);
				});
				content->setContent(
					tr::lng_dialogs_suggestions_birthday_title(
						tr::now,
						Ui::Text::Bold),
					tr::lng_dialogs_suggestions_birthday_about(
						tr::now,
						TextWithEntities::Simple));
				state->desiredWrapToggle.force_assign(
					Toggle{ true, anim::type::normal });
				return;
			} else if (session->premiumPossible() && !session->premium()) {
				const auto isPremiumAnnual = promo->current(
					kSugPremiumAnnual.utf8());
				const auto isPremiumRestore = !isPremiumAnnual
					&& promo->current(kSugPremiumRestore.utf8());
				const auto isPremiumUpgrade = !isPremiumAnnual
					&& !isPremiumRestore
					&& promo->current(kSugPremiumUpgrade.utf8());
				const auto set = [=](QString discount) {
					constexpr auto kMinus = QChar(0x2212);
					const auto &title = isPremiumAnnual
						? tr::lng_dialogs_suggestions_premium_annual_title
						: isPremiumRestore
						? tr::lng_dialogs_suggestions_premium_restore_title
						: tr::lng_dialogs_suggestions_premium_upgrade_title;
					const auto &description = isPremiumAnnual
						? tr::lng_dialogs_suggestions_premium_annual_about
						: isPremiumRestore
						? tr::lng_dialogs_suggestions_premium_restore_about
						: tr::lng_dialogs_suggestions_premium_upgrade_about;
					content->setContent(
						title(
							tr::now,
							lt_text,
							{ discount.replace(kMinus, QChar()) },
							Ui::Text::Bold),
						description(tr::now, TextWithEntities::Simple));
					content->setClickedCallback([=] {
						const auto controller = FindSessionController(parent);
						Settings::ShowPremium(controller, "dialogs_hint");
						promo->dismiss(isPremiumAnnual
							? kSugPremiumAnnual.utf8()
							: isPremiumRestore
							? kSugPremiumRestore.utf8()
							: kSugPremiumUpgrade.utf8());
						repeat(repeat);
					});
					state->desiredWrapToggle.force_assign(
						Toggle{ true, anim::type::normal });
				};
				if (isPremiumAnnual || isPremiumRestore || isPremiumUpgrade) {
					content->setRightIcon(RightIcon::Arrow);
					content->setLeftPadding(state->leftPadding.value());
					const auto api = &session->api().premium();
					api->statusTextValue() | rpl::start_with_next([=] {
						for (const auto &o : api->subscriptionOptions()) {
							if (o.months == 12) {
								set(o.discount);
								state->premiumLifetime.destroy();
								return;
							}
						}
					}, state->premiumLifetime);
					api->reload();
					return;
				}
			}
			if (promo->current(kSugSetUserpic.utf8())
				&& !session->user()->userpicPhotoId()) {
				const auto controller = FindSessionController(parent);
				content->setRightIcon(RightIcon::Close);
				const auto upload = Ui::CreateChild<Ui::UserpicButton>(
					content,
					&controller->window(),
					Ui::UserpicButton::Role::ChoosePhoto,
					st::uploadUserpicButton);
				rpl::combine(
					state->leftPadding.value(),
					content->sizeValue() | rpl::filter_size()
				) | rpl::start_with_next([=](int padding, const QSize &s) {
					upload->raise();
					upload->show();
					upload->moveToLeft(
						padding,
						(s.height() - upload->height()) / 2);
				}, content->lifetime());
				setLeftPaddingRelativeTo(content, upload);
				upload->chosenImages() | rpl::start_with_next([=](
						Ui::UserpicButton::ChosenImage &&chosen) {
					if (chosen.type == Ui::UserpicButton::ChosenType::Set) {
						session->api().peerPhoto().upload(
							session->user(),
							{
								std::move(chosen.image),
								chosen.markup.documentId,
								chosen.markup.colors,
							});
					}
				}, upload->lifetime());

				state->userpicLifetime = session->changes().peerUpdates(
					session->user(),
					Data::PeerUpdate::Flag::Photo
				) | rpl::start_with_next([=] {
					if (session->user()->userpicPhotoId()) {
						repeat(repeat);
					}
				});

				content->setHideCallback([=] {
					promo->dismiss(kSugSetUserpic.utf8());
					repeat(repeat);
				});

				content->setClickedCallback([=] {
					const auto syntetic = [=](QEvent::Type type) {
						Ui::SendSynteticMouseEvent(
							upload,
							type,
							Qt::LeftButton,
							upload->mapToGlobal(QPoint(0, 0)));
					};
					syntetic(QEvent::MouseMove);
					syntetic(QEvent::MouseButtonPress);
					syntetic(QEvent::MouseButtonRelease);
				});
				content->setContent(
					tr::lng_dialogs_suggestions_userpics_title(
						tr::now,
						Ui::Text::Bold),
					tr::lng_dialogs_suggestions_userpics_about(
						tr::now,
						TextWithEntities::Simple));
				state->desiredWrapToggle.force_assign(
					Toggle{ true, anim::type::normal });
				return;
			}
			state->desiredWrapToggle.force_assign(
				Toggle{ false, anim::type::normal });
			base::call_delayed(st::slideWrapDuration * 2, wrap, [=] {
				state->content = nullptr;
				state->wrap = nullptr;
				consumer.put_next(nullptr);
			});
		};

		state->desiredWrapToggle.value() | rpl::combine_previous(
		) | rpl::filter([=] {
			return state->wrap != nullptr;
		}) | rpl::start_with_next([=](Toggle was, Toggle now) {
			state->wrap->toggle(
				state->outerWrapToggle.current() && now.value,
				(was.value == now.value)
					? anim::type::instant
					: now.type);
		}, lifetime);

		state->outerWrapToggle.value() | rpl::combine_previous(
		) | rpl::filter([=] {
			return state->wrap != nullptr;
		}) | rpl::start_with_next([=](bool was, bool now) {
			const auto toggle = state->desiredWrapToggle.current();
			state->wrap->toggle(
				toggle.value && now,
				(was == now) ? toggle.type : anim::type::instant);
		}, lifetime);

		rpl::merge(
			session->promoSuggestions().value(),
			session->api().authorizations().unreviewedChanges(),
			Data::AmPremiumValue(session) | rpl::skip(1) | rpl::to_empty
		) | rpl::start_with_next([=] {
			const auto was = state->wrap.get();
			processCurrentSuggestion(processCurrentSuggestion);
			if (was != state->wrap) {
				consumer.put_next_copy(state->wrap);
			}
		}, lifetime);

		return lifetime;
	};
}

} // namespace Dialogs
