/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_chat_preview.h"

#include "data/data_peer.h"
#include "history/history.h"
#include "ui/chat/chat_theme.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

class Item final : public Ui::Menu::ItemBase {
public:
	Item(not_null<Ui::RpWidget*> parent, not_null<History*> history);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

private:
	void setupBackground();

	int contentHeight() const override;

	void paintEvent(QPaintEvent *e) override;

	const not_null<QAction*> _dummyAction;
	const std::shared_ptr<Ui::ChatTheme> _theme;

	QImage _bg;

};

Item::Item(not_null<Ui::RpWidget*> parent, not_null<History*> history)
: Ui::Menu::ItemBase(parent, st::previewMenu.menu)
, _dummyAction(new QAction(parent))
, _theme(Window::Theme::DefaultChatThemeOn(lifetime())) {
	setPointerCursor(false);
	setMinWidth(st::previewMenu.menu.widthMin);
	resize(minWidth(), contentHeight());
	setupBackground();
}

not_null<QAction*> Item::action() const {
	return _dummyAction;
}

bool Item::isEnabled() const {
	return false;
}

int Item::contentHeight() const {
	return st::previewMenu.maxHeight;
}

void Item::setupBackground() {
	const auto ratio = style::DevicePixelRatio();
	_bg = QImage(
		size() * ratio,
		QImage::Format_ARGB32_Premultiplied);

	const auto paint = [=] {
		auto p = QPainter(&_bg);
		Window::SectionWidget::PaintBackground(
			p,
			_theme.get(),
			QSize(width(), height() * 2),
			QRect(QPoint(), size()));
	};
	paint();
	_theme->repaintBackgroundRequests() | rpl::start_with_next([=] {
		paint();
		update();
	}, lifetime());
}

void Item::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.drawImage(0, 0, _bg);
}

} // namespace

base::unique_qptr<Ui::PopupMenu> MakeChatPreview(
		QWidget *parent,
		not_null<Dialogs::Entry*> entry) {
	if (const auto topic = entry->asTopic()) {
		return nullptr;
	}
	const auto history = entry->asHistory();
	if (!history || history->peer->isForum()) {
		return nullptr;
	}

	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::previewMenu);

	result->addAction(base::make_unique_q<Item>(result.get(), history));

	return result;
}

} // namespace HistoryView
