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
#pragma once

namespace base {

using Task = lambda<void()>;

// An attempt to create/use a TaskQueue or one of the default queues
// after the main() has returned leads to an undefined behaviour.
class TaskQueue {
	enum class Type {
		Main, // Unique queue for main thread tasks.
		Serial,
		Concurrent,
		Special, // Unique special queue for thread pool lists terminal item.
	};

public:
	enum class Priority {
		Normal,
		Background,
	};

	// Creating custom serial queues.
	TaskQueue(Priority priority) : TaskQueue(Type::Serial, priority) {
	}

	// Default main and two concurrent queues.
	static TaskQueue &Main();
	static TaskQueue &Normal();
	static TaskQueue &Background();

	void Put(Task &&task);

	static void ProcessMainTasks();
	static void ProcessMainTasks(TimeMs max_time_spent);

	~TaskQueue();

private:
	static bool ProcessOneMainTask();

	TaskQueue(Type type, Priority priority);

	bool IsMyThread() const;
	bool SerialTaskInProcess() const {
		return (destroyed_flag_ != nullptr);
	}

	const Type type_;
	const Priority priority_;

	QList<Task*> tasks_; // TODO: std_::deque_of_moveable<Task>
	QMutex tasks_mutex_; // Only for the main queue.

	// Only for the other queues, not main.
	class TaskThreadPool;
	QWeakPointer<TaskThreadPool> weak_thread_pool_;

	class TaskQueueList;

	struct TaskQueueListEntry {
		TaskQueue *before = nullptr;
		TaskQueue *after = nullptr;
	};

	// Thread pool queues linked list.
	static constexpr int kAllQueuesList = 0;

	// Thread pool queues linked list with excluded Background queues.
	static constexpr int kOnlyNormalQueuesList = 1;

	static constexpr int kQueuesListsCount = 2;
	TaskQueueListEntry list_entries_[kQueuesListsCount];

	// Only for Serial queues: non-null value means a task is currently processed.
	bool *destroyed_flag_ = nullptr;

};

} // namespace base
