#include "SFMLDisplayEngine.h"
#include "../avgconfigwrapper.h"

#ifdef __APPLE__
#include "SDLMain.h"
#endif

#include "Shape.h"

#include "Event.h"
#include "MouseEvent.h"
#include "KeyEvent.h"
#if defined(HAVE_XI2_1) || defined(HAVE_XI2_2) 
#include "XInputMTInputDevice.h"
#endif
#include "../base/MathHelper.h"
#include "../base/Exception.h"
#include "../base/Logger.h"
#include "../base/ScopeTimer.h"
#include "../base/OSHelper.h"
#include "../base/StringHelper.h"

#include "../graphics/GLContext.h"
#include "../graphics/Filterflip.h"
#include "../graphics/Filterfliprgb.h"

#include "OGLSurface.h"
#include "OffscreenCanvas.h"

//#include <SDL/SDL.h>
#include <SFML/Window.hpp>

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#endif
#ifdef linux
//#include <SDL/SDL_syswm.h>
#include <X11/extensions/xf86vmode.h>
#endif

#ifdef __APPLE__
#include <OpenGL/OpenGL.h>
#endif

#ifdef linux
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#endif

#include <signal.h>
#include <iostream>
#include <sstream>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std;
using namespace sf;

namespace avg {

float SFMLDisplayEngine::s_RefreshRate = 0.0;

/**void safeSetAttribute(SDL_GLattr attr, int value) 
{
    int err = SDL_GL_SetAttribute(attr, value);
    if (err == -1) {
        throw Exception(AVG_ERR_VIDEO_GENERAL, SDL_GetError());
    }
}**/

SFMLDisplayEngine::SFMLDisplayEngine()
    : IInputDevice(EXTRACT_INPUTDEVICE_CLASSNAME(SFMLDisplayEngine)),
      m_WindowSize(0,0),
      m_ScreenResolution(0,0),
      m_PPMM(0),
      m_pScreen(0),
      m_bMouseOverApp(true),
      m_pLastMouseEvent(new MouseEvent(Event::CURSORMOTION, false, false, false, 
            IntPoint(-1, -1), MouseEvent::NO_BUTTON, glm::vec2(-1, -1), 0)),
      m_NumMouseButtonsDown(0)
{
#ifdef __APPLE__
    static bool bSDLInitialized = false;
    if (!bSDLInitialized) {
        CustomSDLMain();
        bSDLInitialized = true;
    }
#endif
    if (SDL_InitSubSystem(SDL_INIT_VIDEO)==-1) {
        AVG_TRACE(Logger::ERROR, "Can't init SDL display subsystem.");
        exit(-1);
    }
    m_Gamma[0] = 1.0;
    m_Gamma[1] = 1.0;
    m_Gamma[2] = 1.0;
    initTranslationTable();
}

SFMLDisplayEngine::~SFMLDisplayEngine()
{
#ifndef _WIN32
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
#endif
}

void SFMLDisplayEngine::init(const DisplayParams& dp, GLConfig glConfig) 
{
    calcScreenDimensions(dp.m_DotsPerMM);
    stringstream ss;
    if (dp.m_Pos.x != -1) {
        ss << dp.m_Pos.x << "," << dp.m_Pos.y;
        setEnv("SFML_VIDEO_WINDOW_POS", ss.str().c_str());
    }
    float aspectRatio = float(dp.m_Size.x)/float(dp.m_Size.y);
    if (dp.m_WindowSize == IntPoint(0, 0)) {
        m_WindowSize = dp.m_Size;
    } else if (dp.m_WindowSize.x == 0) {
        m_WindowSize.x = int(dp.m_WindowSize.y*aspectRatio);
        m_WindowSize.y = dp.m_WindowSize.y;
    } else {
        m_WindowSize.x = dp.m_WindowSize.x;
        m_WindowSize.y = int(dp.m_WindowSize.x/aspectRatio);
    }
    AVG_ASSERT(m_WindowSize.x != 0 && m_WindowSize.y != 0);
/**    switch (dp.m_BPP) {
        case 32:
            safeSetAttribute(SDL_GL_RED_SIZE, 8);
            safeSetAttribute(SDL_GL_GREEN_SIZE, 8);
            safeSetAttribute(SDL_GL_BLUE_SIZE, 8);
            safeSetAttribute(SDL_GL_BUFFER_SIZE, 32);
            break;
        case 24:
            safeSetAttribute(SDL_GL_RED_SIZE, 8);
            safeSetAttribute(SDL_GL_GREEN_SIZE, 8);
            safeSetAttribute(SDL_GL_BLUE_SIZE, 8);
            safeSetAttribute(SDL_GL_BUFFER_SIZE, 24);
            break;
        case 16:
            safeSetAttribute(SDL_GL_RED_SIZE, 5);
            safeSetAttribute(SDL_GL_GREEN_SIZE, 6);
            safeSetAttribute(SDL_GL_BLUE_SIZE, 5);
            safeSetAttribute(SDL_GL_BUFFER_SIZE, 16);
            break;
        case 15:
            safeSetAttribute(SDL_GL_RED_SIZE, 5);
            safeSetAttribute(SDL_GL_GREEN_SIZE, 5);
            safeSetAttribute(SDL_GL_BLUE_SIZE, 5);
            safeSetAttribute(SDL_GL_BUFFER_SIZE, 15);
            break;
        default:
            AVG_TRACE(Logger::ERROR, "Unsupported bpp " << dp.m_BPP <<
                    "in SDLDisplayEngine::init()");
            exit(-1);
    }
    safeSetAttribute(SDL_GL_DEPTH_SIZE, 0);
    safeSetAttribute(SDL_GL_STENCIL_SIZE, 8);
    safeSetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    safeSetAttribute(SDL_GL_SWAP_CONTROL , 0); *//
    unsigned int Flags = SDL_OPENGL;
    if (dp.m_bFullscreen) {
        Flags |= Style::Fullscreen;
    }
    m_bIsFullscreen = dp.m_bFullscreen;

    if (!dp.m_bHasWindowFrame) {
        Flags |= Style::None;
    }

    bool bAllMultisampleValuesTested = false;
    m_pScreen = 0;
    while (!bAllMultisampleValuesTested && !m_pScreen) {
        if (glConfig.m_MultiSampleSamples > 1) {
            safeSetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
            safeSetAttribute(SDL_GL_MULTISAMPLESAMPLES, glConfig.m_MultiSampleSamples);
        } else {
            safeSetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
            safeSetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        }
        m_pScreen.Create(VideoMode(m_WindowSize.x, m_WindowSize.y, dp.m_BPP), "libavg", Flags);
//        m_pScreen = SDL_SetVideoMode(m_WindowSize.x, m_WindowSize.y, dp.m_BPP, Flags);
        if (!m_pScreen) {
            switch (glConfig.m_MultiSampleSamples) {
                case 1:
                    bAllMultisampleValuesTested = true;
                    break;
                case 2:  
                    glConfig.m_MultiSampleSamples = 1;
                    break;
                case 4:  
                    glConfig.m_MultiSampleSamples = 2;
                    break;
                case 8:  
                    glConfig.m_MultiSampleSamples = 4;
                    break;
                default:
                    glConfig.m_MultiSampleSamples = 8;
                    break;
            }
        }
    }
    if (!m_pScreen) {
        throw Exception(AVG_ERR_UNSUPPORTED, string("Setting SDL video mode failed: ")
                + SDL_GetError() + ". (size=" + toString(m_WindowSize) + ", bpp=" + 
                toString(dp.m_BPP) + ", multisamplesamples=" + 
                toString(glConfig.m_MultiSampleSamples) + ").");
    }
    m_input = m_pScreen.GetInput();
    m_pGLContext = new GLContext(true, glConfig);
    GLContext::setMain(m_pGLContext);

#if defined(HAVE_XI2_1) || defined(HAVE_XI2_2) 
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    m_pXIMTInputDevice = 0;
#endif
    SDL_WM_SetCaption("libavg", 0);
    calcRefreshRate();

    glEnable(GL_BLEND);
    GLContext::checkError("init: glEnable(GL_BLEND)");
    glShadeModel(GL_FLAT);
    GLContext::checkError("init: glShadeModel(GL_FLAT)");
    glDisable(GL_DEPTH_TEST);
    GLContext::checkError("init: glDisable(GL_DEPTH_TEST)");
    glEnable(GL_STENCIL_TEST);
    GLContext::checkError("init: glEnable(GL_STENCIL_TEST)");
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); 
    GLContext::checkError("init: glTexEnvf()");
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    if (!queryOGLExtension("GL_ARB_vertex_buffer_object")) {
        throw Exception(AVG_ERR_UNSUPPORTED,
            "Graphics driver lacks vertex buffer support, unable to initialize graphics.");
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_TEXTURE_2D);
    setGamma(dp.m_Gamma[0], dp.m_Gamma[1], dp.m_Gamma[2]);
    showCursor(dp.m_bShowCursor);
    if (dp.m_Framerate == 0) {
        setVBlankRate(dp.m_VBRate);
    } else {
        setFramerate(dp.m_Framerate);
    }
    glproc::UseProgramObject(0);
    if (m_pGLContext->useMinimalShader()) {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
    }

