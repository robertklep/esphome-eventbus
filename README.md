# ESPHome Event Bus

An event bus for ESPHome.

Allows emitting and handling events and is most useful for larger ESPHome projects that use [packages](https://esphome.io/components/packages/), where you want to limit the amount of dependencies between packages (for example, anywhere where package A directly references components/scripts/etc in package B).

See below for a rationale and more explanation on why this might be useful.

## Setup

Add the following to your project's YAML:

```yaml
external_components:
  - source: github://robertklep/esphome-eventbus@1.0.0
    components: [ eventbus ]
    refresh: never

eventbus:
  id: ev
```

You only have to assign an `id` if you want to emit events programmatically (see below). There's only one event bus instance active in a project.

## Emitting events

There are various ways of emitting events, starting with a regular ESPHome action:

```yaml
eventbus.emit:
  event: my_event
  data:
    name: Alice
    age: 23
    admin: 'false'
```

You can use lambdas for individual fields:
```yaml
eventbus.emit:
  event: my_event
  data:
    name: Alice
    age: 23
    admin: !lambda 'return false;'
```

Or a single lambda for all the fields:
```yaml
eventbus.emit:
  event: my_event
  data: !lambda |-
    data["name"] = "Alice";
    data["age"] = 23;
    data["admin"] = false;
```
(see below for more explanation on what `data` is, and how it works)

Or programmatically:
```c++
id(ev).emit("my_event",
  "name",  "Alice",
  "age",   23,
  "admin", false
);
```
Note how `ev` refers to the `id` in the `eventbus` component setup. There's also a global variable that you can always use: `esphome::eventbus::global_eventbus->emit(...)`

## Side note about `data`

The `data` variable that is available to event handler triggers, and also to action lambdas, is a fairly flexible container that holds key/value pairs. Keys are always strings, values can be most C++ types (integers, floats, booleans, string, `std` containers, etc).

It is loosely modeled after the [`JsonVariant` class from the ArduinoJSON library](https://arduinojson.org/v7/api/jsonvariant/).

### Extracting data by implicit or explicit casting

Generally speaking, it's up to you to keep track of the type of value you associate with a key: if you _emit_ a string value, you need to explicitly _extract_ a string value when handling the data.

For example, the last emit example above emits three key/value pairs:
* `name`, which is a string;
* `age`, which is an integer;
* `admin`, which is a boolean.

To extract their respective values inside a handler, you need to explicitly cast them to their respective type:
```c++
std::string name = data["name"];
unsigned int age = data["age"];
bool admin       = data["admin"];
```

Or, alternatively, use `.as<typename T><()`:
```c++
auto name  = data["name"].as<std::string>();
auto age   = data["age"].as<unsigned int>();
auto admin = data["admin"].as<bool>();
```

### Default/fallback values

Common C++ types will return a reasonable default value if you try to access a key that doesn't exist.

| Type                                         | Default value       |
| -------------------------------------------- | ------------------- |
| Integral types (`short`, `int`, `long`, etc) | `0`                 |
| Floating point types (`float`, `double`)     | `0`                 |
| `std:string`                                 | `""` (empty string) |
| `const char*`                                | `nullptr`           |
| `bool`                                       | `false`             |

Assign the default for an `unsigned int` (0) to `value`:
```c++
unsigned int value = data["missing"];
```

You can also specify a fallback value that should be used if a key doesn't exist, using the `|` operator:
```c++
unsigned int value = data["missing"] | 123;
```

### Type conversion

Some types can be converted from/to each other:

* strings to numbers (both integral and floating point types):
    ```c++
    // assigning
    data["num"] = "123";

    // extracting
    unsigned int num = data["num"];
    ```

* numbers (both integral and floating point types) to strings:
    ```c++
    // assigning
    data["num"] = 123;

    // extracting
    std::string num = data["num"];
    ```

* strings to booleans:
    ```c++
    // assigning
    data["b"] = "False";

    // extracting
    bool b = data["b"];
    ```

    Supported are strings that _start_ with `true` or `false` (case-independent) or strings containing numerical values (where `"0"` converts to `false`, and all other numerical values to `true`).

* `std::string` to `const char*`:

    ```c++
    // assigning
    data["str"] = std::string("string");

    // extracting
    const char* str = data["str"];
    ```

### Existence checking

You can check if a particular key exists using an explicit boolean cast:
```c++
if (data["somekey"]) {
  ...
}
```

Or using a method call:
```c++
if (data["somekey"].exists()) {
  ...
}
```

Possible pitfall: if `data["somekey"]` is a boolean, and you want to continue when it's true, you need to use a cast first:
```c++
if (data["somekey"].as<bool>()) {
  ...
}
```

### Type checking

Check if a key holds a value of a particular type:
```c++
if (data["name"].is<std::string>()) {
  ...
}
```

## Receiving and handling events

To receive and handle events, use the `on_event` trigger:
```yaml
eventbus:
  on_event:
    - event: my_event
      then:
        ...
```

It's also possible to trigger on multiple events:
```yaml
eventbus:
  on_event:
    - event:
        - my_event
        - my_other_event
      then:
        ...
```

Actions receive two arguments:
* `event`, a `std::string` that contains the event name (`my_event`, `my_other_event`, ...);
* `data`, the aforementioned key/value container.

```yaml
eventbus:
  on_event:
    - event:
        - my_event
        - my_other_event
      then:
        lambda: |-
          ESP_LOGD("", "Received event '%s'", event.c_str();

          if (event == "my_event") {
            const char* name = data["name"];
            ESP_LOGE("", "- name = %s", name);
          } else if (event == "my_other_event") {
            unsigned int age = data["age"];
            ESP_LOGE("", "- age = %u", age);
          }
```

## Rationale

In large ESPHome projects that are split up across different packages, you might run into issues where different parts of the project may need to respond to certain events/input/action/etc.

For instance, you may have an LVGL-based UI with a button that, when pressed, should publish a message over MQTT and turn on an LED.

An obvious way of implementing this could look like this:
```yaml
# light_package.yaml
light:
  - platform: binary
    id: my_led
    ...

# mqtt_package.yaml
mqtt:
  ...

# lvgl_package.yaml
lvgl:
  ...
  button:
    ...
    on_press:
      - light.turn_on: my_led
      - mqtt.publish: ...
```

Which works fine, but causes a lot of dependencies between different parts of the project. What if you need to make MQTT-support in your project optional? What if you also want to support LED-strips instead of just a single LED?

This is where the event bus comes in. Instead of hard-coding references to other components, you only emit an event when the button is pressed:
```yaml
button:
  ...
  on_press:
    eventbus.emit: button_is_pressed
```

Then, wherever you need to respond to button presses, you add an event listeners to the package (you can, of course, create event listeners anywhere you want, but I find that keeping related components and event listeners together helps with maintaining projects):
```yaml
# light_package.yaml
light:
  - platform: binary
    id: my_led

eventbus:
  on_event:
    event: button_is_pressed
    then:
      light.turn_on: my_led

# mqtt_package.yaml
mqtt:
  ...

eventbus:
  on_event:
    event: button_is_pressed
    then:
      mqtt.publish: ...
```

Then, when you want to build your project without MQTT support, you simply don't load the `mqtt_package.yaml` file and nothing will break.
