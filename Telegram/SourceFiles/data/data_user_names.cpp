/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_user_names.h"

namespace Data {

UsernamesInfo::UsernamesInfo() = default;

void UsernamesInfo::setUsername(const QString &username) {
	if (_usernames.empty()) {
		if (username.isEmpty()) {
			_indexEditableUsername = -1;
		} else {
			_usernames.push_back(username);
			_indexEditableUsername = 0;
		}
	} else if ((_indexEditableUsername < 0)
			|| (_indexEditableUsername >= _usernames.size())) {
		if (username.isEmpty()) {
			_indexEditableUsername = -1;
		} else {
			_usernames.push_back(username);
			_indexEditableUsername = 0;
		}
	} else if (_usernames[_indexEditableUsername] != username) {
		if (username.isEmpty()) {
			_usernames.erase(begin(_usernames) + _indexEditableUsername);
			_indexEditableUsername = -1;
		} else {
			_usernames[_indexEditableUsername] = username;
		}
	}
}

void UsernamesInfo::setUsernames(const Usernames &usernames) {
	auto editableUsername = QString();
	auto newUsernames = ranges::views::all(
		usernames
	) | ranges::views::filter([&](const Data::Username &username) {
		if (username.editable) {
			editableUsername = username.username;
			return true;
		}
		return username.active;
	}) | ranges::views::transform([](const Data::Username &username) {
		return username.username;
	}) | ranges::to_vector;

	if (!ranges::equal(_usernames, newUsernames)) {
		_usernames = std::move(newUsernames);
	}
	if (!editableUsername.isEmpty()) {
		for (auto i = 0; i < _usernames.size(); i++) {
			if (_usernames[i] == editableUsername) {
				_indexEditableUsername = i;
				break;
			}
		}
	} else {
		_indexEditableUsername = -1;
	}
}

QString UsernamesInfo::username() const {
	return _usernames.empty() ? QString() : _usernames.front();
}

QString UsernamesInfo::editableUsername() const {
	return (_indexEditableUsername < 0)
		? QString()
		: _usernames[_indexEditableUsername];
}

const std::vector<QString> &UsernamesInfo::usernames() const {
	return _usernames;
}

} // namespace Data
