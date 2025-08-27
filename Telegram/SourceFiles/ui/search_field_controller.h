/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/variable.h>
#include "ui/rp_widget.h"
#include "base/unique_qptr.h"

namespace style {
struct SearchFieldRow;
struct InputField;
} // namespace style

namespace Ui {

class CrossButton;
class InputField;

class SearchFieldController {
public:
	SearchFieldController(const QString &query);

	base::unique_qptr<Ui::InputField> createField(
		QWidget *parent,
		const style::InputField &st);
	struct RowView {
		base::unique_qptr<Ui::RpWidget> wrap;
		QPointer<Ui::InputField> field;
	};
	RowView createRowView(
		QWidget *parent,
		const style::SearchFieldRow &st);

	QString query() const;
	rpl::producer<QString> queryValue() const;
	rpl::producer<QString> queryChanges() const;

	void setQuery(const QString &query);

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	base::unique_qptr<QWidget> _view;
	rpl::variable<QString> _query;

	rpl::lifetime _lifetime;

};

} // namespace Ui
