/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

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

class PeerBadge {
public:
	PeerBadge();
	~PeerBadge();

	struct Descriptor {
		not_null<PeerData*> peer;
		const style::icon *verified = nullptr;
		const style::icon *premium = nullptr;
		const style::color *scam = nullptr;
		const style::color *premiumFg = nullptr;
		Fn<void()> customEmojiRepaint;
		crl::time now = 0;
		bool paused = false;
	};
	int drawGetWidth(
		Painter &p,
		QRect rectForName,
		int nameWidth,
		int outerWidth,
		const Descriptor &descriptor);
	void unload();

private:
	struct EmojiStatus;
	std::unique_ptr<EmojiStatus> _emojiStatus;

};

QSize ScamBadgeSize(bool fake);
void DrawScamBadge(
	bool fake,
	Painter &p,
	QRect rect,
	int outerWidth,
	const style::color &color);

} // namespace Ui
