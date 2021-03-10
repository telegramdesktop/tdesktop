/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_group_menu.h"

#include "calls/calls_group_call.h"
#include "calls/calls_group_settings.h"
#include "calls/calls_group_panel.h"
#include "data/data_peer.h"
#include "data/data_group_call.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/layers/generic_box.h"
#include "lang/lang_keys.h"
#include "base/unixtime.h"
#include "base/timer_rpl.h"
#include "styles/style_calls.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Calls::Group {
namespace {

void EditGroupCallTitleBox(
		not_null<Ui::GenericBox*> box,
		const QString &placeholder,
		const QString &title,
		Fn<void(QString)> done) {
	box->setTitle(tr::lng_group_call_edit_title());
	const auto input = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::groupCallField,
		rpl::single(placeholder),
		title));
	box->setFocusCallback([=] {
		input->setFocusFast();
	});
	box->addButton(tr::lng_settings_save(), [=] {
		const auto result = input->getLastText().trimmed();
		box->closeBox();
		done(result);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void StartGroupCallRecordingBox(
		not_null<Ui::GenericBox*> box,
		const QString &title,
		Fn<void(QString)> done) {
	box->setTitle(tr::lng_group_call_recording_start());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box.get(),
			tr::lng_group_call_recording_start_sure(),
			st::groupCallBoxLabel));

	const auto input = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::groupCallField,
		tr::lng_group_call_recording_start_field(),
		title));
	box->setFocusCallback([=] {
		input->setFocusFast();
	});
	box->addButton(tr::lng_group_call_recording_start_button(), [=] {
		const auto result = input->getLastText().trimmed();
		if (result.isEmpty()) {
			input->showError();
			return;
		}
		box->closeBox();
		done(result);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void StopGroupCallRecordingBox(
		not_null<Ui::GenericBox*> box,
		Fn<void(QString)> done) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box.get(),
			tr::lng_group_call_recording_stop_sure(),
			st::groupCallBoxLabel),
		style::margins(
			st::boxRowPadding.left(),
			st::boxPadding.top(),
			st::boxRowPadding.right(),
			st::boxPadding.bottom()));

	box->addButton(tr::lng_box_ok(), [=] {
		box->closeBox();
		done(QString());
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

[[nodiscard]] auto ToDurationFrom(TimeId startDate) {
	return [=] {
		const auto now = base::unixtime::now();
		const auto elapsed = std::max(now - startDate, 0);
		const auto hours = elapsed / 3600;
		const auto minutes = (elapsed % 3600) / 60;
		const auto seconds = (elapsed % 60);
		return hours
			? QString("%1:%2:%3"
			).arg(hours
			).arg(minutes, 2, 10, QChar('0')
			).arg(seconds, 2, 10, QChar('0'))
			: QString("%1:%2"
			).arg(minutes
			).arg(seconds, 2, 10, QChar('0'));
	};
}

[[nodiscard]] rpl::producer<QString> ToRecordDuration(TimeId startDate) {
	return !startDate
		? (rpl::single(QString()) | rpl::type_erased())
		: rpl::single(
			rpl::empty_value()
		) | rpl::then(base::timer_each(
			crl::time(1000)
		)) | rpl::map(ToDurationFrom(startDate));
}

} // namespace

void LeaveBox(
		not_null<Ui::GenericBox*> box,
		not_null<GroupCall*> call,
		bool discardChecked,
		BoxContext context) {
	box->setTitle(tr::lng_group_call_leave_title());
	const auto inCall = (context == BoxContext::GroupCallPanel);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box.get(),
		tr::lng_group_call_leave_sure(),
		(inCall ? st::groupCallBoxLabel : st::boxLabel)));
	const auto discard = call->peer()->canManageGroupCall()
		? box->addRow(object_ptr<Ui::Checkbox>(
			box.get(),
			tr::lng_group_call_end(),
			discardChecked,
			(inCall ? st::groupCallCheckbox : st::defaultBoxCheckbox),
			(inCall ? st::groupCallCheck : st::defaultCheck)),
			style::margins(
				st::boxRowPadding.left(),
				st::boxRowPadding.left(),
				st::boxRowPadding.right(),
				st::boxRowPadding.bottom()))
		: nullptr;
	const auto weak = base::make_weak(call.get());
	box->addButton(tr::lng_group_call_leave(), [=] {
		const auto discardCall = (discard && discard->checked());
		box->closeBox();

		if (!weak) {
			return;
		} else if (discardCall) {
			call->discard();
		} else {
			call->hangup();
		}
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void ConfirmBox(
		not_null<Ui::GenericBox*> box,
		const QString &text,
		rpl::producer<QString> button,
		Fn<void()> callback) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box.get(),
			text,
			st::groupCallBoxLabel),
		st::boxPadding);
	box->addButton(std::move(button), callback);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void FillMenu(
		not_null<Ui::DropdownMenu*> menu,
		not_null<PeerData*> peer,
		not_null<GroupCall*> call,
		Fn<void()> chooseJoinAs,
		Fn<void(object_ptr<Ui::BoxContent>)> showBox) {
	const auto weak = base::make_weak(call.get());
	const auto resolveReal = [=] {
		const auto real = peer->groupCall();
		const auto strong = weak.get();
		return (real && strong && (real->id() == strong->id()))
			? real
			: nullptr;
	};
	const auto real = resolveReal();
	if (!real) {
		return;
	}

	const auto addEditJoinAs = call->showChooseJoinAs();
	const auto addEditTitle = peer->canManageGroupCall();
	const auto addEditRecording = peer->canManageGroupCall();
	if (addEditJoinAs) {
		menu->addAction(
			tr::lng_group_call_display_as_header(tr::now),
			chooseJoinAs);
		menu->addSeparator();
	}
	if (addEditTitle) {
		menu->addAction(tr::lng_group_call_edit_title(tr::now), [=] {
			const auto done = [=](const QString &title) {
				if (const auto strong = weak.get()) {
					strong->changeTitle(title);
				}
			};
			if (const auto real = resolveReal()) {
				showBox(Box(
					EditGroupCallTitleBox,
					peer->name,
					real->title(),
					done));
			}
		});
	}
	if (addEditRecording) {
		const auto label = (real->recordStartDate() != 0)
			? tr::lng_group_call_recording_stop(tr::now)
			: tr::lng_group_call_recording_start(tr::now);
		const auto action = menu->addAction(label, [=] {
			const auto real = resolveReal();
			if (!real) {
				return;
			}
			const auto recordStartDate = real->recordStartDate();
			const auto done = [=](QString title) {
				if (const auto strong = weak.get()) {
					strong->toggleRecording(!recordStartDate, title);
				}
			};
			if (recordStartDate) {
				showBox(Box(
					StopGroupCallRecordingBox,
					done));
			} else {
				showBox(Box(
					StartGroupCallRecordingBox,
					real->title(),
					done));
			}
		});
		rpl::combine(
			real->recordStartDateValue(),
			tr::lng_group_call_recording_stop(),
			tr::lng_group_call_recording_start()
		) | rpl::map([=](TimeId startDate, QString stop, QString start) {
			using namespace rpl::mappers;
			return startDate
				? ToRecordDuration(
					startDate
				) | rpl::map(stop + '\t' + _1) : rpl::single(start);
		}) | rpl::flatten_latest() | rpl::start_with_next([=](QString text) {
			action->setText(text);
		}, menu->lifetime());
	}
	menu->addAction(tr::lng_group_call_settings(tr::now), [=] {
		if (const auto strong = weak.get()) {
			showBox(Box(SettingsBox, strong));
		}
	});
	menu->addAction(tr::lng_group_call_end(tr::now), [=] {
		if (const auto strong = weak.get()) {
			showBox(Box(
				LeaveBox,
				strong,
				true,
				BoxContext::GroupCallPanel));
		}
	});

}

} // namespace Calls::Group
