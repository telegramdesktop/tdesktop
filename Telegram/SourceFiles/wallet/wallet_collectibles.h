/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class SessionShow;
} // namespace Main

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Wallet {

class CollectibleMedia;

void AddCollectiblesList(
	not_null<Ui::VerticalLayout*> container,
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<CollectibleMedia> media);

} // namespace Wallet
