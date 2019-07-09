## Obtaining your API credentials

To build your version of Telegram Desktop you're required to provide your own **api_id** and **api_hash** for the Telegram API access.

How to obtain your **api_id** and **api_hash** is described here: [https://core.telegram.org/api/obtaining_api_id](https://core.telegram.org/api/obtaining_api_id)

If you're building the application not for deployment, but only for test purposes you can use TEST ONLY credentials, which are very limited by the Telegram API server:

**api_id**: 17349
**api_hash**: 344583e45741c457fe1862106095a5eb

Your users will start getting internal server errors on login if you deploy an app using those **api_id** and **api_hash**.

