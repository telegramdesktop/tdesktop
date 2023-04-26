/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/statistics_box.h"

#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"

namespace {
} // namespace

void StatisticsBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer) {
	box->setTitle(tr::lng_stats_title());
}
