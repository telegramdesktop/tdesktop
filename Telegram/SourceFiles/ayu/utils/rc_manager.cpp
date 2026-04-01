// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/utils/rc_manager.h"

std::unordered_set<ID> default_developers = {};

std::unordered_set<ID> default_channels = {};

void RCManager::start() {
	DEBUG_LOG(("RCManager: starting"));
	initialized = true;
	_developers.clear();
	_officialChannels.clear();
	_supporters.clear();
	_supporterChannels.clear();
	_customBadges.clear();
}

void RCManager::makeRequest() {
	initialized = true;
}

void RCManager::sendRequest() {
	initialized = true;
}

bool RCManager::tryRetryWithExteraFallback() {
	return false;
}

void RCManager::gotResponse() {
	initialized = true;
}

void RCManager::gotFailure(QNetworkReply::NetworkError e) {
	Q_UNUSED(e);
	initialized = true;
}

void RCManager::clearSentRequest() {
	initialized = true;
}

bool RCManager::handleResponse(const QByteArray &response) {
	Q_UNUSED(response);
	return true;
}

bool RCManager::applyResponse(const QByteArray &response) {
	Q_UNUSED(response);
	return true;
}

RCManager::~RCManager() {
	clearSentRequest();
	_manager = nullptr;
}
