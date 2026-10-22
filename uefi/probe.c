/* ClawOS Native UEFI firmware probe, both IA32 and x64.
 * IMPORTANT: This is a BOOT DIAGNOSTIC, not a complete kernel.
 * It runs directly under UEFI boot services. Not Linux, Node, or OpenClaw.
 */
#include <stdint.h>
#include <stddef.h>
#if defined(__x86_64__)
#define EFIAPI __attribute__((ms_abi))
#define ARCH "UEFI x64"
#else
#define EFIAPI __attribute__((stdcall))
#define ARCH "UEFI IA32"
#endif
typedef uintptr_t UINTN;
typedef UINTN EFI_STATUS;
typedef uint16_t CHAR16;
typedef void *EFI_HANDLE;
typedef struct {uint64_t Signature;uint32_t Revision,HeaderSize,CRC32,Reserved;} EFI_TABLE_HEADER;
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_TEXT_OUT;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
 void *Reset;
 EFI_STATUS (EFIAPI *OutputString)(EFI_TEXT_OUT *,const CHAR16 *);
 void *TestString,*QueryMode,*SetMode,*SetAttribute;
 EFI_STATUS (EFIAPI *ClearScreen)(EFI_TEXT_OUT *);
 void *SetCursorPosition,*EnableCursor,*Mode;
};
typedef struct {uint16_t ScanCode;CHAR16 UnicodeChar;} EFI_INPUT_KEY;
typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_TEXT_IN;
struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
 void *Reset;
 EFI_STATUS (EFIAPI *ReadKeyStroke)(EFI_TEXT_IN *,EFI_INPUT_KEY *);
 void *WaitForKey;
};
typedef struct {
 EFI_TABLE_HEADER Hdr;
 CHAR16 *FirmwareVendor;
 uint32_t FirmwareRevision;
 EFI_HANDLE ConsoleInHandle;
 EFI_TEXT_IN *ConIn;
 EFI_HANDLE ConsoleOutHandle;
 EFI_TEXT_OUT *ConOut;
 EFI_HANDLE StandardErrorHandle;
 EFI_TEXT_OUT *StdErr;
 void *RuntimeServices;
 void *BootServices;
 UINTN NumberOfTableEntries;
 void *ConfigurationTable;
} EFI_SYSTEM_TABLE;
static CHAR16 linebuffer[160];
static void say(EFI_TEXT_OUT*out,const char*msg){size_t i=0;while(msg[i]&&i<154){linebuffer[i]=(CHAR16)(uint8_t)msg[i];i++;}linebuffer[i++]='\r';linebuffer[i++]='\n';linebuffer[i]=0;out->OutputString(out,linebuffer);}
EFI_STATUS EFIAPI efi_main(EFI_HANDLE handle, EFI_SYSTEM_TABLE *system){
 (void)handle;
 if(!system||!system->ConOut)return 1;
 EFI_TEXT_OUT*o=system->ConOut;
 if(o->ClearScreen)o->ClearScreen(o);
 say(o,"========================================");
 say(o,"           ClawOS Native v0.2");
 say(o,"          EFI Boot Diagnostics");
 say(o,"========================================");
 say(o,"");
 say(o,"Firmware entry: " ARCH);
 say(o,"No Linux or macOS was loaded.");
 say(o,"EFI boot environment: AVAILABLE.");
 say(o,"Native kernel launch: NOT IMPLEMENTED for EFI.");
 say(o,"OpenClaw / Node / Network: NOT IMPLEMENTED.");
 say(o,"");
 say(o,"This probe makes no disk changes.");
 say(o,"Press a key to return to boot firmware.");
 if(system->ConIn&&system->ConIn->ReadKeyStroke){EFI_INPUT_KEY k;for(;;){if(system->ConIn->ReadKeyStroke(system->ConIn,&k)==0)break;}}
 return 0;
}
