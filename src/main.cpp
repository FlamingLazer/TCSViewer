#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
#include <avrt.h>
#include <wrl/client.h>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <uxtheme.h>
#include "obs_api.hpp"
#include "obs_discovery.hpp"
#include "hotkey.hpp"
#include "window_layout.hpp"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
static constexpr UINT WM_VIEWER_ERROR = WM_APP + 1;
static constexpr UINT WM_TEST_TOGGLE = WM_APP + 2;
static constexpr UINT WM_TEST_STOP = WM_APP + 3;
static constexpr UINT WM_TOGGLE_VIEWER = WM_APP + 4;
static std::mutex logMutex;
static std::ofstream logFile;
static std::ofstream timingFile;

static std::string Utf8(const std::wstring& s) {
 if (s.empty()) return {};
 int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
 std::string r(n, '\0');
 WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n, nullptr, nullptr);
 return r;
}
static std::wstring Wide(const std::string& s) {
 if (s.empty()) return {};
 int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
 std::wstring r(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n); return r;
}
static double ClockMs() {
 static LARGE_INTEGER f = [] { LARGE_INTEGER v; QueryPerformanceFrequency(&v); return v; }();
 LARGE_INTEGER n; QueryPerformanceCounter(&n); return double(n.QuadPart) * 1000.0 / f.QuadPart;
}
static void Log(const std::string& s) {
 std::lock_guard lock(logMutex);
 if (logFile) { logFile << std::fixed << std::setprecision(3) << ClockMs() << " " << s << '\n'; logFile.flush(); }
 OutputDebugStringA((s + "\n").c_str());
}
static void ObsLog(int level, const char* fmt, va_list args, void*) {
 if (level > LOG_DEBUG) return;
 char buf[4096]; vsnprintf(buf, sizeof(buf), fmt, args); Log(std::string("OBS: ") + buf);
}
static void Check(HRESULT hr, const char* what) {
 if (FAILED(hr)) { std::ostringstream s; s << what << " (HRESULT 0x" << std::hex << unsigned(hr) << ")"; throw std::runtime_error(s.str()); }
}
struct Monitor { HMONITOR handle; RECT rect; RECT work; std::wstring name; bool primary; };
static BOOL CALLBACK MonitorCallback(HMONITOR h, HDC, LPRECT, LPARAM value) {
 MONITORINFOEXW m{}; m.cbSize = sizeof(m); GetMonitorInfoW(h, &m);
 reinterpret_cast<std::vector<Monitor>*>(value)->push_back({h, m.rcMonitor, m.rcWork, m.szDevice, bool(m.dwFlags & MONITORINFOF_PRIMARY)}); return TRUE;
}
static std::vector<Monitor> Monitors() {
 std::vector<Monitor> result; EnumDisplayMonitors(nullptr, nullptr, MonitorCallback, reinterpret_cast<LPARAM>(&result));
 std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; }); return result;
}

struct Config {
 fs::path ini;
 std::wstring obsDir; // Empty means automatic discovery; explicit overrides remain available for QA.
 std::wstring exe = L"LEGOStarWarsSaga.exe";
 std::wstring monitor = L"\\\\.\\DISPLAY2";
 UINT modifiers = MOD_CONTROL | MOD_ALT, key = VK_F10;
 unsigned captureFps = 120;
 bool windowed=false;
 bool framedWindow=false,windowPlaced=false;
 int windowX=0,windowY=0;
 int windowSize=2,windowPosition=0;
 bool testPattern = false;
 void Read(const fs::path& path) {
  ini = path; wchar_t buf[1024];
  GetPrivateProfileStringW(L"viewer", L"obs_dir", obsDir.c_str(), buf, 1024, ini.c_str()); obsDir = buf;
  GetPrivateProfileStringW(L"viewer", L"game_exe", exe.c_str(), buf, 1024, ini.c_str()); exe = buf;
  GetPrivateProfileStringW(L"viewer", L"monitor", monitor.c_str(), buf, 1024, ini.c_str()); monitor = buf;
  modifiers = GetPrivateProfileIntW(L"viewer", L"hotkey_modifiers", modifiers, ini.c_str());
  key = GetPrivateProfileIntW(L"viewer", L"hotkey_vk", key, ini.c_str());
  captureFps = std::clamp(GetPrivateProfileIntW(L"viewer", L"capture_fps", 120, ini.c_str()), 30u, 240u);
  windowed=GetPrivateProfileIntW(L"viewer",L"windowed",0,ini.c_str())!=0;
  framedWindow=windowed && GetPrivateProfileIntW(L"viewer",L"framed_window",0,ini.c_str())!=0;
  windowPlaced=GetPrivateProfileIntW(L"viewer",L"window_placed",0,ini.c_str())!=0;
  windowX=int(GetPrivateProfileIntW(L"viewer",L"window_x",0,ini.c_str()));
  windowY=int(GetPrivateProfileIntW(L"viewer",L"window_y",0,ini.c_str()));
  windowSize=std::clamp(int(GetPrivateProfileIntW(L"viewer",L"window_size",2,ini.c_str())),0,3);
  windowPosition=std::clamp(int(GetPrivateProfileIntW(L"viewer",L"window_position",0,ini.c_str())),0,4);
 }
 void Save() {
  WritePrivateProfileStringW(L"viewer", L"obs_dir", obsDir.c_str(), ini.c_str());
  WritePrivateProfileStringW(L"viewer", L"game_exe", exe.c_str(), ini.c_str());
  WritePrivateProfileStringW(L"viewer", L"monitor", monitor.c_str(), ini.c_str());
  WritePrivateProfileStringW(L"viewer", L"hotkey_modifiers", std::to_wstring(modifiers).c_str(), ini.c_str());
  WritePrivateProfileStringW(L"viewer", L"hotkey_vk", std::to_wstring(key).c_str(), ini.c_str());
  WritePrivateProfileStringW(L"viewer", L"capture_fps", std::to_wstring(captureFps).c_str(), ini.c_str());
  WritePrivateProfileStringW(L"viewer",L"windowed",windowed?L"1":L"0",ini.c_str());
  WritePrivateProfileStringW(L"viewer",L"framed_window",framedWindow?L"1":L"0",ini.c_str());
  WritePrivateProfileStringW(L"viewer",L"window_placed",windowPlaced?L"1":L"0",ini.c_str());
  WritePrivateProfileStringW(L"viewer",L"window_x",std::to_wstring(windowX).c_str(),ini.c_str());
  WritePrivateProfileStringW(L"viewer",L"window_y",std::to_wstring(windowY).c_str(),ini.c_str());
  WritePrivateProfileStringW(L"viewer",L"window_size",std::to_wstring(windowSize).c_str(),ini.c_str());
  WritePrivateProfileStringW(L"viewer",L"window_position",std::to_wstring(windowPosition).c_str(),ini.c_str());
 }
};

// One keyed GPU texture is a mailbox, not a queue. Neither producer nor
// consumer waits for access: when busy, that update is skipped.
struct Bridge {
 ComPtr<ID3D11Device> device;
 ComPtr<ID3D11DeviceContext> context;
 ComPtr<ID3D11Texture2D> texture;
 ComPtr<IDXGIKeyedMutex> mutex;
 HANDLE shared = nullptr;
 unsigned width = 1920, height = 1080;
 std::atomic<uint64_t> serial{0};
 std::atomic<uint64_t> busy{0};
 std::atomic<double> publishedMs{0};
 void Create(ID3D11Device* d) {
  device = d; device->GetImmediateContext(&context);
  D3D11_TEXTURE2D_DESC t{}; t.Width = width; t.Height = height; t.MipLevels = t.ArraySize = 1;
  t.Format = DXGI_FORMAT_B8G8R8A8_UNORM; t.SampleDesc.Count = 1;
  t.Usage = D3D11_USAGE_DEFAULT; t.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  t.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
  Check(d->CreateTexture2D(&t, nullptr, &texture), "Create GPU mailbox");
  Check(texture.As(&mutex), "Create producer mutex");
  ComPtr<IDXGIResource> r; Check(texture.As(&r), "Get shared resource");
  Check(r->GetSharedHandle(&shared), "Get GPU mailbox handle");
 }
 void Reset() { mutex.Reset(); texture.Reset(); context.Reset(); device.Reset(); shared = nullptr; serial = 0; busy = 0; publishedMs = 0; }
};

