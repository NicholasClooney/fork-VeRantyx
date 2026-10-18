# vxrt: the VeRantyx Windows runtime. An x86-64 macOS binary; Rosetta 2 runs it and the Windows program together.
CC      = clang
CFLAGS  = -arch x86_64 -O1 -g -Wall -Wextra -Wno-unused-parameter -mmacosx-version-min=14.0
SRC     = src/loader.c src/kernel32.c src/system.c src/desktop.c src/services.c src/client.c src/iphlp.c src/steambridge.c src/calltrace.c src/netdiag.c src/renderdiag.c src/pipes.c src/ucrt.c src/sync.c src/seh.c src/profile.c src/opengl.c src/wininet.m src/extras.c src/dinput.c src/context.S src/text.c src/files.c src/registry.c \
          src/user32.c src/gdi.c src/control.c src/net.c src/mm.c src/vcruntime.c src/com.c src/cocoa.m src/d3d11.m src/d3d11_draw.m src/d3d11_shared.m src/dxgi.m src/d3d9.m src/dxbc.c src/d3d12.m src/dxil.c src/dxil_msl.c src/input.m src/audio.m src/xaudio2.m
MINGW   = x86_64-w64-mingw32-gcc
MINGWXX = x86_64-w64-mingw32-g++

all: build/vxrt tests

build/vxrt: src/renderdiag.h $(SRC) src/iphlp.h src/vx.h src/ui.h src/d3d_common.h src/d3d_tables.h src/d3d12_tables.h src/dxil.h src/dxil_msl.h src/audio_tables.h src/xaudio2_tables.h src/dxbc.h
	@mkdir -p build
	$(CC) $(CFLAGS) -fobjc-arc -o $@ $(SRC) -framework SystemConfiguration -framework Cocoa -framework CoreText -framework Metal -framework QuartzCore -framework GameController -framework CoreGraphics -framework IOSurface -framework AudioToolbox -framework CoreAudio -framework ImageIO -framework OpenGL -Wl,-pagezero_size,0x10000

# The ARM64 build (phase 4, TRANSLATOR.md): the same runtime as ARM64 code, the program run by the translator's
# engine in the process. Thunks for every ms_abi function come from the sources' C signatures (tools/vx_thunks.py).
ARM64_SRC = $(filter-out src/context.S,$(SRC)) src/arm64.c src/context_arm64.S translator/xlat.c translator/x87.c translator/x86dec.c translator/engine.c translator/vcpu.c
build/thunks/stamp: $(filter %.c %.m,$(SRC)) src/vx.h tools/vx_thunks.py tools/vx_abi_scan.py
	@mkdir -p build/thunks
	python3 tools/vx_thunks.py build/thunks $(filter %.c %.m,$(SRC))
	@touch $@
# Code that runs inside the vCPU guest (translator/guest.c): no calls, no globals, no stack protector.
GUEST_CFLAGS = -arch arm64 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-stack-check -mmacosx-version-min=14.0 -Itranslator
build/guest.o: translator/guest.c translator/vcpu.h translator/xlat.h translator/xvheap.h
	@mkdir -p build
	$(CC) $(GUEST_CFLAGS) -c $< -o $@
build/libvxrt_vm.dylib: src/vm_hooks.c translator/xvheap.h
	@mkdir -p build
	$(CC) -arch arm64 -O1 -mmacosx-version-min=14.0 -dynamiclib -install_name @executable_path/libvxrt_vm.dylib -Itranslator -o $@ $<
