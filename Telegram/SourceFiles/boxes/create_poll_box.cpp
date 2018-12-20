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
#include "ui/widgets/input_fields.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "settings/settings_common.h"
#include "base/unique_qptr.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kQuestionLimit = 255;
constexpr auto kMaxOptionsCount = 10;
constexpr auto kOptionLimit = 100;
constexpr auto kWarnQuestionLimit = 80;
constexpr auto kWarnOptionLimit = 30;

class Options {
public:
	Options(
		not_null<QWidget*> outer,
		not_null<Ui::VerticalLayout*> container);

	[[nodiscard]] bool isValid() const;
	[[nodiscard]] rpl::producer<bool> isValidChanged() const;
	[[nodiscard]] std::vector<PollAnswer> toPollAnswers() const;
	void focusFirst();

	[[nodiscard]] rpl::producer<int> usedCount() const;
	[[nodiscard]] rpl::producer<not_null<QWidget*>> scrollToWidget() const;

private:
	class Option {
	public:
		static Option Create(
			not_null<QWidget*> outer,
			not_null<Ui::VerticalLayout*> container,
			int position);

		[[nodisacrd]] bool hasShadow() const;
		void createShadow();
		//void destroyShadow();

		void createRemove();
		void toggleRemoveAlways(bool toggled);

		[[nodiscard]] bool isEmpty() const;
		[[nodiscard]] bool isGood() const;
		[[nodiscard]] bool hasFocus() const;
		void setFocus() const;
		void clearValue();

		void setPlaceholder() const;
		void removePlaceholder() const;

		not_null<Ui::InputField*> field() const;

		[[nodiscard]] PollAnswer toPollAnswer(char id) const;

		[[nodiscard]] rpl::producer<Qt::MouseButton> removeClicks() const;

	private:
		Option() = default;

		base::unique_qptr<Ui::InputField> _field;
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
	int findField(not_null<Ui::InputField*> field) const;

	not_null<QWidget*> _outer;
	not_null<Ui::VerticalLayout*> _container;
	int _position = 0;
	std::vector<Option> _list;
	rpl::variable<bool> _valid = false;
	rpl::variable<int> _usedCount = 0;
	rpl::event_stream<not_null<QWidget*>> _scrollToWidget;

};

void InitField(
		not_null<QWidget*> container,
		not_null<Ui::InputField*> field) {
	field->setInstantReplaces(Ui::InstantReplaces::Default());
	field->setInstantReplacesEnabled(Global::ReplaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(container, field);
}

Options::Option Options::Option::Create(
		not_null<QWidget*> outer,
		not_null<Ui::VerticalLayout*> container,
		int position) {
	auto result = Option();
	const auto field = container->insert(
		position,
		object_ptr<Ui::InputField>(
			container,
			st::createPollOptionField,
			Ui::InputField::Mode::MultiLine,
			langFactory(lng_polls_create_option_add)));
	InitField(outer, field);
	result._field.reset(field);

	result.createShadow();
	result.createRemove();
	return result;
}

bool Options::Option::hasShadow() const {
	return (_shadow != nullptr);
}

void Options::Option::createShadow() {
	Expects(_field != nullptr);

	if (_shadow) {
		return;
	}
	const auto value = Ui::CreateChild<Ui::PlainShadow>(_field.get());
	value->show();
	_field->sizeValue(
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

	const auto field = _field.get();
	auto &lifetime = field->lifetime();

	const auto remove = Ui::CreateChild<Ui::CrossButton>(
		field,
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

bool Options::Option::isEmpty() const {
	return _field->getLastText().trimmed().isEmpty();
}

bool Options::Option::isGood() const {
	const auto text = _field->getLastText().trimmed();
	return !text.isEmpty() && (text.size() <= kOptionLimit);
}

bool Options::Option::hasFocus() const {
	return _field->hasFocus();
}

void Options::Option::setFocus() const {
	_field->setFocus();
}

void Options::Option::clearValue() {
	_field->setText(QString());
}

void Options::Option::setPlaceholder() const {
	_field->setPlaceholder(langFactory(lng_polls_create_option_add));
}

void Options::Option::toggleRemoveAlways(bool toggled) {
	*_removeAlways = toggled;
}

not_null<Ui::InputField*> Options::Option::field() const {
	return _field.get();
}

void Options::Option::removePlaceholder() const {
	_field->setPlaceholder(nullptr);
}

PollAnswer Options::Option::toPollAnswer(char id) const {
	return PollAnswer{
		_field->getLastText().trimmed(),
		QByteArray(1, id)
	};
}

rpl::producer<Qt::MouseButton> Options::Option::removeClicks() const {
	return _remove->clicks();
}

Options::Options(
	not_null<QWidget*> outer,
	not_null<Ui::VerticalLayout*> container)
: _outer(outer)
, _container(container)
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

std::vector<PollAnswer> Options::toPollAnswers() const {
	auto result = std::vector<PollAnswer>();
	result.reserve(_list.size());
	auto counter = char(0);
	const auto makeAnswer = [&](const Option &option) {
		return option.toPollAnswer(++counter);
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
	auto reversed = ranges::view::reverse(_list);
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
	_list.erase(emptyItem + 1, end);
	fixAfterErase();
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
		_position + _list.size()));
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
			_list.erase(item);
			fixAfterErase();
			validateState();
		}));
	}, field->lifetime());

	//fixShadows();
}

void Options::validateState() {
	checkLastOption();
	_valid = (ranges::count_if(_list, &Option::isGood) > 1);
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

CreatePollBox::CreatePollBox(QWidget*) {
}

rpl::producer<PollData> CreatePollBox::submitRequests() const {
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

	AddSubsectionTitle(container, lng_polls_create_question);
	const auto question = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::createPollField,
			Ui::InputField::Mode::MultiLine,
			langFactory(lng_polls_create_question_placeholder)),
		st::createPollFieldPadding);
	InitField(getDelegate()->outerContainer(), question);

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
	AddSubsectionTitle(container, lng_polls_create_options);
	const auto options = lifetime().make_state<Options>(
		getDelegate()->outerContainer(),
		container);
	auto limit = options->usedCount() | rpl::map([=](int count) {
		return (count < kMaxOptionsCount)
			? lng_polls_create_limit(lt_count, kMaxOptionsCount - count)
			: lang(lng_polls_create_maximum);
	}) | rpl::after_next([=] {
		container->resizeToWidth(container->widthNoMargins());
	});
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(limit),
			st::createPollLimitLabel),
		st::createPollFieldPadding);

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
	const auto updateValid = [=] {
		valid->fire(isValidQuestion() && options->isValid());
	};
	valid->events(
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool valid) {
		clearButtons();
		if (valid) {
			addButton(
				langFactory(lng_polls_create_button),
				[=] { _submitRequests.fire(collectResult()); });
		}
		addButton(langFactory(lng_cancel), [=] { closeBox(); });
	}, lifetime());

	options->isValidChanged(
	) | rpl::start_with_next([=] {
		updateValid();
	}, lifetime());

	options->scrollToWidget(
	) | rpl::start_with_next([=](not_null<QWidget*> widget) {
		scrollToWidget(widget);
	}, lifetime());

	return std::move(result);
}

void CreatePollBox::prepare() {
	setTitle(langFactory(lng_polls_create_title));

	const auto inner = setInnerWidget(setupContent());

	setDimensionsToContent(st::boxWideWidth, inner);
}
