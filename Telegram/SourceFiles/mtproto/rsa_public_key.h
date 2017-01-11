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

namespace MTP {
namespace internal {

// this class holds an RSA public key and can encrypt fixed-size messages with it
class RSAPublicKey final {
public:

	// key in RSAPublicKey "-----BEGIN RSA PUBLIC KEY----- ..." format
	RSAPublicKey(const char *key);

	bool isValid() const;
	uint64 getFingerPrint() const;

	// data has exactly 256 chars to be encrypted
	bool encrypt(const void *data, std::string &result) const;

private:

	struct Impl;
	typedef QSharedPointer<Impl> ImplPtr;
	ImplPtr impl_;

};

} // namespace internal
} // namespace MTP
