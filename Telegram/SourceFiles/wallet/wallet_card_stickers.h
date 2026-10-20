/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "history/view/media/history_view_sticker_player_abstract.h"
#include "ui/effects/animations.h"
#include "wallet/wallet_card_space.h"

class DocumentData;

namespace Data {
struct WalletCardSticker;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Wallet {

struct CardLook;

struct StickerPlayback {
	std::unique_ptr<HistoryView::StickerPlayer> player;
	QImage frame;
	QSize frameSize;
	int side = 0;
	int index = 0;
	bool advanced = false;
	bool awaitingFrame = false;
};

struct StickerPlaybackStep {
	QImage fresh;
	bool show = false;
	bool finished = false;
};

struct TravelArtwork {
	StickerPlayback playback;
	DocumentData *document = nullptr;
	QImage rest;
};

void RefreshOwnCard(not_null<Main::Session*> session);
[[nodiscard]] auto OwnCardStickersValue(not_null<Main::Session*> session)
-> rpl::producer<std::vector<Data::WalletCardSticker>>;
[[nodiscard]] QImage CardStickerOutlined(QImage frame, int ratio);

[[nodiscard]] bool TravelArtworkPaused();
void BindStickerPlayback(StickerPlayback &playback, Fn<void()> repaint);
[[nodiscard]] StickerPlaybackStep AdvanceStickerPlayback(
	StickerPlayback &playback,
	crl::time now);
[[nodiscard]] bool PresentStickerPlayback(StickerPlayback &playback);

class CardStickers final : public base::has_weak_ptr {
public:
	CardStickers(
		not_null<Main::Session*> session,
		rpl::producer<std::vector<Data::WalletCardSticker>> stickers,
		Fn<void()> repaint);
	~CardStickers();

	void paint(QPainter &p, const CardFold &fold);
	void paintAbove(QPainter &p, const CardFold &fold) const;

	void hold(const QString &address);
	void release(const QString &address);
	void grip(const QString &address);
	void ungrip(const QString &address);
	void raise(const QString &address);
	void land(
		const QString &address,
		float64 fromScale = 1.,
		QImage frame = QImage(),
		TravelArtwork artwork = TravelArtwork());
	void preview(const QString &address, const Data::StoryArea &area);
	void clearPreview();
	void play(const QString &address);
	void stopPlayback();

	[[nodiscard]] bool shown(const QString &address) const;
	[[nodiscard]] bool held(const QString &address) const;
	[[nodiscard]] QImage image(const QString &address) const;
	[[nodiscard]] std::vector<QString> stack() const;
	[[nodiscard]] const QImage &settledLayer() const;
	[[nodiscard]] rpl::producer<bool> arrivingValue() const;
	[[nodiscard]] rpl::producer<QString> releases() const;

private:
	struct Entry;
	struct Landing;
	struct Sinking;
	struct Preview;

	void apply(std::vector<Data::WalletCardSticker> &&stickers);
	[[nodiscard]] std::vector<not_null<Entry*>> ordered() const;
	void lowerRejected();
	[[nodiscard]] bool validate(not_null<Entry*> entry);
	void prepare(not_null<Entry*> entry, int side);
	void createPlayer(not_null<Entry*> entry);
	void capture(not_null<Entry*> entry);
	void adoptArtwork(not_null<Entry*> entry, TravelArtwork &&artwork);
	void compose(crl::time now);
	[[nodiscard]] bool advance(not_null<Entry*> entry, crl::time now);
	void presented(not_null<Entry*> entry);
	bool paintArt(
		QPainter &p,
		not_null<const Entry*> entry,
		const Data::StoryArea &area,
		float64 scale) const;
	void paintLanding(
		QPainter &p,
		not_null<const Entry*> entry,
		const Landing &landing,
		crl::time now) const;
	[[nodiscard]] Entry *find(const QString &address) const;
	[[nodiscard]] Entry *lookup(const QString &canonical) const;
	void dropArrival(const QString &canonical);
	void pruneLandings(crl::time now);
	[[nodiscard]] bool landingStep(crl::time now);
	void sink(const QString &canonical, crl::time now);
	[[nodiscard]] float64 sinkCover(
		const QString &canonical,
		crl::time now) const;
	void pruneSinkings(crl::time now);
	[[nodiscard]] bool sinkStep(crl::time now);
	void refreshArriving();
	void invalidate();
	void invalidateAbove();

	const not_null<Main::Session*> _session;
	const Fn<void()> _repaint;
	std::vector<std::unique_ptr<Entry>> _entries;
	std::vector<Landing> _landings;
	std::vector<Sinking> _sinkings;
	std::unique_ptr<Preview> _preview;
	base::flat_set<QString> _holds;
	base::flat_set<QString> _grips;
	base::flat_set<QString> _rejectedGrips;
	std::vector<QString> _raised;
	Ui::Animations::Basic _landingAnimation;
	Ui::Animations::Basic _sinkAnimation;
	rpl::variable<bool> _arriving;
	rpl::event_stream<QString> _releases;
	QImage _layer;
	QImage _above;
	QSize _layerSize;
	int _layerRatio = 0;
	bool _layerValid = false;
	bool _aboveValid = false;
	rpl::lifetime _lifetime;

};

struct CardHost {
	not_null<Ui::RpWidget*> body;
	base::weak_ptr<CardStickers> painter;
	Fn<CardFold()> fold;
	Fn<void()> bring;
	Fn<void(const CardLook&)> prepareColor;
};

void StackCardStickers(
	std::vector<Data::WalletCardSticker> &stickers,
	const std::vector<QString> &stack);

} // namespace Wallet
