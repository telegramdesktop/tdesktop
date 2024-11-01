/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;
class DocumentData;

namespace Api {
struct SendOptions;
} // namespace Api

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace Ui {

class GenericBox;

void SendGifWithCaptionBox(
	not_null<Ui::GenericBox*> box,
	not_null<DocumentData*> document,
	not_null<PeerData*> peer,
	const SendMenu::Details &details,
	Fn<void(Api::SendOptions, TextWithTags)> done);

} // namespace Ui
