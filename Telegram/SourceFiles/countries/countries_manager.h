/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

namespace Main {
class Domain;
} // namespace Main

namespace Countries {

class Manager final {
public:
	Manager(not_null<Main::Domain*> domain);
	~Manager();

	void read();
	void write() const;

	rpl::lifetime &lifetime();

private:
	void request();

	std::optional<MTP::Sender> _api;
	const QString _path;
	int _hash = 0;

	rpl::lifetime _lifetime;
};

} // namespace Countries