class Presenter {
 Bridge& bridge;
 HWND window, owner;
 bool validatePixels;
 bool expectGray;
 std::thread thread;
 HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
 HANDLE refresh = CreateEventW(nullptr,FALSE,FALSE,nullptr);
 std::atomic<bool> forceRefresh{false};
 std::atomic<bool> running{false};
public:
 std::atomic<uint64_t> displayed{0};
 std::atomic<unsigned> surfaceWidth{0},surfaceHeight{0},surfaceGeneration{0};
 std::atomic<unsigned> resizedPresented{0};
 std::atomic<bool> resizedCoverageVerified{false};
 Presenter(Bridge& b, HWND w, HWND parent, bool validate, bool gray) : bridge(b), window(w), owner(parent), validatePixels(validate), expectGray(gray) {}
 ~Presenter() { Stop(); if (stop) CloseHandle(stop);if(refresh)CloseHandle(refresh); }
 void Refresh(bool force=true){if(force)forceRefresh=true;if(refresh)SetEvent(refresh);}
 void Start() {
  if (!stop || !refresh) throw std::runtime_error("Cannot create display events.");
  ResetEvent(stop); running = true;
  thread = std::thread([this] {
   DWORD task = 0; HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Games", &task);
   try { Run(); }
   catch (const std::exception& e) {
    Log(std::string("Presenter error: ") + e.what());
    auto* message = new std::wstring(Wide(e.what()));
    if (!PostMessageW(owner, WM_VIEWER_ERROR, 0, reinterpret_cast<LPARAM>(message))) delete message;
   }
   if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
   running = false;
  });
 }
 void Stop() { if (stop) SetEvent(stop); if (thread.joinable()) thread.join(); }
private:
 void Run() {
  ComPtr<IDXGIDevice> obsDxgi; Check(bridge.device.As(&obsDxgi), "Get capture adapter");
  ComPtr<IDXGIAdapter> adapter; Check(obsDxgi->GetAdapter(&adapter), "Get capture adapter");
  ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL feature;
  Check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
   nullptr, 0, D3D11_SDK_VERSION, &device, &feature, &context), "Create display device");
  ComPtr<IDXGIFactory2> factory; Check(adapter->GetParent(IID_PPV_ARGS(&factory)), "Get DXGI factory");
  RECT rc{}; GetClientRect(window, &rc);
  DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = rc.right; desc.Height = rc.bottom;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
  desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
  ComPtr<IDXGISwapChain1> swap;
  Check(factory->CreateSwapChainForHwnd(device.Get(), window, &desc, nullptr, nullptr, &swap), "Create display swap chain");
  factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
  ComPtr<IDXGISwapChain2> swap2; Check(swap.As(&swap2), "Get latency controls");
  Check(swap2->SetMaximumFrameLatency(1), "Set display queue depth");
  HANDLE ready = swap2->GetFrameLatencyWaitableObject();
  if (!ready) throw std::runtime_error("Display latency wait handle is unavailable.");
  struct Close { HANDLE h; ~Close() { CloseHandle(h); } } closeReady{ready};
  ComPtr<ID3D11Texture2D> back; Check(swap->GetBuffer(0, IID_PPV_ARGS(&back)), "Get display backbuffer");
  ComPtr<ID3D11RenderTargetView> rtv; Check(device->CreateRenderTargetView(back.Get(), nullptr, &rtv), "Create display target");
  surfaceWidth=desc.Width;surfaceHeight=desc.Height;surfaceGeneration=1;
  ComPtr<ID3D11Texture2D> shared; Check(device->OpenSharedResource(bridge.shared, IID_PPV_ARGS(&shared)), "Open GPU mailbox");
  ComPtr<IDXGIKeyedMutex> mutex; Check(shared.As(&mutex), "Get display mutex");
  D3D11_TEXTURE2D_DESC td; shared->GetDesc(&td); td.MiscFlags = 0; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> local; Check(device->CreateTexture2D(&td, nullptr, &local), "Create newest-frame texture");
  ComPtr<ID3D11ShaderResourceView> srv; Check(device->CreateShaderResourceView(local.Get(), nullptr, &srv), "Create display texture view");
  const char* shader = R"(
   Texture2D frame : register(t0); SamplerState samp : register(s0);
   struct V { float4 pos:SV_Position; float2 uv:TEXCOORD0; };
   V vs(uint id:SV_VertexID) { V o; float2 p=float2((id<<1)&2,id&2); o.uv=p; o.pos=float4(p*float2(2,-2)+float2(-1,1),0,1); return o; }
   float4 ps(V v):SV_Target { return float4(frame.Sample(samp,v.uv).rgb,1); }
  )";
  ComPtr<ID3DBlob> vb, pb, err;
  Check(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "vs", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vb, &err), "Compile display vertex shader");
  Check(D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "ps", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pb, &err), "Compile display pixel shader");
  ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
  Check(device->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &vs), "Create vertex shader");
  Check(device->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &ps), "Create pixel shader");
  D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> sampler; Check(device->CreateSamplerState(&sd, &sampler), "Create display sampler");
  auto fittedViewport=[&](){
   float scale=std::min(float(desc.Width)/bridge.width,float(desc.Height)/bridge.height);
   float width=bridge.width*scale,height=bridge.height*scale;
   return D3D11_VIEWPORT{(desc.Width-width)/2,(desc.Height-height)/2,width,height,0,1};
  };
  D3D11_VIEWPORT viewport=fittedViewport();
  context->RSSetViewports(1, &viewport); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vs.Get(), nullptr, 0); context->PSSetShader(ps.Get(), nullptr, 0);
  ID3D11SamplerState* smp = sampler.Get(); context->PSSetSamplers(0, 1, &smp);
  uint64_t serial = 0, presents = 0, repeats = 0, longFrames = 0, sessionLong = 0;
  bool pixelsChecked = false;
  bool surfacePixelsChecked=false;
  double previous = 0, report = ClockMs(), worst = 0, publicationAge = 0;
  Log("Presenter started: flip discard, maximum frame latency 1, compositor-synchronized, no production CPU readback");
  HANDLE events[] = {stop,refresh,ready};
  while (true) {
   double waitBegin=ClockMs();
   DWORD wait = WaitForMultipleObjects(3, events, FALSE, 1000);
   double waitEnd=ClockMs(), acquireMs=0, copyMs=0, releaseMs=0;
   if (wait == WAIT_OBJECT_0) break;
   if(wait!=WAIT_OBJECT_0+1 && wait!=WAIT_OBJECT_0+2 && wait!=WAIT_TIMEOUT)throw std::runtime_error("Display wait failed.");
   RECT currentClient{};
   if(!GetClientRect(window,&currentClient))throw std::runtime_error("Cannot read viewer client dimensions.");
   if(IsIconic(window) || currentClient.right<=0 || currentClient.bottom<=0){WaitForSingleObject(stop,50);continue;}
   bool rebuild=forceRefresh.exchange(false);
   if(unsigned(currentClient.right)!=desc.Width || unsigned(currentClient.bottom)!=desc.Height || rebuild){
    context->OMSetRenderTargets(0,nullptr,nullptr);rtv.Reset();back.Reset();context->Flush();
    Check(swap->ResizeBuffers(0,currentClient.right,currentClient.bottom,DXGI_FORMAT_UNKNOWN,desc.Flags),"Resize display surface");
    desc.Width=currentClient.right;desc.Height=currentClient.bottom;
    Check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"Reacquire display backbuffer");
    Check(device->CreateRenderTargetView(back.Get(),nullptr,&rtv),"Recreate display target");
    viewport=fittedViewport();context->RSSetViewports(1,&viewport);
    surfaceWidth=desc.Width;surfaceHeight=desc.Height;surfaceGeneration.fetch_add(1);
    Log("Display surface refreshed: "+std::to_string(desc.Width)+"x"+std::to_string(desc.Height));
   }
   if(wait==WAIT_TIMEOUT){Log("Display wait timeout; surface checked");continue;}
   if(wait==WAIT_OBJECT_0+1)continue;
   auto next = bridge.serial.load(std::memory_order_acquire);
   bool updated = false;
   double acquireBegin=ClockMs();
   HRESULT acquired=(next && next!=serial) ? mutex->AcquireSync(0,0) : HRESULT(WAIT_TIMEOUT);
   acquireMs=ClockMs()-acquireBegin;
   if (acquired == S_OK) {
    // Unbind before writing the local shader texture.
    ID3D11ShaderResourceView* empty = nullptr; context->PSSetShaderResources(0, 1, &empty);
    double copyBegin=ClockMs(); context->CopyResource(local.Get(), shared.Get()); copyMs=ClockMs()-copyBegin;
    serial = bridge.serial.load(std::memory_order_acquire);
    publicationAge = std::max(0.0, ClockMs() - bridge.publishedMs.load());
    double releaseBegin=ClockMs(); Check(mutex->ReleaseSync(0), "Release display mailbox"); releaseMs=ClockMs()-releaseBegin; updated = true;
    if (validatePixels && !pixelsChecked) {
     auto stagingDesc = td; stagingDesc.Usage = D3D11_USAGE_STAGING; stagingDesc.BindFlags = 0; stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
     ComPtr<ID3D11Texture2D> staging; Check(device->CreateTexture2D(&stagingDesc,nullptr,&staging),"Create test readback");
     context->CopyResource(staging.Get(),local.Get()); D3D11_MAPPED_SUBRESOURCE mapped{};
     Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read test pixel");
     auto* pixel=static_cast<unsigned char*>(mapped.pData)+(td.Height/2)*mapped.RowPitch+(td.Width/2)*4;
     bool nonblack=pixel[0] || pixel[1] || pixel[2];
     bool gray=abs(int(pixel[0])-128)<=3 && abs(int(pixel[1])-128)<=3 && abs(int(pixel[2])-128)<=3;
     std::string values=std::to_string(pixel[0])+","+std::to_string(pixel[1])+","+std::to_string(pixel[2]);
     context->Unmap(staging.Get(),0);
     if (!nonblack) throw std::runtime_error("Self-test received an unexpectedly black center pixel.");
     if (expectGray && !gray) throw std::runtime_error("Mid-grey colour test failed. Expected 128,128,128; got "+values);
     Log("SELFTEST mailbox pixel BGR="+values);
     pixelsChecked=true; Log("SELFTEST GPU pixel check passed");
    }
   }
   float black[4] = {0,0,0,1}; context->ClearRenderTargetView(rtv.Get(), black);
   ID3D11RenderTargetView* target = rtv.Get(); context->OMSetRenderTargets(1, &target, nullptr);
   if (serial) { ID3D11ShaderResourceView* view = srv.Get(); context->PSSetShaderResources(0, 1, &view); context->Draw(3, 0); }
   if(validatePixels && expectGray && serial && surfaceGeneration.load()>1 && !surfacePixelsChecked){
    D3D11_TEXTURE2D_DESC testDesc{};back->GetDesc(&testDesc);testDesc.Usage=D3D11_USAGE_STAGING;testDesc.BindFlags=0;testDesc.MiscFlags=0;testDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;Check(device->CreateTexture2D(&testDesc,nullptr,&staging),"Create resized surface test texture");
    context->CopyResource(staging.Get(),back.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read resized surface pixels");
    auto* center=static_cast<unsigned char*>(mapped.pData)+(testDesc.Height/2)*mapped.RowPitch+(testDesc.Width/2)*4;
    auto* edge=static_cast<unsigned char*>(mapped.pData)+(testDesc.Height-1)*mapped.RowPitch+(testDesc.Width/2)*4;
    bool centerValid=abs(int(center[0])-128)<=3 && abs(int(center[1])-128)<=3 && abs(int(center[2])-128)<=3 && center[3]==255;
    bool opaqueEdge=edge[3]==255;
    bool blackLetterbox=viewport.Height>=desc.Height-2 || (edge[0]==0 && edge[1]==0 && edge[2]==0);
    context->Unmap(staging.Get(),0);
    if(!centerValid || !opaqueEdge || !blackLetterbox)throw std::runtime_error("Resized output colour/opaque edge check failed.");
    surfacePixelsChecked=true;resizedCoverageVerified=true;Log("SELFTEST resized output center and opaque edge pixels passed");
   }
   double begin = ClockMs();
   HRESULT result = swap->Present(0, 0);
   if (result == DXGI_STATUS_OCCLUDED) {
    if (WaitForSingleObject(stop, 25) == WAIT_OBJECT_0) break;
   } else Check(result, "Present frame");
   if (serial) displayed.fetch_add(1);
   if(serial && surfaceGeneration.load()>1)resizedPresented.fetch_add(1);
   double now = ClockMs(), interval = previous ? now - previous : 0;
   if (interval>50) {
    std::ostringstream detail; detail<<"DISPLAY STALL interval_ms="<<interval<<" wait_ms="<<waitEnd-waitBegin
     <<" acquire_ms="<<acquireMs<<" copy_ms="<<copyMs<<" release_ms="<<releaseMs
     <<" present_ms="<<now-begin<<" mailbox_age_ms="<<publicationAge<<" copied="<<updated;
    Log(detail.str());
   }
   previous = now; ++presents; if (!updated) ++repeats;
   worst = std::max(worst, interval); if (interval > 25) { ++longFrames; ++sessionLong; }
   if (now - report >= 1000) {
    std::lock_guard lock(logMutex);
    timingFile << std::fixed << std::setprecision(3) << now << ',' << presents << ',' << repeats << ','
      << longFrames << ',' << worst << ',' << now - begin << ',' << publicationAge << ',' << bridge.busy.load() << '\n'; timingFile.flush();
    presents = repeats = longFrames = 0; worst = 0; report = now;
   }
  }
  context->ClearState(); context->Flush();
  Log("Presenter stopped; intervals >25ms: " + std::to_string(sessionLong));
 }
};

