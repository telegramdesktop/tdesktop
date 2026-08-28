/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/discrete_sliders.h"

#include "ui/effects/ripple_animation.h"
#include "ui/screen_reader_mode.h"
#include "styles/style_widgets.h"

namespace Ui {

DiscreteSlider::DiscreteSlider(QWidget *parent, bool snapToLabel)
: RpWidget(parent)
, _snapToLabel(snapToLabel) {
	setCursor(style::cur_pointer);
}

DiscreteSlider::~DiscreteSlider() = default;

void DiscreteSlider::setActiveSection(int index) {
	_activeIndex = index;
	activateCallback();
	setSelectedSection(index);
}

void DiscreteSlider::activateCallback() {
	if (_timerId >= 0) {
		killTimer(_timerId);
		_timerId = -1;
	}
	auto ms = crl::now();
	if (ms >= _callbackAfterMs) {
		_sectionActivated.fire_copy(_activeIndex);
	} else {
		_timerId = startTimer(_callbackAfterMs - ms, Qt::PreciseTimer);
	}
}

void DiscreteSlider::timerEvent(QTimerEvent *e) {
	activateCallback();
}

void DiscreteSlider::setActiveSectionFast(int index) {
	setActiveSection(index);
	finishAnimating();
}

void DiscreteSlider::finishAnimating() {
	_a_left.stop();
	_a_width.stop();
	update();
	_callbackAfterMs = 0;
	if (_timerId >= 0) {
		activateCallback();
	}
}

void DiscreteSlider::selectSection(int index) {
	if (index < 0 || index >= _sections.size()) {
		for (auto &other : _sections) {
			if (other.ripple) {
				other.ripple->lastStop();
			}
		}
		return;
	}
	const auto &section = _sections[index];
	if (section.ripple && !section.ripple->empty()) {
		return;
	}
	for (auto &other : _sections) {
		if (other.ripple) {
			other.ripple->lastStop();
		}
	}
	startRipple(index);
}

void DiscreteSlider::setAdditionalContentWidthToSection(int index, int w) {
	if (index >= 0 && index < _sections.size()) {
		auto &section = _sections[index];
		section.contentWidth = section.label.maxWidth() + w;
	}
}

rpl::producer<int> DiscreteSlider::accessibilitySectionBrowsed() const {
	return _accessibilitySectionBrowsed.events();
}

int DiscreteSlider::sectionsCount() const {
	return int(_sections.size());
}

int DiscreteSlider::lookupSectionLeft(int index) const {
	Expects(index >= 0 && index < _sections.size());

	return _sections[index].left;
}

void DiscreteSlider::setSelectOnPress(bool selectOnPress) {
	_selectOnPress = selectOnPress;
}

bool DiscreteSlider::paused() const {
	return _paused && _paused();
}

std::vector<DiscreteSlider::Section> &DiscreteSlider::sectionsRef() {
	return _sections;
}

void DiscreteSlider::addSection(const QString &label) {
	_sections.push_back(Section(label, getLabelStyle()));
	resizeToWidth(width());
}

void DiscreteSlider::addSection(
		const TextWithEntities &label,
		Text::MarkedContext context) {
	context.repaint = [this] { update(); };
	_sections.push_back(Section(label, getLabelStyle(), context));
	resizeToWidth(width());
}

void DiscreteSlider::setSections(const std::vector<QString> &labels) {
	Assert(!labels.empty());

	_sections.clear();
	for (const auto &label : labels) {
		_sections.push_back(Section(label, getLabelStyle()));
	}
	refresh();
}

void DiscreteSlider::setSections(
		const std::vector<TextWithEntities> &labels,
		Text::MarkedContext context,
		Fn<bool()> paused) {
	Assert(!labels.empty());

	context.repaint = [this] { update(); };

	_sections.clear();
	for (const auto &label : labels) {
		_sections.push_back(Section(label, getLabelStyle(), context));
	}
	_paused = std::move(paused);
	refresh();
}

void DiscreteSlider::refresh() {
	stopAnimation();
	if (_activeIndex >= _sections.size()) {
		_activeIndex = 0;
	}
	if (_selected >= _sections.size()) {
		_selected = 0;
	}
	if (_accessibilitySelected >= int(_sections.size())) {
		_accessibilitySelected = -1;
	}
	resizeToWidth(width());
	update();
}

DiscreteSlider::Range DiscreteSlider::getFinalActiveRange() const {
	const auto raw = (_sections.empty() || _selected < 0)
		? nullptr
		: &_sections[_selected];
	if (!raw) {
		return { 0, 0 };
	}
	const auto width = _snapToLabel
		? std::min(raw->width, raw->contentWidth)
		: raw->width;
	return { raw->left + ((raw->width - width) / 2), width };
}

DiscreteSlider::Range DiscreteSlider::getCurrentActiveRange() const {
	const auto to = getFinalActiveRange();
	return {
		int(base::SafeRound(_a_left.value(to.left))),
		int(base::SafeRound(_a_width.value(to.width))),
	};
}

void DiscreteSlider::enumerateSections(Fn<bool(Section&)> callback) {
	for (auto &section : _sections) {
		if (!callback(section)) {
			return;
		}
	}
}

void DiscreteSlider::enumerateSections(
		Fn<bool(const Section&)> callback) const {
	for (const auto &section : _sections) {
		if (!callback(section)) {
			return;
		}
	}
}

void DiscreteSlider::mousePressEvent(QMouseEvent *e) {
	const auto index = getIndexFromPosition(e->pos());
	if (_selectOnPress) {
		setSelectedSection(index);
	}
	startRipple(index);
	_pressed = index;
}

void DiscreteSlider::mouseMoveEvent(QMouseEvent *e) {
	if (_pressed < 0) {
		return;
	}
	if (_selectOnPress) {
		setSelectedSection(getIndexFromPosition(e->pos()));
	}
}

void DiscreteSlider::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed < 0) {
		return;
	}

	const auto index = getIndexFromPosition(e->pos());
	if (pressed < _sections.size()) {
		if (_sections[pressed].ripple) {
			_sections[pressed].ripple->lastStop();
		}
	}
	if (_selectOnPress || index == pressed) {
		setActiveSection(index);
	}
}

