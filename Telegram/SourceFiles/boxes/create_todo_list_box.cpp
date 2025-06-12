/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/create_todo_list_box.h"

#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "base/random.h"
#include "base/unique_qptr.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_todo_list.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/view/history_view_schedule_box.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/emoji_button_factory.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h" // defaultComposeFiles.
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace {

constexpr auto kMaxOptionsCount = TodoListData::kMaxOptions;
constexpr auto kWarnTitleLimit = 12;
constexpr auto kWarnTaskLimit = 24;
constexpr auto kErrorLimit = 99;

class Tasks {
public:
	Tasks(
		not_null<Ui::BoxContent*> box,
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		ChatHelpers::TabbedPanel *emojiPanel,
		std::vector<TodoListItem> existing = {},
		bool existingLocked = false);

	[[nodiscard]] bool hasTasks() const;
	[[nodiscard]] bool isValid() const;
	[[nodiscard]] std::vector<TodoListItem> toTodoListItems() const;
	void focusFirst();

	[[nodiscard]] rpl::producer<int> addedCount() const;
	[[nodiscard]] rpl::producer<not_null<QWidget*>> scrollToWidget() const;
	[[nodiscard]] rpl::producer<> backspaceInFront() const;
	[[nodiscard]] rpl::producer<> tabbed() const;

private:
	class Task {
	public:
		Task(
			not_null<QWidget*> outer,
			not_null<Ui::VerticalLayout*> container,
			not_null<Main::Session*> session,
			int id,
			TextWithEntities text,
			int position,
			bool locked);

		Task(const Task &other) = delete;
		Task &operator=(const Task &other) = delete;

		void toggleRemoveAlways(bool toggled);

		void show(anim::type animated);
		void destroy(FnMut<void()> done);

		[[nodiscard]] bool hasShadow() const;
		void createShadow();
		void destroyShadow();

		[[nodiscard]] int id() const;
		[[nodiscard]] bool locked() const;
		[[nodiscard]] bool isEmpty() const;
		[[nodiscard]] bool isGood() const;
		[[nodiscard]] bool isTooLong() const;
		[[nodiscard]] bool hasFocus() const;
		void setFocus() const;
		void clearValue();

		void setPlaceholder() const;
		void removePlaceholder() const;

		[[nodiscard]] not_null<Ui::InputField*> field() const;

		[[nodiscard]] TodoListItem toTodoListItem(int nextId) const;

		[[nodiscard]] rpl::producer<Qt::MouseButton> removeClicks() const;

	private:
		void createRemove();
		void createWarning();
		void updateFieldGeometry();

		int _id = 0;
		base::unique_qptr<Ui::SlideWrap<Ui::RpWidget>> _wrap;
		not_null<Ui::RpWidget*> _content;
		Ui::InputField *_field = nullptr;
		base::unique_qptr<Ui::PlainShadow> _shadow;
		base::unique_qptr<Ui::CrossButton> _remove;
		rpl::variable<bool> *_removeAlways = nullptr;
		int _limit = 0;

	};

	[[nodiscard]] bool full() const;
	[[nodiscard]] bool correctShadows() const;
	void fixShadows();
	void removeEmptyTail();
	void addEmptyTask();
	void addTask(
		int id,
		TextWithEntities text,
		anim::type animated);
	void initTaskField(not_null<Task*> task);
	void checkLastTask();
	void validateState();
	void fixAfterErase();
	void destroy(std::unique_ptr<Task> task);
	void removeDestroyed(not_null<Task*> field);
	int findField(not_null<Ui::InputField*> field) const;

	not_null<Ui::BoxContent*> _box;
	not_null<Ui::VerticalLayout*> _container;
	const not_null<Window::SessionController*> _controller;
	const int _existingCount = 0;
	const bool _existingLocked = false;
	ChatHelpers::TabbedPanel * const _emojiPanel;
	int _position = 0;
	int _tasksLimit = 0;
	std::vector<std::unique_ptr<Task>> _list;
	std::vector<std::unique_ptr<Task>> _destroyed;
	rpl::variable<int> _addedCount = 0;
	bool _hasTasks = false;
	bool _isValid = false;
	rpl::event_stream<not_null<QWidget*>> _scrollToWidget;
	rpl::event_stream<> _backspaceInFront;
	rpl::event_stream<> _tabbed;
	rpl::lifetime _emojiPanelLifetime;

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
	field->changes(
	) | rpl::start_with_next([=] {
		Ui::PostponeCall(crl::guard(field, [=] {
			const auto length = field->getLastText().size();
			const auto value = valueLimit - length;
			const auto shown = (value < warnLimit)
				&& (field->height() > st::createPollOptionField.heightMin);
			if (value >= 0) {
				result->setText(QString::number(value));
			} else {
				constexpr auto kMinus = QChar(0x2212);
				result->setMarkedText(Ui::Text::Colorized(
					kMinus + QString::number(std::abs(value))));
			}
			result->setVisible(shown);
		}));
	}, field->lifetime());
	return result;
}

void FocusAtEnd(not_null<Ui::InputField*> field) {
	field->setFocus();
	field->setCursorPosition(field->getLastText().size());
	field->ensureCursorVisible();
}

[[nodiscard]] base::unique_qptr<ChatHelpers::TabbedPanel> MakeEmojiPanel(
		not_null<QWidget*> outer,
		not_null<Window::SessionController*> controller) {
	auto result = base::make_unique_q<ChatHelpers::TabbedPanel>(
		outer,
		controller,
		object_ptr<ChatHelpers::TabbedSelector>(
			nullptr,
			controller->uiShow(),
			Window::GifPauseReason::Layer,
			ChatHelpers::TabbedSelector::Mode::EmojiOnly));
	result->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	result ->hide();
	result->selector()->setCurrentPeer(controller->session().user());
	return result;
}

Tasks::Task::Task(
	not_null<QWidget*> outer,
	not_null<Ui::VerticalLayout*> container,
	not_null<Main::Session*> session,
	int id,
	TextWithEntities text,
	int position,
	bool locked)
: _id(id)
, _wrap(container->insert(
	position,
	object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
		container,
		object_ptr<Ui::RpWidget>(container))))
