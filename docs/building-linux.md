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
        -v $PWD:/usr/src/tdesktop \
        tdesktop:centos_env \
        /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
        -D TDESKTOP_API_ID=YOUR_API_ID \
        -D TDESKTOP_API_HASH=YOUR_API_HASH

Or, to create a debug build, run (also using [your **api_id** and **api_hash**](#obtain-your-api-credentials))

    docker run --rm -it \
        -v $PWD:/usr/src/tdesktop \
        -e DEBUG=1 \
        tdesktop:centos_env \
        /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
        -D TDESKTOP_API_ID=YOUR_API_ID \
        -D TDESKTOP_API_HASH=YOUR_API_HASH

If you need a backward compatible binary (running on older OS like the official one), you should build the binary with LTO.  
To do this, add `-D CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON` option.

The built files will be in the `out` directory.

[api_credentials]: api_credentials.md
