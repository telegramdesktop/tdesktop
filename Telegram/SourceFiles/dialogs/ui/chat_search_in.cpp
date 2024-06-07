/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/chat_search_in.h"

#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"

namespace Dialogs {
namespace {

class Action final : public Ui::Menu::ItemBase {
public:
	Action(
		not_null<Ui::PopupMenu*> parentMenu,
		std::shared_ptr<Ui::DynamicImage> icon,
		const QString &label,
		bool chosen);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

protected:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

private:
	void paint(Painter &p);

	void resolveMinWidth();
	void refreshDimensions();

	const not_null<Ui::PopupMenu*> _parentMenu;
	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const int _height = 0;

	std::shared_ptr<Ui::DynamicImage> _icon;
	Ui::Text::String _text;
	bool _checked = false;

};

[[nodiscard]] QString TabLabel(
		ChatSearchTab tab,
		ChatSearchPeerTabType type = {}) {
	switch (tab) {
	case ChatSearchTab::MyMessages:
		return tr::lng_search_tab_my_messages(tr::now);
	case ChatSearchTab::ThisTopic:
		return tr::lng_search_tab_this_topic(tr::now);
	case ChatSearchTab::ThisPeer:
		switch (type) {
		case ChatSearchPeerTabType::Chat:
			return tr::lng_search_tab_this_chat(tr::now);
		case ChatSearchPeerTabType::Channel:
			return tr::lng_search_tab_this_channel(tr::now);
		case ChatSearchPeerTabType::Group:
			return tr::lng_search_tab_this_group(tr::now);
		}
		Unexpected("Type in Dialogs::TabLabel.");
	case ChatSearchTab::PublicPosts:
		return tr::lng_search_tab_public_posts(tr::now);
	}
	Unexpected("Tab in Dialogs::TabLabel.");
}

Action::Action(
	not_null<Ui::PopupMenu*> parentMenu,
	std::shared_ptr<Ui::DynamicImage> icon,
	const QString &label,
	bool chosen)
: ItemBase(parentMenu->menu(), parentMenu->menu()->st())
, _parentMenu(parentMenu)
, _dummyAction(CreateChild<QAction>(parentMenu->menu().get()))
, _st(parentMenu->menu()->st())
, _height(st::dialogsSearchInHeight)
, _icon(std::move(icon))
, _checked(chosen) {
	const auto parent = parentMenu->menu();

	_text.setText(st::semiboldTextStyle, label);
	_icon->subscribeToUpdates([=] { update(); });

	initResizeHook(parent->sizeValue());
	resolveMinWidth();

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	enableMouseSelecting();
}

void Action::resolveMinWidth() {
	const auto maxWidth = st::dialogsSearchInPhotoPadding
		+ st::dialogsSearchInPhotoSize
		+ st::dialogsSearchInSkip
		+ _text.maxWidth()
		+ st::dialogsSearchInCheckSkip
		+ st::dialogsSearchInCheck.width()
		+ st::dialogsSearchInCheckSkip;
	setMinWidth(maxWidth);
}

void Action::paint(Painter &p) {
	const auto enabled = isEnabled();
	const auto selected = isSelected();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), _height, _st.itemBg);
	}
	const auto &bg = selected ? _st.itemBgOver : _st.itemBg;
	p.fillRect(0, 0, width(), _height, bg);
	if (enabled) {
		paintRipple(p, 0, 0);
	}

	auto x = st::dialogsSearchInPhotoPadding;
	const auto photos = st::dialogsSearchInPhotoSize;
	const auto photoy = (height() - photos) / 2;
	p.drawImage(QRect{ x, photoy, photos, photos }, _icon->image(photos));
	x += photos + st::dialogsSearchInSkip;
	const auto available = width()
		- x
		- st::dialogsSearchInCheckSkip
		- st::dialogsSearchInCheck.width()
		- st::dialogsSearchInCheckSkip;

	p.setPen(!enabled
		? _st.itemFgDisabled
		: selected
		? _st.itemFgOver
		: _st.itemFg);
	_text.drawLeftElided(
		p,
		x,
		st::dialogsSearchInNameTop,
		available,
		width());
	x += available;
	if (_checked) {
		x += st::dialogsSearchInCheckSkip;
		const auto &icon = st::dialogsSearchInCheck;
		const auto icony = (height() - icon.height()) / 2;
		icon.paint(p, x, icony, width());
	}
}

