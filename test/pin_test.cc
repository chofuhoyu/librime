//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <gtest/gtest.h>
#include <rime/dict/text_db.h>
#include <rime/dict/user_db.h>
#include <rime/dict/user_dictionary.h>
#include <rime/gear/fixed_position_filter.h>
#include <rime/gear/translator_commons.h>

using namespace rime;

using TestDb = UserDbWrapper<TextDb>;

class PinTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_ = New<TestDb>(path{"pin_test.txt"}, "pin_test");
    if (db_->Exists())
      db_->Remove();
    ASSERT_TRUE(db_->Open());
    ud_.reset(new UserDictionary("pin_test", db_));
    ud_->set_static_weights(true);
    ASSERT_TRUE(ud_->Load());
  }

  void TearDown() override {
    ud_.reset();
    db_->Close();
    db_->Remove();
  }

  an<TestDb> db_;
  the<UserDictionary> ud_;
};

TEST_F(PinTest, PinSetsDeeAndTick) {
  DictEntry e;
  e.text = "测试";
  e.custom_code = "abc ";
  ASSERT_TRUE(ud_->UpdateEntry(e, 1, "", /*pin=*/true));

  string key = "abc \t测试";
  string value;
  ASSERT_TRUE(db_->Fetch(key, &value));

  UserDbValue v(value);
  EXPECT_EQ(v.commits, 1);
  EXPECT_GE(v.dee, 200.0);  // kPinWeight
  EXPECT_GT(v.tick, 0);
}

TEST_F(PinTest, TickIncrements) {
  DictEntry e1;
  e1.text = "第一";
  e1.custom_code = "aa ";
  ASSERT_TRUE(ud_->UpdateEntry(e1, 1, "", /*pin=*/true));

  DictEntry e2;
  e2.text = "第二";
  e2.custom_code = "bb ";
  ASSERT_TRUE(ud_->UpdateEntry(e2, 1, "", /*pin=*/true));

  string key1 = "aa \t第一";
  string key2 = "bb \t第二";
  string v1, v2;
  ASSERT_TRUE(db_->Fetch(key1, &v1));
  ASSERT_TRUE(db_->Fetch(key2, &v2));

  UserDbValue uv1(v1);
  UserDbValue uv2(v2);
  EXPECT_GT(uv2.tick, uv1.tick);
}

TEST_F(PinTest, StaticWeightsComputePinWeight) {
  DictEntry e;
  e.text = "已置顶";
  e.custom_code = "cc ";
  ASSERT_TRUE(ud_->UpdateEntry(e, 1, "", /*pin=*/true));

  string key = "cc \t已置顶";
  string value;
  ASSERT_TRUE(db_->Fetch(key, &value));

  auto entry = ud_->CreateDictEntry(key, value, ud_->tick() + 1);
  ASSERT_TRUE(entry);
  EXPECT_GE(entry->weight, 200.0);
  EXPECT_NEAR(entry->weight, 200.0 + (double)(ud_->tick()) / 1e6, 1e-9);
}

TEST_F(PinTest, StaticWeightsNonPinIsCredibility) {
  // In static_weights mode, new non-pin entries are not recorded by UpdateEntry.
  // Insert a raw entry to simulate a pre-existing word in the user dict.
  string key = "dd \t普通";
  UserDbValue v;
  v.commits = 5;
  v.dee = 0.5;
  v.tick = 2;
  ASSERT_TRUE(db_->Update(key, v.Pack()));

  double credibility = 3.5;
  string value;
  ASSERT_TRUE(db_->Fetch(key, &value));
  auto entry = ud_->CreateDictEntry(key, value, ud_->tick() + 1, credibility);
  ASSERT_TRUE(entry);
  // non-pin entry should use credibility as weight
  EXPECT_DOUBLE_EQ(entry->weight, credibility);
}

TEST_F(PinTest, RepinUpdatesTick) {
  DictEntry e;
  e.text = "重复";
  e.custom_code = "ee ";
  ASSERT_TRUE(ud_->UpdateEntry(e, 1, "", /*pin=*/true));

  string key = "ee \t重复";
  string v1;
  ASSERT_TRUE(db_->Fetch(key, &v1));
  TickCount tick1 = UserDbValue(v1).tick;

  // pin again
  ASSERT_TRUE(ud_->UpdateEntry(e, 1, "", /*pin=*/true));

  string v2;
  ASSERT_TRUE(db_->Fetch(key, &v2));
  TickCount tick2 = UserDbValue(v2).tick;

  EXPECT_GT(tick2, tick1);
}

TEST_F(PinTest, CustomCodeTrailingSpacePreserved) {
  // PinProcessor strips trailing spaces then appends one.
  // UpdateEntry uses custom_code as-is for the key.
  // Test that pinning twice with the same custom_code produces the same key.
  DictEntry e;
  e.text = "空格";
  e.custom_code = "ff ";
  ASSERT_TRUE(ud_->UpdateEntry(e, 1, "", /*pin=*/true));

  string key = e.custom_code + "\t" + e.text;
  string value;
  ASSERT_TRUE(db_->Fetch(key, &value));

  // pin again with same custom_code — key unchanged, value updated
  ASSERT_TRUE(ud_->UpdateEntry(e, 1, "", /*pin=*/true));
  value.clear();
  ASSERT_TRUE(db_->Fetch(key, &value));
  UserDbValue v(value);
  EXPECT_GE(v.tick, 2);
}

