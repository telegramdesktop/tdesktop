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

namespace Tgw {

class WidgetMedia;

void RegisterCarrier(not_null<WidgetMedia*> media);
void UnregisterCarrier(not_null<WidgetMedia*> media);

void ApplyStateUpdate(
	not_null<Main::Session*> session,
	const MTPDupdateMessageWidgetData &data);
void WatchersChanged();

} // namespace Tgw
