/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "styles/style_widgets.h"

namespace Ui {

class RippleAnimation;

class DiscreteSlider : public RpWidget {
public:
	DiscreteSlider(QWidget *parent);

	void addSection(const QString &label);
	void setSections(const std::vector<QString> &labels);
	int activeSection() const {
		return _activeIndex;
	}
	void setActiveSection(int index);
	void setActiveSectionFast(int index);
	void finishAnimating();

	auto sectionActivated() const {
		return _sectionActivated.events();
	}

protected:
	void timerEvent(QTimerEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	int resizeGetHeight(int newWidth) override = 0;

	struct Section {
		Section(const QString &label, const style::TextStyle &st);

		int left = 0;
		int width = 0;
		Ui::Text::String label;
		std::unique_ptr<RippleAnimation> ripple;
	};

	int getCurrentActiveLeft();

	int getSectionsCount() const {
		return _sections.size();
	}

	template <typename Lambda>
	void enumerateSections(Lambda callback);

	template <typename Lambda>
	void enumerateSections(Lambda callback) const;

	virtual void startRipple(int sectionIndex) {
	}

	void stopAnimation() {
		_a_left.stop();
	}

	void setSelectOnPress(bool selectOnPress);

private:
	void activateCallback();
	virtual const style::TextStyle &getLabelStyle() const = 0;
	virtual int getAnimationDuration() const = 0;

	int getIndexFromPosition(QPoint pos);
	void setSelectedSection(int index);

	std::vector<Section> _sections;
	int _activeIndex = 0;
	bool _selectOnPress = true;

	rpl::event_stream<int> _sectionActivated;

	int _pressed = -1;
	int _selected = 0;
	Ui::Animations::Simple _a_left;

	int _timerId = -1;
	crl::time _callbackAfterMs = 0;

};

class SettingsSlider : public DiscreteSlider {
public:
	SettingsSlider(QWidget *parent, const style::SettingsSlider &st = st::defaultSettingsSlider);

	void setRippleTopRoundRadius(int radius);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

	void startRipple(int sectionIndex) override;

private:
	const style::TextStyle &getLabelStyle() const override;
	int getAnimationDuration() const override;
	QImage prepareRippleMask(int sectionIndex, const Section &section);

	void resizeSections(int newWidth);
	std::vector<float64> countSectionsWidths(int newWidth) const;

	const style::SettingsSlider &_st;
	int _rippleTopRoundRadius = 0;


};

} // namespace Ui