    m_Size = dp.m_Size;
    // SDL sets up a signal handler we really don't want.
    signal(SIGSEGV, SIG_DFL);
    m_pGLContext->logConfig();

//    SDL_EnableUNICODE(1);
}

#ifdef _WIN32
#pragma warning(disable: 4996)
#endif
void SFMLDisplayEngine::teardown()
{
    if (m_pScreen) {
        if (m_Gamma[0] != 1.0f || m_Gamma[1] != 1.0f || m_Gamma[2] != 1.0f) {
            internalSetGamma(1.0f, 1.0f, 1.0f);
        }
#ifdef linux
        // Workaround for broken mouse cursor on exit under Ubuntu 8.04.
        SDL_ShowCursor(SDL_ENABLE);
#endif
        m_pScreen = 0;
        delete m_pGLContext;
        GLContext::setMain(0);
    }
}

float SFMLDisplayEngine::getRefreshRate() 
{
    if (s_RefreshRate == 0.0) {
        calcRefreshRate();
    }
    return s_RefreshRate;
}

void SFMLDisplayEngine::setGamma(float red, float green, float blue)
{
    if (red > 0) {
        AVG_TRACE(Logger::CONFIG, "Setting gamma to " << red << ", " << green << ", "
                << blue);
        bool bOk = internalSetGamma(red, green, blue);
        m_Gamma[0] = red;
        m_Gamma[1] = green;
        m_Gamma[2] = blue;
        if (!bOk) {
            AVG_TRACE(Logger::WARNING, "Unable to set display gamma.");
        }
    }
}