, _content(_wrap->entity())
, _field(
	Ui::CreateChild<Ui::InputField>(
		_content.get(),
		session->user()->isPremium()
			? st::createPollOptionFieldPremium
			: st::createPollOptionField,
		Ui::InputField::Mode::NoNewlines,
		tr::lng_todo_create_list_add()))
, _limit(session->appConfig().todoListItemTextLimit()) {
	InitField(outer, _field, session);
	_field->setMaxLength(_limit + kErrorLimit);
	_field->setTextWithTags({
		text.text,
		TextUtilities::ConvertEntitiesToTextTags(text.entities)
	});
	_field->finishAnimating();
	_field->show();
	if (locked) {
		_field->setDisabled(true);
	} else {
		_field->customTab(true);
	}

	_wrap->hide(anim::type::instant);

	_content->widthValue(
	) | rpl::start_with_next([=] {
		updateFieldGeometry();
	}, _field->lifetime());

	_field->heightValue(
	) | rpl::start_with_next([=](int height) {
		_content->resize(_content->width(), height);
	}, _field->lifetime());

	createShadow();
	if (!locked) {
		createRemove();
		createWarning();
	}
	updateFieldGeometry();
}

bool Tasks::Task::hasShadow() const {
	return (_shadow != nullptr);
}

void Tasks::Task::createShadow() {
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

void Tasks::Task::destroyShadow() {
	_shadow = nullptr;
}

void Tasks::Task::createRemove() {
	using namespace rpl::mappers;

	const auto field = this->field();
	auto &lifetime = field->lifetime();

	const auto remove = Ui::CreateChild<Ui::CrossButton>(
		field.get(),
		st::createPollOptionRemove);
	remove->show(anim::type::instant);

	const auto toggle = lifetime.make_state<rpl::variable<bool>>(false);
	_removeAlways = lifetime.make_state<rpl::variable<bool>>(false);

	field->changes(
	) | rpl::start_with_next([field, toggle] {
		// Don't capture 'this'! Because Option is a value type.
		*toggle = !field->getLastText().isEmpty();
	}, field->lifetime());
#if 0
	rpl::combine(
		toggle->value(),
		_removeAlways->value(),
		_1 || _2
	) | rpl::start_with_next([=](bool shown) {
		remove->toggle(shown, anim::type::normal);
	}, remove->lifetime());
#endif

	field->widthValue(
	) | rpl::start_with_next([=](int width) {
		remove->moveToRight(
			st::createPollOptionRemovePosition.x(),
			st::createPollOptionRemovePosition.y(),
			width);
	}, remove->lifetime());

	_remove.reset(remove);
}

void Tasks::Task::createWarning() {
	using namespace rpl::mappers;

	const auto field = this->field();
	const auto warning = CreateWarningLabel(
		field,
		field,
		_limit,
		kWarnTaskLimit);
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

bool Tasks::Task::isEmpty() const {
	return field()->getLastText().trimmed().isEmpty();
}

bool Tasks::Task::isGood() const {
	return !locked()
		&& !field()->getLastText().trimmed().isEmpty()
		&& !isTooLong();
}

bool Tasks::Task::isTooLong() const {
	return (field()->getLastText().size() > _limit);
}

bool Tasks::Task::hasFocus() const {
	return field()->hasFocus();
}

void Tasks::Task::setFocus() const {
	if (!locked()) {
		FocusAtEnd(field());
	}
}

void Tasks::Task::clearValue() {
	field()->setText(QString());
}

void Tasks::Task::setPlaceholder() const {
	field()->setPlaceholder(tr::lng_todo_create_list_add());
}

void Tasks::Task::toggleRemoveAlways(bool toggled) {
	if (_removeAlways) {
		*_removeAlways = toggled;
	}
}

void Tasks::Task::updateFieldGeometry() {
	_field->resizeToWidth(_content->width());
	_field->moveToLeft(0, 0);
}

not_null<Ui::InputField*> Tasks::Task::field() const {
	return _field;
}

void Tasks::Task::removePlaceholder() const {
	field()->setPlaceholder(rpl::single(QString()));
}

int Tasks::Task::id() const {
	return _id;
}

bool Tasks::Task::locked() const {
	return !_remove;
}

TodoListItem Tasks::Task::toTodoListItem(int nextId) const {
	const auto text = field()->getTextWithTags();
	auto result = TodoListItem{
		.text = TextWithEntities{
			.text = text.text,
			.entities = TextUtilities::ConvertTextTagsToEntities(text.tags),
		},
		.id = _id ? _id : nextId,
	};
	TextUtilities::Trim(result.text);
	return result;
}

rpl::producer<Qt::MouseButton> Tasks::Task::removeClicks() const {
	return _remove ? _remove->clicks() : rpl::never<Qt::MouseButton>();
}

Tasks::Tasks(
	not_null<Ui::BoxContent*> box,
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller,
	ChatHelpers::TabbedPanel *emojiPanel,
	std::vector<TodoListItem> existing,
	bool existingLocked)
: _box(box)
, _container(container)
, _controller(controller)
, _existingCount(existing.size())
, _existingLocked(existingLocked)
, _emojiPanel(emojiPanel)
, _position(_container->count())
, _tasksLimit(controller->session().appConfig().todoListItemsLimit()) {
	for (const auto &task : existing) {
		addTask(task.id, task.text, anim::type::instant);
	}
	checkLastTask();
}

bool Tasks::full() const {
	return (_list.size() >= _tasksLimit);
}

bool Tasks::hasTasks() const {
	return _hasTasks;
}

bool Tasks::isValid() const {
	return _isValid;
}

rpl::producer<int> Tasks::addedCount() const {
	return _addedCount.value();
}

rpl::producer<not_null<QWidget*>> Tasks::scrollToWidget() const {
	return _scrollToWidget.events();
}

rpl::producer<> Tasks::backspaceInFront() const {
	return _backspaceInFront.events();
}

rpl::producer<> Tasks::tabbed() const {
	return _tabbed.events();
}

void Tasks::Task::show(anim::type animated) {
	_wrap->show(animated);
}

void Tasks::Task::destroy(FnMut<void()> done) {
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

std::vector<TodoListItem> Tasks::toTodoListItems() const {
	auto result = std::vector<TodoListItem>();
	result.reserve(_list.size());
	auto usedId = 0;
	for (const auto &task : _list) {
		if (task->isGood()) {
			result.push_back(task->toTodoListItem(++usedId));
		} else if (const auto id = task->id()) {
			usedId = std::max(usedId, id);
		}
	}
	return result;
}

void Tasks::focusFirst() {
	const auto locked = _existingLocked ? _existingCount : 0;
	Assert(locked < _list.size());
	FocusAtEnd((_list.begin() + locked)->get()->field());
}

bool Tasks::correctShadows() const {
	// Last one should be without shadow.
	const auto noShadow = ranges::find(
		_list,
		true,
		ranges::not_fn(&Task::hasShadow));
	return (noShadow == end(_list) - 1);
}

void Tasks::fixShadows() {
	if (correctShadows()) {
		return;
	}
	for (auto &option : _list) {
		option->createShadow();
	}
	_list.back()->destroyShadow();
}

void Tasks::removeEmptyTail() {
	// Only one option at the end of options list can be empty.
	// Remove all other trailing empty options.
	// Only last empty and previous option have non-empty placeholders.
	const auto focused = ranges::find_if(
		_list,
		&Task::hasFocus);
	const auto end = _list.end();
	const auto reversed = ranges::views::reverse(_list);
	const auto emptyItem = ranges::find_if(
		reversed,
		ranges::not_fn(&Task::isEmpty)).base();
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

void Tasks::destroy(std::unique_ptr<Task> task) {
	const auto value = task.get();
	task->destroy([=] { removeDestroyed(value); });
	_destroyed.push_back(std::move(task));
}

void Tasks::fixAfterErase() {
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

void Tasks::addEmptyTask() {
	if (!_list.empty() && _list.back()->isEmpty()) {
		return;
	}
	const auto locked = _existingLocked ? _existingCount : 0;
	addTask(
		0, // id
		TextWithEntities(),
		(locked < _list.size()) ? anim::type::normal : anim::type::instant);
}

void Tasks::addTask(
		int id,
		TextWithEntities text,
		anim::type animated) {
	if (full()) {
		return;
	}
	if (_list.size() > 1) {
		(*(_list.end() - 2))->removePlaceholder();
		(*(_list.end() - 2))->toggleRemoveAlways(true);
	}
	const auto locked = id && _existingLocked;
	_list.push_back(std::make_unique<Task>(
		_box,
		_container,
		&_controller->session(),
		id,
		std::move(text),
		_position + _list.size() + _destroyed.size(),
		locked));
	if (!locked) {
		initTaskField(_list.back().get());
	}
	_list.back()->show(animated);
	fixShadows();
}

void Tasks::initTaskField(not_null<Task*> task) {
	const auto field = task->field();
	if (const auto emojiPanel = _emojiPanel) {
		const auto emojiToggle = Ui::AddEmojiToggleToField(
			field,
			_box,
			_controller,
			emojiPanel,
			QPoint(
				-st::createPollOptionFieldPremium.textMargins.right(),
				st::createPollOptionEmojiPositionSkip));
		emojiToggle->shownValue() | rpl::start_with_next([=](bool shown) {
			if (!shown) {
				return;
			}
			_emojiPanelLifetime.destroy();
			emojiPanel->selector()->emojiChosen(
			) | rpl::start_with_next([=](ChatHelpers::EmojiChosen data) {
				if (field->hasFocus()) {
					Ui::InsertEmojiAtCursor(field->textCursor(), data.emoji);
				}
			}, _emojiPanelLifetime);
			emojiPanel->selector()->customEmojiChosen(
			) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
				if (field->hasFocus()) {
					Data::InsertCustomEmoji(field, data.document);
				}
			}, _emojiPanelLifetime);
		}, emojiToggle->lifetime());
	}
	field->submits(
	) | rpl::start_with_next([=] {
		const auto index = findField(field);
		if (_list[index]->isGood() && index + 1 < _list.size()) {
			_list[index + 1]->setFocus();
		}
	}, field->lifetime());
	field->changes(
	) | rpl::start_with_next([=] {
		Ui::PostponeCall(crl::guard(field, [=] {
			validateState();
		}));
	}, field->lifetime());
	field->focusedChanges(
	) | rpl::filter(rpl::mappers::_1) | rpl::start_with_next([=] {
		_scrollToWidget.fire_copy(field);
	}, field->lifetime());
	field->tabbed(
	) | rpl::start_with_next([=] {
		const auto index = findField(field);
		if (index + 1 < _list.size()) {
			_list[index + 1]->setFocus();
		} else {
			_tabbed.fire({});
		}
	}, field->lifetime());
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

	task->removeClicks(
	) | rpl::start_with_next([=] {
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
}

void Tasks::removeDestroyed(not_null<Task*> task) {
	const auto i = ranges::find(
		_destroyed,
		task.get(),
		&std::unique_ptr<Task>::get);
	Assert(i != end(_destroyed));
	_destroyed.erase(i);
}

void Tasks::validateState() {
	checkLastTask();
	_hasTasks = (ranges::count_if(_list, &Task::isGood) > 0);
	_isValid = _hasTasks && ranges::none_of(_list, &Task::isTooLong);

	const auto lastEmpty = !_list.empty() && _list.back()->isEmpty();
	_addedCount = _list.size()
		- (lastEmpty ? 1 : 0)
		- (_existingLocked ? _existingCount : 0);
}

int Tasks::findField(not_null<Ui::InputField*> field) const {
	const auto result = ranges::find(
		_list,
		field,
		&Task::field) - begin(_list);

	Ensures(result >= 0 && result < _list.size());
	return result;
}

void Tasks::checkLastTask() {
	removeEmptyTail();
	addEmptyTask();
}

} // namespace

CreateTodoListBox::CreateTodoListBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	rpl::producer<int> starsRequired,
	Api::SendType sendType,
	SendMenu::Details sendMenuDetails)
: _controller(controller)
, _sendType(sendType)
, _sendMenuDetails([result = sendMenuDetails] { return result; })
, _starsRequired(std::move(starsRequired))
, _titleLimit(controller->session().appConfig().todoListTitleLimit()) {
}

auto CreateTodoListBox::submitRequests() const -> rpl::producer<Result> {
	return _submitRequests.events();
}

void CreateTodoListBox::setInnerFocus() {
	_setInnerFocus();
}

void CreateTodoListBox::submitFailed(const QString &error) {
	showToast(error);
}

not_null<Ui::InputField*> CreateTodoListBox::setupTitle(
		not_null<Ui::VerticalLayout*> container) {
	using namespace Settings;

	const auto session = &_controller->session();
	const auto isPremium = session->premium();

	const auto title = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::createPollField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_todo_create_title_placeholder()),
		st::createPollFieldPadding
			+ (isPremium
				? QMargins(0, 0, st::defaultComposeFiles.emoji.inner.width, 0)
				: QMargins()));
	InitField(getDelegate()->outerContainer(), title, session);
	title->setMaxLength(_titleLimit + kErrorLimit);
	title->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
	title->customTab(true);

	if (isPremium) {
		_emojiPanel = MakeEmojiPanel(
			getDelegate()->outerContainer(),
			_controller);
		const auto emojiToggle = Ui::AddEmojiToggleToField(
			title,
			this,
			_controller,
			_emojiPanel.get(),
			st::createPollOptionFieldPremiumEmojiPosition);
		_emojiPanel->selector()->emojiChosen(
		) | rpl::start_with_next([=](ChatHelpers::EmojiChosen data) {
			if (title->hasFocus()) {
				Ui::InsertEmojiAtCursor(title->textCursor(), data.emoji);
			}
		}, emojiToggle->lifetime());
		_emojiPanel->selector()->customEmojiChosen(
		) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
			if (title->hasFocus()) {
				Data::InsertCustomEmoji(title, data.document);
			}
		}, emojiToggle->lifetime());
	}

	const auto warning = CreateWarningLabel(
		container,
		title,
		_titleLimit,
		kWarnTitleLimit);
	rpl::combine(
		title->geometryValue(),
		warning->sizeValue()
	) | rpl::start_with_next([=](QRect geometry, QSize label) {
		warning->moveToLeft(
			(container->width()
				- label.width()
				- st::createPollWarningPosition.x()),
			(geometry.y()
				- st::createPollFieldPadding.top()
				- st::defaultSubsectionTitlePadding.bottom()
				- st::defaultSubsectionTitle.style.font->height
				+ st::defaultSubsectionTitle.style.font->ascent
				- st::createPollWarning.style.font->ascent),
			geometry.width());
	}, warning->lifetime());

	return title;
}

