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

private:
	struct Entry;
	struct Landing;

	void apply(std::vector<Data::WalletCardSticker> &&stickers);
	[[nodiscard]] bool validate(not_null<Entry*> entry);
	void prepare(not_null<Entry*> entry, int side);
	void createPlayer(not_null<Entry*> entry);
	void capture(not_null<Entry*> entry);
	void compose(crl::time now);
	void paintLanding(
		QPainter &p,
		not_null<const Entry*> entry,
		const Landing &landing,
		crl::time now) const;
	[[nodiscard]] Entry *lookup(const QString &canonical) const;
	void dropLanding(const QString &canonical);
	void pruneLandings(crl::time now);
	[[nodiscard]] bool landingStep(crl::time now);
	void invalidate();

	const not_null<Main::Session*> _session;
	const Fn<void()> _repaint;
	std::vector<std::unique_ptr<Entry>> _entries;
	std::vector<Landing> _landings;
	base::flat_set<QString> _holds;
	Ui::Animations::Basic _landingAnimation;
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
};

[[nodiscard]] CardSpace CurrentCardSpace(
	const CardHost &host,
	const std::vector<Data::WalletCardSticker> &stickers);

} // namespace Wallet
