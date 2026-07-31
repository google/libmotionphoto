// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef TEST_FRAMEWORK_H_
#define TEST_FRAMEWORK_H_

#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

namespace libmotionphoto {
namespace test {

class TestRegistry {
 public:
  struct TestInfo {
    std::string test_case_name;
    std::string test_name;
    void (*test_func)();
  };

  static TestRegistry& GetInstance() {
    static TestRegistry instance;
    return instance;
  }

  void RegisterTest(const std::string& test_case_name, const std::string& test_name, void (*test_func)()) {
    tests_.push_back({test_case_name, test_name, test_func});
  }

  int RunAllTests() {
    int failed = 0;
    for (const auto& test : tests_) {
      std::cout << "[ RUN      ] " << test.test_case_name << "." << test.test_name << std::endl;
      try {
        test.test_func();
        std::cout << "[       OK ] " << test.test_case_name << "." << test.test_name << std::endl;
      } catch (const std::exception& e) {
        std::cerr << "[  FAILED  ] " << test.test_case_name << "." << test.test_name << ": " << e.what() << std::endl;
        failed++;
      } catch (...) {
        std::cerr << "[  FAILED  ] " << test.test_case_name << "." << test.test_name << ": Unknown exception" << std::endl;
        failed++;
      }
    }
    std::cout << "[==========] " << tests_.size() << " tests ran." << std::endl;
    std::cout << "[  PASSED  ] " << (tests_.size() - failed) << " tests." << std::endl;
    if (failed > 0) {
      std::cout << "[  FAILED  ] " << failed << " tests." << std::endl;
    }
    return failed == 0 ? 0 : 1;
  }

 private:
  std::vector<TestInfo> tests_;
};

struct TestRegistrar {
  TestRegistrar(const std::string& test_case_name, const std::string& test_name, void (*test_func)()) {
    TestRegistry::GetInstance().RegisterTest(test_case_name, test_name, test_func);
  }
};

#define TEST(test_case_name, test_name) \
  void test_case_name##_##test_name(); \
  static ::libmotionphoto::test::TestRegistrar test_case_name##_##test_name##_registrar(#test_case_name, #test_name, test_case_name##_##test_name); \
  void test_case_name##_##test_name()

// Helper to allow string conversion for EXPECT_EQ
template <typename T>
std::string ToString(const T& val) {
  return std::to_string(val);
}
inline std::string ToString(const std::string& val) {
  return val;
}
inline std::string ToString(const char* val) {
  return val;
}
inline std::string ToString(bool val) {
  return val ? "true" : "false";
}
template <typename T>
std::string ToString(T* val) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%p", static_cast<const void*>(val));
  return buf;
}
inline std::string ToString(std::nullptr_t) {
  return "nullptr";
}

#define EXPECT_EQ(val1, val2) \
  do { \
    auto v1 = (val1); \
    auto v2 = (val2); \
    if (v1 != v2) { \
      throw std::runtime_error("Expectation failed: " #val1 " == " #val2 " (actual: " + ::libmotionphoto::test::ToString(v1) + " vs " + ::libmotionphoto::test::ToString(v2) + ")"); \
    } \
  } while (0)

#define EXPECT_NE(val1, val2) \
  do { \
    auto v1 = (val1); \
    auto v2 = (val2); \
    if (v1 == v2) { \
      throw std::runtime_error("Expectation failed: " #val1 " != " #val2 " (actual: " + ::libmotionphoto::test::ToString(v1) + " vs " + ::libmotionphoto::test::ToString(v2) + ")"); \
    } \
  } while (0)

#define EXPECT_TRUE(val) \
  do { \
    if (!(val)) { \
      throw std::runtime_error("Expectation failed: " #val " is true"); \
    } \
  } while (0)

#define EXPECT_FALSE(val) \
  do { \
    if (val) { \
      throw std::runtime_error("Expectation failed: " #val " is false"); \
    } \
  } while (0)

#define ASSERT_TRUE EXPECT_TRUE
#define ASSERT_FALSE EXPECT_FALSE
#define ASSERT_EQ EXPECT_EQ
#define ASSERT_NE EXPECT_NE

}  // namespace test
}  // namespace libmotionphoto

#endif  // TEST_FRAMEWORK_H_