void SFMLDisplayEngine::setMousePos(const IntPoint& pos)
{
    m_pScreen.SetCursorPosition(pos.x, pos.y);
}

int SFMLDisplayEngine::getKeyModifierState() const
{
    int result = 0x0000;
    m_input.IsKeyDown(Key::LShift){
        result |= 0x0001;
    }
    m_input.IsKeyDown(Key::RShift){
        result |= 0x0002;
    }
    m_input.IsKeyDown(Key::LControl){
        result |= 0x0040;
    }
    m_input.IsKeyDown(Key::RControl){
        result |= 0x0080;
    }
    m_input.IsKeyDown(Key::LAlt){
        result |= 0x0100;
    }
    m_input.IsKeyDown(Key::RAlt){
        result |= 0x0200;
    }
/*    m_input.IsKeyDown(Key::LMeta){
        result |= 0x0400;
    }
    m_input.IsKeyDown(Key::RMeta){
        result |= 0x0800;
    }
    m_input.IsKeyDown(Key::Num){
        result |= 0x1000;
    }
    m_input.IsKeyDown(Key::Caps){
        result |= 0x2000;
    }
    m_input.IsKeyDown(Key::Mode){
        result |= 0x4000;
    }*/
    return result;
}

void SFMLDisplayEngine::calcScreenDimensions(float dotsPerMM)
{
    if (m_ScreenResolution.x == 0) {
//        const SDL_VideoInfo* pInfo = SDL_GetVideoInfo();
        m_ScreenResolution = IntPoint(m_pScreen.GetWidth(), m_pScreen.GetHight());
    }
    if (dotsPerMM != 0) {
        m_PPMM = dotsPerMM;
    }

    if (m_PPMM == 0) {
#ifdef WIN32
        HDC hdc = CreateDC("DISPLAY", NULL, NULL, NULL);
        m_PPMM = GetDeviceCaps(hdc, LOGPIXELSX)/25.4f;
#else
    #ifdef linux
        Display * pDisplay = XOpenDisplay(0);
        glm::vec2 displayMM(DisplayWidthMM(pDisplay,0), DisplayHeightMM(pDisplay,0));
    #elif defined __APPLE__
        CGSize size = CGDisplayScreenSize(CGMainDisplayID());
        glm::vec2 displayMM(size.width, size.height);
    #endif
        // Non-Square pixels cause errors here. We'll fix that when it happens.
        m_PPMM = m_ScreenResolution.x/displayMM.x;
#endif
    }
}

bool SFMLDisplayEngine::internalSetGamma(float red, float green, float blue)                // gamma kann man nicht setzen nur durch shader umsetzbar
{
#ifdef __APPLE__
    // Workaround for broken SDL_SetGamma for libSDL 1.2.15 under Lion
    CGError err = CGSetDisplayTransferByFormula(kCGDirectMainDisplay, 0, 1, 1/red,
            0, 1, 1/green, 0, 1, 1/blue);
    return (err == CGDisplayNoErr);
#else
    int err = SDL_SetGamma(float(red), float(green), float(blue));
    return (err != -1);
#endif
}

static ProfilingZoneID SwapBufferProfilingZone("Render - swap buffers");

void SFMLDisplayEngine::swapBuffers()                                                       // SFML Swapt selber und es ist immer aktiviert
{
    ScopeTimer timer(SwapBufferProfilingZone);
    SDL_GL_SwapBuffers();
    GLContext::checkError("swapBuffers()");
}

void SFMLDisplayEngine::showCursor(bool bShow)
{
#ifdef _WIN32
#define MAX_CORE_POINTERS   6
    // Hack to fix a pointer issue with fullscreen, SDL and touchscreens
    // Refer to Mantis bug #140
    for (int i = 0; i < MAX_CORE_POINTERS; ++i) {
        m_pScreen.ShowMouseCursor(bShow);
    }
#else
    if (bShow) {
        m_pScreen.ShowMouseCursor(true);
    } else {
        m_pScreen.ShowMouseCursor(false);
    }
#endif
}

BitmapPtr SFMLDisplayEngine::screenshot(int buffer)
{
    BitmapPtr pBmp (new Bitmap(m_WindowSize, B8G8R8X8, "screenshot"));
    string sTmp;
    bool bBroken = getEnv("AVG_BROKEN_READBUFFER", sTmp);
    GLenum buf = buffer;
    if (!buffer) {
        if (bBroken) {
            // Workaround for buggy GL_FRONT on some machines.
            buf = GL_BACK;
        } else {
            buf = GL_FRONT;
        }
    }
    glReadBuffer(buf);
    glproc::BindBuffer(GL_PIXEL_PACK_BUFFER_EXT, 0);
    GLContext::checkError("SFMLDisplayEngine::screenshot:glReadBuffer()");
    glReadPixels(0, 0, m_WindowSize.x, m_WindowSize.y, GL_BGRA, GL_UNSIGNED_BYTE, 
            pBmp->getPixels());
    GLContext::checkError("SFMLDisplayEngine::screenshot:glReadPixels()");
    FilterFlip().applyInPlace(pBmp);
    return pBmp;
}

