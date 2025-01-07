/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

class DocumentData;
enum class PremiumFeature;

namespace style {
struct RoundButton;
} // namespace style

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {
class RpWidget;
class RoundButton;
class GradientButton;
class VerticalLayout;
} // namespace Ui

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type PremiumId();

void ShowPremium(not_null<::Main::Session*> session, const QString &ref);
void ShowPremium(
	not_null<Window::SessionController*> controller,
	const QString &ref);
void ShowGiftPremium(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	int months,
	bool me);
void ShowEmojiStatusPremium(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

void StartPremiumPayment(
	not_null<Window::SessionController*> controller,
	const QString &ref);

[[nodiscard]] QString LookupPremiumRef(PremiumFeature section);

void ShowPremiumPromoToast(
	std::shared_ptr<ChatHelpers::Show> show,
	TextWithEntities textWithLink,
	const QString &ref);
void ShowPremiumPromoToast(
	std::shared_ptr<::Main::SessionShow> show,
	Fn<Window::SessionController*(not_null<::Main::Session*>)> resolveWindow,
	TextWithEntities textWithLink,
	const QString &ref);

struct SubscribeButtonArgs final {
	Window::SessionController *controller = nullptr;
	not_null<Ui::RpWidget*> parent;
	Fn<QString()> computeRef;
	std::optional<rpl::producer<QString>> text;
	std::optional<QGradientStops> gradientStops;
	Fn<QString()> computeBotUrl; // nullable
	std::shared_ptr<ChatHelpers::Show> show;
	bool showPromo = false;
};


[[nodiscard]] not_null<Ui::RoundButton*> CreateLockedButton(
	not_null<QWidget*> parent,
	rpl::producer<QString> text,
	const style::RoundButton &st,
	rpl::producer<bool> locked);

[[nodiscard]] not_null<Ui::GradientButton*> CreateSubscribeButton(
	SubscribeButtonArgs &&args);

[[nodiscard]] not_null<Ui::GradientButton*> CreateSubscribeButton(
	std::shared_ptr<::Main::SessionShow> show,
	Fn<Window::SessionController*(not_null<::Main::Session*>)> resolveWindow,
	SubscribeButtonArgs &&args);

[[nodiscard]] std::vector<PremiumFeature> PremiumFeaturesOrder(
	not_null<::Main::Session*> session);

void AddSummaryPremium(
	not_null<Ui::VerticalLayout*> content,
	not_null<Window::SessionController*> controller,
	const QString &ref,
	Fn<void(PremiumFeature)> buttonCallback);

[[nodiscard]] std::unique_ptr<Ui::RpWidget> MakeEmojiStatusPreview(
	not_null<QWidget*> parent,
	not_null<DocumentData*> document);
[[nodiscard]] std::unique_ptr<Ui::RpWidget> MakeEmojiSetStatusPreview(
	not_null<QWidget*> parent,
	not_null<PeerData*> peer,
	not_null<DocumentData*> document);

} // namespace Settings

