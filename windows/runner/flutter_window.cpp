#include "flutter_window.h"

#include <optional>
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
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    return false;
  }

  RegisterPlugins(flutter_controller_->engine());
  SetChildContent(flutter_controller_->view()->GetNativeWindow());
  SetWindowSubclass(flutter_controller_->view()->GetNativeWindow(),
                    AccessibilityChildProc, 7,
                    reinterpret_cast<DWORD_PTR>(this));

  accessibility_channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          flutter_controller_->engine()->messenger(),
          "torn_pda/windows_accessibility",
          &flutter::StandardMethodCodec::GetInstance());

  flutter_controller_->engine()->SetNextFrameCallback([&]() { this->Show(); });
  flutter_controller_->ForceRedraw();
  return true;
}

void FlutterWindow::OnDestroy() {
  if (flutter_controller_) {
    RemoveWindowSubclass(flutter_controller_->view()->GetNativeWindow(),
                         AccessibilityChildProc, 7);
    accessibility_channel_.reset();
    flutter_controller_ = nullptr;
  }
  Win32Window::OnDestroy();
}

LRESULT FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                                      WPARAM const wparam,
                                      LPARAM const lparam) noexcept {
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  if (message == WM_FONTCHANGE && flutter_controller_) {
    flutter_controller_->engine()->ReloadSystemFonts();
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK FlutterWindow::AccessibilityChildProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass_id, DWORD_PTR reference_data) {
  auto* window = reinterpret_cast<FlutterWindow*>(reference_data);
  const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

  if (message == WM_SYSKEYDOWN && alt && wparam >= '0' && wparam <= '9') {
    if (window->accessibility_channel_) {
      window->accessibility_channel_->InvokeMethod(
          "altNumber",
          std::make_unique<flutter::EncodableValue>(
              static_cast<int>(wparam - '0')));
    }
    return 0;
  }

  if (message == WM_SYSCHAR && wparam >= '0' && wparam <= '9') {
    return 0;
  }

  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(hwnd, AccessibilityChildProc, subclass_id);
  }
  return DefSubclassProc(hwnd, message, wparam, lparam);
}
