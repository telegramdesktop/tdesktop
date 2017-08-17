/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "base/task_queue.h"

#include <thread>
#include <condition_variable>

namespace base {
namespace {

auto MainThreadId = std::this_thread::get_id();
const auto MaxThreadsCount = qMax(std::thread::hardware_concurrency(), 2U);

} // namespace

class TaskQueue::TaskQueueList {
public:
	TaskQueueList();

	void Register(TaskQueue *queue);
	void Unregister(TaskQueue *queue);
	bool IsInList(TaskQueue *queue) const;
	void Clear();
	bool Empty(int list_index_) const;
	TaskQueue *TakeFirst(int list_index_);

private:
	void Insert(TaskQueue *queue, int list_index_);
	void Remove(TaskQueue *queue, int list_index_);

	TaskQueue *Tail() { return &tail_; }
	const TaskQueue *Tail() const { return &tail_; }

	TaskQueue tail_ = { Type::Special, Priority::Normal };
	TaskQueue *(lists_[kQueuesListsCount]);

};

class TaskQueue::TaskThreadPool {
	struct Private {
	};

public:
	TaskThreadPool(const Private &) { }
	static const std::shared_ptr<TaskThreadPool> &Instance();

	void AddQueueTask(TaskQueue *queue, Task &&task);
	void RemoveQueue(TaskQueue *queue);

	~TaskThreadPool();

private:
	void ThreadFunction();

	std::vector<std::thread> threads_;
	QMutex queues_mutex_;

	// queues_mutex_ must be locked when working with the list.
	TaskQueueList queue_list_;

