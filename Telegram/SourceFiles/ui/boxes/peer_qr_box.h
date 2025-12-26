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
class Show;

void DefaultShowFillPeerQrBoxCallback(
	std::shared_ptr<Ui::Show> show,
	PeerData *peer);

void FillPeerQrBox(
	not_null<Ui::GenericBox*> box,
	PeerData *peer,
	std::optional<QString> customLink,
	rpl::producer<QString> about);

} // namespace Ui