build/vxrt-arm64: src/renderdiag.h src/iphlp.h $(ARM64_SRC) build/thunks/stamp build/libvxrt_vm.dylib build/guest.o src/arm64.h src/vx.h src/ui.h src/d3d_common.h src/d3d_tables.h src/d3d12_tables.h src/dxil.h src/dxil_msl.h src/audio_tables.h src/xaudio2_tables.h src/dxbc.h translator/xlat.h translator/xlat_sse.inc translator/a64.h translator/engine.h translator/x87.h translator/cpuid_table.h
	$(CC) -arch arm64 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -mmacosx-version-min=14.0 -Isrc -Ibuild -fobjc-arc -o $@ $(ARM64_SRC) -framework SystemConfiguration -framework Cocoa -framework CoreText -framework Metal -framework QuartzCore -framework GameController -framework CoreGraphics -framework IOSurface -framework AudioToolbox -framework CoreAudio -framework ImageIO -framework OpenGL -framework Hypervisor build/libvxrt_vm.dylib build/guest.o
	codesign -s - -f --entitlements translator/hypervisor.entitlements $@

tests: build/search_path.exe build/open_timer.exe build/volume_mount.exe build/dinput_create.exe build/netdiag_api.exe build/hello_k32.exe build/hello_crt.exe build/threads.exe build/cxx_exceptions.exe build/hw_exceptions.exe build/breakpoint.exe build/minidump.exe build/nt_thread_info.exe \
       build/files.exe build/dll_test.exe build/vxplugin.dll build/dllprobe.exe build/winhttp_proxy.exe build/c_seh.exe build/best_route.exe \
       build/window.exe build/net.exe build/msvc_eh.exe build/com.exe build/vxcom.dll build/iocp.exe build/d3d11.exe build/gl.exe build/hlslc.exe build/dxbc2msl build/draw.exe build/compute.exe build/d3d12.exe build/input.exe build/audio.exe build/xaudio2.exe build/dxbcscan build/pipes.exe build/ipc.exe build/shared.exe build/steam.exe build/smc.exe build/lowmem.exe build/clip.exe build/shoplist.exe

build/netdiag_api.exe: tests/netdiag_api.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -lws2_32 -liphlpapi -lwinhttp

build/netdiag_host: tests/netdiag_host.c src/netdiag.c src/vx.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ tests/netdiag_host.c src/netdiag.c

build/netdiag_messagebox: tests/netdiag_messagebox.c src/user32.c src/netdiag.c src/vx.h src/ui.h
	@mkdir -p build
	$(CC) $(CFLAGS) -ffunction-sections -Wl,-dead_strip -Isrc -o $@ tests/netdiag_messagebox.c src/netdiag.c

netdiag-check: build/netdiag_host build/netdiag_messagebox build/netdiag_api.exe $(VXRT)
	env -u VXRT_NET_DIAG build/netdiag_host
	VXRT_NET_DIAG=0 build/netdiag_host
	VXRT_NET_DIAG=01 build/netdiag_host
	VXRT_NET_DIAG=true build/netdiag_host
	VXRT_NET_DIAG=1 build/netdiag_host
	VXRT_NET_DIAG=1 build/netdiag_messagebox
	sh tests/netdiag_check.sh $(VXRT)

build/hello_k32.exe: tests/hello_k32.c
	@mkdir -p build
	$(MINGW) -O1 -nostdlib -e start -o $@ $< -lkernel32

build/hello_crt.exe: tests/hello_crt.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

# -static: winpthread and libgcc are linked into the program, so it imports only the system DLLs.
build/threads.exe: tests/threads.c
	@mkdir -p build
	$(MINGW) -O1 -static -o $@ $<

build/lowmem.exe: tests/lowmem.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/hw_exceptions.exe: tests/hw_exceptions.c
	@mkdir -p build
	$(MINGW) -O1 -static -o $@ $<

build/breakpoint.exe: tests/breakpoint.c
	@mkdir -p build
	$(MINGW) -O1 -static -o $@ $<

build/minidump.exe: tests/minidump.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/nt_thread_info.exe: tests/nt_thread_info.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/winhttp_proxy.exe: tests/winhttp_proxy.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/best_route.exe: tests/best_route.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/cxx_exceptions.exe: tests/cxx_exceptions.cpp
	@mkdir -p build
	$(MINGWXX) -O1 -static -o $@ $<

