/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/peer_qr_layer.h"

namespace Ui::Text {
class CustomEmoji;
struct MarkedContext;
} // namespace Ui::Text

namespace Ui {

[[nodiscard]] uint32 PeerQrHash(uint32 value);
[[nodiscard]] uint32 PeerQrNodeHash(int row, int column, uint32 salt);
[[nodiscard]] QImage PeerQrRenderStill(
	not_null<Text::CustomEmoji*> art,
	int frameSize,
	QColor textColor);
void PeerQrApplyItemTransform(
	QPainter &p,
	const QTransform &base,
	QPointF center,
	float64 rotation,
	float64 size,
	float64 frame);
[[nodiscard]] std::optional<PeerQrLayerPlace> PeerQrPlaceItem(
	const PeerQrLayerFalloff &falloff,
	QPointF flat,
	float64 size);
[[nodiscard]] QRectF PeerQrCardCull(QRectF card);
[[nodiscard]] bool PeerQrCovered(
	const QRectF &cull,
	QPointF center,
	float64 size);

using PeerQrEmojiFactory = Fn<std::unique_ptr<Text::CustomEmoji>(
	QStringView,
	const Text::MarkedContext &)>;

struct PeerQrEmojiSource {
	PeerQrEmojiFactory factory;
	int frameSize = 0;
};

struct PeerQrEmojiMetrics {
	float64 skipX = 0.;
	float64 skipY = 0.;
	float64 size = 0.;
	float64 offsetMin = 0.;
	float64 offsetMax = 0.;
	float64 bob = 0.;

	[[nodiscard]] static PeerQrEmojiMetrics Live();
};

struct PeerQrEmojiRest {
	std::vector<QImage> stills;
	PeerQrEmojiMetrics metrics;
};

void PaintPeerQrEmojiRest(
	QPainter &p,
	const PeerQrLayerFalloff &falloff,
	QPointF origin,
	QRectF card,
	const PeerQrEmojiRest &rest);

enum class PeerQrEmojiChange {
	Instant,
	Swap,
	Entry,
};

struct PeerQrEmojiPaint {
	PeerQrLayerFalloff falloff;
	QPointF origin;
	QRectF card;
	crl::time now = 0;
	crl::time elapsed = 0;
	bool paused = false;
};

class PeerQrEmojiField final {
public:
	PeerQrEmojiField(PeerQrEmojiSource source, Fn<void()> repaint);
	~PeerQrEmojiField();

	void setSource(PeerQrEmojiSource source);
	void setPicks(
		std::vector<QString> keys,
		PeerQrEmojiChange change,
		crl::time now);
	void startEntry(crl::time now);
	void setTextColor(QColor color);
	void paint(QPainter &p, const PeerQrEmojiPaint &args);

	[[nodiscard]] bool animating(crl::time now) const;
	[[nodiscard]] PeerQrEmojiRest prepareRest(
		PeerQrEmojiMetrics metrics) const;

private:
	struct Pick {
		QString key;
		std::unique_ptr<Text::CustomEmoji> emoji;
		crl::time readyAt = 0;
	};

	[[nodiscard]] std::unique_ptr<Text::CustomEmoji> createEmoji(
		const QString &key) const;
	[[nodiscard]] const Pick *findPick(const QString &key) const;
	void refreshReady(crl::time now);
	void finishSwap();

	PeerQrEmojiSource _source;
	const Fn<void()> _repaint;
	std::vector<Pick> _picks;
	std::vector<QString> _previous;
	std::vector<Pick> _leaving;
	crl::time _changedAt = 0;
	std::optional<crl::time> _entryAt;
	QColor _textColor;

};

} // namespace Ui
