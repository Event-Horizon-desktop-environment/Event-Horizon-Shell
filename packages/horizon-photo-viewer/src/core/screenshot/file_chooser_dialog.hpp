#pragma once

#include "core/screenshot/dialog_base.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <unistd.h>

namespace hpv::sc::dialog {

class FileChooserDialog : public DialogBase {
public:
  enum class Mode {
    Open,
    Save,
    SaveFiles,
    Directory,
  };

  FileChooserDialog(Mode mode, const std::string& title,
                     const std::string& mime_filter = "",
                     bool allow_multiple = false)
    : mode_(mode), title_(title), mime_filter_(mime_filter),
      allow_multiple_(allow_multiple) {}

  void set_suggested_filename(const std::string& name) { suggested_name_ = name; }

  Result run() override
  {
    Result result;
    result.response = -1;
    return result;
  }

private:
  Mode mode_ = Mode::Open;
  std::string title_;
  std::string mime_filter_;
  bool allow_multiple_ = false;
  std::string suggested_name_;
};

}
