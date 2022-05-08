/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/choose_time.h"

#include "base/qt_signal_producer.h"
#include "ui/ui_utility.h"
#include "ui/widgets/fields/time_part_input_with_placeholder.h"
#include "ui/wrap/padding_wrap.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace Ui {

ChooseTimeResult ChooseTimeWidget(
		not_null<RpWidget*> parent,
		TimeId startSeconds) {
	using TimeField = Ui::TimePartWithPlaceholder;
	const auto putNext = [](not_null<TimeField*> field, QChar ch) {
		field->setCursorPosition(0);
		if (ch.unicode()) {
			field->setText(ch + field->getLastText());
			field->setCursorPosition(1);
		}
		field->onTextEdited();
		field->setFocus();
	};

	const auto erasePrevious = [](not_null<TimeField*> field) {
		const auto text = field->getLastText();
		if (!text.isEmpty()) {
			field->setCursorPosition(text.size() - 1);
			field->setText(text.mid(0, text.size() - 1));
		}
		field->setFocus();
	};

	struct State {
		not_null<TimeField*> day;
		not_null<TimeField*> hour;
		not_null<TimeField*> minute;

		rpl::variable<int> valueInSeconds = 0;
	};

	auto content = object_ptr<Ui::FixedHeightWidget>(
		parent,
		st::scheduleHeight);

	const auto startDays = startSeconds / 86400;
	startSeconds -= startDays * 86400;
	const auto startHours = startSeconds / 3600;
	startSeconds -= startHours * 3600;
	const auto startMinutes = startSeconds / 60;

	const auto state = content->lifetime().make_state<State>(State{
		.day = Ui::CreateChild<TimeField>(
			content.data(),
			st::muteBoxTimeField,
			rpl::never<QString>(),
			QString::number(startDays)),
		.hour = Ui::CreateChild<TimeField>(
			content.data(),
			st::muteBoxTimeField,
			rpl::never<QString>(),
			QString::number(startHours)),
		.minute = Ui::CreateChild<TimeField>(
			content.data(),
			st::muteBoxTimeField,
			rpl::never<QString>(),
			QString::number(startMinutes)),
	});

	const auto day = Ui::MakeWeak(state->day);
	const auto hour = Ui::MakeWeak(state->hour);
	const auto minute = Ui::MakeWeak(state->minute);

	day->setPhrase(tr::lng_days);
	day->setMaxValue(31);
	day->setWheelStep(1);
	day->putNext() | rpl::start_with_next([=](QChar ch) {
		putNext(hour, ch);
	}, content->lifetime());

	hour->setPhrase(tr::lng_hours);
	hour->setMaxValue(23);
	hour->setWheelStep(1);
	hour->putNext() | rpl::start_with_next([=](QChar ch) {
		putNext(minute, ch);
	}, content->lifetime());
	hour->erasePrevious() | rpl::start_with_next([=] {
		erasePrevious(day);
	}, content->lifetime());

	minute->setPhrase(tr::lng_minutes);
	minute->setMaxValue(59);
	minute->setWheelStep(10);
	minute->erasePrevious() | rpl::start_with_next([=] {
		erasePrevious(hour);
	}, content->lifetime());

	content->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto inputWidth = s.width() / 3;
		auto rect = QRect(
			0,
			(s.height() - day->height()) / 2,
			inputWidth,
			day->height());
		for (const auto &input : { day, hour, minute }) {
			input->setGeometry(rect - st::muteBoxTimeFieldPadding);
			rect.translate(inputWidth, 0);
		}
	}, content->lifetime());

	rpl::merge(
		rpl::single(rpl::empty),
		base::qt_signal_producer(day.data(), &MaskedInputField::changed),
		base::qt_signal_producer(hour.data(), &MaskedInputField::changed),
		base::qt_signal_producer(minute.data(), &MaskedInputField::changed)
	) | rpl::start_with_next([=] {
		state->valueInSeconds = 0
			+ day->getLastText().toUInt() * 3600 * 24
			+ hour->getLastText().toUInt() * 3600
			+ minute->getLastText().toUInt() * 60;
	}, content->lifetime());
	return {
		object_ptr<Ui::RpWidget>::fromRaw(content.release()),
		state->valueInSeconds.value(),
	};
}

} // namespace Ui
