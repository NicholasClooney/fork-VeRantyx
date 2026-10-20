// Synthetic SM5 compute DXBC: exercise the actual translator, Metal compiler and GPU readback.
// Build from VXRuntime: clang -arch x86_64 -fobjc-arc -Isrc tests/half_conversion.m src/dxbc.c -framework Metal -framework Foundation -o build/half_conversion
// Token layout: microsoft/DirectX-Headers, include/directx/D3D12TokenizedProgramFormat.hpp.
// Conversion semantics: learn.microsoft.com/windows/win32/direct3dhlsl/f32tof16--sm5---asm-
// and f16tof32--sm5---asm-. No compiler-generated or game bytecode is used.
#import <Metal/Metal.h>
#include "dxbc.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Exact representable values keep the oracle independent of host half conversion and rounding modes.
// NaNs are checked by classification, never by a particular payload or sign.
static const struct { uint32_t f; uint16_t h; } values[] = {
    {0x00000000,0x0000}, {0x80000000,0x8000}, {0x3f800000,0x3c00}, {0xc0200000,0xc100},
    {0x38800000,0x0400}, {0xb8800000,0x8400}, {0x477fe000,0x7bff}, {0xc77fe000,0xfbff},
    {0x33800000,0x0001}, {0xb3800000,0x8001}, {0x387fc000,0x03ff}, {0xb87fc000,0x83ff},
    {0x7f800000,0x7c00}, {0xff800000,0xfc00}, {0x7fc12000,0x7e09}, {0xffc02000,0xfe01},
    {0x3eaaa000,0x3555}, {0xbeaaa000,0xb555}, {0x40000000,0x4000}, {0xc0000000,0xc000},
    {0x3f802000,0x3c01}, {0xbf802000,0xbc01}, {0x34000000,0x0002}, {0xb4000000,0x8002},
};
static const unsigned masks[] = {1,3,7,15,5,10,13,8,2,4,6,9,11,12,14};
enum { MODES = 5, CASES = 15 * MODES * 2, WORDS = CASES * 4 };
static const uint32_t untouched = 0x4f123456;
static uint32_t blob[16384];
static size_t count;
static void word(uint32_t v) { blob[count++] = v; }
static void op(unsigned opcode, unsigned length) { word(opcode | length << 24); }
static void dest(unsigned type, unsigned reg, unsigned mask) { word(2 | mask << 4 | type << 12 | 1 << 20); word(reg); }
static void source(unsigned reg, unsigned swizzle) { word(2 | 1 << 2 | swizzle << 4 | 1 << 20); word(reg); }
static void immediate(uint32_t bits) { word(1 | 4 << 12); word(bits); }
static size_t shader(unsigned opcode) {
    memset(blob,0,sizeof blob);
    memcpy(blob,"DXBC",4); blob[5]=1; blob[7]=1; blob[8]=36;
    memcpy(blob+9,"SHEX",4); blob[11]=(5u<<16)|0x50; count=13;
    op(104,2); word(2); // dcl_temps 2
    op(155,4); word(1); word(1); word(1); // dcl_thread_group 1,1,1
    op(89,4); word(2 | 15<<4 | 8<<12 | 2<<20); word(0); word(1); // cb0[1]
    op(157,3); dest(30,0,15); // dcl_uav_raw u0
    unsigned test=0;
    for(unsigned mode=0;mode<MODES;mode++) for(unsigned mi=0;mi<15;mi++) for(unsigned alias=0;alias<2;alias++,test++) {
        op(54,6); dest(0,0,15); // mov r0, cb0[0].xyzw
        word(2 | 1<<2 | 0xe4<<4 | 8<<12 | 2<<20); word(0); word(0);
        op(54,5); dest(0,1,15); immediate(untouched); // mov r1, sentinel
        op(opcode,5); dest(0,alias ? 0 : 1,masks[mi]);
        if(mode==4) immediate(opcode==130 ? 0xc0200000 : 0xa5a5c100);
        else if(mode==3) { word(2 | 2<<2 | 1<<4 | 1<<20); word(0); } // select-one y
        else source(0,mode==0 ? 0xe4 : mode==1 ? 0x1b : 0xaa);
        op(166,7); dest(30,0,15); immediate(test*16); source(alias ? 0 : 1,0xe4);
    }
    op(62,1);
    blob[12]=(uint32_t)count-11; blob[10]=blob[12]*4; blob[6]=(uint32_t)count*4;
    return count*4;
}
static int is_nan(uint32_t bits, int half) {
    uint32_t exponent=half ? 0x7c00 : 0x7f800000, mantissa=half ? 0x03ff : 0x007fffff;
    return (bits & exponent)==exponent && (bits & mantissa)!=0;
}
static int run(id<MTLDevice> dev,id<MTLCommandQueue> queue,unsigned opcode) {
    const char *name=opcode==130 ? "f32tof16" : "f16tof32";
    dxbc_shader sh;
    size_t size=shader(opcode);
    if(dxbc_translate(blob,size,&sh)) { fprintf(stderr,"%s translate: %s\n",name,sh.error); dxbc_free(&sh); return 1; }
    MTLCompileOptions *options=[MTLCompileOptions new];
    // Preserve signed zero, infinities and NaN classification in the oracle.
    if (@available(macOS 15.0, *)) options.mathMode=MTLMathModeSafe;
    else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        options.fastMathEnabled=NO;
#pragma clang diagnostic pop
    }
    NSError *error=nil;
    id<MTLLibrary> lib=[dev newLibraryWithSource:@(sh.msl) options:options error:&error];
    if(!lib) { fprintf(stderr,"%s Metal compile: %s\n",name,error.localizedDescription.UTF8String); dxbc_free(&sh); return 1; }
    id<MTLComputePipelineState> pso=[dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@(sh.entry)] error:&error];
    dxbc_free(&sh);
    if(!pso) { fprintf(stderr,"%s pipeline: %s\n",name,error.localizedDescription.UTF8String); return 1; }
    id<MTLBuffer> output=[dev newBufferWithLength:WORDS*4 options:MTLResourceStorageModeShared];
    if(!output) { fprintf(stderr,"output allocation failed\n"); return 1; }
    unsigned bad=0,checked=0;
    for(unsigned row=0;row<sizeof values/sizeof values[0]/4;row++) {
        uint32_t input[4];
        for(unsigned c=0;c<4;c++) input[c]=opcode==130 ? values[row*4+c].f : values[row*4+c].h | (c%2 ? 0xffff0000u : 0xa5a50000u);
        memset(output.contents,0xcd,WORDS*4);
        dxbc_aux aux={0}; aux.sizes[0]=WORDS*4;
        id<MTLCommandBuffer> cmd=[queue commandBuffer];
        id<MTLComputeCommandEncoder> enc=[cmd computeCommandEncoder];
        [enc setComputePipelineState:pso];
        [enc setBytes:input length:sizeof input atIndex:0];
        [enc setBuffer:output offset:0 atIndex:DXBC_UAV_BUFFER_BASE];
        [enc setBytes:&aux length:sizeof aux atIndex:DXBC_SIZES_BUFFER];
        [enc dispatchThreadgroups:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
        [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
        if(cmd.status!=MTLCommandBufferStatusCompleted) { fprintf(stderr,"%s dispatch: %s\n",name,cmd.error.localizedDescription.UTF8String); return 1; }
        const uint32_t *actual=output.contents;
        unsigned test=0;
        for(unsigned mode=0;mode<MODES;mode++) for(unsigned mi=0;mi<15;mi++) for(unsigned alias=0;alias<2;alias++,test++) for(unsigned c=0;c<4;c++) {
            uint32_t want=alias ? input[c] : untouched;
            int nan=0;
            if(masks[mi] & (1u<<c)) {
                unsigned s=mode==0 ? c : mode==1 ? 3-c : mode==2 ? 2 : 1;
                unsigned vi=mode==4 ? 3 : row*4+s;
                want=opcode==130 ? values[vi].h : values[vi].f;
                nan=is_nan(want,opcode==130);
            }
            uint32_t got=actual[test*4+c];
            int ok=nan ? is_nan(got,opcode==130) && (opcode!=130 || !(got & 0xffff0000u)) : got==want;
            if(!ok && bad++<12) fprintf(stderr,"%s row=%u mode=%u mask=%x alias=%u lane=%u got=%08x want=%08x%s\n",name,row,mode,masks[mi],alias,c,got,want,nan ? " (any NaN payload)" : "");
            checked++;
        }
    }
    printf("%s: Metal compiled, %u GPU words checked, %u failures\n",name,checked,bad);
    return bad ? 1 : 0;
}
int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev=MTLCreateSystemDefaultDevice();
        if(!dev) { fprintf(stderr,"half_conversion: Metal device unavailable (not a pass)\n"); return 1; }
        id<MTLCommandQueue> queue=[dev newCommandQueue];
        if(!queue) { fprintf(stderr,"half_conversion: Metal queue unavailable\n"); return 1; }
        int bad=run(dev,queue,130);
        bad+=run(dev,queue,131); // Report both constructor error classes on the old translator.
        printf("half_conversion: %s\n",bad ? "FAILED" : "ok");
        return bad ? 1 : 0;
    }
}
