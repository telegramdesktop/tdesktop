/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_timecode_action.h"

#include "base/unique_qptr.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/popup_menu.h"

#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

namespace {

class TimecodeAction final : public Ui::Menu::Action {
public:
	TimecodeAction(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		not_null<QAction*> action,
		const style::icon *icon,
		const QString &timecode,
		rpl::producer<QString> updates);

private:
	void paintEvent(QPaintEvent *e) override;

	const style::icon *_replyIcon = nullptr;
	QString _timecode;

};

TimecodeAction::TimecodeAction(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	not_null<QAction*> action,
	const style::icon *icon,
	const QString &timecode,
	rpl::producer<QString> updates)
: Ui::Menu::Action(parent, st, action, nullptr, nullptr)
, _replyIcon(icon)
, _timecode(timecode) {
	std::move(updates) | rpl::on_next([=](const QString &value) {
		_timecode = value;
		update();
	}, lifetime());
}

void TimecodeAction::paintEvent(QPaintEvent *e) {
	Ui::Menu::Action::paintEvent(e);

	auto p = Painter(this);
	if (_replyIcon) {
		const auto pos = st().itemIconPosition;
		const auto iconW = _replyIcon->width();
		constexpr auto kScale = 0.8;
		const auto dx = pos.x() + (iconW * (1. - kScale)) / 2.;
		const auto dy = pos.y();
		auto hq = PainterHighQualityEnabler(p);
		p.translate(dx, dy);
		p.scale(kScale, kScale);
		_replyIcon->paint(p, 0, 0, width() / kScale);
		p.resetTransform();
	}
	const auto &font = st::menuTimecodeFont;
	p.setFont(font);
	p.setPen(st::menuIconColor);
	const auto iconRight = st().itemIconPosition.x()
		+ st::menuIconReply.width();
	const auto textWidth = font->width(_timecode);
	const auto x = iconRight - textWidth;
	const auto y = st().itemIconPosition.y()
		+ st::menuIconReply.height()
		- font->descent;
	p.drawText(x, y, _timecode);
}

} // namespace

namespace Menu {

void InsertTextAtCursor(
		not_null<Ui::InputField*> field,
		const QString &text) {
	auto cursor = field->textCursor();
	const auto pos = cursor.position();
	const auto doc = field->getTextWithTags().text;
	const auto needSpaceBefore = pos > 0
		&& !doc[pos - 1].isSpace();
	auto insert = QString();
	if (needSpaceBefore) {
		insert += ' ';
	}
	insert += text + ' ';
	cursor.insertText(insert);
	field->setTextCursor(cursor);
}

not_null<QAction*> AddTimecodeAction(
		not_null<Ui::PopupMenu*> menu,
		const QString &timecode,
		rpl::producer<QString> updates,
		Fn<void()> callback) {
	auto item = base::make_unique_q<TimecodeAction>(
		menu->menu(),
		menu->st().menu,
		Ui::Menu::CreateAction(
			menu->menu().get(),
			tr::lng_context_reply_with_timecode(tr::now),
			std::move(callback)),
		&st::menuIconReply,
		timecode,
		std::move(updates));
	return menu->addAction(std::move(item));
}

} // namespace Menu
