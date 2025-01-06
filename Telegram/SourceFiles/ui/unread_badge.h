/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_custom_emoji.h"
#include "ui/rp_widget.h"

namespace style {
struct VerifiedBadge;
} // namespace style

namespace Ui {

class UnreadBadge : public RpWidget {
public:
	using RpWidget::RpWidget;

	void setText(const QString &text, bool active);
	int textBaseline() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QString _text;
	bool _active = false;

};

struct BotVerifyDetails {
	UserId botId = 0;
	DocumentId iconId = 0;
	TextWithEntities description;

	explicit operator bool() const {
		return iconId != 0;
	}
	friend inline bool operator==(
		const BotVerifyDetails &,
		const BotVerifyDetails &) = default;
};

class PeerBadge {
public:
	PeerBadge();
	~PeerBadge();

	struct Descriptor {
		not_null<PeerData*> peer;
		QRect rectForName;
		int nameWidth = 0;
		int outerWidth = 0;
		const style::icon *verified = nullptr;
		const style::icon *premium = nullptr;
		const style::color *scam = nullptr;
		const style::color *premiumFg = nullptr;
		Fn<void()> customEmojiRepaint;
		crl::time now = 0;
		bool prioritizeVerification = false;
		bool bothVerifyAndStatus = false;
		bool paused = false;
	};
	int drawGetWidth(Painter &p, Descriptor &&descriptor);
	void unload();

	[[nodiscard]] bool ready(const BotVerifyDetails *details) const;
	void set(
		not_null<const BotVerifyDetails*> details,
		Text::CustomEmojiFactory factory,
		Fn<void()> repaint);

	// How much horizontal space the badge took.
	int drawVerified(
		QPainter &p,
		QPoint position,
		const style::VerifiedBadge &st);

private:
	struct EmojiStatus;
	struct BotVerifiedData;

	int drawScamOrFake(Painter &p, const Descriptor &descriptor);
	int drawVerifyCheck(Painter &p, const Descriptor &descriptor);
	int drawPremiumEmojiStatus(Painter &p, const Descriptor &descriptor);
	int drawPremiumStar(Painter &p, const Descriptor &descriptor);

	std::unique_ptr<EmojiStatus> _emojiStatus;
	mutable std::unique_ptr<BotVerifiedData> _botVerifiedData;

};

QSize ScamBadgeSize(bool fake);
void DrawScamBadge(
	bool fake,
	Painter &p,
	QRect rect,
	int outerWidth,
	const style::color &color);

} // namespace Ui