void DiscreteSlider::setSelectedSection(int index) {
	if (index >= int(_sections.size())) {
		return;
	}

	if (_selected != index) {
		const auto from = getFinalActiveRange();
		_selected = index;
		const auto to = getFinalActiveRange();
		const auto duration = getAnimationDuration();
		const auto updater = [this] { update(); };
		_a_left.start(updater, from.left, to.left, duration);
		_a_width.start(updater, from.width, to.width, duration);
		_callbackAfterMs = crl::now() + duration;
	}
}

int DiscreteSlider::getIndexFromPosition(QPoint pos) {
	const auto count = _sections.size();
	for (auto i = 0; i != count; ++i) {
		if (_sections[i].left + _sections[i].width > pos.x()) {
			return i;
		}
	}
	return count - 1;
}

void DiscreteSlider::focusInEvent(QFocusEvent *e) {
	// On real Tab traversal always land on the active section: the
	// accessibility SetFocus / Invoke actions leave a browse position behind,
	// and keeping it here would move Tab focus to whatever section was last
	// acted on instead. Those actions themselves come through with
	// OtherFocusReason (plain setFocus()) and must keep the position they
	// have just set.
	const auto count = int(_sections.size());
	const auto tab = (e->reason() == Qt::TabFocusReason)
		|| (e->reason() == Qt::BacktabFocusReason);
	if (count > 0 && (tab || _accessibilitySelected < 0)) {
		setAccessibilitySelected(
			std::clamp(_activeIndex, 0, count - 1),
			Announce::No);
	}
	// Taking focus raises a focus event of its own, which the platform hands
	// to focusChild() - the browsed section - so announcing it here as well
	// would read it twice.
	RpWidget::focusInEvent(e);
}

