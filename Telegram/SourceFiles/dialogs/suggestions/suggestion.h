/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Dialogs {

class TopBarSuggestionContent;

namespace TopBarSuggestions {

struct Context {
	not_null<Ui::RpWidget*> parent;
	not_null<Main::Session*> session;
	Fn<not_null<TopBarSuggestionContent*>()> ensureContent;
	Fn<not_null<Window::SessionController*>()> findController;
	Fn<rpl::producer<float64>()> childListShown;
};

struct ActivateArgs {
	Context context;
	not_null<rpl::lifetime*> lifetime;
	Fn<void(not_null<Ui::RpWidget*>, Fn<void()>)> done;
	Fn<void()> recompute;
};

enum class Priority : int {
	UserpicSetup     = 1,
	PremiumOffer     = 2,
	BirthdaySetup    = 3,
	BirthdayContacts = 4,
	LowCreditsSubs   = 5,
	PremiumGrace     = 6,
	CustomPromo      = 7,
	GiftAuctions     = 8,
	UnreviewedAuth   = 9,
};

struct Spec {
	Priority priority = Priority{};
	Fn<bool(const Context&)> available;
	Fn<void(ActivateArgs)> activate;
	bool dayDependent = false;
};

[[nodiscard]] std::vector<Spec> AllSpecs();

[[nodiscard]] Spec MakeBirthdayContactsSpec();
[[nodiscard]] Spec MakeBirthdaySetupSpec();
[[nodiscard]] Spec MakeCustomPromoSpec();
[[nodiscard]] Spec MakeGiftAuctionsSpec();
[[nodiscard]] Spec MakeLowCreditsSubsSpec();
[[nodiscard]] Spec MakePremiumGraceSpec();
[[nodiscard]] Spec MakePremiumOfferSpec();
[[nodiscard]] Spec MakeUnreviewedAuthSpec();
[[nodiscard]] Spec MakeUserpicSetupSpec();

} // namespace TopBarSuggestions
} // namespace Dialogs
