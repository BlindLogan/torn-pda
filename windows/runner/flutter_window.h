#ifndef RUNNER_FLUTTER_WINDOW_H_
#define RUNNER_FLUTTER_WINDOW_H_

#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>

#include <memory>
#include <deque>
#include <string>
#include <flutter/method_channel.h>
#include <flutter/encodable_value.h>
#include <windows.h>
#include <commctrl.h>

#include "win32_window.h"

// A window that does nothing but host a Flutter view.
class FlutterWindow : public Win32Window {
 public:
  // Creates a new FlutterWindow hosting a Flutter view running |project|.
  explicit FlutterWindow(const flutter::DartProject& project);
  virtual ~FlutterWindow();

 protected:
  // Win32Window:
  bool OnCreate() override;
  void OnDestroy() override;
  LRESULT MessageHandler(HWND window, UINT const message, WPARAM const wparam,
                         LPARAM const lparam) noexcept override;

 private:
  void RecordFocusEvent(const std::string& event);
  std::string FocusReport() const;
  static LRESULT CALLBACK DiagnosticChildProc(HWND hwnd, UINT message,
      WPARAM wparam, LPARAM lparam, UINT_PTR subclass_id,
      DWORD_PTR reference_data);
  std::deque<std::string> focus_events_;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> diagnostic_channel_;
  bool has_deactivated_ = false;
  // The project to run.
  flutter::DartProject project_;

  // The Flutter instance hosted by this window.
  std::unique_ptr<flutter::FlutterViewController> flutter_controller_;
};

#endif  // RUNNER_FLUTTER_WINDOW_H_