void DiscreteSlider::keyPressEvent(QKeyEvent *e) {
	const auto count = int(_sections.size());
	const auto key = e->key();
	// Sections are painted mirrored in RTL, so arrows move by visual order.
	const auto forward = style::RightToLeft() ? Qt::Key_Left : Qt::Key_Right;
	const auto backward = style::RightToLeft() ? Qt::Key_Right : Qt::Key_Left;
	if ((key == forward || key == backward) && count > 0) {
		const auto current = (_accessibilitySelected >= 0)
			? _accessibilitySelected
			: _activeIndex;
		const auto delta = (key == forward) ? 1 : -1;
		browseAndActivate(std::clamp(current + delta, 0, count - 1));
	} else if (key == Qt::Key_Home && count > 0) {
		browseAndActivate(0);
	} else if (key == Qt::Key_End && count > 0) {
		browseAndActivate(count - 1);
	} else {
		RpWidget::keyPressEvent(e);
	}
}

void DiscreteSlider::browseAndActivate(int index) {
	// A native Windows tab control switches to the tab the arrows land on
	// right away, with no separate Enter / Space step, and what the screen
	// reader announces is the focus moving onto that tab - which by then
	// already reports itself selected. So activate silently first, then
	// announce through the focus move.
	if (index != _activeIndex) {
		setActiveSection(index);
		accessibilityChildStateChanged(index, { .selected = true });
		setAccessibilitySelected(index, Announce::Always);
	} else {
		// Nothing to switch (e.g. the edge tab again) - just keep the browse
		// position in place, announcing only if it actually moved.
		setAccessibilitySelected(index, Announce::OnChange);
	}
}

void DiscreteSlider::activateSectionByAccessibility(int index) {
	const auto previous = _activeIndex;
	setActiveSection(index);
	accessibilityChildStateChanged(index, { .selected = true });
	// The state change alone stays silent on Windows, where the bridge reads
	// only the checked flag out of it - the selection events below are the
	// ones a screen reader hears, the way SideBarButton announces the active
	// folder of the vertical strip.
	if (previous != index
		&& previous >= 0
		&& previous < int(_sections.size())) {
		accessibilityChildSelectionChanged(previous);
	}
	accessibilityChildSelectionChanged(index);
}

int DiscreteSlider::accessibilityBrowsedSection() const {
	return (_accessibilitySelected >= 0
		&& _accessibilitySelected < int(_sections.size()))
		? _accessibilitySelected
		: -1;
}

void DiscreteSlider::setAccessibilitySelected(int index, Announce announce) {
	if (index >= int(_sections.size())) {
		return;
	}
	const auto changed = (_accessibilitySelected != index);
	_accessibilitySelected = index;
	if (changed && index >= 0) {
		_accessibilitySectionBrowsed.fire_copy(index);
	}
	const auto shouldAnnounce = (announce == Announce::Always)
		|| (announce == Announce::OnChange && changed);
	if (shouldAnnounce && index >= 0) {
		accessibilityChildFocused(index);
	}
}

QAccessible::Role DiscreteSlider::accessibilityRole() {
	return QAccessible::PageTabList;
}

bool DiscreteSlider::accessibilitySelectionList() const {
	// Grants the sections the platform selection item pattern, which is what
	// a screen reader reads "selected" / "not selected" from while browsing.
	return true;
}

Qt::FocusPolicy DiscreteSlider::accessibilityFocusPolicy() {
	return Qt::TabFocus;
}

std::optional<Qt::Orientation> DiscreteSlider::accessibilityOrientation() const {
	return Qt::Horizontal;
}

QAccessible::Role DiscreteSlider::accessibilityChildRole() const {
	return QAccessible::PageTab;
}

