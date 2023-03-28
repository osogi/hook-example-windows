// dllmain.cpp : Определяет точку входа для приложения DLL.


#include "pch.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <conio.h>
#include <cstdlib>
#include <ctime>

FILE* LogFile;
#define LOG_FILE "log.txt"

void log(const char* format_str, ...) {
    LogFile = fopen(LOG_FILE, "a");

    va_list args;
    va_start(args, format_str);
    vfprintf(LogFile, format_str, args);
    va_end(args);

    fclose(LogFile);

}


#define STR_MERGE_IMPL(a, b) a##b
#define STR_MERGE(a, b) STR_MERGE_IMPL(a, b)
#define MAKE_PAD(size) STR_MERGE(_pad, __COUNTER__)[size]
#define CREATE_MEMBER(type, name, offset)               \
      struct {unsigned char MAKE_PAD(offset); type name;} 

INT_PTR BaseAddress = (INT_PTR)GetModuleHandleW(L"PongGame.exe");

struct Well;

typedef void(__thiscall* move_fnc_t)(Well*);
move_fnc_t wellMoveRight = (move_fnc_t)(BaseAddress + 0x04431C0);
move_fnc_t wellMoveLeft =  (move_fnc_t)(BaseAddress + 0x04430C0);


typedef void(__thiscall* rotate_fnc_t)(Well*, BYTE);
rotate_fnc_t wellRotate= (rotate_fnc_t)(BaseAddress + 0x04432D0);


BYTE JMP_PATTERN64[] = {
    0x68, 0xcb, 0x12, 0x47, 0x23,     // 0  push 0x234712cb */
    0xc7, 0x44, 0x24, 0x04,           // 5
    0xf6, 0x7f, 0x00, 0x00,           //    mov dword [rsp + 4], 0x7ff6 
    0xc3                              //13  ret
};

BYTE JMP_PATTERN32[] = {
0xe9, 0xcb, 0x12, 0x47, 0x23,        /* 0 jmp <...> */ 
};


bool is32bit() {
    return sizeof(void*) == 4;
}

UINT32 len_jmp_pattern() {
    if(is32bit())
        return sizeof(JMP_PATTERN32);
    else
        return sizeof(JMP_PATTERN64);
}

void add_jmp64(BYTE* dst, INT_PTR jmp_addr) {
    memcpy(dst, JMP_PATTERN64, sizeof(JMP_PATTERN64));
    UINT32 first_half = jmp_addr & 0xffffffff;
    UINT32 second_half = (jmp_addr>>32) & 0xffffffff;
    *((UINT32*)(dst + 1)) = first_half;
    *((UINT32*)(dst + 5+4)) = second_half;
}

void add_jmp32(BYTE* dst, INT_PTR jmp_addr) {
    memcpy(dst, JMP_PATTERN32, sizeof(JMP_PATTERN32));
    *((UINT32*)(dst + 1)) = jmp_addr-(((UINT32)dst)+5);
}

void add_jmp(BYTE* dst, INT_PTR jmp_addr) {
    if (is32bit())
        add_jmp32(dst, jmp_addr);
    else
        add_jmp64(dst, jmp_addr);
}

BYTE* InjectAlloc(INT_PTR inject_address, UINT32 len_of_nop, BYTE* source, UINT32 len_of_source) {
    BYTE* res = (BYTE*)malloc(len_of_source + len_jmp_pattern());
    if (len_of_nop >= len_jmp_pattern()) {
        DWORD prev_protect;
        if (res) {
            VirtualProtect((BYTE*)inject_address, len_of_nop, PAGE_EXECUTE_READWRITE, &prev_protect);
            memset((BYTE*)inject_address, 0x90, len_of_nop);
            //заполняем его нопами
            add_jmp((BYTE*)inject_address, (INT_PTR)res);
            //добавляем прыжок на выделенную памяти
            VirtualProtect((BYTE*)inject_address, len_of_nop, prev_protect, &prev_protect);


            VirtualProtect(res, len_of_source + len_jmp_pattern(), PAGE_EXECUTE_READWRITE, &prev_protect);
            memcpy(res, source, len_of_source);
            //копируем в выделенную память нужные байты
            add_jmp(res + len_of_source, inject_address+ len_jmp_pattern());
            //добавляем прыжок назад (на иструкцию после прыжка) 
        }
    }
    else {
        log("No enough nops\n");
    }
    return res;
}


struct Well {
    union 
    {
        CREATE_MEMBER(INT8, x, 0xC90);
    };

    void moveRight() {
        wellMoveRight(this);
    }

    void moveLeft() {
        wellMoveLeft(this);
    }

    void rotate(BYTE direction) {
        direction &= 1;
        wellRotate(this, direction);
    }

    
};

void __fastcall randomStart(Well* well) {
    int pos = (std::rand() % 10) - 5;

    for (int i = 0; i < pos; i++)
        well->moveRight();
    for (int i = 0; i > pos; i--)
        well->moveLeft();

    int rotates = std::rand() % 4;
    for (int i = 0; i < rotates; i++)
        well->rotate(0);

    printf("New rand x pos: %hhd\n", well->x);
}

void InjectAddPiece() {
    int start_addr = 0x00443974;
    int end_addr =  0x00443979;

    BYTE inject_code[] = {
    //lost bytes
    0xb8, 0x14, 0x00, 0x00, 0x00,               //0 mov eax, 0x14 */
    //

    0x50, 0x51, 0x52, 0x53, 				    //5  #save state
    0x54, 0x55, 0x56, 0x57, 				    //9   #of regs

    0x89, 0xf1,                                 //13 mov ecx, esi */
    0xe8, 0x7f, 0x1e, 0xe2, 0x10,               //15 call 0x11223344 */

    0x5f, 0x5e, 0x5d, 0x5c,				        //20  #return state
    0x5b, 0x5a, 0x59, 0x58,			            //24    #of regs 
    };

    BYTE* res = InjectAlloc(BaseAddress+ start_addr, end_addr-start_addr, inject_code, sizeof(inject_code));
    add_jmp32(res+15, (INT_PTR)&randomStart);
    *(res + 15) = 0xe8; // switch jmp to call
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        std::srand(std::time(nullptr));
        InjectAddPiece();
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

