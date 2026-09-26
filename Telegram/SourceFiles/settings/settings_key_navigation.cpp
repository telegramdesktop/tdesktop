/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_key_navigation.h"

#include "base/invoke_queued.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"

#include "styles/style_settings.h"

namespace Settings {
namespace {

constexpr auto kPageSkip = 5;

[[nodiscard]] bool SameBand(const QRect &a, const QRect &b) {
	const auto delta = std::abs(rect::center(a).y() - rect::center(b).y());
	return (delta * 2) < std::min(a.height(), b.height());
}

} // namespace

KeyNavigation::KeyNavigation(not_null<Ui::RpWidget*> inner)
: _inner(inner) {
}

void KeyNavigation::anchorTo(not_null<QWidget*> widget) {
	const auto raw = widget.get();
	const auto entries = list();
	for (auto i = 0; i != int(entries.size()); ++i) {
		if (entries[i].widget.get() == raw) {
			select(entries, i);
			return;
		}
	}
	_anchor = raw;
}

auto KeyNavigation::list() const -> std::vector<Entry> {
	auto result = std::vector<Entry>();
	const auto widgets = _inner->findChildren<QWidget*>();
	for (const auto widget : widgets) {
		auto fullWidth = true;
		auto ownBackground = false;
		const auto button = [&]() -> Ui::AbstractButton* {
			if (const auto row = dynamic_cast<Ui::SettingsButton*>(widget)) {
				ownBackground = true;
				return row;
			} else if (const auto check = dynamic_cast<Ui::Checkbox*>(widget)) {
				return check;
			}
			fullWidth = false;
			if (const auto link = dynamic_cast<Ui::LinkButton*>(widget)) {
				return link;
			}
			const auto abstract = dynamic_cast<Ui::AbstractButton*>(widget);
			return (abstract && abstract->isListItem()) ? abstract : nullptr;
		}();
		const auto slider = button
			? nullptr
			: dynamic_cast<Ui::ContinuousSlider*>(widget);
		if (slider) {
			fullWidth = true;
		}
		const auto raw = button
			? static_cast<Ui::RpWidget*>(button)
			: static_cast<Ui::RpWidget*>(slider);
		if (!raw
			|| !raw->isVisibleTo(_inner)
			|| (button ? button->isDisabled() : slider->isDisabled())) {
			continue;
		}
		const auto geometry = QRect(
			raw->mapTo(_inner, QPoint()),
			raw->size());
		if (!geometry.isEmpty()) {
			result.push_back({
				raw,
				button,
				slider,
				geometry,
				fullWidth,
				ownBackground,
			});
		}
	}
	ranges::sort(result, ranges::less(), [](const Entry &entry) {
		return std::pair(entry.geometry.y(), entry.geometry.x());
	});
	return result;
}

bool KeyNavigation::handle(not_null<QKeyEvent*> e) {
	const auto key = e->key();
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	const auto navigationKey = (key == Qt::Key_Up)
		|| (key == Qt::Key_Down)
		|| (key == Qt::Key_PageUp)
		|| (key == Qt::Key_PageDown);
	const auto horizontalKey = (key == Qt::Key_Left)
		|| (key == Qt::Key_Right);
	const auto submitKey = (key == Qt::Key_Return)
		|| (key == Qt::Key_Enter)
		|| (key == Qt::Key_Space);
	if ((modifiers != Qt::NoModifier)
		|| (!navigationKey && !horizontalKey && !submitKey)) {
		return false;
	}
	const auto entries = list();
	if (entries.empty()) {
		return false;
	}
	const auto selectedWidget = _selected.get();
	auto selected = -1;
	auto hovered = -1;
	for (auto i = 0; i != int(entries.size()); ++i) {
		if (entries[i].widget == selectedWidget) {
			selected = i;
		} else if ((hovered < 0) && entries[i].widget->underMouse()) {
			hovered = i;
		}
	}
	if (submitKey) {
		if (selected < 0) {
			return false;
		} else if (const auto button = entries[selected].button) {
			if (!e->isAutoRepeat()) {
				activate(button, e->modifiers());
			}
		}
		return true;
	}
	const auto count = int(entries.size());
	const auto anchorIndex = [&] {
		const auto anchor = _anchor.get();
		if (!anchor || !anchor->isVisibleTo(_inner)) {
			return -1;
		}
		const auto center = rect::center(QRect(
			anchor->mapTo(_inner, QPoint()),
			anchor->size())).y();
		for (auto i = 0; i != count; ++i) {
			if (rect::center(entries[i].geometry).y() >= center) {
				return i - 1;
			}
		}
		return count - 1;
	};
	const auto start = (selected >= 0)
		? selected
		: (hovered >= 0)
		? hovered
		: anchorIndex();
	if (horizontalKey) {
		if (start < 0) {
			return false;
		}
		const auto &current = entries[start];
		if (current.slider) {
			QCoreApplication::sendEvent(current.slider, e);
			return true;
		}
		const auto next = start + ((key == Qt::Key_Right) ? 1 : -1);
		if (next >= 0
			&& next < count
			&& SameBand(current.geometry, entries[next].geometry)) {
			select(entries, next);
		}
		return true;
	}
	const auto visible = _inner->visibleRegion().boundingRect();
	const auto firstVisible = [&] {
		for (auto i = 0; i != count; ++i) {
			if (rect::bottom(entries[i].geometry) > visible.y()) {
				return i;
			}
		}
		return 0;
	};
	const auto lastVisible = [&] {
		for (auto i = count - 1; i >= 0; --i) {
			if (entries[i].geometry.y() < rect::bottom(visible)) {
				return i;
			}
		}
		return count - 1;
	};
	const auto bandStart = [&](int index) {
		while (index > 0
			&& SameBand(
				entries[index - 1].geometry,
				entries[index].geometry)) {
			--index;
		}
		return index;
	};
	if (key == Qt::Key_Down) {
		if (start < 0) {
			select(entries, firstVisible());
		} else {
			auto next = start + 1;
			while (next < count
				&& SameBand(
					entries[next].geometry,
					entries[start].geometry)) {
				++next;
			}
			select(entries, (next < count) ? next : 0);
		}
	} else if (key == Qt::Key_Up) {
		if (start < 0) {
			select(entries, bandStart(lastVisible()));
		} else {
			auto prev = start - 1;
			while (prev >= 0
				&& SameBand(
					entries[prev].geometry,
					entries[start].geometry)) {
				--prev;
			}
			select(entries, bandStart((prev >= 0) ? prev : (count - 1)));
		}
	} else if ((key == Qt::Key_PageDown) || (key == Qt::Key_PageUp)) {
		if (start < 0) {
			return false;
		}
		const auto delta = (key == Qt::Key_PageDown)
			? kPageSkip
			: -kPageSkip;
		select(entries, start + delta);
	}
	return true;
}

void KeyNavigation::activate(
		not_null<Ui::AbstractButton*> button,
		Qt::KeyboardModifiers modifiers) {
	if (const auto checkbox = dynamic_cast<Ui::Checkbox*>(button.get())) {
		const auto radio = dynamic_cast<Ui::Radiobutton*>(button.get());
		if (!radio) {
			checkbox->setChecked(!checkbox->checked());
		} else if (!checkbox->checked()) {
			checkbox->setChecked(true);
		}
	}
	button->clicked(modifiers, Qt::LeftButton);
}

void KeyNavigation::select(const std::vector<Entry> &entries, int index) {
	if (entries.empty()) {
		return;
	}
	index = std::clamp(index, 0, int(entries.size()) - 1);
	const auto widget = entries[index].widget;
	_anchor = nullptr;
	if (_selected.get() == widget.get()) {
		return;
	}
	for (const auto &entry : entries) {
		track(entry.widget);
	}
	const auto button = entries[index].button;
	_selectedGeometry = entries[index].geometry;
	_selectedLifetime.destroy();
	widget->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		const auto type = e->type();
		return (type == QEvent::Hide) || (type == QEvent::HideToParent);
	}) | rpl::on_next([=] {
		InvokeQueued(_inner, [=] {
			reselectFromHidden();
		});
	}, _selectedLifetime);
	const auto apply = [&] {
		for (const auto &entry : entries) {
			const auto other = entry.button;
			if (other && other != button && other->isOver()) {
				other->setSynteticOver(false);
			}
		}
		_selected = widget;
		if (button) {
			button->setSynteticOver(true);
		}
		updateHighlight();
	};
	apply();
	RevealWidget(widget, st::settingsNavigationMargin);
	apply();
}

