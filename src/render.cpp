#include "common.h"
#include "render.h"
#include "game.h"
#include "config.h"
#include "log.h"
#include "patch.h"
#include "display.h"

// ---------------------------------------------------------------------------
// Rendering fixes
//  - Sun shadows: the PC build renders the sun's shadow (track, scenery) into
//    an 8 x 8 shadow map (0x57D6F0: an R32F render target and a D24X8 depth
//    surface, both 8 x 8; the caster pass's viewport 8 x 8 at 0x57C28F;
//    g_ShadowMapSize 8.0 at 0x54A70D). Spread over the 64 units in front of the
//    camera it's a blur: a dark patch round the car that comes and goes. The
//    two creates are replaced (shadowMapSize x shadowMapSize), the viewport
//    and g_ShadowMapSize follow. The shadow's reach (0x436xxx, each constant
//    read there only: 64 deep, fading over the last 16; width 128) is
//    shadowRange deep, fading over shadowFade, twice as wide as deep.
//    The shadow "camera" (0x64DBD0 -> 0x64CE70/0x64C570) is an orthographic
//    box fitted round the stretch of track ahead of the camera, looking down the
//    sun. But its depth range is thin: it starts only "fade width" (16) toward
//    the sun from the box centre and ends at "reach" (64) - so whatever is
//    higher up toward the sun (a roof, a wall top) fell outside it and cast no
//    shadow, and the edge of that moved with the camera. With shadowFit=1 the
//    light backs off shadowBackOff units toward the sun (0x64D084) and sees to
//    shadowFar (0x64DDF9); the fade width it hands the shaders (0x64DE17) stays
//    shadowFade. All three read our floats instead of the shared parameter block.
//  - See-through things (glass, the glass road pieces) flickered between clear
//    and washed out as the camera moved. Two causes:
//    - the alpha pass (0x54B7D0) sorts its draws (0x54B8F8-0x54BB17) by pass
//      only, not by distance, and unstably - so their order changed from frame
//      to frame. With glassSort=1 they're sorted back to front by their depth
//      (record +0x70), nearer last; equal ones by pass, then address.
//    - blended materials kept writing depth (0x5470A0 only turns it off for a
//      material flag; the blend switch 0x5420F0 only for MAX), so glass hid
//      whatever was drawn behind it later. With glassNoZWrite=1, after the
//      blend switch (called at 0x547204) sets a blended mode (0 add, 1 sub,
//      2 alpha, 3 mod2x, 6 add2) depth writing is turned off (0x541FE0) for
//      that draw; the next material sets it again as usual.
//    - meshes (the bell jar, the glass road) go through their own lists
//      (renderer+0xEC4128 and +0xED79A8), sorted in 0x54B7D0 only by technique
//      class (instance +0x1FC), unstably; their see-through parts are drawn
//      afterward in reverse list order (0x54B640) - so two glass meshes of the
//      same class swapped places as the visible set changed. With glassSort=1
//      both lists are sorted by class, then by the instance's address (the
//      order the frame's scene walk made them in): the same order every frame.
// ---------------------------------------------------------------------------
static float g_shadowSizeF = 8.0f;
static DWORD g_shadowSize = 8;
typedef HRESULT (__stdcall *CreateTex_t)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**, void*);
typedef HRESULT (__stdcall *CreateDS_t)(void*, UINT, UINT, DWORD, DWORD, DWORD, BOOL, void**, void*);
extern "C" HRESULT __cdecl ShadowCreateTex(BYTE* r) {          // replaces 0x57D7BA-0x57D7E7 (edi = r)
    void* dev = *reinterpret_cast<void**>(r + 0xB8);
    *reinterpret_cast<DWORD*>(r + 0x1CB6658) = 3;
    void** vt = *reinterpret_cast<void***>(dev);
    HRESULT hr = reinterpret_cast<CreateTex_t>(vt[0x5C / 4])(dev, g_shadowSize, g_shadowSize, 1, 1 /* RENDERTARGET */,
        0x72 /* R32F */, 0 /* DEFAULT */, reinterpret_cast<void**>(r + 0xA4), nullptr);
    if (hr < 0 && g_shadowSize != 8) {                          // (too big for this card: the game's size)
        Logf("shadows: %ux%u shadow map not made (%08lX), 8x8 kept\n", g_shadowSize, g_shadowSize, (unsigned long)hr);
        g_shadowSize = 8;
        hr = reinterpret_cast<CreateTex_t>(vt[0x5C / 4])(dev, 8, 8, 1, 1, 0x72, 0, reinterpret_cast<void**>(r + 0xA4), nullptr);
    }
    return hr;
}
extern "C" HRESULT __cdecl ShadowCreateDepth(BYTE* r, void** out) {   // replaces 0x57D7F8-0x57D815 (edi = r, ebx = out)
    void* dev = *reinterpret_cast<void**>(r + 0xB8);
    void** vt = *reinterpret_cast<void***>(dev);
    HRESULT hr = reinterpret_cast<CreateDS_t>(vt[0x74 / 4])(dev, g_shadowSize, g_shadowSize, 0x4D /* D24X8 */, 0, 0, TRUE, out, nullptr);
    static int logged = 0;
    if (logged++ < 2) Logf("shadows: sun shadow map %ux%u %s\n", g_shadowSize, g_shadowSize, hr >= 0 ? "made" : "FAILED");
    // the caster pass's viewport and the shaders' g_ShadowMapSize follow the size made
    DWORD vp = g_shadowSize;
    PatchBytes(hot::shadowViewport + 1, &vp, 4);
    g_shadowSizeF = (float)g_shadowSize;
    return hr;
}
extern "C" void ShadowTexStub(); extern "C" void ShadowDepthStub();
extern "C" { DWORD g_shadowTexCont = hot::shadowTexEnd; DWORD g_shadowDepthCont = hot::shadowDepthEnd; }
asm(".text\n"
    ".globl _ShadowTexStub\n_ShadowTexStub:\n"     // jmp from 0x57D7BA; on to 0x57D7E7 with eax = HRESULT
    "    pushl %edi\n"
    "    call _ShadowCreateTex\n"
    "    addl $4, %esp\n"
    "    jmp *_g_shadowTexCont\n"
    ".globl _ShadowDepthStub\n_ShadowDepthStub:\n"   // jmp from 0x57D7F8; on to 0x57D815 with eax = HRESULT
    "    pushl %ebx\n"
    "    pushl %edi\n"
    "    call _ShadowCreateDepth\n"
    "    addl $8, %esp\n"
    "    movl 0xB8(%edi), %edi\n"                  // (as the original left it)
    "    jmp *_g_shadowDepthCont\n");
