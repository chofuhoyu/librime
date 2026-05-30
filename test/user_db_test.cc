//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-07-03 GONG Chen <chen.sst@gmail.com>
//
#include <gtest/gtest.h>
#include <rime/algo/syllabifier.h>
#include <rime/dict/text_db.h>
#include <rime/dict/user_db.h>

using namespace rime;

using TestDb = UserDbWrapper<TextDb>;

TEST(RimeUserDbTest, AccessRecordByKey) {
  TestDb db(path{"user_db_test.txt"}, "user_db_test");
  if (db.Exists())
    db.Remove();
  ASSERT_FALSE(db.Exists());
  db.Open();
  EXPECT_TRUE(db.loaded());
  EXPECT_TRUE(db.Update("abc", "ZYX"));
  EXPECT_TRUE(db.Update("zyx", "CBA"));
  EXPECT_TRUE(db.Update("zyx", "ABC"));
  string value;
  EXPECT_TRUE(db.Fetch("abc", &value));
  EXPECT_EQ("ZYX", value);
  value.clear();
  EXPECT_TRUE(db.Fetch("zyx", &value));
  EXPECT_EQ("ABC", value);
  value.clear();
  EXPECT_FALSE(db.Fetch("wvu", &value));
  EXPECT_TRUE(value.empty());
  value.clear();
  EXPECT_TRUE(db.Erase("zyx"));
  EXPECT_FALSE(db.Fetch("zyx", &value));
  EXPECT_TRUE(value.empty());
  EXPECT_TRUE(db.Close());
  ASSERT_FALSE(db.loaded());
}

TEST(RimeUserDbTest, Query) {
  TestDb db(path{"user_db_test.txt"}, "user_db_test");
  if (db.Exists())
    db.Remove();
  ASSERT_FALSE(db.Exists());
  db.Open();
  EXPECT_TRUE(db.Update("abc", "ZYX"));
  EXPECT_TRUE(db.Update("abc\tdef", "ZYX WVU"));
  EXPECT_TRUE(db.Update("zyx", "ABC"));
  EXPECT_TRUE(db.Update("wvu", "DEF"));
  {
    an<DbAccessor> accessor = db.Query("abc");
    ASSERT_TRUE(bool(accessor));
    EXPECT_FALSE(accessor->exhausted());
    string key, value;
    EXPECT_TRUE(accessor->GetNextRecord(&key, &value));
    EXPECT_EQ("abc", key);
    EXPECT_EQ("ZYX", value);
    key.clear();
    value.clear();
    EXPECT_TRUE(accessor->GetNextRecord(&key, &value));
    EXPECT_EQ("abc\tdef", key);
    EXPECT_EQ("ZYX WVU", value);
    key.clear();
    value.clear();
    EXPECT_FALSE(accessor->GetNextRecord(&key, &value));
    // key, value contain invalid contents
    EXPECT_EQ("", key);
    EXPECT_EQ("", value);
  }
  {
    an<DbAccessor> accessor = db.Query("wvu\tt");
    ASSERT_TRUE(bool(accessor));
    EXPECT_TRUE(accessor->exhausted());
    string key, value;
    EXPECT_FALSE(accessor->GetNextRecord(&key, &value));
  }
  {
    an<DbAccessor> accessor = db.Query("z");
    ASSERT_TRUE(bool(accessor));
    EXPECT_FALSE(accessor->exhausted());
    string key, value;
    EXPECT_TRUE(accessor->GetNextRecord(&key, &value));
    EXPECT_EQ("zyx", key);
    EXPECT_EQ("ABC", value);
    EXPECT_FALSE(accessor->GetNextRecord(&key, &value));
  }
  db.Close();
}

TEST(RimeUserDbTest, SyncPersistsToDisk) {
  TestDb db(path{"user_db_sync_test.txt"}, "user_db_sync_test");
  if (db.Exists())
    db.Remove();
  ASSERT_TRUE(db.Open());
  // userdb format requires key = "code \t text"
  EXPECT_TRUE(db.Update("abc \thello", "c=1 d=0.5 t=0"));
  EXPECT_TRUE(db.Sync());

  // second instance on same file should see the data
  TestDb db2(path{"user_db_sync_test.txt"}, "user_db_sync_test");
  ASSERT_TRUE(db2.OpenReadOnly());
  string value;
  EXPECT_TRUE(db2.Fetch("abc \thello", &value));
  EXPECT_EQ("c=1 d=0.5 t=0", value);
  db2.Close();

  db.Close();
  db.Remove();
}

TEST(RimeUserDbTest, NoSyncDoesNotWrite) {
  TestDb db(path{"user_db_nosync_test.txt"}, "user_db_nosync_test");
  if (db.Exists())
    db.Remove();
  ASSERT_TRUE(db.Open());
  EXPECT_TRUE(db.Update("abc \thello", "c=1 d=0.5 t=0"));
  // NO Sync call — file was never created, so OpenReadOnly will fail
  // verify the file doesn't exist on disk
  EXPECT_FALSE(db.Exists());

  db.Close();
  // Close will save since modified_, so clean up
  db.Remove();
}
