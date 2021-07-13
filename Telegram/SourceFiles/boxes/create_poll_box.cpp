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
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/toast/toast.h"
#include "main/main_session.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/send_context_menu.h"
#include "history/view/history_view_schedule_box.h"
#include "settings/settings_common.h"
#include "base/unique_qptr.h"
#include "base/event_filter.h"
#include "base/call_delayed.h"
#include "base/openssl_help.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kQuestionLimit = 255;
constexpr auto kMaxOptionsCount = PollData::kMaxOptions;
constexpr auto kOptionLimit = 100;
constexpr auto kWarnQuestionLimit = 80;
constexpr auto kWarnOptionLimit = 30;
constexpr auto kSolutionLimit = 200;
constexpr auto kWarnSolutionLimit = 60;
constexpr auto kErrorLimit = 99;

class Options {
public:
	Options(
		not_null<QWidget*> outer,
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session,
		bool chooseCorrectEnabled);

	[[nodiscard]] bool hasOptions() const;
	[[nodiscard]] bool isValid() const;
	[[nodiscard]] bool hasCorrect() const;
	[[nodiscard]] std::vector<PollAnswer> toPollAnswers() const;
	void focusFirst();

	void enableChooseCorrect(bool enabled);

	[[nodiscard]] rpl::producer<int> usedCount() const;
	[[nodiscard]] rpl::producer<not_null<QWidget*>> scrollToWidget() const;
	[[nodiscard]] rpl::producer<> backspaceInFront() const;
	[[nodiscard]] rpl::producer<> tabbed() const;

private:
	class Option {
	public:
		Option(
			not_null<QWidget*> outer,
			not_null<Ui::VerticalLayout*> container,
			not_null<Main::Session*> session,
			int position,
			std::shared_ptr<Ui::RadiobuttonGroup> group);

		Option(const Option &other) = delete;
		Option &operator=(const Option &other) = delete;

		void toggleRemoveAlways(bool toggled);
		void enableChooseCorrect(
			std::shared_ptr<Ui::RadiobuttonGroup> group);

		void show(anim::type animated);
		void destroy(FnMut<void()> done);

		[[nodiscard]] bool hasShadow() const;
		void createShadow();
		void destroyShadow();

		[[nodiscard]] bool isEmpty() const;
		[[nodiscard]] bool isGood() const;
		[[nodiscard]] bool isTooLong() const;
		[[nodiscard]] bool isCorrect() const;
		[[nodiscard]] bool hasFocus() const;
		void setFocus() const;
		void clearValue();

		void setPlaceholder() const;
		void removePlaceholder() const;

		not_null<Ui::InputField*> field() const;

		[[nodiscard]] PollAnswer toPollAnswer(int index) const;

		[[nodiscard]] rpl::producer<Qt::MouseButton> removeClicks() const;

	private:
		void createRemove();
		void createWarning();
		void toggleCorrectSpace(bool visible);
		void updateFieldGeometry();

		base::unique_qptr<Ui::SlideWrap<Ui::RpWidget>> _wrap;
		not_null<Ui::RpWidget*> _content;
		base::unique_qptr<Ui::FadeWrapScaled<Ui::Radiobutton>> _correct;
		Ui::Animations::Simple _correctShown;
		bool _hasCorrect = false;
		Ui::InputField *_field = nullptr;
		base::unique_qptr<Ui::PlainShadow> _shadow;
		base::unique_qptr<Ui::CrossButton> _remove;
		rpl::variable<bool> *_removeAlways = nullptr;

	};

	[[nodiscard]] bool full() const;
	[[nodiscard]] bool correctShadows() const;
	void fixShadows();
	void removeEmptyTail();
	void addEmptyOption();
	void checkLastOption();
	void validateState();
	void fixAfterErase();
	void destroy(std::unique_ptr<Option> option);
	void removeDestroyed(not_null<Option*> field);
	int findField(not_null<Ui::InputField*> field) const;
	[[nodiscard]] auto createChooseCorrectGroup()
		-> std::shared_ptr<Ui::RadiobuttonGroup>;

