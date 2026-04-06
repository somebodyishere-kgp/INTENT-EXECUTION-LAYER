#pragma once

#include "CapabilityExtractor.h"
#include "Intent.h"

namespace iee {

class IntentNormalizer {
public:
    Intent Normalize(const Capability& capability) const;
};

}  // namespace iee
