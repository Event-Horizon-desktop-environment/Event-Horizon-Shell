#pragma once

#include "core/dialog_base.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <unistd.h>

namespace hs::dialog {

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
    std::string cmd;
    if (mode_ == Mode::Save) {
      cmd = "zenity --file-selection --save --confirm-overwrite --title=\"";
      cmd += title_ + "\" --filename=\"" + suggested_name_ + "\" 2>/dev/null";
    } else {
      cmd = "zenity --file-selection --title=\"";
      cmd += title_ + "\" 2>/dev/null";
    }

    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
      result.response = -1;
      return result;
    }
    char buf[4096];
    if (fgets(buf, sizeof(buf), fp)) {
      size_t len = strlen(buf);
      if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
      if (strlen(buf) > 0) {
        result.uris.push_back(std::string("file://") + buf);
      }
    }
    int rc = pclose(fp);
    result.response = (rc == 0 && !result.uris.empty()) ? 0 : -1;
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
