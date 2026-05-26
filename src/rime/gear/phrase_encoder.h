//
// Copyright RIME Developers
// Distributed under the BSD License
//
#ifndef RIME_PHRASE_ENCODER_H_
#define RIME_PHRASE_ENCODER_H_

#include <rime/common.h>
#include <rime/component.h>
#include <rime/processor.h>
#include <rime/algo/encoder.h>

namespace rime {

class UserDictionary;
class ReverseLookupDictionary;

class PhraseEncoder : public Processor {
 public:
  explicit PhraseEncoder(const Ticket& ticket);
  ProcessResult ProcessKeyEvent(const KeyEvent& key_event) override;

 private:
  void RefreshPreview();
  bool CommitPhrase();
  vector<string> GetLastNTexts(int n);
  bool EncodePhrase(const vector<string>& texts,
                    string* code,
                    string* phrase);
  bool ApplyFormula(const vector<string>& codes,
                    const vector<CodeCoords>& coords,
                    string* result);

  bool active_ = false;
  int phrase_length_ = 2;
  string pending_code_;   // pre-committed code for Esc undo
  string pending_phrase_;  // pre-committed phrase text for Esc undo
  vector<KeyEvent> hotkeys_;
  the<UserDictionary> user_dict_;
  an<ReverseLookupDictionary> rev_dict_;
  vector<TableEncodingRule> encoding_rules_;
};

}  // namespace rime

#endif  // RIME_PHRASE_ENCODER_H_
