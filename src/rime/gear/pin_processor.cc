//
// Copyright RIME Developers
// Distributed under the BSD License
//

#include <rime/candidate.h>
#include <rime/common.h>
#include <rime/composition.h>
#include <rime/config.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/key_event.h>
#include <rime/schema.h>
#include <rime/dict/dict_entry.h>
#include <rime/dict/user_dictionary.h>
#include <rime/gear/pin_processor.h>
#include <rime/gear/translator_commons.h>

namespace rime {

PinProcessor::PinProcessor(const Ticket& ticket) : Processor(ticket) {}

ProcessResult PinProcessor::ProcessKeyEvent(const KeyEvent& key_event) {
  int ch = key_event.keycode();
  if (ch < '1' || ch > '9' || !key_event.ctrl())
    return kNoop;
  int index = ch - '1';

  Context* ctx = engine_->context();
  if (!ctx || !ctx->HasMenu())
    return kNoop;

  Segment& seg = ctx->composition().back();
  if (!seg.menu || seg.menu->Prepare(index + 1) <= index)
    return kNoop;

  auto cand = seg.menu->GetCandidateAt(index);
  if (!cand)
    return kNoop;
  auto phrase = As<Phrase>(Candidate::GetGenuineCandidate(cand));
  if (!phrase)
    return kNoop;

  // 获取编码
  string code_str = phrase->entry().custom_code;
  if (code_str.empty())
    code_str = ctx->input().substr(seg.start, seg.end - seg.start);
  if (code_str.empty())
    return kNoop;

  // 从 schema 读取用户词典名和类型
  Config* config = engine_->schema()->config();
  string dict_name;
  config->GetString("translator/dictionary", &dict_name);
  string db_class = "userdb";
  config->GetString("translator/db_class", &db_class);

  auto comp = UserDictionary::Require("user_dictionary");
  if (!comp)
    return kNoop;
  the<UserDictionary> user_dict(comp->Create(dict_name, db_class));
  if (!user_dict)
    return kNoop;
  user_dict->Load();

  DictEntry e;
  e.text = phrase->text();
  e.custom_code = code_str;
  user_dict->UpdateEntry(e, 1, "", /*pin=*/true);

  ctx->RefreshNonConfirmedComposition();
  return kAccepted;
}

}  // namespace rime