QAccessible::State DiscreteSlider::accessibilityChildState(int index) const {
	QAccessible::State state;
	state.selectable = true;
	if (ScreenReaderModeActive()) {
		state.focusable = true;
	}
	if (index == _activeIndex) {
		state.selected = true;
	}
	if (index == _accessibilitySelected) {
		state.active = true;
		if (hasFocus()) {
			state.focused = true;
		}
	}
	return state;
}

int DiscreteSlider::accessibilityChildCount() const {
	return int(_sections.size());
}

QString DiscreteSlider::accessibilityChildName(int index) const {
	if (index < 0 || index >= int(_sections.size())) {
		return {};
	}
	return _sections[index].label.toString();
}

QRect DiscreteSlider::accessibilityChildRect(int index) const {
	if (index < 0 || index >= int(_sections.size())) {
		return {};
	}
	const auto &section = _sections[index];
	return myrtlrect(section.left, 0, section.width, height());
}

bool DiscreteSlider::accessibilityChildSupportsActions(int index) const {
	// Every section can be focused and activated, and each has a stable
	// identity below. Tying the opt-in to a valid identity keeps the action
	// interface off invalid indices.
	return accessibilityChildIdentity(index) != 0;
}

quintptr DiscreteSlider::accessibilityChildIdentity(int index) const {
	// The sections are rebuilt wholesale by setSections(), so an index is
	// not stable by the time a queued action runs. The label text names a
	// section within one slider; hash collisions (or duplicate labels) are
	// possible but acceptable, same as in the language and country boxes.
	// Shift instead of masking so that small hash values keep their
	// distinguishing low bits; the tag bit keeps the token non-zero.
	if (index < 0 || index >= int(_sections.size())) {
		return 0;
	}
	const auto value = quintptr(qHash(_sections[index].label.toString()));
	return value ? ((value << 3) | quintptr(1)) : quintptr(0);
}

int DiscreteSlider::accessibilityChildIndexByIdentity(
		quintptr identity) const {
	if (!identity) {
		return -1;
	}
	const auto count = accessibilityChildCount();
	for (auto i = 0; i != count; ++i) {
		if (accessibilityChildIdentity(i) == identity) {
			return i;
		}
	}
	return -1;
}

void DiscreteSlider::accessibilityChildSetFocus(quintptr identity) {
	// UIA invokes provider actions (SetFocus) on a background thread, so hop
	// to the main thread before touching any widget state. Resolve the stable
	// identity to its current index here (not on the background thread) so a
	// sections rebuild does not move focus to another section.
	crl::on_main(this, [=] {
		const auto index = accessibilityChildIndexByIdentity(identity);
		if (index < 0) {
			return;
		}
		// The sections are virtual (no real QWidget), so the screen reader's
		// SetFocus can't move real keyboard focus to a section. Translate it
		// into our browse position, then either announce it directly or grab
		// keyboard focus (taking it announces the section by itself).
		setAccessibilitySelected(
			index,
			hasFocus() ? Announce::Always : Announce::No);
		if (!hasFocus()) {
			setFocus();
		}
	});
}

void DiscreteSlider::accessibilityChildActivate(quintptr identity) {
	// UIA invokes the press action on a background thread too; resolve the
	// identity, move the browse position and activate the section on the
	// main thread. Take focus onto the section first (as the SetFocus action
	// does), so the "selected" announcement is heard from Invoke as well as
	// from Space.
	crl::on_main(this, [=] {
		const auto index = accessibilityChildIndexByIdentity(identity);
		if (index < 0) {
			return;
		}
		setAccessibilitySelected(
			index,
			hasFocus() ? Announce::OnChange : Announce::No);
		if (!hasFocus()) {
			setFocus();
		}
		activateSectionByAccessibility(index);
	});
}

DiscreteSlider::Section::Section(
	const QString &label,
	const style::TextStyle &st)
: label(st, label)
, contentWidth(Section::label.maxWidth()) {
}

DiscreteSlider::Section::Section(
		const TextWithEntities &label,
		const style::TextStyle &st,
		const Text::MarkedContext &context) {
	this->label.setMarkedText(st, label, kMarkupTextOptions, context);
	contentWidth = Section::label.maxWidth();
}

