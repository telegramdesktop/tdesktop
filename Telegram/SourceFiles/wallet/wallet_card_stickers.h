/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "ui/effects/animations.h"
#include "wallet/wallet_card_space.h"

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

void RefreshOwnCard(not_null<Main::Session*> session);
[[nodiscard]] auto OwnCardStickersValue(not_null<Main::Session*> session)
-> rpl::producer<std::vector<Data::WalletCardSticker>>;

class CardStickers final : public base::has_weak_ptr {
public:
	CardStickers(
		not_null<Main::Session*> session,
		rpl::producer<std::vector<Data::WalletCardSticker>> stickers,
		Fn<void()> repaint);
	~CardStickers();

	void paint(QPainter &p, QRect rest);

	void hold(const QString &address);
	void release(const QString &address);
	void land(
		const QString &address,
		float64 fromScale = 1.,
		QImage frame = QImage());
	void preview(const QString &address, const Data::StoryArea &area);
	void clearPreview();
	void play(const QString &address);
	void stopPlayback();

	[[nodiscard]] bool shown(const QString &address) const;
	[[nodiscard]] bool held(const QString &address) const;
	[[nodiscard]] QImage image(const QString &address) const;
	[[nodiscard]] rpl::producer<bool> arrivingValue() const;
	[[nodiscard]] rpl::producer<QString> releases() const;

private:
	struct Entry;
	struct Landing;
	struct Preview;

	void apply(std::vector<Data::WalletCardSticker> &&stickers);
	[[nodiscard]] bool validate(not_null<Entry*> entry);
	void prepare(not_null<Entry*> entry, int side);
	void createPlayer(not_null<Entry*> entry);
	void capture(not_null<Entry*> entry);
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
	void dropLanding(const QString &canonical);
	void pruneLandings(crl::time now);
	[[nodiscard]] bool landingStep(crl::time now);
	void refreshArriving();
	void invalidate();

	const not_null<Main::Session*> _session;
	const Fn<void()> _repaint;
	std::vector<std::unique_ptr<Entry>> _entries;
	std::vector<Landing> _landings;
	std::unique_ptr<Preview> _preview;
	base::flat_set<QString> _holds;
	Ui::Animations::Basic _landingAnimation;
	rpl::variable<bool> _arriving;
	rpl::event_stream<QString> _releases;
	QImage _layer;
	QSize _layerSize;
	int _layerRatio = 0;
	bool _layerValid = false;
	rpl::lifetime _lifetime;

};

struct CardHost {
	not_null<Ui::RpWidget*> body;
	base::weak_ptr<CardStickers> painter;
	Fn<CardFold()> fold;
	Fn<std::vector<QRectF>()> protectedAreas;
	Fn<void()> bring;
	Fn<void(const CardLook&)> prepareColor;
};

[[nodiscard]] CardSpace CurrentCardSpace(
	const CardHost &host,
	const std::vector<Data::WalletCardSticker> &stickers);

} // namespace Wallet
