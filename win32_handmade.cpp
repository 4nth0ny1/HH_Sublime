#include <windows.h>
#include <xinput.h>
#include <dsound.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define internal static 
#define local_persist static 
#define global_variable static

#define Pi32 3.14159265359f

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef int32 bool32;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef float real32;
typedef double real64;

#include "handmade.h"
#include "handmade.cpp"
#include "win32_handmade.h"

struct player {
    int x;
    int y;
    int width;
    int height;
    uint32 color;
};

typedef enum game_state {
    START_MENU,
    GAME
} game_state;

// TODO(casey): This is a global for now.
global_variable bool GlobalRunning;
global_variable win32_offscreen_buffer GlobalBackbuffer;
global_variable player GlobalPlayer;
global_variable LPDIRECTSOUNDBUFFER GlobalSecondaryBuffer;
global_variable game_state GlobalGameStateEnum = START_MENU;


// load the gamepad inputs ourselves without window's help, meaning we didn't add this to the library include settings
// NOTE(casey): XInputGetState
#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub) {
    return(ERROR_DEVICE_NOT_CONNECTED);
}
global_variable x_input_get_state* XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_

// NOTE(casey): XInputSetState
#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub) {
    return(ERROR_DEVICE_NOT_CONNECTED);
}
global_variable x_input_set_state* XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_


#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);


int grid[10][10] = {
    {1,1,1,1,1,1,1,1,1,1},
    {1,0,0,1,1,1,0,0,1,1},
    {1,0,0,0,1,1,0,0,0,1},
    {1,0,0,0,1,1,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,1,1,0,0,0,1},
    {1,0,0,0,1,1,0,0,0,1},
    {1,0,0,0,1,1,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1}
};

void *
PlatformLoadFile(char* Filename) {
    return(0);
}


internal void
Win32LoadXInput(void) {

    HMODULE XInputLibrary = LoadLibraryA("xinput1_3.dll");
    if (!XInputLibrary) {
        XInputLibrary = LoadLibraryA("xinput1_3.dll");
    }

    if (XInputLibrary) {
        XInputGetState = (x_input_get_state*)GetProcAddress(XInputLibrary, "XInputGetState");
        if (!XInputGetState) { XInputGetState = XInputGetStateStub; }

        XInputSetState = (x_input_set_state*)GetProcAddress(XInputLibrary, "XInputSetState");
        if (!XInputSetState) { XInputSetState = XInputSetStateStub; }
    }
}

