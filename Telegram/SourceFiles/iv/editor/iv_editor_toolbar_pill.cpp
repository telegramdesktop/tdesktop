/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_toolbar_pill.h"

#include "dialogs/ui/dialogs_pill.h"
#include "iv/editor/iv_editor_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/buttons.h"

#include "styles/palette.h"
#include "styles/style_dialogs.h"
#include "styles/style_iv.h"

namespace Iv::Editor {

ToolbarPill::ToolbarPill(QWidget *parent, const style::BoxShadow &shadow)
: RpWidget(parent)
, _shadow(shadow)
, _shadowMargins(_shadow.extend()) {
}

not_null<Ui::IconButton*> ToolbarPill::addButton(
		const style::IconButton &buttonSt,
		const style::icon *icon,
		const style::icon *iconOver,
		ToolbarButtonState state) {
	auto button = object_ptr<Ui::IconButton>(this, buttonSt);
	const auto raw = button.data();
	if (icon) {
		raw->setIconOverride(icon, iconOver ? iconOver : icon);
	}
	SetupToolbarButton(raw, state, anim::type::instant);
	addButton(std::move(button), buttonSt);
	return raw;
}

void ToolbarPill::addButton(
		object_ptr<Ui::RippleButton> button,
		const style::IconButton &buttonSt) {
	const auto raw = button.data();
	raw->show();
	if (_buttons.empty()) {
		_buttonSt = &buttonSt;
	}
	_buttons.push_back(std::move(button));
	updateGeometryToContent();
}

// The pill wraps the buttons' RIPPLE AREA (not their full widget rect): inner
// height = rippleAreaSize + 2*pad, inner width = 2*pad + N*rippleAreaSize +
// (N-1)*skip. Each child IconButton is positioned so its ripple area lands at
// (pad, pad) inside the pill, subtracting rippleAreaPosition because the ripple
// sits at that offset within the widget. Shadow extend() margins are reserved
// around the inner rect so the blur is never clipped.
void ToolbarPill::updateGeometryToContent() {
	const auto count = int(_buttons.size());
	const auto pad = st::ivEditorPillPadding;
	const auto skip = st::ivEditorPillButtonSkip;
	const auto &button = _buttonSt
		? *_buttonSt
		: st::ivEditorToolbarButton;
	const auto rippleSize = button.rippleAreaSize;

	const auto innerHeight = rippleSize + 2 * pad;
	const auto innerWidth = count
		? (2 * pad + count * rippleSize + (count - 1) * skip)
		: 0;
	const auto fullWidth = innerWidth + rect::m::sum::h(_shadowMargins);
	const auto fullHeight = innerHeight + rect::m::sum::v(_shadowMargins);
	resize(fullWidth, fullHeight);

	const auto top = _shadowMargins.top()
		+ pad
		- button.rippleAreaPosition.y();
	for (auto i = 0; i != count; ++i) {
		const auto left = _shadowMargins.left()
			+ pad
			+ i * (rippleSize + skip)
			- button.rippleAreaPosition.x();
		_buttons[i]->moveToLeft(left, top);
	}
}

QSize ToolbarPill::naturalSize() const {
	return size();
}

QMargins ToolbarPill::shadowMargins() const {
	return _shadowMargins;
}

void ToolbarPill::paintEvent(QPaintEvent *e) {
	if (_buttons.empty() || width() <= 0 || height() <= 0) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto pill = rect() - _shadowMargins;
	const auto radius = pill.height() / 2;

	_shadow.paint(p, pill, radius);

	p.setBrush(st::dialogsBg);
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(pill, radius, radius);

	Dialogs::PaintPillOutline(p, pill, radius);
}

} // namespace Iv::Editor
