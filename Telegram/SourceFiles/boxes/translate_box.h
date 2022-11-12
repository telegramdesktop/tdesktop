/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Ui {

class GenericBox;

void TranslateBox(
	not_null<Ui::GenericBox*> box,
	not_null<PeerData*> peer,
	MsgId msgId,
	TextWithEntities text);

} // namespace Ui
