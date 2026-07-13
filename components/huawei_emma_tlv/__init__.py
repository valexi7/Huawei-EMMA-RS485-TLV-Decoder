from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CoroPriority, coroutine_with_priority


CODEOWNERS = ["@valexi7"]
DEPENDENCIES = ["uart"]

CONFIG_SCHEMA = cv.Schema({})

DECODER_SOURCE = Path(__file__).with_name("huawei_proprietary.inc")


@coroutine_with_priority(CoroPriority.FINAL)
async def to_code(config):
    # The decoder intentionally uses the template sensor IDs declared by the
    # package. Inject its source after code generation has declared those
    # globals. The non-header extension prevents ESPHome from also adding the
    # file to its early aggregate include block.
    cg.add_global(cg.RawStatement(DECODER_SOURCE.read_text(encoding="utf-8")))
