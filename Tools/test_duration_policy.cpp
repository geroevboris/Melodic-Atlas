#include "AtlasDurationPolicy.h"
#include <cassert>
#include <limits>

int main()
{
    using Policy = Atlas::AtlasDurationPolicy;
    static_assert(Policy::minimumSeconds == 2.0);
    assert(!Policy::isEligible(-1.0));
    assert(!Policy::isEligible(0.0));
    assert(!Policy::isEligible(1.99999));
    assert(Policy::isEligible(2.0));
    assert(Policy::isEligible(2.00001));
    assert(!Policy::isEligible(std::numeric_limits<double>::quiet_NaN()));
    assert(!Policy::isEligible(std::numeric_limits<double>::infinity()));
}
