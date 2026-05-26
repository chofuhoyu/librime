//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <boost/algorithm/string.hpp>
#include <rime/candidate.h>
#include <rime/commit_history.h>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/key_event.h>
#include <rime/schema.h>
#include <rime/algo/encoder.h>
#include <rime/dict/dict_settings.h>
#include <rime/dict/reverse_lookup_dictionary.h>
#include <rime/dict/user_dictionary.h>
#include <rime/gear/phrase_encoder.h>

namespace rime {

static bool ParseFormula(const string& formula, vector<CodeCoords>* coords) {
  if (formula.length() % 2 != 0)
    return false;
  for (auto it = formula.cbegin(), end = formula.cend(); it != end;) {
    CodeCoords c;
    if (*it < 'A' || *it > 'Z')
      return false;
    c.char_index = (*it >= 'U') ? (*it - 'Z' - 1) : (*it - 'A');
    ++it;
    if (*it < 'a' || *it > 'z')
      return false;
    c.code_index = (*it >= 'u') ? (*it - 'z' - 1) : (*it - 'a');
    ++it;
    coords->push_back(c);
  }
  return true;
}

PhraseEncoder::PhraseEncoder(const Ticket& ticket) : Processor(ticket) {
  Config* config = ticket.schema->config();

  // Load hotkeys
  if (auto hotkeys = config->GetList("phrase_encoder/hotkeys")) {
    for (size_t i = 0; i < hotkeys->size(); ++i) {
      auto val = hotkeys->GetValueAt(i);
      if (val)
        hotkeys_.push_back(KeyEvent(val->str()));
    }
  }
  if (hotkeys_.empty())
    hotkeys_.push_back(KeyEvent("Control+equal"));

  // Create user dictionary (same pattern as PinProcessor)
  string dict_name;
  config->GetString("translator/dictionary", &dict_name);
  string db_class = "userdb";
  config->GetString("translator/db_class", &db_class);

  auto comp = UserDictionary::Require("user_dictionary");
  if (comp) {
    auto udc = static_cast<UserDictionaryComponent*>(comp);
    user_dict_.reset(udc->Create(dict_name, db_class));
    if (user_dict_) {
      user_dict_->Load();
      user_dict_->set_static_weights(true);

      bool sync = false;
      config->GetBool("translator/text_userdb_sync", &sync);
      user_dict_->set_text_userdb_sync(sync);
    }
  }

  // Create reverse lookup dictionary
  auto rev_component =
      ReverseLookupDictionary::Require("reverse_lookup_dictionary");
  if (rev_component) {
    Ticket rev_ticket(engine_, "translator");
    rev_dict_.reset(rev_component->Create(rev_ticket));
    if (rev_dict_) {
      rev_dict_->Load();
      // Load encoding rules from dict settings
      auto settings = rev_dict_->GetDictSettings();
      if (settings) {
        if (auto rules = settings->GetList("encoder/rules")) {
          for (auto it = rules->begin(); it != rules->end(); ++it) {
            auto rule = As<ConfigMap>(*it);
            if (!rule || !rule->HasKey("formula"))
              continue;
            const string formula(rule->GetValue("formula")->str());
            TableEncodingRule r;
            if (!ParseFormula(formula, &r.coords))
              continue;
            r.min_word_length = r.max_word_length = 0;
            if (auto value = rule->GetValue("length_equal")) {
              int length = 0;
              if (!value->GetInt(&length))
                continue;
              r.min_word_length = r.max_word_length = length;
            } else if (auto range =
                           As<ConfigList>(rule->Get("length_in_range"))) {
              if (range->size() != 2 ||
                  !range->GetValueAt(0)->GetInt(&r.min_word_length) ||
                  !range->GetValueAt(1)->GetInt(&r.max_word_length))
                continue;
            }
            encoding_rules_.push_back(r);
          }
        }
      }
      LOG(INFO) << "phrase_encoder: loaded " << encoding_rules_.size()
                << " encoding rules";
    }
  }
}

vector<string> PhraseEncoder::GetLastNTexts(int n) {
  vector<string> texts;
  const auto& history = engine_->context()->commit_history();
  for (auto it = history.rbegin(); it != history.rend(); ++it) {
    if (it->type == "thru")
      continue;
    texts.insert(texts.begin(), it->text);
    if (static_cast<int>(texts.size()) >= n)
      break;
  }
  return texts;
}

bool PhraseEncoder::ApplyFormula(const vector<string>& codes,
                                  const vector<CodeCoords>& coords,
                                  string* result) {
  int n = static_cast<int>(codes.size());
  for (auto& c : coords) {
    int char_idx = c.char_index;
    if (char_idx < 0)
      char_idx += n;
    if (char_idx < 0 || char_idx >= n)
      continue;
    int code_idx = c.code_index;
    if (code_idx < 0)
      code_idx += static_cast<int>(codes[char_idx].size());
    if (code_idx < 0 || code_idx >= static_cast<int>(codes[char_idx].size()))
      continue;
    *result += codes[char_idx][code_idx];
  }
  return !result->empty();
}

// Split a UTF-8 string into individual characters
static vector<string> SplitToChars(const string& text) {
  vector<string> chars;
  const char* p = text.data();
  const char* end = p + text.size();
  while (p < end) {
    size_t len = 1;
    unsigned char c = static_cast<unsigned char>(*p);
    if (c >= 0xFC)
      len = 6;
    else if (c >= 0xF8)
      len = 5;
    else if (c >= 0xF0)
      len = 4;
    else if (c >= 0xE0)
      len = 3;
    else if (c >= 0xC0)
      len = 2;
    chars.push_back(string(p, len));
    p += len;
  }
  return chars;
}

bool PhraseEncoder::EncodePhrase(const vector<string>& texts,
                                  string* code,
                                  string* phrase) {
  *phrase = boost::join(texts, "");
  if (texts.empty())
    return false;

  // Split each text into individual UTF-8 characters, then reverse-lookup
  vector<vector<string>> all_codes;
  for (auto& text : texts) {
    auto chars = SplitToChars(text);
    for (auto& ch : chars) {
      string str_list;
      if (!rev_dict_->LookupStems(ch, &str_list))
        rev_dict_->ReverseLookup(ch, &str_list);
      if (str_list.empty()) {
        LOG(INFO) << "phrase_encoder: no code found for '" << ch << "'";
        return false;
      }
      vector<string> codes;
      boost::split(codes, str_list, boost::is_any_of(" "));
      all_codes.push_back(codes);
    }
  }

  // Find matching rule and apply
  int n = static_cast<int>(all_codes.size());  // # of chars, not # of texts
  for (auto& rule : encoding_rules_) {
    if (n < rule.min_word_length || n > rule.max_word_length)
      continue;
    // Try first code for each character
    vector<string> attempt;
    for (auto& codes : all_codes)
      attempt.push_back(codes[0]);
    string result;
    if (ApplyFormula(attempt, rule.coords, &result)) {
      *code = result;
      return true;
    }
  }
  return false;
}

bool PhraseEncoder::CommitPhrase() {
  auto texts = GetLastNTexts(phrase_length_);
  if (static_cast<int>(texts.size()) < phrase_length_)
    return false;

  string code, phrase;
  if (!EncodePhrase(texts, &code, &phrase))
    return false;

  DictEntry e;
  e.text = phrase;
  e.custom_code = code + " ";
  user_dict_->UpdateEntry(e, 1, "", /*pin=*/true);
  LOG(INFO) << "phrase_encoder: created phrase '" << phrase << "' -> " << code;
  return true;
}

void PhraseEncoder::RefreshPreview() {
  auto texts = GetLastNTexts(phrase_length_);
  if (static_cast<int>(texts.size()) < phrase_length_) {
    engine_->context()->set_input(
        "造词(" + std::to_string(phrase_length_) + "字): 历史记录不足");
    return;
  }
  string code, phrase;
  if (!EncodePhrase(texts, &code, &phrase)) {
    engine_->context()->set_input(
        "造词: 编码失败");
    return;
  }
  // Count actual characters after splitting
  int char_count = 0;
  for (auto& text : texts)
    char_count += static_cast<int>(SplitToChars(text).size());
  engine_->context()->set_input(
      "造词(" + std::to_string(char_count) + "字): " + phrase + "\xe2\x86\x92" + code);
  // \xe2\x86\x92 = UTF-8 "→"
}

ProcessResult PhraseEncoder::ProcessKeyEvent(const KeyEvent& key_event) {
  if (!active_) {
    for (auto& hotkey : hotkeys_) {
      if (key_event == hotkey) {
        active_ = true;
        phrase_length_ = 2;
        RefreshPreview();
        return kAccepted;
      }
    }
    return kNoop;
  }

  // Active mode: handle navigation and action keys
  int ch = key_event.keycode();
  if (ch == XK_Left) {
    if (phrase_length_ > 2) {
      --phrase_length_;
      RefreshPreview();
    }
    return kAccepted;
  }
  if (ch == XK_Right) {
    if (phrase_length_ < 10) {
      ++phrase_length_;
      RefreshPreview();
    }
    return kAccepted;
  }
  if (ch == XK_Return || ch == XK_KP_Enter) {
    CommitPhrase();
    active_ = false;
    engine_->context()->set_input("");
    return kAccepted;
  }
  if (ch == XK_Escape) {
    active_ = false;
    engine_->context()->set_input("");
    return kAccepted;
  }
  return kAccepted;  // swallow all other keys
}

}  // namespace rime
