/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_bot_downloads.h"

#include "lang/lang_keys.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "styles/style_chat.h"

namespace Ui::BotWebView {
namespace {

class Action final : public Menu::ItemBase {
public:
	Action(
		not_null<RpWidget*> parent,
		const DownloadsEntry &entry,
		Fn<void(DownloadsAction)> callback);

	bool isEnabled() const override;
	not_null<QAction*> action() const override { return _dummyAction; }
	void handleKeyPress(not_null<QKeyEvent*> e) override;

	void refresh(const DownloadsEntry &entry);

private:
	QPoint prepareRippleStartPosition() const override {
		return mapFromGlobal(QCursor::pos());
	}
	QImage prepareRippleMask() const override {
		return Ui::RippleAnimation::RectMask(size());
	}
	int contentHeight() const override { return _height; }

	void prepare();
	void paint(Painter &p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st = st::defaultMenu;

	DownloadsEntry _entry;
	Text::String _name;
	FlatLabel _progress;
	IconButton _cancel;
	int _textWidth = 0;
	const int _height;
};

Action::Action(
	not_null<RpWidget*> parent,
	const DownloadsEntry &entry,
	Fn<void(DownloadsAction)> callback)
: ItemBase(parent, st::defaultMenu)
, _dummyAction(new QAction(parent))
, _progress(this, st::botDownloadProgress)
, _cancel(this, st::botDownloadCancel)
, _height(st::ttlItemPadding.top()
	+ _st.itemStyle.font->height
	+ st::ttlItemTimerFont->height
	+ st::ttlItemPadding.bottom()) {
	setAcceptBoth(true);
	initResizeHook(parent->sizeValue());
	setClickedCallback([=] {
		if (isEnabled()) {
			callback(DownloadsAction::Open);
		}
	});
	_cancel.setClickedCallback([=] {
		callback(DownloadsAction::Cancel);
	});

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	widthValue() | rpl::start_with_next([=](int width) {
		_progress.moveToLeft(
			_st.itemPadding.left(),
			st::ttlItemPadding.top() + _st.itemStyle.font->height,
			width);

		_cancel.moveToRight(
			_st.itemPadding.right(),
			(_height - _cancel.height()) / 2,
			width);
	}, lifetime());

	_progress.setClickHandlerFilter([=](const auto &...) {
		callback(DownloadsAction::Retry);
		return false;
	});

	enableMouseSelecting();
	refresh(entry);
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
	_name.drawLeftElided(
		p,
		_st.itemPadding.left(),
		st::ttlItemPadding.top(),
		_textWidth,
		width());

	_progress.setTextColorOverride(
		selected ? _st.itemFgShortcutOver->c : _st.itemFgShortcut->c);
}

void Action::prepare() {
	const auto filenameWidth = _name.maxWidth();
	const auto progressWidth = _progress.textMaxWidth();
	const auto &padding = _st.itemPadding;

	const auto goodWidth = std::max(filenameWidth, progressWidth);

	// Example max width: "4000 / 4000 MB"
	const auto countWidth = [&](const QString &text) {
		return st::ttlItemTimerFont->width(text);
	};
	const auto maxProgressWidth = countWidth(tr::lng_media_save_progress(
		tr::now,
		lt_ready,
		"0000",
		lt_total,
		"0000",
		lt_mb,
		"MB"));
	const auto maxStartingWidth = countWidth(
		tr::lng_bot_download_starting(tr::now));
	const auto maxFailedWidth = countWidth(tr::lng_bot_download_failed(
		tr::now,
		lt_retry,
		tr::lng_bot_download_retry(tr::now)));

	const auto cancel = _cancel.width() + padding.right();
	const auto paddings = padding.left() + padding.right() + cancel;
	const auto w = std::clamp(
		paddings + std::max({
			goodWidth,
			maxProgressWidth,
			maxStartingWidth,
			maxFailedWidth,
		}),
		_st.widthMin,
		_st.widthMax);
	_textWidth = w - paddings;
	_progress.resizeToWidth(_textWidth);
	setMinWidth(w);
	update();
}

bool Action::isEnabled() const {
	return _entry.total > 0 && _entry.ready == _entry.total;
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

void Action::refresh(const DownloadsEntry &entry) {
	_entry = entry;
	const auto filename = entry.path.split('/').last();
	_name.setMarkedText(_st.itemStyle, { filename }, kDefaultTextOptions);

	const auto progressText = (entry.total && entry.total == entry.ready)
		? TextWithEntities{ FormatSizeText(entry.total) }
		: entry.loading
		? (entry.total
			? TextWithEntities{
				FormatProgressText(entry.ready, entry.total),
			}
			: tr::lng_bot_download_starting(tr::now, Text::WithEntities))
		: tr::lng_bot_download_failed(
			tr::now,
			lt_retry,
			Text::Link(tr::lng_bot_download_retry(tr::now)),
			Text::WithEntities);
	_progress.setMarkedText(progressText);

	const auto enabled = isEnabled();
	setCursor(enabled ? style::cur_pointer : style::cur_default);
	_cancel.setVisible(!enabled && _entry.loading);
	_progress.setAttribute(Qt::WA_TransparentForMouseEvents, enabled);

	prepare();
}

} // namespace

FnMut<void(not_null<PopupMenu*>)> FillAttachBotDownloadsSubmenu(
		rpl::producer<std::vector<DownloadsEntry>> content,
		Fn<void(uint32, DownloadsAction)> callback) {
	return [callback, moved = std::move(content)](
			not_null<PopupMenu*> menu) mutable {
		struct Row {
			not_null<Action*> action;
			uint32 id = 0;
		};
		struct State {
			std::vector<Row> rows;
		};
		const auto state = menu->lifetime().make_state<State>();
		std::move(
			moved
		) | rpl::start_with_next([=](
			const std::vector<DownloadsEntry> &entries) {
			auto found = base::flat_set<uint32>();
			for (const auto &entry : entries | ranges::views::reverse) {
				const auto id = entry.id;
				const auto path = entry.path;
				const auto i = ranges::find(state->rows, id, &Row::id);
				found.emplace(id);

				if (i != end(state->rows)) {
					i->action->refresh(entry);
				} else {
					auto action = base::make_unique_q<Action>(
						menu,
						entry,
						[=](DownloadsAction type) { callback(id, type); });
					state->rows.push_back({
						.action = action.get(),
						.id = id,
						});
					menu->addAction(std::move(action));
				}
			}
			for (auto i = begin(state->rows); i != end(state->rows);) {
				if (!found.contains(i->id)) {
					menu->removeAction(i - begin(state->rows));
					i = state->rows.erase(i);
				} else {
					++i;
				}
			}
		}, menu->lifetime());
	};
}

} // namespace Ui::BotWebView