static float g_shadowDepth = 64.0f, g_shadowFadeW = 16.0f, g_shadowWide = 128.0f;

static float g_shadowBackOff = 1000.0f, g_shadowFar = 2000.0f, g_shadowFadeOut = 16.0f;
static int __cdecl AlphaRecCmp(const void* a, const void* b) {
    const BYTE* ra = *static_cast<BYTE* const*>(a);
    const BYTE* rb = *static_cast<BYTE* const*>(b);
    int pa = *reinterpret_cast<const int*>(ra), pb = *reinterpret_cast<const int*>(rb);
    if (pa != pb) return pa < pb ? -1 : 1;                      // (pass first, as the game)
    float za = *reinterpret_cast<const float*>(ra + 0x70), zb = *reinterpret_cast<const float*>(rb + 0x70);
    if (za != zb) return za > zb ? -1 : 1;                      // farther (larger) first
    return ra < rb ? -1 : (ra > rb ? 1 : 0);
}
extern "C" void __cdecl AlphaSort(BYTE** list, int count) {
    if (list && count > 1 && count < 100000) qsort(list, count, sizeof(BYTE*), AlphaRecCmp);
}
static int __cdecl MeshInstCmp(const void* a, const void* b) {
    const BYTE* ia = *static_cast<BYTE* const*>(a);
    const BYTE* ib = *static_cast<BYTE* const*>(b);
    int ka = *reinterpret_cast<const int*>(ia + 0x1FC), kb = *reinterpret_cast<const int*>(ib + 0x1FC);
    if (ka != kb) return ka < kb ? -1 : 1;                      // technique class first, as the game (draws go by its ranges)
    return ia < ib ? -1 : (ia > ib ? 1 : 0);
}
extern "C" void __cdecl MeshSort(BYTE** list, int count) {
    if (list && count > 1 && count < 100000) qsort(list, count, sizeof(BYTE*), MeshInstCmp);
}
extern "C" void MeshSortStub1(); extern "C" void MeshSortStub2();
extern "C" { DWORD g_meshSort1Cont = hot::meshSort2; DWORD g_meshSort2Cont = hot::meshSortEnd; }
asm(".text\n"
    ".globl _MeshSortStub1\n_MeshSortStub1:\n"   // jmp from 0x54BB17: esi = group, ebx = renderer; on to 0x54BD77
    "    pushal\n"
    "    movl (%esi), %ecx\n"
    "    leal 0xEC4128(%ebx,%ecx,4), %edx\n"
    "    pushl 4(%esi)\n"
    "    pushl %edx\n"
    "    call _MeshSort\n"
    "    addl $8, %esp\n"
    "    popal\n"
    "    jmp *_g_meshSort1Cont\n"
    ".globl _MeshSortStub2\n_MeshSortStub2:\n"   // jmp from 0x54BD77; on to 0x54BFE7
    "    pushal\n"
    "    movl 8(%esi), %ecx\n"
    "    leal 0xED79A8(%ebx,%ecx,4), %edx\n"
    "    pushl 0xC(%esi)\n"
    "    pushl %edx\n"
    "    call _MeshSort\n"
    "    addl $8, %esp\n"
    "    popal\n"
    "    jmp *_g_meshSort2Cont\n");
