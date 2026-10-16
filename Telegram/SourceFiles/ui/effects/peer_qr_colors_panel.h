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

struct PeerQrThemeKey {
	enum class Type : uchar {
		Default,
		Color,
		Backdrop,
		Other,
	};
	Type type = Type::Default;
	int id = 0;

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

class PeerQrColorsPanel final : public RpWidget {
public:
	explicit PeerQrColorsPanel(not_null<QWidget*> parent);
	~PeerQrColorsPanel();

	[[nodiscard]] static int HeightForWidth(int width);

	void setSwatches(std::vector<PeerQrSwatch> swatches);
	void setSelected(std::optional<PeerQrThemeKey> key, anim::type animated);
	void setIslandRect(QRect island);
	void setColors(const PeerQrColors &colors);
	void toggle(bool shown, anim::type animated);

	[[nodiscard]] bool toggled() const;
	[[nodiscard]] rpl::producer<float64> progressValue() const;
	[[nodiscard]] rpl::producer<PeerQrThemeKey> picks() const;
	[[nodiscard]] rpl::producer<> outsidePresses() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	class Inner;

	[[nodiscard]] bool animationCallback(crl::time now);
	void finishToggle();
	void updateAll();
	[[nodiscard]] QRectF glassRect() const;
	[[nodiscard]] QPainterPath glassPath() const;
	[[nodiscard]] QColor glassColor() const;
	[[nodiscard]] bool insideGlass(QPoint point) const;

	const not_null<ElasticScroll*> _scroll;
	const not_null<Inner*> _inner;
	rpl::variable<float64> _progress = 0.;
	rpl::event_stream<PeerQrThemeKey> _picks;
	rpl::event_stream<> _outsidePresses;
	QRect _island;
	QColor _islandColor;
	Animations::Basic _animation;
	std::optional<crl::time> _openedAtClose;
	crl::time _toggledAt = 0;
	float64 _progressFrom = 0.;
	bool _shown = false;

};

} // namespace Ui
