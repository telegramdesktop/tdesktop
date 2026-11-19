// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: 4ecdc7a8
// Mokagram: окно «Музыка» (поиск SoundCloud, очередь, воспроизведение через плеер клиента).
#pragma once

#include <QtCore/QPoint>
#include <QtCore/QString>
#include <QtGui/QColor>

#include "rpl/producer.h"

class AudioMsgId;
class QImage;

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Mokagram::Music {

struct Track;

void ShowMusicBox(
	not_null<Window::SessionController*> controller,
	const QString &initialQuery = QString());

// Окно музыки сразу в поиске альбомов; альбомы с треком trackId (SoundCloud) идут первыми.
void ShowAlbumsBox(
	not_null<Window::SessionController*> controller,
	const QString &query,
	const QString &trackId = QString());

// Мост к плееру клиента: если играет трек из очереди SoundCloud, кнопки
// «вперёд/назад», повтор и перемешивание работают по этой очереди.
// Мост для панели плеера в баре Omarchy (Quickshell), см. StartShellBridge.
void StartShellBridge();

[[nodiscard]] bool OwnsTrack(const AudioMsgId &id);
[[nodiscard]] bool UsesQueue(const AudioMsgId &id);
[[nodiscard]] bool NextAvailable();
[[nodiscard]] bool PreviousAvailable();
void Step(int delta);
[[nodiscard]] rpl::producer<> QueueChanges();
// Текущий трек очереди SoundCloud, пустой для обычных аудио Telegram.
[[nodiscard]] Track CurrentQueueTrack();

// Название и исполнитель текущего трека из очереди (false, если играет чужой трек).
[[nodiscard]] bool CurrentTrackInfo(QString &title, QString &artist);

// Главный цвет обложки играющего трека (невалидный, если выключено в настройках
// или обложка ещё не загружена).
[[nodiscard]] QColor CurrentCoverAccent();
[[nodiscard]] QImage CoverForPlayback(const AudioMsgId &id);

// Меню по правой кнопке на панели плеера: то же, что в окне музыки, плюс управление.
void ShowCurrentTrackMenu(
	not_null<Window::SessionController*> controller,
	QPoint globalPosition,
	QWidget *parent = nullptr,
	bool islandMenu = false);

// Полноценный плеер: обложка, перемотка, кнопки, очередь.
void ShowPlayerBox(not_null<Window::SessionController*> controller);

// Полноэкранный плеер поверх окна: открывается по клику на обложку в мини-плеере.
void ShowFullscreenPlayer(not_null<Window::SessionController*> controller);
void ShowFullscreenPlayerHome(not_null<Window::SessionController*> controller);

// Панель с текстом песни справа от чата. Кнопка в панели плеера переключает её,
// anchor - сама панель плеера: под ней и начинается текст.
void ToggleLyricsPanel(
	not_null<Window::SessionController*> controller,
	not_null<Ui::RpWidget*> anchor);
// Показать панель, если она включена (вызывается при создании панели плеера).
void SyncLyricsPanel(
	not_null<Window::SessionController*> controller,
	not_null<Ui::RpWidget*> anchor);

// Значок комментариев в баре, когда у трека нет текста песни.
void AttachCommentsToPlayer(
	not_null<Window::SessionController*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::RpWidget*> anchor,
	bool compactTimeline = false,
	Fn<bool()> available = nullptr);

// Найден ли текст для играющего трека (кнопка в плеере неактивна, пока нет).
[[nodiscard]] bool LyricsAvailable();
[[nodiscard]] rpl::producer<> LyricsAvailabilityChanges();

void DebugSaveTrack(int index);
void DebugPlay(int index);
void DebugGrabFullscreen(const QString &path);
void DebugLike(int index);
void DebugTab(int tab);

} // namespace Mokagram::Music