object_ptr<Ui::RpWidget> CreateTodoListBox::setupContent() {
	using namespace Settings;

	const auto id = FullMsgId{
		PeerId(),
		_controller->session().data().nextNonHistoryEntryId(),
	};
	const auto error = lifetime().make_state<Errors>(Error::Title);

	auto result = object_ptr<Ui::VerticalLayout>(this);
	const auto container = result.data();

	const auto title = setupTitle(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_todo_create_list(),
			st::defaultSubsectionTitle),
		st::createPollFieldTitlePadding);
	const auto tasks = lifetime().make_state<Tasks>(
		this,
		container,
		_controller,
		_emojiPanel ? _emojiPanel.get() : nullptr);
	auto limit = tasks->addedCount() | rpl::after_next([=](int count) {
		setCloseByEscape(!count);
		setCloseByOutsideClick(!count);
	}) | rpl::map([=](int count) {
		const auto appConfig = &_controller->session().appConfig();
		const auto max = appConfig->todoListItemsLimit();
		return (count < max)
			? tr::lng_todo_create_limit(tr::now, lt_count, max - count)
			: tr::lng_todo_create_maximum(tr::now);
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

	title->tabbed(
	) | rpl::start_with_next([=] {
		tasks->focusFirst();
	}, title->lifetime());

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_todo_create_settings());

	const auto allowAdd = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			tr::lng_todo_create_allow_add(tr::now),
			true,
			st::defaultCheckbox),
		st::createPollCheckboxMargin);
	const auto allowMark = container->add(
		object_ptr<Ui::Checkbox>(
			container,
			tr::lng_todo_create_allow_mark(tr::now),
			true,
			st::defaultCheckbox),
		st::createPollCheckboxMargin);

	tasks->tabbed(
	) | rpl::start_with_next([=] {
		title->setFocus();
	}, title->lifetime());

	const auto isValidTitle = [=] {
		const auto text = title->getLastText().trimmed();
		return !text.isEmpty() && (text.size() <= _titleLimit);
	};
	title->submits(
	) | rpl::start_with_next([=] {
		if (isValidTitle()) {
			tasks->focusFirst();
		}
	}, title->lifetime());

	_setInnerFocus = [=] {
		title->setFocusFast();
	};

	const auto collectResult = [=] {
		const auto textWithTags = title->getTextWithTags();
		using Flag = TodoListData::Flag;
		auto result = TodoListData(&_controller->session().data(), id);
		result.title.text = textWithTags.text;
		result.title.entities = TextUtilities::ConvertTextTagsToEntities(
			textWithTags.tags);
		TextUtilities::Trim(result.title);
		result.items = tasks->toTodoListItems();
		const auto allowAddTasks = allowAdd->checked();
		const auto allowMarkTasks = allowMark->checked();
		result.setFlags(Flag(0)
			| (allowAddTasks ? Flag::OthersCanAppend : Flag(0))
			| (allowMarkTasks ? Flag::OthersCanComplete : Flag(0)));
		return result;
	};
	const auto collectError = [=] {
		if (isValidTitle()) {
			*error &= ~Error::Title;
		} else {
			*error |= Error::Title;
		}
		if (!tasks->hasTasks()) {
			*error |= Error::Tasks;
		} else if (!tasks->isValid()) {
			*error |= Error::Other;
		} else {
			*error &= ~(Error::Tasks | Error::Other);
		}
	};
	const auto showError = [show = uiShow()](
			tr::phrase<> text) {
		show->showToast(text(tr::now));
	};

	const auto send = [=](Api::SendOptions sendOptions) {
		collectError();
		if (*error & Error::Title) {
			showError(tr::lng_todo_choose_title);
			title->setFocus();
		} else if (*error & Error::Tasks) {
			showError(tr::lng_todo_choose_tasks);
			tasks->focusFirst();
		} else if (!*error) {
			_submitRequests.fire({ collectResult(), sendOptions });
		}
	};
	const auto sendAction = SendMenu::DefaultCallback(
		_controller->uiShow(),
		crl::guard(this, send));

	tasks->scrollToWidget(
	) | rpl::start_with_next([=](not_null<QWidget*> widget) {
		scrollToWidget(widget);
	}, lifetime());

	tasks->backspaceInFront(
	) | rpl::start_with_next([=] {
		FocusAtEnd(title);
	}, lifetime());

	const auto isNormal = (_sendType == Api::SendType::Normal);
	const auto schedule = [=] {
		sendAction(
			{ .type = SendMenu::ActionType::Schedule },
			_sendMenuDetails());
	};
	const auto submit = addButton(
		tr::lng_todo_create_button(),
		[=] { isNormal ? send({}) : schedule(); });
	submit->setText(PaidSendButtonText(_starsRequired.value(), isNormal
		? tr::lng_todo_create_button()
		: tr::lng_schedule_button()));
	const auto sendMenuDetails = [=] {
		collectError();
		return (*error) ? SendMenu::Details() : _sendMenuDetails();
	};
	SendMenu::SetupMenuAndShortcuts(
		submit.data(),
		_controller->uiShow(),
		sendMenuDetails,
		sendAction);
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	return result;
}

