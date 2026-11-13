"""
OpenGL for the game window (drawing on the graphics card). Nothing to install: Windows' own opengl32.dll.

    ctx = GLWindow(hwnd)        # on the thread that will draw (the drawing thread); hwnd = the Tk frame's window handle
    getproc = ctx.getproc       # hand to psp_core.dll's gl_setup()
    ctx.swap()                  # after gl_present(): show the picture

On Linux (for testing without a window) HeadlessGL() gives an off-screen context through EGL.
"""
import ctypes as ct, sys

GETPROC_T = ct.CFUNCTYPE(ct.c_void_p, ct.c_char_p)


class GLWindow:
    def __init__(self, hwnd):
        if sys.platform != 'win32': raise OSError("GLWindow is for Windows")
        from ctypes import wintypes as wt
        self.user32 = ct.WinDLL('user32'); self.gdi32 = ct.WinDLL('gdi32'); self.gl = ct.WinDLL('opengl32')
        self.kernel32 = ct.WinDLL('kernel32')
        self.user32.GetDC.restype = wt.HDC; self.user32.GetDC.argtypes = [wt.HWND]
        self.hwnd = hwnd; self.hdc = self.user32.GetDC(hwnd)
        if not self.hdc: raise OSError("GetDC failed")

        class PFD(ct.Structure):
            _fields_ = [('nSize', wt.WORD), ('nVersion', wt.WORD), ('dwFlags', wt.DWORD), ('iPixelType', ct.c_ubyte),
                        ('cColorBits', ct.c_ubyte), ('cRedBits', ct.c_ubyte), ('cRedShift', ct.c_ubyte), ('cGreenBits', ct.c_ubyte),
                        ('cGreenShift', ct.c_ubyte), ('cBlueBits', ct.c_ubyte), ('cBlueShift', ct.c_ubyte), ('cAlphaBits', ct.c_ubyte),
                        ('cAlphaShift', ct.c_ubyte), ('cAccumBits', ct.c_ubyte), ('cAccumRedBits', ct.c_ubyte), ('cAccumGreenBits', ct.c_ubyte),
                        ('cAccumBlueBits', ct.c_ubyte), ('cAccumAlphaBits', ct.c_ubyte), ('cDepthBits', ct.c_ubyte), ('cStencilBits', ct.c_ubyte),
                        ('cAuxBuffers', ct.c_ubyte), ('iLayerType', ct.c_ubyte), ('bReserved', ct.c_ubyte), ('dwLayerMask', wt.DWORD),
                        ('dwVisibleMask', wt.DWORD), ('dwDamageMask', wt.DWORD)]
        pfd = PFD(); pfd.nSize = ct.sizeof(PFD); pfd.nVersion = 1
        pfd.dwFlags = 0x4 | 0x20 | 0x1                                # DRAW_TO_WINDOW | SUPPORT_OPENGL | DOUBLEBUFFER
        pfd.iPixelType = 0; pfd.cColorBits = 32; pfd.cDepthBits = 24; pfd.iLayerType = 0
        self.gdi32.ChoosePixelFormat.argtypes = [wt.HDC, ct.c_void_p]; self.gdi32.SetPixelFormat.argtypes = [wt.HDC, ct.c_int, ct.c_void_p]
        fmt = self.gdi32.ChoosePixelFormat(self.hdc, ct.byref(pfd))
        if not fmt or not self.gdi32.SetPixelFormat(self.hdc, fmt, ct.byref(pfd)): raise OSError("no OpenGL pixel format")
        self.gl.wglCreateContext.restype = ct.c_void_p; self.gl.wglCreateContext.argtypes = [wt.HDC]
        self.gl.wglMakeCurrent.argtypes = [wt.HDC, ct.c_void_p]
        self.ctx = self.gl.wglCreateContext(self.hdc)
        if not self.ctx or not self.gl.wglMakeCurrent(self.hdc, self.ctx): raise OSError("could not make an OpenGL context")
        self.gl.wglGetProcAddress.restype = ct.c_void_p; self.gl.wglGetProcAddress.argtypes = [ct.c_char_p]
        self.kernel32.GetProcAddress.restype = ct.c_void_p; self.kernel32.GetProcAddress.argtypes = [ct.c_void_p, ct.c_char_p]
        glh = self.gl._handle
        def getproc(name):
            p = self.gl.wglGetProcAddress(name)
            if p in (None, 0, 1, 2, 3, -1, 0xFFFFFFFFFFFFFFFF): p = self.kernel32.GetProcAddress(glh, name)
            return p
        self.getproc = GETPROC_T(getproc)
        sw = getproc(b'wglSwapIntervalEXT')                            # don't wait for the monitor: the game keeps its own pace
        if sw: ct.CFUNCTYPE(ct.c_int, ct.c_int)(sw)(0)
        self.gdi32.SwapBuffers.argtypes = [wt.HDC]
        glgs = ct.CFUNCTYPE(ct.c_char_p, ct.c_uint)(self.getproc(b'glGetString'))
        self.info = b' / '.join(x or b'?' for x in (glgs(0x1F00), glgs(0x1F01), glgs(0x1F02))).decode('latin1', 'replace')

    def swap(self): self.gdi32.SwapBuffers(self.hdc)


