/*
 * psp_vkgl.inc - the Vulkan renderer for the MGS Portable Ops PC port.
 *
 * The OpenGL renderer (psp_gl.inc) is the one that knows the PSP: how its draws become triangles, framebuffers,
 * textures, the 60 fps in-between pictures. It reaches the graphics card through ~60 OpenGL calls, all through
 * function pointers. This file implements exactly those calls on top of Vulkan 1.3, so psp_gl.inc runs unchanged on
 * Vulkan: vk_setup() hands psp_gl.inc's gl_setup() a "getproc" that returns the functions below instead of the
 * graphics driver's OpenGL ones.
 *
 * How the OpenGL calls map:
 *  - textures / renderbuffers -> VkImages (always in GENERAL layout; a full barrier between rendering and copies
 *    keeps it simple and correct). Re-uploading a texture makes a fresh image; the old one is destroyed once the
 *    GPU is done with the frame that used it, so nothing is ever overwritten while in use.
 *  - framebuffers -> dynamic rendering (no render pass objects). Framebuffer 0 is the window (swapchain).
 *  - the two shader programs -> precompiled SPIR-V (psp_vkspv.h), uniforms -> push constants.
 *  - state that OpenGL keeps (blend, depth, scissor...) -> pipelines from a cache + dynamic state.
 * Coordinates: images are stored the OpenGL way (row 0 = bottom), so every coordinate psp_gl.inc computes is used
 * as it is; only the window (swapchain) is drawn with a flipped viewport.
 *
 * Vulkan is reached only through the vkGetInstanceProcAddr Python hands over: the DLL still imports nothing.
 */
#define VK_NO_PROTOTYPES
#include "vk/vulkan_core.h"
#include "psp_vkspv.h"

/* ------------------------------------------------------------------ Vulkan entry points */
#define VKF(name) static PFN_##name name##_
VKF(vkGetInstanceProcAddr); VKF(vkGetDeviceProcAddr); VKF(vkCreateInstance); VKF(vkEnumerateInstanceExtensionProperties);
VKF(vkEnumeratePhysicalDevices); VKF(vkGetPhysicalDeviceProperties); VKF(vkGetPhysicalDeviceFeatures); VKF(vkGetPhysicalDeviceQueueFamilyProperties);
VKF(vkGetPhysicalDeviceMemoryProperties); VKF(vkCreateDevice); VKF(vkEnumerateDeviceExtensionProperties); VKF(vkDestroyInstance);
VKF(vkGetPhysicalDeviceSurfaceSupportKHR); VKF(vkGetPhysicalDeviceSurfaceCapabilitiesKHR); VKF(vkGetPhysicalDeviceSurfaceFormatsKHR);
VKF(vkGetPhysicalDeviceSurfacePresentModesKHR); VKF(vkDestroySurfaceKHR); VKF(vkGetPhysicalDeviceFormatProperties);
VKF(vkGetDeviceQueue); VKF(vkCreateCommandPool); VKF(vkAllocateCommandBuffers); VKF(vkBeginCommandBuffer); VKF(vkEndCommandBuffer);
VKF(vkResetCommandBuffer); VKF(vkQueueSubmit); VKF(vkQueueWaitIdle); VKF(vkDeviceWaitIdle); VKF(vkCreateFence); VKF(vkWaitForFences); VKF(vkGetFenceStatus); VKF(vkResetFences);
VKF(vkCreateSemaphore); VKF(vkCreateImage); VKF(vkDestroyImage); VKF(vkGetImageMemoryRequirements); VKF(vkAllocateMemory); VKF(vkFreeMemory);
VKF(vkBindImageMemory); VKF(vkCreateImageView); VKF(vkDestroyImageView); VKF(vkCreateBuffer); VKF(vkDestroyBuffer);
VKF(vkGetBufferMemoryRequirements); VKF(vkBindBufferMemory); VKF(vkMapMemory); VKF(vkCreateSampler); VKF(vkCreateShaderModule);
VKF(vkCreateDescriptorSetLayout); VKF(vkCreatePipelineLayout); VKF(vkCreateGraphicsPipelines); VKF(vkCreatePipelineCache); VKF(vkGetPipelineCacheData); VKF(vkDestroyPipelineCache);
VKF(vkCmdPipelineBarrier); VKF(vkCmdCopyBufferToImage); VKF(vkCmdCopyImageToBuffer); VKF(vkCmdBlitImage); VKF(vkCmdResolveImage);
VKF(vkCmdCopyImage); VKF(vkCmdBeginRendering); VKF(vkCmdEndRendering); VKF(vkCmdBindPipeline); VKF(vkCmdSetViewport); VKF(vkCmdSetScissor);
VKF(vkCmdSetBlendConstants); VKF(vkCmdSetDepthTestEnable); VKF(vkCmdSetDepthWriteEnable); VKF(vkCmdSetDepthCompareOp);
VKF(vkCmdSetStencilTestEnable); VKF(vkCmdSetStencilOp); VKF(vkCmdSetStencilCompareMask); VKF(vkCmdSetStencilWriteMask); VKF(vkCmdSetStencilReference);
VKF(vkCmdSetPrimitiveTopology); VKF(vkCmdPushConstants); VKF(vkCmdBindVertexBuffers); VKF(vkCmdDraw); VKF(vkCmdClearAttachments);
VKF(vkCreateDescriptorPool); VKF(vkResetDescriptorPool); VKF(vkAllocateDescriptorSets); VKF(vkUpdateDescriptorSets); VKF(vkCmdBindDescriptorSets); VKF(vkCmdClearColorImage); VKF(vkCmdClearDepthStencilImage);
VKF(vkCreateSwapchainKHR); VKF(vkDestroySwapchainKHR); VKF(vkGetSwapchainImagesKHR); VKF(vkAcquireNextImageKHR); VKF(vkQueuePresentKHR);
typedef struct { VkStructureType sType; const void *pNext; VkFlags flags; void *hinstance; void *hwnd; } VkWin32SurfaceCreateInfo_;
typedef VkResult (VKAPI_PTR *PFN_vkCreateWin32SurfaceKHR_)(VkInstance, const VkWin32SurfaceCreateInfo_ *, const VkAllocationCallbacks *, VkSurfaceKHR *);
static PFN_vkCreateWin32SurfaceKHR_ vkCreateWin32SurfaceKHR_;

static void *vk_calloc(size_t n); static void vk_free(void *p); static void dev_idle(void);
/* (perf_log) time spent, in processor clock ticks: 0 waiting for the graphics card (frame fence), 1 waiting for the
 * lock (drawing thread), 2 getting a window image, 3 sending work (submit), 4 showing a picture (present call),
 * 5 the pacer's whole vk_present_held, 6 making pipelines, 7 waiting for the lock (pacer) */
static volatile uint64_t g_tm[8];
#define TSC() __builtin_ia32_rdtsc()
GEXPORT void vk_timers(uint64_t *o) { for (int i = 0; i < 8; i++) o[i] = g_tm[i]; }
GEXPORT uint64_t vk_tsc(void) { return TSC(); }
/* ------------------------------------------------------------------ the device */
#define VK_FRAMES 2                                         /* frames the GPU may be working on while the next is built */
#define RING_SIZE (96u << 20)                               /* per frame: vertices and texture uploads */
typedef struct { VkImage img; VkDeviceMemory mem; VkImageView view; VkFormat fmt; int w, h, levels, samples, depth; int blk; VkDeviceSize off, size; } VImg;
typedef struct { VkCommandBuffer up, cb; VkFence fence; VkBuffer ring; VkDeviceMemory ringmem; uint8_t *ringp; uint32_t ringused;
                 VImg *dead[2048]; int ndead; VkBuffer deadbuf[64]; VkDeviceMemory deadmem[64]; int ndeadbuf; int busy;
                 VkDescriptorPool dp[16]; int ndp, curdp; VkImageView lastview, lastview1, lastview2; VkSampler lastsamp; VkDescriptorSet lastset; uint64_t seq; } VFrame;
static struct {
    VkInstance inst; VkPhysicalDevice pd; VkDevice dev; VkQueue q; uint32_t qfam; VkCommandPool pool;
    VkPhysicalDeviceMemoryProperties memp; VkPhysicalDeviceProperties props; int aniso_ok, clamp_ok; VkFormat dsfmt; int stencil_ok;
    VkSurfaceKHR surf; VkSwapchainKHR sc; VkFormat scfmt; VkExtent2D scext; uint32_t nsc; VkImage scimg[8]; VkImageView scview[8];
    VkSemaphore acq_sem[VK_FRAMES], done_sem[8]; uint32_t scidx; int acquired, acq_wait, acq_slot, sc_ok; VkPresentModeKHR pmode;
    VFrame fr[VK_FRAMES]; int f;                            /* f = the frame being built */
    VkDescriptorSetLayout dsl; VkPipelineLayout pl; VkShaderModule sm[4];
    VImg *dummy, *head;                                     /* head: the window picture when there is no window (tests) */
    VkBuffer rb; VkDeviceMemory rbmem; uint8_t *rbp; uint32_t rbsize;   /* readbacks */
    int rendering; GLuint rfb; VkFormat r_cfmt, r_dfmt; int r_samples, r_w, r_h; VkPipeline r_pipe;
    int headless; int win_w, win_h; char err[256];
} V;
static void vk_err(const char *what, VkResult r) {
    static const char hx[] = "0123456789abcdef"; char code[12]; uint32_t u = (uint32_t)r; int k = 0;
    code[k++] = '('; for (int s = 28; s >= 0; s -= 4) code[k++] = hx[(u >> s) & 15]; code[k++] = ')'; code[k] = 0;
    int n = 0; for (const char *p = what; *p && n < 200; p++) V.err[n++] = *p;
    V.err[n++] = ' '; for (int i = 0; code[i]; i++) V.err[n++] = code[i]; V.err[n] = 0;
}
static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < V.memp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (V.memp.memoryTypes[i].propertyFlags & want) == want) return i;
    return 0xFFFFFFFF;
}
static int make_buffer(VkDeviceSize size, VkBufferUsageFlags use, VkBuffer *b, VkDeviceMemory *m, uint8_t **p) {
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = size; bi.usage = use; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer_(V.dev, &bi, 0, b)) return 0;
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements_(V.dev, *b, &mr);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = mr.size;
    ai.memoryTypeIndex = 0xFFFFFFFF;
    if (use == VK_BUFFER_USAGE_TRANSFER_DST_BIT) {
        /* readbacks are read by the CPU: memory it can cache, in the PC's own memory - reading uncached memory (or the
         * graphics card's own, over the bus) ran at a few hundred MB/s: 13 MB per copy of the screen took ~100 ms */
        VkMemoryPropertyFlags w = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        for (uint32_t i = 0; i < V.memp.memoryTypeCount && ai.memoryTypeIndex == 0xFFFFFFFF; i++)
            if ((mr.memoryTypeBits & (1u << i)) && (V.memp.memoryTypes[i].propertyFlags & w) == w
                && !(V.memp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) ai.memoryTypeIndex = i;
        if (ai.memoryTypeIndex == 0xFFFFFFFF) ai.memoryTypeIndex = mem_type(mr.memoryTypeBits, w);
    }
    if (ai.memoryTypeIndex == 0xFFFFFFFF) ai.memoryTypeIndex = mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == 0xFFFFFFFF || vkAllocateMemory_(V.dev, &ai, 0, m)) return 0;
    vkBindBufferMemory_(V.dev, *b, *m, 0);
    return vkMapMemory_(V.dev, *m, 0, VK_WHOLE_SIZE, 0, (void **)p) == VK_SUCCESS;
}
static void full_barrier(VkCommandBuffer cb) {
    VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier_(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, 0, 0, 0);
}
static void img_layout(VkCommandBuffer cb, VkImage img, int depth, int levels, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from; b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = img;
    b.subresourceRange.aspectMask = depth == 2 ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = (uint32_t)levels; b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier_(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, 0, 0, 0, 1, &b);
}
/* ---- image memory: carved out of big blocks (the driver allows only ~4000 separate allocations, and each one is slow).
 * Each block keeps a sorted list of its free ranges; big images (render targets at high resolution) get their own */
