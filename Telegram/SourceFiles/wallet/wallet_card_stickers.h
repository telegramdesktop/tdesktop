/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct WalletCardSticker;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

void RefreshOwnCard(not_null<Main::Session*> session);
[[nodiscard]] auto OwnCardStickersValue(not_null<Main::Session*> session)
-> rpl::producer<std::vector<Data::WalletCardSticker>>;

class CardStickers final {
public:
	CardStickers(
		not_null<Main::Session*> session,
		rpl::producer<std::vector<Data::WalletCardSticker>> stickers,
		Fn<void()> repaint);
	~CardStickers();

	void paint(QPainter &p, QRect rest);

private:
	struct Entry;

	void apply(std::vector<Data::WalletCardSticker> &&stickers);
	[[nodiscard]] bool validate(not_null<Entry*> entry);
	void prepare(not_null<Entry*> entry, int side);
	void createPlayer(not_null<Entry*> entry);
	void capture(not_null<Entry*> entry);
	void compose();

	const not_null<Main::Session*> _session;
	const Fn<void()> _repaint;
	std::vector<std::unique_ptr<Entry>> _entries;
	QImage _layer;
	QSize _layerSize;
	int _layerRatio = 0;
	bool _layerValid = false;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
