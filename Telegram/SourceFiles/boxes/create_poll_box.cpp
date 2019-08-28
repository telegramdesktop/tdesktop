/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/create_poll_box.h"

#include "lang/lang_keys.h"
#include "data/data_poll.h"
#include "ui/toast/toast.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "main/main_session.h"
#include "core/event_filter.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "history/view/history_view_schedule_box.h"
#include "settings/settings_common.h"
#include "base/unique_qptr.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kQuestionLimit = 255;
constexpr auto kMaxOptionsCount = PollData::kMaxOptions;
constexpr auto kOptionLimit = 100;
constexpr auto kWarnQuestionLimit = 80;
constexpr auto kWarnOptionLimit = 30;
constexpr auto kErrorLimit = 99;

class Options {
public:
	Options(
		not_null<QWidget*> outer,
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session);

	[[nodiscard]] bool isValid() const;
	[[nodiscard]] rpl::producer<bool> isValidChanged() const;
	[[nodiscard]] std::vector<PollAnswer> toPollAnswers() const;
	void focusFirst();

	[[nodiscard]] rpl::producer<int> usedCount() const;
	[[nodiscard]] rpl::producer<not_null<QWidget*>> scrollToWidget() const;
	[[nodiscard]] rpl::producer<> backspaceInFront() const;

private:
	class Option {
	public:
		static Option Create(
			not_null<QWidget*> outer,
			not_null<Ui::VerticalLayout*> container,
			not_null<Main::Session*> session,
			int position);

		void toggleRemoveAlways(bool toggled);

		void show(anim::type animated);
		void destroy(FnMut<void()> done);

		//[[nodisacrd]] bool hasShadow() const;
		//void destroyShadow();

		[[nodiscard]] bool isEmpty() const;
		[[nodiscard]] bool isGood() const;
		[[nodiscard]] bool isTooLong() const;
		[[nodiscard]] bool hasFocus() const;
		void setFocus() const;
		void clearValue();

		void setPlaceholder() const;
		void removePlaceholder() const;

		not_null<Ui::InputField*> field() const;

		[[nodiscard]] PollAnswer toPollAnswer(int index) const;

		[[nodiscard]] rpl::producer<Qt::MouseButton> removeClicks() const;

		inline bool operator<(const Option &other) const {
			return field() < other.field();
		}

		friend inline bool operator<(
				const Option &option,
				Ui::InputField *field) {
			return option.field() < field;
		}
		friend inline bool operator<(
				Ui::InputField *field,
				const Option &option) {
			return field < option.field();
		}

	private:
		Option() = default;

		void createShadow();
		void createRemove();
		void createWarning();

		base::unique_qptr<Ui::SlideWrap<Ui::InputField>> _field;
		base::unique_qptr<Ui::PlainShadow> _shadow;
		base::unique_qptr<Ui::CrossButton> _remove;
		rpl::variable<bool> *_removeAlways = nullptr;

	};

	[[nodiscard]] bool full() const;
	//[[nodiscard]] bool correctShadows() const;
	//void fixShadows();
	void removeEmptyTail();
	void addEmptyOption();
	void checkLastOption();
	void validateState();
	void fixAfterErase();
	void destroy(Option &&option);
	void removeDestroyed(not_null<Ui::InputField*> field);
	int findField(not_null<Ui::InputField*> field) const;

	not_null<QWidget*> _outer;
	not_null<Ui::VerticalLayout*> _container;
	const not_null<Main::Session*> _session;
	int _position = 0;
	std::vector<Option> _list;
	std::set<Option, std::less<>> _destroyed;
	rpl::variable<bool> _valid = false;
	rpl::variable<int> _usedCount = 0;
	rpl::event_stream<not_null<QWidget*>> _scrollToWidget;
	rpl::event_stream<> _backspaceInFront;

};

void InitField(
		not_null<QWidget*> container,
		not_null<Ui::InputField*> field,
		not_null<Main::Session*> session) {
	field->setInstantReplaces(Ui::InstantReplaces::Default());
	field->setInstantReplacesEnabled(
		session->settings().replaceEmojiValue());
	auto options = Ui::Emoji::SuggestionsController::Options();
	options.suggestExactFirstWord = false;
	Ui::Emoji::SuggestionsController::Init(
		container,
		field,
		session,
		options);
}

