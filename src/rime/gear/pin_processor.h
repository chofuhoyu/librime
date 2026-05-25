//
// Copyright RIME Developers
// Distributed under the BSD License
//

#ifndef RIME_PIN_PROCESSOR_H_
#define RIME_PIN_PROCESSOR_H_

#include <rime/common.h>
#include <rime/processor.h>

namespace rime {

class PinProcessor : public Processor {
 public:
  explicit PinProcessor(const Ticket& ticket);
  ProcessResult ProcessKeyEvent(const KeyEvent& key_event) override;
};

}  // namespace rime

#endif  // RIME_PIN_PROCESSOR_H_
