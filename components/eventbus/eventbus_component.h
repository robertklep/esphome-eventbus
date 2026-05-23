// vim:ft=cpp:
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "anymap.h"
#include <string>
#include <vector>

namespace esphome::eventbus {

class EventTrigger;

using event_data_t = AnyMap;

class EventBusComponent : public Component {
public:
  EventBusComponent();
  void setup() override;
  void loop() override;
  void dump_config() override;

  // Method to trigger an event (can be called from C++)
  void trigger_event(const std::string &event_name, const event_data_t event_data);

  // Method to emit an event (can be called from YAML)
  void emit(const std::string &event_name, const event_data_t event_data);
  void emit(const std::string &event_name);

  // no args left
  void add_pairs(AnyMap& map) {
    // nothing to do
  }

  template<typename T, typename... Args>
  void add_pairs(AnyMap& map, const char* key, T value, Args... args) {
    map[key] = value;
    add_pairs(map, args...);
  }

  template<typename... Args>
  void emit(const std::string& event_name, Args... args) {
    static_assert(sizeof...(args) % 2 == 0, "Arguments must be key-value pairs");
    AnyMap map;
    add_pairs(map, args...);
    emit(event_name, map);
  }

  // Add a trigger for all events
  void add_trigger(EventTrigger *trigger);

  // Add a trigger for a specific event
  void add_trigger_for_event(const std::string &event_name, EventTrigger *trigger);

protected:
  std::vector<EventTrigger *> all_event_triggers_;
  std::vector<std::pair<std::string, EventTrigger *>> specific_event_triggers_;
};

// Custom trigger that provides event, args, and arg variables
class EventTrigger : public Trigger<std::string, event_data_t> {
public:
  explicit EventTrigger(EventBusComponent *parent) : parent_(parent) {}

  // Set the event name(s) for specific event triggers
  void set_event_name(const std::string &event_name) { event_names_ = {event_name}; }
  void set_event_names(const std::vector<std::string> &event_names) { event_names_ = event_names; }

  // Check if this trigger should fire for the given event
  bool should_trigger(const std::string &event_name) const {
    // If event_names_ is empty, this is an all-event trigger
    if (this->event_names_.empty()) {
      return true;
    }
    // Otherwise, check if the event matches any of the specified event names
    for (const auto &name : this->event_names_) {
      if (name == event_name) {
        return true;
      }
    }
    return false;
  }

  // Check if event names are empty (for add_trigger_for_event)
  bool has_event_names() const { return !event_names_.empty(); }

  // Custom trigger method to handle event triggering
  void trigger(const std::string &event_name, const event_data_t args) {
    Trigger<std::string, event_data_t>::trigger(event_name, args);
  }

protected:
  EventBusComponent *parent_;
  std::vector<std::string> event_names_;  // Empty for all-event triggers
};

// Action for emitting events from YAML
template<typename... Ts> class EventBusEmitAction : public Action<Ts...> {
private:
  using Lambda = std::function<AnyMap::Value(Ts...)>;

  std::unordered_map<std::string, Lambda> lambdas_;
  AnyMap data_;
public:
  void set_parent(EventBusComponent *parent) { parent_ = parent; }

  TEMPLATABLE_VALUE(std::string, event_name)

  template <typename F>
  requires (std::is_invocable_v<F, Ts...>)
  void set_data(const std::string& key, F&& func) {
    this->lambdas_.emplace(key, std::move(func));
  }

  void set_data(const std::string& key, const char* value) {
    this->data_[key] = std::string(value);
  }

  void set_data_lambda(std::function<void(Ts..., event_data_t&)> data_lambda) {
    this->data_lambda_ = data_lambda;
  }

  void play(const Ts &...xs) override {
    if (this->parent_) {
      std::string name = this->event_name_.value(xs...);

      if (! this->data_lambda_) {
        // execute all stored lambdas and update data
        for (auto& [ key, value ] : this->lambdas_) {
          this->data_[key] = value(xs...);
        }
        // emit data
        this->parent_->emit(name, this->data_);
        return;
      }

      // create map and pass it as `data` argument to lambda
      AnyMap map;
      this->data_lambda_(xs..., map);
      this->parent_->emit(name, map);
    }
  }

 protected:
  EventBusComponent *parent_{nullptr};
  std::function<void(Ts..., event_data_t&)> data_lambda_;
};

extern EventBusComponent *global_eventbus; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

} // namespace esphome::eventbus
