/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include <rpl/variable.h>
#include "ui/rp_widget.h"
#include "base/unique_qptr.h"

namespace style {
struct SearchFieldRow;
} // namespace style

namespace Ui {

class CrossButton;
class InputField;

class SearchFieldController {
public:
	base::unique_qptr<Ui::RpWidget> createView(
		QWidget *parent,
		const style::SearchFieldRow &st);

	rpl::producer<QString> queryValue() const {
		return _query.value();
	}

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	void setQueryFromField(const QString &query);
	void clearQuery();

	struct View {
		base::unique_qptr<Ui::RpWidget> wrap;
		Ui::InputField *field = nullptr;
		Ui::CrossButton *cancel = nullptr;
	};
	View _view;
	rpl::variable<QString> _query;

	rpl::lifetime _lifetime;

};

} // namespace Ui
