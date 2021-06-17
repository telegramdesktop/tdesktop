/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/delete_message_context_action.h"

#include "ui/widgets/menu/menu_action.h"
#include "ui/effects/ripple_animation.h"
#include "lang/lang_keys.h"
#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "base/timer.h"
#include "styles/style_chat.h"

namespace Ui {
namespace {

class ActionWithTimer final : public Menu::ItemBase {
public:
	ActionWithTimer(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		TimeId destroyAt,
		Fn<void()> callback,
		Fn<void()> destroyByTimerCallback);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

protected:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

private:
	void prepare();
	void refreshAutoDeleteText();
	void paint(Painter &p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const TimeId _destroyAt = 0;
	const Fn<void()> _destroyByTimerCallback;
	const crl::time _startedAt = 0;
	base::Timer _refreshTimer;

	Text::String _text;
	int _textWidth = 0;
	QString _autoDeleteText;
	const int _height;

};

TextParseOptions MenuTextOptions = {
	TextParseLinks | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

ActionWithTimer::ActionWithTimer(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	TimeId destroyAt,
	Fn<void()> callback,
	Fn<void()> destroyByTimerCallback)
: ItemBase(parent, st)
, _dummyAction(new QAction(parent))
, _st(st)
, _destroyAt(destroyAt)
, _destroyByTimerCallback(destroyByTimerCallback)
, _startedAt(crl::now())
, _refreshTimer([=] { refreshAutoDeleteText(); })
, _height(st::ttlItemPadding.top()
		+ _st.itemStyle.font->height
		+ st::ttlItemTimerFont->height
		+ st::ttlItemPadding.bottom()) {
	setAcceptBoth(true);
	initResizeHook(parent->sizeValue());
	setClickedCallback(std::move(callback));

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	enableMouseSelecting();
	prepare();
}

void ActionWithTimer::paint(Painter &p) {
	const auto selected = isSelected();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), _height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), _height, selected ? _st.itemBgOver : _st.itemBg);
	if (isEnabled()) {
		paintRipple(p, 0, 0);
	}
	p.setPen(selected ? _st.itemFgOver : _st.itemFg);
	_text.drawLeftElided(
		p,
		_st.itemPadding.left(),
		st::ttlItemPadding.top(),
		_textWidth,
		width());

	p.setFont(st::ttlItemTimerFont);
	p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
	p.drawTextLeft(
		_st.itemPadding.left(),
		st::ttlItemPadding.top() + _st.itemStyle.font->height,
		width(),
		_autoDeleteText);
}

void ActionWithTimer::refreshAutoDeleteText() {
	const auto now = base::unixtime::now();
	const auto left = (_destroyAt > now) ? (_destroyAt - now) : 0;
	const auto text = [&] {
		const auto duration = (left >= 86400)
			? tr::lng_group_call_duration_days(
				tr::now,
				lt_count,
				((left + 43200) / 86400))
			: (left >= 3600)
			? QString("%1:%2:%3"
			).arg(left / 3600
			).arg((left % 3600) / 60, 2, 10, QChar('0')
			).arg(left % 60, 2, 10, QChar('0'))
			: QString("%1:%2"
			).arg(left / 60
			).arg(left % 60, 2, 10, QChar('0'));
		return tr::lng_context_auto_delete_in(
			tr::now,
			lt_duration,
			duration);
	}();
	if (_autoDeleteText != text) {
		_autoDeleteText = text;
		update();
	}

	if (!left) {
		base::call_delayed(crl::time(100), this, _destroyByTimerCallback);
		return;
	}
	const auto nextCall = (left >= 86400)
		? ((left % 43200) + 1) * crl::time(1000)
		: crl::time(500) - ((crl::now() - _startedAt) % 500);
	_refreshTimer.callOnce(nextCall);
}

void ActionWithTimer::prepare() {
	refreshAutoDeleteText();

	_text.setMarkedText(
		_st.itemStyle,
		{ tr::lng_context_delete_msg(tr::now) },
		MenuTextOptions);
	const auto textWidth = _text.maxWidth();
	const auto &padding = _st.itemPadding;

	const auto goodWidth = padding.left()
		+ textWidth
		+ padding.right();
	const auto ttlMaxWidth = [&](const QString &duration) {
		return padding.left()
			+ st::ttlItemTimerFont->width(tr::lng_context_auto_delete_in(
				tr::now,
				lt_duration,
				duration))
			+ padding.right();
	};
	const auto maxWidth1 = ttlMaxWidth("23:59:59");
	const auto maxWidth2 = ttlMaxWidth(tr::lng_group_call_duration_days(
		tr::now,
		lt_count,
		7));

	const auto w = std::clamp(
		std::max({ goodWidth, maxWidth1, maxWidth2 }),
		_st.widthMin,
		_st.widthMax);
	_textWidth = w - (goodWidth - textWidth);
	setMinWidth(w);
	update();
}

bool ActionWithTimer::isEnabled() const {
	return true;
}

not_null<QAction*> ActionWithTimer::action() const {
	return _dummyAction;
}

QPoint ActionWithTimer::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage ActionWithTimer::prepareRippleMask() const {
	return Ui::RippleAnimation::rectMask(size());
}

int ActionWithTimer::contentHeight() const {
	return _height;
}

void ActionWithTimer::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Menu::TriggeredSource::Keyboard);
	}
}

} // namespace

base::unique_qptr<Menu::ItemBase> DeleteMessageContextAction(
		not_null<Menu::Menu*> menu,
		Fn<void()> callback,
		TimeId destroyAt,
		Fn<void()> destroyByTimerCallback) {
	if (destroyAt <= 0) {
		return base::make_unique_q<Menu::Action>(
			menu,
			menu->st(),
			Menu::CreateAction(
				menu,
				tr::lng_context_delete_msg(tr::now),
				std::move(callback)),
			nullptr,
			nullptr);
	}
	return base::make_unique_q<ActionWithTimer>(
		menu,
		menu->st(),
		destroyAt,
		std::move(callback),
		std::move(destroyByTimerCallback));
}

} // namespace Ui
