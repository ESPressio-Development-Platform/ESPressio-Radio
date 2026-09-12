#pragma once

#include "ESPressio_RadioTypes.hpp"
#include "ESPressio_RadioProviderContract.hpp"
#include "ESPressio_IRadio.hpp"
#include "ESPressio_RadioServiceProfile.hpp"
#include "ESPressio_RadioCapacity.hpp"
#include "ESPressio_RadioWireV3.hpp"
#include "ESPressio_RadioTransferId.hpp"
#include "ESPressio_RadioReassembly.hpp"
#include "ESPressio_RadioScheduler.hpp"
#include "ESPressio_RadioDomainService.hpp"
#include "ESPressio_RadioDomainRuntime.hpp"
#include "ESPressio_RadioClockWireV1.hpp"
#include "ESPressio_RadioIngressRouter.hpp"
#include "ESPressio_RadioClockCoordinator.hpp"
#include "ESPressio_RadioRuntime.hpp"

// Predecessor observer/control/worker surfaces remain temporarily source-addressable during Tranche 7 migration,
// but are intentionally no longer part of the canonical umbrella. R7-19..R7-21 delete them after the replacement
// runtime/Clock orchestration and provider migrations are validated.