IntPoint SFMLDisplayEngine::getSize()
{
    return m_Size;
}

void SFMLDisplayEngine::calcRefreshRate()
{
    float lastRefreshRate = s_RefreshRate;
    s_RefreshRate = 0;
#ifdef __APPLE__
    CFDictionaryRef modeInfo = CGDisplayCurrentMode(CGMainDisplayID());
    if (modeInfo) {
        CFNumberRef value = (CFNumberRef) CFDictionaryGetValue(modeInfo, 
                kCGDisplayRefreshRate);
        if (value) {
            CFNumberGetValue(value, kCFNumberIntType, &s_RefreshRate);
            if (s_RefreshRate < 1.0) {
                AVG_TRACE(Logger::CONFIG, 
                        "This seems to be a TFT screen, assuming 60 Hz refresh rate.");
                s_RefreshRate = 60;
            }
        } else {
            AVG_TRACE(Logger::WARNING, 
                    "Apple refresh rate calculation (CFDictionaryGetValue) failed");
        }
    } else {
        AVG_TRACE(Logger::WARNING, 
                "Apple refresh rate calculation (CGDisplayCurrentMode) failed");
    }
#elif defined _WIN32
    // This isn't correct for multi-monitor systems.
    HDC hDC = CreateDC("DISPLAY", NULL, NULL, NULL);
    s_RefreshRate = float(GetDeviceCaps(hDC, VREFRESH));
    if (s_RefreshRate < 2) {
        s_RefreshRate = 60;
    }
    DeleteDC(hDC);
#else 
    Display * pDisplay = XOpenDisplay(0);
    int pixelClock;
    XF86VidModeModeLine modeLine;
    bool bOK = XF86VidModeGetModeLine (pDisplay, DefaultScreen(pDisplay), 
            &pixelClock, &modeLine);
    if (!bOK) {
        AVG_TRACE (Logger::WARNING, 
                "Could not get current refresh rate (XF86VidModeGetModeLine failed).");
        AVG_TRACE (Logger::WARNING, 
                "Defaulting to 60 Hz refresh rate.");
    }
    float HSyncRate = pixelClock*1000.0/modeLine.htotal;
    s_RefreshRate = HSyncRate/modeLine.vtotal;
    XCloseDisplay(pDisplay);
#endif
    if (s_RefreshRate == 0) {
        s_RefreshRate = 60;
    }
    if (lastRefreshRate != s_RefreshRate) {
        AVG_TRACE(Logger::CONFIG, "Vertical Refresh Rate: " << s_RefreshRate);
    }

}

vector<long> SFMLDisplayEngine::KeyCodeTranslationTable(SDLK_LAST, key::KEY_UNKNOWN);

const char * getEventTypeName(unsigned char type) 
{
    switch (type) {
            case SDL_ACTIVEEVENT:
                return "SDL_ACTIVEEVENT";
            case SDL_KEYDOWN:
                return "SDL_KEYDOWN";
            case SDL_KEYUP:
                return "SDL_KEYUP";
            case SDL_MOUSEMOTION:
                return "SDL_MOUSEMOTION";
            case SDL_MOUSEBUTTONDOWN:
                return "SDL_MOUSEBUTTONDOWN";
            case SDL_MOUSEBUTTONUP:
                return "SDL_MOUSEBUTTONUP";
            case SDL_JOYAXISMOTION:
                return "SDL_JOYAXISMOTION";
            case SDL_JOYBUTTONDOWN:
                return "SDL_JOYBUTTONDOWN";
            case SDL_JOYBUTTONUP:
                return "SDL_JOYBUTTONUP";
            case SDL_VIDEORESIZE:
                return "SDL_VIDEORESIZE";
            case SDL_VIDEOEXPOSE:
                return "SDL_VIDEOEXPOSE";
            case SDL_QUIT:
                return "SDL_QUIT";
            case SDL_USEREVENT:
                return "SDL_USEREVENT";
            case SDL_SYSWMEVENT:
                return "SDL_SYSWMEVENT";
            default:
                return "Unknown SDL event type";
    }
}

