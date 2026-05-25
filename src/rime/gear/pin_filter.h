//
// Copyright RIME Developers
// Distributed under the BSD License
//

#ifndef RIME_PIN_FILTER_H_
#define RIME_PIN_FILTER_H_

#include <rime/filter.h>

namespace rime {

class PinFilter : public Filter {
 public:
  explicit PinFilter(const Ticket& ticket);
  an<Translation> Apply(an<Translation> translation, CandidateList* candidates) override;
};

}  // namespace rime

#endif  // RIME_PIN_FILTER_H_