not_null<Ui::FlatLabel*> CreateWarningLabel(
		not_null<QWidget*> parent,
		not_null<Ui::InputField*> field,
		int valueLimit,
		int warnLimit) {
	const auto result = Ui::CreateChild<Ui::FlatLabel>(
		parent.get(),
		QString(),
		st::createPollWarning);
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	QObject::connect(field, &Ui::InputField::changed, [=] {
		Ui::PostponeCall(crl::guard(field, [=] {
			const auto length = field->getLastText().size();
			const auto value = valueLimit - length;
			const auto shown = (value < warnLimit)
				&& (field->height() > st::createPollOptionField.heightMin);
			result->setRichText((value >= 0)
				? QString::number(value)
				: textcmdLink(1, QString::number(value)));
			result->setVisible(shown);
		}));
	});
	return result;
}

void FocusAtEnd(not_null<Ui::InputField*> field) {
	field->setFocus();
	field->setCursorPosition(field->getLastText().size());
	field->ensureCursorVisible();
}

Options::Option Options::Option::Create(
		not_null<QWidget*> outer,
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session,
		int position) {
	auto result = Option();
	const auto field = container->insert(
		position,
		object_ptr<Ui::SlideWrap<Ui::InputField>>(
			container,
			object_ptr<Ui::InputField>(
				container,
				st::createPollOptionField,
				Ui::InputField::Mode::NoNewlines,
				tr::lng_polls_create_option_add())));
	InitField(outer, field->entity(), session);
	field->entity()->setMaxLength(kOptionLimit + kErrorLimit);
	result._field.reset(field);

	result.createShadow();
	result.createRemove();
	result.createWarning();
	return result;
}

//bool Options::Option::hasShadow() const {
//	return (_shadow != nullptr);
//}

void Options::Option::createShadow() {
	Expects(_field != nullptr);

	if (_shadow) {
		return;
	}
	const auto value = Ui::CreateChild<Ui::PlainShadow>(field().get());
	value->show();
	field()->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto left = st::createPollFieldPadding.left();
		value->setGeometry(
			left,
			size.height() - st::lineWidth,
			size.width() - left,
			st::lineWidth);
	}, value->lifetime());
	_shadow.reset(value);
}

//void Options::Option::destroyShadow() {
//	_shadow = nullptr;
//}

void Options::Option::createRemove() {
	using namespace rpl::mappers;

	const auto field = this->field();
	auto &lifetime = field->lifetime();

	const auto remove = Ui::CreateChild<Ui::CrossButton>(
		field.get(),
		st::createPollOptionRemove);
	remove->hide(anim::type::instant);

	const auto toggle = lifetime.make_state<rpl::variable<bool>>(false);
	_removeAlways = lifetime.make_state<rpl::variable<bool>>(false);

	QObject::connect(field, &Ui::InputField::changed, [=] {
		// Don't capture 'this'! Because Option is a value type.
		*toggle = !field->getLastText().isEmpty();
	});
	rpl::combine(
		toggle->value(),
		_removeAlways->value(),
		_1 || _2
	) | rpl::start_with_next([=](bool shown) {
		remove->toggle(shown, anim::type::normal);
	}, remove->lifetime());

	field->widthValue(
	) | rpl::start_with_next([=](int width) {
		remove->moveToRight(
			st::createPollOptionRemovePosition.x(),
			st::createPollOptionRemovePosition.y(),
			width);
	}, remove->lifetime());

	_remove.reset(remove);
}

void Options::Option::createWarning() {
	using namespace rpl::mappers;

	const auto field = this->field();
	const auto warning = CreateWarningLabel(
		field,
		field,
		kOptionLimit,
		kWarnOptionLimit);
	rpl::combine(
		field->sizeValue(),
		warning->sizeValue()
	) | rpl::start_with_next([=](QSize size, QSize label) {
		warning->moveToLeft(
			(size.width()
				- label.width()
				- st::createPollWarningPosition.x()),
			(size.height()
				- label.height()
				- st::createPollWarningPosition.y()),
			size.width());
	}, warning->lifetime());
}

bool Options::Option::isEmpty() const {
	return field()->getLastText().trimmed().isEmpty();
}

bool Options::Option::isGood() const {
	return !field()->getLastText().trimmed().isEmpty() && !isTooLong();
}