extern "C" void AlphaSortStub();
extern "C" { DWORD g_alphaSortCont = hot::alphaSortEnd; }
asm(".text\n"
    ".globl _AlphaSortStub\n_AlphaSortStub:\n"   // jmp from 0x54B8F8: eax = count, esi = segment, ebx = renderer; on to 0x54BB17
    "    pushal\n"
    "    movl 0x10(%esi), %ecx\n"
    "    leal 0xD53628(%ebx,%ecx,4), %edx\n"     // the segment's draws in the alpha list
    "    pushl %eax\n"
    "    pushl %edx\n"
    "    call _AlphaSort\n"
    "    addl $8, %esp\n"
    "    popal\n"
    "    jmp *_g_alphaSortCont\n");
typedef void (__attribute__((thiscall)) *BlendMode_t)(BYTE*, int);
static void __attribute__((thiscall)) HkBlendMode(BYTE* r, int mode) {
    reinterpret_cast<BlendMode_t>(hot::fnBlendMode)(r, mode);
    if (mode == 0 || mode == 1 || mode == 2 || mode == 3 || mode == 6)
        reinterpret_cast<BlendMode_t>(hot::fnZWrite)(r, 0);
}
// Sky. The level's sky (its SKY_ node, drawn first by 0x53E650) is centred on
// the camera, scale 1, with the far plane set to 5000 for it (0x53E7B9) - with
// the wider speed-FOV view a black hole showed at the top of it. skyFar: the
// sky's own far plane (0x53E7BB reads our value; 0x8AA444 itself has 11 other
// users); skyStretch: the sky's world matrix (both instances, 0x53E84F and
// 0x53EA33: they load the SetWorld call) gets its up axis scaled, so its rim
// stays above a wider view.
static float g_skyFar = 5000.0f;
extern "C" { float g_skyStretch = 1.0f; }             // (read by the stub below)
extern "C" void SkyWorldStub();
asm(".text\n"
    ".globl _SkyWorldStub\n_SkyWorldStub:\n"     // called instead of the renderer's SetWorld (vtable +0x224): ecx = renderer, the matrix by value at [esp+4]
    "    flds 0x14(%esp)\n    fmuls _g_skyStretch\n    fstps 0x14(%esp)\n"   // row 1 (up): x y z
    "    flds 0x18(%esp)\n    fmuls _g_skyStretch\n    fstps 0x18(%esp)\n"
    "    flds 0x1C(%esp)\n    fmuls _g_skyStretch\n    fstps 0x1C(%esp)\n"
    "    movl (%ecx), %eax\n"
    "    jmp *0x224(%eax)\n");
