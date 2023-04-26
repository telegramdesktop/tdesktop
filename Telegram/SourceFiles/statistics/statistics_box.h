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
} // namespace Ui

void StatisticsBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer);
