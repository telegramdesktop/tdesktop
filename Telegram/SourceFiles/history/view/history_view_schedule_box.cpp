/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_schedule_box.h"

#include "api/api_common.h"
#include "lang/lang_keys.h"
#include "base/unixtime.h"
#include "boxes/calendar_box.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "styles/style_history.h"

namespace HistoryView {

void ScheduleBox(
		not_null<GenericBox*> box,
		Fn<void(Api::SendOptions)> done) {
	box->setTitle(tr::lng_schedule_title());

	const auto content = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, st::scheduleHeight));
	const auto day = Ui::CreateChild<Ui::InputField>(
		content,
		st::scheduleDateField);
	const auto time = Ui::CreateChild<Ui::InputField>(
		content,
		st::scheduleDateField);
	const auto at = Ui::CreateChild<Ui::FlatLabel>(
		content,
		tr::lng_schedule_at(),
		st::scheduleAtLabel);

	content->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto paddings = width
			- at->width()
			- 2 * st::scheduleAtSkip
			- 2 * st::scheduleDateWidth;
		const auto left = paddings / 2;
		day->resizeToWidth(st::scheduleDateWidth);
		day->moveToLeft(left, st::scheduleDateTop, width);
		at->moveToLeft(
			left + st::scheduleDateWidth + st::scheduleAtSkip,
			st::scheduleAtTop,
			width);
		time->resizeToWidth(st::scheduleDateWidth);
		time->moveToLeft(
			width - left - st::scheduleDateWidth,
			st::scheduleDateTop,
			width);
	}, content->lifetime());

	const auto save = [=] {
		auto result = Api::SendOptions();

		const auto dayValue = day->getLastText().trimmed();
		const auto dayMatch = QRegularExpression("(\\d\\d)\\.(\\d\\d)\\.(\\d\\d\\d\\d)").match(dayValue);
		const auto timeValue = time->getLastText().trimmed();
		const auto timeMatch = QRegularExpression("(\\d\\d):(\\d\\d)").match(timeValue);

		if (!dayMatch.hasMatch()) {
			day->showError();
			return;
		}

		if (!timeMatch.hasMatch()) {
			time->showError();
			return;
		}

		const auto date = QDateTime(
			QDate(
				dayMatch.captured(3).toInt(),
				dayMatch.captured(2).toInt(),
				dayMatch.captured(1).toInt()),
			QTime(
				timeMatch.captured(1).toInt(),
				timeMatch.captured(2).toInt()));
		result.scheduled = date.toTime_t();

		auto copy = done;
		box->closeBox();
		copy(result);
	};

	box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace HistoryView
