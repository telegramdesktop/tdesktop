/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace style {
struct Checkbox;
struct ServiceCheck;
} // namespace style

namespace Ui {

class Checkbox;

[[nodiscard]] object_ptr<Checkbox> MakeChatServiceCheckbox(
	QWidget *parent,
	const QString &text,
	const style::Checkbox &st,
	const style::ServiceCheck &stCheck,
	bool checked,
	Fn<QColor()> bg = nullptr);

} // namespace Ui
