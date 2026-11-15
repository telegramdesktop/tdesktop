<p align="center">
  <img src="docs/readme/logo.png" alt="mokaGram" width="120">
</p>

<h1 align="center">mokaGram</h1>

<p align="center">
  Клиент на основе Telegram Desktop с переработанным дизайном и новыми функциями.
</p>

<p align="center">
  <a href="https://github.com/mokagram/mokagram/releases"><img src="https://img.shields.io/github/v/release/mokagram/mokagram?include_prereleases&style=for-the-badge&label=релиз&color=0a84ff&labelColor=282828" alt="Релиз"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/лицензия-GPLv3-8e8e93?style=for-the-badge&labelColor=282828" alt="GPLv3"></a>
  <a href="https://t.me/ecronx"><img src="https://img.shields.io/badge/автор-@ecronx-8e8e93?style=for-the-badge&labelColor=282828&logo=telegram&logoColor=white" alt="@ecronx"></a>
</p>

<p align="center">
  <img src="docs/readme/chat-full.webp" alt="mokaGram: окно чата" width="86%">
</p>

## О проекте

**mokaGram** — кастомный форк Telegram Desktop с полностью переработанным визуалом в md3-стиле и кучей новых полезных функций. Все секции в настройках лежат отдельными скруглёнными карточками, каждая функция включается отдельно, и включать нужно только то, что нужно.

Клиент работает с обычным аккаунтом Telegram и обычными серверами. Плагины из экстеры, но на ПК: свой API, Python runtime и магазин прямо в клиенте.

## Что умеет

- **Приватность**: режим стримера прячет номер, юзернейм и ID, режим призрака не отмечает прочитанным и скрывает, что ты в сети
- **Удалённые и правки** сообщений
- **Безопасность**
- **Музыка**: SoundCloud и треки из Telegram, плейлисты, синхронизация лайков, текст песни и комментарии с таймкодами
- **Плеер**: обычная панель или Dynamic Island, на macOS отдельный островок у верхнего края экрана
- **Быстрые команды**
- **Система**: анти-лаг и экономия в фоне
- **Кастомизация** интерфейса
- **Инструменты**: скриншот сообщений карточкой
- **Плагины** в формате `.plugin`, Python runtime и магазин прямо в клиенте

Всё лежит в одном месте: **Настройки → Настройки mokaGram**. Подробно со скриншотами: [docs/FEATURES.md](docs/FEATURES.md).

## Скачать

| Система | Версия | Файл |
|---|---|---|
| **macOS Apple Silicon (arm64)** | v0.1.9 | [mokaGram-7.3.3-arm64.dmg](https://github.com/mokagram/mokagram/releases/download/v0.1.9/mokaGram-7.3.3-arm64.dmg) |
| **Windows** | v0.1.7 | [mokagram-win-x64.zip](https://github.com/mokagram/mokagram/releases/download/v0.1.7/mokagram-win-x64.zip) |
| **Linux** | v0.1.3 | [AppImage](https://github.com/mokagram/mokagram/releases/download/v0.1.3/mokagram-x86_64.AppImage) · [.deb](https://github.com/mokagram/mokagram/releases/download/v0.1.3/mokagram_0.1.3_amd64.deb) · [.rpm](https://github.com/mokagram/mokagram/releases/download/v0.1.3/mokagram-0.1.3-1.x86_64.rpm) · [pacman](https://github.com/mokagram/mokagram/releases/download/v0.1.3/mokagram-0.1.3-1-x86_64.pkg.tar.zst) · [tar.xz](https://github.com/mokagram/mokagram/releases/download/v0.1.3/mokagram-linux-x64.tar.xz) |

macOS: откройте DMG и перетащите **mokaGram** в **Applications**. Версия релиза mokaGram: **v0.1.9**, версия клиента в macOS: **7.3.3**. В этом релизе обновлена только сборка для Apple Silicon; ссылки Windows и Linux ведут на предыдущие версии.

Сборки macOS и Windows без подписи разработчика, система может предупредить при первом запуске.

### Новое в v0.1.9

- Плеер с Dynamic Island, анимациями и цветами обложки или Canvas; на macOS островок работает отдельно от окна клиента.
- Импорт и синхронизация плейлистов SoundCloud, подборки, похожие исполнители и просмотр комментариев по времени трека.
- Выбор чатов и каналов для поиска музыки, закрепление источников, лайки локальных треков через SoundCloud.
- Плавающее поле ввода, обновлённые пузыри сообщений, настройки производительности на отдельной странице и новый DMG с подсказкой установки.

Все изменения: [release notes v0.1.9](https://github.com/mokagram/mokagram/releases/tag/v0.1.9).

## Плагины

Плагин: один файл `.plugin` с классом, наследующим `BasePlugin`. Метаданные, права и команды лежат в самом файле, лимит 300 действий в минуту, каждое действие пишется в журнал. Плагины выполняются на Python runtime и работают с клиентом через API mokaGram.

Полное описание: [docs-plugins-api.md](docs-plugins-api.md), пример: [examples/showcase.plugin](examples/showcase.plugin).

## Сборка

mokaGram собирается так же, как Telegram Desktop:

- [macOS](docs/building-mac.md)
- [Windows](docs/building-win.md)
- [GNU/Linux (Docker)](docs/building-linux.md)

Устройство репозитория: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md). Исходники Telegram Desktop и список сторонних библиотек: [telegramdesktop/tdesktop](https://github.com/telegramdesktop/tdesktop).

## Авторство и лицензия

> **Основано на mokaGram, автор [@ecronx](https://t.me/ecronx) (https://t.me/ecronx). https://github.com/mokagram/mokagram**

mokaGram основан на [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) (© The Telegram Desktop Authors, GPLv3 с исключением OpenSSL). Доработки © 2026 [@ecronx](https://t.me/ecronx), лицензия та же, GPLv3, плюс условие об сохранении авторства.

Кто берёт исходники или сборки mokaGram, обязан сохранить [уведомление](MOKAGRAM_NOTICE.md) и указать авторство в README и в окне «О программе». При нарушении лицензия прекращается.

*Anyone using mokaGram source code **must credit the author** ([@ecronx](https://t.me/ecronx), https://github.com/mokagram/mokagram) in the README and the About window and keep the [notice](MOKAGRAM_NOTICE.md).*

<p align="center">
  <a href="https://t.me/ecronx">@ecronx</a> · <a href="https://github.com/mokagram/mokagram/releases">релизы</a>
</p>