bool Options::Option::isTooLong() const {
	return (field()->getLastText().size() > kOptionLimit);
}

bool Options::Option::hasFocus() const {
	return field()->hasFocus();
}

void Options::Option::setFocus() const {
	FocusAtEnd(field());
}

void Options::Option::clearValue() {
	field()->setText(QString());
}

void Options::Option::setPlaceholder() const {
	field()->setPlaceholder(tr::lng_polls_create_option_add());
}

void Options::Option::toggleRemoveAlways(bool toggled) {
	*_removeAlways = toggled;
}

not_null<Ui::InputField*> Options::Option::field() const {
	return _field->entity();
}

void Options::Option::removePlaceholder() const {
	field()->setPlaceholder(rpl::single(QString()));
}

PollAnswer Options::Option::toPollAnswer(int index) const {
	Expects(index >= 0 && index < kMaxOptionsCount);

	return PollAnswer{
		field()->getLastText().trimmed(),
		QByteArray(1, ('0' + index))
	};
}

rpl::producer<Qt::MouseButton> Options::Option::removeClicks() const {
	return _remove->clicks();
}

Options::Options(
	not_null<QWidget*> outer,
	not_null<Ui::VerticalLayout*> container,
	not_null<Main::Session*> session)
: _outer(outer)
, _container(container)
, _session(session)
, _position(_container->count()) {
	checkLastOption();
}

bool Options::full() const {
	return (_list.size() == kMaxOptionsCount);
}

bool Options::isValid() const {
	return _valid.current();
}

rpl::producer<bool> Options::isValidChanged() const {
	return _valid.changes();
}

rpl::producer<int> Options::usedCount() const {
	return _usedCount.value();
}

rpl::producer<not_null<QWidget*>> Options::scrollToWidget() const {
	return _scrollToWidget.events();
}

rpl::producer<> Options::backspaceInFront() const {
	return _backspaceInFront.events();
}

void Options::Option::show(anim::type animated) {
	_field->hide(anim::type::instant);
	_field->show(animated);
}

void Options::Option::destroy(FnMut<void()> done) {
	if (anim::Disabled() || _field->isHidden()) {
		Ui::PostponeCall(std::move(done));
		return;
	}
	_field->hide(anim::type::normal);
	App::CallDelayed(
		st::slideWrapDuration * 2,
		_field.get(),
		std::move(done));
}

std::vector<PollAnswer> Options::toPollAnswers() const {
	auto result = std::vector<PollAnswer>();
	result.reserve(_list.size());
	auto counter = int(0);
	const auto makeAnswer = [&](const Option &option) {
		return option.toPollAnswer(counter++);
	};
	ranges::copy(
		_list
		| ranges::view::filter(&Option::isGood)
		| ranges::view::transform(makeAnswer),
		ranges::back_inserter(result));
	return result;
}

void Options::focusFirst() {
	Expects(!_list.empty());

	_list.front().setFocus();
}
//
//bool Options::correctShadows() const {
//	// Last one should be without shadow if all options were used.
//	const auto noShadow = ranges::find(
//		_list,
//		true,
//		ranges::not_fn(&Option::hasShadow));
//	return (noShadow == end(_list) - (full() ? 1 : 0));
//}
//
//void Options::fixShadows() {
//	if (correctShadows()) {
//		return;
//	}
//	for (auto &option : _list) {
//		option.createShadow();
//	}
//	if (full()) {
//		_list.back().destroyShadow();
//	}
//}

void Options::removeEmptyTail() {
	// Only one option at the end of options list can be empty.
	// Remove all other trailing empty options.
	// Only last empty and previous option have non-empty placeholders.
	const auto focused = ranges::find_if(
		_list,
		&Option::hasFocus);
	const auto end = _list.end();
	const auto reversed = ranges::view::reverse(_list);
	const auto emptyItem = ranges::find_if(
		reversed,
		ranges::not_fn(&Option::isEmpty)).base();
	const auto focusLast = (focused > emptyItem) && (focused < end);
	if (emptyItem == end) {
		return;
	}
	if (focusLast) {
		emptyItem->setFocus();
	}
	for (auto i = emptyItem + 1; i != end; ++i) {
		destroy(std::move(*i));
	}
	_list.erase(emptyItem + 1, end);
	fixAfterErase();
}