vector<EventPtr> SFMLDisplayEngine::pollEvents()
{
    SFML_Event sfmlEvent;
    vector<EventPtr> events;

    while (m_pScreen.GetEvent(sfmlEvent&)) {
        EventPtr pNewEvent;
        switch (sfmlEvent.type) {
            case SFML_Event::MouseMoved:
                if (m_bMouseOverApp) {
                    pNewEvent = createMouseEvent(Event::CURSORMOTION, sfmlEvent, 
                            MouseEvent::NO_BUTTON);
                    CursorEventPtr pNewCursorEvent = 
                            boost::dynamic_pointer_cast<CursorEvent>(pNewEvent);
                    if (!events.empty()) {
                        CursorEventPtr pLastEvent = 
                                boost::dynamic_pointer_cast<CursorEvent>(events.back());
                        if (pLastEvent && *pNewCursorEvent == *pLastEvent) {
                            pNewEvent = EventPtr();
                        }
                    }
                }
                break;
            case SFML_Event::MouseButtonPressed:
                pNewEvent = createMouseButtonEvent(Event::CURSORDOWN, sfmlEvent);
                break;
            case SFML_Event::MouseButtonReleased:
                pNewEvent = createMouseButtonEvent(Event::CURSORUP, sfmlEvent);
                break;
            case SFML_Event::JoyMoved:
//                pNewEvent = createAxisEvent(sfmlEvent));
                break;
            case SFML_Event::JoyButtonPressed:
//                pNewEvent = createButtonEvent(Event::BUTTON_DOWN, sfmlEvent));
                break;
            case SFML_Event::JoyButtonReleased:
//                pNewEvent = createButtonEvent(Event::BUTTON_UP, sfmlEvent));
                break;
            case SFML_Event::KeyPressed:
                pNewEvent = createKeyEvent(Event::KEYDOWN, sfmlEvent);
                break;
            case SFML_Event::KeyReleased:
                pNewEvent = createKeyEvent(Event::KEYUP, sfmlEvent);
                break;
            case SFML_Event::Closed:
                pNewEvent = EventPtr(new Event(Event::QUIT, Event::NONE));
                break;
            case SFML_Event::Resized:
                break;
/*            case SDL_SYSWMEVENT:                                                       // generelle fehler events gibt es nciht in sfml
                {
#if defined(HAVE_XI2_1) || defined(HAVE_XI2_2) 
                    SDL_SysWMmsg* pMsg = sdlEvent.syswm.msg;
                    AVG_ASSERT(pMsg->subsystem == SDL_SYSWM_X11);
                    if (m_pXIMTInputDevice) {
                        m_pXIMTInputDevice->handleXIEvent(pMsg->event.xevent);
                    }
#endif
                }
                break; */
            default:
                // Ignore unknown events.
                break;
        }
        if (pNewEvent) {
            events.push_back(pNewEvent);
        }
    }
    return  ;
}

void SFMLDisplayEngine::setXIMTInputDevice(XInputMTInputDevice* pInputDevice)
{
    AVG_ASSERT(!m_pXIMTInputDevice);
    m_pXIMTInputDevice = pInputDevice;
}

EventPtr SFMLDisplayEngine::createMouseEvent(Event::Type type, const SFML_Event& sfmlEvent,
        long button)
{
    int x = m_input.GetMouseX();
    int y = m_input.GetMouseY();

//    Uint8 buttonState = SDL_GetMouseState(&x, &y); //  ?? buttonState jetzt andaers abfragen
    x = int((x*m_Size.x)/m_WindowSize.x);
    y = int((y*m_Size.y)/m_WindowSize.y);
    glm::vec2 lastMousePos = m_pLastMouseEvent->getPos();
    glm::vec2 speed;
    if (lastMousePos.x == -1) {
        speed = glm::vec2(0,0);
    } else {
        float lastFrameTime = 1000/getEffectiveFramerate();
        speed = glm::vec2(x-lastMousePos.x, y-lastMousePos.y)/lastFrameTime;
    }
    MouseEventPtr pEvent(new MouseEvent(type, m_input.IsMouseButtonDown(sf::Mouse::Left),
            m_input.IsMouseButtonDown(sf::Mouse::Middle), m_input.IsMouseButtonDown(sf::Mouse::Right),
            IntPoint(x, y), button, speed));

    m_pLastMouseEvent = pEvent;
    return pEvent; 
}

EventPtr SFMLDisplayEngine::createMouseButtonEvent(Event::Type type, 
        const SFML_Event& sfmlEvent) 
{
    long button = 0;
    switch (sfmlEvent.button.button) {
        case SDL_BUTTON_LEFT:
            button = MouseEvent::LEFT_BUTTON;
            break;
        case SDL_BUTTON_MIDDLE:
            button = MouseEvent::MIDDLE_BUTTON;
            break;
        case SDL_BUTTON_RIGHT:
            button = MouseEvent::RIGHT_BUTTON;
            break;
        case SDL_BUTTON_WHEELUP:
            button = MouseEvent::WHEELUP_BUTTON;
            break;
        case SDL_BUTTON_WHEELDOWN:
            button = MouseEvent::WHEELDOWN_BUTTON;
            break;
    }
    return createMouseEvent(type, sfmlEvent, button);
 
}

/*
EventPtr SFMLDisplayEngine::createAxisEvent(const SDL_Event & sdlEvent)
{
    return new AxisEvent(sdlEvent.jaxis.which, sdlEvent.jaxis.axis,
                sdlEvent.jaxis.value);
}


EventPtr SFMLDisplayEngine::createButtonEvent
        (Event::Type type, const SDL_Event & sdlEvent) 
{
    return new ButtonEvent(type, sdlEvent.jbutton.which,
                sdlEvent.jbutton.button));
}
*/

