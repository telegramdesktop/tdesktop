Replace () {
    CheckCommand="grep -ci '$1' Makefile"
    CheckCount=$(eval $CheckCommand)
    if [ "$CheckCount" -gt 0 ]; then
        echo "Requested '$1' to '$2', found - replacing.."
        ReplaceCommand="sed -i 's/$1/$2/g' Makefile"
        eval $ReplaceCommand
    else
        echo "Skipping '$1' to '$2'"
    fi
}

Replace '\-llzma' '\/usr\/lib\/x86_64\-linux\-gnu\/liblzma\.a'
Replace '\-lssl' '\/usr\/lib\/x86_64\-linux\-gnu\/libssl\.a'
Replace '\-lcrypto' '\/usr\/lib\/x86_64\-linux\-gnu\/libcrypto\.a'
Replace '\-lgobject\-2\.0' '\/usr\/lib\/x86_64\-linux\-gnu\/libgobject\-2\.0\.a \/usr\/lib\/x86_64\-linux\-gnu\/libffi\.a'
Replace '\-lXi' '\/usr\/lib\/x86_64\-linux\-gnu\/libXi\.a'
Replace '\-lSM' '\/usr\/lib\/x86_64\-linux\-gnu\/libSM\.a'
Replace '\-lICE' '\/usr\/lib\/x86_64\-linux\-gnu\/libICE\.a'
Replace '\-lfontconfig' '\/usr\/lib\/x86_64\-linux\-gnu\/libfontconfig\.a \/usr\/lib\/x86_64\-linux\-gnu\/libexpat\.a'
Replace '\-lfreetype' '\/usr\/lib\/x86_64\-linux\-gnu\/libfreetype\.a'
Replace '\-lXext' '\/usr\/lib\/x86_64\-linux\-gnu\/libXext\.a'
Replace '\-lopus' '\/usr\/local\/lib\/libopus\.a'
Replace '\-lopenal' '\/usr\/local\/lib\/libopenal\.a'
Replace '\-lavformat' '\/usr\/local\/lib\/libavformat\.a'
Replace '\-lavcodec' '\/usr\/local\/lib\/libavcodec\.a'
Replace '\-lswresample' '\/usr\/local\/lib\/libswresample\.a'
Replace '\-lswscale' '\/usr\/local\/lib\/libswscale\.a'
Replace '\-lavutil' '\/usr\/local\/lib\/libavutil\.a'
Replace '\-lva' '\/usr\/local\/lib\/libva\.a'
