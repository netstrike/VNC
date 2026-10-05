// vnc_service.exe install | uninstall | run
//
// Runs vnc_server.exe as a Windows service, started at boot under the
// SYSTEM account, so the operator (who has already set a password with
// `vnc_server --set-password`, required for any connection to succeed, see
// vnc/auth.h) can reach their own machine's screen before anyone logs on,
// while locked, or during a Remote Desktop session.
//
// Windows ties screen capture and input injection to a session and to a
// desktop within it (see desktop_util.h); SYSTEM alone does not grant
// access to "the monitor" directly. This service solves that by:
//   1. Running vnc_server.exe inside the session attached to the physical
//      console (WTSGetActiveConsoleSessionId), not the service's own
//      session 0, which has no display.
//   2. Watching for session-change notifications (logon, logoff, lock,
//      unlock, an RDP session connecting or disconnecting) and restarting
//      the child in the session that is the console session afterwards.
// vnc_server itself re-attaches to the current input desktop on every
// capture/input call (desktop_util.h), which is what lets it keep showing
// the login or lock screen rather than a blank session.
#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>

#include <string>

namespace {

constexpr wchar_t kServiceName[] = L"VncRemoteService";
constexpr wchar_t kDisplayName[] = L"VNC Remote Access";

SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
SERVICE_STATUS g_status = {};
HANDLE g_stopEvent = nullptr;
PROCESS_INFORMATION g_child = {};
DWORD g_childSessionId = 0xFFFFFFFF;

std::wstring serverExePath() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring p(path);
  auto slash = p.find_last_of(L"\\/");
  return (slash == std::wstring::npos ? L"" : p.substr(0, slash + 1)) + L"vnc_server.exe";
}

void stopChild() {
  if (g_child.hProcess) {
    TerminateProcess(g_child.hProcess, 0);
    CloseHandle(g_child.hProcess);
    CloseHandle(g_child.hThread);
    g_child = {};
  }
}

// Launches vnc_server.exe inside the given session, with that session's own
// user token when one is logged on there (falling back to running as
// SYSTEM in that session otherwise, e.g. at the login/lock screen).
bool startChildInSession(DWORD sessionId) {
  stopChild();

  HANDLE userToken = nullptr;
  HANDLE primaryToken = nullptr;
  if (WTSQueryUserToken(sessionId, &userToken)) {
    DuplicateTokenEx(userToken, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation,
                     TokenPrimary, &primaryToken);
    CloseHandle(userToken);
  }

  void* env = nullptr;
  if (primaryToken) CreateEnvironmentBlock(&env, primaryToken, FALSE);

  STARTUPINFOW si = {sizeof(si)};
  si.lpDesktop = const_cast<wchar_t*>(L"winsta0\\default");
  std::wstring cmd = L"\"" + serverExePath() + L"\"";

  BOOL ok;
  if (primaryToken) {
    ok = CreateProcessAsUserW(primaryToken, nullptr, cmd.data(), nullptr, nullptr, FALSE,
                              CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE, env, nullptr, &si,
                              &g_child);
  } else {
    // No one logged on in this session (e.g. still at the login screen):
    // run as SYSTEM directly in that session instead.
    ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE, env, nullptr, &si,
                        &g_child);
  }

  if (env) DestroyEnvironmentBlock(env);
  if (primaryToken) CloseHandle(primaryToken);

  if (ok) g_childSessionId = sessionId;
  return ok;
}

void ensureChildOnConsoleSession() {
  DWORD console = WTSGetActiveConsoleSessionId();
  if (console == 0xFFFFFFFF) return;  // no physical console right now
  if (g_child.hProcess) {
    DWORD exitCode = 0;
    GetExitCodeProcess(g_child.hProcess, &exitCode);
    if (exitCode == STILL_ACTIVE && g_childSessionId == console) return;  // already correct
  }
  startChildInSession(console);
}

DWORD WINAPI serviceCtrlHandler(DWORD ctrl, DWORD eventType, LPVOID /*data*/, LPVOID /*ctx*/) {
  switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
      g_status.dwCurrentState = SERVICE_STOP_PENDING;
      SetServiceStatus(g_statusHandle, &g_status);
      SetEvent(g_stopEvent);
      return NO_ERROR;
    case SERVICE_CONTROL_SESSIONCHANGE:
      // WTS_CONSOLE_CONNECT, WTS_CONSOLE_DISCONNECT, WTS_SESSION_LOGON,
      // WTS_SESSION_LOGOFF, WTS_SESSION_LOCK, WTS_SESSION_UNLOCK all land
      // here; in every case, re-check which session owns the console now.
      (void)eventType;
      ensureChildOnConsoleSession();
      return NO_ERROR;
    default:
      return NO_ERROR;
  }
}

void WINAPI serviceMain(DWORD, LPWSTR*) {
  g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, serviceCtrlHandler, nullptr);
  if (!g_statusHandle) return;

  g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  g_status.dwControlsAccepted =
      SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE;
  g_status.dwCurrentState = SERVICE_RUNNING;
  g_status.dwWin32ExitCode = NO_ERROR;
  SetServiceStatus(g_statusHandle, &g_status);

  g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ensureChildOnConsoleSession();

  // Belt-and-braces poll alongside the SESSIONCHANGE notifications above,
  // in case the child exits on its own (crash, update) between events.
  while (WaitForSingleObject(g_stopEvent, 5000) == WAIT_TIMEOUT) {
    ensureChildOnConsoleSession();
  }

  stopChild();
  g_status.dwCurrentState = SERVICE_STOPPED;
  SetServiceStatus(g_statusHandle, &g_status);
}

int installService() {
  wchar_t exePath[MAX_PATH];
  GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
  if (!scm) return 1;
  std::wstring cmd = std::wstring(L"\"") + exePath + L"\" run";
  SC_HANDLE svc = CreateServiceW(
      scm, kServiceName, kDisplayName, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
      SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, cmd.c_str(), nullptr, nullptr, nullptr,
      L"LocalSystem", nullptr);
  if (svc) {
    StartService(svc, 0, nullptr);
    CloseServiceHandle(svc);
  }
  CloseServiceHandle(scm);
  return svc ? 0 : 1;
}

int uninstallService() {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (!scm) return 1;
  SC_HANDLE svc = OpenServiceW(scm, kServiceName, SERVICE_STOP | DELETE);
  int rc = 1;
  if (svc) {
    SERVICE_STATUS st;
    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    rc = DeleteService(svc) ? 0 : 1;
    CloseServiceHandle(svc);
  }
  CloseServiceHandle(scm);
  return rc;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc > 1 && std::wstring(argv[1]) == L"install") return installService();
  if (argc > 1 && std::wstring(argv[1]) == L"uninstall") return uninstallService();

  SERVICE_TABLE_ENTRYW table[] = {{const_cast<wchar_t*>(kServiceName), serviceMain}, {nullptr, nullptr}};
  StartServiceCtrlDispatcherW(table);
  return 0;
}
