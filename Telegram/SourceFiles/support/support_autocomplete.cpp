/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "support/support_autocomplete.h"

#include "ui/widgets/scroll_area.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/padding_wrap.h"
#include "support/support_templates.h"
#include "support/support_common.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_service_message.h"
#include "history/history_message.h"
#include "lang/lang_keys.h"
#include "data/data_session.h"
#include "base/unixtime.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"

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
		Ui::Text::String question = { st::windowMinWidth / 2 };
		Ui::Text::String keys = { st::windowMinWidth / 2 };
		Ui::Text::String answer = { st::windowMinWidth / 2 };
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

int TextHeight(const Ui::Text::String &text, int available, int lines) {
	Expects(text.style() != nullptr);

	const auto st = text.style();
	const auto line = st->lineHeight ? st->lineHeight : st->font->height;
	return std::min(text.countHeight(available), lines * line);
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
	_selected = _pressed = -1;
	moveSelection(1);
	update();
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
		row.data.originalKeys.join(qstr(", ")));
	row.answer.setText(st::autocompleteRowAnswer, row.data.value);
}

int Inner::resizeRowGetHeight(Row &row, int newWidth) {
	const auto available = newWidth
		- st::autocompleteRowPadding.left()
		- st::autocompleteRowPadding.right();
	return row.height = st::autocompleteRowPadding.top()
		+ TextHeight(row.question, available, 1)
		+ TextHeight(row.keys, available, 1)
		+ TextHeight(row.answer, available, 2)
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
	const auto drawText = [&](const Ui::Text::String &text, int lines) {
		text.drawLeftElided(
			p,
			padding.left(),
			top,
			available,
			width(),
			lines);
		top += TextHeight(text, available, lines);
	};
	for (auto i = from; i != till; ++i) {
		const auto over = (i - begin(_rows) == _selected);
		if (over) {
			p.fillRect(0, 0, width(), i->height, st::windowBgOver);
		}
		p.setPen(st::mentionNameFg);
		drawText(i->question, 1);
		p.setPen(over ? st::mentionFgOver : st::mentionFg);
		drawText(i->keys, 1);
		p.setPen(st::windowFg);
		drawText(i->answer, 2);

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

AdminLog::OwnedItem GenerateCommentItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const Contact &data) {
	if (data.comment.isEmpty()) {
		return nullptr;
	}
	using Flag = MTPDmessage::Flag;
	const auto id = ServerMaxMsgId + (ServerMaxMsgId / 2);
	const auto flags = Flag::f_entities | Flag::f_from_id | Flag::f_out;
	const auto clientFlags = MTPDmessage_ClientFlag::f_fake_history_item;
	const auto replyTo = 0;
	const auto viaBotId = 0;
	const auto item = history->owner().makeMessage(
		history,
		id,
		flags,
		clientFlags,
		replyTo,
		viaBotId,
		base::unixtime::now(),
		history->session().userId(),
		QString(),
		TextWithEntities{ TextUtilities::Clean(data.comment) });
	return AdminLog::OwnedItem(delegate, item);
}

AdminLog::OwnedItem GenerateContactItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		const Contact &data) {
	using Flag = MTPDmessage::Flag;
	const auto id = ServerMaxMsgId + (ServerMaxMsgId / 2) + 1;
	const auto flags = Flag::f_from_id | Flag::f_media | Flag::f_out;
	const auto replyTo = 0;
	const auto viaBotId = 0;
	const auto message = MTP_message(
		MTP_flags(flags),
		MTP_int(id),
		MTP_int(history->session().userId()),
		peerToMTP(history->peer->id),
		MTPMessageFwdHeader(),
		MTP_int(viaBotId),
		MTP_int(replyTo),
		MTP_int(base::unixtime::now()),
		MTP_string(),
		MTP_messageMediaContact(
			MTP_string(data.phone),
			MTP_string(data.firstName),
			MTP_string(data.lastName),
			MTP_string(),
			MTP_int(0)),
		MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(),
		MTP_int(0),
		MTP_int(0),
		MTP_string(),
		MTP_long(0),
		//MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>());
	const auto item = history->owner().makeMessage(
		history,
		message.c_message(),
		MTPDmessage_ClientFlag::f_fake_history_item);
	return AdminLog::OwnedItem(delegate, item);
}

} // namespace

Autocomplete::Autocomplete(QWidget *parent, not_null<Main::Session*> session)
: RpWidget(parent)
, _session(session) {
	setupContent();
}

