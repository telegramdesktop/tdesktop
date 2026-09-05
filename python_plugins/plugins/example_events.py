def on_load(api):
    api.log("Example plugin loaded")


def on_message(message):
    print(
        "message:",
        message.chat.id,
        message.sender.id,
        message.text,
    )
