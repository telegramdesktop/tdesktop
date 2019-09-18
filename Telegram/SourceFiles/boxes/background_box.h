/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {
class WallPaper;
} // namespace Data

class BackgroundBox : public Ui::BoxContent {
public:
	BackgroundBox(QWidget*, not_null<Main::Session*> session);

protected:
	void prepare() override;

private:
	class Inner;

	void removePaper(const Data::WallPaper &paper);

	const not_null<Main::Session*> _session;

	QPointer<Inner> _inner;

};
