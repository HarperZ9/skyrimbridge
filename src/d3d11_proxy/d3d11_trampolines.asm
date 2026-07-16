; d3d11_trampolines.asm — x64 MASM jump trampolines for D3D11Core* exports
;
; ENB exports exactly 6 D3D11 functions: 4 Core* + 2 Create*.
; The Create functions are implemented in C++ (proxy_main.cpp).
; The Core functions are pure forwarders via these ASM trampolines.
;
; DXGI calls D3D11CoreCreateDevice etc. internally during device creation.
; By the time these are called, LazyInit() has already run (from our
; D3D11CreateDeviceAndSwapChain) and populated the function pointers.

.data
EXTERN g_d3d11Original_D3D11CoreCreateDevice : QWORD
EXTERN g_d3d11Original_D3D11CoreCreateLayeredDevice : QWORD
EXTERN g_d3d11Original_D3D11CoreGetLayeredDeviceSize : QWORD
EXTERN g_d3d11Original_D3D11CoreRegisterLayers : QWORD

.code

Trampoline_D3D11CoreCreateDevice PROC
    jmp QWORD PTR [g_d3d11Original_D3D11CoreCreateDevice]
Trampoline_D3D11CoreCreateDevice ENDP

Trampoline_D3D11CoreCreateLayeredDevice PROC
    jmp QWORD PTR [g_d3d11Original_D3D11CoreCreateLayeredDevice]
Trampoline_D3D11CoreCreateLayeredDevice ENDP

Trampoline_D3D11CoreGetLayeredDeviceSize PROC
    jmp QWORD PTR [g_d3d11Original_D3D11CoreGetLayeredDeviceSize]
Trampoline_D3D11CoreGetLayeredDeviceSize ENDP

Trampoline_D3D11CoreRegisterLayers PROC
    jmp QWORD PTR [g_d3d11Original_D3D11CoreRegisterLayers]
Trampoline_D3D11CoreRegisterLayers ENDP

END
