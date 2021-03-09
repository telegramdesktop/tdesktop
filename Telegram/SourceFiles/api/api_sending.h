/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;
class PhotoData;
class DocumentData;
struct FileLoadResult;

namespace Api {

struct MessageToSend;
struct SendAction;

void SendExistingDocument(
	Api::MessageToSend &&message,
	not_null<DocumentData*> document);

void SendExistingPhoto(
	Api::MessageToSend &&message,
	not_null<PhotoData*> photo);

bool SendDice(Api::MessageToSend &message);

void FillMessagePostFlags(
	const SendAction &action,
	not_null<PeerData*> peer,
	MTPDmessage::Flags &flags);

void SendConfirmedFile(
	not_null<Main::Session*> session,
	const std::shared_ptr<FileLoadResult> &file);

} // namespace Api
