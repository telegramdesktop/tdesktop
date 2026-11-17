/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/output/export_output_abstract.h"
#include "ui/widgets/buttons.h"
#include "base/object_ptr.h"

namespace Export::View {

[[nodiscard]] object_ptr<Ui::RoundButton> CreateFormatButton(
	not_null<QWidget*> parent,
	rpl::producer<Output::Format> value,
	Fn<void(Output::Format)> changed);

[[nodiscard]] object_ptr<Ui::RpWidget> CreateFormatAndLocation(
	not_null<QWidget*> parent,
	rpl::producer<Output::Format> value,
	rpl::producer<TextWithEntities> text,
	Fn<void(Output::Format)> changed,
	Fn<void()> chooseFolder);

} // namespace Export::View
