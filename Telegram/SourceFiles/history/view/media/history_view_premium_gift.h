/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_service_box.h"
#include "info/peer_gifts/info_peer_gifts_common.h"

namespace Data {
class MediaGiftBox;
struct GiftCode;
} // namespace Data

namespace HistoryView {

class PremiumGift final : public ServiceBoxContent {
public:
	PremiumGift(
		not_null<Element*> parent,
		not_null<Data::MediaGiftBox*> gift);
	~PremiumGift();

	int top() override;
	int width() override;
	QSize size() override;
	TextWithEntities title() override;
	TextWithEntities subtitle() override;
	rpl::producer<QString> button() override;
	bool buttonMinistars() override;
	QImage cornerTag(const PaintContext &context) override;
	int buttonSkip() override;
	void draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) override;
	ClickHandlerPtr createViewLink() override;

	bool hideServiceText() override;
	void stickerClearLoopPlayed() override;
	std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) override;

	bool hasHeavyPart() override;
	void unloadHeavyPart() override;

private:
	[[nodiscard]] bool incomingGift() const;
	[[nodiscard]] bool outgoingGift() const;
	[[nodiscard]] bool starGift() const;
	[[nodiscard]] bool starGiftUpgrade() const;
	[[nodiscard]] bool gift() const;
	[[nodiscard]] bool creditsPrize() const;
	[[nodiscard]] int credits() const;
	void ensureStickerCreated() const;

	const not_null<Element*> _parent;
	const not_null<Data::MediaGiftBox*> _gift;
	const Data::GiftCode &_data;
	QImage _badgeCache;
	Info::PeerGifts::GiftBadge _badgeKey;
	mutable std::optional<Sticker> _sticker;

};

[[nodiscard]] ClickHandlerPtr OpenStarGiftLink(not_null<HistoryItem*> item);

} // namespace HistoryView
