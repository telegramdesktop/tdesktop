/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/who_reacted_context_action.h"

#include "base/call_delayed.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "ui/effects/ripple_animation.h"
#include "ui/chat/group_call_userpics.h"
#include "ui/text/text_custom_emoji.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

namespace Lang {
namespace {

struct StringWithReacted {
	QString text;
	int seen = 0;
};

} // namespace

template <typename ResultString>
struct StartReplacements;

template <>
struct StartReplacements<StringWithReacted> {
	static inline StringWithReacted Call(QString &&langString) {
		return { std::move(langString) };
	}
};

template <typename ResultString>
struct ReplaceTag;

template <>
struct ReplaceTag<StringWithReacted> {
	static StringWithReacted Call(
		StringWithReacted &&original,
		ushort tag,
		const StringWithReacted &replacement);
};

StringWithReacted ReplaceTag<StringWithReacted>::Call(
		StringWithReacted &&original,
		ushort tag,
		const StringWithReacted &replacement) {
	const auto offset = FindTagReplacementPosition(original.text, tag);
	if (offset < 0) {
		return std::move(original);
	}
	original.text = ReplaceTag<QString>::Call(
		std::move(original.text),
		tag,
		replacement.text + '/' + QString::number(original.seen));
	return std::move(original);
}

} // namespace Lang

namespace Ui {
namespace {

using Text::CustomEmojiFactory;

struct EntryData {
	QString text;
	QString customEntityData;
	QImage userpic;
	Fn<void()> callback;
};

class Action final : public Menu::ItemBase {
public:
	Action(
		not_null<PopupMenu*> parentMenu,
		rpl::producer<WhoReadContent> content,
		CustomEmojiFactory factory,
		Fn<void(uint64)> participantChosen,
		Fn<void()> showAllChosen);

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
	void resolveMinWidth();
	void refreshText();
	void refreshDimensions();
	void populateSubmenu();

	const not_null<PopupMenu*> _parentMenu;
	const not_null<QAction*> _dummyAction;
	const Fn<void(uint64)> _participantChosen;
	const Fn<void()> _showAllChosen;
	const std::unique_ptr<GroupCallUserpics> _userpics;
	const style::Menu &_st;
	const CustomEmojiFactory _customEmojiFactory;

	WhoReactedListMenu _submenu;

	Text::String _text;
	std::unique_ptr<Ui::Text::CustomEmoji> _custom;
	int _textWidth = 0;
	const int _height = 0;
	int _userpicsWidth = 0;
	bool _appeared = false;

