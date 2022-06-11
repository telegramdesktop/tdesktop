/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/auto_lock_box.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/time_input.h"
#include "ui/ui_utility.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kCustom = std::numeric_limits<int>::max();
constexpr auto kOptions = { 60, 300, 3600, 18000, kCustom };
constexpr auto kDefaultCustom = "10:00"_cs;

auto TimeString(int seconds) {
	const auto hours = seconds / 3600;
	const auto minutes = (seconds - hours * 3600) / 60;
	return QString("%1:%2").arg(hours).arg(minutes, 2, 10, QLatin1Char('0'));
}

} // namespace

AutoLockBox::AutoLockBox(QWidget*) {
}

void AutoLockBox::prepare() {
	setTitle(tr::lng_passcode_autolock());

	addButton(tr::lng_box_ok(), [=] { closeBox(); });

	const auto currentTime = Core::App().settings().autoLock();

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		ranges::contains(kOptions, currentTime) ? currentTime : kCustom);

	const auto x = st::boxPadding.left() + st::boxOptionListPadding.left();
	auto y = st::boxOptionListPadding.top() + st::autolockButton.margin.top();
	const auto count = int(kOptions.size());
	_options.reserve(count);
	for (const auto seconds : kOptions) {
		const auto text = [&] {
			if (seconds == kCustom) {
				return QString();
			}
			const auto minutes = (seconds % 3600);
			return (minutes
				? tr::lng_minutes
				: tr::lng_hours)(
					tr::now,
					lt_count,
					minutes ? (seconds / 60) : (seconds / 3600));
		}();
		_options.emplace_back(
			this,
			group,
			seconds,
			text,
			st::autolockButton);
		_options.back()->moveToLeft(x, y);
		y += _options.back()->heightNoMargins() + st::boxOptionListSkip;
	}

	const auto timeInput = [&] {
		const auto &last = _options.back();
		const auto &st = st::autolockButton;

		const auto textLeft = st.checkPosition.x()
			+ last->checkRect().width()
			+ st.textPosition.x();
		const auto textTop = st.margin.top() + st.textPosition.y();

		const auto timeInput = Ui::CreateChild<Ui::TimeInput>(
			this,
			(group->value() == kCustom)
				? TimeString(currentTime)
				: kDefaultCustom.utf8(),
			st::autolockTimeField,
			st::autolockDateField,
			st::scheduleTimeSeparator,
			st::scheduleTimeSeparatorPadding);
		timeInput->resizeToWidth(st::autolockTimeWidth);
		timeInput->moveToLeft(last->x() + textLeft, last->y() + textTop);
		return timeInput;
	}();

	const auto collect = [=] {
		const auto timeValue = timeInput->valueCurrent().split(':');
		if (timeValue.size() != 2) {
			return 0;
		}
		return timeValue[0].toInt() * 3600 + timeValue[1].toInt() * 60;
	};

	timeInput->focuses(
	) | rpl::start_with_next([=] {
		group->setValue(kCustom);
	}, lifetime());

	group->setChangedCallback([=](int value) {
		if (value != kCustom) {
			durationChanged(value);
		} else {
			timeInput->setFocusFast();
		}
	});

	rpl::merge(
		boxClosing() | rpl::filter([=] { return group->value() == kCustom; }),
		timeInput->submitRequests()
	) | rpl::start_with_next([=] {
		if (const auto result = collect()) {
			durationChanged(result);
		} else {
			timeInput->showError();
		}
	}, lifetime());

	const auto timeInputBottom = timeInput->y() + timeInput->height();
	setDimensions(
		st::autolockWidth,
		st::boxOptionListPadding.top()
			+ (timeInputBottom - _options.back()->bottomNoMargins())
			+ count * _options.back()->heightNoMargins()
			+ (count - 1) * st::boxOptionListSkip
			+ st::boxOptionListPadding.bottom()
			+ st::boxPadding.bottom());
}

void AutoLockBox::durationChanged(int seconds) {
	if (Core::App().settings().autoLock() == seconds) {
		closeBox();
		return;
	}
	Core::App().settings().setAutoLock(seconds);
	Core::App().saveSettingsDelayed();

	Core::App().checkAutoLock(crl::now());
	closeBox();
}
