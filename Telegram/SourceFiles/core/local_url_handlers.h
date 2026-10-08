/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace qthelp {
class RegularExpressionMatch;
} // namespace qthelp

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct StarGift;
} // namespace Data

namespace Settings {
struct CreditsEntryBoxStyleOverrides;
struct UniqueGiftCoverActions;
} // namespace Settings

namespace Window {
class SessionController;
} // namespace Window

namespace Core {

struct LocalUrlHandler {
	QString expression;
	Fn<bool(
		Window::SessionController *controller,
		const qthelp::RegularExpressionMatch &match,
		const QVariant &context)> handler;
};

[[nodiscard]] bool TryRouterForLocalUrl(
	Window::SessionController *controller,
	const QString &command);

[[nodiscard]] const std::vector<LocalUrlHandler> &LocalUrlHandlers();
[[nodiscard]] const std::vector<LocalUrlHandler> &InternalUrlHandlers();

[[nodiscard]] QString TryConvertUrlToLocal(QString url);

[[nodiscard]] bool IsMiniAppUrl(const QString &url);

[[nodiscard]] bool InternalPassportOrOAuthLink(const QString &url);

[[nodiscard]] bool StartUrlRequiresActivate(const QString &url);

void ResolveAndShowUniqueGift(
	std::shared_ptr<ChatHelpers::Show> show,
	const QString &slug,
	::Settings::CreditsEntryBoxStyleOverrides st,
	Fn<void(QString)> fail = nullptr,
	Fn<bool(const Data::StarGift &)> validate = nullptr,
	std::shared_ptr<const ::Settings::UniqueGiftCoverActions> actions = nullptr);
void ResolveAndShowUniqueGift(
	std::shared_ptr<ChatHelpers::Show> show,
	const QString &slug,
	Fn<void(QString)> fail = nullptr,
	Fn<bool(const Data::StarGift &)> validate = nullptr);

[[nodiscard]] TimeId ParseVideoTimestamp(QStringView value);

} // namespace Core
