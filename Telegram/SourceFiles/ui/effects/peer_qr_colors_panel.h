/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/effects/peer_qr_surface.h"
#include "ui/rp_widget.h"

namespace Ui {

class ElasticScroll;
class PeerQrPanel;

struct PeerQrThemeKey {
	enum class Type : uchar {
		Default,
		Backdrop,
		Gift,
	};
	Type type = Type::Default;
	int id = 0;
	uint64 gift = 0;
	std::vector<QString> emoji;

	friend inline bool operator==(
		const PeerQrThemeKey &,
		const PeerQrThemeKey &) = default;
};

struct PeerQrSwatch {
	PeerQrThemeKey key;
	PeerQrAppearance appearance;

	friend inline bool operator==(
		const PeerQrSwatch &,
		const PeerQrSwatch &) = default;
};

[[nodiscard]] bool PeerQrRingNeedsHairline(QColor color);

class PeerQrSelectionRing final {
public:
	void select(
		int index,
		anim::type animated,
		bool visible,
		Fn<void()> repaint);
	void reset(int index);
	[[nodiscard]] int selectedIndex() const;
	void paint(
		QPainter &p,
		int index,
		QRectF rect,
		float64 minScale,
		bool hairline,
		std::optional<float64> radius = std::nullopt) const;

private:
	Animations::Simple _animation;
	int _selectedIndex = -1;
	int _wasIndex = -1;

};

class PeerQrColorsPanel final : public RpWidget {
public:
	explicit PeerQrColorsPanel(not_null<PeerQrPanel*> panel);
	~PeerQrColorsPanel();

	[[nodiscard]] static int HeightForWidth(int width);

	void setSwatches(std::vector<PeerQrSwatch> swatches);
	void setSelected(std::optional<PeerQrThemeKey> key, anim::type animated);
	void scrollToSelected();

	[[nodiscard]] rpl::producer<PeerQrThemeKey> picks() const;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	class Inner;

	const not_null<ElasticScroll*> _scroll;
	const not_null<Inner*> _inner;
	rpl::event_stream<PeerQrThemeKey> _picks;

};

} // namespace Ui
