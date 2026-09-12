#!/usr/bin/env python3
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
forbidden_sources = [
    "ESPressio_DeferredLogicalTransferObserverBridge.hpp",
    "ESPressio_DeferredLogicalTransferTracker.hpp",
    "ESPressio_RadioClockSynchronizationDiagnostics.hpp",
    "ESPressio_RadioClockSynchronizer.hpp",
    "ESPressio_RadioControl.hpp",
    "ESPressio_RadioControlWorker.hpp",
    "ESPressio_RadioEventBridge.hpp",
    "ESPressio_RadioEvents.hpp",
    "ESPressio_RadioObservers.hpp",
    "ESPressio_RadioTransport.hpp",
    "ESPressio_RadioWorker.hpp",
]
for name in forbidden_sources:
    assert not (root / "src" / name).exists(), f"predecessor source remains: {name}"

manifest = json.loads((root / "library.json").read_text())
deps = [entry["name"] for entry in manifest.get("dependencies", [])]
assert deps == ["ESPressio-System", "ESPressio-Task", "ESPressio-Timing", "ESPressio-Units"], deps
assert not (root / ".github" / "workflows" / "test.yml").exists(), "legacy predecessor workflow remains"

source_text = "\n".join(path.read_text(errors="ignore") for path in (root / "src").glob("*.hpp"))
for forbidden in ("ESPressio-Observable", "ESPressio-Event", "ESPressio-Threads", "PrecisionThread"):
    assert forbidden not in source_text, f"forbidden canonical dependency/surface remains: {forbidden}"
