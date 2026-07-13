/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/stickers/data_stickers.h"
#include "mtproto/sender.h"

class DocumentData;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class PopupMenu;
namespace Menu {
struct MenuCallback;
} // namespace Menu
} // namespace Ui

namespace Api {

inline constexpr auto kStickersInOwnedSetMax = 120;
inline constexpr auto kEmojiInOwnedSetMax = 200;
inline constexpr auto kEmojiStickerSideMax = 100;

void AddExistingStickerToSet(
	not_null<Main::Session*> session,
	const StickerSetIdentifier &set,
	not_null<DocumentData*> document,
	const QString &emoji,
	Fn<void(MTPmessages_StickerSet)> done,
	Fn<void(QString)> fail);

void DeleteStickerSet(
	not_null<Main::Session*> session,
	const StickerSetIdentifier &set,
	Fn<void()> done,
	Fn<void(QString)> fail);

[[nodiscard]] bool HasOwnedStickerSets(not_null<Main::Session*> session);
[[nodiscard]] bool HasOwnedEmojiSets(not_null<Main::Session*> session);

[[nodiscard]] QString StickerEmojiOrDefault(
	not_null<DocumentData*> document);

void FillChooseStickerSetMenu(
	not_null<Ui::PopupMenu*> menu,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<DocumentData*> document);

void FillChooseEmojiSetMenu(
	not_null<Ui::PopupMenu*> menu,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<DocumentData*> document);

void AddAddToStickerSetAction(
	const Ui::Menu::MenuCallback &addAction,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<DocumentData*> document);

void AddAddToEmojiSetAction(
	const Ui::Menu::MenuCallback &addAction,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<DocumentData*> document);

void AddAddToOwnedSetAction(
	const Ui::Menu::MenuCallback &addAction,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<DocumentData*> document);

class StickerUpload final : public base::has_weak_ptr {
public:
	StickerUpload(
		not_null<Main::Session*> session,
		StickerSetIdentifier set,
		QByteArray webpBytes,
		QString emoji,
		Data::StickersType type = Data::StickersType::Stickers);
	~StickerUpload();

	void start(
		Fn<void(MTPmessages_StickerSet)> done,
		Fn<void(QString)> fail,
		Fn<void(int /*percent*/)> progress = nullptr);

	void cancel();

private:
	void uploadReady(const MTPInputFile &file);
	void uploadFailed();
	void uploadProgressed();
	void requestAddSticker(const MTPInputDocument &document);

	const not_null<Main::Session*> _session;
	StickerSetIdentifier _set;
	QByteArray _bytes;
	QString _emoji;
	Data::StickersType _type = Data::StickersType::Stickers;
	MTP::Sender _api;
	rpl::lifetime _uploadLifetime;
	FullMsgId _uploadId;
	DocumentId _documentId = 0;
	mtpRequestId _addRequestId = 0;

	Fn<void(MTPmessages_StickerSet)> _done;
	Fn<void(QString)> _fail;
	Fn<void(int)> _progress;
	int _lastReportedPercent = -1;

};

} // namespace Api