void KeyNavigation::reselectFromHidden() {
	const auto widget = _selected.get();
	if (!widget) {
		return;
	} else if (widget->isVisibleTo(_inner)) {
		updateHighlight();
		return;
	}
	const auto center = rect::center(_selectedGeometry).y();
	clearSelection();
	const auto entries = list();
	if (entries.empty()) {
		return;
	}
	auto index = int(entries.size()) - 1;
	for (auto i = 0; i != int(entries.size()); ++i) {
		if (rect::center(entries[i].geometry).y() >= center) {
			index = i;
			break;
		}
	}
	select(entries, index);
}

void KeyNavigation::updateHighlight() {
	const auto selected = _selected.get();
	if (!selected) {
		if (_highlight) {
			_highlight->hide();
		}
		return;
	}
	if (!_highlight) {
		_highlight = Ui::CreateChild<Ui::RpWidget>(_inner.get());
		const auto raw = _highlight;
		raw->setAttribute(Qt::WA_TransparentForMouseEvents);
		raw->paintRequest(
		) | rpl::on_next([=](QRect clip) {
			auto p = QPainter(raw);
			if (_highlightRounded) {
				auto hq = PainterHighQualityEnabler(p);
				const auto radius = st::roundRadiusLarge;
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBgOver);
				p.drawRoundedRect(raw->rect(), radius, radius);
			} else {
				p.fillRect(clip, st::windowBgOver);
			}
		}, raw->lifetime());
		raw->lower();
		_inner->sizeValue(
		) | rpl::on_next([=] {
			updateHighlight();
		}, _lifetime);
	}
	const auto entries = list();
	const auto i = ranges::find(entries, selected, [](const Entry &entry) {
		return entry.widget.get();
	});
	if (i == end(entries) || i->ownBackground) {
		_highlight->hide();
		return;
	}
	const auto &geometry = i->geometry;
	const auto banded = ranges::any_of(entries, [&](const Entry &entry) {
		return (entry.widget.get() != selected)
			&& SameBand(entry.geometry, geometry);
	});
	const auto skip = st::settingsNavigationBandSkip;
	_highlightRounded = banded || !i->fullWidth;
	_highlight->setGeometry(_highlightRounded
		? geometry.marginsAdded(Margins(skip))
		: QRect(0, geometry.y(), _inner->width(), geometry.height()));
	_highlight->update();
	_highlight->show();
}

void KeyNavigation::clearSelection() {
	if (const auto widget = _selected.get()) {
		if (const auto button = dynamic_cast<Ui::AbstractButton*>(widget)) {
			button->setSynteticOver(false);
		}
	}
	_selected = nullptr;
	_selectedLifetime.destroy();
	if (_highlight) {
		_highlight->hide();
	}
}

void KeyNavigation::track(not_null<Ui::RpWidget*> widget) {
	if (!_tracked.emplace(widget).second) {
		return;
	}
	widget->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::on_next([=] {
		if (_selected.get()) {
			clearSelection();
		}
	}, _lifetime);
}

} // namespace Settings
