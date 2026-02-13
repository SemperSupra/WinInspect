#pragma once
#include <string>
#include <vector>
#include <functional>

namespace wininspect_gui {

struct Node {
  std::string hwnd;
  std::string label;
  std::vector<Node> children;
};

struct Property {
  std::string key;
  std::string value;
};

struct ITransport {
  virtual ~ITransport() = default;
  virtual std::string request(const std::string& json) = 0; // sync for v1
};

class ViewModel {
public:
  explicit ViewModel(ITransport* t);

  // Pure-ish operations that can be unit tested.
  void refresh();
  void select_hwnd(const std::string& hwnd);

  const std::vector<Node>& tree() const { return tree_; }
  const std::vector<Property>& props() const { return props_; }

private:
  ITransport* t_;
  std::vector<Node> tree_;
  std::vector<Property> props_;
};

} // namespace wininspect_gui
