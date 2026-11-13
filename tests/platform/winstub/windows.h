/* A stand-in for <windows.h>, just enough to type-check the GPU presenters
 * (src/platform/gpu_d3d12.c, gpu_vulkan.c, gpu_gl.c) on a computer without
 * Windows headers: tests/platform/gpu_decl_check.c. Not for building anything. */
#ifndef BR_WINSTUB_WINDOWS_H
#define BR_WINSTUB_WINDOWS_H
#include <stddef.h>
#include <stdint.h>
#define WINAPI
#define __stdcall                  /* (an x64 compiler ignores it anyway) */
#define __declspec(x)
#define STDMETHODCALLTYPE
#define TRUE 1
#define FALSE 0
typedef int BOOL;
typedef unsigned int UINT;
typedef int INT;
typedef uint32_t ULONG;            /* (32-bit, as on Windows) */
typedef int32_t LONG;
typedef uint32_t DWORD;
typedef int32_t HRESULT;
typedef uint64_t UINT64;
typedef uint16_t UINT16;
typedef uint8_t UINT8;
typedef size_t SIZE_T;
typedef float FLOAT;
typedef const char *LPCSTR;
typedef struct HWND__ *HWND;
typedef struct HINSTANCE__ *HINSTANCE;
typedef HINSTANCE HMODULE;
typedef void *HANDLE;
typedef struct HDC__ *HDC;
typedef struct HGLRC__ *HGLRC;
typedef intptr_t (WINAPI *FARPROC)(void);
typedef intptr_t (WINAPI *PROC)(void);
typedef struct { uint32_t Data1; uint16_t Data2, Data3; uint8_t Data4[8]; } GUID;
typedef struct { LONG left, top, right, bottom; } RECT;
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
HMODULE LoadLibraryA(LPCSTR);
FARPROC GetProcAddress(HMODULE, LPCSTR);
HANDLE CreateEventW(void *, BOOL, BOOL, const void *);
DWORD WaitForSingleObject(HANDLE, DWORD);
BOOL CloseHandle(HANDLE);
#endif
