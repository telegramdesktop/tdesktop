/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_format_button.h"

#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"

#include "styles/style_export.h"
#include "styles/style_media_player.h"

#include <QtGui/QMouseEvent>

namespace Export::View {
namespace {

class FormatButton final : public Ui::RoundButton {
public:
	using Ui::RoundButton::RoundButton;

protected:
	void mousePressEvent(QMouseEvent *e) override;

};

void FormatButton::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return RoundButton::mousePressEvent(e);
	}
	e->accept();
	clicked(e->modifiers(), e->button());
}

} // namespace

object_ptr<Ui::RoundButton> CreateFormatButton(
		not_null<QWidget*> parent,
		rpl::producer<Output::Format> value,
		Fn<void(Output::Format)> changed) {
	using Format = Output::Format;
	auto result = object_ptr<FormatButton>(
		parent,
		rpl::single(QString()),
		st::exportFormatButton);
	const auto button = result.data();
	button->setPointerCursor(true);
	struct State {
		rpl::variable<Format> value = Format::Html;
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = button->lifetime().make_state<State>();
	state->value = std::move(value);
	button->setText(rpl::combine(
		state->value.value(),
		tr::lng_export_option_html_and_json()
	) | rpl::map([](Format format, const QString &both) {
		const auto text = (format == Format::Html)
			? u"HTML"_q
			: (format == Format::Json)
			? u"JSON"_q
			: both;
		return tr::marked(text).append(
			Ui::Text::IconEmoji(&st::exportFormatDropdownArrow));
	}));
	button->setClickedCallback([=] {
		if (state->menu) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		const auto menu = state->menu.get();
		const auto add = [&](Format format, const QString &text) {
			menu->addAction(text, [=] {
				changed(format);
			}, (state->value.current() == format)
				? &st::mediaPlayerMenuCheck
				: nullptr);
		};
		add(Format::Html, tr::lng_export_option_html(tr::now));
		add(Format::Json, tr::lng_export_option_json(tr::now));
		add(Format::HtmlAndJson, tr::lng_export_option_html_and_json(tr::now));
		menu->setForcedOrigin(Ui::PanelAnimation::Origin::BottomLeft);
		if (menu->prepareGeometryFor(button->mapToGlobal(QPoint()))) {
			const auto shadow = Ui::BoxShadow::ExtendFor(menu->st().shadow);
			menu->move(menu->pos() - QPoint(shadow.left(), shadow.bottom()));
			if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
				menu->menu()->handlePressedOutside(QCursor::pos());
			}
			menu->popupPrepared();
		}
	});
	return result;
}

object_ptr<Ui::RpWidget> CreateFormatAndLocation(
		not_null<QWidget*> parent,
		rpl::producer<Output::Format> value,
		rpl::producer<TextWithEntities> text,
		Fn<void(Output::Format)> changed,
		Fn<void()> chooseFolder) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto row = result.data();
	const auto spacing = row->lifetime().make_state<rpl::variable<QSize>>();
	const auto before = Ui::CreateChild<Ui::FlatLabel>(
		row,
		st::exportLocationLabel);
	const auto after = Ui::CreateChild<Ui::FlatLabel>(
		row,
		st::exportLocationLabel);
	auto owned = CreateFormatButton(row, std::move(value), std::move(changed));
	const auto button = owned.release();
	before->overrideLinkClickHandler(chooseFolder);
	after->overrideLinkClickHandler(std::move(chooseFolder));
	std::move(text) | rpl::on_next([=](const TextWithEntities &text) {
		const auto position = text.text.indexOf(QChar(0xFFFC));
		Expects(position >= 0);
		const auto space = st::exportLocationLabel.style.font->spacew;
		*spacing = QSize(
			(position > 0 && text.text[position - 1].isSpace()) ? space : 0,
			(position + 1 < text.text.size()
				&& text.text[position + 1].isSpace()) ? space : 0);
		before->setMarkedText(Ui::Text::Mid(text, 0, position));
		after->setMarkedText(Ui::Text::Mid(text, position + 1));
	}, row->lifetime());
	rpl::combine(
		row->widthValue(),
		before->naturalWidthValue(),
		after->naturalWidthValue(),
		button->sizeValue(),
		spacing->value()
	) | rpl::on_next([=](
			int width,
			int left,
			int right,
			QSize size,
			QSize spacing) {
		const auto available = std::max(
			width - size.width() - spacing.width() - spacing.height(),
			0);
		const auto beforeWidth = std::min(left, available);
		before->resizeToWidth(beforeWidth);
		after->resizeToWidth(std::min(right, available - beforeWidth));
		const auto height = std::max({
			before->height(),
			after->height(),
			size.height(),
		});
		const auto buttonTop = (height - size.height()) / 2
			+ st::exportFormatButtonTop;
		row->resize(width, std::max(height, buttonTop + size.height()));
		before->moveToLeft(0, (height - before->height()) / 2, width);
		button->moveToLeft(
			beforeWidth + spacing.width(),
			buttonTop,
			width);
		after->moveToLeft(
			beforeWidth + spacing.width() + size.width() + spacing.height(),
			(height - after->height()) / 2,
			width);
	}, row->lifetime());
	return result;
}

} // namespace Export::View
