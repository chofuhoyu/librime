//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <gtest/gtest.h>
#include <rime/dict/text_db.h>
#include <rime/dict/user_db.h>
#include <rime/dict/user_dictionary.h>

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
  EXPECT_FALSE(db_->Fetch(key, nullptr));
}
