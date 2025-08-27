## Build instructions for Linux using Docker

### Prepare folder

Choose a folder for the future build, for example **/home/user/TBuild**. It will be named ***BuildPath*** in the rest of this document. All commands will be launched from Terminal.

### Obtain your API credentials

You will require **api_id** and **api_hash** to access the Telegram API servers. To learn how to obtain them [click here][api_credentials].

### Clone source code and prepare libraries

Install [poetry](https://python-poetry.org), go to ***BuildPath*** and run

    git clone --recursive https://github.com/telegramdesktop/tdesktop.git
    ./tdesktop/Telegram/build/prepare/linux.sh

### Building the project

Go to ***BuildPath*/tdesktop** and run (using [your **api_id** and **api_hash**](#obtain-your-api-credentials))

    docker run --rm -it \
        -u $(id -u) \
        -v "$PWD:/usr/src/tdesktop" \
        tdesktop:centos_env \
        /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
        -D TDESKTOP_API_ID=YOUR_API_ID \
        -D TDESKTOP_API_HASH=YOUR_API_HASH

Or, to create a debug build, run (also using [your **api_id** and **api_hash**](#obtain-your-api-credentials))

    docker run --rm -it \
        -u $(id -u) \
        -v "$PWD:/usr/src/tdesktop" \
        -e CONFIG=Debug \
        tdesktop:centos_env \
        /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
        -D TDESKTOP_API_ID=YOUR_API_ID \
        -D TDESKTOP_API_HASH=YOUR_API_HASH

The built files will be in the `out` directory.

### Visual Studio Code integration

Ensure you've followed the instruction up to the [**Clone source code and prepare libraries**](#clone-source-code-and-prepare-libraries) step at least.

Open the repository in Visual Studio Code, install the [Dev Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) extension and add the following to `.vscode/settings.json` (using [your **api_id** and **api_hash**](#obtain-your-api-credentials)):

    {
        "cmake.configureSettings": {
            "TDESKTOP_API_ID": "YOUR_API_ID",
            "TDESKTOP_API_HASH": "YOUR_API_HASH"
        }
    }

After that, choose **Reopen in Container** via the menu triggered by the green button in bottom left corner and you're done.

![Quick actions Status bar item](https://code.visualstudio.com/assets/docs/devcontainers/containers/remote-dev-status-bar.png)

[api_credentials]: api_credentials.md
