# EcoWatch Julia display logic as an ESPHome external component.
# The component has no settings: adding `ecowatch:` to a config only makes ESPHome compile
# ecowatch.h into the firmware, so the lambdas in ecowatch-julia-base.yaml can call ecowatch::...
import esphome.config_validation as cv

DEPENDENCIES = ["light"]

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    pass