	QWaitCondition thread_condition_;
	bool stopped_ = false;
	int tasks_in_process_ = 0;
	int background_tasks_in_process_ = 0;

};

TaskQueue::TaskQueueList::TaskQueueList() {
	for (auto &list : lists_) {
		list = &tail_;
	}
}

void TaskQueue::TaskQueueList::Register(TaskQueue *queue) {
	Assert(!queue->SerialTaskInProcess());

	Insert(queue, kAllQueuesList);
	if (queue->priority_ == Priority::Normal) {
		Insert(queue, kOnlyNormalQueuesList);
	}
}

void TaskQueue::TaskQueueList::Unregister(TaskQueue *queue) {
	Remove(queue, kAllQueuesList);
	if (queue->priority_ == Priority::Normal) {
		Remove(queue, kOnlyNormalQueuesList);
	}
}

void TaskQueue::TaskQueueList::Insert(TaskQueue *queue, int list_index_) {
	Assert(list_index_ < kQueuesListsCount);

	auto tail = Tail();
	if (lists_[list_index_] == tail) {
		lists_[list_index_] = queue;
	}

	auto &list_entry = queue->list_entries_[list_index_];
	Assert(list_entry.after == nullptr);
	if ((list_entry.before = tail->list_entries_[list_index_].before)) {
		list_entry.before->list_entries_[list_index_].after = queue;
	}
	list_entry.after = tail;
	tail->list_entries_[list_index_].before = queue;
}

void TaskQueue::TaskQueueList::Remove(TaskQueue *queue, int list_index_) {
	Assert(list_index_ < kQueuesListsCount);

	auto &list_entry = queue->list_entries_[list_index_];
	Assert(list_entry.after != nullptr);
	if (lists_[list_index_] == queue) {
		lists_[list_index_] = list_entry.after;
	} else {
		Assert(list_entry.before != nullptr);
		list_entry.before->list_entries_[list_index_].after = list_entry.after;
	}
	list_entry.after->list_entries_[list_index_].before = list_entry.before;
	list_entry.before = list_entry.after = nullptr;
}

bool TaskQueue::TaskQueueList::IsInList(TaskQueue *queue) const {
	if (queue->list_entries_[kAllQueuesList].after) {
		return true;
	}
	Assert(queue->list_entries_[kOnlyNormalQueuesList].after == nullptr);
	return false;
}

void TaskQueue::TaskQueueList::Clear() {
	auto tail = Tail();
	for (int i = 0; i < kQueuesListsCount; ++i) {
		for (auto j = lists_[i], next = j; j != tail; j = next) {
			auto &list_entry = j->list_entries_[i];
			next = list_entry.after;
			list_entry.before = list_entry.after = nullptr;
		}
		lists_[i] = tail;
	}
}

bool TaskQueue::TaskQueueList::Empty(int list_index_) const {
	Assert(list_index_ < kQueuesListsCount);

	auto list = lists_[list_index_];
	Assert(list != nullptr);
	return (list->list_entries_[list_index_].after == nullptr);
}

TaskQueue *TaskQueue::TaskQueueList::TakeFirst(int list_index_) {
	Assert(!Empty(list_index_));

	auto queue = lists_[list_index_];
	Unregister(queue);
//	log_msgs.push_back("Unregistered from list in TakeFirst");
	return queue;
}

void TaskQueue::TaskThreadPool::AddQueueTask(TaskQueue *queue, Task &&task) {
	QMutexLocker lock(&queues_mutex_);

	queue->tasks_.push_back(std::move(task));
	auto list_was_empty = queue_list_.Empty(kAllQueuesList);
	auto threads_count = threads_.size();
	auto all_threads_processing = (threads_count == tasks_in_process_);
	auto some_threads_are_vacant = !all_threads_processing && list_was_empty;
	auto will_create_thread = !some_threads_are_vacant && (threads_count < MaxThreadsCount);

	if (!queue->SerialTaskInProcess()) {
		if (!queue_list_.IsInList(queue)) {
			queue_list_.Register(queue);
		}
	}
	if (will_create_thread) {
		threads_.emplace_back([this]() {
			ThreadFunction();
		});
	} else if (some_threads_are_vacant) {
		Assert(threads_count > tasks_in_process_);
		thread_condition_.wakeOne();
	}
}

void TaskQueue::TaskThreadPool::RemoveQueue(TaskQueue *queue) {
	QMutexLocker lock(&queues_mutex_);
	if (queue_list_.IsInList(queue)) {
		queue_list_.Unregister(queue);
	}
	if (queue->destroyed_flag_) {
		*queue->destroyed_flag_ = true;
	}
}

TaskQueue::TaskThreadPool::~TaskThreadPool() {
	{
		QMutexLocker lock(&queues_mutex_);
		queue_list_.Clear();
		stopped_ = true;
	}
	thread_condition_.wakeAll();
	for (auto &thread : threads_) {
		thread.join();
	}
}

const std::shared_ptr<TaskQueue::TaskThreadPool> &TaskQueue::TaskThreadPool::Instance() { // static
	static auto Pool = std::make_shared<TaskThreadPool>(Private());
	return Pool;
}

void TaskQueue::TaskThreadPool::ThreadFunction() {
	// Flag marking that the previous processed task was
	// with a Background priority. We count all the background
	// tasks being processed.
	bool background_task = false;

	// Saved serial queue pointer. When we process a serial
	// queue task we don't return the queue to the list until
	// the task is processed and we return it on the next cycle.
	TaskQueue *serial_queue = nullptr;
	bool serial_queue_destroyed = false;
	bool task_was_processed = false;
	while (true) {
		Task task;
		{
			QMutexLocker lock(&queues_mutex_);

			// Finish the previous task processing.
			if (task_was_processed) {
				--tasks_in_process_;
			}
			if (background_task) {
				--background_tasks_in_process_;
				background_task = false;
			}
			if (serial_queue) {
				if (!serial_queue_destroyed) {
					serial_queue->destroyed_flag_ = nullptr;
					if (!serial_queue->tasks_.empty()) {
						queue_list_.Register(serial_queue);
					}
				}
				serial_queue = nullptr;
				serial_queue_destroyed = false;
			}

			// Wait for a task to appear in the queues list.
			while (queue_list_.Empty(kAllQueuesList)) {
				if (stopped_) {
					return;
				}
				thread_condition_.wait(&queues_mutex_);
			}

			// Select a task we will be processing.
			auto processing_background = (background_tasks_in_process_ > 0);
			auto take_only_normal = processing_background && !queue_list_.Empty(kOnlyNormalQueuesList);
			auto take_from_list_ = take_only_normal ? kOnlyNormalQueuesList : kAllQueuesList;
			auto queue = queue_list_.TakeFirst(take_from_list_);

			Assert(!queue->tasks_.empty());

			task = std::move(queue->tasks_.front());
			queue->tasks_.pop_front();

			if (queue->type_ == Type::Serial) {
				// Serial queues are returned in the list for processing
				// only after the task is finished.
				serial_queue = queue;
				Assert(serial_queue->destroyed_flag_ == nullptr);
				serial_queue->destroyed_flag_ = &serial_queue_destroyed;
			} else if (!queue->tasks_.empty()) {
				queue_list_.Register(queue);
			}

			++tasks_in_process_;
			task_was_processed = true;
			if (queue->priority_ == Priority::Background) {
				++background_tasks_in_process_;
				background_task = true;
			}
		}

		task();
	}
}

TaskQueue::TaskQueue(Type type, Priority priority)
: type_(type)
, priority_(priority) {
	if (type_ != Type::Main && type_ != Type::Special) {
		weak_thread_pool_ = TaskThreadPool::Instance();
	}
}

TaskQueue::~TaskQueue() {
	if (type_ != Type::Main && type_ != Type::Special) {
		if (auto thread_pool = weak_thread_pool_.lock()) {
			thread_pool->RemoveQueue(this);
		}
	}
}

void TaskQueue::Put(Task &&task) {
	if (type_ == Type::Main) {
		QMutexLocker lock(&tasks_mutex_);
		tasks_.push_back(std::move(task));

		Sandbox::MainThreadTaskAdded();
	} else {
		Assert(type_ != Type::Special);
		TaskThreadPool::Instance()->AddQueueTask(this, std::move(task));
	}
}

void TaskQueue::ProcessMainTasks() { // static
	Assert(std::this_thread::get_id() == MainThreadId);

	while (ProcessOneMainTask()) {
	}
}

void TaskQueue::ProcessMainTasks(TimeMs max_time_spent) { // static
	Assert(std::this_thread::get_id() == MainThreadId);

	auto start_time = getms();
	while (ProcessOneMainTask()) {
		if (getms() >= start_time + max_time_spent) {
			break;
		}
	}
}

bool TaskQueue::ProcessOneMainTask() { // static
	Task task;
	{
		QMutexLocker lock(&Main().tasks_mutex_);
		auto &tasks = Main().tasks_;
		if (tasks.empty()) {
			return false;
		}

		task = std::move(tasks.front());
		tasks.pop_front();
	}

	task();
	return true;
}

bool TaskQueue::IsMyThread() const {
	if (type_ == Type::Main) {
		return (std::this_thread::get_id() == MainThreadId);
	}
	Assert(type_ != Type::Special);
	return false;
}

// Default queues.
TaskQueue &TaskQueue::Main() { // static
	static TaskQueue MainQueue { Type::Main, Priority::Normal };
	return MainQueue;
}

TaskQueue &TaskQueue::Normal() { // static
	static TaskQueue NormalQueue { Type::Concurrent, Priority::Normal };
	return NormalQueue;
}

TaskQueue &TaskQueue::Background() { // static
	static TaskQueue BackgroundQueue { Type::Concurrent, Priority::Background };
	return BackgroundQueue;
}

} // namespace base