#define MB_SIZE ((VkDeviceSize)128 << 20)
#define MB_MAX 64
#define MB_FREE 2048
typedef struct { VkDeviceMemory mem; uint32_t type; int nfree; VkDeviceSize fo[MB_FREE], fs[MB_FREE]; } MBlock;
static MBlock *g_mb[MB_MAX], g_mbst[MB_MAX]; static int g_nmb;
static VkDeviceSize g_img_bytes; static int g_img_count, g_img_dedicated;
static void mb_move(VkDeviceSize *d, const VkDeviceSize *src, size_t bytes) {   /* (overlapping copy: no C library here) */
    size_t n = bytes / sizeof(VkDeviceSize);
    if (d < src) for (size_t i = 0; i < n; i++) d[i] = src[i]; else for (size_t i = n; i-- > 0;) d[i] = src[i];
}
static int mb_take(MBlock *b, VkDeviceSize size, VkDeviceSize align, VkDeviceSize *out) {
    for (int i = 0; i < b->nfree; i++) {
        VkDeviceSize o = (b->fo[i] + align - 1) / align * align, end = b->fo[i] + b->fs[i];
        if (o + size > end) continue;
        VkDeviceSize f0 = b->fo[i], tail = end - (o + size);
        if (o > f0) {                                          /* (alignment gap stays free) */
            b->fs[i] = o - f0;
            if (tail) { if (b->nfree >= MB_FREE) return 0; mb_move(&b->fo[i + 2], &b->fo[i + 1], sizeof(VkDeviceSize) * (size_t)(b->nfree - i - 1));
                        mb_move(&b->fs[i + 2], &b->fs[i + 1], sizeof(VkDeviceSize) * (size_t)(b->nfree - i - 1)); b->fo[i + 1] = o + size; b->fs[i + 1] = tail; b->nfree++; }
        } else if (tail) { b->fo[i] = o + size; b->fs[i] = tail; }
        else { mb_move(&b->fo[i], &b->fo[i + 1], sizeof(VkDeviceSize) * (size_t)(b->nfree - i - 1)); mb_move(&b->fs[i], &b->fs[i + 1], sizeof(VkDeviceSize) * (size_t)(b->nfree - i - 1)); b->nfree--; }
        *out = o; return 1;
    }
    return 0;
}
static void mb_give(MBlock *b, VkDeviceSize o, VkDeviceSize size) {
    int i = 0; while (i < b->nfree && b->fo[i] < o) i++;
    int prev = i > 0 && b->fo[i - 1] + b->fs[i - 1] == o, next = i < b->nfree && o + size == b->fo[i];
    if (prev && next) { b->fs[i - 1] += size + b->fs[i]; mb_move(&b->fo[i], &b->fo[i + 1], sizeof(VkDeviceSize) * (size_t)(b->nfree - i - 1)); mb_move(&b->fs[i], &b->fs[i + 1], sizeof(VkDeviceSize) * (size_t)(b->nfree - i - 1)); b->nfree--; }
    else if (prev) b->fs[i - 1] += size;
    else if (next) { b->fo[i] = o; b->fs[i] += size; }
    else if (b->nfree < MB_FREE) { mb_move(&b->fo[i + 1], &b->fo[i], sizeof(VkDeviceSize) * (size_t)(b->nfree - i)); mb_move(&b->fs[i + 1], &b->fs[i], sizeof(VkDeviceSize) * (size_t)(b->nfree - i)); b->fo[i] = o; b->fs[i] = size; b->nfree++; }
    /* (no room to note it: that bit of the block stays unused) */
}
static int img_mem(VImg *m, const VkMemoryRequirements *mr, uint32_t type) {
    m->blk = -1; m->off = 0; m->size = mr->size;
    if (mr->size <= MB_SIZE / 4) {
        for (int k = 0; k < g_nmb; k++) { MBlock *b = g_mb[k]; VkDeviceSize o;
            if (b && b->type == type && mb_take(b, mr->size, mr->alignment ? mr->alignment : 1, &o)) { m->mem = b->mem; m->off = o; m->blk = k; return 1; } }
        if (g_nmb < MB_MAX) {
            MBlock *b = &g_mbst[g_nmb]; memset(b, 0, sizeof *b);
            VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = MB_SIZE; ai.memoryTypeIndex = type;
            if (b && vkAllocateMemory_(V.dev, &ai, 0, &b->mem) == VK_SUCCESS) {
                b->type = type; b->nfree = 1; b->fo[0] = 0; b->fs[0] = MB_SIZE; g_mb[g_nmb] = b; VkDeviceSize o;
                if (mb_take(b, mr->size, mr->alignment ? mr->alignment : 1, &o)) { m->mem = b->mem; m->off = o; m->blk = g_nmb++; return 1; }
                g_nmb++;
            }
        }
    }
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = mr->size; ai.memoryTypeIndex = type;
    VkResult r = vkAllocateMemory_(V.dev, &ai, 0, &m->mem); if (r) { vk_err("vkAllocateMemory (image)", r); return 0; }
    g_img_dedicated++; return 1;
}
/* a new image, already in GENERAL layout (the transition is recorded into the upload commands: before any use) */
static VImg *make_image__b(int w, int h, VkFormat fmt, int samples, int levels, int depth);
static VImg *make_image(int w, int h, VkFormat fmt, int samples, int levels, int depth) { uint64_t _t = PF_TSC(); VImg * _r = make_image__b(w, h, fmt, samples, levels, depth); pf_add(11, _t); return _r; }
static VImg *make_image__b(int w, int h, VkFormat fmt, int samples, int levels, int depth) {
    VImg *m = (VImg *)vk_calloc(sizeof(VImg)); if (!m) return 0;
    m->w = w; m->h = h; m->fmt = fmt; m->samples = samples; m->levels = levels; m->depth = depth;
    VkImageCreateInfo ii = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt; ii.extent.width = (uint32_t)w; ii.extent.height = (uint32_t)h; ii.extent.depth = 1;
    ii.mipLevels = (uint32_t)levels; ii.arrayLayers = 1; ii.samples = (VkSampleCountFlagBits)samples; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                     : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       (samples == 1 ? VK_IMAGE_USAGE_SAMPLED_BIT : 0);
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = vkCreateImage_(V.dev, &ii, 0, &m->img); if (r) { vk_err("vkCreateImage", r); vk_free(m); return 0; }
    VkMemoryRequirements mr; vkGetImageMemoryRequirements_(V.dev, m->img, &mr);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = mr.size;
    ai.memoryTypeIndex = mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == 0xFFFFFFFF) ai.memoryTypeIndex = mem_type(mr.memoryTypeBits, 0);
    if (!img_mem(m, &mr, ai.memoryTypeIndex)) { vkDestroyImage_(V.dev, m->img, 0); vk_free(m); return 0; }
    vkBindImageMemory_(V.dev, m->img, m->mem, m->off); g_img_bytes += m->size; g_img_count++;
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = m->img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt;
    vi.subresourceRange.aspectMask = depth == 2 ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = (uint32_t)levels; vi.subresourceRange.layerCount = 1;
    vkCreateImageView_(V.dev, &vi, 0, &m->view);
    img_layout(V.fr[V.f].up, m->img, depth, levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    return m;
}
static void dcache_forget(VkImageView v);
static void free_image_now(VImg *m) {
    if (!m) return;
    dcache_forget(m->view);
    vkDestroyImageView_(V.dev, m->view, 0); vkDestroyImage_(V.dev, m->img, 0);
    if (m->blk >= 0 && m->blk < g_nmb && g_mb[m->blk]) mb_give(g_mb[m->blk], m->off, m->size); else { vkFreeMemory_(V.dev, m->mem, 0); g_img_dedicated--; }
    g_img_bytes -= m->size; g_img_count--; vk_free(m);
}
static void drop_image(VImg *m) {                          /* destroyed once the GPU is done with this frame */
    if (!m) return;
    VFrame *F = &V.fr[V.f];
    if (F->ndead < 2048) F->dead[F->ndead++] = m;
    else { dev_idle(); free_image_now(m); }
}

/* ------------------------------------------------------------------ memory without a C library */
static uint8_t *g_vk_heap; static size_t g_vk_heap_used, g_vk_heap_size;
typedef struct VkFreeBlk { struct VkFreeBlk *next; size_t size; } VkFreeBlk;
static VkFreeBlk *g_vk_freel;
static void *vk_calloc(size_t n) {                          /* small fixed-size objects (VImg): a free list per exact size */
    n = (n + 15) & ~(size_t)15;
    for (VkFreeBlk **p = &g_vk_freel; *p; p = &(*p)->next)
        if ((*p)->size == n) { VkFreeBlk *b = *p; *p = b->next; memset(b, 0, n); return b; }
    if (!g_vk_heap || g_vk_heap_used + n > g_vk_heap_size) return 0;
    void *r = g_vk_heap + g_vk_heap_used; g_vk_heap_used += n; memset(r, 0, n); return r;
}
static void vk_free(void *p) {
    if (!p) return;
    VkFreeBlk *b = (VkFreeBlk *)p; b->size = (sizeof(VImg) + 15) & ~(size_t)15; b->next = g_vk_freel; g_vk_freel = b;
}

/* ------------------------------------------------------------------ OpenGL objects */
#define VG_MAX 8192
typedef struct { VImg *im; GLenum minf, magf, ws, wt; float aniso; int used; } VGTex;
typedef struct { VImg *im; int used; } VGRb;
typedef struct { int ctype; GLuint color; GLuint depth; int used; } VGFb;     /* ctype 1 = texture, 2 = renderbuffer */
static VGTex vg_tex[VG_MAX]; static VGRb vg_rb[VG_MAX]; static VGFb vg_fb[256];
static GLuint vg_ntex = 1, vg_nrb = 1, vg_nfb = 1; static GLuint vg_freetex[VG_MAX]; static int vg_nfreetex;
/* state */
static struct {
    GLuint tex, tex1, tex2, rb, dfb, rfb, prog; int unit; int scissor, depth, blend, stencil; GLenum sfunc, sfail, szfail, szpass; int sref, smask, swmask, cstencil; int sc[4], vp[4]; GLboolean cmask[4], dmask; GLenum dfunc;
    GLenum bs, bd, bsa, bda, beq; float bcol[4]; float ccol[4]; double cdepth; uint32_t vbo_off; int vbo_ok; GLenum err;
    uint8_t pc[2][128];                                     /* push constants (uniforms) of program 1 and 2 */
    int shader_kind[64]; int nshader; int prog_kind[16]; int nprog; const char *src[64];
} S;

/* ---- pipelines: one per (program, attachments, blending) */
typedef struct { uint32_t prog, cfmt, dfmt, samples, blend, bs, bd, bsa, bda, beq, cmask; } PKey;
typedef struct { PKey k; VkPipeline p; } PEnt;
static PEnt vg_pipes[1024]; static int vg_npipes;
/* pipelines are slow to make (the graphics driver compiles the shaders for each one): a new one in the middle of play
 * is a hitch. So they are looked up through a hash table, compiled with a pipeline cache, and the cache plus the list
 * of pipelines used is saved (play.py: vk_pipelines.bin) and all of them are made again at the next start */
static short vg_phash[4096]; static VkPipelineCache g_pcache; static int g_pipe_new, g_pipe_dirty;
static uint32_t pk_hash(const PKey *k) { const uint32_t *w = (const uint32_t *)k; uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof *k / 4; i++) h = (h ^ w[i]) * 16777619u; return (h ^ (h >> 15)) & 4095; }
static VkBlendFactor bf(GLenum f) {
    switch (f) {
    case GL_ZERO: return VK_BLEND_FACTOR_ZERO; case GL_ONE: return VK_BLEND_FACTOR_ONE;
    case GL_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR; case GL_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case GL_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA; case GL_ONE_MINUS_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA; case GL_ONE_MINUS_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case GL_DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR; case GL_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case GL_CONSTANT_COLOR: return VK_BLEND_FACTOR_CONSTANT_COLOR; case GL_ONE_MINUS_CONSTANT_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case 0x8003: return VK_BLEND_FACTOR_CONSTANT_ALPHA; case 0x8004: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    default: return VK_BLEND_FACTOR_ONE;
    }
}
static VkBlendOp bop(GLenum e) {
    switch (e) { case GL_FUNC_SUBTRACT: return VK_BLEND_OP_SUBTRACT; case GL_FUNC_REVERSE_SUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
                 case GL_MIN: return VK_BLEND_OP_MIN; case GL_MAX: return VK_BLEND_OP_MAX; default: return VK_BLEND_OP_ADD; }
}
static VkPipeline get_pipe__b(const PKey *k);
static VkPipeline get_pipe(const PKey *k) { uint64_t _t = PF_TSC(); VkPipeline _r = get_pipe__b(k); pf_add(12, _t); return _r; }
static VkPipeline get_pipe__b(const PKey *k) {
    uint32_t h = pk_hash(k);
    for (int j = 0; j < 4096; j++) { int e = vg_phash[(h + j) & 4095]; if (!e) break; if (!memcmp(&vg_pipes[e - 1].k, k, sizeof *k)) return vg_pipes[e - 1].p; }
    int main_prog = k->prog == 1;
    VkPipelineShaderStageCreateInfo st[2]; memset(st, 0, sizeof st);
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[0].pName = st[1].pName = "main";
    st[0].module = V.sm[main_prog ? 0 : 2]; st[1].module = V.sm[main_prog ? 1 : 3];
    VkVertexInputBindingDescription vb = {0, sizeof(GLVtx), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[5] = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 16},
                                               {2, 0, VK_FORMAT_R8G8B8A8_UNORM, 24}, {3, 0, VK_FORMAT_R32_SFLOAT, 28}, {4, 0, VK_FORMAT_R32G32B32_SFLOAT, 32}};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (main_prog) { vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vb; vi.vertexAttributeDescriptionCount = 5; vi.pVertexAttributeDescriptions = va; }
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.depthClampEnable = V.clamp_ok ? VK_TRUE : VK_FALSE; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = (VkSampleCountFlagBits)k->samples;
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS; ds.maxDepthBounds = 1.f;
    VkPipelineColorBlendAttachmentState ba; memset(&ba, 0, sizeof ba);
    ba.blendEnable = k->blend ? VK_TRUE : VK_FALSE; ba.srcColorBlendFactor = bf(k->bs); ba.dstColorBlendFactor = bf(k->bd);
    ba.colorBlendOp = bop(k->beq); ba.srcAlphaBlendFactor = bf(k->bsa); ba.dstAlphaBlendFactor = bf(k->bda); ba.alphaBlendOp = bop(k->beq);
    ba.colorWriteMask = k->cmask;
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount = 1; cb.pAttachments = &ba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
                            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
                            VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
                            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dy.dynamicStateCount = 12; dy.pDynamicStates = dyn;
    VkFormat cf = (VkFormat)k->cfmt;
    VkPipelineRenderingCreateInfo ri = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &cf; ri.depthAttachmentFormat = (VkFormat)k->dfmt;
    if (k->dfmt == VK_FORMAT_D32_SFLOAT_S8_UINT || k->dfmt == VK_FORMAT_D24_UNORM_S8_UINT) ri.stencilAttachmentFormat = (VkFormat)k->dfmt;
    VkGraphicsPipelineCreateInfo pi = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; pi.pNext = &ri;
    pi.stageCount = 2; pi.pStages = st; pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia; pi.pViewportState = &vp;
    pi.pRasterizationState = &rs; pi.pMultisampleState = &ms; pi.pDepthStencilState = &ds; pi.pColorBlendState = &cb;
    pi.pDynamicState = &dy; pi.layout = V.pl;
    VkPipeline p = 0; uint64_t tp = TSC(); VkResult r = vkCreateGraphicsPipelines_(V.dev, g_pcache, 1, &pi, 0, &p); g_tm[6] += TSC() - tp;
    if (r) { vk_err("vkCreateGraphicsPipelines", r); return 0; }
    g_pipe_new++; g_pipe_dirty = 1;
    if (vg_npipes < 1024) {
        vg_pipes[vg_npipes].k = *k; vg_pipes[vg_npipes].p = p; vg_npipes++;
        for (int j = 0; j < 4096; j++) if (!vg_phash[(h + j) & 4095]) { vg_phash[(h + j) & 4095] = (short)vg_npipes; break; }
    }
    return p;
}

