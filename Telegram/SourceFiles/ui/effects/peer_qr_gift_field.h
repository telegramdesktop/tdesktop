/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/peer_qr_emoji_field.h"

namespace Ui {

struct PeerQrGiftLook {
	uint64 id = 0;
	QString model;
	QString pattern;
	QColor patternColor;

	[[nodiscard]] explicit operator bool() const {
		return id != 0;
	}

	friend inline bool operator==(
		const PeerQrGiftLook &,
		const PeerQrGiftLook &) = default;
};

using PeerQrGiftArtFactory = Fn<std::unique_ptr<Text::CustomEmoji>(
	QStringView key,
	Fn<void()> repaint)>;

struct PeerQrGiftSource {
	PeerQrGiftArtFactory model;
	PeerQrGiftArtFactory pattern;
	int patternFrame = 0;
};

struct PeerQrGiftMetrics {
	float64 modelSize = 0.;
	float64 modelSkipX = 0.;
	float64 modelSkipY = 0.;
	float64 modelOffset = 0.;
	float64 symbolSize = 0.;
	float64 symbolSkipX = 0.;
	float64 symbolSkipY = 0.;
	float64 symbolJitterX = 0.;
	float64 symbolJitterY = 0.;

	[[nodiscard]] static PeerQrGiftMetrics Live();
};

struct PeerQrGiftRest {
	QImage model;
	QImage symbol;
	PeerQrGiftMetrics metrics;
};

void PaintPeerQrGiftRest(
	QPainter &p,
	const PeerQrLayerFalloff &falloff,
	QPointF origin,
	QRectF card,
	const PeerQrGiftRest &rest);

class PeerQrGiftField final {
public:
	PeerQrGiftField(
		PeerQrGiftSource source,
		PeerQrGiftLook look,
		Fn<void()> repaint,
		bool instant);
	~PeerQrGiftField();

	[[nodiscard]] const PeerQrGiftLook &look() const;
	void setSource(PeerQrGiftSource source);
	void startEntry(crl::time now);
	void paint(QPainter &p, const PeerQrEmojiPaint &args);

	[[nodiscard]] bool animating(crl::time now) const;
	[[nodiscard]] PeerQrGiftRest prepareRest(PeerQrGiftMetrics metrics) const;

private:
	void createArt();
	void validateSymbol(crl::time now);

	PeerQrGiftSource _source;
	const PeerQrGiftLook _look;
	const Fn<void()> _repaint;
	std::unique_ptr<Text::CustomEmoji> _model;
	std::unique_ptr<Text::CustomEmoji> _pattern;
	QImage _symbol;
	std::optional<crl::time> _entryAt;
	crl::time _modelReadyAt = 0;
	crl::time _symbolReadyAt = 0;

};

} // namespace Ui