build/callbench.exe: tests/callbench.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/smc.exe: tests/smc.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/window.exe: tests/window.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -lgdi32 -lwinmm

build/clip.exe: tests/clip.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -lgdi32

build/shoplist.exe: tests/shoplist.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -lgdi32

build/net.exe: tests/net.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -lws2_32

# MSVC-ABI test programs: compiled by clang for x86_64-pc-windows-msvc, linked by MinGW's linker against import
# libraries of this runtime's DLLs, without any C runtime.
MSVCCC = clang -target x86_64-pc-windows-msvc -O1 -fexceptions -fcxx-exceptions
build/libvcruntime140.a: tests/msvc/vcruntime140.def
	@mkdir -p build
	x86_64-w64-mingw32-dlltool -d $< -l $@ -m i386:x86-64

build/msvc_eh.exe: tests/msvc_eh.cpp tests/msvc/start.c build/libvcruntime140.a
	$(MSVCCC) -c tests/msvc_eh.cpp -o build/msvc_eh.obj
	$(MSVCCC) -c tests/msvc/start.c -o build/msvc_start.obj
	$(MINGW) -nostdlib -e mainCRTStartup -o $@ build/msvc_eh.obj build/msvc_start.obj -Lbuild -lvcruntime140 -lkernel32 -lucrtbase

build/c_seh.exe: tests/c_seh.c tests/msvc/start.c
	@mkdir -p build
	$(MSVCCC) -c tests/c_seh.c -o build/c_seh.obj
	$(MSVCCC) -c tests/msvc/start.c -o build/c_seh_start.obj
	$(MINGW) -nostdlib -e mainCRTStartup -o $@ build/c_seh.obj build/c_seh_start.obj -lucrtbase -lkernel32

build/com.exe: tests/com.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -lole32 -loleaut32 -luuid

build/vxcom.dll: tests/vxcom.c
	@mkdir -p build
	$(MINGW) -O1 -shared -o $@ $< -luuid

build/iocp.exe: tests/iocp.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/gl.exe: tests/gl.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -lopengl32 -lgdi32

build/d3d11.exe: tests/d3d11.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -ld3d11 -ldxgi -ldxguid -luuid

