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
#include "ui/text/text.h"

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
		Text::MarkedContext context = {});
	void setSections(const std::vector<QString> &labels);
	void setSections(
		const std::vector<TextWithEntities> &labels,
		Text::MarkedContext context = {},
		Fn<bool()> paused = nullptr);
	int activeSection() const {
		return _activeIndex;
	}
	void setActiveSection(int index);
	void setActiveSectionFast(int index);
	void finishAnimating();
	void selectSection(int index);

	void setAdditionalContentWidthToSection(int index, int width);

	[[nodiscard]] rpl::producer<int> sectionActivated() const {
		return _sectionActivated.events();
	}

	// Fired when the screen reader browse position moves to a section, so
	// owners of a scrollable slider can bring that section into view.
	[[nodiscard]] rpl::producer<int> accessibilitySectionBrowsed() const;

	[[nodiscard]] int sectionsCount() const;
	[[nodiscard]] int lookupSectionLeft(int index) const;

	// Accessibility: a horizontal strip of painted tabs.
	QAccessible::Role accessibilityRole() override;
	bool accessibilitySelectionList() const override;
	Qt::FocusPolicy accessibilityFocusPolicy() override;
	std::optional<Qt::Orientation> accessibilityOrientation() const override;
	QAccessible::Role accessibilityChildRole() const override;
	QAccessible::State accessibilityChildState(int index) const override;
	int accessibilityChildCount() const override;
	QString accessibilityChildName(int index) const override;
	QRect accessibilityChildRect(int index) const override;
	bool accessibilityChildSupportsActions(int index) const override;
	quintptr accessibilityChildIdentity(int index) const override;
	int accessibilityChildIndexByIdentity(quintptr identity) const override;
	void accessibilityChildSetFocus(quintptr identity) override;
	void accessibilityChildActivate(quintptr identity) override;

protected:
	void timerEvent(QTimerEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void focusInEvent(QFocusEvent *e) override;

	// Activation coming from accessibility (keyboard or a screen reader
	// action); subclasses can intercept it (e.g. locked premium folders).
	virtual void activateSectionByAccessibility(int index);

	// The section the browse position is on, or -1 when it isn't on any -
	// a context menu opened from the keyboard has no position to look at.
	[[nodiscard]] int accessibilityBrowsedSection() const;

	int resizeGetHeight(int newWidth) override = 0;

	struct Section {
		Section(const QString &label, const style::TextStyle &st);
		Section(
			const TextWithEntities &label,
			const style::TextStyle &st,
			const Text::MarkedContext &context);

		Text::String label;
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

	[[nodiscard]] std::vector<Section> &sectionsRef();

	[[nodiscard]] bool paused() const;

private:
	enum class Announce {
		No,
		OnChange,
		Always,
	};

	void activateCallback();
	virtual const style::TextStyle &getLabelStyle() const = 0;
	virtual int getAnimationDuration() const = 0;

	int getIndexFromPosition(QPoint pos);
	void setSelectedSection(int index);
	void setAccessibilitySelected(int index, Announce announce);
	void browseAndActivate(int index);

	std::vector<Section> _sections;
	Fn<bool()> _paused;
	int _activeIndex = 0;
	bool _selectOnPress = true;
	bool _snapToLabel = false;

	rpl::event_stream<int> _sectionActivated;
	rpl::event_stream<int> _accessibilitySectionBrowsed;

	int _pressed = -1;
	int _selected = 0;
	int _accessibilitySelected = -1;
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