internal void
Win32InitDSound(HWND Window, int32 SamplesPerSecond, int32 BufferSize) {
    // Always start from a known state
    GlobalSecondaryBuffer = 0;

    // Load dsound.dll
    HMODULE DSoundLibrary = LoadLibraryA("dsound.dll");
    if (!DSoundLibrary) {
        OutputDebugStringA("Win32InitDSound: LoadLibraryA(dsound.dll) FAILED.\n");
        return;
    }

    // Get DirectSoundCreate
    direct_sound_create* DirectSoundCreate =
        (direct_sound_create*)GetProcAddress(DSoundLibrary, "DirectSoundCreate");

    if (!DirectSoundCreate) {
        OutputDebugStringA("Win32InitDSound: GetProcAddress(DirectSoundCreate) FAILED.\n");
        return;
    }

    // Create DirectSound object
    LPDIRECTSOUND DirectSound = 0;
    HRESULT Error = DirectSoundCreate(0, &DirectSound, 0);
    if (FAILED(Error) || !DirectSound) {
        OutputDebugStringA("Win32InitDSound: DirectSoundCreate FAILED.\n");
        return;
    }

    // Wave format
    WAVEFORMATEX WaveFormat = {};
    WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
    WaveFormat.nChannels = 2;
    WaveFormat.nSamplesPerSec = (DWORD)SamplesPerSecond;
    WaveFormat.wBitsPerSample = 16;
    WaveFormat.nBlockAlign = (WaveFormat.nChannels * WaveFormat.wBitsPerSample) / 8;
    WaveFormat.nAvgBytesPerSec = WaveFormat.nSamplesPerSec * WaveFormat.nBlockAlign;
    WaveFormat.cbSize = 0;

    // Cooperative level
    Error = DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY);
    if (FAILED(Error)) {
        OutputDebugStringA("Win32InitDSound: SetCooperativeLevel FAILED.\n");
        return;
    }

    // Create and set PRIMARY buffer format
    {
        DSBUFFERDESC BufferDescription = {};
        BufferDescription.dwSize = sizeof(BufferDescription);
        BufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;

        LPDIRECTSOUNDBUFFER PrimaryBuffer = 0;
        Error = DirectSound->CreateSoundBuffer(&BufferDescription, &PrimaryBuffer, 0);
        if (SUCCEEDED(Error) && PrimaryBuffer) {
            Error = PrimaryBuffer->SetFormat(&WaveFormat);
            if (SUCCEEDED(Error)) {
                OutputDebugStringA("Win32InitDSound: Primary buffer format was set.\n");
            }
            else {
                OutputDebugStringA("Win32InitDSound: PrimaryBuffer->SetFormat FAILED.\n");
            }

            // Optional: release primary buffer (you usually don't need to keep it)
            PrimaryBuffer->Release();
            PrimaryBuffer = 0;
        }
        else {
            OutputDebugStringA("Win32InitDSound: CreateSoundBuffer PRIMARY FAILED.\n");
        }
    }

    // Create SECONDARY buffer and store it globally (this is the important fix)
    {
        DSBUFFERDESC BufferDescription = {};
        BufferDescription.dwSize = sizeof(BufferDescription);
        BufferDescription.dwFlags = 0;
        BufferDescription.dwBufferBytes = (DWORD)BufferSize;
        BufferDescription.lpwfxFormat = &WaveFormat;

        LPDIRECTSOUNDBUFFER SecondaryBuffer = 0;
        Error = DirectSound->CreateSoundBuffer(&BufferDescription, &SecondaryBuffer, 0);
        if (SUCCEEDED(Error) && SecondaryBuffer) {
            GlobalSecondaryBuffer = SecondaryBuffer; // <-- critical assignment
            OutputDebugStringA("Win32InitDSound: Secondary buffer created successfully.\n");
        }
        else {
            GlobalSecondaryBuffer = 0;
            OutputDebugStringA("Win32InitDSound: CreateSoundBuffer SECONDARY FAILED.\n");
        }
    }
}


win32_window_dimension Win32GetWindowDimension(HWND Window) {
    win32_window_dimension Result;

    RECT ClientRect;
    GetClientRect(Window, &ClientRect);
    Result.Width = ClientRect.right - ClientRect.left;
    Result.Height = ClientRect.bottom - ClientRect.top;

    return(Result);
}

internal void
DrawRectangle(win32_offscreen_buffer* Buffer, int MinX, int MinY, int MaxX, int MaxY, uint32 Color) {
    if (MinX < 0) {
        MinX = 0;
    }

    if (MinY < 0) {
        MinY = 0;
    }

    if (MaxX > Buffer->Width) {
        MaxX = Buffer->Width;
    }

    if (MaxY > Buffer->Height) {
        MaxY = Buffer->Height;
    }

    uint8* Row = ((uint8*)Buffer->Memory + MinX * Buffer->BytesPerPixel + MinY * Buffer->Pitch);
    for (int Y = MinY; Y < MaxY; ++Y) {
        uint32* Pixel = (uint32*)Row + MinX;
        for (int X = MinX; X < MaxX; ++X) {
            *Pixel++ = Color;
        }
        Row += Buffer->Pitch;
    }
}

internal void
ClearBackbuffer(win32_offscreen_buffer* Buffer, uint32 Color) {
    DrawRectangle(Buffer, 0, 0, Buffer->Width, Buffer->Height, Color);
}

internal void 
DrawTileMap(win32_offscreen_buffer* Buffer, int grid[10][10], uint32 c1, uint32 c2) {

    int tileSize = 160; 

    for (int row = 0; row < 10; row += 1) {
        for (int col = 0; col < 10; col += 1) {
            int value = grid[row][col];                     // get the value of the map spot
            uint32 color = (value == 1) ? c1 : c2;          // assign the color or texture when ready 

            int left = col * tileSize;                      // create the coordinates 
            int top = row * 120;                            
            int right = left + tileSize;
            int bottom = top + 120;                         

            DrawRectangle(Buffer, left, top, right, bottom, color); // create the Rectangle
        }
    }
}