void Options::destroy(Option &&option) {
	const auto field = option.field();
	option.destroy([=] { removeDestroyed(field); });
	_destroyed.emplace(std::move(option));
}

void Options::fixAfterErase() {
	Expects(!_list.empty());

	const auto last = _list.end() - 1;
	last->setPlaceholder();
	last->toggleRemoveAlways(false);
	if (last != begin(_list)) {
		(last - 1)->setPlaceholder();
		(last - 1)->toggleRemoveAlways(false);
	}
}

void Options::addEmptyOption() {
	if (full()) {
		return;
	} else if (!_list.empty() && _list.back().isEmpty()) {
		return;
	}
	if (_list.size() > 1) {
		(_list.end() - 2)->removePlaceholder();
		(_list.end() - 2)->toggleRemoveAlways(true);
	}
	_list.push_back(Option::Create(
		_outer,
		_container,
		_session,
		_position + _list.size() + _destroyed.size()));
	const auto field = _list.back().field();
	QObject::connect(field, &Ui::InputField::submitted, [=] {
		const auto index = findField(field);
		if (_list[index].isGood() && index + 1 < _list.size()) {
			_list[index + 1].setFocus();
		}
	});
	QObject::connect(field, &Ui::InputField::changed, [=] {
		Ui::PostponeCall(crl::guard(field, [=] {
			validateState();
		}));
	});
	QObject::connect(field, &Ui::InputField::focused, [=] {
		_scrollToWidget.fire_copy(field);
	});
	Core::InstallEventFilter(field, [=](not_null<QEvent*> event) {
		if (event->type() != QEvent::KeyPress
			|| !field->getLastText().isEmpty()) {
			return false;
		}
		const auto key = static_cast<QKeyEvent*>(event.get())->key();
		if (key != Qt::Key_Backspace) {
			return false;
		}

		const auto index = findField(field);
		if (index > 0) {
			_list[index - 1].setFocus();
		} else {
			_backspaceInFront.fire({});
		}
		return true;
	});

	_list.back().removeClicks(
	) | rpl::start_with_next([=] {
		Ui::PostponeCall(crl::guard(field, [=] {
			Expects(!_list.empty());

			const auto item = begin(_list) + findField(field);
			if (item == _list.end() - 1) {
				item->clearValue();
				return;
			}
			if (item->hasFocus()) {
				(item + 1)->setFocus();
			}
			destroy(std::move(*item));
			_list.erase(item);
			fixAfterErase();
			validateState();
		}));
	}, field->lifetime());

	_list.back().show((_list.size() == 1)
		? anim::type::instant
		: anim::type::normal);
	//fixShadows();
}

void Options::removeDestroyed(not_null<Ui::InputField*> field) {
	_destroyed.erase(_destroyed.find(field));
}

void Options::validateState() {
	checkLastOption();
	_valid = (ranges::count_if(_list, &Option::isGood) > 1)
		&& (ranges::find_if(_list, &Option::isTooLong) == end(_list));
	const auto lastEmpty = !_list.empty() && _list.back().isEmpty();
	_usedCount = _list.size() - (lastEmpty ? 1 : 0);
}

int Options::findField(not_null<Ui::InputField*> field) const {
	const auto result = ranges::find(
		_list,
		field,
		&Option::field) - begin(_list);

	Ensures(result >= 0 && result < _list.size());
	return result;
}

void Options::checkLastOption() {
	removeEmptyTail();
	addEmptyOption();
}

} // namespace

CreatePollBox::CreatePollBox(
	QWidget*,
	not_null<Main::Session*> session,
	Api::SendType sendType)
: _session(session)
, _sendType(sendType) {
}

rpl::producer<CreatePollBox::Result> CreatePollBox::submitRequests() const {
	return _submitRequests.events();
}

void CreatePollBox::setInnerFocus() {
	_setInnerFocus();
}

void CreatePollBox::submitFailed(const QString &error) {
	Ui::Toast::Show(error);
}