TEST_F(PinTest, EraseEntryRemovesRecord) {
  // EraseEntry needs the entry to exist. Insert a raw entry first.
  string key = "gg \t删除";
  UserDbValue v;
  v.commits = 3;
  v.dee = 0.3;
  v.tick = 1;
  ASSERT_TRUE(db_->Update(key, v.Pack()));

  DictEntry e;
  e.text = "删除";
  e.custom_code = "gg ";
  ASSERT_TRUE(ud_->EraseEntry(e));
  string unused;
  EXPECT_FALSE(db_->Fetch(key, &unused));
}

// fixed_position tests: simulate stabledb (read-only custom_phrase) entries

class FixedPositionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Write entries to a temp db, then open it read-only like stabledb does.
    path db_path{"fixed_pos_test.txt"};
    {
      auto write_db = New<TestDb>(db_path, "fixed_pos_test");
      if (write_db->Exists())
        write_db->Remove();
      ASSERT_TRUE(write_db->Open());
      // need "/tick" in metadata so Load() won't fail on read-only open
      ASSERT_TRUE(write_db->CreateMetadata());
      ASSERT_TRUE(write_db->MetaUpdate("/tick", "0"));

      // position 1
      UserDbValue v1;
      v1.commits = 1;
      v1.dee = 0.0;
      v1.tick = 0;
      ASSERT_TRUE(write_db->Update("abc \tfirst", v1.Pack()));

      // position 5
      UserDbValue v5;
      v5.commits = 5;
      v5.dee = 0.0;
      v5.tick = 0;
      ASSERT_TRUE(write_db->Update("abc \tfifth", v5.Pack()));

      write_db->Close();
    }

    db_ = New<TestDb>(db_path, "fixed_pos_test");
    ASSERT_TRUE(db_->OpenReadOnly());
    ud_.reset(new UserDictionary("fixed_pos_test", db_));
    ud_->set_static_weights(true);
    ud_->set_fixed_position(true);
    ASSERT_TRUE(ud_->Load());
  }

  void TearDown() override {
    ud_.reset();
    db_->Close();
    db_->Remove();
  }

  an<TestDb> db_;
  the<UserDictionary> ud_;
};

TEST_F(FixedPositionTest, PositionStoredInCommitCount) {
  string key = "abc \tfirst";
  string value;
  ASSERT_TRUE(db_->Fetch(key, &value));
  auto entry = ud_->CreateDictEntry(key, value, /*present_tick=*/0);
  ASSERT_TRUE(entry);
  // position is stored in commit_count, weight = credibility (default 0)
  EXPECT_EQ(entry->commit_count, 1);
  EXPECT_DOUBLE_EQ(entry->weight, 0.0);
}

TEST_F(FixedPositionTest, DifferentPositionsHaveSameWeight) {
  // Position info is in commit_count, not weight. Both entries get the same weight.
  string key1 = "abc \tfirst";
  string key5 = "abc \tfifth";
  string val1, val5;
  ASSERT_TRUE(db_->Fetch(key1, &val1));
  ASSERT_TRUE(db_->Fetch(key5, &val5));

  auto e1 = ud_->CreateDictEntry(key1, val1, /*present_tick=*/0);
  auto e5 = ud_->CreateDictEntry(key5, val5, /*present_tick=*/0);
  ASSERT_TRUE(e1);
  ASSERT_TRUE(e5);

  EXPECT_EQ(e1->commit_count, 1);
  EXPECT_EQ(e5->commit_count, 5);
  EXPECT_DOUBLE_EQ(e1->weight, e5->weight);  // both = credibility = 0
}

TEST_F(FixedPositionTest, FixedPositionWithoutReadonlyDbUsesNormalWeight) {
  // When the db is NOT readonly, fixed_position has no effect —
  // normal static_weights logic applies. Test with a writable db + pin.
  ud_.reset();
  db_->Close();
  db_->Remove();

  auto write_db = New<TestDb>(path{"fixed_pos_test2.txt"}, "fixed_pos_test2");
  if (write_db->Exists())
    write_db->Remove();
  ASSERT_TRUE(write_db->Open());
  ASSERT_TRUE(write_db->CreateMetadata());

  auto test_ud = the<UserDictionary>(new UserDictionary("fixed_pos_test2", write_db));
  test_ud->set_static_weights(true);
  test_ud->set_fixed_position(true);
  ASSERT_TRUE(test_ud->Load());

  // db opened with Open() is NOT readonly, so fixed_position is inactive.
  // Pin an entry: dee >= kPinWeight, so it follows pin logic.
  DictEntry e;
  e.text = "pinned";
  e.custom_code = "pin ";
  ASSERT_TRUE(test_ud->UpdateEntry(e, 1, "", /*pin=*/true));

  string key = "pin \tpinned";
  string value;
  ASSERT_TRUE(write_db->Fetch(key, &value));

  auto entry = test_ud->CreateDictEntry(key, value, test_ud->tick() + 1);
  ASSERT_TRUE(entry);
  // Pin entry: weight >= 200
  EXPECT_GE(entry->weight, 200.0);

  test_ud.reset();
  write_db->Close();
  write_db->Remove();
}

