#!/bin/sh
# Fetch tc358762.c of the running kernel's version from kernel.org and apply the
# burst-mode patch. Usage: ./get-source.sh [kernel version, default: uname -r]
set -eu
V=${1:-$(uname -r)}
V=${V%%-*}                       # 6.18.62-foo -> 6.18.62
case $V in *.*.0) V=${V%.0} ;; esac  # tags are v6.18, not v6.18.0
URL="https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/plain/drivers/gpu/drm/bridge/tc358762.c?h=v$V"
cd "$(dirname "$0")"
curl -fsSL "$URL" -o tc358762.c
patch -p5 < tc358762-burst.patch
echo "tc358762.c (v$V) patched; build with: make"
