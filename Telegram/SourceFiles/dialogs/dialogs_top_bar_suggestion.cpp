/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_top_bar_suggestion.h"

#include "api/api_authorizations.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "core/application.h"
#include "data/components/gift_auctions.h"
#include "data/components/promo_suggestions.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "dialogs/suggestions/suggestion.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "main/main_session.h"
#include "ui/ui_utility.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

[[nodiscard]] not_null<Window::SessionController*> FindSessionController(
		not_null<Ui::RpWidget*> widget) {
	const auto window = Core::App().findWindow(widget);
	Assert(window != nullptr);
	return window->sessionController();
}

} // namespace

rpl::producer<Ui::SlideWrap<Ui::RpWidget>*> TopBarSuggestionValue(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		rpl::producer<bool> outerWrapToggleValue,
		rpl::producer<float64> childListShown,
		rpl::producer<> prepareCollapseSnapshot) {
	return [=,
			outerWrapToggleValue = rpl::duplicate(outerWrapToggleValue),
			childListShown = rpl::duplicate(childListShown),
			prepareCollapseSnapshot = rpl::duplicate(
				prepareCollapseSnapshot)](
			auto consumer) {
		auto lifetime = rpl::lifetime();

		struct Toggle {
			bool value = false;
			anim::type type;
		};

		struct State {
			TopBarSuggestionContent *content = nullptr;
			base::unique_qptr<Ui::SlideWrap<Ui::RpWidget>> wrap;
			rpl::variable<Toggle> desiredWrapToggle;
			rpl::variable<bool> outerWrapToggle;
			rpl::lifetime activeLifetime;
			std::optional<TopBarSuggestions::Priority> activeSpec;
			std::optional<int> activeSpecDay;
			Fn<void()> prepareSnapshot;
			int activationId = 0;
		};

		const auto state = lifetime.make_state<State>();
		state->outerWrapToggle = rpl::duplicate(outerWrapToggleValue);

		const auto ensureContent
		= [=]() -> not_null<TopBarSuggestionContent*> {
			if (!state->content) {
				const auto window = FindSessionController(parent);
				state->content = Ui::CreateChild<TopBarSuggestionContent>(
					parent,
					[=] { return window->isGifPausedAtLeastFor(
						Window::GifPauseReason::Layer); });
				state->content->setCollapseProgress(
					rpl::duplicate(childListShown));
			}
			return state->content;
		};

		const auto context = TopBarSuggestions::Context{
			.parent = parent,
			.session = session,
			.ensureContent = ensureContent,
			.findController = [=]()
			-> not_null<Window::SessionController*> {
				return FindSessionController(parent);
			},
			.childListShown = [=]() -> rpl::producer<float64> {
				return rpl::duplicate(childListShown);
			},
		};

		const auto specs = lifetime.make_state<std::vector<
			TopBarSuggestions::Spec>>(TopBarSuggestions::AllSpecs());
		ranges::sort(
			*specs,
			std::greater<>(),
			&TopBarSuggestions::Spec::priority);

		const auto processCurrentSuggestion = [=](auto repeat) -> void {
			auto winner = (const TopBarSuggestions::Spec*)(nullptr);
			for (auto i = 0; i < int(specs->size()); ++i) {
				const auto &spec = (*specs)[i];
				if (spec.available(context)) {
					winner = &spec;
					break;
				}
			}

			const auto currentDay = QDate::currentDate().day();
			if (winner
				&& winner->dayDependent
				&& state->activeSpecDay
				&& *state->activeSpecDay != currentDay) {
				state->activeSpec = std::nullopt;
			}

			const auto winnerPriority = winner
				? std::optional<TopBarSuggestions::Priority>(winner->priority)
				: std::nullopt;
			if (state->activeSpec == winnerPriority
				&& (!winner || state->wrap)) {
				return;
			}

			state->activeLifetime.destroy();
			state->activeSpec = winnerPriority;
			state->activeSpecDay = (winner && winner->dayDependent)
				? std::optional<int>(currentDay)
				: std::nullopt;
			const auto activationId = ++state->activationId;

			if (winner) {
				auto args = TopBarSuggestions::ActivateArgs{
					.context = context,
					.lifetime = &state->activeLifetime,
					.done = [=](
							not_null<Ui::RpWidget*> widget,
							Fn<void()> prepareSnapshot) {
						if (state->activationId != activationId) {
							return;
						}
						if (state->wrap
							&& state->wrap->entity() != widget.get()) {
							state->wrap = nullptr;
							if (widget.get() != state->content) {
								state->content = nullptr;
							}
							state->prepareSnapshot = nullptr;
						}
						if (!state->wrap) {
							state->wrap = base::make_unique_q<
								Ui::SlideWrap<Ui::RpWidget>>(
								parent,
								object_ptr<Ui::RpWidget>::fromRaw(widget));
							state->desiredWrapToggle.force_assign(
								Toggle{ false, anim::type::instant });
							state->prepareSnapshot = std::move(
								prepareSnapshot);
						}
						state->desiredWrapToggle.force_assign(
							Toggle{ true, anim::type::normal });
					},
					.recompute = [=] {
						if (state->activationId == activationId) {
							repeat(repeat);
						}
					},
				};
				winner->activate(std::move(args));
				return;
			}

			if (!state->wrap) {
				return;
			}
			const auto wrap = state->wrap.get();
			state->desiredWrapToggle.force_assign(
				Toggle{ false, anim::type::normal });
			base::call_delayed(st::slideWrapDuration * 2, wrap, [=] {
				if (state->activeSpec) {
					return;
				}
				state->content = nullptr;
				state->wrap = nullptr;
				state->prepareSnapshot = nullptr;
				consumer.put_next(nullptr);
			});
		};

		state->desiredWrapToggle.value() | rpl::combine_previous(
		) | rpl::filter([=] {
			return state->wrap != nullptr;
		}) | rpl::on_next([=](Toggle was, Toggle now) {
			state->wrap->toggle(
				state->outerWrapToggle.current() && now.value,
				(was.value == now.value)
					? anim::type::instant
					: now.type);
		}, lifetime);

		state->outerWrapToggle.value() | rpl::combine_previous(
		) | rpl::filter([=] {
			return state->wrap != nullptr;
		}) | rpl::on_next([=](bool was, bool now) {
			const auto toggle = state->desiredWrapToggle.current();
			state->wrap->toggle(
				toggle.value && now,
				(was == now) ? toggle.type : anim::type::instant);
		}, lifetime);

		rpl::merge(
			session->promoSuggestions().value(),
			session->api().authorizations().unreviewedChanges(),
			Data::AmPremiumValue(session) | rpl::skip(1) | rpl::to_empty,
			session->giftAuctions().hasActiveChanges() | rpl::to_empty
		) | rpl::on_next([=] {
			const auto was = state->wrap.get();
			const auto weak = base::make_weak(was);
			processCurrentSuggestion(processCurrentSuggestion);
			if (was != state->wrap || (was && !weak)) {
				consumer.put_next_copy(state->wrap.get());
			}
		}, lifetime);

		rpl::duplicate(prepareCollapseSnapshot) | rpl::on_next([=] {
			if (state->prepareSnapshot) {
				state->prepareSnapshot();
			}
		}, lifetime);

		return lifetime;
	};
}

} // namespace Dialogs
