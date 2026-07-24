// This file is part of Telegram Desktop,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace Ui {

class DynamicImage;

struct HoveredItemInfo {
	int index = -1;
	QPoint globalPos;
};

class DynamicImagesStrip final : public RpWidget {
public:
	DynamicImagesStrip(
		QWidget *parent,
		std::vector<std::shared_ptr<DynamicImage>> thumbnails,
		int userpicSize,
		int gap);

	void setProgress(float64 progress);
	void setClickCallback(Fn<void(int)> callback);
	[[nodiscard]] rpl::producer<HoveredItemInfo> hoveredItemValue() const;

	void handleKeyPressEvent(QKeyEvent *e);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void startAnimation();
	void updateHoveredItem(int index);
	void setSelectedIndex(int index);

	[[nodiscard]] bool hasMouseMoved() const;
	void mouseMoved();

	std::vector<std::shared_ptr<DynamicImage>> _thumbnails;
	int _userpicSize = 0;
	int _gap = 0;
	float64 _progress = 0.;
	int _hoveredIndex = -1;
	int _motions = 0;
	std::vector<float64> _scales;
	std::vector<float64> _alphas;
	std::vector<float64> _scaleTargets;
	std::vector<float64> _alphaTargets;
	Animations::Basic _animation;
	Fn<void(int)> _clickCallback;
	rpl::event_stream<HoveredItemInfo> _hoveredItem;
	bool _pressed = false;

};

} // namespace Ui
