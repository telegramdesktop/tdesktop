/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/round_rect.h"
#include "ui/effects/animations.h"

namespace style {
struct TextStyle;
struct SettingsSlider;
} // namespace style

namespace st {
extern const style::SettingsSlider &defaultSettingsSlider;
} // namespace st

namespace Ui {

class RippleAnimation;

class DiscreteSlider : public RpWidget {
public:
	DiscreteSlider(QWidget *parent, bool snapToLabel);
	~DiscreteSlider();

	void addSection(const QString &label);
	void addSection(
		const TextWithEntities &label,
		const std::any &context = {});
	void setSections(const std::vector<QString> &labels);
	void setSections(
		const std::vector<TextWithEntities> &labels,
		const std::any &context = {});
	int activeSection() const {
		return _activeIndex;
	}
	void setActiveSection(int index);
	void setActiveSectionFast(int index);
	void finishAnimating();

	void setAdditionalContentWidthToSection(int index, int width);

	[[nodiscard]] rpl::producer<int> sectionActivated() const {
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
		Section(
			const TextWithEntities &label,
			const style::TextStyle &st,
			const std::any &context);

		Ui::Text::String label;
		std::unique_ptr<RippleAnimation> ripple;
		int left = 0;
		int width = 0;
		int contentWidth = 0;
	};
	struct Range {
		int left = 0;
		int width = 0;
	};

	[[nodiscard]] Range getFinalActiveRange() const;
	[[nodiscard]] Range getCurrentActiveRange() const;

	[[nodiscard]] int getSectionsCount() const {
		return _sections.size();
	}

	void enumerateSections(Fn<bool(Section&)> callback);
	void enumerateSections(Fn<bool(const Section&)> callback) const;

	virtual void startRipple(int sectionIndex) {
	}

	void stopAnimation() {
		_a_left.stop();
		_a_width.stop();
	}
	void refresh();

	void setSelectOnPress(bool selectOnPress);

	std::vector<Section> &sectionsRef();

private:
	void activateCallback();
	virtual const style::TextStyle &getLabelStyle() const = 0;
	virtual int getAnimationDuration() const = 0;

	int getIndexFromPosition(QPoint pos);
	void setSelectedSection(int index);

	std::vector<Section> _sections;
	int _activeIndex = 0;
	bool _selectOnPress = true;
	bool _snapToLabel = false;

	rpl::event_stream<int> _sectionActivated;

	int _pressed = -1;
	int _selected = 0;
	Ui::Animations::Simple _a_left;
	Ui::Animations::Simple _a_width;

	int _timerId = -1;
	crl::time _callbackAfterMs = 0;

};

class SettingsSlider : public DiscreteSlider {
public:
	SettingsSlider(
		QWidget *parent,
		const style::SettingsSlider &st = st::defaultSettingsSlider);

	[[nodiscard]] const style::SettingsSlider &st() const;

	[[nodiscard]] int centerOfSection(int section) const;
	virtual void fitWidthToSections();

	void setRippleTopRoundRadius(int radius);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

	void startRipple(int sectionIndex) override;

	std::vector<float64> countSectionsWidths(int newWidth) const;

private:
	const style::TextStyle &getLabelStyle() const override;
	int getAnimationDuration() const override;
	QImage prepareRippleMask(int sectionIndex, const Section &section);

	void resizeSections(int newWidth);

	const style::SettingsSlider &_st;
	std::optional<Ui::RoundRect> _bar;
	std::optional<Ui::RoundRect> _barActive;
	int _rippleTopRoundRadius = 0;


};

} // namespace Ui