void CreateTodoListBox::prepare() {
	setTitle(tr::lng_todo_create_title());

	const auto inner = setInnerWidget(setupContent());

	setDimensionsToContent(st::boxWideWidth, inner);
}

AddTodoListTasksBox::AddTodoListTasksBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item)
: _controller(controller)
, _item(item) {
	_controller->session().changes().messageUpdates(
		Data::MessageUpdate::Flag::Destroyed
	) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		if (update.item == item) {
			closeBox();
		}
	}, lifetime());
}

void AddTodoListTasksBox::prepare() {
	setTitle(tr::lng_todo_add_title());

	const auto inner = setInnerWidget(setupContent());

	setDimensionsToContent(st::boxWideWidth, inner);

	scrollToY(ScrollMax);
}

object_ptr<Ui::RpWidget> AddTodoListTasksBox::setupContent() {
	auto result = object_ptr<Ui::VerticalLayout>(this);
	const auto container = result.data();

	if (_controller->session().premium()) {
		_emojiPanel = MakeEmojiPanel(
			getDelegate()->outerContainer(),
			_controller);
	}

	const auto media = _item->media();
	const auto todolist = media ? media->todolist() : nullptr;
	Assert(todolist != nullptr);
	const auto tasks = lifetime().make_state<Tasks>(
		this,
		container,
		_controller,
		_emojiPanel ? _emojiPanel.get() : nullptr,
		todolist->items,
		true);
	const auto already = int(todolist->items.size());
	auto limit = tasks->addedCount() | rpl::after_next([=](int count) {
		setCloseByEscape(!count);
		setCloseByOutsideClick(!count);
	}) | rpl::map([=](int count) {
		const auto appConfig = &_controller->session().appConfig();
		const auto max = appConfig->todoListItemsLimit();
		const auto total = already + count;
		return (total < max)
			? tr::lng_todo_create_limit(tr::now, lt_count, max - total)
			: tr::lng_todo_create_maximum(tr::now);
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

	_setInnerFocus = [=] {
		tasks->focusFirst();
	};

	tasks->scrollToWidget(
	) | rpl::start_with_next([=](not_null<QWidget*> widget) {
		scrollToWidget(widget);
	}, lifetime());

	const auto submit = addButton(tr::lng_settings_save(), [=] {
		_submitRequests.fire({ tasks->toTodoListItems() });
	});
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	return result;
}

auto AddTodoListTasksBox::submitRequests() const -> rpl::producer<Result> {
	return _submitRequests.events();
}

void AddTodoListTasksBox::setInnerFocus() {
	_setInnerFocus();
}
