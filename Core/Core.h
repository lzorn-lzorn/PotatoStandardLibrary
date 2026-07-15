#pragma once

// Unified public entry point for external integration.
// Downstream users can include only this header:
//   #include <Core/Core.h>

#include "platform.h"

#include "exception_handler.h"
#include "logger.h"

#include "Containers/buffer.h"
#include "Containers/observed.h"
#include "Containers/mpmc_queue.h"
#include "Containers/radix_tree.h"

#include "math.h"
#include "time.h"

#include "Memory/memory_facade.h"
#include "Memory/Observability/memory_observability.h"
