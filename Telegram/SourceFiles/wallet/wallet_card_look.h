/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "wallet/wallet_card_gradient.h"

class DocumentData;

namespace Data {
struct WalletCardSticker;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Wallet {

struct CardLook {
	QString source;
	QColor center;
	QColor edge;
	DocumentData *pattern = nullptr;

	[[nodiscard]] bool brand() const {
		return source.isEmpty();
	}

	friend inline bool operator==(const CardLook &, const CardLook &) = default;
};

[[nodiscard]] CardLook ResolveCardLook(
	const std::vector<Data::WalletCardSticker> &stickers,
	const QString &background);
[[nodiscard]] rpl::producer<CardLook> OwnCardLookValue(
	not_null<Main::Session*> session);

class CardLookPainter final {
public:
	explicit CardLookPainter(Fn<void()> repaint);
	~CardLookPainter();

	[[nodiscard]] bool setLook(CardLook look);
	void paint(QPainter &p, const QRect &card, float64 angle);
	[[nodiscard]] QColor stripInk();
	[[nodiscard]] CardMarkTint markTint();

private:
	void validateTint();
	void paintPattern(QPainter &p, const QRect &card);

	const Fn<void()> _repaint;
	CardLook _look;
	CardBackground _background;
	std::vector<QColor> _surfaces;
	QColor _ink;
	QColor _markHue;
	std::unique_ptr<Ui::Text::CustomEmoji> _emoji;
	base::flat_map<float64, QImage> _symbols;
	int _symbolsRatio = 0;
	bool _tintValid = false;

};

} // namespace Wallet
