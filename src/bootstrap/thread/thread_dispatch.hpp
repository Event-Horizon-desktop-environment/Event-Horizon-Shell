#pragma once

#include <functional>

class DeferredCall {
public:

  static void callLater(std::function<void()> fn);

  static void drain();
};
