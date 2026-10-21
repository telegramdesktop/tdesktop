/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/keys_legend.h"

#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "core/shortcuts.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "ui/controls/keys_legend.h"
#include "ui/layers/layer_widget.h"
#include "ui/painter.h"
#include "ui/round_rect.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "window/window_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_keys_legend.h"
#include "styles/style_layers.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>

namespace {

using namespace Ui::KeysLegend;

using Command = ::Shortcuts::Command;
using KeysMap = base::flat_map<QKeySequence, base::flat_set<Command>>;

[[nodiscard]] QString MiddleDragKey() {
	return tr::lng_photo_editor_keys_middle_drag(tr::now);
}

[[nodiscard]] QString MiddleClickKey() {
	return tr::lng_photo_editor_keys_middle_click(tr::now);
}

[[nodiscard]] QString DoubleClickKey() {
	return tr::lng_photo_editor_keys_double_click(tr::now);
}

[[nodiscard]] bool IsModifierGlyph(QChar ch) {
	for (const auto glyph : { 0x2318, 0x2303, 0x2325, 0x21E7 }) {
		if (ch.unicode() == glyph) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QStringList SplitDisplayString(const QString &text) {
	auto list = QStringList();
	auto index = 0;
	while (true) {
		const auto next = text.indexOf(QChar(0x2B), index);
		if (next < 0 || next + 1 == text.size()) {
			break;
		}
		list << text.mid(index, next - index);
		index = next + 1;
	}
	list << text.mid(index);
	return list;
}

[[nodiscard]] QStringList CommandCaps(
		const KeysMap &currents,
		Command command) {
	for (const auto &[keys, commands] : currents) {
		if (commands.contains(command)) {
			auto text = Shortcuts::ToDisplayString(keys);
			if (!Platform::IsMac()) {
				return SplitDisplayString(text);
			}
			auto list = QStringList();
			auto index = 0;
			while (index < text.size()
				&& IsModifierGlyph(text.at(index))) {
				list << QString(text.at(index));
				++index;
			}
			if (index < text.size()) {
				list << text.mid(index);
			}
			return list;
		}
	}
	return {};
}

[[nodiscard]] QStringList GroupCaps(
		const KeysMap &currents,
		std::vector<Command> commands) {
	auto parts = std::vector<QStringList>();
	parts.reserve(commands.size());
	for (const auto command : commands) {
		auto caps = CommandCaps(currents, command);
		if (!caps.isEmpty()) {
			parts.push_back(std::move(caps));
		}
	}
	if (parts.empty()) {
		return {};
	}
	auto shared = 0;
	while (true) {
		auto same = true;
		for (const auto &part : parts) {
			if (shared + 1 >= part.size()
				|| part[shared] != parts[0][shared]) {
				same = false;
				break;
			}
		}
		if (!same) {
			break;
		}
		++shared;
	}
	auto result = QStringList(parts[0].mid(0, shared));
	auto tails = QStringList();
	tails.reserve(parts.size());
	for (const auto &part : parts) {
		const auto tail = part.mid(shared).join(u"+"_q);
		if (!tails.contains(tail)) {
			tails << tail;
		}
	}
	const auto singleDigit = [](const QString &tail) {
		return tail.size() == 1 && tail[0].isDigit();
	};
	for (auto i = 0; i < tails.size();) {
		auto j = i + 1;
		if (singleDigit(tails[i])) {
			while (j < tails.size()
				&& singleDigit(tails[j])
				&& tails[j][0].unicode() == tails[j - 1][0].unicode() + 1) {
				++j;
			}
		}
		if (j - i >= 3) {
			result << (tails[i] + QChar(0x2026) + tails[j - 1]);
		} else {
			for (auto k = i; k < j; ++k) {
				result << tails[k];
			}
		}
		i = j;
	}
	return result;
}

[[nodiscard]] std::vector<Section> Sections(const KeysMap &currents) {
	const auto caps = [&](Command command) {
		return CommandCaps(currents, command);
	};
	auto result = std::vector<Section>();
	result.push_back({
		.rows = {
			{
				{ ShiftKey(), u"?"_q },
				tr::lng_keys_legend_open(tr::now),
			},
		},
		.stickToPrevious = true,
	});
	result.push_back({
		.title = tr::lng_keys_legend_section_general(tr::now),
		.rows = {
			{ caps(Command::Close), tr::lng_shortcuts_close(tr::now) },
			{ caps(Command::Lock), tr::lng_shortcuts_lock(tr::now) },
			{
				caps(Command::Minimize),
				tr::lng_shortcuts_minimize(tr::now),
			},
			{ caps(Command::Quit), tr::lng_shortcuts_quit(tr::now) },
			{ caps(Command::Search), tr::lng_shortcuts_search(tr::now) },
			{
				caps(Command::ShowContacts),
				tr::lng_shortcuts_contacts(tr::now),
			},
		},
	});
	result.push_back({
		.title = tr::lng_keys_legend_section_chats(tr::now),
		.rows = {
			{
				caps(Command::ChatPrevious),
				tr::lng_shortcuts_chat_previous(tr::now),
			},
			{
				caps(Command::ChatNext),
				tr::lng_shortcuts_chat_next(tr::now),
			},
			{
				GroupCaps(currents, {
					Command::ChatPinned1,
					Command::ChatPinned2,
					Command::ChatPinned3,
					Command::ChatPinned4,
					Command::ChatPinned5,
					Command::ChatPinned6,
					Command::ChatPinned7,
					Command::ChatPinned8,
				}),
				tr::lng_keys_legend_pinned_chats(tr::now),
			},
			{
				caps(Command::FolderPrevious),
				tr::lng_shortcuts_folder_previous(tr::now),
			},
			{
				caps(Command::FolderNext),
				tr::lng_shortcuts_folder_next(tr::now),
			},
			{
				GroupCaps(currents, ::Shortcuts::kShowFolder),
				tr::lng_keys_legend_folder_numbers(tr::now),
			},
			{
				caps(Command::ShowArchive),
				tr::lng_shortcuts_archive(tr::now),
			},
			{
				caps(Command::ChatSelf),
				tr::lng_shortcuts_chat_self(tr::now),
			},
			{
				caps(Command::ShowChatMenu),
				tr::lng_shortcuts_show_chat_menu(tr::now),
			},
			{
				caps(Command::ReadChat),
				tr::lng_shortcuts_read_chat(tr::now),
			},
			{
				{ ControlKey(), u"Tab"_q },
				tr::lng_keys_legend_chat_switcher(tr::now),
			},
			{
				{ CtrlKey(), tr::lng_keys_legend_click(tr::now) },
				tr::lng_keys_legend_new_window(tr::now),
			},
			{
				{ AltKey(), tr::lng_keys_legend_click(tr::now) },
				tr::lng_keys_legend_quick_preview(tr::now),
			},
		},
	});
	result.push_back({
		.title = tr::lng_keys_legend_section_media(tr::now),
		.rows = {
			{
				{ SpaceKey() },
				tr::lng_keys_legend_media_play(tr::now),
			},
			{
				{ SpaceKey() },
				tr::lng_keys_legend_media_2x_hold(tr::now),
			},
			{
				{ u"\u2190"_q, u"\u2192"_q },
				tr::lng_keys_legend_media_nav(tr::now),
			},
			{
				{ EscKey() },
				tr::lng_keys_legend_media_escape(tr::now),
			},
			{
				{ CtrlKey(), u"F"_q },
				tr::lng_keys_legend_media_fullscreen(tr::now),
			},
			{
				{ u"J"_q, u"L"_q },
				tr::lng_keys_legend_media_seek(tr::now),
			},
			{
				{ u"K"_q },
				tr::lng_keys_legend_media_pause(tr::now),
			},
			{
				{ u","_q, u"."_q },
				tr::lng_keys_legend_media_frame(tr::now),
			},
			{
				{ u"H"_q, u"V"_q },
				tr::lng_keys_legend_media_flip(tr::now),
			},
			{
				{ CtrlKey(), tr::lng_keys_legend_wheel(tr::now) },
				tr::lng_keys_legend_media_zoom(tr::now),
			},
			{
				{ CtrlKey(), u"0"_q },
				tr::lng_keys_legend_media_zoom_reset(tr::now),
			},
			{
				{ CtrlKey(), u"S"_q },
				tr::lng_keys_legend_media_save(tr::now),
			},
			{
				{ MiddleClickKey() },
				tr::lng_keys_legend_media_fit(tr::now),
			},
			{
				{ DoubleClickKey() },
				tr::lng_keys_legend_media_double(tr::now),
			},
			{
				{ tr::lng_keys_legend_click(tr::now) },
				tr::lng_keys_legend_media_speed_scrub(tr::now),
			},
			{
				{ AltKey(), u"\u2190"_q },
				tr::lng_keys_legend_media_chapter_prev(tr::now),
			},
			{
				{ AltKey(), u"\u2192"_q },
				tr::lng_keys_legend_media_chapter_next(tr::now),
			},
		},
	});
	result.push_back({
		.title = tr::lng_keys_legend_section_fullscreen(tr::now),
		.rows = {
			{
				{ u"\u2190"_q, u"\u2192"_q },
				tr::lng_keys_legend_media_seek(tr::now),
			},
			{
				{ u"0\u20269"_q },
				tr::lng_keys_legend_media_percent(tr::now),
			},
			{
				{ EscKey() },
				tr::lng_keys_legend_media_fullscreen_escape(tr::now),
			},
		},
		.stickToPrevious = true,
	});
	result.push_back({
		.title = tr::lng_keys_legend_section_messages(tr::now),
		.rows = {
			{
				{ ShiftKey(), u"\u2190/\u2192"_q },
				tr::lng_keys_legend_extend_selection(tr::now),
			},
			{
				{ MiddleDragKey() },
				tr::lng_keys_legend_autoscroll(tr::now),
			},
			{
				{ AltKey(), tr::lng_keys_legend_drag(tr::now) },
				tr::lng_keys_legend_alt_text_select(tr::now),
			},
		},
	});
	result.push_back({
		.title = tr::lng_keys_legend_section_compose(tr::now),
		.rows = {
			{
				{ u"\u2191"_q },
				tr::lng_keys_legend_edit_last(tr::now),
			},
			{
				{ CtrlKey(), u"\u2191"_q },
				tr::lng_keys_legend_reply_prev(tr::now),
			},
			{
				{ CtrlKey(), u"\u2193"_q },
				tr::lng_keys_legend_reply_next(tr::now),
			},
			{
				{ CtrlKey(), u"O"_q },
				tr::lng_keys_legend_attach(tr::now),
			},
			{
				{ CtrlKey(), ShiftKey(), u"V"_q },
				tr::lng_keys_legend_paste_plain(tr::now),
			},
			{
				caps(Command::StashMessage),
				tr::lng_shortcuts_stash_message(tr::now),
			},
			{
				caps(Command::RecordVoice),
				tr::lng_shortcuts_record_voice_message(tr::now),
			},
		},
	});
	result.push_back({
		.title = tr::lng_keys_legend_section_iv(tr::now),
		.rows = {
			{
				{ CtrlKey(), u"+/\u2212"_q },
				tr::lng_keys_legend_iv_zoom(tr::now),
			},
			{
				{ CtrlKey(), u"0"_q },
				tr::lng_keys_legend_iv_zoom_reset(tr::now),
			},
			{
				{ CtrlKey(), u"F"_q },
				tr::lng_keys_legend_iv_search(tr::now),
			},
			{
				{ EscKey() },
				tr::lng_keys_legend_iv_close(tr::now),
			},
		},
	});
	for (auto &section : result) {
		auto &rows = section.rows;
		rows.erase(
			std::remove_if(
				rows.begin(),
				rows.end(),
				[](const Row &row) { return row.keys.isEmpty(); }),
			rows.end());
	}
	return result;
}

[[nodiscard]] St PanelSt() {
	return {
		.titleStyle = &st::keysLegendTitleStyle,
		.titleFg = &st::keysLegendTitleFg,
		.titleSkip = st::keysLegendTitleSkip,
		.rowSkip = st::keysLegendRowSkip,
		.capStyle = &st::keysLegendCapStyle,
		.capHeight = st::keysLegendCapHeight,
		.capMinWidth = st::keysLegendCapMinWidth,
		.capMaxWidth = st::keysLegendCapMaxWidth,
		.capPadding = st::keysLegendCapPadding,
		.capRadius = st::keysLegendCapRadius,
		.capSkip = st::keysLegendCapSkip,
		.capBg = &st::keysLegendCapBg,
		.capBorder = &st::keysLegendCapBorder,
		.capFg = &st::keysLegendCapFg,
		.textSkip = st::keysLegendTextSkip,
		.textWidth = st::keysLegendTextWidth,
		.textMinWidth = st::keysLegendTextMinWidth,
		.textFg = &st::keysLegendTextFg,
	};
}

class Columns final : public Ui::RpWidget {
public:
	Columns(
		not_null<QWidget*> parent,
		std::vector<Section> sections);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct PreparedRow {
		std::vector<Cap> caps;
		Ui::Text::String text;
	};
	struct PreparedSection {
		Ui::Text::String title;
		std::vector<PreparedRow> rows;
		bool stickToPrevious = false;
		int height = 0;
		int column = 0;
		int top = 0;
	};

	const std::vector<Section> _sections;
	std::vector<PreparedSection> _prepared;
	int _capsWidth = 0;
	int _columns = 1;
	int _columnWidth = 0;
	int _textWidth = 0;
	int _height = 0;

};

Columns::Columns(
		not_null<QWidget*> parent,
		std::vector<Section> sections)
: RpWidget(parent)
, _sections(std::move(sections)) {
	const auto st = PanelSt();
	for (const auto &section : _sections) {
		for (const auto &row : section.rows) {
			auto width = 0;
			for (const auto &key : row.keys) {
				width += MakeCap(key, st).width + st.capSkip;
			}
			_capsWidth = std::max(_capsWidth, width - st.capSkip);
		}
	}
	_prepared.reserve(_sections.size());
	for (const auto &section : _sections) {
		auto prepared = PreparedSection{
			.title = Ui::Text::String(*st.titleStyle, section.title),
			.stickToPrevious = section.stickToPrevious,
		};
		prepared.rows.reserve(section.rows.size());
		for (const auto &row : section.rows) {
			auto caps = std::vector<Cap>();
			caps.reserve(row.keys.size());
			for (const auto &key : row.keys) {
				caps.push_back(MakeCap(key, st));
			}
			prepared.rows.push_back({
				.caps = std::move(caps),
				.text = Ui::Text::String(
					st::defaultTextStyle,
					row.text,
					kDefaultTextOptions,
					st.textMinWidth),
			});
		}
		_prepared.push_back(std::move(prepared));
	}
}

int Columns::resizeGetHeight(int newWidth) {
	const auto st = PanelSt();
	const auto padding = st::keysLegendPanelPadding;
	const auto gap = st::keysLegendColumnGap;
	const auto avail = newWidth - 2 * padding;
	const auto count = std::clamp(
		(avail + gap) / (st::keysLegendColumnMin + gap),
		1,
		3);
	const auto columnWidth = std::max(
		(avail - (count - 1) * gap) / count,
		1);
	const auto textWidth = std::clamp(
		columnWidth - _capsWidth - st.textSkip,
		st.textMinWidth,
		st.textWidth);

	auto heights = std::vector<int>(count, 0);
	for (auto i = 0; i < int(_prepared.size()); ++i) {
		auto &section = _prepared[i];
		auto height = section.title.minHeight() + st.titleSkip;
		for (const auto &row : section.rows) {
			height += std::max(
				CapsMaxHeight(row.caps, st.capHeight),
				row.text.countHeight(textWidth)
			) + st.rowSkip;
		}
		section.height = height - st.rowSkip;
		auto column = 0;
		if (section.stickToPrevious && i > 0) {
			column = _prepared[i - 1].column;
		} else {
			for (auto j = 1; j < count; ++j) {
				if (heights[j] < heights[column]) {
					column = j;
				}
			}
		}
		section.column = column;
		section.top = heights[column]
			+ (heights[column] > 0 ? st::keysLegendSectionSkip : 0);
		heights[column] = section.top + section.height;
	}
	_columns = count;
	_columnWidth = columnWidth;
	_textWidth = textWidth;
	_height = heights.empty()
		? 2 * padding
		: (*std::max_element(heights.begin(), heights.end()) + padding);
	return _height;
}

void Columns::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto st = PanelSt();
	const auto padding = st::keysLegendPanelPadding;
	const auto gap = st::keysLegendColumnGap;
	for (const auto &section : _prepared) {
		const auto left = padding
			+ section.column * (_columnWidth + gap);
		auto top = section.top;
		p.setPen(*st.titleFg);
		section.title.draw(p, left, top, _columnWidth);
		top += section.title.minHeight() + st.titleSkip;
		for (const auto &row : section.rows) {
			const auto textHeight = row.text.countHeight(_textWidth);
			const auto rowHeight = std::max(
				CapsMaxHeight(row.caps, st.capHeight),
				textHeight);
			auto capLeft = left;
			for (const auto &cap : row.caps) {
				PaintCap(
					p,
					cap,
					capLeft,
					top + (rowHeight - cap.height) / 2,
					st);
				capLeft += cap.width + st.capSkip;
			}
			p.setPen(*st.textFg);
			row.text.draw(
				p,
				left + _capsWidth + st.textSkip,
				top + (rowHeight - textHeight) / 2,
				_textWidth);
			top += rowHeight + st.rowSkip;
		}
	}
}

class KeysLegendLayer final : public Ui::LayerWidget {
public:
	explicit KeysLegendLayer(std::vector<Section> sections);

protected:
	void parentResized() override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	Ui::ScrollArea *_scroll = nullptr;
	Columns *_columns = nullptr;
	Ui::IconButton *_close = nullptr;
	Ui::RoundRect _roundRect{ st::boxRadius, st::windowBg };

};

KeysLegendLayer::KeysLegendLayer(std::vector<Section> sections)
: _scroll(Ui::CreateChild<Ui::ScrollArea>(this, st::boxScroll))
, _columns(_scroll->setOwnedWidget(
	object_ptr<Columns>(_scroll, std::move(sections))))
, _close(Ui::CreateChild<Ui::IconButton>(this, st::boxTitleClose)) {
	_close->setClickedCallback([=] { closeLayer(); });
}

void KeysLegendLayer::parentResized() {
	const auto parentSize = parentWidget()->size();
	const auto margin = (parentSize.width() < st::keysLegendCompactWidth)
		? 0
		: st::keysLegendOuterMargin;
	const auto padding = st::keysLegendPanelPadding;
	const auto gap = st::keysLegendColumnGap;
	const auto maxWidthFor = [&](int columns) {
		return 2 * padding
			+ columns * st::keysLegendColumnMax
			+ (columns - 1) * gap;
	};
	auto width = std::min(
		parentSize.width() - 2 * margin,
		maxWidthFor(3));
	const auto count = std::clamp(
		(width - 2 * padding + gap) / (st::keysLegendColumnMin + gap),
		1,
		3);
	width = std::min(width, maxWidthFor(count));
	_columns->resizeToWidth(width);
	const auto fullHeight = st::keysLegendHeaderHeight + _columns->height();
	const auto height = std::min(parentSize.height() - 2 * margin, fullHeight);
	setGeometry(QRect(
		QPoint(
			(parentSize.width() - width) / 2,
			(parentSize.height() - height) / 2),
		QSize(width, height)));
}

void KeysLegendLayer::resizeEvent(QResizeEvent *e) {
	LayerWidget::resizeEvent(e);
	_scroll->setGeometry(
		0,
		st::keysLegendHeaderHeight,
		width(),
		height() - st::keysLegendHeaderHeight);
	_columns->resizeToWidth(_scroll->width());
	_close->moveToRight(
		st::keysLegendCloseSkip,
		(st::keysLegendHeaderHeight - _close->height()) / 2);
}

void KeysLegendLayer::paintEvent(QPaintEvent *e) {
	Painter p(this);
	_roundRect.paint(p, rect());
	p.setPen(st::windowFg);
	p.setFont(st::keysLegendHeaderStyle.font);
	p.drawText(
		QRect(
			st::keysLegendPanelPadding,
			0,
			width() - 2 * st::keysLegendPanelPadding
				- _close->width() - st::keysLegendTextSkip,
			st::keysLegendHeaderHeight),
		tr::lng_keys_legend_title(tr::now),
		Qt::AlignLeft | Qt::AlignVCenter);
}

} // namespace

bool HandleKeysLegendHotkey(
		not_null<Core::Application*> app,
		not_null<QObject*> object,
		QKeyEvent *event) {
	if (event->key() != Qt::Key_Question && event->text() != u"?"_q) {
		return false;
	} else if (event->modifiers()
		& (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
		return false;
	} else if (app->passcodeLocked()) {
		return false;
	}
	const auto widget = object->isWidgetType()
		? static_cast<QWidget*>(object.get())
		: nullptr;
	if (!widget) {
		return false;
	}
	const auto focus = QGuiApplication::focusObject();
	if (focus
		&& (focus->inherits("QLineEdit")
			|| focus->inherits("QTextEdit")
			|| focus->inherits("QPlainTextEdit"))) {
		return false;
	}
	const auto window = app->activeWindow();
	if (!window || widget->window() != window->widget()) {
		return false;
	} else if (event->isAutoRepeat()) {
		return true;
	}
	ShowKeysLegend(window);
	return true;
}

void ShowKeysLegend(not_null<Window::Controller*> window) {
	const auto widget = window->widget();
	const auto existing = [&] {
		for (const auto child : widget->children()) {
			const auto stack = dynamic_cast<Ui::LayerStackWidget*>(child);
			if (!stack) {
				continue;
			}
			for (const auto layer : stack->children()) {
				if (const auto e = dynamic_cast<KeysLegendLayer*>(layer)) {
					return e;
				}
			}
		}
		return static_cast<KeysLegendLayer*>(nullptr);
	}();
	if (existing) {
		existing->closeLayer();
		return;
	}
	window->widget()->showOrHideBoxOrLayer(
		std::make_unique<KeysLegendLayer>(
			Sections(Shortcuts::KeysCurrents())),
		Ui::LayerOption::KeepOther,
		anim::type::normal);
}