// FixedPositionFilter unit tests

static an<Candidate> MakeCandidate(const string& type, const string& text,
                                   int commit_count = 0) {
  auto entry = New<DictEntry>();
  entry->text = text;
  entry->commit_count = commit_count;
  return New<Phrase>(nullptr, type, 0, 0, entry);
}

TEST(FixedPositionFilterTest, InsertAtExactPosition) {
  auto fifo = New<FifoTranslation>();
  fifo->Append(MakeCandidate("table", "主码表A"));
  fifo->Append(MakeCandidate("custom_phrase", "表情1", /*position=*/1));
  fifo->Append(MakeCandidate("table", "主码表B"));
  fifo->Append(MakeCandidate("custom_phrase", "表情3", /*position=*/3));

  FixedPositionFilter filter(Ticket{});
  auto result = filter.Apply(fifo, nullptr);

  ASSERT_TRUE(bool(result));
  EXPECT_FALSE(result->exhausted());

  // position 1: 表情1, position 2: 主码表A, position 3: 表情3, position 4: 主码表B
  auto c = result->Peek();
  ASSERT_TRUE(c);
  EXPECT_EQ(c->type(), "custom_phrase");
  EXPECT_EQ(c->text(), "表情1");

  result->Next();
  c = result->Peek();
  ASSERT_TRUE(c);
  EXPECT_EQ(c->text(), "主码表A");

  result->Next();
  c = result->Peek();
  ASSERT_TRUE(c);
  EXPECT_EQ(c->type(), "custom_phrase");
  EXPECT_EQ(c->text(), "表情3");

  result->Next();
  c = result->Peek();
  ASSERT_TRUE(c);
  EXPECT_EQ(c->text(), "主码表B");

  result->Next();
  EXPECT_TRUE(result->exhausted());
}

TEST(FixedPositionFilterTest, NoCustomPhrasePassthrough) {
  auto fifo = New<FifoTranslation>();
  fifo->Append(MakeCandidate("table", "候选1"));
  fifo->Append(MakeCandidate("user_table", "候选2"));
  fifo->Append(MakeCandidate("table", "候选3"));

  FixedPositionFilter filter(Ticket{});
  auto result = filter.Apply(fifo, nullptr);

  ASSERT_TRUE(bool(result));
  // all pass through unchanged
  EXPECT_EQ(result->Peek()->text(), "候选1");
  result->Next();
  EXPECT_EQ(result->Peek()->text(), "候选2");
  result->Next();
  EXPECT_EQ(result->Peek()->text(), "候选3");
  result->Next();
  EXPECT_TRUE(result->exhausted());
}

TEST(FixedPositionFilterTest, PositionBeyondRegularCount) {
  auto fifo = New<FifoTranslation>();
  fifo->Append(MakeCandidate("table", "唯一"));
  fifo->Append(MakeCandidate("custom_phrase", "末尾", /*position=*/99));

  FixedPositionFilter filter(Ticket{});
  auto result = filter.Apply(fifo, nullptr);

  // 唯一 first, then 末尾 (position 99 beyond regular count)
  EXPECT_EQ(result->Peek()->text(), "唯一");
  result->Next();
  EXPECT_EQ(result->Peek()->text(), "末尾");
  result->Next();
  EXPECT_TRUE(result->exhausted());
}

TEST(FixedPositionFilterTest, RawEchoDroppedWhenOtherCandidatesExist) {
  // echo_translator produces "raw" type candidates that are normally
  // suppressed by Compare() when other candidates exist. FixedPositionFilter
  // must replicate this: drop raw entries unless there's nothing else.
  auto fifo = New<FifoTranslation>();
  fifo->Append(MakeCandidate("table", "经互会"));
  fifo->Append(MakeCandidate("custom_phrase", "emoji", /*position=*/2));
  fifo->Append(MakeCandidate("raw", "xgwf"));  // echo

  FixedPositionFilter filter(Ticket{});
  auto result = filter.Apply(fifo, nullptr);

  ASSERT_TRUE(bool(result));
  // pos 1: 经互会, pos 2: emoji, xgwf dropped
  EXPECT_EQ(result->Peek()->text(), "经互会");
  result->Next();
  EXPECT_EQ(result->Peek()->text(), "emoji");
  result->Next();
  EXPECT_TRUE(result->exhausted());
}

TEST(FixedPositionFilterTest, RawEchoShownWhenNothingElse) {
  // When there are no other candidates, raw/echo entries should still appear
  auto fifo = New<FifoTranslation>();
  fifo->Append(MakeCandidate("raw", "hello"));  // only echo, typing English

  FixedPositionFilter filter(Ticket{});
  auto result = filter.Apply(fifo, nullptr);

  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->Peek()->text(), "hello");
  result->Next();
  EXPECT_TRUE(result->exhausted());
}