internal void 
DrawPlayer(win32_offscreen_buffer* Buffer, player* p) {

    int right = p->x + p->width;
    int bottom = p->y + p->height;

    DrawRectangle(Buffer, p->x, p->y, right, bottom, p->color);
}

internal void
Win32DrawTextOverlay(HWND Window, int X, int Y, const char* Text, COLORREF Color) {
    HDC DC = GetDC(Window);

    SetBkMode(DC, TRANSPARENT);
    SetTextColor(DC, Color);

    TextOutA(DC, X, Y, Text, lstrlenA(Text));

    ReleaseDC(Window, DC);
}


internal void 
ProcessPlayerInput() {

    bool ReturnKeyIsDown;
    bool LeftKeyIsDown;
    bool RightKeyIsDown;
    bool UpKeyIsDown;
    bool DownKeyIsDown;
    bool LeftKeyWasDown;
    bool RightKeyWasDown;
    bool UpKeyWasDown;
    bool DownKeyWasDown;
    bool ReturnKeyWasDown;

    switch (GlobalGameStateEnum) {
        case START_MENU:
            ReturnKeyIsDown = (GetAsyncKeyState(VK_RETURN) & 0x8000);

            if (ReturnKeyIsDown) {
                GlobalGameStateEnum = GAME;
            }
        break;
        case GAME:
            LeftKeyIsDown = (GetAsyncKeyState(VK_LEFT) & 0x8000) || (GetAsyncKeyState('A') & 0x8000);
            RightKeyIsDown = (GetAsyncKeyState(VK_RIGHT) & 0x8000) || (GetAsyncKeyState('D') & 0x8000);
            UpKeyIsDown = (GetAsyncKeyState(VK_UP) & 0x8000) || (GetAsyncKeyState('W') & 0x8000);
            DownKeyIsDown = (GetAsyncKeyState(VK_DOWN) & 0x8000) || (GetAsyncKeyState('S') & 0x8000);

            for (DWORD ControllerIndex = 0;
                             ControllerIndex < XUSER_MAX_COUNT;
                             ++ControllerIndex) {
                XINPUT_STATE ControllerState;
                if (XInputGetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS) {
                    // NOTE(casey): This controller is plugged in
                    // TODO(casey): See if ControllerState.dwPacketNumber increments too rapidly
                    XINPUT_GAMEPAD* Pad = &ControllerState.Gamepad;

                    UpKeyIsDown |= (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
                    DownKeyIsDown |= (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
                    LeftKeyIsDown |= (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
                    RightKeyIsDown |= (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
                    bool Start = (Pad->wButtons & XINPUT_GAMEPAD_START);
                    bool Back = (Pad->wButtons & XINPUT_GAMEPAD_BACK);
                    bool LeftShoulder = (Pad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
                    bool RightShoulder = (Pad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
                    bool AButton = (Pad->wButtons & XINPUT_GAMEPAD_A);
                    bool BButton = (Pad->wButtons & XINPUT_GAMEPAD_B);
                    bool XButton = (Pad->wButtons & XINPUT_GAMEPAD_X);
                    bool YButton = (Pad->wButtons & XINPUT_GAMEPAD_Y);

                    int16 StickX = Pad->sThumbLX;
                    int16 StickY = Pad->sThumbLY;

                }
                else {
                    // NOTE(casey): The controller is not available
                }
            }

            if (LeftKeyIsDown) {
                GlobalPlayer.x--;
            }
            if (RightKeyIsDown) {
                GlobalPlayer.x++;
            }
            if (UpKeyIsDown) {
                GlobalPlayer.y--;
            }
            if (DownKeyIsDown) {
                GlobalPlayer.y++;
            }

            LeftKeyWasDown = LeftKeyIsDown;
            RightKeyWasDown = RightKeyIsDown;
            UpKeyWasDown = UpKeyIsDown;
            DownKeyWasDown = DownKeyIsDown;
        break;
        default:
            OutputDebugStringA("not a valid state");
        break;
    }

    
}

internal void
Win32ResizeDIBSection(win32_offscreen_buffer* Buffer, int Width, int Height) {
    // TODO(casey): Bulletproof this.
    // Maybe don't free first, free after, then free first if that fails.

    if (Buffer->Memory) {
        VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }

    Buffer->Width = Width;
    Buffer->Height = Height;

    int BytesPerPixel = 4;

    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height;
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    // NOTE(casey): Thank you to Chris Hecker of Spy Party fame
    // for clarifying the deal with StretchDIBits and BitBlt!
    // No more DC for us.
    int BitmapMemorySize = (Buffer->Width * Buffer->Height) * BytesPerPixel;
    Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);
    Buffer->Pitch = Width * BytesPerPixel;

    // TODO(casey): Probably clear this to black
}

internal void
Win32DisplayBufferInWindow(HDC DeviceContext,
                           int WindowWidth, int WindowHeight,
                           win32_offscreen_buffer Buffer) {
    // TODO(casey): Aspect ratio correction
    // TODO(casey): Play with stretch modes
    StretchDIBits(DeviceContext,
                  /*
                  X, Y, Width, Height,
                  X, Y, Width, Height,
                  */
                  0, 0, WindowWidth, WindowHeight,
                  0, 0, Buffer.Width, Buffer.Height,
                  Buffer.Memory,
                  &Buffer.Info,
                  DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK
Win32MainWindowCallback(HWND Window,
                        UINT Message,
                        WPARAM WParam,
                        LPARAM LParam) {
    LRESULT Result = 0;

    switch (Message) {
        case WM_CLOSE:
        {
            // TODO(casey): Handle this with a message to the user?
            GlobalRunning = false;
        } break;

        case WM_ACTIVATEAPP:
        {
            OutputDebugStringA("WM_ACTIVATEAPP\n");
        } break;

        case WM_DESTROY:
        {
            // TODO(casey): Handle this as an error - recreate window?
            GlobalRunning = false;
        } break;

        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            uint32 VKCode = WParam;
            bool WasDown = ((LParam & (1 << 30)) != 0);
            bool IsDown = ((LParam & (1 << 31)) == 0);
            if (WasDown != IsDown) {
                if (VKCode == 'W') {
                }
                else if (VKCode == 'A') {
                }
                else if (VKCode == 'S') {
                }
                else if (VKCode == 'D') {
                }
                else if (VKCode == 'Q') {
                }
                else if (VKCode == 'E') {
                }
                else if (VKCode == VK_UP) {
                }
                else if (VKCode == VK_LEFT) {
                }
                else if (VKCode == VK_DOWN) {
                }
                else if (VKCode == VK_RIGHT) {
                }
                else if (VKCode == VK_ESCAPE) {
                    OutputDebugStringA("ESCAPE: ");
                    if (IsDown) {
                        OutputDebugStringA("IsDown ");
                    }
                    if (WasDown) {
                        OutputDebugStringA("WasDown");
                    }
                    OutputDebugStringA("\n");
                }
                else if (VKCode == VK_SPACE) {
                }
            }

            bool32 AltKeyWasDown = (LParam & (1 << 29));
            if ((VKCode == VK_F4) && AltKeyWasDown) {
                GlobalRunning = false;
            }
        } break;

        case WM_PAINT:
        {
            PAINTSTRUCT Paint;
            HDC DeviceContext = BeginPaint(Window, &Paint);
            win32_window_dimension Dimension = Win32GetWindowDimension(Window);
            Win32DisplayBufferInWindow(
                DeviceContext,
                Dimension.Width,
                Dimension.Height,
                GlobalBackbuffer
            );
            EndPaint(Window, &Paint);
        } break;

        default:
        {
            // OutputDebugStringA("default\n");
            Result = DefWindowProc(Window, Message, WParam, LParam);
        } break;
    }

    return(Result);
}

internal void
Win32ClearBuffer(win32_sound_output* SoundOutput) {
    VOID* Region1;
    DWORD Region1Size;
    VOID* Region2;
    DWORD Region2Size;
    if (SUCCEEDED(GlobalSecondaryBuffer->Lock(0, SoundOutput->SecondaryBufferSize,
                                              &Region1, &Region1Size,
                                              &Region2, &Region2Size,
                                              0))) {
        // TODO(casey): assert that Region1Size/Region2Size is valid
        uint8* DestSample = (uint8*)Region1;
        for (DWORD ByteIndex = 0;
            ByteIndex < Region1Size;
            ++ByteIndex) {
            *DestSample++ = 0;
        }

        DestSample = (uint8*)Region2;
        for (DWORD ByteIndex = 0;
            ByteIndex < Region2Size;
            ++ByteIndex) {
            *DestSample++ = 0;
        }

        GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
    }
}

internal void
Win32FillSoundBuffer(win32_sound_output* SoundOutput, DWORD ByteToLock, DWORD BytesToWrite,
                     game_sound_output_buffer* SourceBuffer) {
    // TODO(casey): More strenuous test!
    VOID* Region1;
    DWORD Region1Size;
    VOID* Region2;
    DWORD Region2Size;
    if (SUCCEEDED(GlobalSecondaryBuffer->Lock(ByteToLock, BytesToWrite,
                                              &Region1, &Region1Size,
                                              &Region2, &Region2Size,
                                              0))) {
        // TODO(casey): assert that Region1Size/Region2Size is valid

        // TODO(casey): Collapse these two loops
        DWORD Region1SampleCount = Region1Size / SoundOutput->BytesPerSample;
        int16* DestSample = (int16*)Region1;
        int16* SourceSample = SourceBuffer->Samples;
        for (DWORD SampleIndex = 0;
            SampleIndex < Region1SampleCount;
            ++SampleIndex) {
            *DestSample++ = *SourceSample++;
            *DestSample++ = *SourceSample++;
            ++SoundOutput->RunningSampleIndex;
        }

        DWORD Region2SampleCount = Region2Size / SoundOutput->BytesPerSample;
        DestSample = (int16*)Region2;
        for (DWORD SampleIndex = 0;
            SampleIndex < Region2SampleCount;
            ++SampleIndex) {
            *DestSample++ = *SourceSample++;
            *DestSample++ = *SourceSample++;
            ++SoundOutput->RunningSampleIndex;
        }

        GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
    }
}


internal void
Win32ProcessXInputDigitalButton(DWORD XInputButtonState,
                                game_button_state* OldState, DWORD ButtonBit,
                                game_button_state* NewState) {
    NewState->EndedDown = ((XInputButtonState & ButtonBit) == ButtonBit);
    NewState->HalfTransitionCount = (OldState->EndedDown != NewState->EndedDown) ? 1 : 0;
}

int CALLBACK
WinMain(_In_ HINSTANCE Instance,
        _In_opt_ HINSTANCE PrevInstance,
        _In_ LPSTR CommandLine,
        _In_ int ShowCode) {


    LARGE_INTEGER PerfCountFrequencyResult;
    QueryPerformanceFrequency(&PerfCountFrequencyResult);
    int64 PerfCountFrequency = PerfCountFrequencyResult.QuadPart;

    Win32LoadXInput();

    WNDCLASS WindowClass = {};

    Win32ResizeDIBSection(&GlobalBackbuffer, 1280, 720);

    // TODO(casey): Check if HREDRAW/VREDRAW/OWNDC still matter
    WindowClass.lpfnWndProc = Win32MainWindowCallback;
    WindowClass.hInstance = Instance;
    //    WindowClass.hIcon;
    WindowClass.lpszClassName = "HandmadeHeroWindowClass";


    if (RegisterClassA(&WindowClass)) {
        HWND Window =
            CreateWindowExA(
                0,
                WindowClass.lpszClassName,
                "Handmade Hero",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                0,
                0,
                Instance,
                0);
        if (Window) {

            HDC DeviceContext = GetDC(Window);

            int XOffset = 0;
            int YOffset = 0;

            win32_sound_output SoundOutput = {};

            // TODO(casey): Make this like sixty seconds?
            SoundOutput.SamplesPerSecond = 48000;
            SoundOutput.BytesPerSample = sizeof(int16) * 2;
            SoundOutput.SecondaryBufferSize = SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample;
            SoundOutput.LatencySampleCount = SoundOutput.SamplesPerSecond / 15;
            Win32InitDSound(Window, SoundOutput.SamplesPerSecond, SoundOutput.SecondaryBufferSize);
            Win32ClearBuffer(&SoundOutput);
            GlobalSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);


            GlobalPlayer.x = 50;
            GlobalPlayer.y = 50;
            GlobalPlayer.width = 50;
            GlobalPlayer.height = 50;
            GlobalPlayer.color = 0x00FF69B4;

            GlobalRunning = true;

            // TODO(casey): Pool with bitmap VirtualAlloc
            int16* Samples = (int16*)VirtualAlloc(0, SoundOutput.SecondaryBufferSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

            game_input Input[2] = {};
            game_input* NewInput = &Input[0];
            game_input* OldInput = &Input[1];

            LARGE_INTEGER LastCounter;
            QueryPerformanceCounter(&LastCounter);
            int64 LastCycleCount = __rdtsc();

            while (GlobalRunning) {



                MSG Message;
                while (PeekMessage(&Message, 0, 0, 0, PM_REMOVE)) {
                    if (Message.message == WM_QUIT) {
                        GlobalRunning = false;
                    }

                    TranslateMessage(&Message);
                    DispatchMessageA(&Message);
                }


                // TODO(casey): Should we poll this more frequently
                int MaxControllerCount = XUSER_MAX_COUNT;
                if (MaxControllerCount > ArrayCount(NewInput->Controllers)) {
                    MaxControllerCount = ArrayCount(NewInput->Controllers);
                }


                for (DWORD ControllerIndex = 0;
                     ControllerIndex < MaxControllerCount;
                     ++ControllerIndex) {
                    game_controller_input* OldController = &OldInput->Controllers[ControllerIndex];
                    game_controller_input* NewController = &NewInput->Controllers[ControllerIndex];

                    XINPUT_STATE ControllerState;
                    if (XInputGetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS) {
                        // NOTE(casey): This controller is plugged in
                        // TODO(casey): See if ControllerState.dwPacketNumber increments too rapidly
                        XINPUT_GAMEPAD* Pad = &ControllerState.Gamepad;

                        // TODO(casey): DPad
                        bool32 Up = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
                        bool32 Down = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
                        bool32 Left = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
                        bool32 Right = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);

                        NewController->IsAnalog = true;
                        NewController->StartX = OldController->EndX;
                        NewController->StartY = OldController->EndY;

                        // TODO(casey): Dead zone processing!!
                        // XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
                        // XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 

                        // TODO(casey): Min/Max macros!!!
                        // TODO(casey): Collapse to single function
                        real32 X;
                        if (Pad->sThumbLX < 0) {
                            X = (real32)Pad->sThumbLX / 32768.0f;
                        }
                        else {
                            X = (real32)Pad->sThumbLX / 32767.0f;
                        }
                        NewController->MinX = NewController->MaxX = NewController->EndX = X;

                        real32 Y;
                        if (Pad->sThumbLY < 0) {
                            Y = (real32)Pad->sThumbLY / 32768.0f;
                        }
                        else {
                            Y = (real32)Pad->sThumbLY / 32767.0f;
                        }
                        NewController->MinY = NewController->MaxY = NewController->EndY = Y;

                        Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                        &OldController->Down, XINPUT_GAMEPAD_A,
                                                        &NewController->Down);
                        Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                        &OldController->Right, XINPUT_GAMEPAD_B,
                                                        &NewController->Right);
                        Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                        &OldController->Left, XINPUT_GAMEPAD_X,
                                                        &NewController->Left);
                        Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                        &OldController->Up, XINPUT_GAMEPAD_Y,
                                                        &NewController->Up);
                        Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                        &OldController->LeftShoulder, XINPUT_GAMEPAD_LEFT_SHOULDER,
                                                        &NewController->LeftShoulder);
                        Win32ProcessXInputDigitalButton(Pad->wButtons,
                                                        &OldController->RightShoulder, XINPUT_GAMEPAD_RIGHT_SHOULDER,
                                                        &NewController->RightShoulder);

                        // bool32 Start = (Pad->wButtons & XINPUT_GAMEPAD_START);
                        // bool32 Back = (Pad->wButtons & XINPUT_GAMEPAD_BACK);
                    }
                    else {
                        // NOTE(casey): The controller is not available
                    }
                }


                DWORD ByteToLock = 0;
                DWORD TargetCursor = 0;
                DWORD BytesToWrite = 0;
                DWORD PlayCursor = 0;
                DWORD WriteCursor = 0;
                bool32 SoundIsValid = false;
                // TODO(casey): Tighten up sound logic so that we know where we should be
                // writing to and can anticipate the time spent in the game update.
                if (SUCCEEDED(GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor))) {
                    ByteToLock = ((SoundOutput.RunningSampleIndex * SoundOutput.BytesPerSample) %
                                        SoundOutput.SecondaryBufferSize);

                    TargetCursor =
                        ((PlayCursor +
                          (SoundOutput.LatencySampleCount * SoundOutput.BytesPerSample)) %
                         SoundOutput.SecondaryBufferSize);
                    if (ByteToLock > TargetCursor) {
                        BytesToWrite = (SoundOutput.SecondaryBufferSize - ByteToLock);
                        BytesToWrite += TargetCursor;
                    }
                    else {
                        BytesToWrite = TargetCursor - ByteToLock;
                    }

                    SoundIsValid = true;
                }

                game_sound_output_buffer SoundBuffer = {};
                SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
                SoundBuffer.SampleCount = BytesToWrite / SoundOutput.BytesPerSample;
                SoundBuffer.Samples = Samples;

                game_offscreen_buffer Buffer = {};
                Buffer.Memory = GlobalBackbuffer.Memory;
                Buffer.Width = GlobalBackbuffer.Width;
                Buffer.Height = GlobalBackbuffer.Height;
                Buffer.Pitch = GlobalBackbuffer.Pitch;

                ProcessPlayerInput();

                // Rendering
                switch (GlobalGameStateEnum) {
                    case START_MENU:
                        // RenderWeirdGradient(GlobalBackbuffer, XOffset, YOffset);
                        GameUpdateAndRender(NewInput, &Buffer, &SoundBuffer);

                       // ClearBackbuffer(&GlobalBackbuffer, 0x00202020);
                    break;
                    case GAME:
                        //ClearBackbuffer(&GlobalBackbuffer, 0x00202020);
                        DrawTileMap(&GlobalBackbuffer, grid, 0x00202020, 0x0011FFEE);
                        DrawPlayer(&GlobalBackbuffer, &GlobalPlayer);
                        //DrawRectangle(&GlobalBackbuffer, 50, 50, 100, 100, 0x00202020);
                    break;
                    default:
                        OutputDebugStringA("Youre hitting default bro!");
                    break;
                }
                
                // NOTE(casey): DirectSound output test
                if (SoundIsValid) {
                    Win32FillSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite, &SoundBuffer);
                }
                
                HDC DeviceContext = GetDC(Window);
                win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                Win32DisplayBufferInWindow(DeviceContext, Dimension.Width, Dimension.Height, GlobalBackbuffer);
                ReleaseDC(Window, DeviceContext);

                //++XOffset;
                //YOffset += 2;

                // Draw text OVERLAY AFTER PRESENT so it is visible
                if (GlobalGameStateEnum == START_MENU) {
                    Win32DrawTextOverlay(Window, 20, 20, "START MENU", RGB(255, 255, 0));
                    Win32DrawTextOverlay(Window, 20, 45, "Press ENTER to Start", RGB(255, 255, 255));
                }
                else {
                    Win32DrawTextOverlay(Window, 20, 20, "GAME", RGB(0, 255, 255));
                }

         
    
                int64 EndCycleCount = __rdtsc();

                LARGE_INTEGER EndCounter;
                QueryPerformanceCounter(&EndCounter);

                int64 CyclesElapsed = EndCycleCount - LastCycleCount;
                int64 CounterElapsed = EndCounter.QuadPart - LastCounter.QuadPart;
                real64 MSPerFrame = (((1000.0f * (real64)CounterElapsed) / (real64)PerfCountFrequency));
                real64 FPS = (real64)PerfCountFrequency / (real64)CounterElapsed;
                real64 MCPF = ((real64)CyclesElapsed / (1000.0f * 1000.0f));

#if 0
                char Buffer[256];
                sprintf_s(Buffer, "%.02fms/f,  %.02ff/s,  %.02fmc/f\n", MSPerFrame, FPS, MCPF);
                OutputDebugStringA(Buffer);
                LastCounter = EndCounter;
#endif

                game_input* Temp = NewInput;
                NewInput = OldInput;
                OldInput = Temp;
                // TODO(casey): Should I clear these here?
            }
        }
        else {
            // TODO(casey): Logging
        }
    }
    else {
        // TODO(casey): Logging
    }

    return(0);
}