/* ---- samplers: one per (filters, wrap, anisotropy) */
typedef struct { GLenum minf, magf, ws, wt; float an; VkSampler s; } SEnt;
static SEnt vg_samp[64]; static int vg_nsamp;
static VkSampler get_sampler(const VGTex *t) {
    for (int i = 0; i < vg_nsamp; i++) { SEnt *e = &vg_samp[i]; if (e->minf == t->minf && e->magf == t->magf && e->ws == t->ws && e->wt == t->wt && e->an == t->aniso) return e->s; }
    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = t->magf == GL_NEAREST ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    int mip = t->minf >= 0x2700 && t->minf <= 0x2703;            /* *_MIPMAP_* */
    si.minFilter = (t->minf == GL_NEAREST || t->minf == 0x2700 || t->minf == 0x2702) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    si.mipmapMode = (t->minf == 0x2702 || t->minf == 0x2703) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = t->ws == GL_CLAMP_TO_EDGE ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV = t->wt == GL_CLAMP_TO_EDGE ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.anisotropyEnable = (mip && t->aniso > 1.f && V.aniso_ok) ? VK_TRUE : VK_FALSE; si.maxAnisotropy = t->aniso > 1.f ? t->aniso : 1.f;
    si.minLod = 0; si.maxLod = mip ? VK_LOD_CLAMP_NONE : 0.25f;
    VkSampler s = 0; vkCreateSampler_(V.dev, &si, 0, &s);
    if (vg_nsamp < 64) { SEnt *e = &vg_samp[vg_nsamp++]; e->minf = t->minf; e->magf = t->magf; e->ws = t->ws; e->wt = t->wt; e->an = t->aniso; e->s = s; }
    return s;
}

/* ------------------------------------------------------------------ texture bindings: descriptor sets from pools of
 * this frame (reset when the frame comes round again) */
static VkDescriptorSet alloc_set(void) {
    VFrame *F = &V.fr[V.f];
    for (;;) {
        if (F->curdp >= F->ndp) {
            if (F->ndp >= 16) { vk_err("too many texture bindings", 0); return 0; }
            VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 24576};
            VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pi.maxSets = 8192; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
            if (vkCreateDescriptorPool_(V.dev, &pi, 0, &F->dp[F->ndp])) return 0;
            F->ndp++;
        }
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; ai.descriptorPool = F->dp[F->curdp];
        ai.descriptorSetCount = 1; ai.pSetLayouts = &V.dsl;
        VkDescriptorSet set = 0;
        if (vkAllocateDescriptorSets_(V.dev, &ai, &set) == VK_SUCCESS) return set;
        F->curdp++;
    }
}
/* ---- texture bindings kept from picture to picture: one descriptor set per (textures, samplers) combination, from
 * long-lived pools. Building a new set for every texture change cost most of the drawing time. When a picture
 * (image view) is destroyed, its sets are forgotten; when the pools are full everything is dropped and rebuilt. */