	not_null<QWidget*> _outer;
	not_null<Ui::VerticalLayout*> _container;
	const not_null<Main::Session*> _session;
	std::shared_ptr<Ui::RadiobuttonGroup> _chooseCorrectGroup;
	int _position = 0;
	std::vector<std::unique_ptr<Option>> _list;
	std::vector<std::unique_ptr<Option>> _destroyed;
	rpl::variable<int> _usedCount = 0;
	bool _hasOptions = false;
	bool _isValid = false;
	bool _hasCorrect = false;
	rpl::event_stream<not_null<QWidget*>> _scrollToWidget;
	rpl::event_stream<> _backspaceInFront;
	rpl::event_stream<> _tabbed;

};

void InitField(
		not_null<QWidget*> container,
		not_null<Ui::InputField*> field,
		not_null<Main::Session*> session) {
	field->setInstantReplaces(Ui::InstantReplaces::Default());
	field->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
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

Options::Option::Option(
	not_null<QWidget*> outer,
	not_null<Ui::VerticalLayout*> container,
	not_null<Main::Session*> session,
	int position,
	std::shared_ptr<Ui::RadiobuttonGroup> group)
: _wrap(container->insert(
	position,
	object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
		container,
		object_ptr<Ui::RpWidget>(container))))
, _content(_wrap->entity())
, _field(
	Ui::CreateChild<Ui::InputField>(
		_content.get(),
		st::createPollOptionField,
		Ui::InputField::Mode::NoNewlines,
		tr::lng_polls_create_option_add())) {
	InitField(outer, _field, session);
	_field->setMaxLength(kOptionLimit + kErrorLimit);
	_field->show();
	_field->customTab(true);

	_wrap->hide(anim::type::instant);

	_content->widthValue(
	) | rpl::start_with_next([=] {
		updateFieldGeometry();
	}, _field->lifetime());

	_field->heightValue(
	) | rpl::start_with_next([=](int height) {
		_content->resize(_content->width(), height);
	}, _field->lifetime());

	QObject::connect(_field, &Ui::InputField::changed, [=] {
		Ui::PostponeCall(crl::guard(_field, [=] {
			if (_hasCorrect) {
				_correct->toggle(isGood(), anim::type::normal);
			}
		}));
	});

	createShadow();
	createRemove();
	createWarning();
	enableChooseCorrect(group);
	_correctShown.stop();
	if (_correct) {
		_correct->finishAnimating();
	}
	updateFieldGeometry();
}

bool Options::Option::hasShadow() const {
	return (_shadow != nullptr);
}

void Options::Option::createShadow() {
	Expects(_content != nullptr);

	if (_shadow) {
		return;
	}
	_shadow.reset(Ui::CreateChild<Ui::PlainShadow>(field().get()));
	_shadow->show();
	field()->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto left = st::createPollFieldPadding.left();
		_shadow->setGeometry(
			left,
			size.height() - st::lineWidth,
			size.width() - left,
			st::lineWidth);
	}, _shadow->lifetime());
}

void Options::Option::destroyShadow() {
	_shadow = nullptr;
}

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

bool Options::Option::isCorrect() const {
	return isGood() && _correct && _correct->entity()->Checkbox::checked();
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

void Options::Option::enableChooseCorrect(
		std::shared_ptr<Ui::RadiobuttonGroup> group) {
	if (!group) {
		if (_correct) {
			_hasCorrect = false;
			_correct->hide(anim::type::normal);
			toggleCorrectSpace(false);
		}
		return;
	}
	static auto Index = 0;
	const auto button = Ui::CreateChild<Ui::FadeWrapScaled<Ui::Radiobutton>>(
		_content.get(),
		object_ptr<Ui::Radiobutton>(
			_content.get(),
			group,
			++Index,
			QString(),
			st::defaultCheckbox));
	button->entity()->resize(
		button->entity()->height(),
		button->entity()->height());
	button->hide(anim::type::instant);
	_content->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto left = st::createPollFieldPadding.left();
		button->moveToLeft(
			left,
			(size.height() - button->heightNoMargins()) / 2);
	}, button->lifetime());
	_correct.reset(button);
	_hasCorrect = true;
	if (isGood()) {
		_correct->show(anim::type::normal);
	} else {
		_correct->hide(anim::type::instant);
	}
	toggleCorrectSpace(true);
}

void Options::Option::toggleCorrectSpace(bool visible) {
	_correctShown.start(
		[=] { updateFieldGeometry(); },
		visible ? 0. : 1.,
		visible ? 1. : 0.,
		st::fadeWrapDuration);
}