EventPtr SFMLDisplayEngine::createKeyEvent(Event::Type type, const SDL_Event& sdlEvent)
{
    long keyCode = KeyCodeTranslationTable[sdlEvent.key.keysym.sym];
    unsigned int modifiers = key::KEYMOD_NONE;

    if (sdlEvent.key.keysym.mod & KMOD_LSHIFT) 
        { modifiers |= key::KEYMOD_LSHIFT; }
    if (sdlEvent.key.keysym.mod & KMOD_RSHIFT) 
        { modifiers |= key::KEYMOD_RSHIFT; }
    if (sdlEvent.key.keysym.mod & KMOD_LCTRL) 
        { modifiers |= key::KEYMOD_LCTRL; }
    if (sdlEvent.key.keysym.mod & KMOD_RCTRL) 
        { modifiers |= key::KEYMOD_RCTRL; }
    if (sdlEvent.key.keysym.mod & KMOD_LALT) 
        { modifiers |= key::KEYMOD_LALT; }
    if (sdlEvent.key.keysym.mod & KMOD_RALT) 
        { modifiers |= key::KEYMOD_RALT; }
    if (sdlEvent.key.keysym.mod & KMOD_LMETA) 
        { modifiers |= key::KEYMOD_LMETA; }
    if (sdlEvent.key.keysym.mod & KMOD_RMETA) 
        { modifiers |= key::KEYMOD_RMETA; }
    if (sdlEvent.key.keysym.mod & KMOD_NUM) 
        { modifiers |= key::KEYMOD_NUM; }
    if (sdlEvent.key.keysym.mod & KMOD_CAPS) 
        { modifiers |= key::KEYMOD_CAPS; }
    if (sdlEvent.key.keysym.mod & KMOD_MODE) 
        { modifiers |= key::KEYMOD_MODE; }
    if (sdlEvent.key.keysym.mod & KMOD_RESERVED) 
        { modifiers |= key::KEYMOD_RESERVED; }

    KeyEventPtr pEvent(new KeyEvent(type,
            sdlEvent.key.keysym.scancode, keyCode,
            SDL_GetKeyName(sdlEvent.key.keysym.sym), sdlEvent.key.keysym.unicode, modifiers));
    return pEvent;
}

