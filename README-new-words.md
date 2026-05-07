# static_user_dict_weights：分离造词功能与动态词频调整

## 背景

librime 的造词（用户词典编码）与动态词频调整（commit 计数、dee 衰减、tick 时间）在 `UserDictionary` 中紧密耦合。每次用户上屏选词时，`TableTranslator::Memorize` 或 `ScriptTranslator::Memorize` 会同时做两件事：

1. 调用 `user_dict_->UpdateEntry(entry, 1)` —— 递增 commit 计数，更新衰减值 dee，推进全局 tick
2. 调用 `encoder_->EncodePhrase()` —— 将新词组编码后存入用户词典

这两条路径都经过 `UpdateEntry`，无法通过现有配置项分离。

对于**形码输入法**（如五笔、郑码、仓颉）用户来说，期望的行为是：

- **造词**：上屏后自动编码新词组，存入用户词典以便下次输入
- **词频**：所有条目保持静态权重（与系统词典行为一致），不因使用频率而自动调整排序

在不修改 librime 的情况下，`enable_user_dict: false` 会完全禁用用户词典；`disable_user_dict_for_patterns` 只能按正则过滤输入。没有配置项能在保留造词的同时冻结词频。

## 设计方案

在 `UserDictionary` 层面增加“静态模式”，通过 `TranslatorOptions` 的 `static_user_dict_weights` 配置项控制。当开启后：

- **新造词组**：仍然通过 encoder 存入用户词典，但 commit/dee/tick 固定为 `1/0/0`（仅标记存在，不做动态权重计算）
- **已有条目**：上屏时跳过 `UpdateEntry` 的调频逻辑，不递增 commit 计数，不更新 dee/tick
- **删除条目**：仍然正常工作（标记为 deleted）
- **权重计算**：`CreateDictEntry` 在静态模式下跳过 `formula_d` / `formula_p` 公式，直接使用 `credibility` 作为固定权重

## 修改文件

### 1. `src/rime/gear/translator_commons.h` / `.cc`

**新增配置项** `translator/static_user_dict_weights`（bool，默认 false）：

- `.h`：在 `TranslatorOptions` 类中增加 `bool static_user_dict_weights_ = false` 字段和 public getter
- `.cc`：在构造函数中从 schema 配置读取

### 2. `src/rime/dict/user_dictionary.h`

- 新增 `bool static_weights_ = false` 字段
- 新增 `set_static_weights(bool)` setter 和 `static_weights()` getter
- 将 `CreateDictEntry` 从 **static** 方法改为**普通成员方法**（需要访问 `static_weights_` 标志）

### 3. `src/rime/dict/user_dictionary.cc`

**`UpdateEntry` 修改** —— 静态模式下三分支处理：

| 场景 | 行为 |
|------|------|
| `commits < 0`（删除） | 正常执行删除，dee/tick 清零 |
| `commits > 0 && v.commits == 0`（新条目） | 创建条目，`commits=1, dee=0, tick=0`，不调用 `UpdateTickCount` |
| `commits > 0 && v.commits != 0`（已有条目） | 直接 `return true`，不做任何更新 |
| `commits == 0`（ScriptTranslator 的衰减路径） | 直接 `return true`，跳过 |

**`CreateDictEntry` 修改** —— 静态模式下跳过动态公式：

```cpp
if (static_weights_) {
    e->weight = credibility;  // 固定权重
} else {
    // 原有的 formula_d + formula_p 计算
}
```

**`DfsState` 结构体** —— 新增 `UserDictionary* user_dict` 字段，供非静态 `CreateDictEntry` 调用。

**`Lookup` / `LookupWords`** —— 更新 `CreateDictEntry` 调用点为成员调用。

### 4. `src/rime/gear/table_translator.cc` 和 `script_translator.cc`

在两个 translator 构造函数末尾，将 `TranslatorOptions` 的 `static_user_dict_weights_` 传递给 `UserDictionary`：

```cpp
if (user_dict_)
    user_dict_->set_static_weights(static_user_dict_weights_);
```

## 配置使用方式

在 schema 的 translator 配置块中添加：

```yaml
translator:
  dictionary: wubi86
  enable_encoder: true           # 开启造词
  encode_commit_history: true    # 从历史上屏编码新词
  max_phrase_length: 5           # 最大词组长度
  static_user_dict_weights: true # 新增：冻结词频，不动态调频
```

## 测试方法

1. 使用 `make debug` 构建 debug 版本，`make test-debug` 运行测试（已通过）
2. 手动测试：
   - 配置一个形码 schema（如五笔），开启 `enable_encoder` 和 `static_user_dict_weights`
   - 输入编码 → 选择候选上屏 → 输入同一编码 → 确认新造词组出现在候选列表中
   - 重复选择同一个用户词组多次 → 确认该词组在候选列表中的排序不因多次选择而上升
   - 使用删除快捷键删除用户词组 → 确认删除功能正常
3. 回归检查：将 `static_user_dict_weights` 设为 `false` 或完全不写该配置项 → 确认词频动态调整行为与修改前一致

## 限制与注意事项

- 静态模式下，用户词典条目的固定权重为 `credibility`（在 TableTranslator 的 `LookupWords` 路径中固定为 1.0），即 `exp(1.0) ≈ 2.72` 的 quality，加上 user phrase bonus `+0.5` 后约为 3.22。这可能使静态用户词条排在部分系统词条之前。
- 如果需要调整静态条目的权重，可以配合 `translator/initial_quality` 使用。
- 该选项对 `ScriptTranslator`（拼音类）同样有效，但 ScriptTranslator 的 `credibility` 是沿音节图路径累积的，值会有所不同。
- 现有用户词典中的条目（在开启本选项之前已存储的）不会被迁移。开启后，旧条目的 commit/dee/tick 值仍会保留在数据库中，但 `CreateDictEntry` 在静态模式下不会使用这些值。
