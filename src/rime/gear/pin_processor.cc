//
// Copyright RIME Developers
// Distributed under the BSD License
//

#include <rime/candidate.h>
#include <rime/composition.h>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/key_event.h>
#include <rime/menu.h>
#include <rime/schema.h>
#include <rime/dict/user_dictionary.h>
#include <rime/dict/vocabulary.h>
#include <rime/gear/pin_processor.h>
#include <rime/gear/translator_commons.h>

namespace rime {

static const char kPinTag[] = "pin-v0.1.0";

PinProcessor::PinProcessor(const Ticket& ticket) : Processor(ticket) {
  Config* config = ticket.schema->config();
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
    }
  }
  LOG(INFO) << kPinTag << " created, user_dict=" << (user_dict_ ? "ok" : "null");
}

ProcessResult PinProcessor::ProcessKeyEvent(const KeyEvent& key_event) {
  int ch = key_event.keycode();
  if (ch < '1' || ch > '9' || !key_event.ctrl())
    return kNoop;
  int index = ch - '1';

  Context* ctx = engine_->context();
  if (!ctx || !ctx->HasMenu()) {
    LOG(INFO) << kPinTag << " no context or no menu";
    return kNoop;
  }
  if (!user_dict_) {
    LOG(INFO) << kPinTag << " user_dict_ is null";
    return kNoop;
  }

  Segment& seg = ctx->composition().back();
  if (!seg.menu || seg.menu->Prepare(index + 1) <= index) {
    LOG(INFO) << kPinTag << " no menu or can't prepare index " << index;
    return kNoop;
  }

  auto cand = seg.menu->GetCandidateAt(index);
  if (!cand) {
    LOG(INFO) << kPinTag << " no candidate at index " << index;
    return kNoop;
  }
  auto phrase = As<Phrase>(Candidate::GetGenuineCandidate(cand));
  if (!phrase) {
    LOG(INFO) << kPinTag << " candidate is not a phrase";
    return kNoop;
  }

  string code_str = phrase->entry().custom_code;
  if (code_str.empty())
    code_str = ctx->input().substr(seg.start, seg.end - seg.start);
  if (code_str.empty()) {
    LOG(INFO) << kPinTag << " empty code_str";
    return kNoop;
  }

  DictEntry e;
  e.text = phrase->text();
  e.custom_code = code_str + " ";
  LOG(INFO) << kPinTag << " pinning text=" << e.text << " code=" << code_str;
  user_dict_->UpdateEntry(e, 1, "", /*pin=*/true);

  ctx->RefreshNonConfirmedComposition();
  return kAccepted;
}

}  // namespace rime
