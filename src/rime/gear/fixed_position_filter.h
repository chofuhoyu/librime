//
// Copyright RIME Developers
// Distributed under the BSD License
//

#ifndef RIME_FIXED_POSITION_FILTER_H_
#define RIME_FIXED_POSITION_FILTER_H_

#include <rime/filter.h>

namespace rime {

class FixedPositionFilter : public Filter {
 public:
  explicit FixedPositionFilter(const Ticket& ticket);
  an<Translation> Apply(an<Translation> translation,
                         CandidateList* candidates) override;
};

}  // namespace rime

#endif  // RIME_FIXED_POSITION_FILTER_H_
