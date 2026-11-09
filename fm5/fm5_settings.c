#include "fm5/fm5_settings.h"
#include "shared/support.h"
typedef struct {unsigned magic,version;int values[FM5_SAVED_COUNT];} Settings;
static int values[FM5_SAVED_COUNT];
/* Camera slot 12 is cockpit motion blur: enabled for a fresh settings file.
   A saved user choice remains authoritative. */
static const int defaults[FM5_SAVED_COUNT]={1,35,70,80,70,55,65,96,96,96,96,96,1,0,0,0,100,100,100,100,100,100,100,100,100,100,100,100,100,100};
static wchar_t path[PATH_CAPACITY],temporary[PATH_CAPACITY];
int *fm5_settings_values(void){return values;}
static int valid(unsigned i,int v){
    if(i==0)return v>=0 && v<=2;
    if(i<7)return v>=0 && v<=100;
    if(i<12)return v>=40 && v<=240;
    if(i<15)return v==0 || v==1;
    if(i==15)return v>=0;
    return v>=0 && v<=100;
}
void fm5_settings_save(void){
    if(!path[0] || !temporary[0])return;
    Settings state={0x35434f45,4,{0}};memory_copy(state.values,values,sizeof(values));
    HANDLE file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return;
    DWORD written=0;int ok=WriteFile(file,&state,sizeof(state),&written,NULL) && written==sizeof(state);CloseHandle(file);
    if(ok)MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);else DeleteFileW(temporary);
}
void fm5_settings_load(const wchar_t *directory){
    memory_copy(values,defaults,sizeof(values));path[0]=temporary[0]=0;
    if(!path_join(path,directory,L"FM5_ExtraOptions.dat") || !path_join(temporary,directory,L"FM5_ExtraOptions.dat.tmp")){path[0]=0;return;}
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return;
    Settings state={0};DWORD read=0;LARGE_INTEGER size;
    int ok=GetFileSizeEx(file,&size) && (size.QuadPart==sizeof(state) || size.QuadPart==72) && ReadFile(file,&state,sizeof(state),&read,NULL) && read==size.QuadPart;CloseHandle(file);
    if(!ok || state.magic!=0x35434f45 || !((state.version==3 && read==72) || (state.version==4 && read==sizeof(state))))return;
    unsigned count=(read-8)/4;
    for(unsigned i=0;i<count;++i)if(!valid(i,state.values[i]))return;
    memory_copy(values,state.values,count*4);
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fm5TestSettingsLoad(const wchar_t *d){fm5_settings_load(d);}
__declspec(dllexport) void Fm5TestSettingsSave(void){fm5_settings_save();}
__declspec(dllexport) int *Fm5TestSettingsValues(void){return values;}
#endif
