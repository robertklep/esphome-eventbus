import logging
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome import automation
from esphome.const import CONF_ID, CONF_ON_EVENT, CONF_TRIGGER_ID, CONF_EVENT, CONF_ANY, CONF_DATA
from esphome.config_helpers import OrderedDict

_LOGGER = logging.getLogger(__name__)

eventbus_ns = cg.esphome_ns.namespace("eventbus")
EventBusComponent = eventbus_ns.class_("EventBusComponent", cg.Component)

AnyMap = eventbus_ns.class_("AnyMap")
AnyMapValue = eventbus_ns.class_("AnyMap::Value")

EventTrigger = eventbus_ns.class_("EventTrigger", automation.Trigger.template(cg.std_string, AnyMap))

# Action for emitting events from YAML
EventBusEmitAction = eventbus_ns.class_("EventBusEmitAction", automation.Action)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(EventBusComponent),
    cv.Optional(CONF_ON_EVENT): cv.ensure_list(
        automation.validate_automation(
            cv.Schema({
                cv.Optional(CONF_EVENT): cv.Any(cv.string, cv.ensure_list(cv.string)),
                cv.Optional(CONF_ANY): cv.Any(cv.boolean, None),
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(EventTrigger),
            }, extra=cv.ALLOW_EXTRA)
        )
    )
}).extend(cv.COMPONENT_SCHEMA)

# Custom validation for the any: key
def validate_event_config(config):
    for item_list in config.get(CONF_ON_EVENT, []):
        for item in item_list:
            # Handle the case where 'any:' is used without a value (should be treated as true)
            if CONF_ANY in item and item[CONF_ANY] is None:
                item[CONF_ANY] = True
            # Ensure that event and any are mutually exclusive
            if CONF_EVENT in item and CONF_ANY in item:
                raise cv.Invalid("Cannot specify both 'event:' and 'any:' in the same trigger")
            if CONF_ANY in item and not item[CONF_ANY]:
                raise cv.Invalid("'any:' must be true or empty")
            # Ensure event is either a string or a list of strings
            if event_value := item.get(CONF_EVENT):
                if isinstance(event_value, list):
                    if len(event_value) == 0:
                        raise cv.Invalid("'event:' list cannot be empty")
                    for event in event_value:
                        if not isinstance(event, str):
                            raise cv.Invalid("All items in 'event:' list must be strings")
                elif not isinstance(event_value, str):
                    raise cv.Invalid("'event:' must be a string or list of strings")
    return config

FINAL_VALIDATOR_SCHEMA = cv.All(CONFIG_SCHEMA, validate_event_config)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Process on_event automations
    for conf in config.get(CONF_ON_EVENT, []):
        # The configuration structure is different now
        # conf is a list where each item has 'event' or 'any' and automation
        for item in conf:
            # Find the automation part (it should have CONF_TRIGGER_ID)
            if isinstance(item, dict) and CONF_TRIGGER_ID in item:
                trigger = cg.new_Pvariable(item[CONF_TRIGGER_ID], var)

                if CONF_ANY in item:
                    # All events trigger
                    cg.add(var.add_trigger(trigger))
                elif event_config := item.get(CONF_EVENT):
                    # Specific event trigger
                    if isinstance(event_config, str):
                        # Single event name
                        event_name = event_config
                        cg.add(trigger.set_event_name(event_name))
                        cg.add(var.add_trigger_for_event(event_name, trigger))
                    else:
                        # List of event names
                        event_names = event_config
                        cg.add(trigger.set_event_names(event_names))
                        # For multiple events, add the trigger to all relevant event names
                        for event_name in event_names:
                            cg.add(var.add_trigger_for_event(event_name, trigger))

                await automation.build_automation(trigger, [
                    (cg.std_string, "event"),
                    (AnyMap, "data")
                ], item)

# Register the emit action
KEY_VALUE_SCHEMA = cv.Schema({cv.string: cv.templatable(cv.string)})

@automation.register_action(
    "eventbus.emit",
    EventBusEmitAction,
    cv.maybe_simple_value(
        {
            cv.GenerateID(): cv.use_id(EventBusComponent),
            cv.Required(CONF_EVENT): cv.templatable(cv.string),
            cv.Optional(CONF_DATA): cv.Any(cv.lambda_, KEY_VALUE_SCHEMA)  # Lambda that populates AnyMap
        },
        key=CONF_EVENT,
    ),
    synchronous = True
)
async def eventbus_emit_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    # Set event name
    templ_name = await cg.templatable(config[CONF_EVENT], args, cg.std_string)
    cg.add(var.set_event_name(templ_name))

    # Set event data if provided
    if conf_data := config.get(CONF_DATA):
        if isinstance(conf_data, OrderedDict):
            for key, value in conf_data.items():
                return_type = AnyMapValue if type(value).__name__ == 'Lambda' else cg.std_string
                templ = await cg.templatable(value, args, return_type)
                cg.add(var.set_data(key, templ))
        else:
            # Add data parameter to the lambda args
            args_with_data = args + [(AnyMap.operator("ref"), "data")]
            lambda_ = await cg.process_lambda(conf_data, args_with_data, return_type = cg.void)
            cg.add(var.set_data_lambda(lambda_))

    return var
