/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"
#include "ui/effects/animations.h"

namespace Ui::Paint {
class Blobs;
} // namespace Ui::Paint

namespace Editor {

class RoundRecordButton final : public Ui::AbstractButton {
public:
	explicit RoundRecordButton(not_null<QWidget*> parent);
	~RoundRecordButton();

	void setAmplitude(float64 amplitude);
	void showAnimated();
	void hideAnimated();

	[[nodiscard]] QPoint center() const;

private:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

	[[nodiscard]] bool inCircle(QPoint point) const;

	const std::unique_ptr<Ui::Paint::Blobs> _blobs;
	const int _center = 0;
	Ui::Animations::Basic _levelAnimation;
	Ui::Animations::Simple _shownAnimation;
	crl::time _lastUpdate = 0;
	bool _shown = false;

};

} // namespace Editor