void Options::Option::updateFieldGeometry() {
	const auto shown = _correctShown.value(_hasCorrect ? 1. : 0.);
	const auto skip = st::defaultRadio.diameter
		+ st::defaultCheckbox.textPosition.x();
	const auto left = anim::interpolate(0, skip, shown);
	_field->resizeToWidth(_content->width() - left);
	_field->moveToLeft(left, 0);
}

not_null<Ui::InputField*> Options::Option::field() const {
	return _field;
}

void Options::Option::removePlaceholder() const {
	field()->setPlaceholder(rpl::single(QString()));
}

PollAnswer Options::Option::toPollAnswer(int index) const {
	Expects(index >= 0 && index < kMaxOptionsCount);

	auto result = PollAnswer{
		field()->getLastText().trimmed(),
		QByteArray(1, ('0' + index))
	};
	result.correct = _correct ? _correct->entity()->Checkbox::checked() : false;
	return result;
}

rpl::producer<Qt::MouseButton> Options::Option::removeClicks() const {
	return _remove->clicks();
}

Options::Options(
	not_null<QWidget*> outer,
	not_null<Ui::VerticalLayout*> container,
	not_null<Main::Session*> session,
	bool chooseCorrectEnabled)
: _outer(outer)
, _container(container)
, _session(session)
, _chooseCorrectGroup(chooseCorrectEnabled
	? createChooseCorrectGroup()
	: nullptr)
, _position(_container->count()) {
	checkLastOption();
}

bool Options::full() const {
	return (_list.size() == kMaxOptionsCount);
}

bool Options::hasOptions() const {
	return _hasOptions;
}

bool Options::isValid() const {
	return _isValid;
}