bool Action::isEnabled() const {
	return true;
}

not_null<QAction*> Action::action() const {
	return _dummyAction;
}

QPoint Action::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage Action::prepareRippleMask() const {
	return Ui::RippleAnimation::RectMask(size());
}

int Action::contentHeight() const {
	return _height;
}

void Action::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Ui::Menu::TriggeredSource::Keyboard);
	}
}

} // namespace

FixedHashtagSearchQuery FixHashtagSearchQuery(
		const QString &query,
		int cursorPosition) {
	const auto trimmed = query.trimmed();
	const auto hash = int(trimmed.isEmpty()
		? query.size()
		: query.indexOf(trimmed));
	const auto start = std::min(cursorPosition, hash);
	auto result = query.mid(0, start);
	for (const auto &ch : query.mid(start)) {
		if (ch.isSpace()) {
			if (cursorPosition > result.size()) {
				--cursorPosition;
			}
			continue;
		} else if (result.size() == start) {
			result += '#';
			if (ch != '#') {
				++cursorPosition;
			}
		}
		if (ch != '#') {
			result += ch;
		}
	}
	if (result.size() == start) {
		result += '#';
		++cursorPosition;
	}
	return { result, cursorPosition };
}

bool IsHashtagSearchQuery(const QString &query) {
	const auto trimmed = query.trimmed();
	if (trimmed.isEmpty() || trimmed[0] != '#') {
		return false;
	}
	for (const auto &ch : trimmed) {
		if (ch.isSpace()) {
			return false;
		}
	}
	return true;
}

void ChatSearchIn::Section::update() {
	outer->update();
}

ChatSearchIn::ChatSearchIn(QWidget *parent)
: RpWidget(parent) {
	_in.clicks.events() | rpl::start_with_next([=] {
		showMenu();
	}, lifetime());
}

ChatSearchIn::~ChatSearchIn() = default;

void ChatSearchIn::apply(
		std::vector<PossibleTab> tabs,
		ChatSearchTab active,
		ChatSearchPeerTabType peerTabType,
		std::shared_ptr<Ui::DynamicImage> fromUserpic,
		QString fromName) {
	_tabs = std::move(tabs);
	_peerTabType = peerTabType;
	_active = active;
	const auto i = ranges::find(_tabs, active, &PossibleTab::tab);
	Assert(i != end(_tabs));
	Assert(i->icon != nullptr);
	updateSection(
		&_in,
		i->icon->clone(),
		Ui::Text::Semibold(TabLabel(active, peerTabType)));

	auto text = tr::lng_dlg_search_from(
		tr::now,
		lt_user,
		Ui::Text::Semibold(fromName),
		Ui::Text::WithEntities);
	updateSection(&_from, std::move(fromUserpic), std::move(text));

	resizeToWidth(width());
}

rpl::producer<> ChatSearchIn::cancelInRequests() const {
	return _in.cancelRequests.events();
}

rpl::producer<> ChatSearchIn::cancelFromRequests() const {
	return _from.cancelRequests.events();
}

rpl::producer<> ChatSearchIn::changeFromRequests() const {
	return _from.clicks.events();
}

rpl::producer<ChatSearchTab> ChatSearchIn::tabChanges() const {
	return _active.changes();
}

void ChatSearchIn::showMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::dialogsSearchInMenu);
	const auto active = _active.current();
	auto activeIndex = 0;
	for (const auto &tab : _tabs) {
		if (!tab.icon) {
			continue;
		}
		const auto value = tab.tab;
		if (value == active) {
			activeIndex = _menu->actions().size();
		}
		auto action = base::make_unique_q<Action>(
			_menu.get(),
			tab.icon,
			TabLabel(value, _peerTabType),
			(value == active));
		action->setClickedCallback([=] {
			_active = value;
		});
		_menu->addAction(std::move(action));
	}
	const auto count = int(_menu->actions().size());
	const auto bottomLeft = (activeIndex * 2 >= count);
	const auto single = st::dialogsSearchInHeight;
	const auto in = mapToGlobal(_in.outer->pos()
		+ QPoint(0, bottomLeft ? count * single : 0));
	_menu->setForcedOrigin(bottomLeft
		? Ui::PanelAnimation::Origin::BottomLeft
		: Ui::PanelAnimation::Origin::TopLeft);
	if (_menu->prepareGeometryFor(in)) {
		_menu->move(_menu->pos() - QPoint(_menu->inner().x(), activeIndex * single));
		_menu->popupPrepared();
	}
}

