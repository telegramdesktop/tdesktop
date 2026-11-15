/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_qr_gifts.h"

#include "base/weak_ptr.h"
#include "boxes/star_gift_box.h"
#include "chat_helpers/stickers_lottie.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_peer_share_theme.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "history/view/media/history_view_sticker_player.h"
#include "info/profile/info_profile_share_theme.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"

namespace Info::Profile {
namespace {

using SizeTag = Data::CustomEmojiManager::SizeTag;

[[nodiscard]] QRect FrameTarget(
		QPoint position,
		int size,
		const QImage &frame) {
	const auto logical = frame.size() / style::DevicePixelRatio();
	return QRect(
		position + QPoint(
			(size - logical.width()) / 2,
			(size - logical.height()) / 2),
		logical);
}

class GiftModelArt final : public Ui::Text::CustomEmoji {
public:
	GiftModelArt(
		not_null<DocumentData*> document,
		int size,
		Fn<void()> repaint);

	int width() override;
	QString entityData() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	void createPlayer();

	const not_null<DocumentData*> _document;
	const int _size = 0;
	const Fn<void()> _repaint;
	std::shared_ptr<Data::DocumentMedia> _media;
	std::unique_ptr<HistoryView::StickerPlayer> _player;
	QImage _first;
	crl::time _shownAt = 0;
	rpl::lifetime _loading;

};

GiftModelArt::GiftModelArt(
	not_null<DocumentData*> document,
	int size,
	Fn<void()> repaint)
: _document(document)
, _size(size)
, _repaint(std::move(repaint)) {
	if (!_document->sticker()) {
		return;
	}
	_media = _document->createMediaView();
	_media->checkStickerLarge();
	_media->goodThumbnailWanted();
	_loading = rpl::single() | rpl::then(
		_document->session().downloaderTaskFinished()
	) | rpl::filter([=] {
		return _media->loaded();
	}) | rpl::on_next([=] {
		_loading.destroy();
		createPlayer();
	});
	if (_player) {
		_loading.destroy();
	}
}

int GiftModelArt::width() {
	return _size;
}

QString GiftModelArt::entityData() {
	return Data::SerializeCustomEmojiId(_document);
}

void GiftModelArt::paint(QPainter &p, const Context &context) {
	if (!ready()) {
		return;
	}
	const auto size = QSize(_size, _size);
	const auto transparent = QColor(0, 0, 0, 0);
	if (context.internal.forceFirstFrame) {
		if (_first.isNull()) {
			auto info = _player->frame(
				size,
				transparent,
				false,
				context.now,
				true);
			if (!info.index) {
				_first = std::move(info.image);
			}
		}
		if (!_first.isNull()) {
			p.drawImage(FrameTarget(context.position, _size, _first), _first);
		}
		return;
	}
	const auto info = _player->frame(
		size,
		transparent,
		false,
		context.now,
		context.paused);
	if (!info.index && _first.isNull()) {
		_first = info.image;
	}
	p.drawImage(FrameTarget(context.position, _size, info.image), info.image);
	if (!context.paused && _shownAt != context.now) {
		_shownAt = context.now;
		_player->markFrameShown();
	}
}

void GiftModelArt::unload() {
	_player = nullptr;
	_first = QImage();
	_shownAt = 0;
}

bool GiftModelArt::ready() {
	if (!_player && _media && _media->loaded()) {
		createPlayer();
	}
	return _player && _player->ready();
}

bool GiftModelArt::readyInDefaultState() {
	return ready();
}

void GiftModelArt::createPlayer() {
	const auto sticker = _document->sticker();
	if (_player || !sticker || !_media) {
		return;
	}
	const auto size = QSize(_size, _size);
	if (sticker->isLottie()) {
		_player = std::make_unique<HistoryView::LottiePlayer>(
			ChatHelpers::LottiePlayerFromDocument(
				_media.get(),
				ChatHelpers::StickerLottieSize::MessageHistory,
				size,
				Lottie::Quality::High));
	} else if (sticker->isWebm()) {
		_player = std::make_unique<HistoryView::WebmPlayer>(
			_media->owner()->location(),
			_media->bytes(),
			size);
	} else {
		_player = std::make_unique<HistoryView::StaticStickerPlayer>(
			_media->owner()->location(),
			_media->bytes(),
			size);
	}
	_player->setRepaintCallback(_repaint);
}

[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> CreateModelArt(
		not_null<Main::Session*> session,
		QStringView key,
		Fn<void()> repaint) {
	const auto id = Data::ParseCustomEmojiData(key);
	if (!id) {
		return nullptr;
	}
	return std::make_unique<GiftModelArt>(
		session->data().document(id),
		st::peerQrGiftModelFrame,
		std::move(repaint));
}

[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> CreatePatternArt(
		not_null<Main::Session*> session,
		QStringView key,
		Fn<void()> repaint) {
	const auto id = Data::ParseCustomEmojiData(key);
	if (!id) {
		return nullptr;
	}
	return session->data().customEmojiManager().create(
		id,
		std::move(repaint),
		SizeTag::Large);
}

[[nodiscard]] const Data::SavedStarGift *FindGift(
		const std::vector<Data::SavedStarGift> &list,
		uint64 id) {
	const auto i = ranges::find_if(list, [&](
			const Data::SavedStarGift &entry) {
		return (entry.info.unique->id == id);
	});
	return (i != end(list)) ? &*i : nullptr;
}

void AppendGifts(
		std::vector<Data::SavedStarGift> &list,
		std::vector<Data::SavedStarGift> &&added) {
	for (auto &entry : added) {
		const auto unique = entry.info.unique.get();
		if (unique && !unique->burned && !FindGift(list, unique->id)) {
			list.push_back(std::move(entry));
		}
	}
}

[[nodiscard]] QrGiftsSlice MakeSlice(
		const std::vector<Data::SavedStarGift> &list,
		bool complete) {
	auto result = QrGiftsSlice{ .complete = complete };
	result.gifts.reserve(list.size());
	for (const auto &entry : list) {
		result.gifts.push_back(QrGiftSwatch(*entry.info.unique));
	}
	return result;
}

void BrowseGifts(
		not_null<PeerData*> peer,
		Window::SessionController *controller) {
	if (!controller) {
		return;
	} else if (peer->isSelf()) {
		Ui::ShowStarGiftBox(controller, controller->session().user());
	} else if (Ui::CanSendStarGiftTo(peer)) {
		Ui::ShowStarGiftBox(controller, peer);
	}
}

[[nodiscard]] rpl::producer<bool> BrowseShownValue(
		not_null<PeerData*> peer,
		Window::SessionController *controller) {
	if (!controller) {
		return rpl::single(false);
	} else if (peer->isSelf()) {
		return rpl::single(true);
	} else if (const auto channel = peer->asBroadcast()) {
		if (!channel->wasFullUpdated()) {
			channel->updateFull();
		}
		return channel->flagsValue(
		) | rpl::to_empty | rpl::map([=] {
			return Ui::CanSendStarGiftTo(peer);
		}) | rpl::distinct_until_changed();
	}
	return rpl::single(false);
}

} // namespace

QrGiftList CreateQrGiftList(
		not_null<PeerData*> peer,
		Window::SessionController *controller,
		rpl::lifetime &lifetime) {
	struct State {
		std::vector<Data::SavedStarGift> list;
		QString offset;
		bool complete = false;
		bool loaded = false;
		rpl::lifetime loading;
		rpl::event_stream<QrGiftsSlice> slices;
	};
	const auto state = lifetime.make_state<State>();
	const auto request = [=](QString offset) {
		state->loading = Data::PeerUniqueGiftsSlice(
			peer,
			Data::MyUniqueType::OnlyOwned,
			offset
		) | rpl::on_next([=](Data::MyGiftsDescriptor slice) {
			state->loading.destroy();
			if (offset.isEmpty()) {
				state->list.clear();
			}
			AppendGifts(state->list, std::move(slice.list));
			state->offset = std::move(slice.offset);
			state->complete = state->offset.isEmpty();
			state->loaded = true;
			state->slices.fire(MakeSlice(state->list, state->complete));
		});
	};
	request(QString());

	const auto weak = base::make_weak(controller);
	return {
		.slices = state->slices.events(),
		.browseShown = BrowseShownValue(peer, controller),
		.refresh = [=] {
			request(QString());
		},
		.loadMore = [=] {
			if (state->loaded && !state->complete && !state->loading) {
				request(state->offset);
			}
		},
		.browse = [=] {
			BrowseGifts(peer, weak.get());
		},
		.save = [=](
				const Ui::PeerQrThemeKey &key,
				Fn<void()> done,
				Fn<void(QString)> fail) {
			const auto entry = FindGift(state->list, key.gift);
			if (!entry) {
				if (fail) {
					fail(QString());
				}
				return;
			}
			SaveQrGiftTheme(
				peer,
				Data::ShareThemeGift{
					.gift = entry->info,
					.savedId = entry->manageId,
				},
				std::move(done),
				std::move(fail));
		},
	};
}

rpl::producer<Ui::PeerQrGiftSource> QrGiftSourceValue(
		not_null<Main::Session*> session) {
	const auto weak = base::make_weak(session);
	const auto guarded = [=](auto create) {
		return [=](QStringView key, Fn<void()> repaint) {
			const auto strong = weak.get();
			return strong
				? create(strong, key, std::move(repaint))
				: std::unique_ptr<Ui::Text::CustomEmoji>();
		};
	};
	return rpl::single(Ui::PeerQrGiftSource{
		.model = guarded(CreateModelArt),
		.pattern = guarded(CreatePatternArt),
		.patternFrame = (Data::FrameSizeFromTag(SizeTag::Large)
			/ style::DevicePixelRatio()),
	});
}

} // namespace Info::Profile