class HeadlessGL:
    """off-screen OpenGL through EGL (Linux, for tests)"""
    def __init__(self):
        egl = ct.CDLL('libEGL.so.1'); self.egl = egl
        egl.eglGetProcAddress.restype = ct.c_void_p; egl.eglGetProcAddress.argtypes = [ct.c_char_p]
        gpd = ct.CFUNCTYPE(ct.c_void_p, ct.c_uint, ct.c_void_p, ct.c_void_p)(egl.eglGetProcAddress(b'eglGetPlatformDisplayEXT'))
        dpy = gpd(0x31DD, None, None); egl.eglInitialize.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_void_p]; egl.eglInitialize(dpy, None, None)
        egl.eglBindAPI(0x30A2); egl.eglCreateContext.restype = ct.c_void_p; egl.eglCreateContext.argtypes = [ct.c_void_p] * 4
        ctx = egl.eglCreateContext(dpy, None, None, (ct.c_int * 7)(0x3098, 3, 0x30FB, 3, 0x30FD, 1, 0x3038))
        egl.eglMakeCurrent.argtypes = [ct.c_void_p] * 4
        if not egl.eglMakeCurrent(dpy, None, None, ctx): raise OSError("EGL context failed")
        self.getproc = GETPROC_T(lambda name: egl.eglGetProcAddress(name)); self.info = 'EGL (headless)'
    def swap(self): pass


class VKWindow:
    """Vulkan for the game window (renderer = vulkan). Nothing to install: the graphics driver's vulkan-1.dll.
    psp_core.dll's vk_setup() makes the device and the window's swapchain, then runs the same renderer as OpenGL
    (its OpenGL calls are carried out on Vulkan). Call on the drawing thread."""
    def __init__(self, hwnd, lib, scale, software_too):
        if sys.platform == 'win32':
            vk = ct.WinDLL('vulkan-1'); k32 = ct.WinDLL('kernel32')
            k32.GetModuleHandleW.restype = ct.c_void_p; k32.GetModuleHandleW.argtypes = [ct.c_wchar_p]
            hinst = k32.GetModuleHandleW(None)
        else:
            vk = ct.CDLL('libvulkan.so.1'); hinst = None; hwnd = None              # (tests: no window)
        gipa = ct.cast(vk.vkGetInstanceProcAddr, ct.c_void_p).value
        lib.vk_setup.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_void_p, ct.c_int, ct.c_int]
        lib.vk_error.restype = ct.c_char_p; lib.vk_device_name.restype = ct.c_char_p
        self.vk = vk; self.lib = lib
        r = lib.vk_setup(gipa, hwnd, hinst, scale, software_too)
        if r: raise OSError(f"Vulkan setup failed ({r}): {(lib.vk_error() or b'').decode('latin1', 'replace')}")
        self.info = 'Vulkan - ' + (lib.vk_device_name() or b'?').decode('latin1', 'replace')
        try: self.info += ' - v-sync: ' + {0: 'off', 1: 'mailbox', 2: 'fifo', 3: 'fifo relaxed'}.get(lib.vk_present_mode(), '?')
        except AttributeError: pass
    def swap(self): self.lib.vk_swap()
