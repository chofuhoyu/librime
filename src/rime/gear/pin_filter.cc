//
// Copyright RIME Developers
// Distributed under the BSD License
//

#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/translation.h>
#include <rime/gear/pin_filter.h>

namespace rime {

class PinFilterTranslation : public CacheTranslation {
 public:
  PinFilterTranslation(an<Translation> translation)
      : CacheTranslation(translation) {}

  bool Next() override {
    if (exhausted())
      return false;
    auto cand = Peek();
    if (cand) {
      const string& type = Candidate::GetGenuineCandidate(cand)->type();
      if (type == "user_table") {
        seen_.insert(cand->text());
      }
    }
    do {
      CacheTranslation::Next();
      if (exhausted())
        return false;
      cand = Peek();
      const string& type = Candidate::GetGenuineCandidate(cand)->type();
      if (type == "table" && seen_.count(cand->text())) {
        // 系统码表项，且同一 text 已在用户词典中出现过 → 跳过
        continue;
      }
      if (type == "user_table") {
        seen_.insert(cand->text());
      }
      break;
    } while (true);
    return true;
  }

 private:
  set<string> seen_;
};

PinFilter::PinFilter(const Ticket& ticket) : Filter(ticket) {}

an<Translation> PinFilter::Apply(an<Translation> translation,
                                  CandidateList* candidates) {
  return New<PinFilterTranslation>(translation);
}

}  // namespace rime