# The Steam bridge against the Steam client running on the Mac (VXRT_STEAM_BRIDGE=1 build/vxrt build/steam.exe;
# not part of make check, which must not depend on Steam).
build/steam.exe: tests/steam.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/drawbench.exe: tests/drawbench.c tests/shaders/*.h
	@mkdir -p build
	$(MINGW) -O2 -Itests -o $@ $<  -ld3d11 -ldxgi -ldxguid -luuid

# The translator (TRANSLATOR.md) is native ARM64 code: its tools build for this Mac, not for Rosetta.
HOSTCC = clang -O2 -Wall -Wextra
build/x86dec_test: translator/x86dec.c translator/x86dec.h translator/x86dec_test.c
	@mkdir -p build
	$(HOSTCC) translator/x86dec.c translator/x86dec_test.c -o $@
# llvm-objdump -d --no-show-raw-insn <pe> | build/x86dec_check <pe>: the decoder against LLVM on a real binary.
build/x86dec_check: translator/x86dec.c translator/x86dec.h translator/x86dec_check.c
	@mkdir -p build
	$(HOSTCC) translator/x86dec.c translator/x86dec_check.c -o $@

# The differential test (TRANSLATOR.md): ref_x86 runs instructions for real under Rosetta 2, difftest translates them
# and compares. `make translator-check` runs 3000 cases per form; make check a quick 200.
build/ref_x86: translator/diff/ref_x86.c translator/diff/difftest.h
	@mkdir -p build
	$(CC) -arch x86_64 -O1 -Wall -o $@ translator/diff/ref_x86.c
build/difftest: build/guest.o translator/vcpu.c translator/vcpu.h translator/diff/difftest.c translator/diff/difftest.h translator/xlat.c translator/xlat_sse.inc translator/xlat.h translator/a64.h translator/x86dec.c translator/x86dec.h translator/x87.c translator/x87.h translator/engine.c translator/engine.h translator/cpuid_table.h
	@mkdir -p build
	$(HOSTCC) -Wno-missing-field-initializers translator/x86dec.c translator/xlat.c translator/x87.c translator/engine.c translator/vcpu.c translator/diff/difftest.c build/guest.o -framework Hypervisor -o $@
	codesign -s - -f --entitlements translator/hypervisor.entitlements $@
# translator coverage over the installed games' PEs (every function in .pdata): `make coverage`
build/coverage: translator/cpuid_table.h translator/coverage.c translator/xlat.c translator/xlat_sse.inc translator/xlat.h translator/a64.h translator/x86dec.c translator/x86dec.h translator/x87.c translator/x87.h
	@mkdir -p build
	$(HOSTCC) -Wno-missing-field-initializers -Itranslator translator/coverage.c translator/x86dec.c translator/xlat.c translator/x87.c -o $@
coverage: build/coverage
	python3 translator/coverage.py build/coverage $(HOME)/Verantyx_VR_Drive/vxrt_steam/steamapps/common
# the engine against Rosetta 2 and native ARM64 on the same kernels: `make xlat-bench`
BENCH_X86 = -arch x86_64 -O2 -fno-stack-protector -ffreestanding -fno-builtin -fno-vectorize
build/bench_kernels_x86.o: translator/bench/kernels.c
	@mkdir -p build
	$(CC) $(BENCH_X86) -c $< -o $@
build/bench_image: build/bench_kernels_x86.o
	ld -arch x86_64 -preload -e _start -segaddr __TEXT 0x500000000 $< -o $@
build/bench_rosetta: build/bench_kernels_x86.o translator/bench/rosetta_main.c
	$(CC) -arch x86_64 -O2 translator/bench/rosetta_main.c build/bench_kernels_x86.o -o $@
build/bench_native: translator/bench/kernels.c translator/bench/rosetta_main.c
	$(CC) -arch arm64 -O2 -fno-stack-protector -fno-builtin -fno-vectorize translator/bench/rosetta_main.c translator/bench/kernels.c -o $@
build/bench: build/guest.o translator/bench/bench.c translator/vcpu.c translator/vcpu.h translator/engine.c translator/engine.h translator/xlat.c translator/xlat_sse.inc translator/xlat.h translator/a64.h translator/x86dec.c translator/x87.c translator/cpuid_table.h
	$(HOSTCC) -Wno-missing-field-initializers -Itranslator translator/bench/bench.c translator/engine.c translator/vcpu.c translator/xlat.c translator/x87.c translator/x86dec.c build/guest.o -framework Hypervisor -o $@
	codesign -s - -f --entitlements translator/hypervisor.entitlements $@
xlat-bench: build/bench build/bench_image build/bench_rosetta build/bench_native
	@echo "== Rosetta 2"; build/bench_rosetta 5
	@echo "== VXRuntime engine"; build/bench build/bench_image 5 0
	@echo "== VXRuntime engine, ordered accesses (in-process, no TSO)"; build/bench build/bench_image 5 1
	@echo "== native ARM64"; build/bench_native 5
# x87 transcendentals against exact values (slow: decimal arithmetic): `make x87-exact`
build/x87_ref: translator/diff/x87_ref.c
	@mkdir -p build
	$(CC) -arch x86_64 -O1 -Wall -o $@ translator/diff/x87_ref.c
build/x87_ours: translator/diff/x87_ours.c translator/x87.c translator/x87.h translator/xlat.h
	@mkdir -p build
	$(HOSTCC) -O1 -Wall -Itranslator -o $@ translator/diff/x87_ours.c translator/x87.c
x87-exact: build/x87_ref build/x87_ours
	python3 translator/diff/x87_exact.py build/x87_ref build/x87_ours 500
translator-check: build/ref_x86 build/difftest build/x86dec_test
	build/x86dec_test
	build/difftest build/ref_x86 3000
	DT_ORDERED=1 build/difftest build/ref_x86 1000    # (the ARM64 runtime's default: x86's order with LDAPR / STLR)

build/hvprobe: translator/hvprobe.c translator/hypervisor.entitlements
	@mkdir -p build
	$(HOSTCC) translator/hvprobe.c -framework Hypervisor -o $@
	codesign -s - -f --entitlements translator/hypervisor.entitlements $@

build/dxbcscan: tools/dxbcscan.m src/dxbc.c src/dxbc.h
	@mkdir -p build
	$(CC) $(CFLAGS) -fobjc-arc -o $@ tools/dxbcscan.m src/dxbc.c -framework Metal -framework Foundation

build/dxbc2msl: tools/dxbc2msl.m src/dxbc.c src/dxbc.h
	@mkdir -p build
	$(CC) $(CFLAGS) -fobjc-arc -o $@ tools/dxbc2msl.m src/dxbc.c -framework Metal

# DXIL (Shader Model 6, Direct3D 12): src/dxil.c reads the container and its LLVM bitcode.
build/dxildump: tools/dxildump.c src/dxil.c src/dxil.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tools/dxildump.c src/dxil.c

# DXIL -> Metal Shading Language (src/dxil_msl.c): translated and compiled (dxil2msl), compute shaders run and checked (dxilrun).
build/dxil2msl: tools/dxil2msl.m src/dxil_msl.c src/dxil_msl.h src/dxil.c src/dxil.h
	@mkdir -p build
	$(CC) -O1 -g -Wall -Wextra -Wno-unused-parameter -mmacosx-version-min=14.0 -fobjc-arc -o $@ tools/dxil2msl.m src/dxil_msl.c src/dxil.c -framework Metal -framework Foundation

build/dxilrun: tools/dxilrun.m src/dxil_msl.c src/dxil_msl.h src/dxil.c src/dxil.h
	@mkdir -p build
	$(CC) -O1 -g -Wall -Wextra -Wno-unused-parameter -mmacosx-version-min=14.0 -fobjc-arc -o $@ tools/dxilrun.m src/dxil_msl.c src/dxil.c -framework Metal -framework Foundation

build/dxc.exe: tools/dxc.cpp
	@mkdir -p build
	$(MINGWXX) -O1 -static -o $@ $<

build/draw.exe: tests/draw.c tests/shaders/*.h
	@mkdir -p build
	$(MINGW) -O1 -Itests -o $@ $< -ld3d11 -ldxgi -ldxguid -luuid -lm

build/compute.exe: tests/compute.c tests/shaders/*.h
	@mkdir -p build
	$(MINGW) -O1 -Itests -o $@ $< -ld3d11 -ldxguid -luuid -lm

build/d3d12.exe: tests/d3d12.c tests/shaders12/sum_cs.dxil tests/shaders12/tri_vs.dxil tests/shaders12/tri_ps.dxil
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -ld3d12 -ldxgi -lm

build/input.exe: tests/input.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -lxinput

build/audio.exe: tests/audio.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -lole32 -luuid -lm

build/xaudio2.exe: tests/xaudio2.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -lxaudio2_9 -lm

build/shared.exe: tests/shared.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $< -ld3d11 -ldxgi -ldxguid -luuid

build/ipc.exe: tests/ipc.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/pipes.exe: tests/pipes.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/hlslc.exe: tools/hlslc.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/dllprobe.exe: tools/dllprobe.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/files.exe: tests/files.c
	@mkdir -p build
	$(MINGW) -O1 -o $@ $<

build/vxdll.dll: tests/vxdll.cpp
	@mkdir -p build
	$(MINGWXX) -O1 -shared -static -o $@ $< -Wl,--out-implib,build/libvxdll.a

build/dll_test.exe: tests/dll_test.cpp build/vxdll.dll
	@mkdir -p build
	$(MINGWXX) -O1 -static -o $@ $< build/libvxdll.a

build/vxplugin.dll: tests/vxplugin.c tests/vxplugin.def
	@mkdir -p build
	$(MINGW) -O1 -shared -o $@ tests/vxplugin.c tests/vxplugin.def

# Test shaders: HLSL compiled to DXBC by a d3dcompiler DLL (Microsoft's, as games ship it) running on vxrt.
# The generated tests/shaders/*.h are kept in the repository; regenerate with
#   make shaders D3DCOMPILER='Z:\path\to\d3dcompiler_43.dll'
SHADERS = color_vs:vs_4_0 color_ps:ps_4_0 tex_vs:vs_4_0 tex_ps:ps_4_0 fullscreen_vs:vs_4_0 math_ps:ps_4_0 sum_cs:cs_5_0 atomic_cs:cs_5_0 append_cs:cs_5_0 views_cs:cs_5_0 formats_cs:cs_5_0 features_ps:ps_5_0 features_vs:vs_5_0 features_cs:cs_5_0 quad_gs:gs_4_0 bounds_cs:cs_5_0 cbounds_cs:cs_5_0
shaders: build/vxrt build/hlslc.exe
	@for s in $(SHADERS); do n=$${s%%:*}; p=$${s##*:}; \
	  VXRT_ROOT=build/root build/vxrt build/hlslc.exe '$(D3DCOMPILER)' tests/shaders/$$n.hlsl main $$p tests/shaders/$$n.h $$n || exit 1; done

# Shader Model 6 test shaders: HLSL compiled to DXIL by Microsoft's dxcompiler.dll running on vxrt (Chromium ships one
# beside SteamVR's vrwebhelper), with dxcompiler's own disassembly of each as the reference tools/dxil_check.py reads
# src/dxil.c's reading against. tests/shaders12/*.dxil and *.dis.txt are kept in the repository; regenerate with
#   make shaders12 DXCOMPILER='Z:\path\to\dxcompiler.dll'
SHADERS12 = color_ps:ps_6_0 tex_ps:ps_6_0 sum_cs:cs_6_0 color_vs:vs_6_0 bindless_ps:ps_6_6 tri_vs:vs_6_0 tri_ps:ps_6_0 texq_vs:vs_6_0 texq_ps:ps_6_0
shaders12: build/vxrt build/dxc.exe
	@for s in $(SHADERS12); do n=$${s%%:*}; p=$${s##*:}; \
	  VXRT_ROOT=build/root build/vxrt build/dxc.exe '$(DXCOMPILER)' tests/shaders12/$$n.hlsl main $$p tests/shaders12/$$n.h $$n || exit 1; \
	  VXRT_ROOT=build/root build/vxrt build/dxc.exe -dis '$(DXCOMPILER)' tests/shaders12/$$n.dxil tests/shaders12/$$n.dis.txt || exit 1; done

# Games in VR against their profiles' baselines, games/*.json (tools/vxrt_run.py: own roots, simulated headset, evidence in runs/):
# minutes per scenario; Mac Steam must be signed in and nothing else running. GAME=alyx|thelab|earth: only that one.
vr-check: build/vxrt
	python3 ../tools/vxrt_run.py --baselines $(GAME)

# The tests' C: drive and registry live in build/root, not in ~/.vxrt.
check: export VXRT_ROOT = build/root
check: export VXRT_NO_ACTIVATE = 1
check: all build/x86dec_test build/x86dec_check build/ref_x86 build/difftest build/dxildump build/dxil2msl build/dxilrun
	python3 ../tools/vxrt_games.py check
	python3 ../tools/vxrt_agents.py check
	python3 ../tools/vxrt_tune.py --selftest
	python3 ../tools/vxrt_playtest.py --selftest
	build/x86dec_test
	python3 translator/a64_check.py
	python3 tools/dxil_check.py build/dxildump tests/shaders12
	build/dxil2msl tests/shaders12/*.dxil
	build/dxilrun tests/shaders12
	build/difftest build/ref_x86 200
	python3 ../tools/test_vxrt_network_diagnostic.py
	python3 ../tools/test_vxrt_stream_metrics.py
	$(MAKE) runtime-tests VXRT=build/vxrt

# The runtime's own tests, run by $(VXRT): the x86 build under Rosetta (make check) or the ARM64 build with the
# translator (make check-arm64).
VXRT ?= build/vxrt
runtime-tests: export VXRT_ROOT = build/root
runtime-tests: export VXRT_NO_ACTIVATE = 1
runtime-tests: $(VXRT) tests
	$(MAKE) netdiag-check VXRT=$(VXRT)
	$(MAKE) iphlp-check VXRT=$(VXRT)
	$(MAKE) dinput-check VXRT=$(VXRT)
	$(MAKE) volume-mount-check VXRT=$(VXRT)
	$(MAKE) open-timer-check VXRT=$(VXRT)
	$(MAKE) search-path-check VXRT=$(VXRT)
	$(VXRT) build/hello_k32.exe
	$(VXRT) build/smc.exe
	$(VXRT) build/hello_crt.exe one "two words"
	$(VXRT) build/threads.exe
	$(VXRT) build/cxx_exceptions.exe
	$(VXRT) build/hw_exceptions.exe
	$(VXRT) build/breakpoint.exe
	$(VXRT) build/minidump.exe
	$(VXRT) build/nt_thread_info.exe
	$(VXRT) build/winhttp_proxy.exe
	$(VXRT) build/best_route.exe
	$(VXRT) build/lowmem.exe
	$(VXRT) build/files.exe
	python3 tests/overlay_rename.py $(VXRT) build/files.exe
	rm -rf build/ovtree build/ovshadow && mkdir -p build/ovtree/save && echo old > build/ovtree/save/old.sav && echo orig > build/ovtree/save/same.hl1
	VXRT_OVERLAY=$(CURDIR)/build/ovtree:$(CURDIR)/build/ovshadow $(VXRT) build/files.exe overlay 'Z:$(subst /,\,$(CURDIR))\build\ovtree'
	test "$$(cat build/ovtree/save/same.hl1)" = orig && test ! -e build/ovtree/save/a.hl1
	$(VXRT) build/dll_test.exe
	$(VXRT) build/net.exe
	$(VXRT) build/msvc_eh.exe
	$(VXRT) build/c_seh.exe
	$(VXRT) build/com.exe
	$(VXRT) build/iocp.exe
	$(VXRT) build/window.exe
	python3 tests/clip_check.py $(VXRT)
	python3 tests/shoplist_check.py $(VXRT)
	$(VXRT) build/d3d11.exe
	$(VXRT) build/gl.exe
	build/dxbcscan tests/shaders/*.dxbc
	$(VXRT) build/draw.exe
	$(VXRT) build/compute.exe
	$(VXRT) build/d3d12.exe
	$(VXRT) build/input.exe
	$(VXRT) build/audio.exe
	$(VXRT) build/xaudio2.exe
	$(VXRT) build/pipes.exe
	$(VXRT) build/pipes.exe serve & $(VXRT) build/pipes.exe ask; wait
	$(VXRT) build/ipc.exe second & $(VXRT) build/ipc.exe first; wait
	$(VXRT) build/shared.exe make & $(VXRT) build/shared.exe open; wait
	$(VXRT) build/shared.exe pingpong 1 & $(VXRT) build/shared.exe pingpong 2; wait

check-arm64: build/vxrt-arm64 tests
	python3 ../tools/test_vxrt_network_diagnostic.py
	$(MAKE) runtime-tests VXRT=build/vxrt-arm64

# CPU cost per Direct3D 11 draw (numbers only, not a test).
bench: export VXRT_ROOT = build/root
bench: export VXRT_NO_ACTIVATE = 1
bench: build/vxrt build/drawbench.exe
	build/vxrt build/drawbench.exe

clean:
	rm -rf build

.PHONY: vr-check all tests check bench clean shaders translator-check

# Adapter fixtures use synthetic addresses; host/PE checks print counts only.
build/iphlp_host: tests/iphlp_host.c src/iphlp.c src/iphlp.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/iphlp_host.c -framework SystemConfiguration -framework CoreFoundation

build/iphlp_api.exe: tests/iphlp_api.c
	$(MINGW) -O1 -o $@ $< -liphlpapi -lws2_32

iphlp-check: build/iphlp_host build/iphlp_api.exe $(VXRT)
	build/iphlp_host
	$(VXRT) build/iphlp_api.exe

build/dinput_create.exe: tests/dinput_create.c
	$(MINGW) -O1 -o $@ $< -ldinput8 -ldxguid

dinput-check: build/dinput_create.exe $(VXRT)
	$(VXRT) build/dinput_create.exe

.PHONY: volume-mount-check
build/volume_mount.exe: tests/volume_mount.c
	$(MINGW) -O1 -o $@ $<

volume-mount-check: build/volume_mount.exe $(VXRT)
	$(VXRT) build/volume_mount.exe

.PHONY: open-timer-check
build/open_timer.exe: tests/open_timer.c
	$(MINGW) -O1 -o $@ $<

open-timer-check: build/open_timer.exe $(VXRT)
	$(VXRT) build/open_timer.exe

.PHONY: search-path-check
build/search_path.exe: tests/search_path.c
	$(MINGW) -O1 -o $@ $<

search-path-check: build/search_path.exe $(VXRT)
	mkdir -p build/searchpath-root/drive_c/tree/中文 build/searchpath-root/drive_c/second build/searchpath-root/drive_c/denied build/searchpath-root/drive_c/tree/blocked build/searchpath-shadow/blocked
	touch build/searchpath-root/drive_c/tree/中文/数据.bin build/searchpath-root/drive_c/tree/choice.txt build/searchpath-root/drive_c/second/choice.txt build/searchpath-shadow/shadow.txt build/searchpath-root/drive_c/tree/blocked/choice.txt
	chmod 000 build/searchpath-root/drive_c/denied build/searchpath-shadow/blocked
	@trap 'chmod 700 build/searchpath-root/drive_c/denied build/searchpath-shadow/blocked' EXIT; VXRT_ROOT="$(CURDIR)/build/searchpath-root" VXRT_OVERLAY="$(CURDIR)/build/searchpath-root/drive_c/tree:$(CURDIR)/build/searchpath-shadow" VXRT_NO_WINE=1 VXRT_NO_ACTIVATE=1 $(VXRT) build/search_path.exe

.PHONY: window-metadata-check
build/window_metadata.exe: tests/window_metadata.c
	$(MINGW) -O1 -o $@ $< -luser32

window-metadata-check: build/window_metadata.exe $(VXRT)
	python3 tests/window_metadata_check.py

.PHONY: render-diagnostic-check
build/renderdiag_host: tests/renderdiag_host.c src/renderdiag.c src/renderdiag.h
	$(CC) $(CFLAGS) -o $@ $<

build/render_diagnostic.exe: tests/render_diagnostic.c
	$(MINGW) -O1 -o $@ $< -ld3d11 -ldxgi -ldxguid -luser32

build/activity_diagnostic.exe: tests/activity_diagnostic.c
	$(MINGW) -O1 -o $@ $< -ld3d11 -luser32

render-diagnostic-check: build/renderdiag_host build/render_diagnostic.exe build/activity_diagnostic.exe $(VXRT)
	build/renderdiag_host
	python3 tests/render_diagnostic_check.py
