//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <algorithm>
#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/translation.h>
#include <rime/gear/fixed_position_filter.h>
#include <rime/gear/translator_commons.h>

namespace rime {

class FixedPositionTranslation : public Translation {
 public:
  explicit FixedPositionTranslation(an<Translation> translation) {
    if (!translation)
      return;

    // Collect and separate in one pass
    vector<pair<int, an<Candidate>>> fixed;  // (position, candidate)
    vector<an<Candidate>> regular;
    vector<an<Candidate>> raw_entries;  // echo/raw: only shown if nothing else matches

    while (!translation->exhausted()) {
      auto c = translation->Peek();
      auto genuine = Candidate::GetGenuineCandidate(c);
      auto phrase = As<Phrase>(genuine);
      if (phrase && phrase->type() == "custom_phrase" &&
          phrase->entry().commit_count > 0) {
        int pos = phrase->entry().commit_count;
        bool duplicate = false;
        for (auto& kv : fixed) {
          if (kv.first == pos) {
            duplicate = true;
            break;
          }
        }
        if (!duplicate)
          fixed.emplace_back(pos, c);
      } else if (genuine && genuine->type() == "raw") {
        // echo entries: normally suppressed by EchoTranslation::Compare()
        // when other candidates exist. FixedPositionFilter's eager consumption
        // breaks this, so we explicitly drop them unless there's nothing else.
        raw_entries.push_back(c);
      } else {
        regular.push_back(c);
      }
      translation->Next();
    }

    // Sort fixed entries by position
    std::sort(fixed.begin(), fixed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Interleave: for each position, insert fixed entry or next regular
    size_t ri = 0;
    size_t fi = 0;
    int pos = 1;
    while (ri < regular.size() && fi < fixed.size()) {
      if (fixed[fi].first == pos) {
        result_.push_back(fixed[fi].second);
        fi++;
      } else {
        result_.push_back(regular[ri++]);
      }
      pos++;
    }

    // Append remaining regular entries
    while (ri < regular.size())
      result_.push_back(regular[ri++]);

    // Append remaining fixed entries (positions beyond regular count)
    while (fi < fixed.size())
      result_.push_back(fixed[fi++].second);

    // Only show echo/raw entries when nothing else matched
    if (result_.empty()) {
      for (auto& c : raw_entries)
        result_.push_back(c);
    }

    DLOG(INFO) << "fixed_position_filter: " << result_.size()
               << " candidates (fixed=" << fixed.size() << ")";
  }

  bool Next() override {
    if (++index_ < result_.size())
      return true;
    set_exhausted(true);
    return false;
  }

  an<Candidate> Peek() override {
    if (exhausted() || index_ >= result_.size())
      return nullptr;
    return result_[index_];
  }

 private:
  vector<an<Candidate>> result_;
  size_t index_ = 0;
};

FixedPositionFilter::FixedPositionFilter(const Ticket& ticket)
    : Filter(ticket) {}

an<Translation> FixedPositionFilter::Apply(an<Translation> translation,
                                            CandidateList* candidates) {
  return New<FixedPositionTranslation>(translation);
}

}  // namespace rime
