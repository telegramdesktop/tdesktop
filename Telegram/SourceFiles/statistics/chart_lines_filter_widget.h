/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Statistic {

class ChartLinesFilterWidget final : public Ui::RpWidget {
public:
	ChartLinesFilterWidget(not_null<Ui::RpWidget*> parent);

	void fillButtons(
		const std::vector<QString> &texts,
		const std::vector<QColor> &colors,
		const std::vector<int> &ids);

	void resizeToWidth(int outerWidth);

	struct Entry final {
		int id = 0;
		bool enabled = 0;
	};
	[[nodiscard]] rpl::producer<Entry> buttonEnabledChanges() const;

private:
	class FlatCheckbox;

	std::vector<base::unique_qptr<FlatCheckbox>> _buttons;

	rpl::event_stream<Entry> _buttonEnabledChanges;

};

} // namespace Statistic