	WhoReadContent _content;

};

TextParseOptions MenuTextOptions = {
	TextParseLinks, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

[[nodiscard]] QString FormatReactedString(int reacted, int seen) {
	const auto projection = [&](const QString &text) {
		return Lang::StringWithReacted{ text, seen };
	};
	return tr::lng_context_seen_reacted(
		tr::now,
		lt_count_short,
		reacted,
		projection
	).text;
}

Action::Action(
	not_null<PopupMenu*> parentMenu,
	rpl::producer<WhoReadContent> content,
	Text::CustomEmojiFactory factory,
	Fn<void(uint64)> participantChosen,
	Fn<void()> showAllChosen)
: ItemBase(parentMenu->menu(), parentMenu->menu()->st())
, _parentMenu(parentMenu)
, _dummyAction(CreateChild<QAction>(parentMenu->menu().get()))
, _participantChosen(std::move(participantChosen))
, _showAllChosen(std::move(showAllChosen))
, _userpics(std::make_unique<GroupCallUserpics>(
	st::defaultWhoRead.userpics,
	rpl::never<bool>(),
	[=] { update(); }))
, _st(parentMenu->menu()->st())
, _customEmojiFactory(std::move(factory))
, _submenu(_customEmojiFactory, _participantChosen, _showAllChosen)
, _height(st::defaultWhoRead.itemPadding.top()
		+ _st.itemStyle.font->height
		+ st::defaultWhoRead.itemPadding.bottom()) {
	const auto parent = parentMenu->menu();
	const auto checkAppeared = [=, now = crl::now()] {
		_appeared = (crl::now() - now) >= parentMenu->st().duration;
	};

	setAcceptBoth(true);
	initResizeHook(parent->sizeValue());

	std::move(
		content
	) | rpl::start_with_next([=](WhoReadContent &&content) {
		checkAppeared();
		const auto changed = (_content.participants != content.participants)
			|| (_content.unknown != content.unknown);
		_content = content;
		if (changed) {
			PostponeCall(this, [=] { populateSubmenu(); });
		}
		updateUserpicsFromContent();
		refreshText();
		refreshDimensions();
		setPointerCursor(isEnabled());
		_dummyAction->setEnabled(isEnabled());
		if (!isEnabled()) {
			setSelected(false);
		}
		update();
	}, lifetime());

	resolveMinWidth();

	_userpics->widthValue(
	) | rpl::start_with_next([=](int width) {
		_userpicsWidth = width;
		refreshDimensions();
		update();
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	clicks(
	) | rpl::start_with_next([=] {
		if (_content.participants.size() == 1) {
			if (const auto onstack = _participantChosen) {
				onstack(_content.participants.front().id);
			}
		} else if (_content.fullReactionsCount > 0) {
			if (const auto onstack = _showAllChosen) {
				onstack();
			}
		}
	}, lifetime());

	enableMouseSelecting();

	base::call_delayed(parentMenu->st().duration, this, [=] {
		checkAppeared();
		updateUserpicsFromContent();
	});
}

void Action::resolveMinWidth() {
	const auto maxIconWidth = 0;
	const auto width = [&](const QString &text) {
		return _st.itemStyle.font->width(text);
	};
	const auto maxText = (_content.type == WhoReadType::Listened)
		? tr::lng_context_seen_listened(tr::now, lt_count, 999)
		: (_content.type == WhoReadType::Watched)
		? tr::lng_context_seen_watched(tr::now, lt_count, 999)
		: (_content.type == WhoReadType::Seen)
		? tr::lng_context_seen_text(tr::now, lt_count, 999)
		: QString();
	const auto maxReacted = (_content.fullReactionsCount > 0)
		? (!maxText.isEmpty()
			? FormatReactedString(_content.fullReactionsCount, 999)
			: tr::lng_context_seen_reacted(
				tr::now,
				lt_count_short,
				_content.fullReactionsCount))
		: QString();
	const auto maxTextWidth = std::max(width(maxText), width(maxReacted));
	const auto maxWidth = st::defaultWhoRead.itemPadding.left()
		+ maxIconWidth
		+ maxTextWidth
		+ _userpics->maxWidth()
		+ st::defaultWhoRead.itemPadding.right();
	setMinWidth(maxWidth);
}

void Action::updateUserpicsFromContent() {
	if (!_appeared) {
		return;
	}
	auto users = std::vector<GroupCallUser>();
	if (!_content.participants.empty()) {
		const auto count = std::min(
			int(_content.participants.size()),
			WhoReadParticipant::kMaxSmallUserpics);
		const auto factor = style::DevicePixelRatio();
		users.reserve(count);
		for (auto i = 0; i != count; ++i) {
			auto &participant = _content.participants[i];
			participant.userpicSmall.setDevicePixelRatio(factor);
			users.push_back({
				.userpic = participant.userpicSmall,
				.userpicKey = participant.userpicKey,
				.id = participant.id,
			});
		}
	}
	_userpics->update(users, true);
}

void Action::populateSubmenu() {
	if (_content.participants.size() < 2) {
		_submenu.clear();
		_parentMenu->removeSubmenu(action());
		if (!isEnabled()) {
			setSelected(false);
		}
		return;
	}

	const auto submenu = _parentMenu->ensureSubmenu(
		action(),
		st::whoReadMenu);
	_submenu.populate(submenu, _content);
	_parentMenu->checkSubmenuShow();
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
	if (!_custom && !_content.singleCustomEntityData.isEmpty()) {
		_custom = _customEmojiFactory(
			_content.singleCustomEntityData,
			[=] { update(); });
	}
	if (_custom) {
		const auto ratio = style::DevicePixelRatio();
		const auto size = Emoji::GetSizeNormal() / ratio;
		const auto adjusted = Text::AdjustCustomEmojiSize(size);
		const auto x = st::defaultWhoRead.iconPosition.x()
			+ (st::whoReadChecks.width() - adjusted) / 2;
		const auto y = (_height - adjusted) / 2;
		_custom->paint(p, {
			.preview = _st.ripple.color->c,
			.now = crl::now(),
			.position = { x, y },
		});
	} else {
		const auto &icon = (_content.fullReactionsCount)
			? (!enabled
				? st::whoReadReactionsDisabled
				: selected
				? st::whoReadReactionsOver
				: st::whoReadReactions)
			: (_content.type == WhoReadType::Seen)
			? (!enabled
				? st::whoReadChecksDisabled
				: selected
				? st::whoReadChecksOver
				: st::whoReadChecks)
			: (!enabled
				? st::whoReadPlayedDisabled
				: selected
				? st::whoReadPlayedOver
				: st::whoReadPlayed);
		icon.paint(p, st::defaultWhoRead.iconPosition, width());
	}
	p.setPen(!enabled
		? _st.itemFgDisabled
		: selected
		? _st.itemFgOver
		: _st.itemFg);
	_text.drawLeftElided(
		p,
		st::defaultWhoRead.itemPadding.left(),
		st::defaultWhoRead.itemPadding.top(),
		_textWidth,
		width());
	if (_appeared) {
		_userpics->paint(
			p,
			width() - st::defaultWhoRead.itemPadding.right(),
			(height() - st::defaultWhoRead.userpics.size) / 2,
			st::defaultWhoRead.userpics.size);
	}
}

void Action::refreshText() {
	const auto usersCount = int(_content.participants.size());
	const auto onlySeenCount = ranges::count(
		_content.participants,
		QString(),
		&WhoReadParticipant::customEntityData);
	const auto count = std::max(_content.fullReactionsCount, usersCount);
	_text.setMarkedText(
		_st.itemStyle,
		{ (_content.unknown
			? tr::lng_context_seen_loading(tr::now)
			: (usersCount == 1)
			? _content.participants.front().name
			: (_content.fullReactionsCount > 0
				&& _content.fullReactionsCount <= _content.fullReadCount)
			? FormatReactedString(
				_content.fullReactionsCount,
				_content.fullReadCount)
			: (_content.type == WhoReadType::Reacted
				|| (count > 0 && _content.fullReactionsCount > usersCount)
				|| (count > 0 && onlySeenCount == 0))
			? (count
				? tr::lng_context_seen_reacted(
					tr::now,
					lt_count_short,
					count)
				: tr::lng_context_seen_reacted_none(tr::now))
			: (_content.type == WhoReadType::Watched)
			? (count
				? tr::lng_context_seen_watched(tr::now, lt_count, count)
				: tr::lng_context_seen_watched_none(tr::now))
			: (_content.type == WhoReadType::Listened)
			? (count
				? tr::lng_context_seen_listened(tr::now, lt_count, count)
				: tr::lng_context_seen_listened_none(tr::now))
			: (count
				? tr::lng_context_seen_text(tr::now, lt_count, count)
				: tr::lng_context_seen_text_none(tr::now))) },
		MenuTextOptions);
}

void Action::refreshDimensions() {
	if (!minWidth()) {
		return;
	}
	const auto textWidth = _text.maxWidth();
	const auto &padding = st::defaultWhoRead.itemPadding;

	const auto goodWidth = padding.left()
		+ textWidth
		+ (_userpicsWidth ? (_st.itemStyle.font->spacew + _userpicsWidth) : 0)
		+ padding.right();

	const auto w = std::clamp(
		goodWidth,
		_st.widthMin,
		std::max(minWidth(), _st.widthMin));
	_textWidth = w - (goodWidth - textWidth);
}

bool Action::isEnabled() const {
	return !_content.participants.empty();
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

class WhoReactedListMenu::EntryAction final : public Menu::ItemBase {
public:
	EntryAction(
		not_null<RpWidget*> parent,
		CustomEmojiFactory factory,
		const style::Menu &st,
		EntryData &&data);

	void setData(EntryData &&data);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

private:
	int contentHeight() const override;

	void paint(Painter &&p);

	const not_null<QAction*> _dummyAction;
	const CustomEmojiFactory _customEmojiFactory;
	const style::Menu &_st;
	const int _height = 0;

	Text::String _text;
	std::unique_ptr<Ui::Text::CustomEmoji> _custom;
	QImage _userpic;
	int _textWidth = 0;
	int _customSize = 0;

};

WhoReactedListMenu::EntryAction::EntryAction(
	not_null<RpWidget*> parent,
	CustomEmojiFactory customEmojiFactory,
	const style::Menu &st,
	EntryData &&data)
: ItemBase(parent, st)
, _dummyAction(CreateChild<QAction>(parent.get()))
, _customEmojiFactory(std::move(customEmojiFactory))
, _st(st)
, _height(st::defaultWhoRead.photoSkip * 2 + st::defaultWhoRead.photoSize) {
	setAcceptBoth(true);

	initResizeHook(parent->sizeValue());
	setData(std::move(data));

	paintRequest(
	) | rpl::start_with_next([=] {
		paint(Painter(this));
	}, lifetime());

	enableMouseSelecting();
}

not_null<QAction*> WhoReactedListMenu::EntryAction::action() const {
	return _dummyAction.get();
}

bool WhoReactedListMenu::EntryAction::isEnabled() const {
	return true;
}

int WhoReactedListMenu::EntryAction::contentHeight() const {
	return _height;
}

void WhoReactedListMenu::EntryAction::setData(EntryData &&data) {
	setClickedCallback(std::move(data.callback));
	_userpic = std::move(data.userpic);
	_text.setMarkedText(_st.itemStyle, { data.text }, MenuTextOptions);
	_custom = _customEmojiFactory(data.customEntityData, [=] { update(); });
	const auto ratio = style::DevicePixelRatio();
	const auto size = Emoji::GetSizeNormal() / ratio;
	_customSize = Text::AdjustCustomEmojiSize(size);
	const auto textWidth = _text.maxWidth();
	const auto &padding = _st.itemPadding;
	const auto rightSkip = padding.right()
		+ (_custom ? (size + padding.right()) : 0);
	const auto goodWidth = st::defaultWhoRead.nameLeft
		+ textWidth
		+ rightSkip;
	const auto w = std::clamp(goodWidth, _st.widthMin, _st.widthMax);
	_textWidth = w - (goodWidth - textWidth);
	setMinWidth(w);
	update();
}

void WhoReactedListMenu::EntryAction::paint(Painter &&p) {
	const auto enabled = isEnabled();
	const auto selected = isSelected();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), _height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), _height, selected ? _st.itemBgOver : _st.itemBg);
	if (enabled) {
		paintRipple(p, 0, 0);
	}
	const auto photoSize = st::defaultWhoRead.photoSize;
	const auto photoLeft = st::defaultWhoRead.photoLeft;
	const auto photoTop = (height() - photoSize) / 2;
	if (!_userpic.isNull()) {
		p.drawImage(photoLeft, photoTop, _userpic);
	} else if (!_custom) {
		st::menuIconReactions.paintInCenter(
			p,
			QRect(photoLeft, photoTop, photoSize, photoSize));
	}

	p.setPen(selected
		? _st.itemFgOver
		: enabled
		? _st.itemFg
		: _st.itemFgDisabled);
	_text.drawLeftElided(
		p,
		st::defaultWhoRead.nameLeft,
		(height() - _st.itemStyle.font->height) / 2,
		_textWidth,
		width());

	if (_custom) {
		const auto ratio = style::DevicePixelRatio();
		const auto size = Emoji::GetSizeNormal() / ratio;
		const auto skip = (size - _customSize) / 2;
		_custom->paint(p, {
			.preview = _st.ripple.color->c,
			.now = crl::now(),
			.position = QPoint(
				width() - _st.itemPadding.right() - (size / ratio) + skip,
				(height() - _customSize) / 2),
		});
	}
}

bool operator==(const WhoReadParticipant &a, const WhoReadParticipant &b) {
	return (a.id == b.id)
		&& (a.name == b.name)
		&& (a.userpicKey == b.userpicKey);
}

bool operator!=(const WhoReadParticipant &a, const WhoReadParticipant &b) {
	return !(a == b);
}

base::unique_qptr<Menu::ItemBase> WhoReactedContextAction(
		not_null<PopupMenu*> menu,
		rpl::producer<WhoReadContent> content,
		CustomEmojiFactory factory,
		Fn<void(uint64)> participantChosen,
		Fn<void()> showAllChosen) {
	return base::make_unique_q<Action>(
		menu,
		std::move(content),
		std::move(factory),
		std::move(participantChosen),
		std::move(showAllChosen));
}

WhoReactedListMenu::WhoReactedListMenu(
	CustomEmojiFactory factory,
	Fn<void(uint64)> participantChosen,
	Fn<void()> showAllChosen)
: _customEmojiFactory(std::move(factory))
, _participantChosen(std::move(participantChosen))
, _showAllChosen(std::move(showAllChosen)) {
}

void WhoReactedListMenu::clear() {
	_actions.clear();
}

void WhoReactedListMenu::populate(
		not_null<PopupMenu*> menu,
		const WhoReadContent &content,
		Fn<void()> refillTopActions,
		int addedToBottom,
		Fn<void()> appendBottomActions) {
	const auto reactions = ranges::count_if(
		content.participants,
		[](const auto &p) { return !p.customEntityData.isEmpty(); });
	const auto addShowAll = (content.fullReactionsCount > reactions);
	const auto actionsCount = int(content.participants.size())
		+ (addShowAll ? 1 : 0);
	if (_actions.size() > actionsCount) {
		_actions.clear();
		menu->clearActions();
		if (refillTopActions) {
			refillTopActions();
		}
		addedToBottom = 0;
	}
	auto index = 0;
	const auto append = [&](EntryData &&data) {
		if (index < _actions.size()) {
			_actions[index]->setData(std::move(data));
		} else {
			auto item = base::make_unique_q<EntryAction>(
				menu->menu(),
				_customEmojiFactory,
				menu->menu()->st(),
				std::move(data));
			_actions.push_back(item.get());
			const auto count = int(menu->actions().size());
			if (addedToBottom > 0 && addedToBottom <= count) {
				menu->insertAction(count - addedToBottom, std::move(item));
			} else {
				menu->addAction(std::move(item));
			}
		}
		++index;
	};
	for (const auto &participant : content.participants) {
		const auto chosen = [call = _participantChosen, id = participant.id]{
			call(id);
		};
		append({
			.text = participant.name,
			.customEntityData = participant.customEntityData,
			.userpic = participant.userpicLarge,
			.callback = chosen,
		});
	}
	if (addShowAll) {
		append({
			.text = tr::lng_context_seen_reacted_all(tr::now),
			.callback = _showAllChosen,
		});
	}
	if (!addedToBottom && appendBottomActions) {
		appendBottomActions();
	}
}

} // namespace Ui