class App {
 HINSTANCE instance;
 HWND control = nullptr, viewer = nullptr, status = nullptr, monitorBox = nullptr, gameEdit = nullptr;
 HWND keyEdit = nullptr, ctrlCheck = nullptr, altCheck = nullptr, shiftCheck = nullptr;
 HWND modeBox=nullptr,sizeBox=nullptr,positionBox=nullptr,hint=nullptr;
 HFONT bodyFont=nullptr,sectionFont=nullptr,titleFont=nullptr,smallFont=nullptr;
 HBRUSH whiteBrush=CreateSolidBrush(RGB(255,255,255)),footerBrush=CreateSolidBrush(RGB(244,247,251));
 UINT dpi=96;
 std::vector<Monitor> monitors;
 Config cfg;
 std::wstring selectedObsDir;
 fs::path base;
 ObsApi obs;
 bool engineReady = false, videoReady = false, callbackReady = false, showing = false;
 bool moduleReady = false;
 obs_source_t* source = nullptr;
 gs_texrender_t* target = nullptr;
 Bridge bridge;
 std::unique_ptr<Presenter> presenter;
 std::atomic<bool> active{false};
 bool selfTest = false;
 bool fixtureTest = false;
 bool grayTest = false;
 bool windowTest=false;
 bool framedTest=false;
 bool uiSnapshot=false;
 HotkeyState hotkey;
 double lastCaptureCallbackMs=0;
 int cycleCount = 0;
 double started = 0;
public:
 explicit App(HINSTANCE h, bool test, bool captureTest, bool gray, bool smallWindow,bool snapshot,bool framed) : instance(h), selfTest(test || captureTest), fixtureTest(captureTest), grayTest(gray), windowTest(smallWindow || framed),framedTest(framed),uiSnapshot(snapshot) {
  wchar_t exePath[MAX_PATH]; GetModuleFileNameW(nullptr, exePath, MAX_PATH); base = fs::path(exePath).parent_path();
  cfg.Read(base / L"viewer.ini");
  if (test) cfg.testPattern = true;
  if (captureTest) cfg.exe = L"TCSCaptureFixture.exe";
  if(smallWindow) cfg.windowed=true;
  if(framed){cfg.windowed=true;cfg.framedWindow=true;cfg.windowPlaced=false;}
 }
 ~App() { Stop(); for(auto font:{bodyFont,sectionFont,titleFont,smallFont})if(font)DeleteObject(font);DeleteObject(whiteBrush);DeleteObject(footerBrush); }
 int Run() {
  WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = WindowProc; wc.lpszClassName = L"TCSViewerControl";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = whiteBrush; RegisterClassW(&wc);
  wc.lpszClassName = L"TCSViewerOutput"; wc.hbrBackground = HBRUSH(GetStockObject(BLACK_BRUSH)); RegisterClassW(&wc);
  control = CreateWindowExW(0, L"TCSViewerControl", L"TCS Viewer", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
   CW_USEDEFAULT, CW_USEDEFAULT, 640, 690, nullptr, nullptr, instance, this);
  if (!control) throw std::runtime_error("Cannot create viewer controls.");
  dpi=GetDpiForWindow(control);
  RECT desired{0,0,S(640),S(650)};
  AdjustWindowRectExForDpi(&desired,DWORD(GetWindowLongPtrW(control,GWL_STYLE)),FALSE,0,dpi);
  SetWindowPos(control,nullptr,0,0,desired.right-desired.left,desired.bottom-desired.top,SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  bodyFont=CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  sectionFont=CreateFontW(-MulDiv(11,dpi,72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  titleFont=CreateFontW(-MulDiv(22,dpi,72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  smallFont=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  SetupControls();
  if(!selfTest && !uiSnapshot){
   RAWINPUTDEVICE keyboard{0x01,0x06,RIDEV_INPUTSINK,control};
   if(!RegisterRawInputDevices(&keyboard,1,sizeof(keyboard)))
    throw std::runtime_error("Could not register background keyboard input.");
   Log("Background Raw Input hotkey registered");
  }
  if(uiSnapshot){Snapshot();DestroyWindow(control);}
  else if (!selfTest) {
   ShowWindow(control, SW_SHOWNOACTIVATE);
   SetWindowPos(control,nullptr,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
  else PostMessageW(control, WM_TEST_TOGGLE, 0, 0);
  MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { if(!IsDialogMessageW(control,&msg)){TranslateMessage(&msg); DispatchMessageW(&msg);} }
  return int(msg.wParam);
 }
private:
 int S(int value)const {return MulDiv(value,dpi,96);}
 std::wstring Shortcut()const {
  std::wstring s;if(cfg.modifiers&MOD_CONTROL)s+=L"Ctrl + ";if(cfg.modifiers&MOD_ALT)s+=L"Alt + ";if(cfg.modifiers&MOD_SHIFT)s+=L"Shift + ";
  return s+L"F"+std::to_wstring(cfg.key-VK_F1+1);
 }
 void UpdateHint() {
  SetWindowTextW(hint,(L"Press "+Shortcut()+L" to toggle the viewer").c_str());
 }
 void UpdateWindowOptions() {
  bool enabled=SendMessageW(modeBox,CB_GETCURSEL,0,0)>0;
  EnableWindow(sizeBox,enabled);EnableWindow(positionBox,enabled);
 }
 void PaintContent(HDC dc,RECT client) {
  FillRect(dc,&client,whiteBrush);
  RECT header{0,0,client.right,S(96)};HBRUSH navy=CreateSolidBrush(RGB(20,35,57));FillRect(dc,&header,navy);DeleteObject(navy);
  SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));SelectObject(dc,titleFont);
  RECT title{S(24),S(25),S(615),S(70)};DrawTextW(dc,L"TCS Viewer",-1,&title,DT_LEFT|DT_SINGLELINE);
  HPEN line=CreatePen(PS_SOLID,1,RGB(225,231,239));auto old=SelectObject(dc,line);
  for(int y:{190,365,466}) {MoveToEx(dc,S(24),S(y),nullptr);LineTo(dc,S(616),S(y));}
  SelectObject(dc,old);DeleteObject(line);
  RECT footer{0,S(590),client.right,client.bottom};FillRect(dc,&footer,footerBrush);
 }
 void Paint() {
  PAINTSTRUCT paint{};HDC dc=BeginPaint(control,&paint);RECT client;GetClientRect(control,&client);
  PaintContent(dc,client);EndPaint(control,&paint);
 }
 void Snapshot() {
  // Render only our own control panel into an offscreen DIB for layout QA.
  // This never captures another application or the user's desktop.
  RECT client;GetClientRect(control,&client);int width=client.right,height=client.bottom;
  BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
  info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
  HDC dc=CreateCompatibleDC(nullptr);void* pixels=nullptr;
  HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(dc,bitmap);
  PaintContent(dc,client);
  for(HWND child=GetWindow(control,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
   RECT rect;GetWindowRect(child,&rect);POINT origin{rect.left,rect.top};ScreenToClient(control,&origin);
   int saved=SaveDC(dc);SetViewportOrgEx(dc,origin.x,origin.y,nullptr);IntersectClipRect(dc,0,0,rect.right-rect.left,rect.bottom-rect.top);
   SendMessageW(child,WM_PRINT,WPARAM(dc),PRF_CLIENT | PRF_ERASEBKGND | PRF_NONCLIENT | PRF_CHILDREN);RestoreDC(dc,saved);
  }
  GdiFlush();BITMAPFILEHEADER header{};header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+width*height*4;
  std::ofstream output(base/L"ui-preview.bmp",std::ios::binary);output.write(reinterpret_cast<const char*>(&header),sizeof(header));
  output.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));output.write(static_cast<const char*>(pixels),width*height*4);
  SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);Log("Control panel offscreen render saved");
 }
 void DrawButton(DRAWITEMSTRUCT* item) {
  bool down=(item->itemState&ODS_SELECTED)!=0;COLORREF color=down?RGB(24,76,156):RGB(36,100,201);
  HBRUSH brush=CreateSolidBrush(color);FillRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);
  SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,RGB(255,255,255));SelectObject(item->hDC,bodyFont);
  RECT r=item->rcItem;DrawTextW(item->hDC,L"Save settings",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  if(item->itemState&ODS_FOCUS){InflateRect(&r,-4,-4);DrawFocusRect(item->hDC,&r);}
 }
 HWND Child(const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id = 0) {
  HWND c = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, S(x),S(y),S(w),S(h), control, HMENU(INT_PTR(id)), instance, nullptr);
  SendMessageW(c, WM_SETFONT, WPARAM(bodyFont), TRUE);SetWindowTheme(c,L"Explorer",nullptr); return c;
 }
 void SetupControls() {
  auto section=[&](const wchar_t* text,int y){auto w=Child(L"STATIC",text,0,24,y,592,24);SendMessageW(w,WM_SETFONT,WPARAM(sectionFont),TRUE);};
  section(L"Capture",114);
  Child(L"STATIC", L"Game", 0, 24,153,140,24);
  gameEdit = Child(L"EDIT", cfg.exe.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 184,147,432,29);
  SendMessageW(gameEdit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(S(8),S(8)));
  section(L"Display",203);
  Child(L"STATIC", L"Monitor", 0,24,242,140,24);
  monitorBox = Child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,184,236,432,200);
  monitors = Monitors(); int selected = -1;
  for (size_t i = 0; i < monitors.size(); ++i) {
   auto& m = monitors[i];
   auto label = L"Display "+std::to_wstring(i+1)+L"  /  " + std::to_wstring(m.rect.right-m.rect.left) + L" x " + std::to_wstring(m.rect.bottom-m.rect.top) + (m.primary ? L"  (primary)" : L"  (secondary)");
   SendMessageW(monitorBox, CB_ADDSTRING, 0, LPARAM(label.c_str()));
   if (m.name == cfg.monitor) selected = int(i);
  }
  if (selected < 0) for (size_t i=0; i<monitors.size(); ++i) if (!monitors[i].primary) { selected = int(i); break; }
  SendMessageW(monitorBox, CB_SETCURSEL, selected < 0 ? 0 : selected, 0);
  Child(L"STATIC",L"Mode",0,24,286,140,24);
  modeBox=Child(L"COMBOBOX",L"",CBS_DROPDOWNLIST | WS_TABSTOP,184,280,432,160,201);
  for(auto name:{L"Fullscreen",L"Borderless window",L"Windowed"})SendMessageW(modeBox,CB_ADDSTRING,0,LPARAM(name));
  SendMessageW(modeBox,CB_SETCURSEL,cfg.framedWindow?2:(cfg.windowed?1:0),0);
  Child(L"STATIC",L"Size / position",0,24,330,145,24);
  sizeBox=Child(L"COMBOBOX",L"",CBS_DROPDOWNLIST | WS_TABSTOP,184,324,190,180);
  for(auto size:{L"640 x 360",L"960 x 540",L"1280 x 720",L"1600 x 900"})SendMessageW(sizeBox,CB_ADDSTRING,0,LPARAM(size));
  SendMessageW(sizeBox,CB_SETCURSEL,cfg.windowSize,0);
  positionBox=Child(L"COMBOBOX",L"",CBS_DROPDOWNLIST | WS_TABSTOP,390,324,226,200);
  for(auto position:{L"Center",L"Top left",L"Top right",L"Bottom left",L"Bottom right"})SendMessageW(positionBox,CB_ADDSTRING,0,LPARAM(position));
  SendMessageW(positionBox,CB_SETCURSEL,cfg.windowPosition,0);UpdateWindowOptions();
  section(L"Hotkey",379);
  Child(L"STATIC", L"Toggle viewer", 0,24,422,140,24);
  ctrlCheck = Child(L"BUTTON", L"Ctrl", BS_AUTOCHECKBOX | WS_TABSTOP,184,416,70,28);
  altCheck = Child(L"BUTTON", L"Alt", BS_AUTOCHECKBOX | WS_TABSTOP,264,416,65,28);
  shiftCheck = Child(L"BUTTON", L"Shift", BS_AUTOCHECKBOX | WS_TABSTOP,339,416,75,28);
  SendMessageW(ctrlCheck, BM_SETCHECK, cfg.modifiers & MOD_CONTROL ? BST_CHECKED : BST_UNCHECKED,0);
  SendMessageW(altCheck, BM_SETCHECK, cfg.modifiers & MOD_ALT ? BST_CHECKED : BST_UNCHECKED,0);
  SendMessageW(shiftCheck, BM_SETCHECK, cfg.modifiers & MOD_SHIFT ? BST_CHECKED : BST_UNCHECKED,0);
  keyEdit = Child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,438,416,100,200);
  for (int i=1; i<=12; ++i) { auto s=L"F"+std::to_wstring(i); SendMessageW(keyEdit,CB_ADDSTRING,0,LPARAM(s.c_str())); }
  SendMessageW(keyEdit, CB_SETCURSEL, cfg.key >= VK_F1 && cfg.key <= VK_F12 ? cfg.key - VK_F1 : 9, 0);
  hint=Child(L"STATIC",L"",0,24,494,592,25);SendMessageW(hint,WM_SETFONT,WPARAM(smallFont),TRUE);UpdateHint();
  Child(L"BUTTON", L"Save settings", BS_OWNERDRAW | WS_TABSTOP,24,540,176,36,101);
  Child(L"BUTTON", L"Stop viewer", BS_PUSHBUTTON | WS_TABSTOP,216,540,128,36,102);
  status = Child(L"STATIC", L"OFF  /  Ready.",0,24,607,592,30);
 }
 void Apply() {
  Stop(); wchar_t text[512]; GetWindowTextW(gameEdit,text,512); cfg.exe = text;
  auto oldMonitor=cfg.monitor;bool oldWindowed=cfg.windowed,oldFramed=cfg.framedWindow;
  int oldSize=cfg.windowSize,oldPosition=cfg.windowPosition;
  int index = int(SendMessageW(monitorBox,CB_GETCURSEL,0,0));
  if (index >= 0 && index < int(monitors.size())) cfg.monitor = monitors[index].name;
  if(cfg.exe.empty())throw std::runtime_error("Enter the game executable name.");
  auto mode=SendMessageW(modeBox,CB_GETCURSEL,0,0);
  cfg.windowed=mode>0;cfg.framedWindow=mode==2;
  cfg.windowSize=int(SendMessageW(sizeBox,CB_GETCURSEL,0,0));
  cfg.windowPosition=int(SendMessageW(positionBox,CB_GETCURSEL,0,0));
  if(cfg.monitor!=oldMonitor || cfg.windowed!=oldWindowed || cfg.framedWindow!=oldFramed || cfg.windowSize!=oldSize || cfg.windowPosition!=oldPosition)cfg.windowPlaced=false;
  cfg.modifiers = 0;
  if (SendMessageW(ctrlCheck,BM_GETCHECK,0,0)==BST_CHECKED) cfg.modifiers |= MOD_CONTROL;
  if (SendMessageW(altCheck,BM_GETCHECK,0,0)==BST_CHECKED) cfg.modifiers |= MOD_ALT;
  if (SendMessageW(shiftCheck,BM_GETCHECK,0,0)==BST_CHECKED) cfg.modifiers |= MOD_SHIFT;
  cfg.key = VK_F1 + UINT(SendMessageW(keyEdit,CB_GETCURSEL,0,0));
  hotkey=HotkeyState{};
  cfg.Save();UpdateHint();SetWindowTextW(status,L"OFF  /  Settings saved. Ready for your hotkey.");
 }
 void InitializeEngine() {
  if (engineReady) return;
  if (selectedObsDir.empty()) {
   selectedObsDir = cfg.obsDir.empty() ? DiscoverObs(base).wstring() : cfg.obsDir;
   Log("OBS runtime selected: " + Utf8(selectedObsDir) + (cfg.obsDir.empty() ? " (automatic)" : " (override)"));
  }
  auto bin = selectedObsDir + L"\\bin\\64bit";
  obs.Load(bin); obs.base_set_log_handler(ObsLog, nullptr);
  // Only our process's working directory changes. OBS's own configuration is untouched.
  SetCurrentDirectoryW(bin.c_str());
  fs::create_directories(base / L"runtime");
  if (!obs.obs_startup("en-US", Utf8((base/L"runtime").wstring()).c_str(), nullptr)) throw std::runtime_error("OBS capture engine failed to initialize.");
  engineReady = true;
  obs.obs_add_data_path(Utf8(selectedObsDir + L"\\data\\libobs").c_str());
  Log("Capture engine ready: " + std::string(obs.obs_get_version_string()));
 }
 void LoadCaptureModule() {
  if(moduleReady) return;
  obs_module_t* module = nullptr;
  const bool coreLayout = std::string(obs.obs_get_version_string()).rfind("33.0.", 0) == 0;
  fs::path captureDir = fs::path(selectedObsDir) / (coreLayout ? L"core/win-capture" : L"obs-plugins/64bit");
  fs::path captureData = coreLayout ? captureDir / L"data" : fs::path(selectedObsDir) / L"data/obs-plugins/win-capture";
  auto dllPath=Utf8((captureDir/L"win-capture.dll").generic_wstring());
  auto dataPath=Utf8(captureData.generic_wstring());
  Log("Game Capture module: " + dllPath);
  int result = obs.obs_open_module(&module, dllPath.c_str(), dataPath.c_str());
  if (result != 0 || !module || !obs.obs_init_module(module)) throw std::runtime_error("Cannot initialize OBS Game Capture module.");
  obs.obs_post_load_modules(); moduleReady=true;
 }
 struct FindGame { std::wstring wanted; HWND window = nullptr; };
 static BOOL CALLBACK FindWindow(HWND w, LPARAM value) {
  auto& data = *reinterpret_cast<FindGame*>(value);
  if (!IsWindowVisible(w) || GetWindow(w,GW_OWNER)) return TRUE;
  DWORD pid; GetWindowThreadProcessId(w,&pid);
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
  if (!process) return TRUE;
  wchar_t path[32768]; DWORD size = 32768;
  bool match = QueryFullProcessImageNameW(process,0,path,&size) && _wcsicmp(fs::path(path).filename().c_str(), data.wanted.c_str())==0;
  CloseHandle(process); if (match) { data.window = w; return FALSE; } return TRUE;
 }
 std::string GameWindowId() {
  FindGame find{cfg.exe}; EnumWindows(FindWindow,reinterpret_cast<LPARAM>(&find));
  if (!find.window) throw std::runtime_error("Game window not found. Start TCS first, then toggle the viewer.");
  wchar_t title[1024], cls[256]; GetWindowTextW(find.window,title,1024); GetClassNameW(find.window,cls,256);
  auto escape = [](std::string s) { size_t p=0; while ((p=s.find('#',p))!=std::string::npos) { s.replace(p,1,"#22"); p+=3; } p=0; while ((p=s.find(':',p))!=std::string::npos) { s.replace(p,1,"#3A"); p+=3; } return s; };
  return escape(Utf8(title))+":"+escape(Utf8(cls))+":"+escape(Utf8(cfg.exe));
 }
 void Start() {
  if (active) return;
  HWND foreground = GetForegroundWindow();
  // A source-specific hook is created only for the configured executable.
  if (!cfg.testPattern) {
   auto list = Monitors(); if (list.size()<2) throw std::runtime_error("Connect a second monitor before enabling the viewer.");
  }
  auto windowId = cfg.testPattern ? std::string() : GameWindowId();
  Monitor selected{}; bool found = false;
  for (auto& m: Monitors()) if (m.name == cfg.monitor) { selected=m; found=true; break; }
  if (!found) throw std::runtime_error("Selected monitor is unavailable. Choose a monitor and click Apply.");
  if (!selfTest && MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST)==selected.handle)
   throw std::runtime_error("The viewer monitor contains the focused game. Select your other monitor.");
  InitializeEngine();
  obs_video_info video{}; auto graphicsPath = Utf8(selectedObsDir + L"\\bin\\64bit\\libobs-d3d11.dll");
  video.graphics_module=graphicsPath.c_str(); video.fps_num=cfg.captureFps; video.fps_den=1;
  video.base_width=video.output_width=bridge.width; video.base_height=video.output_height=bridge.height;
  video.output_format=VIDEO_FORMAT_BGRA; video.colorspace=VIDEO_CS_709; video.range=VIDEO_RANGE_FULL;
  video.scale_type=OBS_SCALE_DISABLE; video.gpu_conversion=false;
  int reset = obs.obs_reset_video(&video); if (reset!=0) throw std::runtime_error("Cannot initialize capture graphics: " + std::to_string(reset));
  videoReady=true;
  LoadCaptureModule();
  if (!cfg.testPattern) {
   auto* settings = obs.obs_data_create();
   obs.obs_data_set_string(settings,"capture_mode","window"); obs.obs_data_set_string(settings,"window",windowId.c_str());
   obs.obs_data_set_int(settings,"priority",2); obs.obs_data_set_bool(settings,"capture_cursor",false);
   obs.obs_data_set_bool(settings,"limit_framerate",false); obs.obs_data_set_bool(settings,"capture_overlays",false);
   obs.obs_data_set_bool(settings,"anti_cheat_hook",false); obs.obs_data_set_bool(settings,"capture_audio",false);
   source=obs.obs_source_create("game_capture","TCS Viewer Capture",settings,nullptr); obs.obs_data_release(settings);
   if (!source) throw std::runtime_error("Game Capture source could not be created.");
   obs.obs_source_inc_showing(source); showing=true;
  }
  obs.obs_enter_graphics();
  try {
   bridge.Create(static_cast<ID3D11Device*>(obs.gs_get_device_obj()));
   // The game source may sample sRGB into linear light. An sRGB-capable
   // target re-encodes it; UNORM-only targets stored linear bytes as SDR.
   target=obs.gs_texrender_create(GS_BGRA,GS_ZS_NONE);
   if (!target) throw std::runtime_error("Cannot create capture render target.");
  } catch (...) { obs.obs_leave_graphics(); throw; }
  obs.obs_leave_graphics();
  RECT r=ViewerRectangle(selected.rect,selected.work,cfg.windowed,cfg.windowSize,cfg.windowPosition);
  DWORD outputStyle=cfg.framedWindow?(WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU):WS_POPUP;
  DWORD outputEx=WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
  int expectedWidth=r.right-r.left,expectedHeight=r.bottom-r.top;
  if(cfg.framedWindow){
   RECT frame{0,0,expectedWidth,expectedHeight};AdjustWindowRectExForDpi(&frame,outputStyle,FALSE,outputEx,dpi);
   int outerWidth=frame.right-frame.left,outerHeight=frame.bottom-frame.top;
   int x=cfg.windowPlaced?cfg.windowX:r.left+frame.left;
   int y=cfg.windowPlaced?cfg.windowY:r.top+frame.top;
   x=std::clamp(x,int(selected.work.left),std::max(int(selected.work.left),int(selected.work.right)-outerWidth));
   y=std::clamp(y,int(selected.work.top),std::max(int(selected.work.top),int(selected.work.bottom)-outerHeight));
   r={x,y,x+outerWidth,y+outerHeight};
  }
  Log(std::string("Output mode: ")+(cfg.framedWindow?"draggable window":cfg.windowed?"borderless window":"fullscreen")+" rect="+std::to_string(r.left)+","+std::to_string(r.top)+","+std::to_string(r.right-r.left)+","+std::to_string(r.bottom-r.top));
  viewer=CreateWindowExW(outputEx,L"TCSViewerOutput",L"TCS Viewer",outputStyle,
   r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,this);
  if (!viewer) throw std::runtime_error("Cannot create viewer output.");
  if(selfTest) {
   RECT actual;GetWindowRect(viewer,&actual);
   if(actual.left!=r.left || actual.top!=r.top || actual.right!=r.right || actual.bottom!=r.bottom)throw std::runtime_error("Output window geometry check failed.");
   RECT content;GetClientRect(viewer,&content);
   if(content.right!=expectedWidth || content.bottom!=expectedHeight)throw std::runtime_error("Output content dimensions changed unexpectedly.");
   if(cfg.framedWindow){
    int titleX=(r.left+r.right)/2,titleY=r.top+GetSystemMetricsForDpi(SM_CYFIXEDFRAME,dpi)+GetSystemMetricsForDpi(SM_CYSMCAPTION,dpi)/2;
    if(SendMessageW(viewer,WM_NCHITTEST,0,MAKELPARAM(titleX,titleY))!=HTCAPTION)throw std::runtime_error("Draggable title bar hit test failed.");
    int movedX=r.left+(cfg.windowPosition==2?-12:12),movedY=r.top+12;
    HWND beforeMove=GetForegroundWindow();SetWindowPos(viewer,nullptr,movedX,movedY,0,0,SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT moved;GetWindowRect(viewer,&moved);
    if(moved.left!=movedX || moved.top!=movedY || GetForegroundWindow()!=beforeMove)throw std::runtime_error("Non-activating window movement test failed.");
    Log("SELFTEST caption hit and non-activating window move passed");
   }
   Log("SELFTEST output window geometry matched selected mode");
  }
  active=true; started=ClockMs(); lastCaptureCallbackMs=0;
  obs.obs_add_main_render_callback(DrawCallback,this); callbackReady=true;
  presenter=std::make_unique<Presenter>(bridge,viewer,control,selfTest,grayTest); presenter->Start();
  SetWindowPos(viewer,HWND_TOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE | SWP_SHOWWINDOW);
  if (GetForegroundWindow()!=foreground) Log("WARNING: foreground window changed during startup");
  else Log("Viewer enabled; foreground focus preserved");
  SetWindowTextW(status,cfg.framedWindow?L"ON  /  Windowed. Drag the title bar to move.":cfg.windowed?L"ON  /  Borderless window. Press your hotkey to stop.":L"ON  /  Fullscreen. Press your hotkey to stop.");
  SetTimer(control,2,1000,nullptr);
 }
 static void DrawCallback(void* data,uint32_t,uint32_t) { static_cast<App*>(data)->Draw(); }
 void Draw() {
  if (!active) return;
  double callbackBegin=ClockMs();
  if(lastCaptureCallbackMs && callbackBegin-lastCaptureCallbackMs>50)
   Log("CAPTURE CALLBACK GAP ms="+std::to_string(callbackBegin-lastCaptureCallbackMs));
  lastCaptureCallbackMs=callbackBegin;
  unsigned width=cfg.testPattern ? bridge.width : obs.obs_source_get_width(source);
  unsigned height=cfg.testPattern ? bridge.height : obs.obs_source_get_height(source);
  if (!width || !height) return;
  obs.gs_texrender_reset(target);
  if (!obs.gs_texrender_begin(target,bridge.width,bridge.height)) return;
  vec4 clear{};
  if (cfg.testPattern) { float phase=float(fmod((ClockMs()-started)/1000.0,3.0)); clear.x=phase<1?1.f:0.f; clear.y=phase>=1 && phase<2?1.f:0.f; clear.z=phase>=2?1.f:0.f; clear.w=1; }
  obs.gs_clear(GS_CLEAR_COLOR,&clear,1,0);
  obs.gs_ortho(0,float(bridge.width),0,float(bridge.height),-100,100);
  if (!cfg.testPattern) {
   float scale=std::min(float(bridge.width)/width,float(bridge.height)/height);
   obs.gs_matrix_push(); obs.gs_matrix_identity();
   obs.gs_matrix_translate3f((bridge.width-width*scale)/2,(bridge.height-height*scale)/2,0);
   obs.gs_matrix_scale3f(scale,scale,1); obs.gs_blend_state_push(); obs.gs_reset_blend_state();
   obs.obs_source_video_render(source);
   obs.gs_blend_state_pop(); obs.gs_matrix_pop();
  }
  obs.gs_texrender_end(target);
  if (bridge.mutex->AcquireSync(0,0)==S_OK) {
   auto* texture=static_cast<ID3D11Texture2D*>(obs.gs_texture_get_obj(obs.gs_texrender_get_texture(target)));
   bridge.context->CopyResource(bridge.texture.Get(),texture);
   bridge.publishedMs.store(ClockMs()); bridge.serial.fetch_add(1,std::memory_order_release);
   HRESULT released=bridge.mutex->ReleaseSync(0);
   if (FAILED(released)) { active=false; auto* msg=new std::wstring(L"GPU mailbox synchronization failed."); if (!PostMessageW(control,WM_VIEWER_ERROR,0,LPARAM(msg))) delete msg; }
  } else bridge.busy.fetch_add(1);
  double elapsed=ClockMs()-callbackBegin;
  if(elapsed>50) Log("CAPTURE RENDER STALL ms="+std::to_string(elapsed));
 }
 void Stop() {
  KillTimer(control,2);KillTimer(control,5); active=false;
  if(viewer && cfg.framedWindow && !selfTest){
   RECT placement;if(GetWindowRect(viewer,&placement)){cfg.windowX=placement.left;cfg.windowY=placement.top;cfg.windowPlaced=true;cfg.Save();}
  }
  if (viewer) ShowWindow(viewer,SW_HIDE);
  if (presenter) { presenter->Stop(); presenter.reset(); }
  if (callbackReady) { obs.obs_remove_main_render_callback(DrawCallback,this); callbackReady=false; }
  if (source) { if (showing) obs.obs_source_dec_showing(source); obs.obs_source_release(source); source=nullptr; showing=false; }
  if (videoReady) {
   obs.obs_enter_graphics(); if (target) obs.gs_texrender_destroy(target); target=nullptr; bridge.Reset(); obs.obs_leave_graphics();
   videoReady=false;
  }
  if (engineReady) { obs.obs_shutdown(); engineReady=false; moduleReady=false; Log("Viewer disabled: capture engine and graphics workers stopped"); }
  if (viewer) { DestroyWindow(viewer); viewer=nullptr; }
  if (status) SetWindowTextW(status,L"OFF  /  Capture and rendering stopped.");
 }
 void Toggle() { if (active) Stop(); else { try { Start(); } catch (...) { Stop(); throw; } } }
 void Error(const std::exception& e) {
  Log(std::string("ERROR: ")+e.what()); SetWindowTextW(status,Wide(e.what()).c_str());
  if (selfTest) { Stop(); PostQuitMessage(1); }
  else MessageBoxW(control,Wide(e.what()).c_str(),L"TCS Viewer",MB_OK | MB_ICONERROR);
 }
 static LRESULT CALLBACK WindowProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
  App* app=reinterpret_cast<App*>(GetWindowLongPtrW(w,GWLP_USERDATA));
  if (msg==WM_NCCREATE) { app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(w,GWLP_USERDATA,LONG_PTR(app)); }
  if (!app) return DefWindowProcW(w,msg,wp,lp);
  try {
   if(msg==WM_PAINT && w==app->control){app->Paint();return 0;}
   if(msg==WM_DRAWITEM && wp==101){app->DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}
   if(msg==WM_CTLCOLORSTATIC && w==app->control){HDC dc=HDC(wp);SetTextColor(dc,RGB(40,54,73));SetBkMode(dc,TRANSPARENT);return LRESULT(HWND(lp)==app->status?app->footerBrush:app->whiteBrush);}
   if (msg==WM_MOUSEACTIVATE && w==app->viewer) return MA_NOACTIVATE;
   if (msg==WM_NCHITTEST && w==app->viewer && !app->cfg.framedWindow) return HTTRANSPARENT;
   if(msg==WM_SIZE && w==app->viewer){if(app->presenter)app->presenter->Refresh(false);}
   if(msg==WM_EXITSIZEMOVE && w==app->viewer){
    if(app->presenter)app->presenter->Refresh();
    RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);
    Log("Viewer move/size completed; display refresh requested");
   }
   if(msg==WM_DISPLAYCHANGE && w==app->control && app->viewer){
    if(app->presenter)app->presenter->Refresh();
    RedrawWindow(app->viewer,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);
    Log("Display mode change; display refresh requested");
   }
   if(msg==WM_DPICHANGED && w==app->viewer){
    auto* proposed=reinterpret_cast<RECT*>(lp);
    SetWindowPos(w,nullptr,proposed->left,proposed->top,proposed->right-proposed->left,proposed->bottom-proposed->top,SWP_NOZORDER|SWP_NOACTIVATE);
    if(app->presenter)app->presenter->Refresh();
    RedrawWindow(w,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);return 0;
   }
   if(msg==WM_INPUT && w==app->control) {
    RAWINPUT input{}; UINT size=sizeof(input);
    if(GetRawInputData(HRAWINPUT(lp),RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))!=UINT(-1) && input.header.dwType==RIM_TYPEKEYBOARD) {
     auto& key=input.data.keyboard; UINT vk=key.VKey;
     if(vk==VK_SHIFT) vk=MapVirtualKeyW(key.MakeCode,MAPVK_VSC_TO_VK_EX);
     if(vk==VK_CONTROL) vk=(key.Flags & RI_KEY_E0)?VK_RCONTROL:VK_LCONTROL;
     if(vk==VK_MENU) vk=(key.Flags & RI_KEY_E0)?VK_RMENU:VK_LMENU;
     if(app->hotkey.Update(vk,(key.Flags & RI_KEY_BREAK)==0,app->cfg.modifiers,app->cfg.key))
      PostMessageW(w,WM_TOGGLE_VIEWER,0,0);
    }
    return DefWindowProcW(w,msg,wp,lp);
   }
   if(msg==WM_TOGGLE_VIEWER) { app->Toggle(); return 0; }
   if (msg==WM_COMMAND) { if (LOWORD(wp)==101) app->Apply(); if (LOWORD(wp)==102) app->Stop(); if(LOWORD(wp)==201 && HIWORD(wp)==CBN_SELCHANGE)app->UpdateWindowOptions(); return 0; }
   if (msg==WM_VIEWER_ERROR) { std::unique_ptr<std::wstring> message(reinterpret_cast<std::wstring*>(lp)); app->Stop(); throw std::runtime_error(Utf8(*message)); }
   if (msg==WM_TIMER && wp==2 && app->active) {
    if (!app->bridge.serial.load() && ClockMs()-app->started>8000) SetWindowTextW(app->status,L"Waiting for game capture");
    return 0;
   }
   if (msg==WM_TEST_TOGGLE) { if(app->windowTest){app->cfg.windowSize=app->cycleCount%4;app->cfg.windowPosition=app->cycleCount%5;}app->Toggle(); SetTimer(w,3,app->fixtureTest?6000:2500,nullptr);if(app->framedTest)SetTimer(w,5,1200,nullptr); return 0; }
   if(msg==WM_TIMER && wp==5 && app->framedTest){
    KillTimer(w,5);RECT r;GetWindowRect(app->viewer,&r);
    SetWindowPos(app->viewer,nullptr,r.left+8,r.top+8,r.right-r.left+96,r.bottom-r.top+72,SWP_NOZORDER|SWP_NOACTIVATE);
    SendMessageW(app->viewer,WM_EXITSIZEMOVE,0,0);
    SendMessageW(app->control,WM_DISPLAYCHANGE,32,MAKELPARAM(1920,1080));return 0;
   }
   if (msg==WM_TIMER && wp==3) {
    KillTimer(w,3);
    if (app->bridge.serial.load()<10 || !app->presenter || app->presenter->displayed.load()<10)
     throw std::runtime_error("Self-test failed: capture or presentation did not produce enough frames.");
    if(app->framedTest){
     RECT actual;GetClientRect(app->viewer,&actual);
     if(app->presenter->surfaceGeneration.load()<2 || app->presenter->surfaceWidth.load()!=unsigned(actual.right) || app->presenter->surfaceHeight.load()!=unsigned(actual.bottom) || app->presenter->resizedPresented.load()<10 || (app->grayTest && !app->presenter->resizedCoverageVerified.load()))throw std::runtime_error("Display surface did not recover after resized window.");
     Log("SELFTEST display surface matched changed client dimensions");
    }
    Log("Self-test cycle frames published="+std::to_string(app->bridge.serial.load())+" presented="+std::to_string(app->presenter->displayed.load()));
    app->Stop(); ++app->cycleCount;
    if (app->cycleCount<3) SetTimer(w,4,300,nullptr); else { Log("SELFTEST PASS: three start/stop cycles completed"); DestroyWindow(w); }
    return 0;
   }
   if (msg==WM_TIMER && wp==4) { KillTimer(w,4); PostMessageW(w,WM_TEST_TOGGLE,0,0); return 0; }
   if (msg==WM_CLOSE && w==app->viewer) { app->Stop(); return 0; }
   if (msg==WM_CLOSE && w==app->control) { app->Stop(); DestroyWindow(w); return 0; }
   if (msg==WM_DESTROY && w==app->control) { RAWINPUTDEVICE keyboard{0x01,0x06,RIDEV_REMOVE,nullptr}; RegisterRawInputDevices(&keyboard,1,sizeof(keyboard)); PostQuitMessage(0); return 0; }
  } catch (const std::exception& e) { app->Error(e); return 0; }
  return DefWindowProcW(w,msg,wp,lp);
 }
};

