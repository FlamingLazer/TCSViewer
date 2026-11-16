// Read-only NVIDIA frame limiter diagnostic; never changes driver settings.
#include <windows.h>
#include <cstdio>
#include <initializer_list>
#include <nvapi.h>
#include <NvApiDriverSettings.h>
int main() {
 auto dll=LoadLibraryW(L"nvapi64.dll"); if(!dll)return 1;
 auto query=reinterpret_cast<void*(__cdecl*)(unsigned)>(GetProcAddress(dll,"nvapi_QueryInterface"));if(!query)return 2;
 auto init=reinterpret_cast<decltype(&NvAPI_Initialize)>(query(0x0150e828));
 auto create=reinterpret_cast<decltype(&NvAPI_DRS_CreateSession)>(query(0x0694d52e));
 auto destroy=reinterpret_cast<decltype(&NvAPI_DRS_DestroySession)>(query(0xdad9cff8));
 auto load=reinterpret_cast<decltype(&NvAPI_DRS_LoadSettings)>(query(0x375dbd6b));
 auto getBase=reinterpret_cast<decltype(&NvAPI_DRS_GetCurrentGlobalProfile)>(query(0x617bff9f));
 auto get=reinterpret_cast<decltype(&NvAPI_DRS_GetSetting)>(query(0x73bf8338));
 auto find=reinterpret_cast<decltype(&NvAPI_DRS_FindApplicationByName)>(query(0xeee566b2));
 printf("Initialize: %d\n",init()); NvDRSSessionHandle session=nullptr;
 if(create(&session)!=NVAPI_OK)return 3;
 printf("Load settings: %d\n",load(session));
 NvDRSProfileHandle profile=nullptr;printf("Base profile: %d\n",getBase(session,&profile));
 auto print=[&](const char* name) { for (unsigned id : {unsigned(FRL_FPS_ID),0x10835005u,unsigned(APPIDLE_DYNAMIC_FRL_FPS_ID),unsigned(APPIDLE_DYNAMIC_FRL_THRESHOLD_TIME_ID)}) {
  NVDRS_SETTING s{};s.version=NVDRS_SETTING_VER;auto result=get(session,profile,id,&s);printf("%s setting %08x: status=%d value=%u location=%d\n",name,id,result,s.u32CurrentValue,int(s.settingLocation));
 }};
 print("Global"); NvAPI_UnicodeString exe{};wcscpy_s(reinterpret_cast<wchar_t*>(exe),sizeof(exe)/sizeof(wchar_t),L"TCSViewer.exe");NVDRS_APPLICATION app{};app.version=NVDRS_APPLICATION_VER;
 auto found=find(session,exe,&profile,&app);printf("Viewer profile lookup: %d\n",found);if(found==NVAPI_OK)print("Viewer");
 destroy(session);return 0;
}


