/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core {

class MainQueueProcessor : public QObject {
public:
	MainQueueProcessor();

	~MainQueueProcessor();

protected:
	bool event(QEvent *event) override;

private:
	void acquire();
	void release();

};

} // namespace Core
