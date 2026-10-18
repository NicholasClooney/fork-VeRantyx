#!/bin/sh
set -eu
runtime=${1:-build/vxrt}
log=$(mktemp)
trap 'rm -f "$log"' EXIT HUP INT TERM
for mode in unset 0 01 1; do
    if [ "$mode" = unset ]; then
        env -u VXRT_NET_DIAG VXRT_ROOT=build/root VXRT_NO_ACTIVATE=1 "$runtime" build/netdiag_api.exe >"$log" 2>&1
    else
        VXRT_NET_DIAG=$mode VXRT_ROOT=build/root VXRT_NO_ACTIVATE=1 "$runtime" build/netdiag_api.exe >"$log" 2>&1
    fi
    if [ "$mode" = 1 ]; then
        # Exactly one entry per exported call, correct PE caller and no payload/absolute address.
        awk '/netdiag / { n++; if ($0 !~ /caller=netdiag_api.exe\+0x[0-9a-f]+$/ || $0 ~ /SECRET|https:|token=/) bad=1 }
             END { exit (n != 20 || bad) }' "$log" || { cat "$log"; exit 1; }
        for api in GetBestRoute GetBestInterface GetAdaptersAddresses GetAdaptersInfo GetIfTable GetIpAddrTable GetNetworkParams ConvertInterfaceGuidToLuid GetIfEntry2 WinHttpGetDefaultProxyConfiguration WinHttpGetIEProxyConfigForCurrentUser WSAStartup socket WSASocketA WSASocketW bind connect setsockopt WSAIoctl getaddrinfo; do
            [ "$(grep -c "api=$api " "$log")" -eq 1 ]
        done
    else
        if grep -q 'netdiag ' "$log"; then cat "$log"; exit 1; fi
    fi
done
printf 'netdiag exports: PASS\n'
