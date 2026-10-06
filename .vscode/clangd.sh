#!/bin/sh
# clangd's persistent module cache does not invalidate a module when one of its imports changes,
# which leaves stale .pcm files that crash clangd. Start every session with an empty cache.
rm -rf "$(dirname "$0")/../.cache/clangd/modules"
exec clangd "$@"
