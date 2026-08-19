/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "api/api_common.h"
#include "data/data_todo_list.h"
#include "base/flags.h"

struct TodoListData;

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace SendMenu {
struct Details;
} // namespace SendMenu

class EditTodoListBox : public Ui::BoxContent {
public:
	struct Result {
		TodoListData todolist;
		Api::SendOptions options;
	};

	EditTodoListBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		rpl::producer<int> starsRequired,
		Api::SendType sendType,
		SendMenu::Details sendMenuDetails);
	EditTodoListBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item);

	[[nodiscard]] rpl::producer<Result> submitRequests() const;
	void submitFailed(const QString &error);

	void setInnerFocus() override;

protected:
	void prepare() override;

private:
	enum class Error {
		Title = 0x01,
		Tasks = 0x02,
		Other = 0x04,
	};
	friend constexpr inline bool is_flag_type(Error) { return true; }
	using Errors = base::flags<Error>;

	[[nodiscard]] object_ptr<Ui::RpWidget> setupContent();
	[[nodiscard]] not_null<Ui::InputField*> setupTitle(
		not_null<Ui::VerticalLayout*> container);

	const not_null<Window::SessionController*> _controller;
	const Api::SendType _sendType = Api::SendType();
	const Fn<SendMenu::Details()> _sendMenuDetails;
	HistoryItem *_editingItem = nullptr;
	rpl::variable<int> _starsRequired;
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	Fn<void()> _setInnerFocus;
	Fn<rpl::producer<bool>()> _dataIsValidValue;
	rpl::event_stream<Result> _submitRequests;
	int _titleLimit = 0;

};

class AddTodoListTasksBox : public Ui::BoxContent {
public:
	struct Result {
		std::vector<TodoListItem> items;
	};

	AddTodoListTasksBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item);

	[[nodiscard]] rpl::producer<Result> submitRequests() const;

	void setInnerFocus() override;

protected:
	void prepare() override;

private:
	[[nodiscard]] object_ptr<Ui::RpWidget> setupContent();

	const not_null<Window::SessionController*> _controller;
	const not_null<HistoryItem*> _item;
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	Fn<void()> _setInnerFocus;
	rpl::event_stream<Result> _submitRequests;

};
