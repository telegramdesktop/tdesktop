/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "support/support_autocomplete.h"

#include "ui/widgets/scroll_area.h"
#include "ui/widgets/input_fields.h"
#include "ui/wrap/padding_wrap.h"
#include "support/support_templates.h"
#include "auth_session.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"

namespace Support {
namespace {

class Inner : public Ui::RpWidget {
public:
	Inner(QWidget *parent);

	using Question = details::TemplatesQuestion;
	void showRows(std::vector<Question> &&rows);

	std::pair<int, int> moveSelection(int delta);

	std::optional<Question> selected() const;

	auto activated() const {
		return _activated.events();
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	struct Row {
		Question data;
		Text question = { st::windowMinWidth / 2 };
		Text keys = { st::windowMinWidth / 2 };
		Text answer = { st::windowMinWidth / 2 };
		int top = 0;
		int height = 0;
	};

	void prepareRow(Row &row);
	int resizeRowGetHeight(Row &row, int newWidth);
	void setSelected(int selected);

	std::vector<Row> _rows;
	int _selected = -1;
	int _pressed = -1;
	bool _selectByKeys = false;
	rpl::event_stream<> _activated;

};

Inner::Inner(QWidget *parent) : RpWidget(parent) {
	setMouseTracking(true);
}

void Inner::showRows(std::vector<Question> &&rows) {
	_rows.resize(0);
	_rows.reserve(rows.size());
	for (auto &row : rows) {
		_rows.push_back({ std::move(row) });
		auto &added = _rows.back();
		prepareRow(added);
	}
	resizeToWidth(width());
	update();
	_selected = _pressed = -1;
}

std::pair<int, int> Inner::moveSelection(int delta) {
	const auto selected = _selected + delta;
	if (selected >= 0 && selected < _rows.size()) {
		_selectByKeys = true;
		setSelected(selected);
		const auto top = _rows[_selected].top;
		return { top, top + _rows[_selected].height };
	}
	return { -1, -1 };
}

auto Inner::selected() const -> std::optional<Question> {
	if (_rows.empty()) {
		return std::nullopt;
	} else if (_selected < 0) {
		return _rows[0].data;
	}
	return _rows[_selected].data;
}

void Inner::prepareRow(Row &row) {
	row.question.setText(st::autocompleteRowTitle, row.data.question);
	row.keys.setText(
		st::autocompleteRowKeys,
		row.data.keys.join(qstr(", ")));
	row.answer.setText(st::autocompleteRowAnswer, row.data.value);
}

int Inner::resizeRowGetHeight(Row &row, int newWidth) {
	const auto available = newWidth
		- st::autocompleteRowPadding.left()
		- st::autocompleteRowPadding.right();
	return row.height = st::autocompleteRowPadding.top()
		+ row.question.countHeight(available)
		+ row.keys.countHeight(available)
		+ row.answer.countHeight(available)
		+ st::autocompleteRowPadding.bottom()
		+ st::lineWidth;
}

int Inner::resizeGetHeight(int newWidth) {
	auto top = 0;
	for (auto &row : _rows) {
		row.top = top;
		top += resizeRowGetHeight(row, newWidth);
	}
	return top ? (top - st::lineWidth) : (3 * st::mentionHeight);
}

void Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_rows.empty()) {
		p.setFont(st::boxTextFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect(),
			"Search by question, keys or value",
			style::al_center);
		return;
	}