SettingsSlider::SettingsSlider(
	QWidget *parent,
	const style::SettingsSlider &st)
: DiscreteSlider(parent, st.barSnapToLabel)
, _st(st) {
	if (_st.barRadius > 0) {
		_bar.emplace(_st.barRadius, _st.barFg);
		_barActive.emplace(_st.barRadius, _st.barFgActive);
	}
	setSelectOnPress(_st.ripple.showDuration == 0);
}

const style::SettingsSlider &SettingsSlider::st() const {
	return _st;
}

int SettingsSlider::centerOfSection(int section) const {
	const auto widths = countSectionsWidths(0);
	auto result = 0;
	if (section >= 0 && section < widths.size()) {
		for (auto i = 0; i < section; i++) {
			result += widths[i];
		}
		result += widths[section] / 2;
	}
	return result;
}

void SettingsSlider::fitWidthToSections() {
	const auto widths = countSectionsWidths(0);
	resizeToWidth(ranges::accumulate(widths, .0) + _st.padding * 2);
}

void SettingsSlider::setRippleTopRoundRadius(int radius) {
	_rippleTopRoundRadius = radius;
}

const style::TextStyle &SettingsSlider::getLabelStyle() const {
	return _st.labelStyle;
}

int SettingsSlider::getAnimationDuration() const {
	return _st.duration;
}

void SettingsSlider::resizeSections(int newWidth) {
	const auto count = getSectionsCount();
	if (!count) {
		return;
	}

	const auto sectionWidths = countSectionsWidths(newWidth);

	auto skip = 0;
	auto x = _st.padding * 1.;
	auto sectionWidth = sectionWidths.begin();
	enumerateSections([&](Section &section) {
		Expects(sectionWidth != sectionWidths.end());

		section.left = std::floor(x) + skip;
		x += *sectionWidth;
		section.width = qRound(x) - (section.left - skip);
		skip += _st.barSkip;
		++sectionWidth;
		return true;
	});
	stopAnimation();
}

std::vector<float64> SettingsSlider::countSectionsWidths(int newWidth) const {
	const auto count = getSectionsCount();
	const auto sectionsWidth = newWidth
		- 2 * _st.padding
		- (count - 1) * _st.barSkip;
	const auto sectionWidth = sectionsWidth / float64(count);

	auto result = std::vector<float64>(count, sectionWidth);
	auto labelsWidth = 0;
	auto commonWidth = true;
	enumerateSections([&](const Section &section) {
		labelsWidth += section.contentWidth;
		if (section.contentWidth >= sectionWidth) {
			commonWidth = false;
		}
		return true;
	});
	// If labelsWidth > sectionsWidth we're screwed anyway.
	if (_st.strictSkip || (!commonWidth && labelsWidth <= sectionsWidth)) {
		auto padding = _st.strictSkip
			? (_st.strictSkip / 2.)
			: (sectionsWidth - labelsWidth) / (2. * count);
		auto currentWidth = result.begin();
		enumerateSections([&](const Section &section) {
			Expects(currentWidth != result.end());

			*currentWidth = padding + section.contentWidth + padding;
			++currentWidth;
			return true;
		});
	}
	return result;
}

int SettingsSlider::resizeGetHeight(int newWidth) {
	resizeSections(newWidth);
	return _st.height;
}

void SettingsSlider::startRipple(int sectionIndex) {
	if (!_st.ripple.showDuration) {
		return;
	}
	auto index = 0;
	enumerateSections([this, &index, sectionIndex](Section &section) {
		if (index++ == sectionIndex) {
			if (!section.ripple) {
				auto mask = prepareRippleMask(sectionIndex, section);
				section.ripple = std::make_unique<RippleAnimation>(
					_st.ripple,
					std::move(mask),
					[this] { update(); });
			}
			const auto point = mapFromGlobal(QCursor::pos());
			section.ripple->add(point - QPoint(section.left, 0));
			return false;
		}
		return true;
	});
}

