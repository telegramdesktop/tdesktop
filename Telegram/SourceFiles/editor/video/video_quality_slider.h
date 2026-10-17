/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/video/video_editor_quality.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace Editor {

class VideoQualitySlider final : public Ui::RpWidget {
public:
	explicit VideoQualitySlider(not_null<Ui::RpWidget*> parent);

	void setLevels(std::vector<VideoQualityLevel> levels);

	[[nodiscard]] bool hasChoice() const;

	[[nodiscard]] int value() const;
	void setValue(int shorterSide);

	[[nodiscard]] rpl::producer<int> valueChanges() const {
		return _valueChanges.events();
	}

	[[nodiscard]] int resizeGetHeight(int newWidth) override;

private:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	[[nodiscard]] int dotX(int index) const;
	[[nodiscard]] int indexAt(int x) const;
	[[nodiscard]] bool labelsFit() const;
	void setIndex(int index, bool notify);
	void applyPosition(int x);

	std::vector<VideoQualityLevel> _levels;
	std::vector<QString> _labels;
	int _index = 0;
	int _labelsWidth = 0;
	bool _pressed = false;

	Ui::Animations::Simple _activeShift;

	rpl::event_stream<int> _valueChanges;

};

} // namespace Editor