void InstallRendering() {
    if (g_cfg.skyFar > 5000.0f) {
        g_skyFar = g_cfg.skyFar;
        DWORD a = reinterpret_cast<DWORD>(&g_skyFar);
        if (!PatchBytes(hot::skyFarLd + 2, &a, 4)) Logf("sky: far plane patch FAILED\n");
        else Logf("sky: far plane %.0f (game: 5000)\n", g_skyFar);
    }
    if (g_cfg.skyStretch > 1.0f) {
        g_skyStretch = g_cfg.skyStretch > 4.0f ? 4.0f : g_cfg.skyStretch;
        BYTE mv[6] = { 0xBA, 0, 0, 0, 0, 0x90 };              // mov edx, SkyWorldStub / nop
        DWORD a = reinterpret_cast<DWORD>(&SkyWorldStub); memcpy(mv + 1, &a, 4);
        if (!PatchBytes(hot::skySetWorld1, mv, 6) || !PatchBytes(hot::skySetWorld2, mv, 6)) Logf("sky: stretch patch FAILED\n");
        else Logf("sky: stretched %.2fx upward\n", g_skyStretch);
    }
    if (g_cfg.shadowMapSize > 8) {
        g_shadowSize = (DWORD)ClampInt(g_cfg.shadowMapSize, 8, 4096);
        BYTE j1[6] = { 0xE9 }, j2[6] = { 0xE9 };
        DWORD r1 = reinterpret_cast<DWORD>(&ShadowTexStub) - (hot::shadowTexMake + 5); memcpy(j1 + 1, &r1, 4); j1[5] = 0x90;
        DWORD r2 = reinterpret_cast<DWORD>(&ShadowDepthStub) - (hot::shadowDepthMake + 5); memcpy(j2 + 1, &r2, 4); j2[5] = 0x90;
        BYTE fld[6] = { 0xD9, 0x05 }; DWORD fa = reinterpret_cast<DWORD>(&g_shadowSizeF); memcpy(fld + 2, &fa, 4);
        if (!PatchBytes(hot::shadowTexMake, j1, 6) || !PatchBytes(hot::shadowDepthMake, j2, 6) ||
            !PatchBytes(hot::shadowSizeParam, fld, 6)) Logf("shadows: patch FAILED\n");
        else Logf("shadows: sun shadow map %ux%u (game: 8x8)\n", g_shadowSize, g_shadowSize);
    }
    if (g_cfg.shadowRange > 0.0f && g_cfg.shadowRange != 64.0f) {
        g_shadowDepth = g_cfg.shadowRange;
        g_shadowFadeW = g_cfg.shadowFade > 0.0f ? g_cfg.shadowFade : 16.0f;
        if (g_shadowFadeW > g_shadowDepth * 0.5f) g_shadowFadeW = g_shadowDepth * 0.5f;
        g_shadowWide = g_shadowDepth * 2.0f;
        if (!PatchBytes(hot::shadowDepthK, &g_shadowDepth, 4) || !PatchBytes(hot::shadowFadeK, &g_shadowFadeW, 4) ||
            !PatchBytes(hot::shadowWideK, &g_shadowWide, 4)) Logf("shadows: range patch FAILED\n");
        else Logf("shadows: reach %.0f units, fading over the last %.0f (game: 64, 16)\n", g_shadowDepth, g_shadowFadeW);
    }
    if (g_cfg.glassSort) {
        BYTE j[6] = { 0xE9 }; DWORD rel = reinterpret_cast<DWORD>(&AlphaSortStub) - (hot::alphaSort + 5);
        memcpy(j + 1, &rel, 4); j[5] = 0x90;
        BYTE j1[5] = { 0xE9 }, j2[5] = { 0xE9 };
        DWORD r1 = reinterpret_cast<DWORD>(&MeshSortStub1) - (hot::alphaSortEnd + 5); memcpy(j1 + 1, &r1, 4);
        DWORD r2 = reinterpret_cast<DWORD>(&MeshSortStub2) - (hot::meshSort2 + 5); memcpy(j2 + 1, &r2, 4);
        if (!PatchBytes(hot::alphaSort, j, 6) || !PatchBytes(hot::alphaSortEnd, j1, 5) || !PatchBytes(hot::meshSort2, j2, 5))
            Logf("glass: sort patch FAILED\n");
        else Logf("glass: see-through draws sorted back to front\n");
    }
    if (g_cfg.glassNoZWrite) {
        if (!PatchCall(hot::callBlendMode, reinterpret_cast<void*>(&HkBlendMode), 5)) Logf("glass: depth patch FAILED\n");
        else Logf("glass: blended draws don't write depth\n");
    }
    if (g_cfg.shadowFit) {
        g_shadowBackOff = g_cfg.shadowBackOff > 1.0f ? g_cfg.shadowBackOff : 1000.0f;
        g_shadowFar = g_cfg.shadowFar > g_shadowBackOff ? g_cfg.shadowFar : g_shadowBackOff * 2.0f;
        float need = g_shadowBackOff + (g_cfg.shadowRange > 64.0f ? g_cfg.shadowRange : 64.0f);   // (the light must see the whole box)
        if (g_shadowFar < need) g_shadowFar = need;
        g_shadowFadeOut = g_shadowFadeW;                        // (the fade the shadow box got: 16 if its reach is the game's)
        DWORD a1 = reinterpret_cast<DWORD>(&g_shadowBackOff), a2 = reinterpret_cast<DWORD>(&g_shadowFar),
              a3 = reinterpret_cast<DWORD>(&g_shadowFadeOut);
        if (!PatchBytes(hot::shadowBackOffLd + 2, &a1, 4) || !PatchBytes(hot::shadowFarLd + 2, &a2, 4) ||
            !PatchBytes(hot::shadowFadeLd + 2, &a3, 4)) Logf("shadows: depth range patch FAILED\n");
        else Logf("shadows: light backs off %.0f toward the sun, sees %.0f deep (game: 16, 64)\n", g_shadowBackOff, g_shadowFar);
    }
}

// Car shadows. Each car's shadow is a projector (0x602F40) set 10 units above
// the middle of the car, looking down, from 9.5 to 10 + 3 + 1.4 units: down to
// 4.4 below the car's middle, through its own road onto anything below (a car on
// the spiral's next level shadowed the road under you). carShadowDepth sets how
// far below the middle it reaches: the 3 (a shared constant, 0x8A4268) is read
// from our own value instead.
static double g_shadowFarAdd = 3.0;
void InstallCarShadowDepth() {
    if (g_cfg.carShadowDepth <= 0.0f) return;
    float d = g_cfg.carShadowDepth < 0.5f ? 0.5f : g_cfg.carShadowDepth;
    g_shadowFarAdd = d - 1.4;
    BYTE ins[6] = { 0xDC, 0x05 };                               // fadd qword ptr [&g_shadowFarAdd]
    DWORD a = reinterpret_cast<DWORD>(&g_shadowFarAdd); memcpy(ins + 2, &a, 4);
    Logf(PatchBytes(hot::carShadowFar, ins, 6) ? "shadows: car shadows reach %.1f below the car (game: 4.4)\n"
                                                : "shadows: car shadow depth patch FAILED\n", d);
}
