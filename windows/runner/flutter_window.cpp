#include "flutter_window.h"

#include <optional>
#include <sstream>
#include <fstream>
#include <flutter/standard_method_codec.h>

#include "flutter/generated_plugin_registrant.h"

FlutterWindow::FlutterWindow(const flutter::DartProject& project)
    : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
  if (!Win32Window::OnCreate()) {
    return false;
  }

  RECT frame = GetClientArea();

  // The size here must match the window dimensions to avoid unnecessary surface
  // creation / destruction in the startup path.
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  // Ensure that basic setup of the controller was successful.
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    return false;
  }
  RegisterPlugins(flutter_controller_->engine());
  SetChildContent(flutter_controller_->view()->GetNativeWindow());
  SetWindowSubclass(flutter_controller_->view()->GetNativeWindow(),
                    DiagnosticChildProc, 7, reinterpret_cast<DWORD_PTR>(this));
  diagnostic_channel_ = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
      flutter_controller_->engine()->messenger(),
      "torn_pda/windows_focus_diagnostics",
      &flutter::StandardMethodCodec::GetInstance());
  diagnostic_channel_->SetMethodCallHandler(
      [this](const flutter::MethodCall<flutter::EncodableValue>& call,
             std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
        if (call.method_name() == "getReport") {
          RecordFocusEvent("report requested");
          result->Success(flutter::EncodableValue(FocusReport()));
        } else if (call.method_name() == "refreshView") {
          flutter_controller_->ForceRedraw();
          RecordFocusEvent("resume redraw requested");
          result->Success();
        } else {
          result->NotImplemented();
        }
      });
  RecordFocusEvent("diagnostics started");

  flutter_controller_->engine()->SetNextFrameCallback([&]() {
    this->Show();
  });

  // Flutter can complete the first frame before the "show window" callback is
  // registered. The following call ensures a frame is pending to ensure the
  // window is shown. It is a no-op if the first frame hasn't completed yet.
  flutter_controller_->ForceRedraw();

  return true;
}

void FlutterWindow::OnDestroy() {
  if (flutter_controller_) {
    RemoveWindowSubclass(flutter_controller_->view()->GetNativeWindow(), DiagnosticChildProc, 7);
    diagnostic_channel_.reset();
    flutter_controller_ = nullptr;
  }

  Win32Window::OnDestroy();
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                              WPARAM const wparam,
                              LPARAM const lparam) noexcept {
  if (message == WM_ACTIVATE) {
    const bool inactive = LOWORD(wparam) == WA_INACTIVE;
    RecordFocusEvent(inactive ? "host deactivate" : "host activate");
    if (inactive) {
      has_deactivated_ = true;
    } else if (has_deactivated_) {
      PostMessage(hwnd, WM_APP + 19, 0, 0);
    }
  } else if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
    RecordFocusEvent(message == WM_SETFOCUS ? "host focus gained" : "host focus lost");
  } else if (message == WM_SIZE) {
    RecordFocusEvent("host resized");
  }
  if (message == WM_APP + 19 && flutter_controller_) {
    const HWND view = flutter_controller_->view()->GetNativeWindow();
    RECT bounds;
    if (GetClientRect(hwnd, &bounds)) {
      const int width = bounds.right - bounds.left;
      const int height = bounds.bottom - bounds.top;
      if (width > 1 && height > 0) {
        // A real resize reliably restores JAWS. Resize only Flutter's child
        // surface by one pixel and immediately restore it, leaving the visible
        // application window unchanged.
        SetWindowPos(view, nullptr, 0, 0, width - 1, height,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        SetWindowPos(view, nullptr, 0, 0, width, height,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        // Prompt assistive technology to query Flutter's client
        // accessibility provider again after the application is reactivated.
        NotifyWinEvent(EVENT_OBJECT_FOCUS, view, OBJID_CLIENT, CHILDID_SELF);
        RecordFocusEvent("child surface refresh and accessibility focus notification");
      }
    }
    return 0;
  }
  // Give Flutter, including plugins, an opportunity to handle window messages.
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  switch (message) {
    case WM_FONTCHANGE:
      flutter_controller_->engine()->ReloadSystemFonts();
      break;
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}

std::string FlutterWindow::FocusReport() const {
  std::ostringstream report;
  report << "Torn PDA focus diagnostic test 10\n"
         << "Native focus and navigation events only; no account data or typed text.\n";
  for (const auto& event : focus_events_) report << event << '\n';
  return report.str();
}

void FlutterWindow::RecordFocusEvent(const std::string& event) {
  if (!flutter_controller_) return;
  const HWND view = flutter_controller_->view()->GetNativeWindow();
  const HWND focus = GetFocus();
  const char* owner = focus == view ? "flutter" :
      focus == GetHandle() ? "host" : focus == nullptr ? "none" : "other";
  focus_events_.push_back(std::to_string(GetTickCount64()) + " " + event +
      " keyboard_focus=" + owner +
      " foreground=" + (GetForegroundWindow() == GetHandle() ? "app" : "other"));
  if (focus_events_.size() > 500) focus_events_.pop_front();
  // Local-only fallback: still available if the screen reader stops navigating.
  wchar_t local_data[32768];
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_data, 32768);
  if (length == 0 || length >= 32768) return;
  const std::wstring directory = std::wstring(local_data) + L"\\TornPDA";
  CreateDirectoryW(directory.c_str(), nullptr);
  std::ofstream output(directory + L"\\focus-test10.txt", std::ios::trunc);
  if (output) output << FocusReport();
}

LRESULT CALLBACK FlutterWindow::DiagnosticChildProc(HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam, UINT_PTR subclass_id, DWORD_PTR reference_data) {
  auto* window = reinterpret_cast<FlutterWindow*>(reference_data);
  if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
    window->RecordFocusEvent(message == WM_SETFOCUS ? "flutter focus gained" : "flutter focus lost");
  } else if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (wparam == VK_TAB || wparam == VK_RETURN ||
        (wparam >= VK_LEFT && wparam <= VK_DOWN) ||
        (alt && wparam >= '0' && wparam <= '9')) {
      window->RecordFocusEvent("flutter navigation key=" + std::to_string(wparam) +
          " alt=" + (alt ? "yes" : "no"));
    }
    if (message == WM_SYSKEYDOWN && alt && wparam >= '0' && wparam <= '9') {
      if (window->diagnostic_channel_) {
        window->diagnostic_channel_->InvokeMethod(
            "altNumber",
            std::make_unique<flutter::EncodableValue>(
                static_cast<int>(wparam - '0')));
      }
      window->RecordFocusEvent("native Alt+number dispatched");
      return 0;
    }
  } else if (message == WM_SYSCHAR && wparam >= '0' && wparam <= '9') {
    // The shortcut was handled above, so prevent Windows' menu-error ding.
    return 0;
  }
  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, DiagnosticChildProc, subclass_id);
  }
  return DefSubclassProc(hwnd, message, wparam, lparam);
}
