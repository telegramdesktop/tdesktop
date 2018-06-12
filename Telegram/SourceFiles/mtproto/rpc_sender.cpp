/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/rpc_sender.h"

RPCOwnedDoneHandler::RPCOwnedDoneHandler(RPCSender *owner) : _owner(owner) {
	_owner->_rpcRegHandler(this);
}

RPCOwnedDoneHandler::~RPCOwnedDoneHandler() {
	if (_owner) _owner->_rpcUnregHandler(this);
}

RPCOwnedFailHandler::RPCOwnedFailHandler(RPCSender *owner) : _owner(owner) {
	_owner->_rpcRegHandler(this);
}

RPCOwnedFailHandler::~RPCOwnedFailHandler() {
	if (_owner) _owner->_rpcUnregHandler(this);
}
