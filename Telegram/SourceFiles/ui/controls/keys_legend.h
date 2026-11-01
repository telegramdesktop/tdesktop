/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/unique_qptr.h"
#include "ui/style/style_core_types.h"

class QPainter;
class QWidget;

namespace style {
struct PopupMenu;
struct TextStyle;
} // namespace style

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Ui::KeysLegend {

struct Row {
	QStringList keys;
	QString text;
};

struct Section {
	QString title;
	std::vector<Row> rows;
	bool stickToPrevious = false;
};

struct St {
	const style::PopupMenu *menu = nullptr;
	const style::TextStyle *titleStyle = nullptr;
	const style::color *titleFg = nullptr;
	int titleSkip = 0;
	int rowSkip = 0;
	const style::TextStyle *capStyle = nullptr;
	int capHeight = 0;
	int capMinWidth = 0;
	int capMaxWidth = 0;
	int capPadding = 0;
	int capRadius = 0;
	int capSkip = 0;
	const style::color *capBg = nullptr;
	const style::color *capBorder = nullptr;
	const style::color *capFg = nullptr;
	int textSkip = 0;
	int textWidth = 0;
	int textMinWidth = 0;
	const style::color *textFg = nullptr;
	int skip = 0;
};

struct Cap {
	QString text;
	int width = 0;
	int height = 0;
};

[[nodiscard]] inline int CapsMaxHeight(
		const std::vector<Cap> &caps,
		int fallback) {
	auto result = fallback;
	for (const auto &cap : caps) {
		if (cap.height > result) {
			result = cap.height;
		}
	}
	return result;
}

[[nodiscard]] Cap MakeCap(const QString &text, const St &st);
void PaintCap(QPainter &p, const Cap &cap, int x, int y, const St &st);

[[nodiscard]] QString CtrlKey();
// Physical Control key: Qt swaps modifiers on mac, so it is not CtrlKey().
[[nodiscard]] QString ControlKey();
[[nodiscard]] QString ShiftKey();
[[nodiscard]] QString AltKey();
[[nodiscard]] QString DeleteKey();
[[nodiscard]] QString EscKey();
[[nodiscard]] QString SpaceKey();

// Shows the popup above `anchor` or below it when it does not fit.
[[nodiscard]] base::unique_qptr<Ui::PopupMenu> ShowLegendMenu(
	not_null<QWidget*> anchor,
	const St &st,
	std::vector<Section> sections);

} // namespace Ui::KeysLegend
