/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/who_read_context_action.h"

#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "ui/effects/ripple_animation.h"
#include "ui/chat/group_call_userpics.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"

namespace Ui {
namespace {

constexpr auto kMaxUserpics = 3;

class Action final : public Menu::ItemBase {
public:
	Action(
		not_null<PopupMenu*> parentMenu,
		rpl::producer<WhoReadContent> content,
		Fn<void(uint64)> participantChosen);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

protected:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

private:
	void paint(Painter &p);

	void updateUserpicsFromContent();
	void setupSubMenu();
	void resolveMinWidth();
	void refreshText();
	void refreshDimensions();

	const not_null<PopupMenu*> _parentMenu;
	const not_null<QAction*> _dummyAction;
	const Fn<void(uint64)> _participantChosen;
	const std::unique_ptr<GroupCallUserpics> _userpics;
	const style::Menu &_st;

	Text::String _text;
	int _textWidth = 0;
	const int _height = 0;
	int _userpicsWidth = 0;

	WhoReadContent _content;

};

TextParseOptions MenuTextOptions = {
	TextParseLinks | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

Action::Action(
	not_null<PopupMenu*> parentMenu,
	rpl::producer<WhoReadContent> content,
	Fn<void(uint64)> participantChosen)
: ItemBase(parentMenu->menu(), parentMenu->menu()->st())
, _parentMenu(parentMenu)
, _dummyAction(new QAction(parentMenu->menu()))
, _participantChosen(std::move(participantChosen))
, _userpics(std::make_unique<GroupCallUserpics>(
	st::historyGroupCallUserpics,
	rpl::never<bool>(),
	[=] { update(); }))
, _st(parentMenu->menu()->st())
, _height(st::ttlItemPadding.top()
		+ _st.itemStyle.font->height
		+ st::ttlItemTimerFont->height
		+ st::ttlItemPadding.bottom()) {
	const auto parent = parentMenu->menu();

	setAcceptBoth(true);
	initResizeHook(parent->sizeValue());
	setClickedCallback([=] {
		if (!_content.participants.empty()) {
			setupSubMenu();
		}
	});
	resolveMinWidth();

	auto copy = std::move(
		content
	) | rpl::start_spawning(lifetime());

	_userpics->widthValue(
	) | rpl::start_with_next([=](int width) {
		_userpicsWidth = width;
		refreshDimensions();
		update();
	}, lifetime());

	std::move(
		content
	) | rpl::start_with_next([=](WhoReadContent &&content) {
		_content = content;
		updateUserpicsFromContent();
		refreshText();
		refreshDimensions();
		update();
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	enableMouseSelecting();
}

void Action::resolveMinWidth() {
	const auto maxIconWidth = 0;
	const auto width = [&](const QString &text) {
		return _st.itemStyle.font->width(text);
	};
	const auto maxTextWidth = std::max(
		width(tr::lng_context_seen_text(tr::now, lt_count, 999)),
		width(tr::lng_context_seen_listened(tr::now, lt_count, 999)));
	const auto maxWidth = _st.itemPadding.left()
		+ maxIconWidth
		+ maxTextWidth
		+ _userpics->maxWidth()
		+ _st.itemPadding.right();
	setMinWidth(maxWidth);
}

void Action::updateUserpicsFromContent() {
	auto users = std::vector<GroupCallUser>();
	if (!_content.participants.empty()) {
		const auto count = std::min(
			int(_content.participants.size()),
			kMaxUserpics);
		users.reserve(count);
		for (auto i = 0; i != count; ++i) {
			const auto &participant = _content.participants[i];
			users.push_back({
				.userpic = participant.userpic,
				.userpicKey = participant.userpicKey,
				.id = participant.id,
			});
		}
	}
	_userpics->update(users, true);
}

void Action::setupSubMenu() {

}

void Action::paint(Painter &p) {
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
		_st.itemPadding.top(),
		_textWidth,
		width());
	_userpics->paint(
		p,
		width() - _st.itemPadding.right(),
		_st.itemPadding.top(),
		st::historyGroupCallUserpics.size);
}

void Action::refreshText() {
	const auto count = int(_content.participants.size());
	_text.setMarkedText(
		_st.itemStyle,
		{ (_content.unknown
			? tr::lng_context_seen_loading(tr::now)
			: (count == 1)
			? _content.participants.front().name
			: _content.listened
			? tr::lng_context_seen_listened(tr::now, lt_count, count)
			: tr::lng_context_seen_text(tr::now, lt_count, count)) },
		MenuTextOptions);
}

void Action::refreshDimensions() {
	const auto textWidth = _text.maxWidth();
	const auto &padding = _st.itemPadding;

	const auto goodWidth = padding.left()
		+ textWidth
		+ _userpicsWidth
		+ padding.right();

	const auto w = std::clamp(
		goodWidth,
		_st.widthMin,
		_st.widthMax);
	_textWidth = w - (goodWidth - textWidth);
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
	return Ui::RippleAnimation::rectMask(size());
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
		setClicked(Menu::TriggeredSource::Keyboard);
	}
}

} // namespace

base::unique_qptr<Menu::ItemBase> WhoReadContextAction(
		not_null<PopupMenu*> menu,
		rpl::producer<WhoReadContent> content,
		Fn<void(uint64)> participantChosen) {
	return base::make_unique_q<Action>(
		menu,
		std::move(content),
		std::move(participantChosen));
}

} // namespace Ui
