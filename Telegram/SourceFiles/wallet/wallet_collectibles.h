/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "wallet/wallet_card_flight.h"

namespace Main {
class SessionShow;
} // namespace Main

namespace Ui {
class ScrollArea;
class VerticalLayout;
} // namespace Ui

namespace Wallet {

class CollectibleMedia;
struct CardHost;
enum class OwnCardChange : uchar;

struct CollectiblesSquares {
	Fn<std::optional<FlightPose>(const QString &address)> pose;
	Fn<void(const QString &address, bool leaving)> leaving;
};

[[nodiscard]] QString OwnCardRefusalText(OwnCardChange change);

[[nodiscard]] CollectiblesSquares AddCollectiblesList(
	not_null<Ui::VerticalLayout*> container,
	not_null<Ui::ScrollArea*> scroll,
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<CollectibleMedia> media,
	CardHost host);

} // namespace Wallet