QImage SettingsSlider::prepareRippleMask(
		int sectionIndex,
		const Section &section) {
	const auto size = QSize(section.width, height() - _st.rippleBottomSkip);
	if (!_rippleTopRoundRadius
		|| (sectionIndex > 0 && sectionIndex + 1 < getSectionsCount())) {
		return RippleAnimation::RectMask(size);
	}
	return RippleAnimation::MaskByDrawer(size, false, [&](QPainter &p) {
		const auto plusRadius = _rippleTopRoundRadius + 1;
		p.drawRoundedRect(
			0,
			0,
			section.width,
			height() + plusRadius,
			_rippleTopRoundRadius,
			_rippleTopRoundRadius);
		if (sectionIndex > 0) {
			p.fillRect(0, 0, plusRadius, plusRadius, p.brush());
		}
		if (sectionIndex + 1 < getSectionsCount()) {
			p.fillRect(
				section.width - plusRadius,
				0,
				plusRadius,
				plusRadius, p.brush());
		}
	});
}

void SettingsSlider::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto clip = e->rect();
	const auto range = DiscreteSlider::getCurrentActiveRange();

	const auto drawRect = [&](QRect rect, bool active = false) {
		const auto &bar = active ? _barActive : _bar;
		if (bar) {
			bar->paint(p, rect);
		} else {
			p.fillRect(rect, active ? _st.barFgActive : _st.barFg);
		}
	};
	enumerateSections([&](Section &section) {
		const auto activeWidth = _st.barSnapToLabel
			? section.contentWidth
			: section.width;
		const auto activeLeft = section.left
			+ (section.width - activeWidth) / 2;
		const auto divider = std::max(std::min(activeWidth, range.width), 1);
		const auto active = 1.
			- std::clamp(
				std::abs(range.left - activeLeft) / float64(divider),
				0.,
				1.);
		if (section.ripple) {
			const auto color = anim::color(
				_st.rippleBg,
				_st.rippleBgActive,
				active);
			section.ripple->paint(p, section.left, 0, width(), &color);
			if (section.ripple->empty()) {
				section.ripple.reset();
			}
		}
		if (!_st.barSnapToLabel) {
			auto from = activeLeft;
			auto tofill = activeWidth;
			if (range.left > from) {
				const auto fill = std::min(tofill, range.left - from);
				drawRect(myrtlrect(from, _st.barTop, fill, _st.barStroke));
				from += fill;
				tofill -= fill;
			}
			if (range.left + activeWidth > from) {
				const auto fill = std::min(
					tofill,
					range.left + activeWidth - from);
				if (fill) {
					drawRect(
						myrtlrect(from, _st.barTop, fill, _st.barStroke),
						true);
					from += fill;
					tofill -= fill;
				}
			}
			if (tofill) {
				drawRect(myrtlrect(from, _st.barTop, tofill, _st.barStroke));
			}
		}
		const auto labelLeft = section.left
			+ (section.width - section.contentWidth) / 2;
		const auto rect = myrtlrect(
			labelLeft,
			_st.labelTop,
			section.contentWidth,
			_st.labelStyle.font->height);
		if (rect.intersects(clip)) {
			p.setPen(anim::pen(_st.labelFg, _st.labelFgActive, active));
			section.label.draw(p, {
				.position = QPoint(labelLeft, _st.labelTop),
				.outerWidth = width(),
				.availableWidth = section.label.maxWidth(),
				.paused = paused(),
			});
		}
		return true;
	});
	if (_st.barSnapToLabel) {
		const auto add = _st.barStroke / 2;
		const auto from = std::max(range.left - add, 0);
		const auto till = std::min(range.left + range.width + add, width());
		if (from < till) {
			drawRect(
				myrtlrect(from, _st.barTop, till - from, _st.barStroke),
				true);
		}
	}
}

} // namespace Ui
