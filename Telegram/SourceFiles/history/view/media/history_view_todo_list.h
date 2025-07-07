/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "ui/effects/animations.h"
#include "data/data_todo_list.h"
#include "base/weak_ptr.h"

namespace Ui {
class RippleAnimation;
class FireworksAnimation;
} // namespace Ui

namespace HistoryView {

class Message;

class TodoList final : public Media {
public:
	TodoList(
		not_null<Element*> parent,
		not_null<TodoListData*> todolist,
		Element *replacing);
	~TodoList();

	void draw(Painter &p, const PaintContext &context) const override;
	TextState textState(QPoint point, StateRequest request) const override;

	bool toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const override {
		return true;
	}
	bool dragItemByHandler(const ClickHandlerPtr &p) const override {
		return true;
	}

	bool needsBubble() const override {
		return true;
	}
	bool customInfoLayout() const override {
		return false;
	}

	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	uint16 fullSelectionLength() const override;
	TextForMimeData selectedText(TextSelection selection) const override;

	void paintBubbleFireworks(
		Painter &p,
		const QRect &bubble,
		crl::time ms) const override;

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

	void unloadHeavyPart() override;
	bool hasHeavyPart() const override;

	void hideSpoilers() override;

	std::vector<TodoTaskInfo> takeTasksInfo() override;

private:
	struct Task;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

	[[nodiscard]] bool canComplete() const;

	[[nodiscard]] int countTaskTop(
		const Task &task,
		int innerWidth) const;
	[[nodiscard]] int countTaskHeight(
		const Task &task,
		int innerWidth) const;
	[[nodiscard]] ClickHandlerPtr createTaskClickHandler(
		const Task &task);
	void updateTexts();
	void updateTasks(bool skipAnimations);
	void startToggleAnimation(Task &task);
	void updateCompletionStatus();
	void maybeStartFireworks();
	void setupPreviousState(const std::vector<TodoTaskInfo> &info);

	int paintTask(
		Painter &p,
		const Task &task,
		int left,
		int top,
		int width,
		int outerWidth,
		const PaintContext &context) const;
	void paintRadio(
		Painter &p,
		const Task &task,
		int left,
		int top,
		const PaintContext &context) const;
	void paintStatus(
		Painter &p,
		const Task &task,
		int left,
		int top,
		const PaintContext &context) const;
	void paintBottom(
		Painter &p,
		int left,
		int top,
		int paintw,
		const PaintContext &context) const;
	void appendTaskHighlight(
		int id,
		int top,
		int height,
		const PaintContext &context) const;

	void radialAnimationCallback() const;

	void toggleRipple(Task &task, bool pressed);
	void toggleCompletion(int id);

	[[nodiscard]] int bottomButtonHeight() const;

	const not_null<TodoListData*> _todolist;
	int _todoListVersion = 0;
	int _total = 0;
	int _incompleted = 0;
	TodoListData::Flags _flags = TodoListData::Flags();

	Ui::Text::String _title;
	Ui::Text::String _subtitle;

	std::vector<Task> _tasks;
	Ui::Text::String _completionStatusLabel;

	mutable std::unique_ptr<Ui::FireworksAnimation> _fireworksAnimation;
	mutable QPoint _lastLinkPoint;
	mutable QImage _userpicCircleCache;
	mutable QImage _fillingIconCache;

};

} // namespace HistoryView
