/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/credits_amount.h"
#include "core/file_utilities.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_chat_participant_status.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_photo.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "history/history_item_components.h"
#include "lang/lang_values.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "platform/platform_specific.h"

#include <crl/crl_on_main.h>

// WHY: the linked support code references these app entries; tests never reach them.
PeerId peerFromMTP(const MTPPeer &peer) {
	Unexpected("peerFromMTP in test_iv_editor_state.");
}

CreditsAmount CreditsAmountFromTL(const MTPStarsAmount *amount) {
	Unexpected("CreditsAmountFromTL in test_iv_editor_state.");
}

ChatAdminRightsInfo::ChatAdminRightsInfo(const MTPChatAdminRights &rights) {
	Unexpected("ChatAdminRightsInfo::ChatAdminRightsInfo in test_iv_editor_state.");
}

ReplyKeyboard::Button::~Button() {
	Unexpected("ReplyKeyboard::Button::~Button in test_iv_editor_state.");
}

Main::Session &DocumentData::session() const {
	Unexpected("DocumentData::session in test_iv_editor_state.");
}

bool DocumentData::isVoiceMessage() const {
	Unexpected("DocumentData::isVoiceMessage in test_iv_editor_state.");
}

bool DocumentData::isAudioFile() const {
	Unexpected("DocumentData::isAudioFile in test_iv_editor_state.");
}

Main::Session &PeerData::session() const {
	Unexpected("PeerData::session in test_iv_editor_state.");
}

const QString &PeerData::name() const {
	Unexpected("PeerData::name in test_iv_editor_state.");
}

Main::Session &PhotoData::session() const {
	Unexpected("PhotoData::session in test_iv_editor_state.");
}

namespace Data {

PremiumLimits::PremiumLimits(not_null<Main::Session*> session)
: _session(session) {
	Unexpected("Data::PremiumLimits::PremiumLimits in test_iv_editor_state.");
}

int PremiumLimits::messageLengthCurrent() const {
	Unexpected("Data::PremiumLimits::messageLengthCurrent in test_iv_editor_state.");
}

HistoryItem *Session::message(FullMsgId itemId) const {
	Unexpected("Data::Session::message in test_iv_editor_state.");
}

UserData *Session::userLoaded(UserId id) const {
	Unexpected("Data::Session::userLoaded in test_iv_editor_state.");
}

not_null<PeerData*> Session::processChat(const MTPChat &data) {
	Unexpected("Data::Session::processChat in test_iv_editor_state.");
}

not_null<PhotoData*> Session::processPhoto(const MTPPhoto &data) {
	Unexpected("Data::Session::processPhoto in test_iv_editor_state.");
}

not_null<DocumentData*> Session::processDocument(
		const MTPDocument &data,
		const MTPVector<MTPDocument> *qualities) {
	Unexpected("Data::Session::processDocument in test_iv_editor_state.");
}

QString SerializeCustomEmojiId(DocumentId id) {
	Unexpected("Data::SerializeCustomEmojiId in test_iv_editor_state.");
}

} // namespace Data

namespace Main {

double AppConfig::getDouble(const QString &key, double fallback) const {
	Unexpected("Main::AppConfig::getDouble in test_iv_editor_state.");
}

AppConfig &Session::appConfig() const {
	Unexpected("Main::Session::appConfig in test_iv_editor_state.");
}

UserId Session::userId() const {
	Unexpected("Main::Session::userId in test_iv_editor_state.");
}

} // namespace Main

namespace File {

void OpenUrl(const QString &url) {
	Unexpected("File::OpenUrl in test_iv_editor_state.");
}

} // namespace File

namespace Platform {

void LaunchMaps(const Data::LocationPoint &point, Fn<void()> fail) {
	Unexpected("Platform::LaunchMaps in test_iv_editor_state.");
}

} // namespace Platform

namespace Lang::details {

bool IsNonDefaultPlural(ushort keyBase) {
	Unexpected("Lang::details::IsNonDefaultPlural in test_iv_editor_state.");
}

} // namespace Lang::details

namespace crl {

rpl::producer<> on_main_update_requests() {
	Unexpected("crl::on_main_update_requests in test_iv_editor_state.");
}

} // namespace crl
