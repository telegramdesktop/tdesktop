# Local Assets Admin

Отдельная веб-панель в браузере + локальный Python/SQLite сервер. Не является частью Telegram Desktop и не подключается к инфраструктуре Telegram.

## Запуск (Windows)

1. Установи Python 3.10+.
2. Открой PowerShell в этой папке.
3. Запусти:

```powershell
$env:ADMIN_PASSWORD = "придумай-длинный-пароль"
python server.py
```

4. Открой http://127.0.0.1:8765
5. Войди с паролем из шага 3.

Если ADMIN_PASSWORD не задан, сервер сам создаст временный пароль и напечатает его в консоли.

## Возможности

- Создание локального пользователя с любым номером (номер не проверяется через SMS).
- Одноразовый 6-значный код на 10 минут; код показывается администратору в браузерной панели.
- Начисление/списание внутреннего баланса Stars с журналом.
- Уникальные локальные username.
- Выдача уникальных виртуальных номеров +888.
- Блокировка, удаление, история операций.
- Внутренний магазин: покупки за Stars через API `POST /api/store/buy`.

## API для локального клиентского приложения

- `POST /api/user/verify` — body: `{"phone":"+79991234567","code":"123456"}`.
- После входа использовать токен пользователя как `Authorization: Bearer <token>`.
- `POST /api/store/buy` — body: `{"user_id":1,"item":"theme_dark"}`; товары: `theme_dark` (25), `username_style` (50), `profile_badge` (100).

## Важно

Сервис привязывается только к `127.0.0.1`. Не выставляй порт 8765 в интернет и не используй эту систему для доступа к чужим Telegram-аккаунтам. Коды входа — локальные коды нашей платформы; SMS не отправляются. Stars, username и +888 — внутренние активы этой платформы, они не действуют в официальном Telegram.


## Windows ZIP release

The repository workflow `Package Windows release` packages the Windows x64 `Telegram.exe` together with this local server and browser panel into `LocalAssets-Windows-x64.zip`, then publishes it as a GitHub Release after the x64 build succeeds.
