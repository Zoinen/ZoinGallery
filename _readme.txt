macOS

1. If facing /bin/sh: cmake: command not found add to ~/.conan/profiles/default this:
[env]
PATH=[/usr/local/bin]
cmake must be symlinked to /usr/local/bin beforehands