int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR cmd,int) {
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 bool testing=wcsstr(cmd,L"--self-test") || wcsstr(cmd,L"--capture-test") || wcsstr(cmd,L"--ui-snapshot");
 HANDLE singleton=CreateMutexW(nullptr,FALSE,testing?L"Local\\TCSViewer-QA":L"Local\\TCSViewer-0.1");
 if (!singleton || GetLastError()==ERROR_ALREADY_EXISTS) { if(!testing)MessageBoxW(nullptr,L"TCS Viewer is already running.",L"TCS Viewer",MB_OK); if(singleton)CloseHandle(singleton); return 1; }
 wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr,path,MAX_PATH); fs::path base=fs::path(path).parent_path();
 logFile.open(base/L"viewer.log",std::ios::trunc); timingFile.open(base/L"timing.csv",std::ios::trunc);
 timingFile << "qpc_ms,present_calls,reused_mailbox_frames,intervals_over_25ms,max_present_interval_ms,last_present_call_ms,last_mailbox_age_ms,producer_mutex_busy_total\n";
 Log("TCS Viewer 0.6.0-alpha.2 starting");
 int result=1;
 try { App app(h,wcsstr(cmd,L"--self-test")!=nullptr,wcsstr(cmd,L"--capture-test")!=nullptr,wcsstr(cmd,L"--expect-gray")!=nullptr,wcsstr(cmd,L"--windowed-test")!=nullptr,wcsstr(cmd,L"--ui-snapshot")!=nullptr,wcsstr(cmd,L"--framed-test")!=nullptr); result=app.Run(); }
 catch(const std::exception& e) { Log(std::string("FATAL: ")+e.what()); MessageBoxW(nullptr,Wide(e.what()).c_str(),L"TCS Viewer",MB_OK|MB_ICONERROR); }
 Log("TCS Viewer exit: "+std::to_string(result)); CloseHandle(singleton); return result;
}

