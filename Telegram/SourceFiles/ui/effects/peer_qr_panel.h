/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace Ui {

enum class PeerQrPanelContent {
	Animated,
	Masked,
};

struct PeerQrPanelPhase {
	std::optional<crl::time> openedAtClose;
	crl::time elapsed = 0;
	bool shown = false;
	bool animating = false;
};

class PeerQrPanel final : public RpWidget {
public:
	explicit PeerQrPanel(not_null<QWidget*> parent);
	~PeerQrPanel();

	void setIslandRect(QRect island);
	void setColors(QColor island, QColor open);
	void setRadius(int radius);
	void setContent(not_null<RpWidget*> content, PeerQrPanelContent kind);
	void toggle(bool shown, anim::type animated);

	[[nodiscard]] bool toggled() const;
	[[nodiscard]] PeerQrPanelPhase phase() const;
	[[nodiscard]] QPainterPath glassPath() const;
	[[nodiscard]] bool insideGlass(QPoint point) const;
	[[nodiscard]] QRegion interiorRegion() const;
	void pressOutside();

	[[nodiscard]] rpl::producer<float64> progressValue() const;
	[[nodiscard]] rpl::producer<> outsidePresses() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	[[nodiscard]] bool animationCallback(crl::time now);
	void finishToggle();
	void updateAll();
	void updateContentGeometry();
	[[nodiscard]] bool masked() const;
	[[nodiscard]] QRect contentGeometry() const;
	[[nodiscard]] QRectF glassRect() const;
	[[nodiscard]] float64 glassRadius(QRectF glass) const;
	[[nodiscard]] QColor glassColor() const;
	[[nodiscard]] float64 contentOpacity(crl::time now) const;

	rpl::variable<float64> _progress = 0.;
	rpl::event_stream<> _outsidePresses;
	QRect _island;
	QColor _islandColor;
	QColor _openColor;
	int _radius = 0;
	RpWidget *_content = nullptr;
	PeerQrPanelContent _kind = PeerQrPanelContent::Animated;
	QImage _contentCache;
	Animations::Basic _animation;
	std::optional<crl::time> _openedAtClose;
	crl::time _toggledAt = 0;
	float64 _progressFrom = 0.;
	bool _shown = false;

};

} // namespace Ui