not_null<Ui::InputField*> CreatePollBox::setupQuestion(
		not_null<Ui::VerticalLayout*> container) {
	using namespace Settings;

	AddSubsectionTitle(container, tr::lng_polls_create_question());
	const auto question = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::createPollField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_polls_create_question_placeholder()),
		st::createPollFieldPadding);
	InitField(getDelegate()->outerContainer(), question, _session);
	question->setMaxLength(kQuestionLimit + kErrorLimit);

	const auto warning = CreateWarningLabel(
		container,
		question,
		kQuestionLimit,
		kWarnQuestionLimit);
	rpl::combine(
		question->geometryValue(),
		warning->sizeValue()
	) | rpl::start_with_next([=](QRect geometry, QSize label) {
		warning->moveToLeft(
			(container->width()
				- label.width()
				- st::createPollWarningPosition.x()),
			(geometry.y()
				- st::createPollFieldPadding.top()
				- st::settingsSubsectionTitlePadding.bottom()
				- st::settingsSubsectionTitle.style.font->height
				+ st::settingsSubsectionTitle.style.font->ascent
				- st::createPollWarning.style.font->ascent),
			geometry.width());
	}, warning->lifetime());

	return question;
}

object_ptr<Ui::RpWidget> CreatePollBox::setupContent() {
	using namespace Settings;

	const auto id = rand_value<uint64>();
	const auto valid = lifetime().make_state<rpl::event_stream<bool>>();

	auto result = object_ptr<Ui::VerticalLayout>(this);
	const auto container = result.data();

	const auto question = setupQuestion(container);
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_polls_create_options());
	const auto options = lifetime().make_state<Options>(
		getDelegate()->outerContainer(),
		container,
		_session);
	auto limit = options->usedCount() | rpl::after_next([=](int count) {
		setCloseByEscape(!count);
		setCloseByOutsideClick(!count);
	}) | rpl::map([=](int count) {
		return (count < kMaxOptionsCount)
			? tr::lng_polls_create_limit(tr::now, lt_count, kMaxOptionsCount - count)
			: tr::lng_polls_create_maximum(tr::now);
	}) | rpl::after_next([=] {
		container->resizeToWidth(container->widthNoMargins());
	});
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(limit),
			st::createPollLimitLabel),
		st::createPollLimitPadding);

	const auto isValidQuestion = [=] {
		const auto text = question->getLastText().trimmed();
		return !text.isEmpty() && (text.size() <= kQuestionLimit);
	};

	connect(question, &Ui::InputField::submitted, [=] {
		if (isValidQuestion()) {
			options->focusFirst();
		}
	});

	_setInnerFocus = [=] {
		question->setFocusFast();
	};

	const auto collectResult = [=] {
		auto result = PollData(id);
		result.question = question->getLastText().trimmed();
		result.answers = options->toPollAnswers();
		return result;
	};
	const auto send = [=](Api::SendOptions options) {
		_submitRequests.fire({ collectResult(), options });
	};
	const auto sendSilent = [=] {
		auto options = Api::SendOptions();
		options.silent = true;
		send(options);
	};
	const auto sendScheduled = [=] {
		Ui::show(
			HistoryView::PrepareScheduleBox(
				this,
				SendMenuType::Scheduled,
				send),
			LayerOption::KeepOther);
	};
	const auto updateValid = [=] {
		valid->fire(isValidQuestion() && options->isValid());
	};
	connect(question, &Ui::InputField::changed, [=] {
		updateValid();
	});
	valid->events_starting_with(
		false
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool valid) {
		clearButtons();
		if (valid) {
			const auto submit = addButton(
				tr::lng_polls_create_button(),
				[=] { send({}); });
			if (_sendType == Api::SendType::Normal) {
				SetupSendMenu(
					submit.data(),
					[=] { return SendMenuType::Scheduled; },
					sendSilent,
					sendScheduled);
			}
		}
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	}, lifetime());

	options->isValidChanged(
	) | rpl::start_with_next([=] {
		updateValid();
	}, lifetime());

	options->scrollToWidget(
	) | rpl::start_with_next([=](not_null<QWidget*> widget) {
		scrollToWidget(widget);
	}, lifetime());

	options->backspaceInFront(
	) | rpl::start_with_next([=] {
		FocusAtEnd(question);
	}, lifetime());

	return std::move(result);
}

void CreatePollBox::prepare() {
	setTitle(tr::lng_polls_create_title());

	const auto inner = setInnerWidget(setupContent());

	setDimensionsToContent(st::boxWideWidth, inner);
}