	const auto clip = e->rect();
	const auto from = ranges::upper_bound(
		_rows,
		clip.y(),
		std::less<>(),
		[](const Row &row) { return row.top + row.height; });
	const auto till = ranges::lower_bound(
		_rows,
		clip.y() + clip.height(),
		std::less<>(),
		[](const Row &row) { return row.top; });
	if (from == end(_rows)) {
		return;
	}
	p.translate(0, from->top);
	const auto padding = st::autocompleteRowPadding;
	const auto available = width() - padding.left() - padding.right();
	auto top = padding.top();
	const auto drawText = [&](const Text &text) {
		text.drawLeft(
			p,
			padding.left(),
			top,
			available,
			width());
		top += text.countHeight(available);
	};
	for (auto i = from; i != till; ++i) {
		const auto over = (i - begin(_rows) == _selected);
		if (over) {
			p.fillRect(0, 0, width(), i->height, st::windowBgOver);
		}
		p.setPen(st::mentionNameFg);
		drawText(i->question);
		p.setPen(over ? st::mentionFgOver : st::mentionFg);
		drawText(i->keys);
		p.setPen(st::windowFg);
		drawText(i->answer);

		p.translate(0, i->height);
		top = padding.top();

		if (i - begin(_rows) + 1 == _selected) {
			p.fillRect(
				0,
				-st::lineWidth,
				width(),
				st::lineWidth,
				st::windowBgOver);
		} else if (!over) {
			p.fillRect(
				padding.left(),
				-st::lineWidth,
				available,
				st::lineWidth,
				st::shadowFg);
		}
	}
}

void Inner::mouseMoveEvent(QMouseEvent *e) {
	static auto lastGlobalPos = QPoint();
	const auto moved = (e->globalPos() != lastGlobalPos);
	if (!moved && _selectByKeys) {
		return;
	}
	_selectByKeys = false;
	lastGlobalPos = e->globalPos();
	const auto i = ranges::upper_bound(
		_rows,
		e->pos().y(),
		std::less<>(),
		[](const Row &row) { return row.top + row.height; });
	setSelected((i == end(_rows)) ? -1 : (i - begin(_rows)));
}

void Inner::leaveEventHook(QEvent *e) {
	setSelected(-1);
}

void Inner::setSelected(int selected) {
	if (_selected != selected) {
		_selected = selected;
			update();
	}
}

void Inner::mousePressEvent(QMouseEvent *e) {
	_pressed = _selected;
}

void Inner::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = base::take(_pressed);
	if (pressed == _selected && pressed >= 0) {
		_activated.fire({});
	}
}

} // namespace

Autocomplete::Autocomplete(QWidget *parent, not_null<AuthSession*> session)
: RpWidget(parent)
, _session(session) {
	setupContent();
}

void Autocomplete::activate() {
	_activate();
}

void Autocomplete::deactivate() {
	_deactivate();
}

void Autocomplete::setBoundings(QRect rect) {
	const auto maxHeight = int(4.5 * st::mentionHeight);
	const auto height = std::min(rect.height(), maxHeight);
	setGeometry(
		rect.x(),
		rect.y() + rect.height() - height,
		rect.width(),
		height);
}

rpl::producer<QString> Autocomplete::insertRequests() {
	return _insertRequests.events();
}

void Autocomplete::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Up) {
		_moveSelection(-1);
	} else if (e->key() == Qt::Key_Down) {
		_moveSelection(1);
	}
}

void Autocomplete::setupContent() {
	const auto inputWrap = Ui::CreateChild<Ui::PaddingWrap<Ui::InputField>>(
		this,
		object_ptr<Ui::InputField>(
			this,
			st::gifsSearchField,
			[] { return "Search for templates"; }),
		st::autocompleteSearchPadding);
	const auto input = inputWrap->entity();
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		this,
		st::mentionScroll);

	const auto inner = scroll->setOwnedWidget(object_ptr<Inner>(scroll));

	const auto submit = [=] {
		if (const auto question = inner->selected()) {
			_insertRequests.fire_copy(question->value);
		}
	};

	const auto refresh = [=] {
		inner->showRows(
			_session->supportTemplates()->query(input->getLastText()));
		scroll->scrollToY(0);
	};

	inner->activated() | rpl::start_with_next(submit, lifetime());
	connect(input, &Ui::InputField::blurred, [=] {
		App::CallDelayed(10, this, [=] {
			if (!input->hasFocus()) {
				deactivate();
			}
		});
	});
	connect(input, &Ui::InputField::cancelled, [=] { deactivate(); });
	connect(input, &Ui::InputField::changed, refresh);
	connect(input, &Ui::InputField::submitted, submit);
	input->customUpDown(true);

	_activate = [=] {
		input->setText(QString());
		show();
		input->setFocus();
	};
	_deactivate = [=] {
		hide();
	};
	_moveSelection = [=](int delta) {
		const auto range = inner->moveSelection(delta);
		if (range.second > range.first) {
			scroll->scrollToY(range.first, range.second);
		}
	};

	paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter p(this);
		p.fillRect(
			clip.intersected(QRect(0, st::lineWidth, width(), height())),
			st::mentionBg);
		p.fillRect(
			clip.intersected(QRect(0, 0, width(), st::lineWidth)),
			st::shadowFg);
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		inputWrap->resizeToWidth(size.width());
		inputWrap->moveToLeft(0, st::lineWidth, size.width());
		scroll->setGeometry(
			0,
			inputWrap->height(),
			size.width(),
			size.height() - inputWrap->height() - st::lineWidth);
		inner->resizeToWidth(size.width());
	}, lifetime());
}

} // namespace Support
