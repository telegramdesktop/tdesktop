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
	if (_indexEditableUsername < 0) {
		if (!username.isEmpty()) {
			_indexEditableUsername = int(_usernames.size());
			_usernames.push_back(username);
		}
	} else if (username.isEmpty()) {
		_usernames.erase(begin(_usernames) + _indexEditableUsername);
		_indexEditableUsername = -1;
	} else {
		_usernames[_indexEditableUsername] = username;
	}
}

void UsernamesInfo::setUsernames(const Usernames &usernames) {
	// Bots may deactivate the editable username, leaving it out of the list.
	_usernames.clear();
	_indexEditableUsername = -1;
	for (const auto &username : usernames) {
		if (username.active) {
			if (username.editable) {
				_indexEditableUsername = int(_usernames.size());
			}
			_usernames.push_back(username.username);
		}
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

bool UsernamesInfo::isEditable(const QString &username) const {
	return (_indexEditableUsername >= 0)
		&& (_indexEditableUsername < _usernames.size())
		&& (_usernames[_indexEditableUsername] == username);
}

} // namespace Data