void Autocomplete::activate(not_null<Ui::InputField*> field) {
	if (_session->settings().supportTemplatesAutocomplete()) {
		_activate();
	} else {
		const auto &templates = _session->supportTemplates();
		const auto max = templates.maxKeyLength();
		auto cursor = field->textCursor();
		const auto position = cursor.position();
		const auto anchor = cursor.anchor();
		const auto text = (position != anchor)
			? field->getTextWithTagsPart(
				std::min(position, anchor),
				std::max(position, anchor))
			: field->getTextWithTagsPart(
				std::max(position - max, 0),
				position);
		const auto result = (position != anchor)
			? templates.matchExact(text.text)
			: templates.matchFromEnd(text.text);
		if (result) {
			const auto till = std::max(position, anchor);
			const auto from = till - result->key.size();
			cursor.setPosition(from);
			cursor.setPosition(till, QTextCursor::KeepAnchor);
			field->setTextCursor(cursor);
			submitValue(result->question.value);
		}
	}
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

rpl::producer<QString> Autocomplete::insertRequests() const {
	return _insertRequests.events();
}

rpl::producer<Contact> Autocomplete::shareContactRequests() const {
	return _shareContactRequests.events();
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
			rpl::single(qsl("Search for templates"))), // #TODO hard_lang
		st::autocompleteSearchPadding);
	const auto input = inputWrap->entity();
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		this,
		st::mentionScroll);

	const auto inner = scroll->setOwnedWidget(object_ptr<Inner>(scroll));

	const auto submit = [=] {
		if (const auto question = inner->selected()) {
			submitValue(question->value);
		}
	};

	const auto refresh = [=] {
		inner->showRows(
			_session->supportTemplates().query(input->getLastText()));
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

void Autocomplete::submitValue(const QString &value) {
	const auto prefix = qstr("contact:");
	if (value.startsWith(prefix)) {
		const auto line = value.indexOf('\n');
		const auto text = (line > 0) ? value.mid(line + 1) : QString();
		const auto commented = !text.isEmpty();
		const auto contact = value.mid(
			prefix.size(),
			(line > 0) ? (line - prefix.size()) : -1);
		const auto parts = contact.split(' ', QString::SkipEmptyParts);
		if (parts.size() > 1) {
			const auto phone = parts[0];
			const auto firstName = parts[1];
			const auto lastName = (parts.size() > 2)
				? QStringList(parts.mid(2)).join(' ')
				: QString();
			_shareContactRequests.fire(Contact{
				text,
				phone,
				firstName,
				lastName });
		}
	} else {
		_insertRequests.fire_copy(value);
	}
}

ConfirmContactBox::ConfirmContactBox(
	QWidget*,
	not_null<History*> history,
	const Contact &data,
	Fn<void(Qt::KeyboardModifiers)> submit)
: _comment(GenerateCommentItem(this, history, data))
, _contact(GenerateContactItem(this, history, data))
, _submit(submit) {
}

void ConfirmContactBox::prepare() {
	setTitle(rpl::single(qsl("Confirmation"))); // #TODO hard_lang

	auto maxWidth = 0;
	if (_comment) {
		_comment->setAttachToNext(true);
		_contact->setAttachToPrevious(true);
		_comment->initDimensions();
		accumulate_max(maxWidth, _comment->maxWidth());
	}
	_contact->initDimensions();
	accumulate_max(maxWidth, _contact->maxWidth());
	maxWidth += st::boxPadding.left() + st::boxPadding.right();
	const auto width = snap(maxWidth, st::boxWidth, st::boxWideWidth);
	const auto available = width
		- st::boxPadding.left()
		- st::boxPadding.right();
	auto height = 0;
	if (_comment) {
		height += _comment->resizeGetHeight(available);
	}
	height += _contact->resizeGetHeight(available);
	setDimensions(width, height);
	_contact->initDimensions();

	_submit = [=, original = std::move(_submit)](Qt::KeyboardModifiers m) {
		const auto weak = make_weak(this);
		original(m);
		if (weak) {
			closeBox();
		}
	};

	const auto button = addButton(tr::lng_send_button(), [] {});
	button->clicks(
	) | rpl::start_with_next([=](Qt::MouseButton which) {
		_submit((which == Qt::RightButton)
			? SkipSwitchModifiers()
			: button->clickModifiers());
	}, button->lifetime());
	button->setAcceptBoth(true);

	addButton(tr::lng_cancel(), [=] { closeBox(); });
}

void ConfirmContactBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		_submit(e->modifiers());
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void ConfirmContactBox::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::boxBg);

	const auto ms = crl::now();
	p.translate(st::boxPadding.left(), 0);
	if (_comment) {
		_comment->draw(p, rect(), TextSelection(), ms);
		p.translate(0, _comment->height());
	}
	_contact->draw(p, rect(), TextSelection(), ms);
}

HistoryView::Context ConfirmContactBox::elementContext() {
	return HistoryView::Context::ContactPreview;
}

} // namespace Support