#define DC_SIZE 16384
typedef struct { VkImageView v[3]; VkSampler s[3]; VkDescriptorSet set; } DCEnt;
static DCEnt g_dc[DC_SIZE]; static int g_dc_n;
static VkDescriptorPool g_dcpool[8]; static int g_dcnpool, g_dccur, g_dc_full;
static void dev_idle(void);
static void dc_restart(void) {   /* (called between pictures) */
    dev_idle();
    for (int i = 0; i < g_dcnpool; i++) vkResetDescriptorPool_(V.dev, g_dcpool[i], 0);
    memset(g_dc, 0, sizeof g_dc); g_dc_n = 0; g_dccur = 0; g_dc_full = 0;
    for (int i = 0; i < VK_FRAMES; i++) V.fr[i].lastset = 0;
}
static uint32_t dc_hash(const VkImageView *v, const VkSampler *sm) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 3; i++) { h = (h ^ (uint64_t)(uintptr_t)v[i]) * 1099511628211ull; h = (h ^ (uint64_t)(uintptr_t)sm[i]) * 1099511628211ull; }
    return (uint32_t)(h ^ (h >> 29));
}
static void dcache_forget(VkImageView v) {
    if (!v || !g_dc_n) return;
    for (int i = 0; i < DC_SIZE; i++) if (g_dc[i].set && (g_dc[i].v[0] == v || g_dc[i].v[1] == v || g_dc[i].v[2] == v)) g_dc[i].v[0] = (VkImageView)(uintptr_t)~(uintptr_t)0;  /* (never matches) */
}
static VkDescriptorSet dc_alloc(void) {
    for (;;) {
        if (g_dccur >= g_dcnpool) {
            if (g_dcnpool >= 8) { g_dc_full = 1; return 0; }  /* full: start over at the next picture */
            VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * 4096};
            VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pi.maxSets = 4096; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
            if (vkCreateDescriptorPool_(V.dev, &pi, 0, &g_dcpool[g_dcnpool])) return 0;
            g_dcnpool++;
        }
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; ai.descriptorPool = g_dcpool[g_dccur];
        ai.descriptorSetCount = 1; ai.pSetLayouts = &V.dsl;
        VkDescriptorSet set = 0;
        if (vkAllocateDescriptorSets_(V.dev, &ai, &set) == VK_SUCCESS) return set;
        g_dccur++;
    }
}
static VkDescriptorSet dc_get(const VkDescriptorImageInfo *ii) {
    VkImageView v[3] = {ii[0].imageView, ii[1].imageView, ii[2].imageView}; VkSampler sm[3] = {ii[0].sampler, ii[1].sampler, ii[2].sampler};
    uint32_t h = dc_hash(v, sm) & (DC_SIZE - 1);
    for (int k = 0; k < 64; k++) {
        DCEnt *e = &g_dc[(h + k) & (DC_SIZE - 1)];
        if (!e->set) break;
        if (!memcmp(e->v, v, sizeof v) && !memcmp(e->s, sm, sizeof sm)) return e->set;
    }
    if (g_dc_n > DC_SIZE * 3 / 4) g_dc_full = 1;
    VkDescriptorSet set = g_dc_full ? 0 : dc_alloc();
    int keep = set != 0;
    if (!set) { set = alloc_set(); if (!set) return 0; }       /* (this picture only) */
    VkWriteDescriptorSet w[3]; memset(w, 0, sizeof w);
    for (int u = 0; u < 3; u++) { w[u].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[u].dstSet = set; w[u].dstBinding = (uint32_t)u; w[u].descriptorCount = 1;
                                  w[u].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[u].pImageInfo = &ii[u]; }
    vkUpdateDescriptorSets_(V.dev, 3, w, 0, 0);
    if (keep) for (int k = 0; k < 64; k++) {
        DCEnt *e = &g_dc[(h + k) & (DC_SIZE - 1)];
        if (!e->set || e->v[0] == (VkImageView)(uintptr_t)~(uintptr_t)0) { memcpy(e->v, v, sizeof v); memcpy(e->s, sm, sizeof sm); if (!e->set) g_dc_n++; e->set = set; return set; }
    }
    return set;                                                /* (no room near: used once, not kept) */
}
/* ------------------------------------------------------------------ frames, rendering */
static VkCommandBuffer CB(void) { return V.fr[V.f].cb; }
static void begin_cbs(void) {
    VFrame *F = &V.fr[V.f]; VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer_(F->up, 0); vkResetCommandBuffer_(F->cb, 0);
    vkBeginCommandBuffer_(F->up, &bi); vkBeginCommandBuffer_(F->cb, &bi);
    full_barrier(F->cb);
}
static void end_rendering(void) { if (V.rendering) { vkCmdEndRendering_(CB()); V.rendering = 0; V.r_pipe = 0; full_barrier(CB()); } }
static void acquire_window(void);
/* the colour / depth images of a framebuffer (0 = the window) */
static VImg *fb_color(GLuint fb) {
    if (fb == 0) return 0;
    VGFb *f = &vg_fb[fb]; if (!f->used || !f->color) return 0;
    return f->ctype == 1 ? vg_tex[f->color].im : vg_rb[f->color].im;
}
static VImg *fb_depth(GLuint fb) { if (fb == 0) return 0; VGFb *f = &vg_fb[fb]; return f->depth ? vg_rb[f->depth].im : 0; }
static int begin_rendering(void) {
    GLuint fb = S.dfb;
    if (V.rendering && V.rfb == fb) return 1;
    end_rendering();
    VkRenderingAttachmentInfo ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}, da = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO}; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &ca;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE; ca.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    if (fb == 0) {
        if (V.headless) { ca.imageView = V.head->view; V.r_cfmt = V.head->fmt; V.r_w = V.head->w; V.r_h = V.head->h; }
        else {
            acquire_window(); if (!V.acquired) return 0;
            ca.imageView = V.scview[V.scidx]; V.r_cfmt = V.scfmt; V.r_w = (int)V.scext.width; V.r_h = (int)V.scext.height;
        }
        V.r_dfmt = VK_FORMAT_UNDEFINED; V.r_samples = 1;
    } else {
        VImg *c = fb_color(fb), *d = fb_depth(fb); if (!c) return 0;
        ca.imageView = c->view; V.r_cfmt = c->fmt; V.r_samples = c->samples; V.r_w = c->w; V.r_h = c->h;
        if (d) { da.imageView = d->view; da.imageLayout = VK_IMAGE_LAYOUT_GENERAL; da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; da.storeOp = VK_ATTACHMENT_STORE_OP_STORE; ri.pDepthAttachment = &da; V.r_dfmt = d->fmt;
                 if (d->depth == 2) ri.pStencilAttachment = &da; }   /* (the same image: depth + stencil) */
        else V.r_dfmt = VK_FORMAT_UNDEFINED;
    }
    ri.renderArea.extent.width = (uint32_t)V.r_w; ri.renderArea.extent.height = (uint32_t)V.r_h;
    vkCmdBeginRendering_(CB(), &ri); V.rendering = 1; V.rfb = fb; V.r_pipe = 0;
    return 1;
}
/* OpenGL window coordinates (from the bottom) -> Vulkan rows: the same for our images; flipped for the window */
static VkRect2D gl_rect(int x, int y, int w, int h) {
    VkRect2D r; if (V.rfb == 0 && !V.headless) y = V.r_h - (y + h);
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > V.r_w) w = V.r_w - x; if (y + h > V.r_h) h = V.r_h - y;
    if (w < 0) w = 0; if (h < 0) h = 0;
    r.offset.x = x; r.offset.y = y; r.extent.width = (uint32_t)w; r.extent.height = (uint32_t)h; return r;
}
static VkStencilOp sop(GLenum o) {
    switch (o) { case 0: return VK_STENCIL_OP_ZERO; case 0x1E01: return VK_STENCIL_OP_REPLACE; case 0x1E02: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
                 case 0x1E03: return VK_STENCIL_OP_DECREMENT_AND_CLAMP; case 0x150A: return VK_STENCIL_OP_INVERT; default: return VK_STENCIL_OP_KEEP; }
}
static void set_dynamic(void) {
    VkCommandBuffer cb = CB();
    VkViewport vp; vp.x = (float)S.vp[0]; vp.width = (float)S.vp[2]; vp.minDepth = 0.f; vp.maxDepth = 1.f;
    if (V.rfb == 0 && !V.headless) { vp.y = (float)(V.r_h - S.vp[1]); vp.height = -(float)S.vp[3]; }   /* window: flipped */
    else { vp.y = (float)S.vp[1]; vp.height = (float)S.vp[3]; }
    vkCmdSetViewport_(cb, 0, 1, &vp);
    VkRect2D sc = S.scissor ? gl_rect(S.sc[0], S.sc[1], S.sc[2], S.sc[3]) : gl_rect(0, 0, V.r_w, V.r_h);
    vkCmdSetScissor_(cb, 0, 1, &sc);
    vkCmdSetBlendConstants_(cb, S.bcol);
    int dz = S.depth && V.r_dfmt != VK_FORMAT_UNDEFINED;
    vkCmdSetDepthTestEnable_(cb, dz ? VK_TRUE : VK_FALSE); vkCmdSetDepthWriteEnable_(cb, (dz && S.dmask) ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp_(cb, (VkCompareOp)(S.dfunc >= 0x200 && S.dfunc <= 0x207 ? S.dfunc - 0x200 : 7));
    int hs = V.r_dfmt == VK_FORMAT_D32_SFLOAT_S8_UINT || V.r_dfmt == VK_FORMAT_D24_UNORM_S8_UINT, ss = S.stencil && hs;
    vkCmdSetStencilTestEnable_(cb, ss ? VK_TRUE : VK_FALSE);
    vkCmdSetStencilOp_(cb, VK_STENCIL_FACE_FRONT_AND_BACK, sop(S.sfail), sop(S.szpass), sop(S.szfail),
                       (VkCompareOp)(S.sfunc >= 0x200 && S.sfunc <= 0x207 ? S.sfunc - 0x200 : 7));
    vkCmdSetStencilCompareMask_(cb, VK_STENCIL_FACE_FRONT_AND_BACK, (uint32_t)S.smask & 0xFF);
    vkCmdSetStencilWriteMask_(cb, VK_STENCIL_FACE_FRONT_AND_BACK, (uint32_t)S.swmask & 0xFF);
    vkCmdSetStencilReference_(cb, VK_STENCIL_FACE_FRONT_AND_BACK, (uint32_t)S.sref & 0xFF);
}

/* ------------------------------------------------------------------ the window */
/* Held pictures: play.py's pacer has a picture drawn well before the refresh it is for (the drawing thread may be busy
 * with the game's own drawing when the moment comes), then hands it to the screen from its own thread just before that
 * refresh (vk_present_held). The queue and the swapchain are then used from two threads: a small lock around them */
static volatile int g_qlock;

static void qlock_(int who) { uint64_t t = TSC(); while (__atomic_exchange_n(&g_qlock, 1, __ATOMIC_ACQUIRE)) __builtin_ia32_pause(); g_tm[who] += TSC() - t; }
static void qlock(void) { qlock_(1); }
static void qunlock(void) { __atomic_store_n(&g_qlock, 0, __ATOMIC_RELEASE); }
static void dev_idle(void) { qlock(); vkDeviceWaitIdle_(V.dev); qunlock(); }
static int g_held = -1; static VkSwapchainKHR g_held_sc; static uint32_t g_held_seq, g_auto_seq, g_hold_seq; static int g_hold_next;
static int g_npres_late, g_acq_block;                                  /* (log) held pictures that weren't drawn in time */
/* (perf_log) when each picture really reached the screen: VK_KHR_present_wait. Present ids count up; a tracker thread
 * (vk_present_track) waits for them in turn */
static PFN_vkWaitForPresentKHR vkWaitForPresentKHR_; static int g_pw_ok; static volatile uint64_t g_pid, g_pid_done; static volatile int g_pw_busy;
typedef struct { uint64_t id, tsc; uint32_t seq; VkSwapchainKHR sc; } PRec; static PRec g_prec[1024];
static void present_img_(VkSwapchainKHR sc, uint32_t idx, uint32_t seq);
static void present_img(VkSwapchainKHR sc, uint32_t idx) { present_img_(sc, idx, g_hold_next ? g_hold_seq : 0); }
static void present_img_(VkSwapchainKHR sc, uint32_t idx, uint32_t seq) {     /* (with the lock) */
    VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &V.done_sem[idx];
    pi.swapchainCount = 1; pi.pSwapchains = &sc; pi.pImageIndices = &idx;
    VkPresentIdKHR pid = {VK_STRUCTURE_TYPE_PRESENT_ID_KHR}; uint64_t id = 0;
    if (g_pw_ok) { id = g_pid + 1; pid.swapchainCount = 1; pid.pPresentIds = &id; pi.pNext = &pid; g_pid = id;
                   PRec *pr = &g_prec[id & 1023]; pr->id = id; pr->tsc = TSC(); pr->seq = seq; pr->sc = sc; }
    uint64_t t0 = TSC(); VkResult r = vkQueuePresentKHR_(V.q, &pi); g_tm[4] += TSC() - t0;
    if (sc == V.sc && (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)) V.sc_ok = 0;
}
static void flush_held(void) { if (g_held >= 0) { present_img_(g_held_sc, (uint32_t)g_held, g_held_seq); g_held = -1; } }   /* (with the lock) */
static int make_swapchain(void) {
    VkSurfaceCapabilitiesKHR caps; vkGetPhysicalDeviceSurfaceCapabilitiesKHR_(V.pd, V.surf, &caps);
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFF) { ext.width = (uint32_t)(V.win_w > 0 ? V.win_w : 1280); ext.height = (uint32_t)(V.win_h > 0 ? V.win_h : 720); }
    if (ext.width == 0 || ext.height == 0) return 0;           /* (minimized) */
    VkSwapchainKHR old = V.sc;
    VkSwapchainCreateInfoKHR ci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; ci.surface = V.surf;
    ci.minImageCount = caps.minImageCount + 2 < 3 ? 3 : caps.minImageCount + 2;   /* (+2: one can be held while the next is drawn) */ if (caps.maxImageCount && ci.minImageCount > caps.maxImageCount) ci.minImageCount = caps.maxImageCount;
    ci.imageFormat = V.scfmt; ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; ci.imageExtent = ext; ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; ci.presentMode = V.pmode; ci.clipped = VK_TRUE; ci.oldSwapchain = old;
    VkResult r = vkCreateSwapchainKHR_(V.dev, &ci, 0, &V.sc); if (r) { vk_err("vkCreateSwapchainKHR", r); V.sc = 0; return 0; }
    if (old) { qlock(); flush_held(); for (int i = 0; i < 1000000 && g_pw_busy; i++) __builtin_ia32_pause(); g_pid_done = g_pid; vkDeviceWaitIdle_(V.dev); qunlock(); for (uint32_t i = 0; i < V.nsc; i++) vkDestroyImageView_(V.dev, V.scview[i], 0); vkDestroySwapchainKHR_(V.dev, old, 0); }
    V.nsc = 8; vkGetSwapchainImagesKHR_(V.dev, V.sc, &V.nsc, V.scimg); V.scext = ext;
    for (uint32_t i = 0; i < V.nsc; i++) {
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = V.scimg[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = V.scfmt;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
        vkCreateImageView_(V.dev, &vi, 0, &V.scview[i]);
    }
    V.sc_ok = 1; return 1;
}
static void acquire_window(void) {
    if (V.acquired || V.headless) return;
    for (int tries = 0; tries < 2; tries++) {
        if (!V.sc_ok && !make_swapchain()) return;
        qlock(); uint64_t ta = TSC();
        VkResult r = vkAcquireNextImageKHR_(V.dev, V.sc, 0, V.acq_sem[V.f], 0, &V.scidx);
        if (r == VK_NOT_READY || r == VK_TIMEOUT) { g_acq_block++; flush_held(); r = vkAcquireNextImageKHR_(V.dev, V.sc, 1000000000ull, V.acq_sem[V.f], 0, &V.scidx); }
        g_tm[2] += TSC() - ta; qunlock();
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
            V.acquired = 1; V.acq_wait = 1; V.acq_slot = V.f;
            img_layout(CB(), V.scimg[V.scidx], 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            return;
        }
        V.sc_ok = 0;                                         /* window size changed: make it again */
    }
}
static void free_frame_garbage(VFrame *F) {
    for (int i = 0; i < F->ndead; i++) free_image_now(F->dead[i]);
    F->ndead = 0;
    for (int i = 0; i < F->ndeadbuf; i++) { vkDestroyBuffer_(V.dev, F->deadbuf[i], 0); vkFreeMemory_(V.dev, F->deadmem[i], 0); }
    F->ndeadbuf = 0; F->ringused = 0;
    for (int i = 0; i < F->ndp; i++) vkResetDescriptorPool_(V.dev, F->dp[i], 0);
    F->curdp = 0; F->lastview = 0; F->lastview1 = 0; F->lastview2 = 0; F->lastsamp = 0; F->lastset = 0;
}
/* send what was recorded; present = show the window picture. wait = until the GPU is done (readbacks) */
static int g_nsync[4]; static uint64_t g_subseq;   /* (count of submits: readbacks without waiting) */   /* waits for the graphics card: 0 readback, 1 finish, 2 ring full */
static void submit__b(int present, int wait);
static void submit(int present, int wait) { uint64_t _t = PF_TSC(); submit__b(present, wait); pf_add(13, _t); }
static void submit__b(int present, int wait) {
    VFrame *F = &V.fr[V.f];
    end_rendering();
    int pres = present && V.acquired;
    if (pres) img_layout(F->cb, V.scimg[V.scidx], 0, 1, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    vkEndCommandBuffer_(F->up); vkEndCommandBuffer_(F->cb);
    VkCommandBuffer cbs[2] = {F->up, F->cb};
    VkPipelineStageFlags ws = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 2; si.pCommandBuffers = cbs;
    int waits = V.acq_wait && !V.headless;                  /* (the window image must be ready before drawing into it) */
    if (waits) { si.waitSemaphoreCount = 1; si.pWaitSemaphores = &V.acq_sem[V.acq_slot]; si.pWaitDstStageMask = &ws; V.acq_wait = 0; }
    if (pres) { si.signalSemaphoreCount = 1; si.pSignalSemaphores = &V.done_sem[V.scidx]; }
    vkResetFences_(V.dev, 1, &F->fence);
    F->seq = ++g_subseq;
    qlock(); uint64_t ts = TSC();
    VkResult r = vkQueueSubmit_(V.q, 1, &si, F->fence); if (r) vk_err("vkQueueSubmit", r);
    g_tm[3] += TSC() - ts;
    F->busy = 1;
    if (pres) {
        flush_held();                                          /* (an older held picture goes first) */
        if (g_hold_next && g_auto_seq != g_hold_seq) { g_held = (int)V.scidx; g_held_sc = V.sc; g_held_seq = g_hold_seq; }
        else { if (g_hold_next) g_npres_late++; present_img(V.sc, V.scidx); }   /* (now: not held, or its moment has passed) */
        V.acquired = 0;
    }
    qunlock(); g_hold_next = 0;
    uint64_t tf = TSC();
    if (wait) {                                            /* same frame continues: the GPU is done with it */
        vkWaitForFences_(V.dev, 1, &F->fence, VK_TRUE, ~0ull); F->busy = 0; g_tm[0] += TSC() - tf; free_frame_garbage(F);
    } else {                                               /* next frame: wait until the GPU is done with that one */
        V.f = (V.f + 1) % VK_FRAMES; F = &V.fr[V.f];
        if (F->busy) { vkWaitForFences_(V.dev, 1, &F->fence, VK_TRUE, ~0ull); F->busy = 0; } g_tm[0] += TSC() - tf;
        free_frame_garbage(F);
    }
    if (g_dc_full) dc_restart();
    if (S.vbo_ok) S.vbo_ok = 2;                              /* (new command buffers: bind the vertices again) */
    begin_cbs();
}
/* room in this frame's ring buffer (vertices, uploads); a full ring sends the frame first */
static uint32_t ring_alloc(uint32_t n, uint8_t **p) {
    VFrame *F = &V.fr[V.f]; uint32_t off = (F->ringused + 255u) & ~255u;
    if (off + n > RING_SIZE) { if (n > RING_SIZE) return 0xFFFFFFFF; g_nsync[2]++; submit(0, 1); F = &V.fr[V.f]; off = 0; }
    F->ringused = off + n; *p = F->ringp + off; return off;
}

/* ------------------------------------------------------------------ the OpenGL calls */
static void vg_GenTextures(GLsizei n, GLuint *o) {
    for (int i = 0; i < n; i++) {
        GLuint id = vg_nfreetex ? vg_freetex[--vg_nfreetex] : (vg_ntex < VG_MAX ? vg_ntex++ : 0);
        if (id) { memset(&vg_tex[id], 0, sizeof vg_tex[id]); vg_tex[id].used = 1; vg_tex[id].minf = 0x2702; vg_tex[id].magf = GL_LINEAR;
                  vg_tex[id].ws = vg_tex[id].wt = GL_REPEAT; }
        o[i] = id;
    }
}
static void vg_DeleteTextures(GLsizei n, const GLuint *ids) {
    for (int i = 0; i < n; i++) { GLuint id = ids[i]; if (!id || id >= VG_MAX || !vg_tex[id].used) continue;
        drop_image(vg_tex[id].im); vg_tex[id].im = 0; vg_tex[id].used = 0; if (vg_nfreetex < VG_MAX) vg_freetex[vg_nfreetex++] = id;
        if (S.tex == id) S.tex = 0; if (S.tex1 == id) S.tex1 = 0; if (S.tex2 == id) S.tex2 = 0; }
}
static void vg_BindTexture(GLenum t, GLuint id) { (void)t; id = id < VG_MAX ? id : 0; if (S.unit == 2) S.tex2 = id; else if (S.unit) S.tex1 = id; else S.tex = id; }
static void vg_ActiveTexture(GLenum t) { S.unit = (int)(t - GL_TEXTURE0); if (S.unit < 0 || S.unit > 2) S.unit = 0; }
static VGTex *cur_tex(void) { GLuint id = S.unit == 2 ? S.tex2 : S.unit ? S.tex1 : S.tex; return id ? &vg_tex[id] : 0; }   /* (the one texture calls change) */
static int ilog2(int v) { int l = 0; while ((1 << (l + 1)) <= v) l++; return l; }
static void vg_TexImage2D(GLenum tg, GLint lvl, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum fmt, GLenum type, const void *data) {
    (void)tg; (void)ifmt; (void)b; (void)type;
    if (lvl != 0 || !cur_tex() || w <= 0 || h <= 0) return;
    VGTex *t = cur_tex(); GLuint tid = S.unit == 2 ? S.tex2 : S.unit ? S.tex1 : S.tex; int mx = w > h ? w : h;
    int levels = (data && g_mips) ? ilog2(mx) + 1 : 1;
    if (V.rendering && data) { /* (uploads go into the commands sent before this frame's drawing: no need to stop) */ }
    drop_image(t->im);
    t->im = make_image(w, h, VK_FORMAT_R8G8B8A8_UNORM, 1, levels, 0);
    if (!t->im) return;
    if (!data) {                                              /* a new render target: clear it (like a fresh OpenGL texture) */
        VkClearColorValue cc; memset(&cc, 0, sizeof cc); VkImageSubresourceRange rr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage_(V.fr[V.f].up, t->im->img, VK_IMAGE_LAYOUT_GENERAL, &cc, 1, &rr);
        return;
    }
    uint32_t n = (uint32_t)w * (uint32_t)h * 4; uint8_t *p;
    uint32_t off = ring_alloc(n, &p); if (off == 0xFFFFFFFF) return;
    t = &vg_tex[tid];                                         /* (ring_alloc may have sent the frame: t->im is still ours) */
    if (fmt == 0x1907) {                                      /* GL_RGB rows -> RGBA */
        const uint8_t *s = (const uint8_t *)data;
        for (uint32_t i = 0; i < (uint32_t)w * (uint32_t)h; i++) { p[4 * i] = s[3 * i]; p[4 * i + 1] = s[3 * i + 1]; p[4 * i + 2] = s[3 * i + 2]; p[4 * i + 3] = 255; }
    } else memcpy(p, data, n);
    VkCommandBuffer up = V.fr[V.f].up; full_barrier(up);
    VkBufferImageCopy bc; memset(&bc, 0, sizeof bc); bc.bufferOffset = off;
    bc.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; bc.imageSubresource.layerCount = 1;
    bc.imageExtent.width = (uint32_t)w; bc.imageExtent.height = (uint32_t)h; bc.imageExtent.depth = 1;
    vkCmdCopyBufferToImage_(up, V.fr[V.f].ring, t->im->img, VK_IMAGE_LAYOUT_GENERAL, 1, &bc);
    full_barrier(up);
}
static void vg_GenerateMipmap(GLenum tg) {
    (void)tg; if (!cur_tex()) return; VImg *m = cur_tex()->im; if (!m || m->levels < 2) return;
    VkCommandBuffer up = V.fr[V.f].up; int w = m->w, h = m->h;
    for (int l = 1; l < m->levels; l++) {
        int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        VkImageBlit bl; memset(&bl, 0, sizeof bl);
        bl.srcSubresource.aspectMask = bl.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bl.srcSubresource.mipLevel = (uint32_t)(l - 1); bl.dstSubresource.mipLevel = (uint32_t)l; bl.srcSubresource.layerCount = bl.dstSubresource.layerCount = 1;
        bl.srcOffsets[1].x = w; bl.srcOffsets[1].y = h; bl.srcOffsets[1].z = 1; bl.dstOffsets[1].x = nw; bl.dstOffsets[1].y = nh; bl.dstOffsets[1].z = 1;
        vkCmdBlitImage_(up, m->img, VK_IMAGE_LAYOUT_GENERAL, m->img, VK_IMAGE_LAYOUT_GENERAL, 1, &bl, VK_FILTER_LINEAR);
        full_barrier(up); w = nw; h = nh;
    }
}
static void vg_TexParameteri(GLenum tg, GLenum p, GLint v) {
    (void)tg; VGTex *t = cur_tex(); if (!t) return;
    if (p == GL_TEXTURE_MIN_FILTER) t->minf = (GLenum)v; else if (p == GL_TEXTURE_MAG_FILTER) t->magf = (GLenum)v;
    else if (p == GL_TEXTURE_WRAP_S) t->ws = (GLenum)v; else if (p == GL_TEXTURE_WRAP_T) t->wt = (GLenum)v;
}
static void vg_TexParameterf(GLenum tg, GLenum p, GLfloat v) { (void)tg; if (cur_tex() && p == 0x84FE) cur_tex()->aniso = v; }
static void vg_GenFramebuffers(GLsizei n, GLuint *o) { for (int i = 0; i < n; i++) { o[i] = vg_nfb < 256 ? vg_nfb++ : 0; if (o[i]) memset(&vg_fb[o[i]], 0, sizeof(VGFb)), vg_fb[o[i]].used = 1; } }
static void vg_BindFramebuffer(GLenum t, GLuint id) {
    if (t == GL_FRAMEBUFFER || t == GL_DRAW_FRAMEBUFFER) S.dfb = id;
    if (t == GL_FRAMEBUFFER || t == GL_READ_FRAMEBUFFER) S.rfb = id;
}
static void vg_FramebufferTexture2D(GLenum t, GLenum att, GLenum tt, GLuint tex, GLint l) {
    (void)t; (void)tt; (void)l; VGFb *f = &vg_fb[S.dfb]; if (att == GL_COLOR_ATTACHMENT0) { f->ctype = 1; f->color = tex; }
}
static GLenum vg_CheckFramebufferStatus(GLenum t) { (void)t; return GL_FRAMEBUFFER_COMPLETE; }
static void vg_GenRenderbuffers(GLsizei n, GLuint *o) { for (int i = 0; i < n; i++) { o[i] = vg_nrb < VG_MAX ? vg_nrb++ : 0; if (o[i]) vg_rb[o[i]].used = 1; } }
static void vg_BindRenderbuffer(GLenum t, GLuint id) { (void)t; S.rb = id; }
static void vg_RenderbufferStorageMultisample(GLenum t, GLsizei samples, GLenum ifmt, GLsizei w, GLsizei h) {
    (void)t; if (!S.rb) return; int depth = ifmt == GL_DEPTH_COMPONENT24 || ifmt == 0x88F0 /* DEPTH24_STENCIL8 */;
    int st = depth && ifmt == 0x88F0 && V.stencil_ok;
    drop_image(vg_rb[S.rb].im);
    vg_rb[S.rb].im = make_image(w, h, st ? V.dsfmt : depth ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM, samples < 1 ? 1 : samples, 1, st ? 2 : depth);
}
static void vg_RenderbufferStorage(GLenum t, GLenum ifmt, GLsizei w, GLsizei h) { vg_RenderbufferStorageMultisample(t, 1, ifmt, w, h); }
static void vg_FramebufferRenderbuffer(GLenum t, GLenum att, GLenum rt, GLuint rb) {
    (void)t; (void)rt; VGFb *f = &vg_fb[S.dfb];
    if (att == GL_COLOR_ATTACHMENT0) { f->ctype = 2; f->color = rb; } else if (att == GL_DEPTH_ATTACHMENT || att == 0x821A /* DEPTH_STENCIL */) f->depth = rb;
}
static void vg_Viewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    S.vp[0] = x; S.vp[1] = y; S.vp[2] = w; S.vp[3] = h;
    if (S.dfb == 0 && x == 0 && y == 0) { V.win_w = w; V.win_h = h; }      /* (the window's size: psp_gl.inc sets it first) */
}
static void vg_Scissor(GLint x, GLint y, GLsizei w, GLsizei h) { S.sc[0] = x; S.sc[1] = y; S.sc[2] = w; S.sc[3] = h; }
static void vg_Enable(GLenum c) { if (c == GL_SCISSOR_TEST) S.scissor = 1; else if (c == GL_DEPTH_TEST) S.depth = 1; else if (c == GL_BLEND) S.blend = 1; else if (c == 0x0B90) S.stencil = 1; }
static void vg_Disable(GLenum c) { if (c == GL_SCISSOR_TEST) S.scissor = 0; else if (c == GL_DEPTH_TEST) S.depth = 0; else if (c == GL_BLEND) S.blend = 0; else if (c == 0x0B90) S.stencil = 0; }
static void vg_StencilFunc(GLenum f, GLint ref, GLuint mask) { S.sfunc = f; S.sref = ref; S.smask = (int)mask; }
static void vg_StencilOp(GLenum sf, GLenum zf, GLenum zp) { S.sfail = sf; S.szfail = zf; S.szpass = zp; }
static void vg_StencilMask(GLuint m) { S.swmask = (int)m; }
static void vg_ClearStencil(GLint v) { S.cstencil = v; }
static void vg_ClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { S.ccol[0] = r; S.ccol[1] = g; S.ccol[2] = b; S.ccol[3] = a; }
static void vg_ClearDepth(GLdouble d) { S.cdepth = d; }
static void vg_Clear(GLbitfield bits) {
    if (!begin_rendering()) return;
    VkClearAttachment at[2]; int n = 0;
    if ((bits & GL_COLOR_BUFFER_BIT) && (S.cmask[0] || S.cmask[1] || S.cmask[2] || S.cmask[3])) {
        at[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; at[n].colorAttachment = 0;
        for (int i = 0; i < 4; i++) at[n].clearValue.color.float32[i] = S.ccol[i];
        n++;
    }
    int hs = V.r_dfmt == VK_FORMAT_D32_SFLOAT_S8_UINT || V.r_dfmt == VK_FORMAT_D24_UNORM_S8_UINT;
    int cd = (bits & GL_DEPTH_BUFFER_BIT) && S.dmask && V.r_dfmt != VK_FORMAT_UNDEFINED, cs = (bits & 0x400 /* STENCIL */) && hs;
    if (cd || cs) {
        at[n].aspectMask = (cd ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) | (cs ? VK_IMAGE_ASPECT_STENCIL_BIT : 0); at[n].colorAttachment = 0;
        at[n].clearValue.depthStencil.depth = (float)S.cdepth; at[n].clearValue.depthStencil.stencil = (uint32_t)S.cstencil & 0xFF; n++;
    }
    if (!n) return;
    VkClearRect cr; cr.rect = S.scissor ? gl_rect(S.sc[0], S.sc[1], S.sc[2], S.sc[3]) : gl_rect(0, 0, V.r_w, V.r_h);
    cr.baseArrayLayer = 0; cr.layerCount = 1;
    if (cr.rect.extent.width && cr.rect.extent.height) vkCmdClearAttachments_(CB(), (uint32_t)n, at, 1, &cr);
}
static void vg_DepthFunc(GLenum f) { S.dfunc = f; }
static void vg_DepthMask(GLboolean m) { S.dmask = m; }
static void vg_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { S.cmask[0] = r; S.cmask[1] = g; S.cmask[2] = b; S.cmask[3] = a; }
static void vg_BlendFuncSeparate(GLenum s, GLenum d, GLenum sa, GLenum da) { S.bs = s; S.bd = d; S.bsa = sa; S.bda = da; }
static void vg_BlendEquation(GLenum e) { S.beq = e; }
static void vg_BlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { S.bcol[0] = r; S.bcol[1] = g; S.bcol[2] = b; S.bcol[3] = a; }
/* shaders: the sources psp_gl.inc compiles are told apart by what is in them; the SPIR-V is ready made */
static int has_text(const char *s, const char *w) { for (; *s; s++) { const char *a = s, *b = w; while (*a && *b && *a == *b) { a++; b++; } if (!*b) return 1; } return 0; }
static GLuint vg_CreateShader(GLenum t) { (void)t; return S.nshader < 63 ? (GLuint)++S.nshader : 0; }
static void vg_ShaderSource(GLuint s, GLsizei n, const GLchar *const *src, const GLint *len) { (void)n; (void)len; if (s < 64) S.src[s] = src[0]; }
static void vg_CompileShader(GLuint s) { if (s < 64 && S.src[s]) S.shader_kind[s] = has_text(S.src[s], "gl_VertexID") || has_text(S.src[s], "u_taps") ? 2 : 1; }
static void vg_GetShaderiv(GLuint s, GLenum p, GLint *o) { (void)s; (void)p; *o = 1; }
static void vg_GetShaderInfoLog(GLuint s, GLsizei n, GLsizei *l, GLchar *o) { (void)s; if (l) *l = 0; if (n > 0) o[0] = 0; }
static GLuint vg_CreateProgram(void) { return S.nprog < 15 ? (GLuint)++S.nprog : 0; }
static void vg_AttachShader(GLuint p, GLuint s) { if (p < 16 && s < 64 && S.shader_kind[s]) S.prog_kind[p] = S.shader_kind[s]; }
static void vg_LinkProgram(GLuint p) { (void)p; }
static void vg_GetProgramiv(GLuint p, GLenum q, GLint *o) { (void)p; (void)q; *o = 1; }
static void vg_GetProgramInfoLog(GLuint p, GLsizei n, GLsizei *l, GLchar *o) { (void)p; if (l) *l = 0; if (n > 0) o[0] = 0; }
static void vg_UseProgram(GLuint p) { S.prog = p; }
/* uniform locations: program kind * 1000 + byte offset in its push constants (+1); -1 = none (the sampler) */
static GLint vg_GetUniformLocation(GLuint p, const GLchar *n) {
    int k = p < 16 ? S.prog_kind[p] : 0;
    static const char *const MAIN[] = {"u_fbsize", "u_texon", "u_tfunc", "u_ta", "u_atest", "u_aref", "u_amask", "u_dbl", "u_fogon", "u_env", "u_fogcol",
                                       "u_lmode", "u_lk", "u_lcam", "u_aox", "u_aoy", "u_aos"};
    static const int MOFF[] = {0, 8, 12, 16, 20, 24, 28, 32, 36, 48, 64, 80, 96, 112, 84, 88, 92};
    static const char *const PRES[] = {"u_rect", "u_step", "u_taps", "u_ovl", "u_grade"};
    static const int POFF[] = {0, 16, 24, 28, 32};
    for (int i = 0; k == 1 && i < 17; i++) if (has_text(n, MAIN[i]) && has_text(MAIN[i], n)) return 1000 + MOFF[i] + 1;
    for (int i = 0; k == 2 && i < 5; i++) if (has_text(n, PRES[i]) && has_text(PRES[i], n)) return 2000 + POFF[i] + 1;
    return -1;
}
static uint8_t *uni(GLint loc, int bytes) {
    if (loc < 1000) return 0; int k = loc / 1000, off = loc % 1000 - 1;
    if (k < 1 || k > 2 || off < 0 || off + bytes > 128) return 0;
    return S.pc[k - 1] + off;
}
static void vg_Uniform1i(GLint l, GLint v) { uint8_t *p = uni(l, 4); if (p) memcpy(p, &v, 4); }
static void vg_Uniform1f(GLint l, GLfloat v) { uint8_t *p = uni(l, 4); if (p) memcpy(p, &v, 4); }
static void vg_Uniform2f(GLint l, GLfloat a, GLfloat b) { uint8_t *p = uni(l, 8); if (p) { memcpy(p, &a, 4); memcpy(p + 4, &b, 4); } }
static void vg_Uniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c) { uint8_t *p = uni(l, 12); if (p) { memcpy(p, &a, 4); memcpy(p + 4, &b, 4); memcpy(p + 8, &c, 4); } }
static void vg_Uniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { uint8_t *p = uni(l, 16); if (p) { memcpy(p, &a, 4); memcpy(p + 4, &b, 4); memcpy(p + 8, &c, 4); memcpy(p + 12, &d, 4); } }
static void vg_BindAttribLocation(GLuint p, GLuint i, const GLchar *n) { (void)p; (void)i; (void)n; }
static void vg_GenVertexArrays(GLsizei n, GLuint *o) { for (int i = 0; i < n; i++) o[i] = 1; }
static void vg_BindVertexArray(GLuint a) { (void)a; }
static void vg_GenBuffers(GLsizei n, GLuint *o) { for (int i = 0; i < n; i++) o[i] = 1; }
static void vg_BindBuffer(GLenum t, GLuint b) { (void)t; (void)b; }
static void vg_BufferData(GLenum t, GLsizeiptr n, const void *d, GLenum u) {
    (void)t; (void)u; uint8_t *p; uint32_t off = ring_alloc((uint32_t)n, &p);
    if (off == 0xFFFFFFFF) { S.vbo_ok = 0; return; }
    if (d) memcpy(p, d, (size_t)n);
    S.vbo_off = off; S.vbo_ok = 1;
    if (V.rendering) { VkDeviceSize o = off; vkCmdBindVertexBuffers_(CB(), 0, 1, &V.fr[V.f].ring, &o); }
    else S.vbo_ok = 2;                                       /* (bound once rendering starts) */
}
static void vg_VertexAttribPointer(GLuint i, GLint n, GLenum t, GLboolean nm, GLsizei s, const void *p) { (void)i; (void)n; (void)t; (void)nm; (void)s; (void)p; }
static void vg_EnableVertexAttribArray(GLuint i) { (void)i; }
static void vg_DrawArrays(GLenum mode, GLint first, GLsizei count) {
    int kind = S.prog < 16 ? S.prog_kind[S.prog] : 0;
    if (count <= 0 || !kind || (kind == 1 && !S.vbo_ok)) return;
    int fresh = !V.rendering || V.rfb != S.dfb;
    if (!begin_rendering()) return;
    VkCommandBuffer cb = CB();
    if (kind == 1 && (fresh || S.vbo_ok == 2)) { VkDeviceSize o = S.vbo_off; vkCmdBindVertexBuffers_(cb, 0, 1, &V.fr[V.f].ring, &o); S.vbo_ok = 1; }
    PKey k; memset(&k, 0, sizeof k);
    k.prog = (uint32_t)kind; k.cfmt = (uint32_t)V.r_cfmt; k.dfmt = (uint32_t)V.r_dfmt; k.samples = (uint32_t)V.r_samples;
    k.blend = (uint32_t)S.blend; if (S.blend) { k.bs = S.bs; k.bd = S.bd; k.bsa = S.bsa; k.bda = S.bda; k.beq = S.beq; }
    k.cmask = (S.cmask[0] ? 1u : 0) | (S.cmask[1] ? 2u : 0) | (S.cmask[2] ? 4u : 0) | (S.cmask[3] ? 8u : 0);
    VkPipeline p = get_pipe(&k); if (!p) return;
    if (p != V.r_pipe) { vkCmdBindPipeline_(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p); V.r_pipe = p; }
    set_dynamic();
    vkCmdSetPrimitiveTopology_(cb, mode == 0x0005 ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    VkDescriptorImageInfo ii[3];
    for (int u = 0; u < 3; u++) {                             /* texture unit 0, 1 (the shadow picture), 2 (distances, for AO) */
        GLuint id = u == 2 ? S.tex2 : u ? S.tex1 : S.tex; VGTex *t = (id && vg_tex[id].im && (u == 0 || kind == 1)) ? &vg_tex[id] : 0;
        ii[u].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        if (t) { ii[u].imageView = t->im->view; ii[u].sampler = get_sampler(t); }
        else { static VGTex dt = {0, GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_REPEAT, 0.f, 1}; ii[u].imageView = V.dummy->view; ii[u].sampler = get_sampler(&dt); }
    }
    VFrame *F = &V.fr[V.f];
    if (ii[0].imageView != F->lastview || ii[0].sampler != F->lastsamp || ii[1].imageView != F->lastview1 || ii[2].imageView != F->lastview2 || !F->lastset) {
        VkDescriptorSet set = dc_get(ii); if (!set) return;
        F->lastview = ii[0].imageView; F->lastsamp = ii[0].sampler; F->lastview1 = ii[1].imageView; F->lastview2 = ii[2].imageView; F->lastset = set;
        vkCmdBindDescriptorSets_(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, V.pl, 0, 1, &set, 0, 0);
    } else if (fresh) vkCmdBindDescriptorSets_(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, V.pl, 0, 1, &F->lastset, 0, 0);
    vkCmdPushConstants_(cb, V.pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128, S.pc[kind - 1]);
    vkCmdDraw_(cb, (uint32_t)count, 1, (uint32_t)first, 0);
}
/* copies: MSAA resolve, readbacks */
static void vg_BlitFramebuffer(GLint a, GLint b, GLint c, GLint d, GLint e, GLint f, GLint g, GLint h, GLbitfield mask, GLenum filt) {
    (void)a; (void)b; (void)e; (void)f; (void)g; (void)h; (void)mask; (void)filt;
    end_rendering();
    VImg *src = fb_color(S.rfb), *dst = fb_color(S.dfb); if (!src || !dst) return;
    VkCommandBuffer cb = CB();
    if (src->samples > 1 && dst->samples == 1) {
        VkImageResolve r; memset(&r, 0, sizeof r);
        r.srcSubresource.aspectMask = r.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; r.srcSubresource.layerCount = r.dstSubresource.layerCount = 1;
        r.extent.width = (uint32_t)(c < dst->w ? c : dst->w); r.extent.height = (uint32_t)(d < dst->h ? d : dst->h); r.extent.depth = 1;
        vkCmdResolveImage_(cb, src->img, VK_IMAGE_LAYOUT_GENERAL, dst->img, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
    } else {
        VkImageCopy r; memset(&r, 0, sizeof r);
        r.srcSubresource.aspectMask = r.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; r.srcSubresource.layerCount = r.dstSubresource.layerCount = 1;
        r.extent.width = (uint32_t)(c < dst->w ? c : dst->w); r.extent.height = (uint32_t)(d < dst->h ? d : dst->h); r.extent.depth = 1;
        vkCmdCopyImage_(cb, src->img, VK_IMAGE_LAYOUT_GENERAL, dst->img, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
    }
    full_barrier(cb);
}
static void vg_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void *out) {
    (void)fmt; (void)type; if (w <= 0 || h <= 0) return;
    end_rendering();
    VkImage img; int bgr = 0, flip = 0, H;
    if (S.rfb == 0) {
        if (V.headless) { img = V.head->img; H = V.head->h; }
        else { if (!V.acquired) return; img = V.scimg[V.scidx]; H = (int)V.scext.height; flip = 1; bgr = V.scfmt == VK_FORMAT_B8G8R8A8_UNORM || V.scfmt == VK_FORMAT_B8G8R8A8_SRGB; }
    } else { VImg *c = fb_color(S.rfb); if (!c || c->samples > 1) return; img = c->img; H = c->h; }
    uint32_t need = (uint32_t)w * (uint32_t)h * 4;
    if (need > V.rbsize) {
        if (V.rb) { dev_idle(); vkDestroyBuffer_(V.dev, V.rb, 0); vkFreeMemory_(V.dev, V.rbmem, 0); V.rb = 0; }
        if (!make_buffer(need, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &V.rb, &V.rbmem, &V.rbp)) { V.rb = 0; V.rbsize = 0; return; }
        V.rbsize = need;
    }
    VkBufferImageCopy bc; memset(&bc, 0, sizeof bc);
    bc.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; bc.imageSubresource.layerCount = 1;
    bc.imageOffset.x = x; bc.imageOffset.y = flip ? H - (y + h) : y; bc.imageExtent.width = (uint32_t)w; bc.imageExtent.height = (uint32_t)h; bc.imageExtent.depth = 1;
    VkCommandBuffer cb = CB(); full_barrier(cb);
    vkCmdCopyImageToBuffer_(cb, img, VK_IMAGE_LAYOUT_GENERAL, V.rb, 1, &bc);
    full_barrier(cb);
    g_nsync[0]++; submit(0, 1);                                /* (the window picture stays: it is shown at the swap) */
    uint8_t *o = (uint8_t *)out;
    for (int r = 0; r < h; r++) {
        const uint8_t *s = V.rbp + (size_t)(flip ? h - 1 - r : r) * (size_t)w * 4; uint8_t *d = o + (size_t)r * (size_t)w * 4;
        if (bgr) for (int i = 0; i < w; i++) { d[4 * i] = s[4 * i + 2]; d[4 * i + 1] = s[4 * i + 1]; d[4 * i + 2] = s[4 * i]; d[4 * i + 3] = s[4 * i + 3]; }
        else memcpy(d, s, (size_t)w * 4);
    }
}

/* readbacks without waiting: the copy is recorded into this frame's commands and picked up once the graphics card has
 * done that frame (a frame later) - no stall while a busy frame (an explosion) is still being drawn. */
#define VRB_N 4
static struct { VkBuffer b; VkDeviceMemory m; uint8_t *p; uint32_t size; uint64_t sn; int fi; } g_vrb[VRB_N];
static int vk_rb_start(int slot, GLuint fbo, int x, int y, int w, int h) {
    if (slot < 0 || slot >= VRB_N || w <= 0 || h <= 0) return 0;
    VImg *c = fb_color(fbo); if (!c || c->samples > 1) return 0;
    uint32_t need = (uint32_t)w * (uint32_t)h * 4;
    if (need > g_vrb[slot].size) {
        if (g_vrb[slot].b) { dev_idle(); vkDestroyBuffer_(V.dev, g_vrb[slot].b, 0); vkFreeMemory_(V.dev, g_vrb[slot].m, 0); g_vrb[slot].b = 0; }
        g_vrb[slot].size = 0;
        if (!make_buffer(need, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &g_vrb[slot].b, &g_vrb[slot].m, &g_vrb[slot].p)) { g_vrb[slot].b = 0; return 0; }
        g_vrb[slot].size = need;
    }
    end_rendering();
    VkBufferImageCopy bc; memset(&bc, 0, sizeof bc);
    bc.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; bc.imageSubresource.layerCount = 1;
    bc.imageOffset.x = x; bc.imageOffset.y = y; bc.imageExtent.width = (uint32_t)w; bc.imageExtent.height = (uint32_t)h; bc.imageExtent.depth = 1;
    VkCommandBuffer cb = CB(); full_barrier(cb);
    vkCmdCopyImageToBuffer_(cb, c->img, VK_IMAGE_LAYOUT_GENERAL, g_vrb[slot].b, 1, &bc);
    full_barrier(cb);
    g_vrb[slot].sn = g_subseq + 1; g_vrb[slot].fi = V.f;
    return 1;
}
static const uint8_t *vk_rb_done(int slot) {          /* the copy's pixels once the graphics card has done it, else 0 */
    if (slot < 0 || slot >= VRB_N || !g_vrb[slot].b || !g_vrb[slot].sn) return 0;
    if (g_subseq < g_vrb[slot].sn) return 0;          /* (not sent yet) */
    VFrame *F = &V.fr[g_vrb[slot].fi];
    if (F->seq == g_vrb[slot].sn && F->busy && vkGetFenceStatus_(V.dev, F->fence) != VK_SUCCESS) return 0;
    return g_vrb[slot].p;
}
static void vg_PixelStorei(GLenum p, GLint v) { (void)p; (void)v; }
static GLenum vg_GetError(void) { GLenum e = S.err; S.err = 0; return e; }
static void vg_Finish(void) { g_nsync[1]++; submit(0, 1); }
static void vg_Flush(void) { }
static void vg_GetIntegerv(GLenum p, GLint *o) {
    if (p == 0x8D57) { VkSampleCountFlags f = V.props.limits.framebufferColorSampleCounts & V.props.limits.framebufferDepthSampleCounts;
                       *o = (f & 8) ? 8 : (f & 4) ? 4 : (f & 2) ? 2 : 1; } else *o = 0;
}
static void vg_GetFloatv(GLenum p, GLfloat *o) { *o = (p == 0x84FF && V.aniso_ok) ? V.props.limits.maxSamplerAnisotropy : 0.f; }

static void *vkgl_getproc(const char *name) {
#define G_(n) if (has_text(name, "gl" #n) && has_text("gl" #n, name)) return (void *)vg_##n;
    G_(GenTextures) G_(DeleteTextures) G_(BindTexture) G_(TexImage2D) G_(TexParameteri) G_(TexParameterf) G_(ActiveTexture)
    G_(GenFramebuffers) G_(BindFramebuffer) G_(FramebufferTexture2D) G_(CheckFramebufferStatus) G_(GenRenderbuffers)
    G_(BindRenderbuffer) G_(RenderbufferStorage) G_(RenderbufferStorageMultisample) G_(FramebufferRenderbuffer)
    G_(Viewport) G_(Scissor) G_(Enable) G_(Disable) G_(ClearColor) G_(ClearDepth) G_(Clear) G_(DepthFunc) G_(DepthMask)
    G_(ColorMask) G_(BlendFuncSeparate) G_(BlendEquation) G_(BlendColor) G_(CreateShader) G_(ShaderSource) G_(CompileShader)
    G_(GetShaderiv) G_(GetShaderInfoLog) G_(CreateProgram) G_(AttachShader) G_(LinkProgram) G_(GetProgramiv)
    G_(GetProgramInfoLog) G_(UseProgram) G_(GetUniformLocation) G_(Uniform1i) G_(Uniform1f) G_(Uniform2f) G_(Uniform3f) G_(Uniform4f)
    G_(BindAttribLocation) G_(GenVertexArrays) G_(BindVertexArray) G_(GenBuffers) G_(BindBuffer) G_(BufferData)
    G_(VertexAttribPointer) G_(EnableVertexAttribArray) G_(DrawArrays) G_(ReadPixels) G_(PixelStorei) G_(BlitFramebuffer)
    G_(GetError) G_(Finish) G_(Flush) G_(GenerateMipmap) G_(GetIntegerv) G_(GetFloatv) G_(StencilFunc) G_(StencilOp) G_(StencilMask) G_(ClearStencil)
#undef G_
    return 0;
}

/* ------------------------------------------------------------------ setup */
static int load_vk_instance_fns(void) {
#define I_(n) if (!(*(void **)&n##_ = (void *)vkGetInstanceProcAddr_(V.inst, #n))) { vk_err("missing " #n, 0); return 0; }
    I_(vkEnumeratePhysicalDevices) I_(vkGetPhysicalDeviceProperties) I_(vkGetPhysicalDeviceFeatures) I_(vkGetPhysicalDeviceQueueFamilyProperties)
    I_(vkGetPhysicalDeviceMemoryProperties) I_(vkCreateDevice) I_(vkEnumerateDeviceExtensionProperties) I_(vkGetDeviceProcAddr)
    I_(vkGetPhysicalDeviceFormatProperties) I_(vkDestroyInstance)
#undef I_
    *(void **)&vkGetPhysicalDeviceSurfaceSupportKHR_ = (void *)vkGetInstanceProcAddr_(V.inst, "vkGetPhysicalDeviceSurfaceSupportKHR");
    *(void **)&vkGetPhysicalDeviceSurfaceCapabilitiesKHR_ = (void *)vkGetInstanceProcAddr_(V.inst, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    *(void **)&vkGetPhysicalDeviceSurfaceFormatsKHR_ = (void *)vkGetInstanceProcAddr_(V.inst, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    *(void **)&vkGetPhysicalDeviceSurfacePresentModesKHR_ = (void *)vkGetInstanceProcAddr_(V.inst, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    *(void **)&vkDestroySurfaceKHR_ = (void *)vkGetInstanceProcAddr_(V.inst, "vkDestroySurfaceKHR");
    *(void **)&vkCreateWin32SurfaceKHR_ = (void *)vkGetInstanceProcAddr_(V.inst, "vkCreateWin32SurfaceKHR");
    return 1;
}
static int load_vk_device_fns(void) {
#define D_(n) if (!(*(void **)&n##_ = (void *)vkGetDeviceProcAddr_(V.dev, #n))) { vk_err("missing " #n, 0); return 0; }
    D_(vkGetDeviceQueue) D_(vkCreateCommandPool) D_(vkAllocateCommandBuffers) D_(vkBeginCommandBuffer) D_(vkEndCommandBuffer)
    D_(vkResetCommandBuffer) D_(vkQueueSubmit) D_(vkQueueWaitIdle) D_(vkDeviceWaitIdle) D_(vkCreateFence) D_(vkWaitForFences) D_(vkGetFenceStatus) D_(vkResetFences)
    D_(vkCreateSemaphore) D_(vkCreateImage) D_(vkDestroyImage) D_(vkGetImageMemoryRequirements) D_(vkAllocateMemory) D_(vkFreeMemory)
    D_(vkBindImageMemory) D_(vkCreateImageView) D_(vkDestroyImageView) D_(vkCreateBuffer) D_(vkDestroyBuffer)
    D_(vkGetBufferMemoryRequirements) D_(vkBindBufferMemory) D_(vkMapMemory) D_(vkCreateSampler) D_(vkCreateShaderModule)
    D_(vkCreateDescriptorSetLayout) D_(vkCreatePipelineLayout) D_(vkCreateGraphicsPipelines) D_(vkCreatePipelineCache) D_(vkGetPipelineCacheData) D_(vkDestroyPipelineCache)
    D_(vkCmdPipelineBarrier) D_(vkCmdCopyBufferToImage) D_(vkCmdCopyImageToBuffer) D_(vkCmdBlitImage) D_(vkCmdResolveImage)
    D_(vkCmdCopyImage) D_(vkCmdBeginRendering) D_(vkCmdEndRendering) D_(vkCmdBindPipeline) D_(vkCmdSetViewport) D_(vkCmdSetScissor)
    D_(vkCmdSetBlendConstants) D_(vkCmdSetDepthTestEnable) D_(vkCmdSetDepthWriteEnable) D_(vkCmdSetDepthCompareOp)
    D_(vkCmdSetStencilTestEnable) D_(vkCmdSetStencilOp) D_(vkCmdSetStencilCompareMask) D_(vkCmdSetStencilWriteMask) D_(vkCmdSetStencilReference)
    D_(vkCmdSetPrimitiveTopology) D_(vkCmdPushConstants) D_(vkCmdBindVertexBuffers) D_(vkCmdDraw) D_(vkCmdClearAttachments)
    D_(vkCreateDescriptorPool) D_(vkResetDescriptorPool) D_(vkAllocateDescriptorSets) D_(vkUpdateDescriptorSets) D_(vkCmdBindDescriptorSets)
    D_(vkCmdClearColorImage) D_(vkCmdClearDepthStencilImage)
#undef D_
    if (!V.headless) {
#define D_(n) if (!(*(void **)&n##_ = (void *)vkGetDeviceProcAddr_(V.dev, #n))) { vk_err("missing " #n, 0); return 0; }
        D_(vkCreateSwapchainKHR) D_(vkDestroySwapchainKHR) D_(vkGetSwapchainImagesKHR) D_(vkAcquireNextImageKHR) D_(vkQueuePresentKHR)
#undef D_
    }
    return 1;
}
static int has_ext(VkExtensionProperties *e, uint32_t n, const char *name) {
    for (uint32_t i = 0; i < n; i++) if (has_text(e[i].extensionName, name) && has_text(name, e[i].extensionName)) return 1;
    return 0;
}
static uint8_t g_vk_heapbuf[4 << 20];
/* Python calls this on the drawing thread. gipa = vkGetInstanceProcAddr; hwnd/hinst = the window (0 = no window:
 * pictures are drawn into an off-screen image, for tests). Then everything else works like the OpenGL renderer.
 * 0 = ok, else see vk_error() / gl_error(). */
GEXPORT int vk_setup(void *gipa, void *hwnd, void *hinst, int scale, int software_too) {
    p_rb_start = vk_rb_start; p_rb_done = vk_rb_done;
    memset(&V, 0, sizeof V); memset(&S, 0, sizeof S);
    g_vk_heap = g_vk_heapbuf; g_vk_heap_size = sizeof g_vk_heapbuf; g_vk_heap_used = 0; g_vk_freel = 0;
    vg_ntex = vg_nrb = vg_nfb = 1; vg_nfreetex = 0; S.unit = 0; memset(g_dc, 0, sizeof g_dc); g_dc_n = 0; g_dcnpool = 0; g_dccur = 0; vg_npipes = 0; vg_nsamp = 0; memset(vg_phash, 0, sizeof vg_phash); g_pcache = 0; g_nmb = 0; g_img_bytes = 0; g_img_count = g_img_dedicated = 0;
    S.cmask[0] = S.cmask[1] = S.cmask[2] = S.cmask[3] = 1; S.dmask = 1; S.dfunc = GL_ALWAYS;
    S.sfunc = GL_ALWAYS; S.sfail = S.szfail = S.szpass = 0x1E00 /* KEEP */; S.smask = S.swmask = 0xFF; S.bs = S.bsa = GL_ONE; S.bd = S.bda = GL_ZERO; S.beq = GL_FUNC_ADD;
    V.headless = hwnd == 0;
    vkGetInstanceProcAddr_ = (PFN_vkGetInstanceProcAddr)gipa;
    *(void **)&vkCreateInstance_ = (void *)vkGetInstanceProcAddr_(0, "vkCreateInstance");
    *(void **)&vkEnumerateInstanceExtensionProperties_ = (void *)vkGetInstanceProcAddr_(0, "vkEnumerateInstanceExtensionProperties");
    if (!vkCreateInstance_) { vk_err("no Vulkan", 0); return 10; }
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = "MGS Portable Ops PC port"; app.apiVersion = VK_API_VERSION_1_3;
    /* (tests: hwnd = 1 is a window that exists only for the graphics driver, VK_EXT_headless_surface) */
    int test_win = hwnd == (void *)1;
    const char *iext[2] = {"VK_KHR_surface", test_win ? "VK_EXT_headless_surface" : "VK_KHR_win32_surface"};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
    if (!V.headless) { ici.enabledExtensionCount = 2; ici.ppEnabledExtensionNames = iext; }
    VkResult r = vkCreateInstance_(&ici, 0, &V.inst); if (r) { vk_err("vkCreateInstance", r); return 11; }
    if (!load_vk_instance_fns()) return 12;
    if (!V.headless) {
        if (test_win) {
            typedef struct { VkStructureType sType; const void *pNext; VkFlags flags; } HSI;
            VkResult (VKAPI_PTR *mk)(VkInstance, const HSI *, const VkAllocationCallbacks *, VkSurfaceKHR *);
            *(void **)&mk = (void *)vkGetInstanceProcAddr_(V.inst, "vkCreateHeadlessSurfaceEXT");
            HSI hs = {(VkStructureType)1000256000, 0, 0}; if (!mk || mk(V.inst, &hs, 0, &V.surf)) { vk_err("headless surface", 0); return 13; }
        } else {
        if (!vkCreateWin32SurfaceKHR_) { vk_err("no window support", 0); return 13; }
        VkWin32SurfaceCreateInfo_ si; memset(&si, 0, sizeof si); si.sType = (VkStructureType)1000009000; si.hinstance = hinst; si.hwnd = hwnd;
        r = vkCreateWin32SurfaceKHR_(V.inst, &si, 0, &V.surf); if (r) { vk_err("vkCreateWin32SurfaceKHR", r); return 14; }
        }
    }
    /* the graphics card: a discrete one first, Vulkan 1.3, graphics queue that can show pictures in the window */
    VkPhysicalDevice pds[8]; uint32_t npd = 8; vkEnumeratePhysicalDevices_(V.inst, &npd, pds);
    int best = -1, bestscore = -1; uint32_t bestq = 0;
    for (uint32_t i = 0; i < npd; i++) {
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties_(pds[i], &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        VkQueueFamilyProperties qf[16]; uint32_t nq = 16; vkGetPhysicalDeviceQueueFamilyProperties_(pds[i], &nq, qf);
        for (uint32_t q = 0; q < nq; q++) {
            if (!(qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 ok = 1; if (!V.headless) vkGetPhysicalDeviceSurfaceSupportKHR_(pds[i], q, V.surf, &ok);
            if (!ok) continue;
            int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > bestscore) { best = (int)i; bestscore = score; bestq = q; }
            break;
        }
    }
    if (best < 0) { vk_err("no Vulkan 1.3 graphics card", 0); return 15; }
    V.pd = pds[best]; V.qfam = bestq;
    vkGetPhysicalDeviceProperties_(V.pd, &V.props); vkGetPhysicalDeviceMemoryProperties_(V.pd, &V.memp);
    VkPhysicalDeviceFeatures have; vkGetPhysicalDeviceFeatures_(V.pd, &have);
    static VkExtensionProperties de[512]; uint32_t nde = 512; vkEnumerateDeviceExtensionProperties_(V.pd, 0, &nde, de);
    const char *dext[3] = {"VK_KHR_swapchain", "VK_KHR_present_id", "VK_KHR_present_wait"};
    int pw = !V.headless && has_ext(de, nde, "VK_KHR_present_id") && has_ext(de, nde, "VK_KHR_present_wait");
    VkPhysicalDeviceFeatures want; memset(&want, 0, sizeof want);
    want.depthClamp = have.depthClamp; want.samplerAnisotropy = have.samplerAnisotropy;
    V.clamp_ok = have.depthClamp != 0; V.aniso_ok = have.samplerAnisotropy != 0;
    /* depth + stencil (the PSP's stencil: the comic cutscenes cut their drawings to the panels with it) */
    V.stencil_ok = 0; V.dsfmt = VK_FORMAT_D32_SFLOAT;
    { VkFormat c[2] = {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
      for (int i = 0; i < 2 && !V.stencil_ok; i++) { VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties_(V.pd, c[i], &fp);
          if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { V.dsfmt = c[i]; V.stencil_ok = 1; } } }
    VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; f13.dynamicRendering = VK_TRUE;
    float prio = 1.f; VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex = V.qfam; qi.queueCount = 1; qi.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.pNext = &f13; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qi;
    dci.enabledExtensionCount = V.headless ? 0 : pw ? 3 : 1; dci.ppEnabledExtensionNames = dext; dci.pEnabledFeatures = &want;
    VkPhysicalDevicePresentIdFeaturesKHR fid = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR}; fid.presentId = VK_TRUE;
    VkPhysicalDevicePresentWaitFeaturesKHR fpw = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR}; fpw.presentWait = VK_TRUE;
    if (pw) { fid.pNext = &fpw; fpw.pNext = &f13; dci.pNext = &fid; }
    r = vkCreateDevice_(V.pd, &dci, 0, &V.dev);
    if (r && pw) { pw = 0; dci.enabledExtensionCount = 1; dci.pNext = &f13; r = vkCreateDevice_(V.pd, &dci, 0, &V.dev); }   /* (without it) */
    if (r) { vk_err("vkCreateDevice", r); return 17; }
    if (!load_vk_device_fns()) return 18;
    g_pw_ok = 0; g_pid = g_pid_done = 0;
    if (pw) { *(void **)&vkWaitForPresentKHR_ = (void *)vkGetDeviceProcAddr_(V.dev, "vkWaitForPresentKHR"); g_pw_ok = vkWaitForPresentKHR_ != 0; }
    vkGetDeviceQueue_(V.dev, V.qfam, 0, &V.q);
    VkCommandPoolCreateInfo cpi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpi.queueFamilyIndex = V.qfam;
    vkCreateCommandPool_(V.dev, &cpi, 0, &V.pool);
    for (int i = 0; i < VK_FRAMES; i++) {
        VFrame *F = &V.fr[i]; VkCommandBuffer cbs[2];
        VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool = V.pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 2;
        vkAllocateCommandBuffers_(V.dev, &ai, cbs); F->up = cbs[0]; F->cb = cbs[1];
        VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; vkCreateFence_(V.dev, &fi, 0, &F->fence);
        if (!make_buffer(RING_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &F->ring, &F->ringmem, &F->ringp)) { vk_err("buffer", 0); return 19; }
        VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; vkCreateSemaphore_(V.dev, &si, 0, &V.acq_sem[i]);
    }
    for (int i = 0; i < 8; i++) { VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; vkCreateSemaphore_(V.dev, &si, 0, &V.done_sem[i]); }
    /* shaders, layout */
    const uint32_t *code[4] = {SPV_MAIN_VERT, SPV_MAIN_FRAG, SPV_PRES_VERT, SPV_PRES_FRAG};
    size_t size[4] = {sizeof SPV_MAIN_VERT, sizeof SPV_MAIN_FRAG, sizeof SPV_PRES_VERT, sizeof SPV_PRES_FRAG};
    for (int i = 0; i < 4; i++) {
        VkShaderModuleCreateInfo si = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; si.codeSize = size[i]; si.pCode = code[i];
        r = vkCreateShaderModule_(V.dev, &si, 0, &V.sm[i]); if (r) { vk_err("vkCreateShaderModule", r); return 20; }
    }
    VkDescriptorSetLayoutBinding lb[3] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, 0},
                                          {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, 0},
                                          {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, 0}};
    VkDescriptorSetLayoutCreateInfo dl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 3; dl.pBindings = lb; vkCreateDescriptorSetLayout_(V.dev, &dl, 0, &V.dsl);
    VkPushConstantRange pr = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};
    VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount = 1; pli.pSetLayouts = &V.dsl; pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pr;
    vkCreatePipelineLayout_(V.dev, &pli, 0, &V.pl);
    { VkPipelineCacheCreateInfo ci = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO}; if (vkCreatePipelineCache_(V.dev, &ci, 0, &g_pcache)) g_pcache = 0; }
    /* the window: its picture format, and how pictures are shown (mailbox: never waits, never tears) */
    if (!V.headless) {
        VkSurfaceFormatKHR sf[64]; uint32_t nsf = 64; vkGetPhysicalDeviceSurfaceFormatsKHR_(V.pd, V.surf, &nsf, sf);
        V.scfmt = nsf ? sf[0].format : VK_FORMAT_B8G8R8A8_UNORM;
        for (uint32_t i = 0; i < nsf; i++) if (sf[i].format == VK_FORMAT_B8G8R8A8_UNORM || sf[i].format == VK_FORMAT_R8G8B8A8_UNORM) { V.scfmt = sf[i].format; break; }
        VkPresentModeKHR pm[16]; uint32_t npm = 16; vkGetPhysicalDeviceSurfacePresentModesKHR_(V.pd, V.surf, &npm, pm);
        /* never "immediate": in fullscreen (no desktop compositor in between) it tears the picture while the camera
         * moves. Mailbox (newest picture at the next refresh) if offered, else FIFO (the standard v-sync) */
        V.pmode = VK_PRESENT_MODE_FIFO_KHR;
        for (uint32_t i = 0; i < npm; i++) if (pm[i] == VK_PRESENT_MODE_MAILBOX_KHR) V.pmode = pm[i];
    }
    V.f = 0; begin_cbs();
    V.dummy = make_image(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, 0);
    { VkClearColorValue cc; memset(&cc, 0, sizeof cc); VkImageSubresourceRange rr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdClearColorImage_(V.fr[V.f].up, V.dummy->img, VK_IMAGE_LAYOUT_GENERAL, &cc, 1, &rr); }
    if (V.headless) V.head = make_image(1280, 720, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, 0);
    /* and from here on the OpenGL renderer does the work */
    int res = gl_setup(vkgl_getproc, scale, software_too);
    if (res) return 30 + res;
    return 0;
}
/* saved pipelines: "PSPPIPE1", key count, key size, the keys, then the driver's pipeline cache data */
GEXPORT int vk_pipes_save(uint8_t *out, int max) {
    if (!V.dev) return 0;
    size_t cs = 0; if (g_pcache) vkGetPipelineCacheData_(V.dev, g_pcache, &cs, 0);
    int head = 16 + vg_npipes * (int)sizeof(PKey), need = head + (int)cs;
    if (!out || max < need) return need;
    memcpy(out, "PSPPIPE1", 8); int32_t n = vg_npipes, ks = (int32_t)sizeof(PKey); memcpy(out + 8, &n, 4); memcpy(out + 12, &ks, 4);
    for (int i = 0; i < vg_npipes; i++) memcpy(out + 16 + i * sizeof(PKey), &vg_pipes[i].k, sizeof(PKey));
    if (cs) { size_t c2 = cs; vkGetPipelineCacheData_(V.dev, g_pcache, &c2, out + head); need = head + (int)c2; }
    g_pipe_dirty = 0; return need;
}
/* (call on the drawing thread right after vk_setup) the cache data, then every saved pipeline made now */
GEXPORT int vk_pipes_load(const uint8_t *in, int size) {
    if (!V.dev || !in || size < 16 || memcmp(in, "PSPPIPE1", 8)) return -1;
    int32_t n, ks; memcpy(&n, in + 8, 4); memcpy(&ks, in + 12, 4);
    if (ks != (int32_t)sizeof(PKey) || n < 0 || n > 1024 || 16 + (long)n * ks > size) return -2;
    int head = 16 + n * ks;
    if (size > head) {
        VkPipelineCacheCreateInfo ci = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO}; ci.initialDataSize = (size_t)(size - head); ci.pInitialData = in + head;
        VkPipelineCache c = 0;
        if (vkCreatePipelineCache_(V.dev, &ci, 0, &c) == VK_SUCCESS) { if (g_pcache) vkDestroyPipelineCache_(V.dev, g_pcache, 0); g_pcache = c; }
    }
    int made = 0, had_err = V.err[0] != 0;
    for (int i = 0; i < n; i++) {
        PKey k; memcpy(&k, in + 16 + i * ks, sizeof k);
        if (k.samples != 1 && k.samples != 2 && k.samples != 4 && k.samples != 8) continue;
        int before = g_pipe_new; if (get_pipe(&k) && g_pipe_new > before) made++;
    }
    if (!had_err) V.err[0] = 0;                               /* (a saved setup this card can't make: just not made) */
    g_pipe_new = 0; g_pipe_dirty = 0; return made;
}
/* pipelines made since the last call (each one a possible hitch), and whether there is anything new to save */
GEXPORT int vk_pipes_new(int *dirty) { int r = g_pipe_new; g_pipe_new = 0; if (dirty) *dirty = g_pipe_dirty; return r; }
GEXPORT void vk_syncs(int *o) { for (int i = 0; i < 3; i++) { o[i] = g_nsync[i]; g_nsync[i] = 0; } }
/* (log) images alive, their memory in MB, memory blocks, own allocations */
GEXPORT void vk_mem_stats(int *o) { o[0] = g_img_count; o[1] = (int)(g_img_bytes >> 20); o[2] = g_nmb; o[3] = g_img_dedicated; }
/* (perf_log, any thread) wait up to 50 ms for the next picture to reach the screen. 1 = out = {seq, tsc presented, tsc
 * shown}; 0 = nothing yet; -1 = not available */
GEXPORT int vk_present_track(uint64_t *out) {
    if (!g_pw_ok || !vkWaitForPresentKHR_) return -1;
    uint64_t want = g_pid_done + 1; if (want > g_pid) return 0;
    PRec *pr = &g_prec[want & 1023]; if (pr->id != want) { g_pid_done = want; return 0; }
    g_pw_busy = 1;
    if (pr->sc != V.sc) { g_pw_busy = 0; g_pid_done = want; return 0; }     /* (window changed since) */
    VkResult r = vkWaitForPresentKHR_(V.dev, pr->sc, want, 50000000ull); uint64_t now = TSC(); g_pw_busy = 0;
    if (r == VK_TIMEOUT) return 0;
    g_pid_done = want; if (r != VK_SUCCESS) return 0;
    out[0] = pr->seq; out[1] = pr->tsc; out[2] = now; return 1;
}
GEXPORT int vk_present_mode(void) { return (int)V.pmode; }   /* 0 immediate, 1 mailbox, 2 fifo */
GEXPORT const char *vk_error(void) { return V.err[0] ? V.err : g_gl_err; }
/* show the window picture drawn since the last call (psp_gl.inc's present has drawn it into framebuffer 0) */
GEXPORT int vk_swap(void) { if (!V.dev) return 0; submit(1, 0); return V.err[0] ? 1 : 0; }
/* like vk_swap, but the picture is kept until vk_present_held(seq) (from another thread) hands it to the screen */
GEXPORT int vk_swap_hold(uint32_t seq) { if (!V.dev) return 0; g_hold_next = 1; g_hold_seq = seq; submit(1, 0); g_hold_next = 0; return V.err[0] ? 1 : 0; }
/* (any thread) show held picture seq now. 1 = shown; 0 = not drawn yet: it is shown the moment it is done */
GEXPORT int vk_present_held(uint32_t seq) {
    if (!V.dev) return 0;
    uint64_t t0 = TSC(); qlock_(7); int r = 0;
    if (g_held >= 0) { r = g_held_seq == seq; flush_held(); }
    if (!r) g_auto_seq = seq;
    qunlock(); g_tm[5] += TSC() - t0; return r;
}
GEXPORT void vk_test_recreate(void) { V.sc_ok = 0; }   /* (tests: as if the window changed size) */
GEXPORT int vk_acq_blocked(void) { int r = g_acq_block; g_acq_block = 0; return r; }
GEXPORT int vk_present_late(void) { int r = g_npres_late; g_npres_late = 0; return r; }
/* (tests) the size of the off-screen window picture */
GEXPORT void vk_head_size(int w, int h) {
    if (!V.headless || w <= 0 || h <= 0) return;
    if (V.head && V.head->w == w && V.head->h == h) return;
    drop_image(V.head); V.head = make_image(w, h, VK_FORMAT_R8G8B8A8_UNORM, 1, 1, 0);
}
GEXPORT const char *vk_device_name(void) { return V.dev ? V.props.deviceName : ""; }
