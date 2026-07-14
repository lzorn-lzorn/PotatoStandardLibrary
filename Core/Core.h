#pragma once

// Unified public entry point for external integration.
// Downstream users can include only this header:
//   #include <Core/Core.h>

#include "Foundation/common.h"
#include "Foundation/std_interface.h"
#include "Foundation/enum_flag.h"

#include "Platform/platform.h"

#include "Diagnostics/exception_handler.h"
#include "Diagnostics/logger.h"

#include "Containers/buffer.h"
#include "Containers/observed.h"
#include "Containers/mpmc_queue.h"
#include "Containers/radix_tree.h"

#include "Math/math.h"
#include "Text/string.h"
#include "Time/time.h"

#include "Memory/memory_facade.h"
#include "Memory/Observability/memory_observability.h"