void ChatSearchIn::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto top = QRect(0, 0, width(), st::searchedBarHeight);
	p.fillRect(top, st::searchedBarBg);
	p.fillRect(rect().translated(0, st::searchedBarHeight), st::dialogsBg);

	p.setFont(st::searchedBarFont);
	p.setPen(st::searchedBarFg);
	p.drawTextLeft(
		st::searchedBarPosition.x(),
		st::searchedBarPosition.y(),
		width(),
		tr::lng_dlg_search_in(tr::now));
}

int ChatSearchIn::resizeGetHeight(int newWidth) {
	auto result = st::searchedBarHeight;
	if (const auto raw = _in.outer.get()) {
		raw->resizeToWidth(newWidth);
		raw->move(0, result);
		result += raw->height();
		_in.shadow->setGeometry(0, result, newWidth, st::lineWidth);
		result += st::lineWidth;
	}
	if (const auto raw = _from.outer.get()) {
		raw->resizeToWidth(newWidth);
		raw->move(0, result);
		result += raw->height();
		_from.shadow->setGeometry(0, result, newWidth, st::lineWidth);
		result += st::lineWidth;
	}
	return result;
}

void ChatSearchIn::updateSection(
		not_null<Section*> section,
		std::shared_ptr<Ui::DynamicImage> image,
		TextWithEntities text) {
	if (section->subscribed) {
		section->image->subscribeToUpdates(nullptr);
		section->subscribed = false;
	}
	if (!image) {
		if (section->outer) {
			section->cancel = nullptr;
			section->shadow = nullptr;
			section->outer = nullptr;
			section->subscribed = false;
		}
		return;
	} else if (!section->outer) {
		auto button = std::make_unique<Ui::AbstractButton>(this);
		const auto raw = button.get();
		section->outer = std::move(button);

		raw->resize(
			st::columnMinimalWidthLeft,
			st::dialogsSearchInHeight);

		raw->paintRequest() | rpl::start_with_next([=] {
			auto p = QPainter(raw);
			if (!section->subscribed) {
				section->subscribed = true;
				section->image->subscribeToUpdates([=] {
					raw->update();
				});
			}
			const auto outer = raw->width();
			const auto size = st::dialogsSearchInPhotoSize;
			const auto left = st::dialogsSearchInPhotoPadding;
			const auto top = (st::dialogsSearchInHeight - size) / 2;
			p.drawImage(
				QRect{ left, top, size, size },
				section->image->image(size));

			const auto x = left + size + st::dialogsSearchInSkip;
			const auto available = outer
				- st::dialogsSearchInSkip
				- section->cancel->width()
				- 2 * st::dialogsSearchInDownSkip
				- st::dialogsSearchInDown.width()
				- x;
			const auto use = std::min(section->text.maxWidth(), available);
			const auto iconx = x + use + st::dialogsSearchInDownSkip;
			const auto icony = st::dialogsSearchInDownTop;
			st::dialogsSearchInDown.paint(p, iconx, icony, outer);
			p.setPen(st::windowBoldFg);
			section->text.draw(p, {
				.position = QPoint(x, st::dialogsSearchInNameTop),
				.outerWidth = outer,
				.availableWidth = available,
				.elisionLines = 1,
			});
		}, raw->lifetime());

		section->shadow = std::make_unique<Ui::PlainShadow>(this);
		section->shadow->show();

		const auto st = &st::dialogsCancelSearchInPeer;
		section->cancel = std::make_unique<Ui::IconButton>(raw, *st);
		section->cancel->show();
		raw->sizeValue() | rpl::start_with_next([=](QSize size) {
			const auto left = size.width() - section->cancel->width();
			const auto top = (size.height() - st->height) / 2;
			section->cancel->moveToLeft(left, top);
		}, section->cancel->lifetime());
		section->cancel->clicks() | rpl::to_empty | rpl::start_to_stream(
			section->cancelRequests,
			section->cancel->lifetime());

		raw->clicks() | rpl::to_empty | rpl::start_to_stream(
			section->clicks,
			raw->lifetime());

		raw->show();
	}
	section->image = std::move(image);
	section->text.setMarkedText(st::dialogsSearchFromStyle, std::move(text));
}

} // namespace Dialogs
