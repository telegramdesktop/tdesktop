/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/credits_graphics.h"

#include <QtCore/QDateTime>

#include "data/data_credits.h"
#include "lang/lang_keys.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "styles/style_credits.h"
#include "styles/style_intro.h" // introFragmentIcon.
#include "styles/style_settings.h"

namespace Ui {

using PaintRoundImageCallback = Fn<void(
	Painter &p,
	int x,
	int y,
	int outerWidth,
	int size)>;

PaintRoundImageCallback GenerateCreditsPaintUserpicCallback(
		const Data::CreditsHistoryEntry &entry) {
	const auto bg = [&]() -> Ui::EmptyUserpic::BgColors {
		switch (entry.peerType) {
		case Data::CreditsHistoryEntry::PeerType::Peer:
			return Ui::EmptyUserpic::UserpicColor(0);
		case Data::CreditsHistoryEntry::PeerType::AppStore:
			return { st::historyPeer7UserpicBg, st::historyPeer7UserpicBg2 };
		case Data::CreditsHistoryEntry::PeerType::PlayMarket:
			return { st::historyPeer2UserpicBg, st::historyPeer2UserpicBg2 };
		case Data::CreditsHistoryEntry::PeerType::Fragment:
			return { st::historyPeer8UserpicBg, st::historyPeer8UserpicBg2 };
		}
		Unexpected("Unknown peer type.");
	}();
	const auto userpic = std::make_shared<Ui::EmptyUserpic>(bg, QString());
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		userpic->paintCircle(p, x, y, outerWidth, size);
		using PeerType = Data::CreditsHistoryEntry::PeerType;
		((entry.peerType == PeerType::AppStore)
			? st::sessionIconiPhone
			: (entry.peerType == PeerType::PlayMarket)
			? st::sessionIconAndroid
			: st::introFragmentIcon).paintInCenter(p, { x, y, size, size });
	};
}

TextWithEntities GenerateEntryName(const Data::CreditsHistoryEntry &entry) {
	return ((entry.peerType == Data::CreditsHistoryEntry::PeerType::Fragment)
		? tr::lng_bot_username_description1_link
		: tr::lng_credits_summary_history_entry_inner_in)(
			tr::now,
			TextWithEntities::Simple);
}

} // namespace Ui
