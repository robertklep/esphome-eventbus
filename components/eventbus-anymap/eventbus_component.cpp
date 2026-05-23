#include "eventbus_component.h"
#include "esphome/core/log.h"

namespace esphome::eventbus {

static const char *const TAG = "eventbus";

EventBusComponent::EventBusComponent() {
  global_eventbus = this;
}

void EventBusComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Event Bus...");
  // no need to loop
  this->disable_loop();
}

void EventBusComponent::loop() {
}

void EventBusComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
    "EventBus:\n"
    "  Number of ANY event triggers     : %zu\n"
    "  Number of specific event triggers: %zu\n",
    this->all_event_triggers_.size(),
    this->specific_event_triggers_.size()
  );
}

void EventBusComponent::trigger_event(const std::string &event_name, const event_data_t event_data) {
  // Trigger all-event triggers
  for (auto *trigger : this->all_event_triggers_) {
    if (trigger->should_trigger(event_name)) {
      trigger->trigger(event_name, event_data);
    }
  }

  // Trigger specific event triggers
  for (auto &pair : this->specific_event_triggers_) {
    if (pair.first == event_name) {
      if (pair.second->should_trigger(event_name)) {
        pair.second->trigger(event_name, event_data);
      }
    }
  }
}

// Alias for emit (same as trigger_event)
void EventBusComponent::emit(const std::string &event_name, const event_data_t event_data) {
  this->trigger_event(event_name, event_data);
}

// Overloaded emit method without data
void EventBusComponent::emit(const std::string &event_name) {
  this->trigger_event(event_name, event_data_t{});
}

void EventBusComponent::add_trigger(EventTrigger *trigger) {
  this->all_event_triggers_.push_back(trigger);
}

void EventBusComponent::add_trigger_for_event(const std::string &event_name, EventTrigger *trigger) {
  // Only set the event name if it's not already set (to avoid overwriting multi-event triggers)
  if (! trigger->has_event_names()) {
    trigger->set_event_name(event_name);
  }
  this->specific_event_triggers_.emplace_back(event_name, trigger);
}

EventBusComponent *global_eventbus; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace esphome::eventbus
