/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

class PeerData;

namespace Ui {
class VerticalLayout;
} // namespace Ui

void AddPermanentLinkBlock(
	not_null<Ui::VerticalLayout*> container,
	not_null<PeerData*> peer);

void ManageInviteLinksBox(
	not_null<Ui::GenericBox*> box,
	not_null<PeerData*> peer);