void SFMLDisplayEngine::initTranslationTable()
{
#define TRANSLATION_ENTRY(x) KeyCodeTranslationTable[SDLK_##x] = key::KEY_##x;

    TRANSLATION_ENTRY(UNKNOWN);
    TRANSLATION_ENTRY(BACKSPACE);
    TRANSLATION_ENTRY(TAB);
    TRANSLATION_ENTRY(CLEAR);
    TRANSLATION_ENTRY(RETURN);
    TRANSLATION_ENTRY(PAUSE);
    TRANSLATION_ENTRY(ESCAPE);
    TRANSLATION_ENTRY(SPACE);
    TRANSLATION_ENTRY(EXCLAIM);
    TRANSLATION_ENTRY(QUOTEDBL);
    TRANSLATION_ENTRY(HASH);
    TRANSLATION_ENTRY(DOLLAR);
    TRANSLATION_ENTRY(AMPERSAND);
    TRANSLATION_ENTRY(QUOTE);
    TRANSLATION_ENTRY(LEFTPAREN);
    TRANSLATION_ENTRY(RIGHTPAREN);
    TRANSLATION_ENTRY(ASTERISK);
    TRANSLATION_ENTRY(PLUS);
    TRANSLATION_ENTRY(COMMA);
    TRANSLATION_ENTRY(MINUS);
    TRANSLATION_ENTRY(PERIOD);
    TRANSLATION_ENTRY(SLASH);
    TRANSLATION_ENTRY(0);
    TRANSLATION_ENTRY(1);
    TRANSLATION_ENTRY(2);
    TRANSLATION_ENTRY(3);
    TRANSLATION_ENTRY(4);
    TRANSLATION_ENTRY(5);
    TRANSLATION_ENTRY(6);
    TRANSLATION_ENTRY(7);
    TRANSLATION_ENTRY(8);
    TRANSLATION_ENTRY(9);
    TRANSLATION_ENTRY(COLON);
    TRANSLATION_ENTRY(SEMICOLON);
    TRANSLATION_ENTRY(LESS);
    TRANSLATION_ENTRY(EQUALS);
    TRANSLATION_ENTRY(GREATER);
    TRANSLATION_ENTRY(QUESTION);
    TRANSLATION_ENTRY(AT);
    TRANSLATION_ENTRY(LEFTBRACKET);
    TRANSLATION_ENTRY(BACKSLASH);
    TRANSLATION_ENTRY(RIGHTBRACKET);
    TRANSLATION_ENTRY(CARET);
    TRANSLATION_ENTRY(UNDERSCORE);
    TRANSLATION_ENTRY(BACKQUOTE);
    TRANSLATION_ENTRY(a);
    TRANSLATION_ENTRY(b);
    TRANSLATION_ENTRY(c);
    TRANSLATION_ENTRY(d);
    TRANSLATION_ENTRY(e);
    TRANSLATION_ENTRY(f);
    TRANSLATION_ENTRY(g);
    TRANSLATION_ENTRY(h);
    TRANSLATION_ENTRY(i);
    TRANSLATION_ENTRY(j);
    TRANSLATION_ENTRY(k);
    TRANSLATION_ENTRY(l);
    TRANSLATION_ENTRY(m);
    TRANSLATION_ENTRY(n);
    TRANSLATION_ENTRY(o);
    TRANSLATION_ENTRY(p);
    TRANSLATION_ENTRY(q);
    TRANSLATION_ENTRY(r);
    TRANSLATION_ENTRY(s);
    TRANSLATION_ENTRY(t);
    TRANSLATION_ENTRY(u);
    TRANSLATION_ENTRY(v);
    TRANSLATION_ENTRY(w);
    TRANSLATION_ENTRY(x);
    TRANSLATION_ENTRY(y);
    TRANSLATION_ENTRY(z);
    TRANSLATION_ENTRY(DELETE);
    TRANSLATION_ENTRY(WORLD_0);
    TRANSLATION_ENTRY(WORLD_1);
    TRANSLATION_ENTRY(WORLD_2);
    TRANSLATION_ENTRY(WORLD_3);
    TRANSLATION_ENTRY(WORLD_4);
    TRANSLATION_ENTRY(WORLD_5);
    TRANSLATION_ENTRY(WORLD_6);
    TRANSLATION_ENTRY(WORLD_7);
    TRANSLATION_ENTRY(WORLD_8);
    TRANSLATION_ENTRY(WORLD_9);
    TRANSLATION_ENTRY(WORLD_10);
    TRANSLATION_ENTRY(WORLD_11);
    TRANSLATION_ENTRY(WORLD_12);
    TRANSLATION_ENTRY(WORLD_13);
    TRANSLATION_ENTRY(WORLD_14);
    TRANSLATION_ENTRY(WORLD_15);
    TRANSLATION_ENTRY(WORLD_16);
    TRANSLATION_ENTRY(WORLD_17);
    TRANSLATION_ENTRY(WORLD_18);
    TRANSLATION_ENTRY(WORLD_19);
    TRANSLATION_ENTRY(WORLD_20);
    TRANSLATION_ENTRY(WORLD_21);
    TRANSLATION_ENTRY(WORLD_22);
    TRANSLATION_ENTRY(WORLD_23);
    TRANSLATION_ENTRY(WORLD_24);
    TRANSLATION_ENTRY(WORLD_25);
    TRANSLATION_ENTRY(WORLD_26);
    TRANSLATION_ENTRY(WORLD_27);
    TRANSLATION_ENTRY(WORLD_28);
    TRANSLATION_ENTRY(WORLD_29);
    TRANSLATION_ENTRY(WORLD_30);
    TRANSLATION_ENTRY(WORLD_31);
    TRANSLATION_ENTRY(WORLD_32);
    TRANSLATION_ENTRY(WORLD_33);
    TRANSLATION_ENTRY(WORLD_34);
    TRANSLATION_ENTRY(WORLD_35);
    TRANSLATION_ENTRY(WORLD_36);
    TRANSLATION_ENTRY(WORLD_37);
    TRANSLATION_ENTRY(WORLD_38);
    TRANSLATION_ENTRY(WORLD_39);
    TRANSLATION_ENTRY(WORLD_40);
    TRANSLATION_ENTRY(WORLD_41);
    TRANSLATION_ENTRY(WORLD_42);
    TRANSLATION_ENTRY(WORLD_43);
    TRANSLATION_ENTRY(WORLD_44);
    TRANSLATION_ENTRY(WORLD_45);
    TRANSLATION_ENTRY(WORLD_46);
    TRANSLATION_ENTRY(WORLD_47);
    TRANSLATION_ENTRY(WORLD_48);
    TRANSLATION_ENTRY(WORLD_49);
    TRANSLATION_ENTRY(WORLD_50);
    TRANSLATION_ENTRY(WORLD_51);
    TRANSLATION_ENTRY(WORLD_52);
    TRANSLATION_ENTRY(WORLD_53);
    TRANSLATION_ENTRY(WORLD_54);
    TRANSLATION_ENTRY(WORLD_55);
    TRANSLATION_ENTRY(WORLD_56);
    TRANSLATION_ENTRY(WORLD_57);
    TRANSLATION_ENTRY(WORLD_58);
    TRANSLATION_ENTRY(WORLD_59);
    TRANSLATION_ENTRY(WORLD_60);
    TRANSLATION_ENTRY(WORLD_61);
    TRANSLATION_ENTRY(WORLD_62);
    TRANSLATION_ENTRY(WORLD_63);
    TRANSLATION_ENTRY(WORLD_64);
    TRANSLATION_ENTRY(WORLD_65);
    TRANSLATION_ENTRY(WORLD_66);
    TRANSLATION_ENTRY(WORLD_67);
    TRANSLATION_ENTRY(WORLD_68);
    TRANSLATION_ENTRY(WORLD_69);
    TRANSLATION_ENTRY(WORLD_70);
    TRANSLATION_ENTRY(WORLD_71);
    TRANSLATION_ENTRY(WORLD_72);
    TRANSLATION_ENTRY(WORLD_73);
    TRANSLATION_ENTRY(WORLD_74);
    TRANSLATION_ENTRY(WORLD_75);
    TRANSLATION_ENTRY(WORLD_76);
    TRANSLATION_ENTRY(WORLD_77);
    TRANSLATION_ENTRY(WORLD_78);
    TRANSLATION_ENTRY(WORLD_79);
    TRANSLATION_ENTRY(WORLD_80);
    TRANSLATION_ENTRY(WORLD_81);
    TRANSLATION_ENTRY(WORLD_82);
    TRANSLATION_ENTRY(WORLD_83);
    TRANSLATION_ENTRY(WORLD_84);
    TRANSLATION_ENTRY(WORLD_85);
    TRANSLATION_ENTRY(WORLD_86);
    TRANSLATION_ENTRY(WORLD_87);
    TRANSLATION_ENTRY(WORLD_88);
    TRANSLATION_ENTRY(WORLD_89);
    TRANSLATION_ENTRY(WORLD_90);
    TRANSLATION_ENTRY(WORLD_91);
    TRANSLATION_ENTRY(WORLD_92);
    TRANSLATION_ENTRY(WORLD_93);
    TRANSLATION_ENTRY(WORLD_94);
    TRANSLATION_ENTRY(WORLD_95);
    TRANSLATION_ENTRY(KP0);
    TRANSLATION_ENTRY(KP1);
    TRANSLATION_ENTRY(KP2);
    TRANSLATION_ENTRY(KP3);
    TRANSLATION_ENTRY(KP4);
    TRANSLATION_ENTRY(KP5);
    TRANSLATION_ENTRY(KP6);
    TRANSLATION_ENTRY(KP7);
    TRANSLATION_ENTRY(KP8);
    TRANSLATION_ENTRY(KP9);
    TRANSLATION_ENTRY(KP_PERIOD);
    TRANSLATION_ENTRY(KP_DIVIDE);
    TRANSLATION_ENTRY(KP_MULTIPLY);
    TRANSLATION_ENTRY(KP_MINUS);
    TRANSLATION_ENTRY(KP_PLUS);
    TRANSLATION_ENTRY(KP_ENTER);
    TRANSLATION_ENTRY(KP_EQUALS);
    TRANSLATION_ENTRY(UP);
    TRANSLATION_ENTRY(DOWN);
    TRANSLATION_ENTRY(RIGHT);
    TRANSLATION_ENTRY(LEFT);
    TRANSLATION_ENTRY(INSERT);
    TRANSLATION_ENTRY(HOME);
    TRANSLATION_ENTRY(END);
    TRANSLATION_ENTRY(PAGEUP);
    TRANSLATION_ENTRY(PAGEDOWN);
    TRANSLATION_ENTRY(F1);
    TRANSLATION_ENTRY(F2);
    TRANSLATION_ENTRY(F3);
    TRANSLATION_ENTRY(F4);
    TRANSLATION_ENTRY(F5);
    TRANSLATION_ENTRY(F6);
    TRANSLATION_ENTRY(F7);
    TRANSLATION_ENTRY(F8);
    TRANSLATION_ENTRY(F9);
    TRANSLATION_ENTRY(F10);
    TRANSLATION_ENTRY(F11);
    TRANSLATION_ENTRY(F12);
    TRANSLATION_ENTRY(F13);
    TRANSLATION_ENTRY(F14);
    TRANSLATION_ENTRY(F15);
    TRANSLATION_ENTRY(NUMLOCK);
    TRANSLATION_ENTRY(CAPSLOCK);
    TRANSLATION_ENTRY(SCROLLOCK);
    TRANSLATION_ENTRY(RSHIFT);
    TRANSLATION_ENTRY(LSHIFT);
    TRANSLATION_ENTRY(RCTRL);
    TRANSLATION_ENTRY(LCTRL);
    TRANSLATION_ENTRY(RALT);
    TRANSLATION_ENTRY(LALT);
    TRANSLATION_ENTRY(RMETA);
    TRANSLATION_ENTRY(LMETA);
    TRANSLATION_ENTRY(LSUPER);
    TRANSLATION_ENTRY(RSUPER);
    TRANSLATION_ENTRY(MODE);
    TRANSLATION_ENTRY(COMPOSE);
    TRANSLATION_ENTRY(HELP);
    TRANSLATION_ENTRY(PRINT);
    TRANSLATION_ENTRY(SYSREQ);
    TRANSLATION_ENTRY(BREAK);
    TRANSLATION_ENTRY(MENU);
    TRANSLATION_ENTRY(POWER);
    TRANSLATION_ENTRY(EURO);
    TRANSLATION_ENTRY(UNDO);
}

const IntPoint& SFMLDisplayEngine::getWindowSize() const
{
    return m_WindowSize;
}

bool SFMLDisplayEngine::isFullscreen() const
{
    return m_bIsFullscreen;
}

IntPoint SFMLDisplayEngine::getScreenResolution()
{
    calcScreenDimensions();
    return m_ScreenResolution;
}

float SFMLDisplayEngine::getPixelsPerMM()
{
    calcScreenDimensions();

    return m_PPMM;
}

glm::vec2 SFMLDisplayEngine::getPhysicalScreenDimensions()
{
    calcScreenDimensions();
    glm::vec2 size;
    glm::vec2 screenRes = glm::vec2(getScreenResolution());
    size.x = screenRes.x/m_PPMM;
    size.y = screenRes.y/m_PPMM;
    return size;
}

void SFMLDisplayEngine::assumePixelsPerMM(float ppmm)
{
    m_PPMM = ppmm;
}

}