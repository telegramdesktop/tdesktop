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

Replace '\-llzma' '\/usr\/lib\/i386\-linux\-gnu\/liblzma\.a'
Replace '\-lz' '\/usr\/lib\/i386\-linux\-gnu\/libz\.a'
Replace '\-lssl' '\/usr\/lib\/i386\-linux\-gnu\/libssl\.a'
Replace '\-lcrypto' '\/usr\/lib\/i386\-linux\-gnu\/libcrypto\.a'
Replace '\-lexif' '\/usr\/lib\/i386\-linux\-gnu\/libexif\.a'
Replace '\-lopusfile' '\/usr\/local\/lib\/libopusfile\.a'
Replace '\-lopus' '\/usr\/local\/lib\/libopus\.a'
Replace '\-lopenal' '\/usr\/local\/lib\/libopenal\.a'
Replace '\-logg' '\/usr\/local\/lib\/libogg\.a'
