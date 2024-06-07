/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/credits_graphics.h"

#include <QtCore/QDateTime>

#include "data/data_credits.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "styles/style_credits.h"
#include "styles/style_intro.h" // introFragmentIcon.
#include "styles/style_settings.h"
#include "styles/style_dialogs.h"

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
		case Data::CreditsHistoryEntry::PeerType::PremiumBot:
			return { st::historyPeer8UserpicBg, st::historyPeer8UserpicBg2 };
		case Data::CreditsHistoryEntry::PeerType::Unsupported:
			return {
				st::historyPeerArchiveUserpicBg,
				st::historyPeerArchiveUserpicBg,
			};
		}
		Unexpected("Unknown peer type.");
	}();
	const auto userpic = std::make_shared<Ui::EmptyUserpic>(bg, QString());
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		userpic->paintCircle(p, x, y, outerWidth, size);
		using PeerType = Data::CreditsHistoryEntry::PeerType;
		if (entry.peerType == PeerType::PremiumBot) {
			return;
		}
		const auto rect = QRect(x, y, size, size);
		((entry.peerType == PeerType::AppStore)
			? st::sessionIconiPhone
			: (entry.peerType == PeerType::PlayMarket)
			? st::sessionIconAndroid
			: (entry.peerType == PeerType::Fragment)
			? st::introFragmentIcon
			: st::dialogsInaccessibleUserpic).paintInCenter(p, rect);
	};
}

Fn<void(Painter &, int, int, int, int)> GenerateCreditsPaintEntryCallback(
		not_null<PhotoData*> photo,
		Fn<void()> update) {
	struct State {
		std::shared_ptr<Data::PhotoMedia> view;
		Image *imagePtr = nullptr;
		QImage image;
		rpl::lifetime downloadLifetime;
		bool entryImageLoaded = false;
	};
	const auto state = std::make_shared<State>();
	state->view = photo->createMediaView();
	photo->load(Data::PhotoSize::Thumbnail, {});

	rpl::single(rpl::empty_value()) | rpl::then(
		photo->owner().session().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		using Size = Data::PhotoSize;
		if (const auto large = state->view->image(Size::Large)) {
			state->imagePtr = large;
		} else if (const auto small = state->view->image(Size::Small)) {
			state->imagePtr = small;
		} else if (const auto t = state->view->image(Size::Thumbnail)) {
			state->imagePtr = t;
		}
		update();
		if (state->view->loaded()) {
			state->entryImageLoaded = true;
			state->downloadLifetime.destroy();
		}
	}, state->downloadLifetime);

	return [=](Painter &p, int x, int y, int outerWidth, int size) {
		if (state->imagePtr
			&& (!state->entryImageLoaded || state->image.isNull())) {
			const auto image = state->imagePtr->original();
			const auto minSize = std::min(image.width(), image.height());
			state->image = Images::Prepare(
				image.copy(
					(image.width() - minSize) / 2,
					(image.height() - minSize) / 2,
					minSize,
					minSize),
				size * style::DevicePixelRatio(),
				{ .options = Images::Option::RoundCircle });
		}
		p.drawImage(x, y, state->image);
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