bool Options::hasCorrect() const {
	return _hasCorrect;
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

rpl::producer<> Options::tabbed() const {
	return _tabbed.events();
}

void Options::Option::show(anim::type animated) {
	_wrap->show(animated);
}

void Options::Option::destroy(FnMut<void()> done) {
	if (anim::Disabled() || _wrap->isHidden()) {
		Ui::PostponeCall(std::move(done));
		return;
	}
	_wrap->hide(anim::type::normal);
	base::call_delayed(
		st::slideWrapDuration * 2,
		_content.get(),
		std::move(done));
}

std::vector<PollAnswer> Options::toPollAnswers() const {
	auto result = std::vector<PollAnswer>();
	result.reserve(_list.size());
	auto counter = int(0);
	const auto makeAnswer = [&](const std::unique_ptr<Option> &option) {
		return option->toPollAnswer(counter++);
	};
	ranges::copy(
		_list
		| ranges::views::filter(&Option::isGood)
		| ranges::views::transform(makeAnswer),
		ranges::back_inserter(result));
	return result;
}

void Options::focusFirst() {
	Expects(!_list.empty());

	_list.front()->setFocus();
}

std::shared_ptr<Ui::RadiobuttonGroup> Options::createChooseCorrectGroup() {
	auto result = std::make_shared<Ui::RadiobuttonGroup>(0);
	result->setChangedCallback([=](int) {
		validateState();
	});
	return result;
}

void Options::enableChooseCorrect(bool enabled) {
	_chooseCorrectGroup = enabled
		? createChooseCorrectGroup()
		: nullptr;
	for (auto &option : _list) {
		option->enableChooseCorrect(_chooseCorrectGroup);
	}
	validateState();
}

bool Options::correctShadows() const {
	// Last one should be without shadow.
	const auto noShadow = ranges::find(
		_list,
		true,
		ranges::not_fn(&Option::hasShadow));
	return (noShadow == end(_list) - 1);
}

void Options::fixShadows() {
	if (correctShadows()) {
		return;
	}
	for (auto &option : _list) {
		option->createShadow();
	}
	_list.back()->destroyShadow();
}

void Options::removeEmptyTail() {
	// Only one option at the end of options list can be empty.
	// Remove all other trailing empty options.
	// Only last empty and previous option have non-empty placeholders.
	const auto focused = ranges::find_if(
		_list,
		&Option::hasFocus);
	const auto end = _list.end();
	const auto reversed = ranges::views::reverse(_list);
	const auto emptyItem = ranges::find_if(
		reversed,
		ranges::not_fn(&Option::isEmpty)).base();
	const auto focusLast = (focused > emptyItem) && (focused < end);
	if (emptyItem == end) {
		return;
	}
	if (focusLast) {
		(*emptyItem)->setFocus();
	}
	for (auto i = emptyItem + 1; i != end; ++i) {
		destroy(std::move(*i));
	}
	_list.erase(emptyItem + 1, end);
	fixAfterErase();
}

void Options::destroy(std::unique_ptr<Option> option) {
	const auto value = option.get();
	option->destroy([=] { removeDestroyed(value); });
	_destroyed.push_back(std::move(option));
}

void Options::fixAfterErase() {
	Expects(!_list.empty());

	const auto last = _list.end() - 1;
	(*last)->setPlaceholder();
	(*last)->toggleRemoveAlways(false);
	if (last != begin(_list)) {
		(*(last - 1))->setPlaceholder();
		(*(last - 1))->toggleRemoveAlways(false);
	}
	fixShadows();
}

void Options::addEmptyOption() {
	if (full()) {
		return;
	} else if (!_list.empty() && _list.back()->isEmpty()) {
		return;
	}
	if (_list.size() > 1) {
		(*(_list.end() - 2))->removePlaceholder();
		(*(_list.end() - 2))->toggleRemoveAlways(true);
	}
	_list.push_back(std::make_unique<Option>(
		_outer,
		_container,
		_session,
		_position + _list.size() + _destroyed.size(),
		_chooseCorrectGroup));
	const auto field = _list.back()->field();
	QObject::connect(field, &Ui::InputField::submitted, [=] {
		const auto index = findField(field);
		if (_list[index]->isGood() && index + 1 < _list.size()) {
			_list[index + 1]->setFocus();
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
	QObject::connect(field, &Ui::InputField::tabbed, [=] {
		const auto index = findField(field);
		if (index + 1 < _list.size()) {
			_list[index + 1]->setFocus();
		} else {
			_tabbed.fire({});
		}
	});
	base::install_event_filter(field, [=](not_null<QEvent*> event) {
		if (event->type() != QEvent::KeyPress
			|| !field->getLastText().isEmpty()) {
			return base::EventFilterResult::Continue;
		}
		const auto key = static_cast<QKeyEvent*>(event.get())->key();
		if (key != Qt::Key_Backspace) {
			return base::EventFilterResult::Continue;
		}

		const auto index = findField(field);
		if (index > 0) {
			_list[index - 1]->setFocus();
		} else {
			_backspaceInFront.fire({});
		}
		return base::EventFilterResult::Cancel;
	});

	_list.back()->removeClicks(
	) | rpl::take(1) | rpl::start_with_next([=] {
		Ui::PostponeCall(crl::guard(field, [=] {
			Expects(!_list.empty());

			const auto item = begin(_list) + findField(field);
			if (item == _list.end() - 1) {
				(*item)->clearValue();
				return;
			}
			if ((*item)->hasFocus()) {
				(*(item + 1))->setFocus();
			}
			destroy(std::move(*item));
			_list.erase(item);
			fixAfterErase();
			validateState();
		}));
	}, field->lifetime());

	_list.back()->show((_list.size() == 1)
		? anim::type::instant
		: anim::type::normal);
	fixShadows();
}

void Options::removeDestroyed(not_null<Option*> option) {
	const auto i = ranges::find(
		_destroyed,
		option.get(),
		&std::unique_ptr<Option>::get);
	Assert(i != end(_destroyed));
	_destroyed.erase(i);
}

void Options::validateState() {
	checkLastOption();
	_hasOptions = (ranges::count_if(_list, &Option::isGood) > 1);
	_isValid = _hasOptions && ranges::none_of(_list, &Option::isTooLong);
	_hasCorrect = ranges::any_of(_list, &Option::isCorrect);

	const auto lastEmpty = !_list.empty() && _list.back()->isEmpty();
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
	not_null<Window::SessionController*> controller,
	PollData::Flags chosen,
	PollData::Flags disabled,
	Api::SendType sendType,
	SendMenu::Type sendMenuType)
: _controller(controller)
, _chosen(chosen)
, _disabled(disabled)
, _sendType(sendType)
, _sendMenuType(sendMenuType) {
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

	const auto session = &_controller->session();
	AddSubsectionTitle(container, tr::lng_polls_create_question());
	const auto question = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::createPollField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_polls_create_question_placeholder()),
		st::createPollFieldPadding);
	InitField(getDelegate()->outerContainer(), question, session);
	question->setMaxLength(kQuestionLimit + kErrorLimit);
	question->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
	question->customTab(true);

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

not_null<Ui::InputField*> CreatePollBox::setupSolution(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<bool> shown) {
	using namespace Settings;

	const auto outer = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container))
	)->setDuration(0)->toggleOn(std::move(shown));
	const auto inner = outer->entity();

	const auto session = &_controller->session();
	AddSkip(inner);
	AddSubsectionTitle(inner, tr::lng_polls_solution_title());
	const auto solution = inner->add(
		object_ptr<Ui::InputField>(
			inner,
			st::createPollSolutionField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_polls_solution_placeholder()),
		st::createPollFieldPadding);
	InitField(getDelegate()->outerContainer(), solution, session);
	solution->setMaxLength(kSolutionLimit + kErrorLimit);
	solution->setInstantReplaces(Ui::InstantReplaces::Default());
	solution->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	solution->setMarkdownReplacesEnabled(rpl::single(true));
	solution->setEditLinkCallback(
		DefaultEditLinkCallback(_controller, solution));
	solution->customTab(true);

	const auto warning = CreateWarningLabel(
		inner,
		solution,
		kSolutionLimit,
		kWarnSolutionLimit);
	rpl::combine(
		solution->geometryValue(),
		warning->sizeValue()
	) | rpl::start_with_next([=](QRect geometry, QSize label) {
		warning->moveToLeft(
			(inner->width()
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

	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			tr::lng_polls_solution_about(),
			st::boxDividerLabel),
		st::createPollFieldTitlePadding);

	return solution;
}

object_ptr<Ui::RpWidget> CreatePollBox::setupContent() {
	using namespace Settings;

	const auto id = openssl::RandomValue<uint64>();
	const auto error = lifetime().make_state<Errors>(Error::Question);

	auto result = object_ptr<Ui::VerticalLayout>(this);
	const auto container = result.data();

	const auto question = setupQuestion(container);
	AddDivider(container);
	AddSkip(container);
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_polls_create_options(),
			st::settingsSubsectionTitle),
		st::createPollFieldTitlePadding);
	const auto options = lifetime().make_state<Options>(
		getDelegate()->outerContainer(),
		container,
		&_controller->session(),
		(_chosen & PollData::Flag::Quiz));
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
		object_ptr<Ui::DividerLabel>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(limit),
				st::boxDividerLabel),
			st::createPollLimitPadding));

	connect(question, &Ui::InputField::tabbed, [=] {
		options->focusFirst();
	});

	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_polls_create_settings());

	const auto anonymous = (!(_disabled & PollData::Flag::PublicVotes))
		? container->add(
			object_ptr<Ui::Checkbox>(
				container,
				tr::lng_polls_create_anonymous(tr::now),
				!(_chosen & PollData::Flag::PublicVotes),
				st::defaultCheckbox),
			st::createPollCheckboxMargin)
		: nullptr;
	const auto hasMultiple = !(_chosen & PollData::Flag::Quiz)
		|| !(_disabled & PollData::Flag::Quiz);
	const auto multiple = hasMultiple
		? container->add(
			object_ptr<Ui::Checkbox>(
				container,
				tr::lng_polls_create_multiple_choice(tr::now),
				(_chosen & PollData::Flag::MultiChoice),
				st::defaultCheckbox),
			st::createPollCheckboxMargin)
		: nullptr;
	const auto quiz = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			tr::lng_polls_create_quiz_mode(tr::now),
			(_chosen & PollData::Flag::Quiz),
			st::defaultCheckbox),
		st::createPollCheckboxMargin);

	const auto solution = setupSolution(
		container,
		rpl::single(quiz->checked()) | rpl::then(quiz->checkedChanges()));

	options->tabbed(
	) | rpl::start_with_next([=] {
		if (quiz->checked()) {
			solution->setFocus();
		} else {
			question->setFocus();
		}
	}, question->lifetime());

	connect(solution, &Ui::InputField::tabbed, [=] {
		question->setFocus();
	});

	quiz->setDisabled(_disabled & PollData::Flag::Quiz);
	if (multiple) {
		multiple->setDisabled((_disabled & PollData::Flag::MultiChoice)
			|| (_chosen & PollData::Flag::Quiz));
		multiple->events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return (e->type() == QEvent::MouseButtonPress) && quiz->checked();
		}) | rpl::start_with_next([=] {
			Ui::Toast::Show(tr::lng_polls_create_one_answer(tr::now));
		}, multiple->lifetime());
	}

	using namespace rpl::mappers;
	quiz->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		if (multiple) {
			if (checked && multiple->checked()) {
				multiple->setChecked(false);
			}
			multiple->setDisabled(checked
				|| (_disabled & PollData::Flag::MultiChoice));
		}
		options->enableChooseCorrect(checked);
	}, quiz->lifetime());

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
		using Flag = PollData::Flag;
		auto result = PollData(&_controller->session().data(), id);
		result.question = question->getLastText().trimmed();
		result.answers = options->toPollAnswers();
		const auto solutionWithTags = quiz->checked()
			? solution->getTextWithAppliedMarkdown()
			: TextWithTags();
		result.solution = TextWithEntities{
			solutionWithTags.text,
			TextUtilities::ConvertTextTagsToEntities(solutionWithTags.tags)
		};
		const auto publicVotes = (anonymous && !anonymous->checked());
		const auto multiChoice = (multiple && multiple->checked());
		result.setFlags(Flag(0)
			| (publicVotes ? Flag::PublicVotes : Flag(0))
			| (multiChoice ? Flag::MultiChoice : Flag(0))
			| (quiz->checked() ? Flag::Quiz : Flag(0)));
		return result;
	};
	const auto collectError = [=] {
		if (isValidQuestion()) {
			*error &= ~Error::Question;
		} else {
			*error |= Error::Question;
		}
		if (!options->hasOptions()) {
			*error |= Error::Options;
		} else if (!options->isValid()) {
			*error |= Error::Other;
		} else {
			*error &= ~(Error::Options | Error::Other);
		}
		if (quiz->checked() && !options->hasCorrect()) {
			*error |= Error::Correct;
		} else {
			*error &= ~Error::Correct;
		}
		if (quiz->checked()
			&& solution->getLastText().trimmed().size() > kSolutionLimit) {
			*error |= Error::Solution;
		} else {
			*error &= ~Error::Solution;
		}
	};
	const auto showError = [](tr::phrase<> text) {
		Ui::Toast::Show(text(tr::now));
	};
	const auto send = [=](Api::SendOptions sendOptions) {
		collectError();
		if (*error & Error::Question) {
			showError(tr::lng_polls_choose_question);
			question->setFocus();
		} else if (*error & Error::Options) {
			showError(tr::lng_polls_choose_answers);
			options->focusFirst();
		} else if (*error & Error::Correct) {
			showError(tr::lng_polls_choose_correct);
		} else if (*error & Error::Solution) {
			solution->showError();
		} else if (!*error) {
			_submitRequests.fire({ collectResult(), sendOptions });
		}
	};
	const auto sendSilent = [=] {
		send({ .silent = true });
	};
	const auto sendScheduled = [=] {
		_controller->show(
			HistoryView::PrepareScheduleBox(
				this,
				SendMenu::Type::Scheduled,
				send),
			Ui::LayerOption::KeepOther);
	};

	options->scrollToWidget(
	) | rpl::start_with_next([=](not_null<QWidget*> widget) {
		scrollToWidget(widget);
	}, lifetime());

	options->backspaceInFront(
	) | rpl::start_with_next([=] {
		FocusAtEnd(question);
	}, lifetime());

	const auto isNormal = (_sendType == Api::SendType::Normal);

	const auto submit = addButton(
		isNormal
			? tr::lng_polls_create_button()
			: tr::lng_schedule_button(),
		[=] { isNormal ? send({}) : sendScheduled(); });
	const auto sendMenuType = [=] {
		collectError();
		return (*error)
			? SendMenu::Type::Disabled
			: _sendMenuType;
	};
	SendMenu::SetupMenuAndShortcuts(
		submit.data(),
		sendMenuType,
		sendSilent,
		sendScheduled);
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	return result;
}

void CreatePollBox::prepare() {
	setTitle(tr::lng_polls_create_title());

	const auto inner = setInnerWidget(setupContent());

	setDimensionsToContent(st::boxWideWidth, inner);
}
