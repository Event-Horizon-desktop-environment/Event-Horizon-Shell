#pragma once

// Output and connection helpers shared by the compositor backends.
//
// Split out of the old shared header: each of these is an independent
// capability. A backend takes the ones its protocol needs and is not forced
// to inherit the rest.
//
// Naming note: this project uses `setOutputResolver` / `openConnection` /
// `outputAttached` / `outputDetached`. Another project uses
// `setOutputNameResolver` / `connectSocket` / `onOutputAdded`. Keep them
// distinct — see Docs/Audit/PROVENANCE.md §7.

#include <functional>
#include <string>

struct wl_output;

// Resolves a wl_output handle to the connector name the compositor uses.
class IOutputResolver {
public:
  using Resolver = std::function<std::string(wl_output*)>;

  virtual ~IOutputResolver() = default;
  virtual void setOutputResolver(Resolver resolver) = 0;
};

// A backend that owns a socket it can open on demand.
class ILinkOpener {
public:
  virtual ~ILinkOpener() = default;
  [[nodiscard]] virtual bool openConnection() = 0;
};

// A backend interested in outputs appearing and disappearing.
class IOutputLifecycle {
public:
  virtual ~IOutputLifecycle() = default;
  virtual void outputAttached(wl_output* output) = 0;
  virtual void outputDetached(wl_output* output) = 0;
};
